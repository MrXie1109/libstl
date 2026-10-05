/*
 * libstl_core.c -- allocator, error handling, iterators, comparators, hashes.
 */

#include "libstl_internal.h"

/* ------------------------------------------------------------------ */
/* Default allocator                                                   */
/* ------------------------------------------------------------------ */

STL_PRIVATE void *stl__default_malloc(size_t size)
{
    if (size == 0) {
        size = 1;
    }
    return malloc(size);
}

STL_PRIVATE void *stl__default_realloc(void *ptr, size_t size)
{
    if (size == 0) {
        size = 1;
    }
    return realloc(ptr, size);
}

STL_PRIVATE void stl__default_free(void *ptr)
{
    if (ptr != NULL) {
        free(ptr);
    }
}

static const stl_allocator stl__default_allocator_instance = {
    stl__default_malloc,
    stl__default_realloc,
    stl__default_free,
    NULL
};

static const stl_allocator *stl__current_allocator = &stl__default_allocator_instance;

const stl_allocator *stl_default_allocator(void)
{
    return &stl__default_allocator_instance;
}

const stl_allocator *stl_set_allocator(const stl_allocator *alloc)
{
    const stl_allocator *previous = stl__current_allocator;

    if (alloc != NULL) {
        if (alloc->malloc_fn == NULL || alloc->free_fn == NULL) {
            stl__set_error_at(STL_ERR_INVALID, __FILE__, __LINE__, "allocator must provide malloc_fn and free_fn");
            return NULL;
        }
        stl__current_allocator = alloc;
    }
    return previous;
}

/* Internal accessor used by every module. */
STL_PRIVATE const stl_allocator *stl__allocator_or_default(const stl_allocator *a)
{
    return (a != NULL) ? a : stl__current_allocator;
}

void *stl_mem_alloc(const stl_allocator *a, size_t count, size_t elem_size)
{
    const stl_allocator *al = stl__allocator_or_default(a);
    size_t bytes;

    if (elem_size == 0) {
        stl__set_error_at(STL_ERR_INVALID, __FILE__, __LINE__, "cannot allocate zero-sized elements");
        return NULL;
    }
    if (count == 0) {
        count = 1;
    }
    if (count > STL_MAX_ELEM_COUNT || elem_size > STL_MAX_ELEM_COUNT / count) {
        stl__set_error_at(STL_ERR_OVERFLOW, __FILE__, __LINE__, "allocation size overflow (%lu * %lu)",
                  (unsigned long)count, (unsigned long)elem_size);
        return NULL;
    }
    bytes = count * elem_size;
    {
        void *p = al->malloc_fn(bytes);
        if (p == NULL) {
            STL_REPORT_NOMEM();
        }
        return p;
    }
}

void *stl_mem_realloc(const stl_allocator *a, void *ptr, size_t count, size_t elem_size)
{
    const stl_allocator *al = stl__allocator_or_default(a);
    size_t bytes;
    void *p;

    if (elem_size == 0) {
        stl__set_error_at(STL_ERR_INVALID, __FILE__, __LINE__, "cannot reallocate zero-sized elements");
        return NULL;
    }
    if (count == 0) {
        count = 1;
    }
    if (count > STL_MAX_ELEM_COUNT || elem_size > STL_MAX_ELEM_COUNT / count) {
        stl__set_error_at(STL_ERR_OVERFLOW, __FILE__, __LINE__, "reallocation size overflow (%lu * %lu)",
                  (unsigned long)count, (unsigned long)elem_size);
        return NULL;
    }
    bytes = count * elem_size;

    if (ptr == NULL) {
        p = al->malloc_fn(bytes);
    } else if (al->realloc_fn != NULL) {
        p = al->realloc_fn(ptr, bytes);
    } else {
        /* No realloc hook: emulate it. */
        p = al->malloc_fn(bytes);
        if (p != NULL) {
            /* We do not know the old size; callers that need the contents
             * preserved use realloc-capable allocators. */
            memcpy(p, ptr, 0);
            al->free_fn(ptr);
        }
    }
    if (p == NULL) {
        STL_REPORT_NOMEM();
    }
    return p;
}

void stl_mem_free(const stl_allocator *a, void *ptr)
{
    const stl_allocator *al = stl__allocator_or_default(a);
    if (ptr != NULL) {
        al->free_fn(ptr);
    }
}

size_t stl_growth_capacity(size_t need)
{
    size_t cap = 8;

    if (need <= 8) {
        return 8;
    }
    if (need > STL_MAX_ELEM_COUNT / 2) {
        return need;
    }
    while (cap < need) {
        size_t next = cap + cap / 2 + 1;   /* 1.5x growth */
        if (next <= cap) {                 /* overflow */
            return need;
        }
        cap = next;
    }
    return cap;
}

/* ------------------------------------------------------------------ */
/* Error handling                                                      */
/* ------------------------------------------------------------------ */

static stl_error_fn stl__error_handler = NULL;
#if STL_HAVE_PTHREAD
static pthread_mutex_t stl__error_mutex = PTHREAD_MUTEX_INITIALIZER;
#  define STL_ERROR_LOCK()   pthread_mutex_lock(&stl__error_mutex)
#  define STL_ERROR_UNLOCK() pthread_mutex_unlock(&stl__error_mutex)
#else
#  define STL_ERROR_LOCK()   ((void)0)
#  define STL_ERROR_UNLOCK() ((void)0)
#endif

static int stl__last_error = STL_OK;

const char *stl_error_string(stl_error_code code)
{
    switch (code) {
    case STL_OK:              return "success";
    case STL_ERR_NOMEM:       return "out of memory";
    case STL_ERR_RANGE:       return "index or iterator out of range";
    case STL_ERR_INVALID:     return "invalid argument";
    case STL_ERR_EMPTY:       return "container is empty";
    case STL_ERR_DUPLICATE:   return "key already exists";
    case STL_ERR_NOT_FOUND:   return "key not found";
    case STL_ERR_TYPE:        return "type or element size mismatch";
    case STL_ERR_STATE:       return "invalid container state";
    case STL_ERR_OVERFLOW:    return "arithmetic or allocation overflow";
    case STL_ERR_UNSUPPORTED: return "operation not supported on this platform";
    default:                  return "unknown error";
    }
}

static void STL_CALL stl__default_error_handler(stl_error_code code, const char *file,
                                                int line, const char *msg)
{
    (void)code;
#if defined(STL_VERBOSE)
    fprintf(stderr, "[libstl] %s:%d: %s (%s)\n",
            (file != NULL) ? file : "?", line,
            (msg != NULL) ? msg : "", stl_error_string(code));
#else
    (void)file;
    (void)line;
    (void)msg;
#endif
}

stl_error_fn stl_set_error_handler(stl_error_fn handler)
{
    stl_error_fn previous;
    STL_ERROR_LOCK();
    previous = stl__error_handler;
    stl__error_handler = handler;
    STL_ERROR_UNLOCK();
    return previous;
}

/* C89-compatible entry point used by the internal reporting macros. */
void stl__set_error_at(stl_error_code code, const char *file, int line,
                       const char *fmt, ...)
{
    char buffer[512];
    va_list ap;

    buffer[0] = '\0';
    va_start(ap, fmt);
    if (fmt != NULL) {
        vsnprintf(buffer, sizeof(buffer), fmt, ap);
    }
    va_end(ap);
    stl_set_error(code, file, line, "%s", buffer);
}

void stl_set_error(stl_error_code code, const char *file, int line, const char *fmt, ...)
{
    char buffer[512];
    va_list ap;

    buffer[0] = '\0';
    va_start(ap, fmt);
    if (fmt != NULL) {
        vsnprintf(buffer, sizeof(buffer), fmt, ap);
    }
    va_end(ap);

    STL_ERROR_LOCK();
    stl__last_error = (int)code;
    {
        stl_error_fn handler = (stl__error_handler != NULL)
                             ? stl__error_handler
                             : stl__default_error_handler;
        handler(code, file, line, buffer);
    }
    STL_ERROR_UNLOCK();
}

int stl_get_error(void)
{
    int code;
    STL_ERROR_LOCK();
    code = stl__last_error;
    stl__last_error = STL_OK;
    STL_ERROR_UNLOCK();
    return code;
}

void stl_clear_error(void)
{
    STL_ERROR_LOCK();
    stl__last_error = STL_OK;
    STL_ERROR_UNLOCK();
}

/* ------------------------------------------------------------------ */
/* swap and raw algorithms                                             */
/* ------------------------------------------------------------------ */

void stl_swap(void *a, void *b, size_t elem_size)
{
    stl_byte *pa = (stl_byte *)a;
    stl_byte *pb = (stl_byte *)b;
    stl_byte tmp[64];

    if (a == NULL || b == NULL || a == b || elem_size == 0) {
        return;
    }
    if (elem_size <= sizeof(tmp)) {
        memcpy(tmp, pa, elem_size);
        memcpy(pa, pb, elem_size);
        memcpy(pb, tmp, elem_size);
        return;
    }
    while (elem_size > 0) {
        size_t chunk = STL_MIN(elem_size, sizeof(tmp));
        memcpy(tmp, pa, chunk);
        memcpy(pa, pb, chunk);
        memcpy(pb, tmp, chunk);
        pa += chunk;
        pb += chunk;
        elem_size -= chunk;
    }
}

void *stl_lower_bound(const void *key, const void *base, size_t count,
                      size_t elem_size, stl_compare_fn cmp)
{
    const stl_byte *lo = (const stl_byte *)base;
    size_t first = 0;
    size_t len = count;

    if (base == NULL || key == NULL || cmp == NULL) {
        return (void *)base;
    }
    while (len > 0) {
        size_t half = len / 2;
        size_t mid = first + half;
        const void *probe = lo + mid * elem_size;
        if (cmp(probe, key) < 0) {
            first = mid + 1;
            len -= half + 1;
        } else {
            len = half;
        }
    }
    return (void *)(lo + first * elem_size);
}

void *stl_upper_bound(const void *key, const void *base, size_t count,
                      size_t elem_size, stl_compare_fn cmp)
{
    const stl_byte *lo = (const stl_byte *)base;
    size_t first = 0;
    size_t len = count;

    if (base == NULL || key == NULL || cmp == NULL) {
        return (void *)base;
    }
    while (len > 0) {
        size_t half = len / 2;
        size_t mid = first + half;
        const void *probe = lo + mid * elem_size;
        if (cmp(key, probe) < 0) {
            len = half;
        } else {
            first = mid + 1;
            len -= half + 1;
        }
    }
    return (void *)(lo + first * elem_size);
}

void *stl_equal_range_lo(const void *key, const void *base, size_t count,
                         size_t elem_size, stl_compare_fn cmp)
{
    return stl_lower_bound(key, base, count, elem_size, cmp);
}

void *stl_equal_range_hi(const void *key, const void *base, size_t count,
                         size_t elem_size, stl_compare_fn cmp)
{
    return stl_upper_bound(key, base, count, elem_size, cmp);
}

int stl_binary_search(const void *key, const void *base, size_t count,
                      size_t elem_size, stl_compare_fn cmp)
{
    const stl_byte *lo = (const stl_byte *)base;
    size_t first = 0;
    size_t len = count;

    if (base == NULL || key == NULL || cmp == NULL) {
        return 0;
    }
    while (len > 0) {
        size_t half = len / 2;
        size_t mid = first + half;
        const void *probe = lo + mid * elem_size;
        int c = cmp(probe, key);
        if (c < 0) {
            first = mid + 1;
            len -= half + 1;
        } else if (c > 0) {
            len = half;
        } else {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Built-in comparators                                                */
/* ------------------------------------------------------------------ */

#define STL_DEFINE_CMP(name, type)                                             \
    int STL_CALL name(const void *a, const void *b)                            \
    {                                                                          \
        type x = *(const type *)a;                                             \
        type y = *(const type *)b;                                             \
        return (x < y) ? STL_LESS : ((x > y) ? STL_GREATER : STL_EQUAL);       \
    }

STL_DEFINE_CMP(stl_cmp_int8, signed char)
STL_DEFINE_CMP(stl_cmp_uint8, unsigned char)
STL_DEFINE_CMP(stl_cmp_int16, short)
STL_DEFINE_CMP(stl_cmp_uint16, unsigned short)
STL_DEFINE_CMP(stl_cmp_int32, int)
STL_DEFINE_CMP(stl_cmp_uint32, unsigned int)
STL_DEFINE_CMP(stl_cmp_long, long)
STL_DEFINE_CMP(stl_cmp_ulong, unsigned long)

#if STL_HAVE_INT64
STL_DEFINE_CMP(stl_cmp_int64, int64_t)
STL_DEFINE_CMP(stl_cmp_uint64, uint64_t)
#else
STL_DEFINE_CMP(stl_cmp_int64, long long)
STL_DEFINE_CMP(stl_cmp_uint64, unsigned long long)
#endif

#undef STL_DEFINE_CMP

int STL_CALL stl_cmp_ptr(const void *a, const void *b)
{
    /* Compares the pointer values stored in the two elements. */
    const void *x = *(const void *const *)a;
    const void *y = *(const void *const *)b;
    if (x == y) return STL_EQUAL;
    return ((const stl_byte *)x < (const stl_byte *)y) ? STL_LESS : STL_GREATER;
}

int STL_CALL stl_cmp_size(const void *a, const void *b)
{
    size_t x = *(const size_t *)a;
    size_t y = *(const size_t *)b;
    return (x < y) ? STL_LESS : ((x > y) ? STL_GREATER : STL_EQUAL);
}

int STL_CALL stl_cmp_float(const void *a, const void *b)
{
    float x = *(const float *)a;
    float y = *(const float *)b;
    if (x < y) return STL_LESS;
    if (x > y) return STL_GREATER;
    return STL_EQUAL;
}

int STL_CALL stl_cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a;
    double y = *(const double *)b;
    if (x < y) return STL_LESS;
    if (x > y) return STL_GREATER;
    return STL_EQUAL;
}

int STL_CALL stl_cmp_double_desc(const void *a, const void *b)
{
    return -stl_cmp_double(a, b);
}

int STL_CALL stl_cmp_cstr(const void *a, const void *b)
{
    const char *x = *(const char *const *)a;
    const char *y = *(const char *const *)b;
    if (x == y) return STL_EQUAL;
    if (x == NULL) return STL_LESS;
    if (y == NULL) return STL_GREATER;
    return strcmp(x, y);
}

int STL_CALL stl_cmp_mem_len(const void *a, const void *b, size_t len)
{
    int c;
    if (len == 0) {
        len = 1;
    }
    c = memcmp(a, b, len);
    return (c < 0) ? STL_LESS : ((c > 0) ? STL_GREATER : STL_EQUAL);
}

int STL_CALL stl_cmp_mem(const void *a, const void *b)
{
    /* Default ordering for containers created without a comparator: compare
     * the leading machine word of each element.  Callers with a meaningful
     * element type should always supply their own comparator. */
    return stl_cmp_mem_len(a, b, sizeof(void *));
}

/* ------------------------------------------------------------------ */
/* Size-generic comparators (used when no comparator was supplied)      */
/* ------------------------------------------------------------------ */

STL_PRIVATE int STL_CALL stl__cmp_u8(const void *a, const void *b)
{
    unsigned char x = *(const unsigned char *)a;
    unsigned char y = *(const unsigned char *)b;
    return (x < y) ? STL_LESS : ((x > y) ? STL_GREATER : STL_EQUAL);
}

STL_PRIVATE int STL_CALL stl__cmp_i32(const void *a, const void *b)
{
    int x = *(const int *)a;
    int y = *(const int *)b;
    return (x < y) ? STL_LESS : ((x > y) ? STL_GREATER : STL_EQUAL);
}

STL_PRIVATE int STL_CALL stl__cmp_i64(const void *a, const void *b)
{
#if STL_HAVE_INT64
    int64_t x = *(const int64_t *)a;
    int64_t y = *(const int64_t *)b;
#else
    long long x = *(const long long *)a;
    long long y = *(const long long *)b;
#endif
    return (x < y) ? STL_LESS : ((x > y) ? STL_GREATER : STL_EQUAL);
}

STL_PRIVATE int STL_CALL stl__cmp_f32(const void *a, const void *b)
{
    return stl_cmp_float(a, b);
}

STL_PRIVATE int STL_CALL stl__cmp_f64(const void *a, const void *b)
{
    return stl_cmp_double(a, b);
}

STL_PRIVATE int STL_CALL stl__cmp_strptr(const void *a, const void *b)
{
    return stl_cmp_cstr(a, b);
}

STL_PRIVATE int STL_CALL stl__eq_strptr(const void *a, const void *b)
{
    return stl_eq_cstr(a, b);
}

STL_PRIVATE size_t STL_CALL stl__hash_strptr(const void *key)
{
    return stl_hash_cstr(key);
}

int STL_CALL stl_eq_int(const void *a, const void *b)
{
    return (*(const int *)a == *(const int *)b);
}

int STL_CALL stl_eq_cstr(const void *a, const void *b)
{
    const char *x = *(const char *const *)a;
    const char *y = *(const char *const *)b;
    if (x == y) return 1;
    if (x == NULL || y == NULL) return 0;
    return strcmp(x, y) == 0;
}

int STL_CALL stl_eq_mem(const void *a, const void *b)
{
    return (a == b) ? 1 : (memcmp(a, b, sizeof(void *)) == 0);
}
int STL_CALL stl_eq_ptr(const void *a, const void *b)
{
    return (*(void *const *)a == *(void *const *)b);
}

/* ------------------------------------------------------------------ */
/* Hashes                                                              */
/* ------------------------------------------------------------------ */

size_t stl_hash_bytes(const void *data, size_t len)
{
    /* FNV-1a, 64-bit on LP64, folded to the platform's size_t width. */
    const unsigned char *p = (const unsigned char *)data;
    size_t i;
#if SIZE_MAX > 0xFFFFFFFFu
    unsigned long long h = 1469598103934665603ULL;
    for (i = 0; i < len; ++i) {
        h ^= (unsigned long long)p[i];
        h *= 1099511628211ULL;
    }
    h ^= (h >> 33);
    return (size_t)h;
#else
    unsigned long h = 2166136261UL;
    for (i = 0; i < len; ++i) {
        h ^= (unsigned long)p[i];
        h *= 16777619UL;
    }
    h ^= (h >> 16);
    return (size_t)h;
#endif
}

STL_PRIVATE size_t stl__hash_bytes_generic(const void *data, size_t len)
{
    return stl_hash_bytes(data, len);
}

size_t STL_CALL stl_hash_cstr(const void *key)
{
    const char *s = *(const char *const *)key;
    if (s == NULL) {
        return 0;
    }
    return stl_hash_bytes(s, strlen(s));
}

size_t STL_CALL stl_hash_mem(const void *key)
{
    /* Key is a NUL-terminated byte string carried as a char[]. */
    const char *s = (const char *)key;
    return stl_hash_bytes(s, strlen(s));
}

size_t STL_CALL stl_hash_int(const void *key)
{
    unsigned int x = (unsigned int)(*(const int *)key);
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return (size_t)x;
}

size_t STL_CALL stl_hash_int64(const void *key)
{
#if STL_HAVE_INT64
    unsigned long long x = (unsigned long long)(*(const int64_t *)key);
#else
    unsigned long long x = (unsigned long long)(*(const long long *)key);
#endif
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return (size_t)x;
}

size_t STL_CALL stl_hash_ptr(const void *key)
{
    size_t x = (size_t)(*(void *const *)key);
    x ^= x >> 33;
    x *= (size_t)0xff51afd7ed558ccdULL;
    x ^= x >> 29;
    return x;
}

/* ------------------------------------------------------------------ */
/* Iterators                                                           */
/* ------------------------------------------------------------------ */

stl_iterator stl_iter_null(void)
{
    stl_iterator it;
    it.elem = NULL;
    it.owner = NULL;
    it.index = 0;
    return it;
}

int stl_iter_is_null(stl_iterator it)
{
    return (it.elem == NULL && it.owner == NULL);
}

void *stl_iter_data(stl_iterator it)
{
    return it.elem;
}

int stl_iter_equal(stl_iterator a, stl_iterator b)
{
    return (a.elem == b.elem) && (a.owner == b.owner);
}

/* ------------------------------------------------------------------ */
/* Version / diagnostics                                               */
/* ------------------------------------------------------------------ */

const char *stl_version_string(void)
{
    return STL_VERSION_STRING;
}

int stl_version(void)
{
    return STL_VERSION;
}

size_t stl_size_of_pointer(void)
{
    return sizeof(void *);
}

/* ------------------------------------------------------------------ */
/* Misc string helpers (declared in the string section but implemented  */
/* here because they are used by the core)                              */
/* ------------------------------------------------------------------ */

int stl_vsnprintf_c(char *buf, size_t cap, const char *fmt, va_list ap)
{
    int n;
    if (buf == NULL || cap == 0) {
        return -1;
    }
    n = vsnprintf(buf, cap, fmt, ap);
    if (n < 0) {
        buf[0] = '\0';
    }
    return n;
}

int stl_snprintf_c(char *buf, size_t cap, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = stl_vsnprintf_c(buf, cap, fmt, ap);
    va_end(ap);
    return n;
}

char *stl_strdup(const char *cstr)
{
    size_t len;
    char *p;
    if (cstr == NULL) {
        return NULL;
    }
    len = strlen(cstr);
    p = (char *)stl_mem_alloc(NULL, len + 1, 1);
    if (p == NULL) {
        return NULL;
    }
    memcpy(p, cstr, len + 1);
    return p;
}

char *stl_strndup(const char *data, size_t len)
{
    char *p;
    if (data == NULL) {
        return NULL;
    }
    p = (char *)stl_mem_alloc(NULL, len + 1, 1);
    if (p == NULL) {
        return NULL;
    }
    memcpy(p, data, len);
    p[len] = '\0';
    return p;
}

int stl_strcasecmp(const char *a, const char *b)
{
    if (a == b) return 0;
    if (a == NULL) return -1;
    if (b == NULL) return 1;
    while (*a != '\0' && *b != '\0') {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) {
            return (ca < cb) ? -1 : 1;
        }
        ++a;
        ++b;
    }
    if (*a == *b) return 0;
    return (*a == '\0') ? -1 : 1;
}

int stl_strncasecmp(const char *a, const char *b, size_t n)
{
    size_t i;
    if (a == b || n == 0) return 0;
    if (a == NULL) return -1;
    if (b == NULL) return 1;
    for (i = 0; i < n; ++i) {
        int ca = tolower((unsigned char)a[i]);
        int cb = tolower((unsigned char)b[i]);
        if (ca != cb) {
            return (ca < cb) ? -1 : 1;
        }
        if (a[i] == '\0') {
            return 0;
        }
    }
    return 0;
}
