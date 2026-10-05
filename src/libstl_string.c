/*
 * libstl_string.c -- std::string counterpart: an owned, NUL-terminated,
 * length-tracked byte buffer.
 */

#include "libstl_internal.h"

struct stl_string {
    unsigned int magic;
    char        *data;      /* always NUL-terminated at data[len] */
    size_t       len;
    size_t       cap;       /* bytes allocated, including the NUL slot */
    const stl_allocator *alloc;
};

#define STL_STRING_MAGIC 0x53u /* 'S' */
#define STL_STRING_SSO   0u    /* no small-string optimisation: keeps ABI simple */

static int stl__string_reserve_impl(stl_string *s, size_t cap)
{
    char *p;
    size_t new_cap;

    if (cap <= s->cap) {
        return STL_OK;
    }
    new_cap = stl_growth_capacity(cap);
    if (new_cap < cap) {
        new_cap = cap;
    }
    p = (char *)stl_mem_realloc(s->alloc, s->data, new_cap, 1);
    if (p == NULL) {
        return STL_ERR_NOMEM;
    }
    s->data = p;
    s->cap = new_cap;
    return STL_OK;
}

static stl_string *stl__string_alloc(const stl_allocator *a)
{
    stl_string *s = (stl_string *)stl_mem_alloc(a, 1, sizeof(*s));
    if (s == NULL) {
        return NULL;
    }
    memset(s, 0, sizeof(*s));
    s->magic = STL_STRING_MAGIC;
    s->alloc = stl__allocator_or_default(a);
    s->data = (char *)stl_mem_alloc(s->alloc, 16, 1);
    if (s->data == NULL) {
        stl_mem_free(s->alloc, s);
        return NULL;
    }
    s->cap = 16;
    s->len = 0;
    s->data[0] = '\0';
    return s;
}

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */

stl_string *stl_string_new_a(const stl_allocator *a) { return stl__string_alloc(a); }
stl_string *stl_string_new(void) { return stl__string_alloc(NULL); }

stl_string *stl_string_new_from(const char *cstr)
{
    stl_string *s = stl__string_alloc(NULL);
    if (s == NULL) {
        return NULL;
    }
    if (cstr != NULL && stl_string_assign(s, cstr) != STL_OK) {
        stl_string_free(s);
        return NULL;
    }
    return s;
}

stl_string *stl_string_new_from_n(const char *data, size_t len)
{
    stl_string *s = stl__string_alloc(NULL);
    if (s == NULL) {
        return NULL;
    }
    if (stl_string_assign_n(s, data, len) != STL_OK) {
        stl_string_free(s);
        return NULL;
    }
    return s;
}

stl_string *stl_string_new_vfmt(const char *fmt, va_list ap)
{
    stl_string *s = stl__string_alloc(NULL);
    if (s == NULL) {
        return NULL;
    }
    if (stl_string_append_vfmt(s, fmt, ap) != STL_OK) {
        stl_string_free(s);
        return NULL;
    }
    return s;
}

stl_string *stl_string_new_fmt(const char *fmt, ...)
{
    stl_string *s;
    va_list ap;
    va_start(ap, fmt);
    s = stl_string_new_vfmt(fmt, ap);
    va_end(ap);
    return s;
}

stl_string *stl_format(const char *fmt, ...)
{
    stl_string *s;
    va_list ap;
    va_start(ap, fmt);
    s = stl_string_new_vfmt(fmt, ap);
    va_end(ap);
    return s;
}

stl_string *stl_string_new_cap(size_t cap)
{
    stl_string *s = stl__string_alloc(NULL);
    if (s == NULL) {
        return NULL;
    }
    if (stl__string_reserve_impl(s, cap + 1) != STL_OK) {
        stl_string_free(s);
        return NULL;
    }
    return s;
}

void stl_string_free(stl_string *s)
{
    if (s == NULL) {
        return;
    }
    STL_CHECK(s->magic == STL_STRING_MAGIC, STL_ERR_INVALID, "not a string");
    stl_mem_free(s->alloc, s->data);
    s->magic = 0;
    stl_mem_free(s->alloc, s);
}

stl_string *stl_string_copy(const stl_string *s)
{
    stl_string *out;
    if (s == NULL) {
        return NULL;
    }
    out = stl__string_alloc(s->alloc);
    if (out == NULL) {
        return NULL;
    }
    if (stl_string_assign_n(out, s->data, s->len) != STL_OK) {
        stl_string_free(out);
        return NULL;
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* Queries                                                             */
/* ------------------------------------------------------------------ */

size_t stl_string_size(const stl_string *s)     { return (s != NULL) ? s->len : 0; }
size_t stl_string_length(const stl_string *s)   { return (s != NULL) ? s->len : 0; }
size_t stl_string_capacity(const stl_string *s) { return (s != NULL) ? s->cap : 0; }
int    stl_string_empty(const stl_string *s)    { return (s == NULL) || (s->len == 0); }
char  *stl_string_cstr(stl_string *s)           { return (s != NULL) ? s->data : NULL; }
const char *stl_string_cstr_c(const stl_string *s) { return (s != NULL) ? s->data : NULL; }
char  *stl_string_data(stl_string *s)           { return (s != NULL) ? s->data : NULL; }
const char *stl_string_data_c(const stl_string *s) { return (s != NULL) ? s->data : NULL; }
const stl_allocator *stl_string_allocator(const stl_string *s) { return (s != NULL) ? s->alloc : NULL; }

char stl_string_at(const stl_string *s, size_t i)
{
    if (s == NULL || i >= s->len) {
        STL_REPORT_RANGE(i, (s != NULL) ? s->len : 0);
        return '\0';
    }
    return s->data[i];
}

char *stl_string_ref(stl_string *s, size_t i)
{
    if (s == NULL || i >= s->len) {
        STL_REPORT_RANGE(i, (s != NULL) ? s->len : 0);
        return NULL;
    }
    return &s->data[i];
}

char stl_string_front(const stl_string *s)
{
    return stl_string_at(s, 0);
}

char stl_string_back(const stl_string *s)
{
    if (s == NULL || s->len == 0) {
        stl__set_error_at(STL_ERR_EMPTY, __FILE__, __LINE__, "string::back on empty string");
        return '\0';
    }
    return s->data[s->len - 1];
}

/* ------------------------------------------------------------------ */
/* Capacity                                                            */
/* ------------------------------------------------------------------ */

int stl_string_reserve(stl_string *s, size_t cap)
{
    if (s == NULL) {
        STL_REPORT_INVALID("NULL string");
        return STL_ERR_INVALID;
    }
    return stl__string_reserve_impl(s, cap + 1);
}

int stl_string_resize(stl_string *s, size_t len)
{
    if (s == NULL) {
        STL_REPORT_INVALID("NULL string");
        return STL_ERR_INVALID;
    }
    if (stl__string_reserve_impl(s, len + 1) != STL_OK) {
        return STL_ERR_NOMEM;
    }
    if (len > s->len) {
        memset(s->data + s->len, 0, len - s->len);
    }
    s->len = len;
    s->data[s->len] = '\0';
    return STL_OK;
}

int stl_string_shrink_to_fit(stl_string *s)
{
    char *p;

    if (s == NULL) {
        STL_REPORT_INVALID("NULL string");
        return STL_ERR_INVALID;
    }
    if (s->cap == s->len + 1) {
        return STL_OK;
    }
    p = (char *)stl_mem_realloc(s->alloc, s->data, s->len + 1, 1);
    if (p == NULL) {
        return STL_ERR_NOMEM;
    }
    s->data = p;
    s->cap = s->len + 1;
    return STL_OK;
}

void stl_string_clear(stl_string *s)
{
    if (s != NULL) {
        s->len = 0;
        s->data[0] = '\0';
    }
}

void stl_string_clear_ex(stl_string *s)
{
    if (s != NULL) {
        memset(s->data, 0, s->len);
        s->len = 0;
        s->data[0] = '\0';
    }
}

/* ------------------------------------------------------------------ */
/* Assignment / append / insert                                        */
/* ------------------------------------------------------------------ */

int stl_string_assign_n(stl_string *s, const char *data, size_t len)
{
    if (s == NULL || (data == NULL && len > 0)) {
        STL_REPORT_INVALID("string::assign(NULL)");
        return STL_ERR_INVALID;
    }
    if (stl__string_reserve_impl(s, len + 1) != STL_OK) {
        return STL_ERR_NOMEM;
    }
    if (len > 0) {
        memmove(s->data, data, len);
    }
    s->len = len;
    s->data[len] = '\0';
    return STL_OK;
}

int stl_string_assign(stl_string *s, const char *cstr)
{
    if (cstr == NULL) {
        stl_string_clear(s);
        return STL_OK;
    }
    return stl_string_assign_n(s, cstr, strlen(cstr));
}

int stl_string_assign_string(stl_string *s, const stl_string *other)
{
    if (other == NULL) {
        stl_string_clear(s);
        return STL_OK;
    }
    if (s == other) {
        return STL_OK;
    }
    return stl_string_assign_n(s, other->data, other->len);
}

int stl_string_append_n(stl_string *s, const char *data, size_t len)
{
    if (s == NULL || (data == NULL && len > 0)) {
        STL_REPORT_INVALID("string::append(NULL)");
        return STL_ERR_INVALID;
    }
    if (len == 0) {
        return STL_OK;
    }
    if (stl__string_reserve_impl(s, s->len + len + 1) != STL_OK) {
        return STL_ERR_NOMEM;
    }
    memmove(s->data + s->len, data, len);
    s->len += len;
    s->data[s->len] = '\0';
    return STL_OK;
}

int stl_string_append(stl_string *s, const char *cstr)
{
    if (cstr == NULL) {
        return STL_OK;
    }
    return stl_string_append_n(s, cstr, strlen(cstr));
}

int stl_string_append_string(stl_string *s, const stl_string *other)
{
    if (other == NULL || other->len == 0) {
        return STL_OK;
    }
    if (s == other) {
        /* Self-append: duplicate the buffer first to avoid aliasing. */
        char *tmp = (char *)stl_mem_alloc(s->alloc, s->len, 1);
        int rc;
        if (tmp == NULL) {
            return STL_ERR_NOMEM;
        }
        memcpy(tmp, s->data, s->len);
        rc = stl_string_append_n(s, tmp, s->len);
        stl_mem_free(s->alloc, tmp);
        return rc;
    }
    return stl_string_append_n(s, other->data, other->len);
}

int stl_string_append_char(stl_string *s, char c)
{
    return stl_string_append_n(s, &c, 1);
}

int stl_string_push_back(stl_string *s, char c)
{
    return stl_string_append_char(s, c);
}

void stl_string_pop_back(stl_string *s)
{
    if (s == NULL || s->len == 0) {
        stl__set_error_at(STL_ERR_EMPTY, __FILE__, __LINE__, "string::pop_back on empty string");
        return;
    }
    s->len -= 1;
    s->data[s->len] = '\0';
}

int stl_string_append_vfmt(stl_string *s, const char *fmt, va_list ap)
{
    va_list copy;
    int needed;
    char stack[512];

    if (s == NULL || fmt == NULL) {
        STL_REPORT_INVALID("string::append_fmt(NULL)");
        return STL_ERR_INVALID;
    }
    stl_va_copy(copy, ap);
    needed = vsnprintf(stack, sizeof(stack), fmt, copy);
    va_end(copy);

    if (needed < 0) {
        return STL_ERR_INVALID;
    }
    if ((size_t)needed < sizeof(stack)) {
        return stl_string_append_n(s, stack, (size_t)needed);
    }
    if (stl__string_reserve_impl(s, s->len + (size_t)needed + 1) != STL_OK) {
        return STL_ERR_NOMEM;
    }
    stl_va_copy(copy, ap);
    vsnprintf(s->data + s->len, (size_t)needed + 1, fmt, copy);
    va_end(copy);
    s->len += (size_t)needed;
    s->data[s->len] = '\0';
    return STL_OK;
}

int stl_string_append_fmt(stl_string *s, const char *fmt, ...)
{
    va_list ap;
    int rc;
    va_start(ap, fmt);
    rc = stl_string_append_vfmt(s, fmt, ap);
    va_end(ap);
    return rc;
}

int stl_string_prepend(stl_string *s, const char *cstr)
{
    return stl_string_insert(s, 0, cstr);
}

int stl_string_insert_n(stl_string *s, size_t pos, const char *data, size_t len)
{
    if (s == NULL || (data == NULL && len > 0)) {
        STL_REPORT_INVALID("string::insert(NULL)");
        return STL_ERR_INVALID;
    }
    if (pos > s->len) {
        STL_REPORT_RANGE(pos, s->len);
        return STL_ERR_RANGE;
    }
    if (len == 0) {
        return STL_OK;
    }
    if (stl__string_reserve_impl(s, s->len + len + 1) != STL_OK) {
        return STL_ERR_NOMEM;
    }
    memmove(s->data + pos + len, s->data + pos, s->len - pos + 1);
    memmove(s->data + pos, data, len);
    s->len += len;
    return STL_OK;
}

int stl_string_insert(stl_string *s, size_t pos, const char *cstr)
{
    if (cstr == NULL) {
        return STL_OK;
    }
    return stl_string_insert_n(s, pos, cstr, strlen(cstr));
}

int stl_string_erase(stl_string *s, size_t pos, size_t len)
{
    if (s == NULL) {
        STL_REPORT_INVALID("NULL string");
        return STL_ERR_INVALID;
    }
    if (pos > s->len) {
        STL_REPORT_RANGE(pos, s->len);
        return STL_ERR_RANGE;
    }
    if (len > s->len - pos) {
        len = s->len - pos;
    }
    memmove(s->data + pos, s->data + pos + len, s->len - pos - len + 1);
    s->len -= len;
    return STL_OK;
}

int stl_string_replace(stl_string *s, size_t pos, size_t len, const char *cstr)
{
    size_t clen = (cstr != NULL) ? strlen(cstr) : 0;

    if (s == NULL) {
        STL_REPORT_INVALID("NULL string");
        return STL_ERR_INVALID;
    }
    if (pos > s->len) {
        STL_REPORT_RANGE(pos, s->len);
        return STL_ERR_RANGE;
    }
    if (len > s->len - pos) {
        len = s->len - pos;
    }
    if (clen == len) {
        if (clen > 0) {
            memmove(s->data + pos, cstr, clen);
        }
        return STL_OK;
    }
    if (clen < len) {
        if (clen > 0) {
            memmove(s->data + pos, cstr, clen);
        }
        memmove(s->data + pos + clen, s->data + pos + len, s->len - pos - len + 1);
        s->len -= (len - clen);
        return STL_OK;
    }
    /* Growing: reserve, then shift the tail and copy. */
    if (stl__string_reserve_impl(s, s->len - len + clen + 1) != STL_OK) {
        return STL_ERR_NOMEM;
    }
    memmove(s->data + pos + clen, s->data + pos + len, s->len - pos - len + 1);
    memmove(s->data + pos, cstr, clen);
    s->len += (clen - len);
    return STL_OK;
}

int stl_string_repeat(stl_string *s, const char *cstr, size_t times)
{
    size_t clen;
    size_t i;

    if (s == NULL || cstr == NULL) {
        STL_REPORT_INVALID("string::repeat(NULL)");
        return STL_ERR_INVALID;
    }
    clen = strlen(cstr);
    if (clen == 0 || times == 0) {
        return STL_OK;
    }
    if (times > STL_MAX_ELEM_COUNT / clen) {
        stl__set_error_at(STL_ERR_OVERFLOW, __FILE__, __LINE__, "string::repeat would overflow");
        return STL_ERR_OVERFLOW;
    }
    if (stl__string_reserve_impl(s, s->len + clen * times + 1) != STL_OK) {
        return STL_ERR_NOMEM;
    }
    for (i = 0; i < times; ++i) {
        memcpy(s->data + s->len, cstr, clen);
        s->len += clen;
    }
    s->data[s->len] = '\0';
    return STL_OK;
}

/* ------------------------------------------------------------------ */
/* Comparison                                                          */
/* ------------------------------------------------------------------ */

ptrdiff_t stl_string_compare(const stl_string *a, const stl_string *b)
{
    size_t n;
    int c;

    if (a == b) return 0;
    if (a == NULL) return -1;
    if (b == NULL) return 1;
    n = (a->len < b->len) ? a->len : b->len;
    c = (n > 0) ? memcmp(a->data, b->data, n) : 0;
    if (c != 0) {
        return (c < 0) ? -1 : 1;
    }
    if (a->len == b->len) return 0;
    return (a->len < b->len) ? -1 : 1;
}

int stl_string_equals(const stl_string *a, const stl_string *b)
{
    if (a == b) return 1;
    if (a == NULL || b == NULL) return 0;
    if (a->len != b->len) return 0;
    return (a->len == 0) || (memcmp(a->data, b->data, a->len) == 0);
}

int stl_string_equals_cstr(const stl_string *a, const char *cstr)
{
    size_t clen;
    if (a == NULL || cstr == NULL) {
        return (a == NULL && cstr == NULL);
    }
    clen = strlen(cstr);
    return (clen == a->len) && (clen == 0 || memcmp(a->data, cstr, clen) == 0);
}

int stl_string_compare_cstr(const stl_string *a, const char *cstr)
{
    if (a == NULL) return (cstr == NULL) ? 0 : -1;
    if (cstr == NULL) return 1;
    return (int)stl_string_compare(a, NULL) + 0 + strcmp(a->data, cstr);
}

/* ------------------------------------------------------------------ */
/* Searching                                                           */
/* ------------------------------------------------------------------ */

size_t stl_string_find(const stl_string *s, const char *needle, size_t pos)
{
    size_t nlen;
    size_t i;

    if (s == NULL || needle == NULL || pos > s->len) {
        return STL_NPOS;
    }
    nlen = strlen(needle);
    if (nlen == 0) {
        return pos;
    }
    if (nlen > s->len) {
        return STL_NPOS;
    }
    for (i = pos; i + nlen <= s->len; ++i) {
        if (s->data[i] == needle[0] && memcmp(s->data + i, needle, nlen) == 0) {
            return i;
        }
    }
    return STL_NPOS;
}

size_t stl_string_rfind(const stl_string *s, const char *needle, size_t pos)
{
    size_t nlen;
    size_t i;

    if (s == NULL || needle == NULL) {
        return STL_NPOS;
    }
    nlen = strlen(needle);
    if (nlen == 0) {
        return (pos < s->len) ? pos : s->len;
    }
    if (nlen > s->len) {
        return STL_NPOS;
    }
    if (pos > s->len - nlen) {
        pos = s->len - nlen;
    }
    i = pos;
    for (;;) {
        if (s->data[i] == needle[0] && memcmp(s->data + i, needle, nlen) == 0) {
            return i;
        }
        if (i == 0) {
            break;
        }
        --i;
    }
    return STL_NPOS;
}

size_t stl_string_find_char(const stl_string *s, char c, size_t pos)
{
    size_t i;
    if (s == NULL || pos >= s->len) {
        return STL_NPOS;
    }
    for (i = pos; i < s->len; ++i) {
        if (s->data[i] == c) {
            return i;
        }
    }
    return STL_NPOS;
}

size_t stl_string_rfind_char(const stl_string *s, char c, size_t pos)
{
    size_t i;
    if (s == NULL || s->len == 0) {
        return STL_NPOS;
    }
    i = (pos < s->len) ? pos : s->len - 1;
    for (;;) {
        if (s->data[i] == c) {
            return i;
        }
        if (i == 0) {
            break;
        }
        --i;
    }
    return STL_NPOS;
}

int stl_string_contains(const stl_string *s, const char *needle)
{
    return stl_string_find(s, needle, 0) != STL_NPOS;
}

int stl_string_starts_with(const stl_string *s, const char *prefix)
{
    size_t plen;
    if (s == NULL || prefix == NULL) {
        return 0;
    }
    plen = strlen(prefix);
    return (plen <= s->len) && (plen == 0 || memcmp(s->data, prefix, plen) == 0);
}

int stl_string_ends_with(const stl_string *s, const char *suffix)
{
    size_t slen;
    if (s == NULL || suffix == NULL) {
        return 0;
    }
    slen = strlen(suffix);
    return (slen <= s->len) &&
           (slen == 0 || memcmp(s->data + (s->len - slen), suffix, slen) == 0);
}

/* ------------------------------------------------------------------ */
/* Transformation                                                      */
/* ------------------------------------------------------------------ */

stl_string *stl_string_substr(const stl_string *s, size_t pos, size_t len)
{
    stl_string *out;

    if (s == NULL) {
        return NULL;
    }
    if (pos > s->len) {
        STL_REPORT_RANGE(pos, s->len);
        return NULL;
    }
    if (len > s->len - pos) {
        len = s->len - pos;
    }
    out = stl__string_alloc(s->alloc);
    if (out == NULL) {
        return NULL;
    }
    if (stl_string_assign_n(out, s->data + pos, len) != STL_OK) {
        stl_string_free(out);
        return NULL;
    }
    return out;
}

int stl_string_substr_into(const stl_string *s, size_t pos, size_t len, stl_string *out)
{
    if (s == NULL || out == NULL) {
        STL_REPORT_INVALID("string::substr into NULL");
        return STL_ERR_INVALID;
    }
    if (pos > s->len) {
        STL_REPORT_RANGE(pos, s->len);
        return STL_ERR_RANGE;
    }
    if (len > s->len - pos) {
        len = s->len - pos;
    }
    return stl_string_assign_n(out, s->data + pos, len);
}

char *stl_string_to_cstr(const stl_string *s)
{
    if (s == NULL) {
        return stl_strdup("");
    }
    return stl_strndup(s->data, s->len);
}

void stl_string_free_cstr(char *p)
{
    stl_mem_free(NULL, p);
}

int stl_string_trim_left(stl_string *s)
{
    size_t i = 0;
    if (s == NULL) {
        return STL_ERR_INVALID;
    }
    while (i < s->len && (s->data[i] == ' ' || s->data[i] == '\t' ||
                          s->data[i] == '\n' || s->data[i] == '\r' ||
                          s->data[i] == '\v' || s->data[i] == '\f')) {
        ++i;
    }
    if (i > 0) {
        memmove(s->data, s->data + i, s->len - i + 1);
        s->len -= i;
    }
    return STL_OK;
}

int stl_string_trim_right(stl_string *s)
{
    if (s == NULL) {
        return STL_ERR_INVALID;
    }
    while (s->len > 0) {
        char c = s->data[s->len - 1];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
            c == '\v' || c == '\f') {
            s->len -= 1;
        } else {
            break;
        }
    }
    s->data[s->len] = '\0';
    return STL_OK;
}

int stl_string_trim(stl_string *s)
{
    (void)stl_string_trim_left(s);
    return stl_string_trim_right(s);
}

int stl_string_toupper(stl_string *s)
{
    size_t i;
    if (s == NULL) {
        return STL_ERR_INVALID;
    }
    for (i = 0; i < s->len; ++i) {
        s->data[i] = (char)toupper((unsigned char)s->data[i]);
    }
    return STL_OK;
}

int stl_string_tolower(stl_string *s)
{
    size_t i;
    if (s == NULL) {
        return STL_ERR_INVALID;
    }
    for (i = 0; i < s->len; ++i) {
        s->data[i] = (char)tolower((unsigned char)s->data[i]);
    }
    return STL_OK;
}

void stl_string_reverse(stl_string *s)
{
    size_t i;
    if (s == NULL) {
        return;
    }
    for (i = 0; i < s->len / 2; ++i) {
        char tmp = s->data[i];
        s->data[i] = s->data[s->len - 1 - i];
        s->data[s->len - 1 - i] = tmp;
    }
}

int stl_string_replace_all(stl_string *s, const char *needle, const char *repl)
{
    size_t nlen, rlen, pos;
    size_t count = 0;

    if (s == NULL || needle == NULL || repl == NULL) {
        STL_REPORT_INVALID("string::replace_all(NULL)");
        return STL_ERR_INVALID;
    }
    nlen = strlen(needle);
    rlen = strlen(repl);
    if (nlen == 0) {
        return STL_OK;
    }
    /* Count first so we can reserve once. */
    pos = stl_string_find(s, needle, 0);
    while (pos != STL_NPOS) {
        ++count;
        pos = stl_string_find(s, needle, pos + nlen);
    }
    if (count == 0) {
        return STL_OK;
    }
    if (rlen > nlen) {
        size_t growth = (rlen - nlen) * count;
        if (stl__string_reserve_impl(s, s->len + growth + 1) != STL_OK) {
            return STL_ERR_NOMEM;
        }
    }
    pos = 0;
    for (;;) {
        size_t at = stl_string_find(s, needle, pos);
        if (at == STL_NPOS) {
            break;
        }
        if (stl_string_replace(s, at, nlen, repl) != STL_OK) {
            return STL_ERR_NOMEM;
        }
        pos = at + rlen;
    }
    return STL_OK;
}

size_t stl_string_split(const stl_string *s, const char *sep, stl_vector *out)
{
    size_t seplen;
    size_t start = 0;
    size_t count = 0;

    if (s == NULL || sep == NULL || out == NULL) {
        return 0;
    }
    if (stl_vector_elem_size(out) != sizeof(stl_string *)) {
        stl__set_error_at(STL_ERR_TYPE, __FILE__, __LINE__, "split output vector must hold stl_string* elements");
        return 0;
    }
    seplen = strlen(sep);
    if (seplen == 0) {
        stl_string *copy = stl_string_copy(s);
        if (copy != NULL && stl_vector_push_back(out, &copy) == STL_OK) {
            return 1;
        }
        stl_string_free(copy);
        return 0;
    }
    for (;;) {
        size_t at = stl_string_find(s, sep, start);
        size_t len = (at == STL_NPOS) ? (s->len - start) : (at - start);
        stl_string *piece = stl_string_new_from_n(s->data + start, len);
        if (piece == NULL || stl_vector_push_back(out, &piece) != STL_OK) {
            stl_string_free(piece);
            return count;
        }
        ++count;
        if (at == STL_NPOS) {
            break;
        }
        start = at + seplen;
    }
    return count;
}

int stl_string_join(stl_string *out, const stl_vector *parts, const char *sep)
{
    size_t i;

    if (out == NULL || parts == NULL) {
        STL_REPORT_INVALID("string::join(NULL)");
        return STL_ERR_INVALID;
    }
    if (stl_vector_elem_size(parts) != sizeof(stl_string *)) {
        stl__set_error_at(STL_ERR_TYPE, __FILE__, __LINE__, "join input vector must hold stl_string* elements");
        return STL_ERR_TYPE;
    }
    stl_string_clear(out);
    for (i = 0; i < stl_vector_size(parts); ++i) {
        stl_string *const *piece = (stl_string *const *)stl_vector_at_c(parts, i);
        if (piece == NULL || *piece == NULL) {
            continue;
        }
        if (i > 0 && sep != NULL && stl_string_append(out, sep) != STL_OK) {
            return STL_ERR_NOMEM;
        }
        if (stl_string_append_string(out, *piece) != STL_OK) {
            return STL_ERR_NOMEM;
        }
    }
    return STL_OK;
}

int stl_string_foreach_token(const char *cstr, const char *sep,
                             int (STL_CALL *fn)(const char *token, size_t len, void *user),
                             void *user)
{
    const char *p;
    size_t seplen;

    if (cstr == NULL || sep == NULL || fn == NULL) {
        return STL_ERR_INVALID;
    }
    seplen = strlen(sep);
    if (seplen == 0) {
        return fn(cstr, strlen(cstr), user) ? 1 : 0;
    }
    p = cstr;
    for (;;) {
        const char *hit = strstr(p, sep);
        size_t len = (hit != NULL) ? (size_t)(hit - p) : strlen(p);
        int stop = fn(p, len, user);
        if (stop || hit == NULL) {
            break;
        }
        p = hit + seplen;
    }
    return STL_OK;
}

/* ------------------------------------------------------------------ */
/* Iterators                                                           */
/* ------------------------------------------------------------------ */

stl_iterator stl_string_begin(stl_string *s)
{
    stl_iterator it;
    it.owner = s;
    it.elem = (s != NULL && s->len > 0) ? &s->data[0] : NULL;
    it.index = 0;
    return it;
}

stl_iterator stl_string_end(stl_string *s)
{
    stl_iterator it;
    it.owner = s;
    it.elem = NULL;
    it.index = (s != NULL) ? s->len : 0;
    return it;
}

stl_iterator stl_string_iter_next(stl_iterator it)
{
    stl_string *s = (stl_string *)it.owner;
    if (s == NULL || it.elem == NULL || it.index + 1 >= s->len) {
        return stl_string_end(s);
    }
    it.elem = &s->data[it.index + 1];
    it.index += 1;
    return it;
}

stl_iterator stl_string_iter_prev(stl_iterator it)
{
    stl_string *s = (stl_string *)it.owner;
    if (s == NULL || s->len == 0) {
        return stl_string_end(s);
    }
    if (it.elem == NULL || it.index == 0) {
        it.elem = &s->data[s->len - 1];
        it.index = s->len - 1;
        return it;
    }
    it.elem = &s->data[it.index - 1];
    it.index -= 1;
    return it;
}
