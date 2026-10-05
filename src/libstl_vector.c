/*
 * libstl_vector.c -- the dynamic array, C++ std::vector's counterpart.
 */

#include "libstl_internal.h"

struct stl_vector {
    unsigned int magic;
    void        *data;          /* element storage (never NULL once created) */
    size_t       size;          /* elements in use                           */
    size_t       capacity;      /* elements allocated                       */
    size_t       elem_size;     /* bytes per element                        */
    size_t       elem_align;    /* for diagnostics / sanity checks          */
    stl_dtor_fn  elem_dtor;     /* may be NULL                              */
    const stl_allocator *alloc;
};

#define STL_VECTOR_MAGIC 0x56u /* 'V' */

static stl_vector *stl__vector_alloc(const stl_allocator *a)
{
    stl_vector *v = (stl_vector *)stl_mem_alloc(a, 1, sizeof(*v));
    if (v == NULL) {
        return NULL;
    }
    memset(v, 0, sizeof(*v));
    v->magic = STL_VECTOR_MAGIC;
    return v;
}

static int stl__vector_init(stl_vector *v, size_t elem_size, size_t cap,
                            stl_dtor_fn elem_dtor, const stl_allocator *a)
{
    if (elem_size == 0) {
        STL_REPORT_INVALID("vector element size must be non-zero");
        return STL_ERR_INVALID;
    }
    v->elem_size = elem_size;
    v->elem_align = sizeof(void *);
    v->elem_dtor = elem_dtor;
    v->alloc = stl__allocator_or_default(a);
    v->size = 0;
    v->capacity = 0;
    v->data = NULL;
    if (cap > 0) {
        void *p = stl_mem_alloc(v->alloc, cap, elem_size);
        if (p == NULL) {
            return STL_ERR_NOMEM;
        }
        v->data = p;
        v->capacity = cap;
    }
    return STL_OK;
}

static int stl__vector_reserve_impl(stl_vector *v, size_t n)
{
    size_t new_cap;
    void *p;

    if (n <= v->capacity) {
        return STL_OK;
    }
    new_cap = (n > stl_growth_capacity(v->capacity)) ? n : stl_growth_capacity(v->capacity);
    p = stl_mem_realloc(v->alloc, v->data, new_cap, v->elem_size);
    if (p == NULL) {
        return STL_ERR_NOMEM;
    }
    v->data = p;
    v->capacity = new_cap;
    return STL_OK;
}

static void stl__vector_destroy_range(stl_vector *v, size_t first, size_t last)
{
    size_t i;
    if (v->elem_dtor == NULL || v->data == NULL) {
        return;
    }
    for (i = first; i < last; ++i) {
        v->elem_dtor(STL_ELEM_AT(v->data, v->elem_size, i));
    }
}

/* ------------------------------------------------------------------ */
/* Construction / destruction                                          */
/* ------------------------------------------------------------------ */

stl_vector *stl_vector_new_a(size_t elem_size, stl_dtor_fn elem_dtor, const stl_allocator *a)
{
    return stl_vector_new_cap(elem_size, 0, elem_dtor, a);
}

stl_vector *stl_vector_new(size_t elem_size, stl_dtor_fn elem_dtor)
{
    return stl_vector_new_cap(elem_size, 0, elem_dtor, NULL);
}

stl_vector *stl_vector_new_cap(size_t elem_size, size_t cap, stl_dtor_fn elem_dtor,
                               const stl_allocator *a)
{
    stl_vector *v = stl__vector_alloc(a);
    if (v == NULL) {
        return NULL;
    }
    if (stl__vector_init(v, elem_size, cap, elem_dtor, a) != STL_OK) {
        stl_mem_free(a, v);
        return NULL;
    }
    return v;
}

stl_vector *stl_vector_from_array(void *data, size_t count, size_t cap,
                                  size_t elem_size, stl_dtor_fn elem_dtor,
                                  const stl_allocator *a)
{
    stl_vector *v = stl__vector_alloc(a);
    if (v == NULL) {
        return NULL;
    }
    if (elem_size == 0) {
        STL_REPORT_INVALID("vector element size must be non-zero");
        stl_mem_free(a, v);
        return NULL;
    }
    v->elem_size = elem_size;
    v->elem_align = sizeof(void *);
    v->elem_dtor = elem_dtor;
    v->alloc = stl__allocator_or_default(a);
    v->data = data;
    v->size = count;
    v->capacity = (cap < count) ? count : cap;
    return v;
}

void stl_vector_free(stl_vector *v)
{
    if (v == NULL) {
        return;
    }
    STL_CHECK(v->magic == STL_VECTOR_MAGIC, STL_ERR_INVALID, "not a vector");
    stl_vector_clear_ex(v);
    stl_mem_free(v->alloc, v->data);
    v->magic = 0;
    stl_mem_free(v->alloc, v);
}

stl_vector *stl_vector_copy_a(const stl_vector *v, stl_copy_fn copy_elem, const stl_allocator *a)
{
    stl_vector *out;
    size_t i;

    if (v == NULL) {
        return NULL;
    }
    out = stl_vector_new_cap(v->elem_size, v->size, NULL, a);
    if (out == NULL) {
        return NULL;
    }
    if (v->size > 0) {
        memcpy(out->data, v->data, v->size * v->elem_size);
    }
    out->size = v->size;
    out->elem_dtor = v->elem_dtor;
    if (copy_elem != NULL) {
        for (i = 0; i < out->size; ++i) {
            if (!copy_elem(STL_ELEM_AT(out->data, out->elem_size, i),
                           STL_ELEM_AT(v->data, v->elem_size, i),
                           v->elem_size)) {
                stl_vector_free(out);
                return NULL;
            }
        }
    }
    return out;
}

stl_vector *stl_vector_copy(const stl_vector *v, stl_copy_fn copy_elem)
{
    return stl_vector_copy_a(v, copy_elem, (v != NULL) ? v->alloc : NULL);
}

/* ------------------------------------------------------------------ */
/* Queries                                                             */
/* ------------------------------------------------------------------ */

size_t stl_vector_size(const stl_vector *v)      { return (v != NULL) ? v->size : 0; }
size_t stl_vector_capacity(const stl_vector *v)  { return (v != NULL) ? v->capacity : 0; }
size_t stl_vector_elem_size(const stl_vector *v) { return (v != NULL) ? v->elem_size : 0; }
int    stl_vector_empty(const stl_vector *v)     { return (v == NULL) || (v->size == 0); }
void  *stl_vector_data(stl_vector *v)            { return (v != NULL) ? v->data : NULL; }
const void *stl_vector_data_c(const stl_vector *v) { return (v != NULL) ? v->data : NULL; }
const stl_allocator *stl_vector_allocator(const stl_vector *v) { return (v != NULL) ? v->alloc : NULL; }

/* ------------------------------------------------------------------ */
/* Capacity                                                            */
/* ------------------------------------------------------------------ */

int stl_vector_reserve(stl_vector *v, size_t n)
{
    if (v == NULL) {
        STL_REPORT_INVALID("NULL vector");
        return STL_ERR_INVALID;
    }
    return stl__vector_reserve_impl(v, n);
}

int stl_vector_resize_v(stl_vector *v, size_t n, const void *value)
{
    if (v == NULL) {
        STL_REPORT_INVALID("NULL vector");
        return STL_ERR_INVALID;
    }
    if (n < v->size) {
        stl__vector_destroy_range(v, n, v->size);
        v->size = n;
        return STL_OK;
    }
    if (n > v->size) {
        size_t old = v->size;
        int rc;
        if (value != NULL) {
            rc = stl__vector_reserve_impl(v, n);
            if (rc != STL_OK) {
                return rc;
            }
            {
                size_t i;
                for (i = old; i < n; ++i) {
                    memcpy(STL_ELEM_AT(v->data, v->elem_size, i), value, v->elem_size);
                }
            }
        } else {
            rc = stl__vector_reserve_impl(v, n);
            if (rc != STL_OK) {
                return rc;
            }
            /* Value-initialise new elements to zero bytes. */
            memset(STL_ELEM_AT(v->data, v->elem_size, old), 0,
                   (n - old) * v->elem_size);
        }
        v->size = n;
    }
    return STL_OK;
}

int stl_vector_resize(stl_vector *v, size_t n)
{
    return stl_vector_resize_v(v, n, NULL);
}

int stl_vector_shrink_to_fit(stl_vector *v)
{
    if (v == NULL) {
        STL_REPORT_INVALID("NULL vector");
        return STL_ERR_INVALID;
    }
    return stl_vector_set_capacity(v, v->size);
}

int stl_vector_set_capacity(stl_vector *v, size_t n)
{
    void *p;
    size_t new_cap;

    if (v == NULL) {
        STL_REPORT_INVALID("NULL vector");
        return STL_ERR_INVALID;
    }
    if (n < v->size) {
        stl__vector_destroy_range(v, n, v->size);
        v->size = n;
    }
    if (n == v->capacity) {
        return STL_OK;
    }
    new_cap = (n == 0) ? 1 : n;
    p = stl_mem_realloc(v->alloc, v->data, new_cap, v->elem_size);
    if (p == NULL) {
        return STL_ERR_NOMEM;
    }
    v->data = p;
    v->capacity = n;
    return STL_OK;
}

/* ------------------------------------------------------------------ */
/* Element access                                                      */
/* ------------------------------------------------------------------ */

void *stl_vector_at(stl_vector *v, size_t i)
{
    if (v == NULL) {
        STL_REPORT_INVALID("NULL vector");
        return NULL;
    }
    if (i >= v->size) {
        STL_REPORT_RANGE(i, v->size);
        return NULL;
    }
    return STL_ELEM_AT(v->data, v->elem_size, i);
}

const void *stl_vector_at_c(const stl_vector *v, size_t i)
{
    if (v == NULL) {
        STL_REPORT_INVALID("NULL vector");
        return NULL;
    }
    if (i >= v->size) {
        STL_REPORT_RANGE(i, v->size);
        return NULL;
    }
    return STL_ELEM_AT(v->data, v->elem_size, i);
}

void *stl_vector_at_checked(stl_vector *v, size_t i, const char *file, int line)
{
    if (v == NULL || i >= v->size) {
        stl_set_error(STL_ERR_RANGE, file, line, "vector::at(%lu) out of range (size %lu)",
                      (unsigned long)i, (unsigned long)((v != NULL) ? v->size : 0));
        return NULL;
    }
    return STL_ELEM_AT(v->data, v->elem_size, i);
}

void *stl_vector_front(stl_vector *v)
{
    if (v == NULL || v->size == 0) {
        stl__set_error_at(STL_ERR_EMPTY, __FILE__, __LINE__, "vector::front on empty vector");
        return NULL;
    }
    return v->data;
}

void *stl_vector_back(stl_vector *v)
{
    if (v == NULL || v->size == 0) {
        stl__set_error_at(STL_ERR_EMPTY, __FILE__, __LINE__, "vector::back on empty vector");
        return NULL;
    }
    return STL_ELEM_AT(v->data, v->elem_size, v->size - 1);
}

void *stl_vector_index(stl_vector *v, ptrdiff_t i)
{
    ptrdiff_t idx = i;
    if (v == NULL) {
        STL_REPORT_INVALID("NULL vector");
        return NULL;
    }
    if (idx < 0) {
        idx += (ptrdiff_t)v->size;
    }
    if (idx < 0 || (size_t)idx >= v->size) {
        stl__set_error_at(STL_ERR_RANGE, __FILE__, __LINE__, "vector::index(%ld) out of range (size %lu)",
                  (long)i, (unsigned long)v->size);
        return NULL;
    }
    return STL_ELEM_AT(v->data, v->elem_size, (size_t)idx);
}

/* ------------------------------------------------------------------ */
/* Modifiers                                                           */
/* ------------------------------------------------------------------ */

int stl_vector_push_back(stl_vector *v, const void *elem)
{
    if (v == NULL || elem == NULL) {
        STL_REPORT_INVALID("vector::push_back(NULL)");
        return STL_ERR_INVALID;
    }
    if (v->size == v->capacity) {
        int rc = stl__vector_reserve_impl(v, v->size + 1);
        if (rc != STL_OK) {
            return rc;
        }
    }
    memcpy(STL_ELEM_AT(v->data, v->elem_size, v->size), elem, v->elem_size);
    v->size += 1;
    return STL_OK;
}

void *stl_vector_emplace(stl_vector *v)
{
    if (v == NULL) {
        STL_REPORT_INVALID("NULL vector");
        return NULL;
    }
    if (v->size == v->capacity) {
        if (stl__vector_reserve_impl(v, v->size + 1) != STL_OK) {
            return NULL;
        }
    }
    {
        void *slot = STL_ELEM_AT(v->data, v->elem_size, v->size);
        memset(slot, 0, v->elem_size);
        v->size += 1;
        return slot;
    }
}

void stl_vector_pop_back(stl_vector *v)
{
    if (v == NULL || v->size == 0) {
        stl__set_error_at(STL_ERR_EMPTY, __FILE__, __LINE__, "vector::pop_back on empty vector");
        return;
    }
    v->size -= 1;
    if (v->elem_dtor != NULL) {
        v->elem_dtor(STL_ELEM_AT(v->data, v->elem_size, v->size));
    }
}

int stl_vector_push_front(stl_vector *v, const void *elem)
{
    return stl_vector_insert(v, 0, elem);
}

void stl_vector_pop_front(stl_vector *v)
{
    (void)stl_vector_erase(v, 0);
}

int stl_vector_insert(stl_vector *v, size_t pos, const void *elem)
{
    return stl_vector_insert_n(v, pos, 1, elem);
}

int stl_vector_insert_n(stl_vector *v, size_t pos, size_t count, const void *elem)
{
    size_t move_count;
    size_t i;
    int rc;

    if (v == NULL || elem == NULL) {
        STL_REPORT_INVALID("vector::insert(NULL)");
        return STL_ERR_INVALID;
    }
    if (pos > v->size) {
        STL_REPORT_RANGE(pos, v->size);
        return STL_ERR_RANGE;
    }
    if (count == 0) {
        return STL_OK;
    }
    rc = stl__vector_reserve_impl(v, v->size + count);
    if (rc != STL_OK) {
        return rc;
    }
    move_count = v->size - pos;
    if (move_count > 0) {
        memmove(STL_ELEM_AT(v->data, v->elem_size, pos + count),
                STL_ELEM_AT(v->data, v->elem_size, pos),
                move_count * v->elem_size);
    }
    for (i = 0; i < count; ++i) {
        memcpy(STL_ELEM_AT(v->data, v->elem_size, pos + i), elem, v->elem_size);
    }
    v->size += count;
    return STL_OK;
}

int stl_vector_insert_array(stl_vector *v, size_t pos, const void *array, size_t count)
{
    size_t move_count;
    int rc;

    if (v == NULL || (array == NULL && count > 0)) {
        STL_REPORT_INVALID("vector::insert_array(NULL)");
        return STL_ERR_INVALID;
    }
    if (pos > v->size) {
        STL_REPORT_RANGE(pos, v->size);
        return STL_ERR_RANGE;
    }
    if (count == 0) {
        return STL_OK;
    }
    rc = stl__vector_reserve_impl(v, v->size + count);
    if (rc != STL_OK) {
        return rc;
    }
    move_count = v->size - pos;
    if (move_count > 0) {
        memmove(STL_ELEM_AT(v->data, v->elem_size, pos + count),
                STL_ELEM_AT(v->data, v->elem_size, pos),
                move_count * v->elem_size);
    }
    memcpy(STL_ELEM_AT(v->data, v->elem_size, pos), array, count * v->elem_size);
    v->size += count;
    return STL_OK;
}

int stl_vector_append_array(stl_vector *v, const void *array, size_t count)
{
    return stl_vector_insert_array(v, stl_vector_size(v), array, count);
}

int stl_vector_erase(stl_vector *v, size_t pos)
{
    return stl_vector_erase_range(v, pos, pos + 1);
}

int stl_vector_erase_range(stl_vector *v, size_t first, size_t last)
{
    size_t move_count;

    if (v == NULL) {
        STL_REPORT_INVALID("NULL vector");
        return STL_ERR_INVALID;
    }
    if (first > last || last > v->size) {
        stl__set_error_at(STL_ERR_RANGE, __FILE__, __LINE__, "vector::erase_range(%lu, %lu) size %lu",
                  (unsigned long)first, (unsigned long)last, (unsigned long)v->size);
        return STL_ERR_RANGE;
    }
    if (first == last) {
        return STL_OK;
    }
    stl__vector_destroy_range(v, first, last);
    move_count = v->size - last;
    if (move_count > 0) {
        memmove(STL_ELEM_AT(v->data, v->elem_size, first),
                STL_ELEM_AT(v->data, v->elem_size, last),
                move_count * v->elem_size);
    }
    v->size -= (last - first);
    return STL_OK;
}

void stl_vector_clear(stl_vector *v)
{
    if (v != NULL) {
        v->size = 0;
    }
}

void stl_vector_clear_ex(stl_vector *v)
{
    if (v != NULL) {
        stl__vector_destroy_range(v, 0, v->size);
        v->size = 0;
    }
}

int stl_vector_assign(stl_vector *v, const void *array, size_t count)
{
    if (v == NULL || (array == NULL && count > 0)) {
        STL_REPORT_INVALID("vector::assign(NULL)");
        return STL_ERR_INVALID;
    }
    stl_vector_clear_ex(v);
    return stl_vector_insert_array(v, 0, array, count);
}

int stl_vector_assign_n(stl_vector *v, size_t count, const void *value)
{
    int rc;
    if (v == NULL || value == NULL) {
        STL_REPORT_INVALID("vector::assign_n(NULL)");
        return STL_ERR_INVALID;
    }
    stl_vector_clear_ex(v);
    if (count == 0) {
        return STL_OK;
    }
    rc = stl__vector_reserve_impl(v, count);
    if (rc != STL_OK) {
        return rc;
    }
    stl_fill(v->data, count, v->elem_size, value);
    v->size = count;
    return STL_OK;
}

/* ------------------------------------------------------------------ */
/* Search                                                              */
/* ------------------------------------------------------------------ */

void *stl_vector_find(const stl_vector *v, const void *elem, stl_equal_fn eq)
{
    if (v == NULL || elem == NULL) {
        return NULL;
    }
    return stl_find(v->data, v->size, v->elem_size, elem, eq);
}

size_t stl_vector_index_of(const stl_vector *v, const void *elem, stl_equal_fn eq)
{
    void *hit;
    if (v == NULL || elem == NULL) {
        return STL_NPOS;
    }
    hit = stl_find(v->data, v->size, v->elem_size, elem, eq);
    if (hit == NULL) {
        return STL_NPOS;
    }
    return (size_t)((stl_byte *)hit - (stl_byte *)v->data) / v->elem_size;
}

void stl_vector_foreach(stl_vector *v, stl_visit_fn fn, void *user)
{
    size_t i;

    if (v == NULL || fn == NULL) {
        return;
    }
    for (i = 0; i < v->size; ++i) {
        if (fn(STL_ELEM_AT(v->data, v->elem_size, i), user)) {
            break;
        }
    }
}

void stl_vector_foreach_c(const stl_vector *v, stl_visit_fn fn, void *user)
{
    stl_vector_foreach((stl_vector *)v, fn, user);
}

/* ------------------------------------------------------------------ */
/* Algorithms                                                          */
/* ------------------------------------------------------------------ */

void stl_vector_sort(stl_vector *v, stl_compare_fn cmp)
{
    if (v == NULL) {
        return;
    }
    stl_sort(v->data, v->size, v->elem_size, (cmp != NULL) ? cmp : stl_cmp_mem);
}

void stl_vector_stable_sort(stl_vector *v, stl_compare_fn cmp)
{
    if (v == NULL) {
        return;
    }
    stl_stable_sort(v->data, v->size, v->elem_size, (cmp != NULL) ? cmp : stl_cmp_mem);
}

void stl_vector_reverse(stl_vector *v)
{
    if (v != NULL) {
        stl__reverse_array(v->data, v->size, v->elem_size);
    }
}

void stl_vector_rotate(stl_vector *v, size_t n)
{
    if (v != NULL) {
        stl__rotate_array(v->data, v->size, n, v->elem_size);
    }
}

void stl_vector_unique(stl_vector *v, stl_equal_fn eq)
{
    void *new_end;
    size_t removed;

    if (v == NULL) {
        return;
    }
    new_end = stl_unique(v->data, v->size, v->elem_size, eq);
    removed = v->size - (size_t)((stl_byte *)new_end - (stl_byte *)v->data) / v->elem_size;
    if (removed > 0) {
        if (v->elem_dtor != NULL) {
            size_t i;
            for (i = 0; i < removed; ++i) {
                v->elem_dtor((stl_byte *)new_end + i * v->elem_size);
            }
        }
        v->size -= removed;
    }
}

size_t stl_vector_remove_if(stl_vector *v, int (STL_CALL *pred)(const void *, void *), void *user)
{
    void *new_end;
    size_t kept, removed;

    if (v == NULL || pred == NULL) {
        return 0;
    }
    new_end = stl_remove_if(v->data, v->size, v->elem_size, pred, user);
    kept = (size_t)((stl_byte *)new_end - (stl_byte *)v->data) / v->elem_size;
    removed = v->size - kept;
    if (removed > 0 && v->elem_dtor != NULL) {
        size_t i;
        for (i = kept; i < v->size; ++i) {
            v->elem_dtor(STL_ELEM_AT(v->data, v->elem_size, i));
        }
    }
    v->size = kept;
    return removed;
}

void stl_vector_fill(stl_vector *v, const void *value)
{
    if (v != NULL && value != NULL) {
        stl_fill(v->data, v->size, v->elem_size, value);
    }
}

size_t stl_vector_lower_bound(const stl_vector *v, const void *key, stl_compare_fn cmp)
{
    void *hit;
    if (v == NULL || key == NULL) {
        return 0;
    }
    hit = stl_lower_bound(key, v->data, v->size, v->elem_size, cmp);
    return (size_t)((stl_byte *)hit - (stl_byte *)v->data) / v->elem_size;
}

size_t stl_vector_upper_bound(const stl_vector *v, const void *key, stl_compare_fn cmp)
{
    void *hit;
    if (v == NULL || key == NULL) {
        return 0;
    }
    hit = stl_upper_bound(key, v->data, v->size, v->elem_size, cmp);
    return (size_t)((stl_byte *)hit - (stl_byte *)v->data) / v->elem_size;
}

int stl_vector_contains(const stl_vector *v, const void *key, stl_compare_fn cmp)
{
    if (v == NULL || key == NULL) {
        return 0;
    }
    return stl_binary_search(key, v->data, v->size, v->elem_size, cmp);
}

/* ------------------------------------------------------------------ */
/* Iterators                                                           */
/* ------------------------------------------------------------------ */

static stl_iterator stl__vector_make_iter(stl_vector *v, size_t index)
{
    stl_iterator it;
    it.owner = v;
    it.index = index;
    it.elem = (v != NULL) ? STL_ELEM_AT(v->data, v->elem_size, index) : NULL;
    return it;
}

stl_iterator stl_vector_begin(stl_vector *v)  { return stl__vector_make_iter(v, 0); }
stl_iterator stl_vector_end(stl_vector *v)    { return stl__vector_make_iter(v, (v != NULL) ? v->size : 0); }
stl_iterator stl_vector_rbegin(stl_vector *v) { return stl__vector_make_iter(v, (v != NULL && v->size > 0) ? v->size - 1 : 0); }
stl_iterator stl_vector_rend(stl_vector *v)
{
    stl_iterator it = stl__vector_make_iter(v, 0);
    it.elem = NULL;
    it.index = (size_t)-1;
    return it;
}

stl_iterator stl_vector_iter_next(stl_iterator it)
{
    stl_vector *v = (stl_vector *)it.owner;
    if (v == NULL || it.index + 1 >= v->size) {
        return stl_vector_end(v);
    }
    return stl__vector_make_iter(v, it.index + 1);
}

stl_iterator stl_vector_iter_prev(stl_iterator it)
{
    stl_vector *v = (stl_vector *)it.owner;
    if (v == NULL) {
        return stl_iter_null();
    }
    if (it.index == 0) {
        return stl__vector_make_iter(v, 0);   /* before-begin clamps to begin */
    }
    if (it.index > v->size) {
        return stl__vector_make_iter(v, v->size);
    }
    return stl__vector_make_iter(v, it.index - 1);
}

ptrdiff_t stl_vector_iter_distance(stl_iterator first, stl_iterator last)
{
    if (first.owner != last.owner) {
        stl__set_error_at(STL_ERR_INVALID, __FILE__, __LINE__, "iterator distance across different containers");
        return 0;
    }
    if (last.index < first.index) {
        return -(ptrdiff_t)(first.index - last.index);
    }
    return (ptrdiff_t)(last.index - first.index);
}
