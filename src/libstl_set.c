/*
 * libstl_set.c -- ordered set / multiset, a thin typed layer over stl_rbtree.
 *
 * A set element is the value itself (key_offset == 0, key_size == elem_size).
 */

#include "libstl_internal.h"

/* Backing-container accessors are declared in libstl_internal.h. */

stl_set *stl_set_new_policy(size_t elem_size, stl_compare_fn cmp, stl_set_policy policy,
                            stl_dtor_fn elem_dtor, const stl_allocator *a)
{
    /* A set element carries its own key: key_offset == 0, key_size == elem_size. */
    return stl_rbtree_new(elem_size, 0, elem_size, policy,
                          (cmp != NULL) ? cmp : stl_cmp_mem, elem_dtor, a);
}

stl_set *stl_set_new_a(size_t elem_size, stl_compare_fn cmp, stl_dtor_fn elem_dtor,
                       const stl_allocator *a)
{
    return stl_set_new_policy(elem_size, cmp, STL_SET_UNIQUE, elem_dtor, a);
}

stl_set *stl_set_new(size_t elem_size, stl_compare_fn cmp, stl_dtor_fn elem_dtor)
{
    return stl_set_new_a(elem_size, cmp, elem_dtor, NULL);
}

stl_set *stl_set_from_array(const void *array, size_t count, size_t elem_size,
                            stl_compare_fn cmp, stl_set_policy policy, const stl_allocator *a)
{
    stl_set *s = stl_set_new_policy(elem_size, cmp, policy, NULL, a);
    size_t i;

    if (s == NULL) {
        return NULL;
    }
    for (i = 0; i < count; ++i) {
        if (stl_rbtree_insert(s, STL_ELEM_AT(array, elem_size, i)) == NULL) {
            stl_set_free(s);
            return NULL;
        }
    }
    return s;
}

void stl_set_free(stl_set *s)
{
    stl_rbtree_free(s);
}

stl_set *stl_set_copy(const stl_set *s, stl_copy_fn copy_elem)
{
    return stl_rbtree_copy(s, copy_elem);
}

/* ------------------------------------------------------------------ */
/* Queries / modifiers                                                 */
/* ------------------------------------------------------------------ */

size_t stl_set_size(const stl_set *s)  { return stl_rbtree_size(s); }
int    stl_set_empty(const stl_set *s) { return stl_rbtree_empty(s); }

const void *stl_set_insert(stl_set *s, const void *elem)
{
    stl_rbtree_node *node;
    size_t before;

    if (s == NULL || elem == NULL) {
        STL_REPORT_INVALID("set::insert(NULL)");
        return NULL;
    }
    before = stl_rbtree_size(s);
    node = stl_rbtree_insert(s, elem);
    if (node == NULL) {
        return NULL;
    }
    /* For a unique set, a duplicate insert returns the existing node. */
    if (stl__rbtree_policy(s) == STL_RBTREE_UNIQUE && stl_rbtree_size(s) == before) {
        return NULL;
    }
    return stl_rbtree_node_data(node);
}

int stl_set_erase(stl_set *s, const void *elem)
{
    return stl_rbtree_erase(s, elem);
}

void stl_set_clear(stl_set *s)    { stl_rbtree_clear(s); }
void stl_set_clear_ex(stl_set *s) { stl_rbtree_clear_ex(s); }

const void *stl_set_find(const stl_set *s, const void *elem)
{
    const stl_rbtree_node *node = stl_rbtree_find_c(s, elem);
    return (node != NULL) ? stl_rbtree_node_data((stl_rbtree_node *)node) : NULL;
}

size_t stl_set_count(const stl_set *s, const void *elem)
{
    return stl_rbtree_count(s, elem);
}

int stl_set_contains(const stl_set *s, const void *elem)
{
    return stl_rbtree_contains(s, elem);
}

const void *stl_set_lower_bound(const stl_set *s, const void *elem)
{
    stl_rbtree_node *node = stl_rbtree_lower_bound((stl_rbtree *)s, elem);
    return (node != NULL) ? stl_rbtree_node_data(node) : NULL;
}

const void *stl_set_upper_bound(const stl_set *s, const void *elem)
{
    stl_rbtree_node *node = stl_rbtree_upper_bound((stl_rbtree *)s, elem);
    return (node != NULL) ? stl_rbtree_node_data(node) : NULL;
}

int stl_set_validate(const stl_set *s)
{
    return stl_rbtree_validate(s);
}

/* ------------------------------------------------------------------ */
/* Set algebra                                                         */
/* ------------------------------------------------------------------ */

static stl_set *stl__set_combine(const stl_set *a, const stl_set *b, stl_copy_fn copy_elem,
                                 int op)
{
    /* op: 0 = union, 1 = intersection, 2 = difference, 3 = symmetric difference */
    stl_set *out;
    stl_rbtree_node *na;
    stl_rbtree_node *nb;

    if (a == NULL || b == NULL) {
        return NULL;
    }
    if (stl_rbtree_elem_size(a) != stl_rbtree_elem_size(b)) {
        stl__set_error_at(STL_ERR_TYPE, __FILE__, __LINE__, "set algebra requires equal element sizes");
        return NULL;
    }
    out = stl_set_new_policy(stl_rbtree_elem_size(a), stl__rbtree_key_cmp(a),
                             STL_SET_UNIQUE, NULL, stl__rbtree_allocator(a));
    if (out == NULL) {
        return NULL;
    }

    na = stl_rbtree_first((stl_rbtree *)a);
    nb = stl_rbtree_first((stl_rbtree *)b);

    while (na != NULL || nb != NULL) {
        int c;
        int take = 0;
        const void *elem = NULL;

        if (na == NULL) {
            c = 1;
        } else if (nb == NULL) {
            c = -1;
        } else {
            c = stl__rbtree_key_cmp(a)(stl__rbtree_data_of(a, na),
                                       stl__rbtree_data_of(b, nb));
        }

        if (c < 0) {
            take = (op == 0 || op == 2 || op == 3);   /* only in a */
            elem = stl__rbtree_data_of(a, na);
        } else if (c > 0) {
            take = (op == 0 || op == 3);              /* only in b */
            elem = stl__rbtree_data_of(b, nb);
        } else {
            take = (op == 0 || op == 1);              /* in both   */
            elem = stl__rbtree_data_of(a, na);
        }

        if (take) {
            stl_rbtree_node *node = stl_rbtree_insert(out, elem);
            if (node == NULL) {
                stl_set_free(out);
                return NULL;
            }
            if (copy_elem != NULL &&
                !copy_elem(stl__rbtree_data_of(out, node), elem, stl_rbtree_elem_size(a))) {
                stl_set_free(out);
                return NULL;
            }
        }
        if (c <= 0) {
            na = stl_rbtree_next(na);
        }
        if (c >= 0) {
            nb = stl_rbtree_next(nb);
        }
    }
    return out;
}

stl_set *stl_set_union(const stl_set *a, const stl_set *b, stl_copy_fn copy_elem)
{
    return stl__set_combine(a, b, copy_elem, 0);
}

stl_set *stl_set_intersection(const stl_set *a, const stl_set *b, stl_copy_fn copy_elem)
{
    return stl__set_combine(a, b, copy_elem, 1);
}

stl_set *stl_set_difference(const stl_set *a, const stl_set *b, stl_copy_fn copy_elem)
{
    return stl__set_combine(a, b, copy_elem, 2);
}

stl_set *stl_set_symmetric_difference(const stl_set *a, const stl_set *b, stl_copy_fn copy_elem)
{
    return stl__set_combine(a, b, copy_elem, 3);
}

int stl_set_is_subset(const stl_set *a, const stl_set *b)
{
    size_t matches = 0;
    size_t na_size;
    stl_rbtree_node *node;

    if (a == NULL || b == NULL) {
        return 0;
    }
    na_size = stl_rbtree_size(a);
    if (na_size > stl_rbtree_size(b)) {
        return 0;
    }
    for (node = stl_rbtree_first((stl_rbtree *)a); node != NULL; node = stl_rbtree_next(node)) {
        if (stl_rbtree_contains(b, stl__rbtree_data_of(a, node))) {
            ++matches;
        }
    }
    return matches >= na_size;
}

/* ------------------------------------------------------------------ */
/* Iteration                                                           */
/* ------------------------------------------------------------------ */

void stl_set_foreach(stl_set *s, stl_visit_fn fn, void *user)
{
    stl_rbtree_node *node;

    if (s == NULL || fn == NULL) {
        return;
    }
    for (node = stl_rbtree_first(s); node != NULL; node = stl_rbtree_next(node)) {
        if (fn(stl_rbtree_node_data(node), user)) {
            break;
        }
    }
}

void stl_set_foreach_c(const stl_set *s, stl_visit_fn fn, void *user)
{
    stl_set_foreach((stl_set *)s, fn, user);
}

stl_iterator stl_set_begin(stl_set *s)  { return stl_rbtree_begin(s); }
stl_iterator stl_set_end(stl_set *s)    { return stl_rbtree_end(s); }
stl_iterator stl_set_rbegin(stl_set *s) { return stl_rbtree_rbegin(s); }
stl_iterator stl_set_rend(stl_set *s)   { return stl_rbtree_rend(s); }
stl_iterator stl_set_iter_next(stl_iterator it) { return stl_rbtree_iter_next(it); }
stl_iterator stl_set_iter_prev(stl_iterator it) { return stl_rbtree_iter_prev(it); }
