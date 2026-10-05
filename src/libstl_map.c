/*
 * libstl_map.c -- ordered map / multimap over stl_rbtree.
 *
 * A map element is a user struct { K key; V value; } laid out with the key at
 * key_offset (normally 0) and the value at value_offset.  All entry points
 * work with pointers to whole pairs; the key and value helpers project them.
 */

#include "libstl_internal.h"

/* Layout description of a map element.  The public `stl_map` type is an
 * stl_rbtree, which already knows the element/key sizes and the comparator but
 * has no concept of a "value" -- that is purely a map notion, so it lives here
 * in a side table indexed by tree pointer. */
struct stl_map_meta {
    size_t value_offset;
    size_t key_offset;
    size_t key_size;
    size_t elem_size;
};

/* Because the public type is `stl_map` == `stl_rbtree`, we cannot store the
 * metadata in the object itself without changing the public ABI.  Instead we
 * register it in a small global table.  The table is append-only and protected
 * by a spinlock; lookups scan it by pointer, which is O(number of live maps)
 * -- acceptable for a general-purpose library that favours clarity, and easily
 * replaced by a hash if it ever shows up in a profile. */

#define STL_MAP_REGISTRY_MAX 1024

static struct {
    const stl_rbtree *tree;
    struct stl_map_meta meta;
} stl__map_registry[STL_MAP_REGISTRY_MAX];
static size_t stl__map_registry_count = 0;
static stl_spinlock *stl__map_registry_lock = NULL;

static void stl__map_registry_ensure_lock(void)
{
    if (stl__map_registry_lock == NULL) {
        stl__map_registry_lock = stl_spinlock_new();
    }
}

static int stl__map_register(const stl_rbtree *tree, size_t value_offset,
                             size_t key_offset, size_t key_size, size_t elem_size)
{
    size_t i;

    stl__map_registry_ensure_lock();
    if (stl__map_registry_lock != NULL) {
        stl_spinlock_lock(stl__map_registry_lock);
    }
    for (i = 0; i < stl__map_registry_count; ++i) {
        if (stl__map_registry[i].tree == NULL) {
            stl__map_registry[i].tree = tree;
            stl__map_registry[i].meta.value_offset = value_offset;
            stl__map_registry[i].meta.key_offset = key_offset;
            stl__map_registry[i].meta.key_size = key_size;
            stl__map_registry[i].meta.elem_size = elem_size;
            if (stl__map_registry_lock != NULL) {
                stl_spinlock_unlock(stl__map_registry_lock);
            }
            return STL_OK;
        }
    }
    if (stl__map_registry_count < STL_MAP_REGISTRY_MAX) {
        stl__map_registry[stl__map_registry_count].tree = tree;
        stl__map_registry[stl__map_registry_count].meta.value_offset = value_offset;
        stl__map_registry[stl__map_registry_count].meta.key_offset = key_offset;
        stl__map_registry[stl__map_registry_count].meta.key_size = key_size;
        stl__map_registry[stl__map_registry_count].meta.elem_size = elem_size;
        stl__map_registry_count += 1;
        if (stl__map_registry_lock != NULL) {
            stl_spinlock_unlock(stl__map_registry_lock);
        }
        return STL_OK;
    }
    if (stl__map_registry_lock != NULL) {
        stl_spinlock_unlock(stl__map_registry_lock);
    }
    stl__set_error_at(STL_ERR_NOMEM, __FILE__, __LINE__, "map registry exhausted (%d live maps); free some maps",
              STL_MAP_REGISTRY_MAX);
    return STL_ERR_NOMEM;
}

static void stl__map_unregister(const stl_rbtree *tree)
{
    size_t i;

    stl__map_registry_ensure_lock();
    if (stl__map_registry_lock != NULL) {
        stl_spinlock_lock(stl__map_registry_lock);
    }
    for (i = 0; i < stl__map_registry_count; ++i) {
        if (stl__map_registry[i].tree == tree) {
            stl__map_registry[i].tree = NULL;
            break;
        }
    }
    if (stl__map_registry_lock != NULL) {
        stl_spinlock_unlock(stl__map_registry_lock);
    }
}

static const struct stl_map_meta *stl__map_meta(const stl_map *m)
{
    size_t i;
    const struct stl_map_meta *found = NULL;

    if (m == NULL) {
        return NULL;
    }
    if (stl__map_registry_lock != NULL) {
        stl_spinlock_lock(stl__map_registry_lock);
    }
    for (i = 0; i < stl__map_registry_count; ++i) {
        if (stl__map_registry[i].tree == m) {
            found = &stl__map_registry[i].meta;
            break;
        }
    }
    if (stl__map_registry_lock != NULL) {
        stl_spinlock_unlock(stl__map_registry_lock);
    }
    if (found == NULL) {
        stl__set_error_at(STL_ERR_INVALID, __FILE__, __LINE__, "map metadata missing (was this a map?)");
    }
    return found;
}

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */

stl_map *stl_map_new_policy(size_t elem_size, size_t value_offset, size_t key_size,
                            stl_compare_fn key_cmp, stl_map_policy policy,
                            stl_dtor_fn elem_dtor, const stl_allocator *a)
{
    stl_rbtree *t;

    if (elem_size == 0 || key_size == 0) {
        STL_REPORT_INVALID("map needs non-zero element and key sizes");
        return NULL;
    }
    if (key_size > elem_size || value_offset + 1 > elem_size) {
        stl__set_error_at(STL_ERR_INVALID, __FILE__, __LINE__, "map key/value layout does not fit element size %lu",
                  (unsigned long)elem_size);
        return NULL;
    }
    /* Key offset is always 0 for maps: the pair begins with the key. */
    t = stl_rbtree_new(elem_size, 0, key_size, policy,
                       (key_cmp != NULL) ? key_cmp : stl_cmp_mem, elem_dtor, a);
    if (t == NULL) {
        return NULL;
    }
    if (stl__map_register(t, value_offset, 0, key_size, elem_size) != STL_OK) {
        stl_rbtree_free(t);
        return NULL;
    }
    return t;
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
    if (m == NULL) {
        return;
    }
    stl__map_unregister(m);
    stl_rbtree_free(m);
}

stl_map *stl_map_copy(const stl_map *m, stl_copy_fn copy_elem)
{
    const struct stl_map_meta *meta = stl__map_meta(m);
    stl_map *out;
    stl_rbtree_node *node;

    if (meta == NULL) {
        return NULL;
    }
    out = stl_map_new_policy(meta->elem_size, meta->value_offset, meta->key_size,
                             stl__rbtree_key_cmp(m), stl__rbtree_policy(m),
                             stl__rbtree_dtor(m), stl__rbtree_allocator(m));
    if (out == NULL) {
        return NULL;
    }
    for (node = stl_rbtree_first((stl_rbtree *)m); node != NULL; node = stl_rbtree_next(node)) {
        stl_rbtree_node *fresh = stl_rbtree_insert(out, stl_rbtree_node_data(node));
        if (fresh == NULL) {
            stl_map_free(out);
            return NULL;
        }
        if (copy_elem != NULL &&
            !copy_elem(stl_rbtree_node_data(fresh), stl_rbtree_node_data(node), meta->elem_size)) {
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
    const struct stl_map_meta *meta = stl__map_meta(m);
    if (meta == NULL || pair == NULL) {
        return NULL;
    }
    return (void *)((stl_byte *)pair + meta->key_offset);
}

const void *stl_map_key_of_c(const stl_map *m, const void *pair)
{
    return stl_map_key_of(m, (void *)pair);
}

void *stl_map_value_of(const stl_map *m, void *pair)
{
    const struct stl_map_meta *meta = stl__map_meta(m);
    if (meta == NULL || pair == NULL) {
        return NULL;
    }
    return (void *)((stl_byte *)pair + meta->value_offset);
}

const void *stl_map_value_of_c(const stl_map *m, const void *pair)
{
    return stl_map_value_of(m, (void *)pair);
}

STL_PRIVATE size_t stl__map_value_offset(const stl_map *m)
{
    const struct stl_map_meta *meta = stl__map_meta(m);
    return (meta != NULL) ? meta->value_offset : 0;
}

/* ------------------------------------------------------------------ */
/* Queries                                                             */
/* ------------------------------------------------------------------ */

size_t stl_map_size(const stl_map *m)  { return stl_rbtree_size(m); }
int    stl_map_empty(const stl_map *m) { return stl_rbtree_empty(m); }

void *stl_map_find(stl_map *m, const void *key)
{
    stl_rbtree_node *node = stl_rbtree_find(m, key);
    return (node != NULL) ? stl_rbtree_node_data(node) : NULL;
}

const void *stl_map_find_c(const stl_map *m, const void *key)
{
    const stl_rbtree_node *node = stl_rbtree_find_c(m, key);
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

size_t stl_map_count(const stl_map *m, const void *key) { return stl_rbtree_count(m, key); }
int    stl_map_contains(const stl_map *m, const void *key) { return stl_rbtree_contains(m, key); }

void *stl_map_lower_bound(stl_map *m, const void *key)
{
    stl_rbtree_node *node = stl_rbtree_lower_bound(m, key);
    return (node != NULL) ? stl_rbtree_node_data(node) : NULL;
}

void *stl_map_upper_bound(stl_map *m, const void *key)
{
    stl_rbtree_node *node = stl_rbtree_upper_bound(m, key);
    return (node != NULL) ? stl_rbtree_node_data(node) : NULL;
}

void stl_map_equal_range(stl_map *m, const void *key, void **first, void **last)
{
    stl_rbtree_node *a = NULL, *b = NULL;
    stl_rbtree_equal_range(m, key, &a, &b);
    if (first != NULL) *first = (a != NULL) ? stl_rbtree_node_data(a) : NULL;
    if (last != NULL)  *last = (b != NULL) ? stl_rbtree_node_data(b) : NULL;
}

int stl_map_validate(const stl_map *m)
{
    return stl_rbtree_validate(m);
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
    before = stl_rbtree_size(m);
    node = stl_rbtree_insert(m, pair);
    if (node == NULL) {
        return NULL;
    }
    if (stl__rbtree_policy(m) == STL_RBTREE_UNIQUE && stl_rbtree_size(m) == before) {
        return NULL;    /* duplicate key: element not replaced */
    }
    return stl_rbtree_node_data(node);
}

void *stl_map_put(stl_map *m, const void *key, const void *value)
{
    const struct stl_map_meta *meta = stl__map_meta(m);
    stl_rbtree_node *node;
    void *pair;

    if (meta == NULL || key == NULL) {
        STL_REPORT_INVALID("map::put(NULL key)");
        return NULL;
    }
    node = stl_rbtree_find(m, key);
    if (node != NULL) {
        pair = stl_rbtree_node_data(node);
        if (value != NULL) {
            memcpy((stl_byte *)pair + meta->value_offset, value,
                   meta->elem_size - meta->value_offset);
        } else {
            memset((stl_byte *)pair + meta->value_offset, 0,
                   meta->elem_size - meta->value_offset);
        }
        return pair;
    }
    /* Build the pair on the stack, then insert. */
    if (meta->elem_size > 4096) {
        /* Large elements: build in a heap scratch buffer. */
        void *scratch = stl_mem_alloc(NULL, 1, meta->elem_size);
        if (scratch == NULL) {
            return NULL;
        }
        memset(scratch, 0, meta->elem_size);
        memcpy((stl_byte *)scratch + meta->key_offset, key, meta->key_size);
        if (value != NULL) {
            memcpy((stl_byte *)scratch + meta->value_offset, value,
                   meta->elem_size - meta->value_offset);
        }
        node = stl_rbtree_insert(m, scratch);
        stl_mem_free(NULL, scratch);
    } else {
        char stack[4096];
        memset(stack, 0, meta->elem_size);
        memcpy(stack + meta->key_offset, key, meta->key_size);
        if (value != NULL) {
            memcpy(stack + meta->value_offset, value, meta->elem_size - meta->value_offset);
        }
        node = stl_rbtree_insert(m, stack);
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
    return stl_rbtree_erase(m, key);
}

void stl_map_clear(stl_map *m)    { stl_rbtree_clear(m); }
void stl_map_clear_ex(stl_map *m) { stl_rbtree_clear_ex(m); }

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
    for (node = stl_rbtree_first(m); node != NULL; node = stl_rbtree_next(node)) {
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
    node = (lo != NULL) ? stl_rbtree_lower_bound(m, lo) : stl_rbtree_first(m);
    while (node != NULL) {
        if (hi != NULL) {
            stl_compare_fn cmp = stl__rbtree_key_cmp(m);
            const void *key = (const stl_byte *)stl_rbtree_node_data(node)
                            + stl__rbtree_key_offset(m);
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

stl_iterator stl_map_begin(stl_map *m)  { return stl_rbtree_begin(m); }
stl_iterator stl_map_end(stl_map *m)    { return stl_rbtree_end(m); }
stl_iterator stl_map_rbegin(stl_map *m) { return stl_rbtree_rbegin(m); }
stl_iterator stl_map_rend(stl_map *m)   { return stl_rbtree_rend(m); }
stl_iterator stl_map_iter_next(stl_iterator it) { return stl_rbtree_iter_next(it); }
stl_iterator stl_map_iter_prev(stl_iterator it) { return stl_rbtree_iter_prev(it); }
