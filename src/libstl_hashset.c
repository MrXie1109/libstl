/*
 * libstl_hashset.c -- unordered set / multiset and unordered map / multimap,
 * thin typed layers over stl_hashtable.
 */

#include "libstl_internal.h"

/* Backing-container accessors are declared in libstl_internal.h. */

stl_hashset *stl_hashset_new_policy(size_t elem_size, stl_hash_fn hash, stl_equal_fn eq,
                                    stl_hashtable_policy policy, stl_dtor_fn elem_dtor,
                                    const stl_allocator *a)
{
    /* A hashset element is its own key: key_offset == 0, key_size == elem_size. */
    return stl_hashtable_new(elem_size, 0, elem_size, policy,
                             (hash != NULL) ? hash : stl_hash_mem,
                             (eq != NULL) ? eq : stl_eq_mem,
                             elem_dtor, a);
}

stl_hashset *stl_hashset_new_a(size_t elem_size, stl_hash_fn hash, stl_equal_fn eq,
                               stl_dtor_fn elem_dtor, const stl_allocator *a)
{
    return stl_hashset_new_policy(elem_size, hash, eq, STL_HASHTABLE_UNIQUE, elem_dtor, a);
}

stl_hashset *stl_hashset_new(size_t elem_size, stl_hash_fn hash, stl_equal_fn eq,
                             stl_dtor_fn elem_dtor)
{
    return stl_hashset_new_a(elem_size, hash, eq, elem_dtor, NULL);
}

void stl_hashset_free(stl_hashset *s) { stl_hashtable_free(s); }

stl_hashset *stl_hashset_copy(const stl_hashset *s, stl_copy_fn copy_elem)
{
    return stl_hashtable_copy(s, copy_elem);
}

size_t stl_hashset_size(const stl_hashset *s)  { return stl_hashtable_size(s); }
int    stl_hashset_empty(const stl_hashset *s) { return stl_hashtable_empty(s); }

const void *stl_hashset_insert(stl_hashset *s, const void *elem)
{
    stl_hashtable_node *node;
    size_t before;

    if (s == NULL || elem == NULL) {
        STL_REPORT_INVALID("hashset::insert(NULL)");
        return NULL;
    }
    before = stl_hashtable_size(s);
    node = stl_hashtable_insert(s, elem);
    if (node == NULL) {
        return NULL;
    }
    if (stl__hashtable_policy(s) == STL_HASHTABLE_UNIQUE && stl_hashtable_size(s) == before) {
        return NULL;    /* already present */
    }
    return stl_hashtable_node_data(node);
}

int stl_hashset_erase(stl_hashset *s, const void *elem)  { return stl_hashtable_erase(s, elem); }
void stl_hashset_clear(stl_hashset *s)    { stl_hashtable_clear(s); }
void stl_hashset_clear_ex(stl_hashset *s) { stl_hashtable_clear_ex(s); }

const void *stl_hashset_find(const stl_hashset *s, const void *elem)
{
    const stl_hashtable_node *node = stl_hashtable_find_c(s, elem);
    return (node != NULL) ? stl_hashtable_node_data((stl_hashtable_node *)node) : NULL;
}

size_t stl_hashset_count(const stl_hashset *s, const void *elem)
{
    return stl_hashtable_count(s, elem);
}

int stl_hashset_contains(const stl_hashset *s, const void *elem)
{
    return stl_hashtable_contains(s, elem);
}

int stl_hashset_reserve(stl_hashset *s, size_t count)
{
    return stl_hashtable_reserve(s, count);
}

void stl_hashset_foreach(stl_hashset *s, stl_visit_fn fn, void *user)
{
    stl_hashtable_foreach(s, fn, user);
}

void stl_hashset_foreach_c(const stl_hashset *s, stl_visit_fn fn, void *user)
{
    stl_hashtable_foreach_c(s, fn, user);
}

/* ================================================================== */
/*  hashmap                                                            */
/* ================================================================== */

/* An stl_hashmap owns its table and the layout of the elements in it.  The
 * value offset used to live in a fixed-size registry keyed by table pointer,
 * which capped the number of live maps and was not even synchronised; keeping
 * it in the map removes both problems. */
struct stl_hashmap {
    stl_hashtable *table;
    size_t         value_offset;
    size_t         value_size;
};

static size_t stl__hashmap_value_offset(const stl_hashmap *m)
{
    return (m != NULL) ? m->value_offset : 0;
}

stl_hashmap *stl_hashmap_new_policy(size_t elem_size, size_t value_offset, size_t value_size,
                                    size_t key_size, stl_hash_fn hash, stl_equal_fn key_eq,
                                    stl_hashtable_policy policy, stl_dtor_fn elem_dtor,
                                    const stl_allocator *a)
{
    stl_hashmap *m;

    if (value_size == 0) {
        value_size = elem_size - value_offset;
    }
    if (elem_size == 0 || key_size == 0 || value_size > elem_size - value_offset) {
        STL_REPORT_INVALID("hashmap needs a non-empty key and a value inside the element");
        return NULL;
    }
    m = (stl_hashmap *)stl_mem_alloc(a, 1, sizeof(*m));
    if (m == NULL) {
        return NULL;
    }
    m->table = stl_hashtable_new(elem_size, 0, key_size, policy,
                                 (hash != NULL) ? hash : stl_hash_mem,
                                 (key_eq != NULL) ? key_eq : stl_eq_mem,
                                 elem_dtor, a);
    if (m->table == NULL) {
        stl_mem_free(a, m);
        return NULL;
    }
    m->value_offset = value_offset;
    m->value_size = value_size;
    return m;
}

stl_hashmap *stl_hashmap_new_a(size_t elem_size, size_t value_offset, size_t value_size,
                               size_t key_size, stl_hash_fn hash, stl_equal_fn key_eq,
                               stl_dtor_fn elem_dtor, const stl_allocator *a)
{
    return stl_hashmap_new_policy(elem_size, value_offset, value_size, key_size, hash, key_eq,
                                  STL_HASHTABLE_UNIQUE, elem_dtor, a);
}

stl_hashmap *stl_hashmap_new(size_t elem_size, size_t value_offset, size_t value_size,
                             size_t key_size, stl_hash_fn hash, stl_equal_fn key_eq,
                             stl_dtor_fn elem_dtor)
{
    return stl_hashmap_new_a(elem_size, value_offset, value_size, key_size, hash, key_eq,
                             elem_dtor, NULL);
}

void stl_hashmap_free(stl_hashmap *m)
{
    const stl_allocator *a;

    if (m == NULL) {
        return;
    }
    a = stl__hashtable_allocator(m->table);
    stl_hashtable_free(m->table);
    stl_mem_free(a, m);
}

stl_hashmap *stl_hashmap_copy(const stl_hashmap *m, stl_copy_fn copy_elem)
{
    size_t value_offset = stl__hashmap_value_offset(m);
    stl_hashmap *out;
    stl_hashtable_node *node;

    out = stl_hashmap_new_policy(stl__hashtable_elem_size(m->table), value_offset,
                                 m->value_size, stl__hashtable_key_size(m->table),
                                 stl__hashtable_hash(m->table), stl__hashtable_eq(m->table),
                                 stl__hashtable_policy(m->table), stl__hashtable_dtor(m->table),
                                 stl__hashtable_allocator(m->table));
    if (out == NULL) {
        return NULL;
    }
    for (node = stl_hashtable_first(m->table); node != NULL;
         node = stl_hashtable_next(m->table, node)) {
        stl_hashtable_node *fresh = stl_hashtable_insert(out->table, stl_hashtable_node_data(node));
        if (fresh == NULL) {
            stl_hashmap_free(out);
            return NULL;
        }
        if (copy_elem != NULL &&
            !copy_elem(stl_hashtable_node_data(fresh), stl_hashtable_node_data(node),
                       stl__hashtable_elem_size(m->table))) {
            stl_hashmap_free(out);
            return NULL;
        }
    }
    return out;
}

size_t stl_hashmap_size(const stl_hashmap *m)  { return stl_hashtable_size(m->table); }
int    stl_hashmap_empty(const stl_hashmap *m) { return stl_hashtable_empty(m->table); }

void *stl_hashmap_key_of(const stl_hashmap *m, void *pair)
{
    STL_UNUSED(m);
    return pair;    /* the key is always first in a hashmap element */
}

void *stl_hashmap_value_of(const stl_hashmap *m, void *pair)
{
    if (pair == NULL) {
        return NULL;
    }
    return (void *)((stl_byte *)pair + stl__hashmap_value_offset(m));
}

void *stl_hashmap_find(stl_hashmap *m, const void *key)
{
    stl_hashtable_node *node = stl_hashtable_find(m->table, key);
    return (node != NULL) ? stl_hashtable_node_data(node) : NULL;
}

const void *stl_hashmap_find_c(const stl_hashmap *m, const void *key)
{
    const stl_hashtable_node *node = stl_hashtable_find_c(m->table, key);
    return (node != NULL) ? stl_hashtable_node_data((stl_hashtable_node *)node) : NULL;
}

void *stl_hashmap_get(stl_hashmap *m, const void *key)
{
    void *pair = stl_hashmap_find(m, key);
    return (pair != NULL) ? stl_hashmap_value_of(m, pair) : NULL;
}

const void *stl_hashmap_get_c(const stl_hashmap *m, const void *key)
{
    const void *pair = stl_hashmap_find_c(m, key);
    return (pair != NULL) ? (const void *)((const stl_byte *)pair + stl__hashmap_value_offset(m))
                          : NULL;
}

size_t stl_hashmap_count(const stl_hashmap *m, const void *key) { return stl_hashtable_count(m->table, key); }
int    stl_hashmap_contains(const stl_hashmap *m, const void *key) { return stl_hashtable_contains(m->table, key); }
int    stl_hashmap_reserve(stl_hashmap *m, size_t count) { return stl_hashtable_reserve(m->table, count); }

void *stl_hashmap_insert(stl_hashmap *m, const void *pair)
{
    stl_hashtable_node *node;
    size_t before;

    if (m == NULL || pair == NULL) {
        STL_REPORT_INVALID("hashmap::insert(NULL)");
        return NULL;
    }
    before = stl_hashtable_size(m->table);
    node = stl_hashtable_insert(m->table, pair);
    if (node == NULL) {
        return NULL;
    }
    if (stl__hashtable_policy(m->table) == STL_HASHTABLE_UNIQUE && stl_hashtable_size(m->table) == before) {
        return NULL;
    }
    return stl_hashtable_node_data(node);
}

void *stl_hashmap_put(stl_hashmap *m, const void *key, const void *value)
{
    size_t value_offset = stl__hashmap_value_offset(m);
    size_t elem_size = stl__hashtable_elem_size(m->table);
    size_t key_size = stl__hashtable_key_size(m->table);
    stl_hashtable_node *node;
    void *pair;

    if (m == NULL || key == NULL) {
        STL_REPORT_INVALID("hashmap::put(NULL key)");
        return NULL;
    }
    node = stl_hashtable_find(m->table, key);
    if (node != NULL) {
        pair = stl_hashtable_node_data(node);
        if (value != NULL) {
            memcpy((stl_byte *)pair + value_offset, value, m->value_size);
        } else {
            memset((stl_byte *)pair + value_offset, 0, m->value_size);
        }
        return pair;
    }
    if (elem_size > 4096) {
        void *scratch = stl_mem_alloc(NULL, 1, elem_size);
        if (scratch == NULL) {
            return NULL;
        }
        memset(scratch, 0, elem_size);
        memcpy(scratch, key, key_size);
        if (value != NULL) {
            memcpy((stl_byte *)scratch + value_offset, value, m->value_size);
        }
        node = stl_hashtable_insert(m->table, scratch);
        stl_mem_free(NULL, scratch);
    } else {
        char stack[4096];
        memset(stack, 0, elem_size);
        memcpy(stack, key, key_size);
        if (value != NULL) {
            memcpy(stack + value_offset, value, m->value_size);
        }
        node = stl_hashtable_insert(m->table, stack);
    }
    if (node == NULL) {
        return NULL;
    }
    return stl_hashtable_node_data(node);
}

void *stl_hashmap_get_or_insert(stl_hashmap *m, const void *key, const void *default_value)
{
    void *existing = stl_hashmap_get(m, key);
    void *pair;
    if (existing != NULL) {
        return existing;
    }
    pair = stl_hashmap_put(m, key, default_value);
    return (pair != NULL) ? stl_hashmap_value_of(m, pair) : NULL;
}

int stl_hashmap_erase(stl_hashmap *m, const void *key)  { return stl_hashtable_erase(m->table, key); }
void stl_hashmap_clear(stl_hashmap *m)    { stl_hashtable_clear(m->table); }
void stl_hashmap_clear_ex(stl_hashmap *m) { stl_hashtable_clear_ex(m->table); }

int stl_hashmap_foreach(stl_hashmap *m, stl_visit_fn fn, void *user)
{
    size_t visited = 0;
    stl_hashtable_node *node;

    if (m == NULL || fn == NULL) {
        return 0;
    }
    for (node = stl_hashtable_first(m->table); node != NULL; node = stl_hashtable_next(m->table, node)) {
        ++visited;
        if (fn(stl_hashtable_node_data(node), user)) {
            break;
        }
    }
    return (int)visited;
}

int stl_hashmap_foreach_c(const stl_hashmap *m, stl_visit_fn fn, void *user)
{
    return stl_hashmap_foreach((stl_hashmap *)m, fn, user);
}

stl_iterator stl_hashmap_begin(stl_hashmap *m) { return stl_hashtable_begin(m->table); }
stl_iterator stl_hashmap_end(stl_hashmap *m)   { return stl_hashtable_end(m->table); }
stl_iterator stl_hashmap_iter_next(stl_iterator it) { return stl_hashtable_iter_next(it); }
