/*
 * libstl_adaptor.c -- stack, queue and priority_queue container adaptors.
 */

#include "libstl_internal.h"

/* ================================================================== */
/*  stack -- LIFO over stl_vector                                      */
/* ================================================================== */

struct stl_stack {
    stl_vector *impl;
};

stl_stack *stl_stack_new_a(size_t elem_size, stl_dtor_fn elem_dtor, const stl_allocator *a)
{
    stl_stack *s;
    if (elem_size == 0) {
        STL_REPORT_INVALID("stack element size must be non-zero");
        return NULL;
    }
    s = (stl_stack *)stl_mem_alloc(a, 1, sizeof(*s));
    if (s == NULL) {
        return NULL;
    }
    s->impl = stl_vector_new_cap(elem_size, 0, elem_dtor, a);
    if (s->impl == NULL) {
        stl_mem_free(a, s);
        return NULL;
    }
    return s;
}

stl_stack *stl_stack_new(size_t elem_size, stl_dtor_fn elem_dtor)
{
    return stl_stack_new_a(elem_size, elem_dtor, NULL);
}

void stl_stack_free(stl_stack *s)
{
    if (s == NULL) {
        return;
    }
    {
        const stl_allocator *alloc = stl_vector_allocator(s->impl);
        stl_vector_free(s->impl);
        stl_mem_free(alloc, s);
    }
}

int stl_stack_push(stl_stack *s, const void *elem)
{
    if (s == NULL) {
        STL_REPORT_INVALID("NULL stack");
        return STL_ERR_INVALID;
    }
    return stl_vector_push_back(s->impl, elem);
}

void stl_stack_pop(stl_stack *s)
{
    if (s != NULL) {
        stl_vector_pop_back(s->impl);
    }
}

void *stl_stack_top(stl_stack *s)
{
    if (s == NULL) {
        STL_REPORT_INVALID("NULL stack");
        return NULL;
    }
    return stl_vector_back(s->impl);
}

const void *stl_stack_top_c(const stl_stack *s)
{
    return (s != NULL) ? stl_vector_back(s->impl) : NULL;
}

size_t stl_stack_size(const stl_stack *s) { return (s != NULL) ? stl_vector_size(s->impl) : 0; }
int stl_stack_empty(const stl_stack *s)   { return (s == NULL) || stl_vector_empty(s->impl); }
void stl_stack_clear(stl_stack *s)        { if (s != NULL) stl_vector_clear(s->impl); }
void stl_stack_clear_ex(stl_stack *s)     { if (s != NULL) stl_vector_clear_ex(s->impl); }

void stl_stack_swap(stl_stack *a, stl_stack *b)
{
    stl_vector *tmp;
    if (a == NULL || b == NULL) {
        return;
    }
    tmp = a->impl;
    a->impl = b->impl;
    b->impl = tmp;
}

/* ================================================================== */
/*  queue -- FIFO over stl_deque                                       */
/* ================================================================== */

struct stl_queue {
    stl_deque *impl;
};

stl_queue *stl_queue_new_a(size_t elem_size, stl_dtor_fn elem_dtor, const stl_allocator *a)
{
    stl_queue *q;
    if (elem_size == 0) {
        STL_REPORT_INVALID("queue element size must be non-zero");
        return NULL;
    }
    q = (stl_queue *)stl_mem_alloc(a, 1, sizeof(*q));
    if (q == NULL) {
        return NULL;
    }
    q->impl = stl_deque_new_a(elem_size, elem_dtor, a);
    if (q->impl == NULL) {
        stl_mem_free(a, q);
        return NULL;
    }
    return q;
}

stl_queue *stl_queue_new(size_t elem_size, stl_dtor_fn elem_dtor)
{
    return stl_queue_new_a(elem_size, elem_dtor, NULL);
}

void stl_queue_free(stl_queue *q)
{
    if (q == NULL) {
        return;
    }
    {
        const stl_allocator *alloc = stl_deque_allocator(q->impl);
        stl_deque_free(q->impl);
        stl_mem_free(alloc, q);
    }
}

int stl_queue_push(stl_queue *q, const void *elem)
{
    if (q == NULL) {
        STL_REPORT_INVALID("NULL queue");
        return STL_ERR_INVALID;
    }
    return stl_deque_push_back(q->impl, elem);
}

void stl_queue_pop(stl_queue *q)
{
    if (q != NULL) {
        stl_deque_pop_front(q->impl);
    }
}

void *stl_queue_front(stl_queue *q) { return (q != NULL) ? stl_deque_front(q->impl) : NULL; }
void *stl_queue_back(stl_queue *q)  { return (q != NULL) ? stl_deque_back(q->impl) : NULL; }
const void *stl_queue_front_c(const stl_queue *q) { return (q != NULL) ? stl_deque_front(q->impl) : NULL; }
const void *stl_queue_back_c(const stl_queue *q)  { return (q != NULL) ? stl_deque_back(q->impl) : NULL; }

size_t stl_queue_size(const stl_queue *q) { return (q != NULL) ? stl_deque_size(q->impl) : 0; }
int stl_queue_empty(const stl_queue *q)   { return (q == NULL) || stl_deque_empty(q->impl); }
void stl_queue_clear(stl_queue *q)        { if (q != NULL) stl_deque_clear(q->impl); }
void stl_queue_clear_ex(stl_queue *q)     { if (q != NULL) stl_deque_clear_ex(q->impl); }

void stl_queue_swap(stl_queue *a, stl_queue *b)
{
    stl_deque *tmp;
    if (a == NULL || b == NULL) {
        return;
    }
    tmp = a->impl;
    a->impl = b->impl;
    b->impl = tmp;
}

/* ================================================================== */
/*  priority_queue -- binary heap over stl_vector                      */
/* ================================================================== */

struct stl_priority_queue {
    stl_vector   *impl;
    stl_compare_fn cmp;
};

stl_priority_queue *stl_priority_queue_new_a(size_t elem_size, stl_compare_fn cmp,
                                             stl_dtor_fn elem_dtor, const stl_allocator *a)
{
    stl_priority_queue *pq;

    if (elem_size == 0) {
        STL_REPORT_INVALID("priority_queue element size must be non-zero");
        return NULL;
    }
    pq = (stl_priority_queue *)stl_mem_alloc(a, 1, sizeof(*pq));
    if (pq == NULL) {
        return NULL;
    }
    pq->impl = stl_vector_new_cap(elem_size, 0, elem_dtor, a);
    if (pq->impl == NULL) {
        stl_mem_free(a, pq);
        return NULL;
    }
    pq->cmp = (cmp != NULL) ? cmp : stl_default_priority_cmp;
    return pq;
}

stl_priority_queue *stl_priority_queue_new(size_t elem_size, stl_compare_fn cmp,
                                           stl_dtor_fn elem_dtor)
{
    return stl_priority_queue_new_a(elem_size, cmp, elem_dtor, NULL);
}

void stl_priority_queue_free(stl_priority_queue *pq)
{
    if (pq == NULL) {
        return;
    }
    /* Capture the allocator before releasing the backing container: after the
     * free, reading it back would be a use-after-free. */
    {
        const stl_allocator *alloc = stl_vector_allocator(pq->impl);
        stl_vector_free(pq->impl);
        stl_mem_free(alloc, pq);
    }
}

int stl_priority_queue_push(stl_priority_queue *pq, const void *elem)
{
    int rc;

    if (pq == NULL || elem == NULL) {
        STL_REPORT_INVALID("priority_queue::push(NULL)");
        return STL_ERR_INVALID;
    }
    rc = stl_vector_push_back(pq->impl, elem);
    if (rc != STL_OK) {
        return rc;
    }
    stl_heap_push(stl_vector_data(pq->impl), stl_vector_size(pq->impl),
                  stl_vector_elem_size(pq->impl), pq->cmp);
    return STL_OK;
}

void stl_priority_queue_pop(stl_priority_queue *pq)
{
    size_t n;

    if (pq == NULL) {
        return;
    }
    n = stl_vector_size(pq->impl);
    if (n == 0) {
        stl__set_error_at(STL_ERR_EMPTY, __FILE__, __LINE__, "priority_queue::pop on empty queue");
        return;
    }
    stl_heap_pop(stl_vector_data(pq->impl), n, stl_vector_elem_size(pq->impl), pq->cmp);
    stl_vector_pop_back(pq->impl);
}

void *stl_priority_queue_top(stl_priority_queue *pq)
{
    if (pq == NULL || stl_vector_empty(pq->impl)) {
        stl__set_error_at(STL_ERR_EMPTY, __FILE__, __LINE__, "priority_queue::top on empty queue");
        return NULL;
    }
    return stl_vector_front(pq->impl);
}

const void *stl_priority_queue_top_c(const stl_priority_queue *pq)
{
    return (const void *)stl_priority_queue_top((stl_priority_queue *)pq);
}

size_t stl_priority_queue_size(const stl_priority_queue *pq)
{
    return (pq != NULL) ? stl_vector_size(pq->impl) : 0;
}

int stl_priority_queue_empty(const stl_priority_queue *pq)
{
    return (pq == NULL) || stl_vector_empty(pq->impl);
}

void stl_priority_queue_clear(stl_priority_queue *pq)
{
    if (pq != NULL) {
        stl_vector_clear(pq->impl);
    }
}

void stl_priority_queue_clear_ex(stl_priority_queue *pq)
{
    if (pq != NULL) {
        stl_vector_clear_ex(pq->impl);
    }
}

int stl_priority_queue_reserve(stl_priority_queue *pq, size_t n)
{
    if (pq == NULL) {
        STL_REPORT_INVALID("NULL priority_queue");
        return STL_ERR_INVALID;
    }
    return stl_vector_reserve(pq->impl, n);
}
