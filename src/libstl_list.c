/*
 * libstl_list.c -- doubly linked list with O(1) splice, stable sort and
 * node handles that stay valid until erased.
 */

#include "libstl_internal.h"

struct stl_list {
    unsigned int   magic;
    size_t         elem_size;
    size_t         node_offset;   /* sizeof(stl_list_node) -- payload follows */
    size_t         size;
    stl_list_node  head;          /* sentinel: head.next = first             */
    stl_dtor_fn    elem_dtor;
    const stl_allocator *alloc;
};

#define STL_LIST_MAGIC 0x4cu /* 'L' */

/* ------------------------------------------------------------------ */
/* Node helpers                                                        */
/* ------------------------------------------------------------------ */

void *stl_list_node_data(stl_list_node *node)
{
    if (node == NULL || node->is_header) {
        return NULL;    /* NULL, the sentinel, or nothing to point at */
    }
    return (void *)((stl_byte *)node + sizeof(stl_list_node));
}

/* Internal variant that knows the real offset (they are identical, but this
 * keeps the intent explicit and future-proof). */
static void *stl__node_data(const stl_list *l, stl_list_node *node)
{
    return (void *)((stl_byte *)node + l->node_offset);
}

static stl_list_node *stl__node_new(stl_list *l, const void *elem)
{
    stl_list_node *node;
    size_t bytes = l->node_offset + l->elem_size;

    node = (stl_list_node *)stl_mem_alloc(l->alloc, 1, bytes);
    if (node == NULL) {
        return NULL;
    }
    node->prev = NULL;
    node->next = NULL;
    node->is_header = 0;
    if (elem != NULL) {
        memcpy((stl_byte *)node + l->node_offset, elem, l->elem_size);
    } else {
        memset((stl_byte *)node + l->node_offset, 0, l->elem_size);
    }
    return node;
}

static void stl__node_free(stl_list *l, stl_list_node *node)
{
    stl_mem_free(l->alloc, node);
}

static void stl__link_before(stl_list *l, stl_list_node *pos, stl_list_node *node)
{
    node->prev = pos->prev;
    node->next = pos;
    pos->prev->next = node;
    pos->prev = node;
    l->size += 1;
}

static void stl__unlink(stl_list *l, stl_list_node *node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->prev = NULL;
    node->next = NULL;
    l->size -= 1;
}

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */

stl_list *stl_list_new_a(size_t elem_size, stl_dtor_fn elem_dtor, const stl_allocator *a)
{
    stl_list *l;

    if (elem_size == 0) {
        STL_REPORT_INVALID("list element size must be non-zero");
        return NULL;
    }
    l = (stl_list *)stl_mem_alloc(a, 1, sizeof(*l));
    if (l == NULL) {
        return NULL;
    }
    memset(l, 0, sizeof(*l));
    l->magic = STL_LIST_MAGIC;
    l->elem_size = elem_size;
    l->node_offset = sizeof(stl_list_node);
    l->elem_dtor = elem_dtor;
    l->alloc = stl__allocator_or_default(a);
    /* The header closes the ring so the internal link/unlink code can treat it
     * like any other node; `is_header` lets the public traversal helpers stop
     * at the boundary without knowing the list address. */
    l->head.next = &l->head;
    l->head.prev = &l->head;
    l->head.is_header = 1;
    return l;
}

stl_list *stl_list_new(size_t elem_size, stl_dtor_fn elem_dtor)
{
    return stl_list_new_a(elem_size, elem_dtor, NULL);
}

void stl_list_free(stl_list *l)
{
    if (l == NULL) {
        return;
    }
    STL_CHECK(l->magic == STL_LIST_MAGIC, STL_ERR_INVALID, "not a list");
    stl_list_clear_ex(l);
    l->magic = 0;
    stl_mem_free(l->alloc, l);
}

stl_list *stl_list_copy(const stl_list *l, stl_copy_fn copy_elem)
{
    stl_list *out;
    stl_list_node *node;

    if (l == NULL) {
        return NULL;
    }
    out = stl_list_new_a(l->elem_size, l->elem_dtor, l->alloc);
    if (out == NULL) {
        return NULL;
    }
    for (node = l->head.next; node != &l->head; node = node->next) {
        stl_list_node *copy = stl__node_new(out, stl__node_data(l, node));
        if (copy == NULL) {
            stl_list_free(out);
            return NULL;
        }
        stl__link_before(out, &out->head, copy);
    }
    if (copy_elem != NULL) {
        stl_list_node *src = l->head.next;
        stl_list_node *dst = out->head.next;
        while (src != &l->head && dst != &out->head) {
            if (!copy_elem(stl__node_data(out, dst), stl__node_data(l, src), l->elem_size)) {
                stl_list_free(out);
                return NULL;
            }
            src = src->next;
            dst = dst->next;
        }
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* Queries                                                             */
/* ------------------------------------------------------------------ */

size_t stl_list_size(const stl_list *l)       { return (l != NULL) ? l->size : 0; }
int    stl_list_empty(const stl_list *l)      { return (l == NULL) || (l->size == 0); }
size_t stl_list_elem_size(const stl_list *l)  { return (l != NULL) ? l->elem_size : 0; }
const stl_allocator *stl_list_allocator(const stl_list *l) { return (l != NULL) ? l->alloc : NULL; }

stl_list_node *stl_list_begin_node(stl_list *l)
{
    if (l == NULL || l->size == 0) {
        return NULL;
    }
    return l->head.next;
}

stl_list_node *stl_list_end_node(stl_list *l)
{
    STL_UNUSED(l);
    return NULL;    /* NULL terminates both sentinel-aware traversal macros */
}

/* Traversal helpers for the public node API.
 *
 * The header (sentinel) node is marked by `prev == NULL`: real nodes always
 * have a non-NULL prev (at worst it is the header) and detached nodes have
 * both links NULL.  begin_node() and end_node() therefore bracket the range
 * without the caller needing the list address, and next()/prev() return NULL
 * exactly at the boundary. */
stl_list_node *stl_list_node_next(stl_list_node *node)
{
    if (node == NULL || node->next == NULL || node->next->is_header) {
        return NULL;        /* detached node, or the step into the header */
    }
    return node->next;
}

stl_list_node *stl_list_node_prev(stl_list_node *node)
{
    if (node == NULL || node->prev == NULL || node->prev->is_header) {
        return NULL;        /* detached node, or the step into the header */
    }
    return node->prev;
}

void *stl_list_front(stl_list *l)
{
    if (l == NULL || l->size == 0) {
        stl__set_error_at(STL_ERR_EMPTY, __FILE__, __LINE__, "list::front on empty list");
        return NULL;
    }
    return stl__node_data(l, l->head.next);
}

void *stl_list_back(stl_list *l)
{
    if (l == NULL || l->size == 0) {
        stl__set_error_at(STL_ERR_EMPTY, __FILE__, __LINE__, "list::back on empty list");
        return NULL;
    }
    return stl__node_data(l, l->head.prev);
}

void *stl_list_at(stl_list *l, size_t i)
{
    stl_list_node *node;
    size_t k = 0;

    if (l == NULL) {
        STL_REPORT_INVALID("NULL list");
        return NULL;
    }
    if (i >= l->size) {
        STL_REPORT_RANGE(i, l->size);
        return NULL;
    }
    for (node = l->head.next; node != &l->head; node = node->next) {
        if (k == i) {
            return stl__node_data(l, node);
        }
        ++k;
    }
    return NULL;
}

int stl_list_contains(const stl_list *l, const void *elem, stl_equal_fn eq)
{
    return stl_list_find(l, elem, eq) != NULL;
}

void *stl_list_find(const stl_list *l, const void *elem, stl_equal_fn eq)
{
    stl_list_node *node;

    if (l == NULL || elem == NULL) {
        return NULL;
    }
    if (eq == NULL) {
        eq = stl_eq_mem;
    }
    for (node = l->head.next; node != &l->head; node = node->next) {
        if (eq(stl__node_data(l, node), elem)) {
            return stl__node_data(l, node);
        }
    }
    return NULL;
}

void stl_list_foreach(stl_list *l, stl_visit_fn fn, void *user)
{
    stl_list_node *node;

    if (l == NULL || fn == NULL) {
        return;
    }
    for (node = l->head.next; node != &l->head; ) {
        stl_list_node *next = node->next;   /* allow erase during visit */
        if (fn(stl__node_data(l, node), user)) {
            break;
        }
        node = next;
    }
}

void stl_list_foreach_c(const stl_list *l, stl_visit_fn fn, void *user)
{
    stl_list_foreach((stl_list *)l, fn, user);
}

/* ------------------------------------------------------------------ */
/* Modifiers                                                           */
/* ------------------------------------------------------------------ */

stl_list_node *stl_list_push_back(stl_list *l, const void *elem)
{
    stl_list_node *node;

    if (l == NULL) {
        STL_REPORT_INVALID("NULL list");
        return NULL;
    }
    node = stl__node_new(l, elem);
    if (node == NULL) {
        return NULL;
    }
    stl__link_before(l, &l->head, node);
    return node;
}

stl_list_node *stl_list_push_front(stl_list *l, const void *elem)
{
    stl_list_node *node;

    if (l == NULL) {
        STL_REPORT_INVALID("NULL list");
        return NULL;
    }
    node = stl__node_new(l, elem);
    if (node == NULL) {
        return NULL;
    }
    stl__link_before(l, l->head.next, node);
    return node;
}

stl_list_node *stl_list_insert_after(stl_list *l, stl_list_node *node, const void *elem)
{
    if (l == NULL) {
        STL_REPORT_INVALID("NULL list");
        return NULL;
    }
    if (node == NULL) {
        return stl_list_push_front(l, elem);
    }
    {
        stl_list_node *fresh = stl__node_new(l, elem);
        if (fresh == NULL) {
            return NULL;
        }
        stl__link_before(l, node->next, fresh);
        return fresh;
    }
}

stl_list_node *stl_list_insert_before(stl_list *l, stl_list_node *node, const void *elem)
{
    if (l == NULL) {
        STL_REPORT_INVALID("NULL list");
        return NULL;
    }
    if (node == NULL) {
        return stl_list_push_back(l, elem);
    }
    {
        stl_list_node *fresh = stl__node_new(l, elem);
        if (fresh == NULL) {
            return NULL;
        }
        stl__link_before(l, node, fresh);
        return fresh;
    }
}

stl_list_node *stl_list_insert(stl_list *l, size_t pos, const void *elem)
{
    stl_list_node *node;
    size_t k = 0;

    if (l == NULL) {
        STL_REPORT_INVALID("NULL list");
        return NULL;
    }
    if (pos > l->size) {
        STL_REPORT_RANGE(pos, l->size);
        return NULL;
    }
    if (pos == l->size) {
        return stl_list_push_back(l, elem);
    }
    for (node = l->head.next; node != &l->head; node = node->next) {
        if (k == pos) {
            return stl_list_insert_before(l, node, elem);
        }
        ++k;
    }
    return NULL;
}

void stl_list_pop_back(stl_list *l)
{
    if (l == NULL || l->size == 0) {
        stl__set_error_at(STL_ERR_EMPTY, __FILE__, __LINE__, "list::pop_back on empty list");
        return;
    }
    (void)stl_list_erase_node(l, l->head.prev);
}

void stl_list_pop_front(stl_list *l)
{
    if (l == NULL || l->size == 0) {
        stl__set_error_at(STL_ERR_EMPTY, __FILE__, __LINE__, "list::pop_front on empty list");
        return;
    }
    (void)stl_list_erase_node(l, l->head.next);
}

int stl_list_erase_node(stl_list *l, stl_list_node *node)
{
    if (l == NULL || node == NULL || node == &l->head) {
        STL_REPORT_INVALID("list::erase_node(invalid node)");
        return STL_ERR_INVALID;
    }
    stl__unlink(l, node);
    stl__node_free(l, node);
    return STL_OK;
}

int stl_list_erase(stl_list *l, size_t pos)
{
    stl_list_node *node;
    size_t k = 0;

    if (l == NULL) {
        STL_REPORT_INVALID("NULL list");
        return STL_ERR_INVALID;
    }
    if (pos >= l->size) {
        STL_REPORT_RANGE(pos, l->size);
        return STL_ERR_RANGE;
    }
    for (node = l->head.next; node != &l->head; node = node->next) {
        if (k == pos) {
            return stl_list_erase_node(l, node);
        }
        ++k;
    }
    return STL_ERR_RANGE;
}

int stl_list_erase_range(stl_list *l, size_t first, size_t last)
{
    stl_list_node *node;
    size_t k = 0;

    if (l == NULL) {
        STL_REPORT_INVALID("NULL list");
        return STL_ERR_INVALID;
    }
    if (first > last || last > l->size) {
        stl__set_error_at(STL_ERR_RANGE, __FILE__, __LINE__, "list::erase_range(%lu, %lu) size %lu",
                  (unsigned long)first, (unsigned long)last, (unsigned long)l->size);
        return STL_ERR_RANGE;
    }
    for (node = l->head.next; node != &l->head; ) {
        stl_list_node *next = node->next;
        if (k >= first && k < last) {
            stl_list_erase_node(l, node);
        }
        ++k;
        node = next;
    }
    return STL_OK;
}

void stl_list_clear(stl_list *l)
{
    stl_list_node *node;

    if (l == NULL) {
        return;
    }
    for (node = l->head.next; node != &l->head; ) {
        stl_list_node *next = node->next;
        stl__node_free(l, node);
        node = next;
    }
    l->head.next = &l->head;
    l->head.prev = &l->head;
    l->size = 0;
}

void stl_list_clear_ex(stl_list *l)
{
    stl_list_node *node;

    if (l == NULL) {
        return;
    }
    for (node = l->head.next; node != &l->head; ) {
        stl_list_node *next = node->next;
        if (l->elem_dtor != NULL) {
            l->elem_dtor(stl__node_data(l, node));
        }
        stl__node_free(l, node);
        node = next;
    }
    l->head.next = &l->head;
    l->head.prev = &l->head;
    l->size = 0;
}

int stl_list_splice(stl_list *dst, size_t dst_pos, stl_list *src)
{
    stl_list_node *pos;
    size_t k = 0;

    if (dst == NULL || src == NULL || src->size == 0) {
        return STL_OK;
    }
    if (dst->elem_size != src->elem_size) {
        stl__set_error_at(STL_ERR_TYPE, __FILE__, __LINE__, "list::splice element size mismatch (%lu vs %lu)",
                  (unsigned long)dst->elem_size, (unsigned long)src->elem_size);
        return STL_ERR_TYPE;
    }
    if (dst == src) {
        return STL_OK;
    }
    if (dst_pos > dst->size) {
        STL_REPORT_RANGE(dst_pos, dst->size);
        return STL_ERR_RANGE;
    }
    for (pos = dst->head.next; pos != &dst->head; pos = pos->next) {
        if (k == dst_pos) {
            break;
        }
        ++k;
    }

    /* Detach src's chain and splice it in before `pos`. */
    {
        stl_list_node *first = src->head.next;
        stl_list_node *last = src->head.prev;

        first->prev = pos->prev;
        pos->prev->next = first;
        last->next = pos;
        pos->prev = last;

        dst->size += src->size;
        src->size = 0;
        src->head.next = &src->head;
        src->head.prev = &src->head;
    }
    return STL_OK;
}

int stl_list_sort(stl_list *l, stl_compare_fn cmp)
{
    if (l == NULL || l->size < 2) {
        return STL_OK;
    }
    /* The merge-based implementation below is already stable, so sort() and
     * stable_sort() share an implementation. */
    return stl_list_sort_stable(l, cmp);
}

int stl_list_sort_stable(stl_list *l, stl_compare_fn cmp)
{
    void *flat;
    stl_list_node *node;
    size_t i;

    if (l == NULL || l->size < 2) {
        return STL_OK;
    }
    if (cmp == NULL) {
        cmp = stl_cmp_mem;
    }
    /* Simple approach: flatten into a buffer, merge-sort, relink.  Keeps the
     * node identities (and therefore existing node handles) intact. */
    flat = stl_mem_alloc(l->alloc, l->size, l->elem_size);
    if (flat == NULL) {
        return STL_ERR_NOMEM;
    }
    i = 0;
    for (node = l->head.next; node != &l->head; node = node->next, ++i) {
        memcpy(STL_ELEM_AT(flat, l->elem_size, i), stl__node_data(l, node), l->elem_size);
    }
    stl_stable_sort(flat, l->size, l->elem_size, cmp);
    i = 0;
    for (node = l->head.next; node != &l->head; node = node->next, ++i) {
        memcpy(stl__node_data(l, node), STL_ELEM_AT(flat, l->elem_size, i), l->elem_size);
    }
    stl_mem_free(l->alloc, flat);
    return STL_OK;
}

int stl_list_merge(stl_list *a, stl_list *b, stl_compare_fn cmp)
{
    stl_list_node *na, *nb;

    if (a == NULL || b == NULL || a == b) {
        return STL_OK;
    }
    if (a->elem_size != b->elem_size) {
        stl__set_error_at(STL_ERR_TYPE, __FILE__, __LINE__, "list::merge element size mismatch");
        return STL_ERR_TYPE;
    }
    if (cmp == NULL) {
        cmp = stl_cmp_mem;
    }
    na = a->head.next;
    nb = b->head.next;
    while (na != &a->head && nb != &b->head) {
        if (cmp(stl__node_data(b, nb), stl__node_data(a, na)) < 0) {
            stl_list_node *moved = nb;
            nb = nb->next;
            stl__unlink(b, moved);
            stl__link_before(a, na, moved);
        } else {
            na = na->next;
        }
    }
    while (nb != &b->head) {
        stl_list_node *moved = nb;
        nb = nb->next;
        stl__unlink(b, moved);
        stl__link_before(a, &a->head, moved);
    }
    return STL_OK;
}

void stl_list_reverse(stl_list *l)
{
    stl_list_node *node;
    stl_list_node *tmp;

    if (l == NULL || l->size < 2) {
        return;
    }
    /* Walk the whole ring, including the header, swapping the links. */
    for (node = &l->head;;) {
        tmp = node->next;
        node->next = node->prev;
        node->prev = tmp;
        node = node->prev;          /* == old node->next */
        if (node == &l->head) {
            break;
        }
    }
}

void stl_list_unique(stl_list *l, stl_equal_fn eq)
{
    stl_list_node *node;

    if (l == NULL || l->size < 2) {
        return;
    }
    if (eq == NULL) {
        eq = stl_eq_mem;
    }
    for (node = l->head.next; node->next != &l->head; ) {
        if (eq(stl__node_data(l, node), stl__node_data(l, node->next))) {
            stl_list_node *dup = node->next;
            if (l->elem_dtor != NULL) {
                l->elem_dtor(stl__node_data(l, dup));
            }
            stl_list_erase_node(l, dup);
        } else {
            node = node->next;
        }
    }
}

size_t stl_list_remove_if(stl_list *l, int (STL_CALL *pred)(const void *, void *), void *user)
{
    stl_list_node *node;
    size_t removed = 0;

    if (l == NULL || pred == NULL) {
        return 0;
    }
    for (node = l->head.next; node != &l->head; ) {
        stl_list_node *next = node->next;
        if (pred(stl__node_data(l, node), user)) {
            if (l->elem_dtor != NULL) {
                l->elem_dtor(stl__node_data(l, node));
            }
            stl_list_erase_node(l, node);
            ++removed;
        }
        node = next;
    }
    return removed;
}

stl_list *stl_list_slice(const stl_list *l, size_t first, size_t last)
{
    stl_list *out;
    stl_list_node *node;
    size_t k = 0;

    if (l == NULL || first > last || last > l->size) {
        stl__set_error_at(STL_ERR_RANGE, __FILE__, __LINE__, "list::slice(%lu, %lu) size %lu",
                  (unsigned long)first, (unsigned long)last,
                  (unsigned long)((l != NULL) ? l->size : 0));
        return NULL;
    }
    out = stl_list_new_a(l->elem_size, NULL, l->alloc);
    if (out == NULL) {
        return NULL;
    }
    for (node = l->head.next; node != &l->head && k < last; node = node->next, ++k) {
        if (k >= first) {
            if (stl_list_push_back(out, stl__node_data(l, node)) == NULL) {
                stl_list_free(out);
                return NULL;
            }
        }
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* Iterators                                                           */
/* ------------------------------------------------------------------ */

static stl_iterator stl__list_iter(stl_list *l, stl_list_node *node)
{
    stl_iterator it;
    size_t index = 0;
    stl_list_node *cur;

    it.owner = l;
    it.elem = (node != NULL && node != &l->head) ? stl__node_data(l, node) : NULL;
    for (cur = l->head.next; cur != node && cur != &l->head; cur = cur->next) {
        ++index;
    }
    it.index = index;
    return it;
}

stl_iterator stl_list_begin(stl_list *l)  { return (l != NULL) ? stl__list_iter(l, l->head.next) : stl_iter_null(); }
stl_iterator stl_list_end(stl_list *l)
{
    stl_iterator it;
    it.owner = l;
    it.elem = NULL;
    it.index = (l != NULL) ? l->size : 0;
    return it;
}

stl_iterator stl_list_rbegin(stl_list *l)
{
    if (l == NULL || l->size == 0) {
        return stl_list_end(l);
    }
    return stl__list_iter(l, l->head.prev);
}

stl_iterator stl_list_rend(stl_list *l)
{
    stl_iterator it = stl_list_end(l);
    it.index = (size_t)-1;
    return it;
}

stl_iterator stl_list_iter_next(stl_iterator it)
{
    stl_list *l = (stl_list *)it.owner;
    stl_list_node *node;

    if (l == NULL) {
        return stl_iter_null();
    }
    if (it.elem == NULL) {
        return stl_list_end(l);
    }
    node = (stl_list_node *)(void *)((stl_byte *)it.elem - l->node_offset);
    if (node->next == &l->head) {
        return stl_list_end(l);
    }
    return stl__list_iter(l, node->next);
}

stl_iterator stl_list_iter_prev(stl_iterator it)
{
    stl_list *l = (stl_list *)it.owner;
    stl_list_node *node;

    if (l == NULL) {
        return stl_iter_null();
    }
    if (it.elem == NULL) {
        /* end() -- step back to the last element. */
        if (l->size == 0) {
            return stl_list_end(l);
        }
        return stl__list_iter(l, l->head.prev);
    }
    node = (stl_list_node *)(void *)((stl_byte *)it.elem - l->node_offset);
    if (node->prev == &l->head) {
        return stl_list_rend(l);
    }
    return stl__list_iter(l, node->prev);
}

ptrdiff_t stl_list_iter_distance(stl_iterator first, stl_iterator last)
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
