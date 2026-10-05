/*
 * libstl_map.c -- ordered map / multimap over stl_rbtree.
 *
 * A map element is a user struct { K key; V value; } laid out with the key at
 * key_offset (normally 0) and the value at value_offset.  All entry points
 * work with pointers to whole pairs; the key and value helpers project them.
 */

#include "libstl_internal.h"

/* An stl_map owns its tree and the layout of the elements stored in it.  Both
 * live in the map itself, so there is no per-map bookkeeping elsewhere and no
 * cap on how many maps a program may have open at once. */
struct stl_map {
    stl_rbtree *tree;
    size_t      value_offset;   /* byte offset of the value inside an element */
    size_t      key_size;       /* byte size of the key, which starts at 0     */
    size_t      elem_size;      /* total element size in bytes                 */
};

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */

stl_map *stl_map_new_policy(size_t elem_size, size_t value_offset, size_t key_size,
                            stl_compare_fn key_cmp, stl_map_policy policy,
                            stl_dtor_fn elem_dtor, const stl_allocator *a)
{
    stl_map *m;

    if (elem_size == 0 || key_size == 0) {
        STL_REPORT_INVALID("map needs non-zero element and key sizes");
        return NULL;
    }
    if (key_size > elem_size || value_offset >= elem_size) {
        stl__set_error_at(STL_ERR_INVALID, __FILE__, __LINE__,
                          "map key/value layout does not fit element size %lu",
                          (unsigned long)elem_size);
        return NULL;
    }

    m = (stl_map *)stl_mem_alloc(a, 1, sizeof(*m));
    if (m == NULL) {
        return NULL;
    }
    /* The key always starts the element, so the tree's key offset is 0. */
    m->tree = stl_rbtree_new(elem_size, 0, key_size, policy,
                             (key_cmp != NULL) ? key_cmp : stl_cmp_mem,
                             elem_dtor, a);
    if (m->tree == NULL) {
        stl_mem_free(a, m);
        return NULL;
    }
    m->value_offset = value_offset;
    m->key_size = key_size;
    m->elem_size = elem_size;
    return m;
}

stl_map *stl_map_new_a(size_t elem_size, size_t value_offset, size_t key_size,
                       stl_compare_fn key_cmp, stl_dtor_fn elem_dtor, const stl_allocator *a)
{
    return stl_map_new_policy(elem_size, value_offset, key_size, key_cmp,
                              STL_MAP_UNIQUE, elem_dtor, a);
}

stl_map *stl_map_new(size_t elem_size, size_t value_offset, size_t key_size,
                     stl_compare_fn key_cmp, stl_dtor_fn elem_dtor)
{
    return stl_map_new_a(elem_size, value_offset, key_size, key_cmp, elem_dtor, NULL);
}

void stl_map_free(stl_map *m)
{
    const stl_allocator *a;

    if (m == NULL) {
        return;
    }
    a = stl__rbtree_allocator(m->tree);
    stl_rbtree_free(m->tree);
    stl_mem_free(a, m);
}

stl_map *stl_map_copy(const stl_map *m, stl_copy_fn copy_elem)
{
    stl_map *out;
    stl_rbtree_node *node;

    if (m == NULL) {
        return NULL;
    }
    out = stl_map_new_policy(m->elem_size, m->value_offset, m->key_size,
                             stl__rbtree_key_cmp(m->tree), stl__rbtree_policy(m->tree),
                             stl__rbtree_dtor(m->tree), stl__rbtree_allocator(m->tree));
    if (out == NULL) {
        return NULL;
    }
    for (node = stl_rbtree_first(m->tree); node != NULL; node = stl_rbtree_next(node)) {
        stl_rbtree_node *fresh = stl_rbtree_insert(out->tree, stl_rbtree_node_data(node));
        if (fresh == NULL) {
            stl_map_free(out);
            return NULL;
        }
        if (copy_elem != NULL &&
            !copy_elem(stl_rbtree_node_data(fresh), stl_rbtree_node_data(node), m->elem_size)) {
            stl_map_free(out);
            return NULL;
        }
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* Key / value projection                                              */
/* ------------------------------------------------------------------ */

void *stl_map_key_of(const stl_map *m, void *pair)
{
    if (m == NULL || pair == NULL) {
        return NULL;
    }
    return (void *)((stl_byte *)pair + 0);
}

const void *stl_map_key_of_c(const stl_map *m, const void *pair)
{
    return stl_map_key_of(m, (void *)pair);
}

void *stl_map_value_of(const stl_map *m, void *pair)
{
    if (m == NULL || pair == NULL) {
        return NULL;
    }
    return (void *)((stl_byte *)pair + m->value_offset);
}

const void *stl_map_value_of_c(const stl_map *m, const void *pair)
{
    return stl_map_value_of(m, (void *)pair);
}

/* ------------------------------------------------------------------ */
/* Queries                                                             */
/* ------------------------------------------------------------------ */

size_t stl_map_size(const stl_map *m)  { return stl_rbtree_size(m->tree); }
int    stl_map_empty(const stl_map *m) { return stl_rbtree_empty(m->tree); }

void *stl_map_find(stl_map *m, const void *key)
{
    stl_rbtree_node *node = stl_rbtree_find(m->tree, key);
    return (node != NULL) ? stl_rbtree_node_data(node) : NULL;
}

const void *stl_map_find_c(const stl_map *m, const void *key)
{
    const stl_rbtree_node *node = stl_rbtree_find_c(m->tree, key);
    return (node != NULL) ? stl_rbtree_node_data((stl_rbtree_node *)node) : NULL;
}

void *stl_map_get(stl_map *m, const void *key)
{
    void *pair = stl_map_find(m, key);
    return (pair != NULL) ? stl_map_value_of(m, pair) : NULL;
}

const void *stl_map_get_c(const stl_map *m, const void *key)
{
    const void *pair = stl_map_find_c(m, key);
    return (pair != NULL) ? stl_map_value_of_c(m, pair) : NULL;
}

void *stl_map_at(stl_map *m, const void *key)
{
    return stl_map_get(m, key);
}

size_t stl_map_count(const stl_map *m, const void *key) { return stl_rbtree_count(m->tree, key); }
int    stl_map_contains(const stl_map *m, const void *key) { return stl_rbtree_contains(m->tree, key); }

void *stl_map_lower_bound(stl_map *m, const void *key)
{
    stl_rbtree_node *node = stl_rbtree_lower_bound(m->tree, key);
    return (node != NULL) ? stl_rbtree_node_data(node) : NULL;
}

void *stl_map_upper_bound(stl_map *m, const void *key)
{
    stl_rbtree_node *node = stl_rbtree_upper_bound(m->tree, key);
    return (node != NULL) ? stl_rbtree_node_data(node) : NULL;
}

void stl_map_equal_range(stl_map *m, const void *key, void **first, void **last)
{
    stl_rbtree_node *a = NULL, *b = NULL;
    stl_rbtree_equal_range(m->tree, key, &a, &b);
    if (first != NULL) *first = (a != NULL) ? stl_rbtree_node_data(a) : NULL;
    if (last != NULL)  *last = (b != NULL) ? stl_rbtree_node_data(b) : NULL;
}

int stl_map_validate(const stl_map *m)
{
    return stl_rbtree_validate(m->tree);
}

/* ------------------------------------------------------------------ */
/* Modifiers                                                           */
/* ------------------------------------------------------------------ */

void *stl_map_insert(stl_map *m, const void *pair)
{
    stl_rbtree_node *node;
    size_t before;

    if (m == NULL || pair == NULL) {
        STL_REPORT_INVALID("map::insert(NULL)");
        return NULL;
    }
    before = stl_rbtree_size(m->tree);
    node = stl_rbtree_insert(m->tree, pair);
    if (node == NULL) {
        return NULL;
    }
    if (stl__rbtree_policy(m->tree) == STL_RBTREE_UNIQUE && stl_rbtree_size(m->tree) == before) {
        return NULL;    /* duplicate key: element not replaced */
    }
    return stl_rbtree_node_data(node);
}

void *stl_map_put(stl_map *m, const void *key, const void *value)
{
    stl_rbtree_node *node;
    void *pair;

    if (m == NULL || key == NULL) {
        STL_REPORT_INVALID("map::put(NULL key)");
        return NULL;
    }
    node = stl_rbtree_find(m->tree, key);
    if (node != NULL) {
        pair = stl_rbtree_node_data(node);
        if (value != NULL) {
            memcpy((stl_byte *)pair + m->value_offset, value,
                   m->elem_size - m->value_offset);
        } else {
            memset((stl_byte *)pair + m->value_offset, 0,
                   m->elem_size - m->value_offset);
        }
        return pair;
    }
    /* Build the pair on the stack, then insert. */
    if (m->elem_size > 4096) {
        /* Large elements: build in a heap scratch buffer. */
        void *scratch = stl_mem_alloc(NULL, 1, m->elem_size);
        if (scratch == NULL) {
            return NULL;
        }
        memset(scratch, 0, m->elem_size);
        memcpy((stl_byte *)scratch + 0, key, m->key_size);
        if (value != NULL) {
            memcpy((stl_byte *)scratch + m->value_offset, value,
                   m->elem_size - m->value_offset);
        }
        node = stl_rbtree_insert(m->tree, scratch);
        stl_mem_free(NULL, scratch);
    } else {
        char stack[4096];
        memset(stack, 0, m->elem_size);
        memcpy(stack + 0, key, m->key_size);
        if (value != NULL) {
            memcpy(stack + m->value_offset, value, m->elem_size - m->value_offset);
        }
        node = stl_rbtree_insert(m->tree, stack);
    }
    if (node == NULL) {
        return NULL;
    }
    pair = stl_rbtree_node_data(node);
    return pair;
}

void *stl_map_get_or_insert(stl_map *m, const void *key, const void *default_value)
{
    void *existing = stl_map_get(m, key);
    void *pair;
    if (existing != NULL) {
        return existing;
    }
    pair = stl_map_put(m, key, default_value);
    return (pair != NULL) ? stl_map_value_of(m, pair) : NULL;
}

int stl_map_erase(stl_map *m, const void *key)
{
    return stl_rbtree_erase(m->tree, key);
}

void stl_map_clear(stl_map *m)    { stl_rbtree_clear(m->tree); }
void stl_map_clear_ex(stl_map *m) { stl_rbtree_clear_ex(m->tree); }

/* ------------------------------------------------------------------ */
/* Iteration                                                           */
/* ------------------------------------------------------------------ */

int stl_map_foreach(stl_map *m, stl_visit_fn fn, void *user)
{
    stl_rbtree_node *node;
    size_t visited = 0;

    if (m == NULL || fn == NULL) {
        return 0;
    }
    for (node = stl_rbtree_first(m->tree); node != NULL; node = stl_rbtree_next(node)) {
        ++visited;
        if (fn(stl_rbtree_node_data(node), user)) {
            break;
        }
    }
    return (int)visited;
}

int stl_map_foreach_c(const stl_map *m, stl_visit_fn fn, void *user)
{
    return stl_map_foreach((stl_map *)m, fn, user);
}

int stl_map_foreach_range(stl_map *m, const void *lo, const void *hi,
                          stl_visit_fn fn, void *user)
{
    stl_rbtree_node *node;
    size_t visited = 0;

    if (m == NULL || fn == NULL) {
        return 0;
    }
    node = (lo != NULL) ? stl_rbtree_lower_bound(m->tree, lo) : stl_rbtree_first(m->tree);
    while (node != NULL) {
        if (hi != NULL) {
            stl_compare_fn cmp = stl__rbtree_key_cmp(m->tree);
            const void *key = (const stl_byte *)stl_rbtree_node_data(node)
                            + stl__rbtree_key_offset(m->tree);
            if (cmp(key, hi) >= 0) {
                break;
            }
        }
        ++visited;
        if (fn(stl_rbtree_node_data(node), user)) {
            break;
        }
        node = stl_rbtree_next(node);
    }
    return (int)visited;
}

stl_iterator stl_map_begin(stl_map *m)  { return stl_rbtree_begin(m->tree); }
stl_iterator stl_map_end(stl_map *m)    { return stl_rbtree_end(m->tree); }
stl_iterator stl_map_rbegin(stl_map *m) { return stl_rbtree_rbegin(m->tree); }
stl_iterator stl_map_rend(stl_map *m)   { return stl_rbtree_rend(m->tree); }
stl_iterator stl_map_iter_next(stl_iterator it) { return stl_rbtree_iter_next(it); }
stl_iterator stl_map_iter_prev(stl_iterator it) { return stl_rbtree_iter_prev(it); }
