/*
 * libstl_deque.c -- double-ended queue.
 *
 * The deque owns a growing array of fixed-size blocks arranged as a ring.  A
 * logical element index maps to (block, offset) through a single "start"
 * cursor that walks the ring: consecutive logical indices always occupy
 * consecutive slots, wrapping around the end of the block array when needed.
 *
 *   start = offset of logical element 0 inside the ring
 *   element i lives at ring slot ((start + i) mod slot_capacity)
 *
 * Both `start` and `slot_capacity` are measured in *element slots*, not in
 * blocks: slot_capacity == block_slots * block_elems.  Growing allocates a
 * bigger block array and copies the logical sequence into it in order, so
 * push_front/push_back keep their amortised O(1) cost while random access
 * stays O(1) with no allocation.
 */

#include "libstl_internal.h"

/* Target block payload size in bytes; blocks hold at least one element. */
#define STL_DEQUE_BLOCK_TARGET 4096u

struct stl_deque {
    unsigned int magic;
    stl_byte   **blocks;        /* array of block pointers                     */
    size_t       block_slots;   /* number of block slots allocated             */
    size_t       block_elems;   /* usable elements per block                   */
    size_t       start;         /* ring slot of logical element 0 (elements!)  */
    size_t       size;          /* logical element count                       */
    size_t       elem_size;
    stl_dtor_fn  elem_dtor;
    const stl_allocator *alloc;
};

/* Number of element slots the ring can address. */
#define STL_DEQUE_SLOTS(d) ((d)->block_slots * (d)->block_elems)

#define STL_DEQUE_MAGIC 0x44u /* 'D' */

/* ------------------------------------------------------------------ */
/* Ring geometry                                                       */
/* ------------------------------------------------------------------ */

/* Materialise the block for element slot `ring_index` and return its address. */
static void *stl__deque_ring_slot(stl_deque *d, size_t ring_index)
{
    size_t block = ring_index / d->block_elems;
    size_t offset = ring_index % d->block_elems;

    if (block >= d->block_slots) {
        return NULL;        /* caller failed to reserve enough slots */
    }
    if (d->blocks[block] == NULL) {
        d->blocks[block] = (stl_byte *)stl_mem_alloc(d->alloc, d->block_elems, d->elem_size);
        if (d->blocks[block] == NULL) {
            return NULL;
        }
    }
    return (void *)(d->blocks[block] + offset * d->elem_size);
}

/* Pointer to logical element `i` (0 <= i < size). */
static void *stl__deque_elem(stl_deque *d, size_t i)
{
    size_t ring = d->start + i;
    size_t slots = STL_DEQUE_SLOTS(d);

    /* start and i are both < slots, so one conditional subtraction suffices. */
    if (ring >= slots) {
        ring -= slots;
    }
    return stl__deque_ring_slot(d, ring);
}

/* ------------------------------------------------------------------ */
/* Ring growth                                                         */
/* ------------------------------------------------------------------ */

/* Make sure `needed` logical elements fit; preserves element order.
 * Growing re-lays the live blocks so that logical element 0 sits at slot 0,
 * which resets `start` and removes every wrap-around from the hot path. */
static int stl__deque_grow(stl_deque *d, size_t needed)
{
    size_t needed_slots;
    size_t new_blocks;
    size_t new_slots;
    stl_byte **fresh;
    size_t i;

    needed_slots = needed + 1;                  /* keep one slot spare */
    if (needed_slots <= STL_DEQUE_SLOTS(d)) {
        return STL_OK;
    }

    new_blocks = (needed_slots + d->block_elems - 1) / d->block_elems;
    if (new_blocks < 2) {
        new_blocks = 2;
    }
    new_slots = new_blocks * d->block_elems;
    while (new_slots < needed_slots) {
        new_blocks = stl_growth_capacity(new_blocks);
        new_slots = new_blocks * d->block_elems;
    }

    fresh = (stl_byte **)stl_mem_alloc(d->alloc, new_blocks, sizeof(stl_byte *));
    if (fresh == NULL) {
        return STL_ERR_NOMEM;
    }
    memset(fresh, 0, new_blocks * sizeof(stl_byte *));

    /* Copy the live block pointers in logical order, then the two possibly
     * non-contiguous partial blocks at the ends.  The element sequence spans
     * whole blocks except for the first and last one, so re-packing means:
     *   - allocate aligned replacement blocks for the partial edges, or
     *   - keep the block array aligned by construction.
     * We take the simple, always-correct route: rebuild the element sequence
     * into a fresh block array. */
    for (i = 0; i < d->size; ++i) {
        size_t old_ring = d->start + i;
        size_t slots = STL_DEQUE_SLOTS(d);
        void *src;
        if (old_ring >= slots) {
            old_ring -= slots;
        }
        src = stl__deque_ring_slot(d, old_ring);
        if (src == NULL) {
            stl_mem_free(d->alloc, fresh);
            return STL_ERR_NOMEM;
        }
        /* Destination slot i inside the new array. */
        {
            size_t dst_block = i / d->block_elems;
            size_t dst_off = i % d->block_elems;
            if (fresh[dst_block] == NULL) {
                fresh[dst_block] = (stl_byte *)stl_mem_alloc(d->alloc, d->block_elems,
                                                             d->elem_size);
                if (fresh[dst_block] == NULL) {
                    size_t k;
                    for (k = 0; k < new_blocks; ++k) {
                        stl_mem_free(d->alloc, fresh[k]);
                    }
                    stl_mem_free(d->alloc, fresh);
                    return STL_ERR_NOMEM;
                }
            }
            memcpy(fresh[dst_block] + dst_off * d->elem_size, src, d->elem_size);
        }
    }

    /* Release the old blocks, then adopt the new array. */
    for (i = 0; i < d->block_slots; ++i) {
        stl_mem_free(d->alloc, d->blocks[i]);
    }
    stl_mem_free(d->alloc, d->blocks);
    d->blocks = fresh;
    d->block_slots = new_blocks;
    d->start = 0;
    return STL_OK;
}

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */

static size_t stl__deque_block_elems_for(size_t elem_size)
{
    size_t n = STL_DEQUE_BLOCK_TARGET / elem_size;
    if (n < 1) {
        n = 1;
    }
    return n;
}

stl_deque *stl_deque_new_a(size_t elem_size, stl_dtor_fn elem_dtor, const stl_allocator *a)
{
    stl_deque *d;

    if (elem_size == 0) {
        STL_REPORT_INVALID("deque element size must be non-zero");
        return NULL;
    }
    d = (stl_deque *)stl_mem_alloc(a, 1, sizeof(*d));
    if (d == NULL) {
        return NULL;
    }
    memset(d, 0, sizeof(*d));
    d->magic = STL_DEQUE_MAGIC;
    d->elem_size = elem_size;
    d->block_elems = stl__deque_block_elems_for(elem_size);
    d->elem_dtor = elem_dtor;
    d->alloc = stl__allocator_or_default(a);
    d->block_slots = 4;
    d->start = 0;
    d->size = 0;
    d->blocks = (stl_byte **)stl_mem_alloc(d->alloc, d->block_slots, sizeof(stl_byte *));
    if (d->blocks == NULL) {
        stl_mem_free(d->alloc, d);
        return NULL;
    }
    memset(d->blocks, 0, d->block_slots * sizeof(stl_byte *));
    return d;
}

stl_deque *stl_deque_new(size_t elem_size, stl_dtor_fn elem_dtor)
{
    return stl_deque_new_a(elem_size, elem_dtor, NULL);
}

static void stl__deque_release_blocks(stl_deque *d)
{
    size_t i;

    if (d->blocks == NULL) {
        return;
    }
    for (i = 0; i < d->block_slots; ++i) {
        if (d->blocks[i] != NULL) {
            stl_mem_free(d->alloc, d->blocks[i]);
            d->blocks[i] = NULL;
        }
    }
    stl_mem_free(d->alloc, d->blocks);
    d->blocks = NULL;
    d->block_slots = 0;
}

void stl_deque_free(stl_deque *d)
{
    if (d == NULL) {
        return;
    }
    STL_CHECK(d->magic == STL_DEQUE_MAGIC, STL_ERR_INVALID, "not a deque");
    stl_deque_clear_ex(d);
    stl__deque_release_blocks(d);
    d->magic = 0;
    stl_mem_free(d->alloc, d);
}

stl_deque *stl_deque_copy(const stl_deque *d, stl_copy_fn copy_elem)
{
    stl_deque *out;
    size_t i;

    if (d == NULL) {
        return NULL;
    }
    out = stl_deque_new_a(d->elem_size, d->elem_dtor, d->alloc);
    if (out == NULL) {
        return NULL;
    }
    for (i = 0; i < d->size; ++i) {
        const void *src = stl__deque_elem((stl_deque *)d, i);
        if (stl_deque_push_back(out, src) != STL_OK) {
            stl_deque_free(out);
            return NULL;
        }
    }
    if (copy_elem != NULL) {
        for (i = 0; i < out->size; ++i) {
            if (!copy_elem(stl__deque_elem(out, i), stl__deque_elem((stl_deque *)d, i),
                           d->elem_size)) {
                stl_deque_free(out);
                return NULL;
            }
        }
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* Queries                                                             */
/* ------------------------------------------------------------------ */

size_t stl_deque_size(const stl_deque *d)      { return (d != NULL) ? d->size : 0; }
int    stl_deque_empty(const stl_deque *d)     { return (d == NULL) || (d->size == 0); }
size_t stl_deque_elem_size(const stl_deque *d) { return (d != NULL) ? d->elem_size : 0; }
const stl_allocator *stl_deque_allocator(const stl_deque *d) { return (d != NULL) ? d->alloc : NULL; }

void *stl_deque_at(stl_deque *d, size_t i)
{
    if (d == NULL) {
        STL_REPORT_INVALID("NULL deque");
        return NULL;
    }
    if (i >= d->size) {
        STL_REPORT_RANGE(i, d->size);
        return NULL;
    }
    return stl__deque_elem(d, i);
}

const void *stl_deque_at_c(const stl_deque *d, size_t i)
{
    return stl_deque_at((stl_deque *)d, i);
}

void *stl_deque_front(stl_deque *d)
{
    if (d == NULL || d->size == 0) {
        stl__set_error_at(STL_ERR_EMPTY, __FILE__, __LINE__, "deque::front on empty deque");
        return NULL;
    }
    return stl__deque_elem(d, 0);
}

void *stl_deque_back(stl_deque *d)
{
    if (d == NULL || d->size == 0) {
        stl__set_error_at(STL_ERR_EMPTY, __FILE__, __LINE__, "deque::back on empty deque");
        return NULL;
    }
    return stl__deque_elem(d, d->size - 1);
}

/* ------------------------------------------------------------------ */
/* Modifiers                                                           */
/* ------------------------------------------------------------------ */

int stl_deque_push_back(stl_deque *d, const void *elem)
{
    void *slot;

    if (d == NULL || elem == NULL) {
        STL_REPORT_INVALID("deque::push_back(NULL)");
        return STL_ERR_INVALID;
    }
    if (stl__deque_grow(d, d->size + 1) != STL_OK) {
        return STL_ERR_NOMEM;
    }
    slot = stl__deque_elem(d, d->size);
    if (slot == NULL) {
        return STL_ERR_NOMEM;
    }
    memcpy(slot, elem, d->elem_size);
    d->size += 1;
    return STL_OK;
}

int stl_deque_push_front(stl_deque *d, const void *elem)
{
    void *slot;

    if (d == NULL || elem == NULL) {
        STL_REPORT_INVALID("deque::push_front(NULL)");
        return STL_ERR_INVALID;
    }
    if (stl__deque_grow(d, d->size + 1) != STL_OK) {
        return STL_ERR_NOMEM;
    }
    /* Step the start cursor back one element slot (wrapping through the ring). */
    {
        size_t slots = STL_DEQUE_SLOTS(d);
        d->start = (d->start == 0) ? (slots - 1) : (d->start - 1);
    }
    slot = stl__deque_ring_slot(d, d->start);
    if (slot == NULL) {
        size_t slots = STL_DEQUE_SLOTS(d);
        d->start = (d->start + 1 == slots) ? 0 : (d->start + 1);
        return STL_ERR_NOMEM;
    }
    memcpy(slot, elem, d->elem_size);
    d->size += 1;
    return STL_OK;
}

void stl_deque_pop_back(stl_deque *d)
{
    if (d == NULL || d->size == 0) {
        stl__set_error_at(STL_ERR_EMPTY, __FILE__, __LINE__, "deque::pop_back on empty deque");
        return;
    }
    d->size -= 1;
    if (d->elem_dtor != NULL) {
        void *p = stl__deque_elem(d, d->size);
        if (p != NULL) {
            d->elem_dtor(p);
        }
    }
}

void stl_deque_pop_front(stl_deque *d)
{
    if (d == NULL || d->size == 0) {
        stl__set_error_at(STL_ERR_EMPTY, __FILE__, __LINE__, "deque::pop_front on empty deque");
        return;
    }
    if (d->elem_dtor != NULL) {
        void *p = stl__deque_elem(d, 0);
        if (p != NULL) {
            d->elem_dtor(p);
        }
    }
    {
        size_t slots = STL_DEQUE_SLOTS(d);
        d->start = (d->start + 1 == slots) ? 0 : (d->start + 1);
    }
    d->size -= 1;
}

int stl_deque_insert(stl_deque *d, size_t pos, const void *elem)
{
    size_t i;

    if (d == NULL || elem == NULL) {
        STL_REPORT_INVALID("deque::insert(NULL)");
        return STL_ERR_INVALID;
    }
    if (pos > d->size) {
        STL_REPORT_RANGE(pos, d->size);
        return STL_ERR_RANGE;
    }
    if (pos == 0) {
        return stl_deque_push_front(d, elem);
    }
    if (pos == d->size) {
        return stl_deque_push_back(d, elem);
    }
    /* Extend at whichever end is closer, then shift the gap into place. */
    if (pos < d->size / 2) {
        if (stl_deque_push_front(d, stl__deque_elem(d, 0)) != STL_OK) {
            return STL_ERR_NOMEM;
        }
        for (i = 1; i < pos; ++i) {
            memcpy(stl__deque_elem(d, i), stl__deque_elem(d, i + 1), d->elem_size);
        }
    } else {
        if (stl_deque_push_back(d, stl__deque_elem(d, d->size - 1)) != STL_OK) {
            return STL_ERR_NOMEM;
        }
        for (i = d->size - 2; i > pos; --i) {
            memcpy(stl__deque_elem(d, i), stl__deque_elem(d, i - 1), d->elem_size);
        }
    }
    memcpy(stl__deque_elem(d, pos), elem, d->elem_size);
    return STL_OK;
}

int stl_deque_erase(stl_deque *d, size_t pos)
{
    size_t i;

    if (d == NULL) {
        STL_REPORT_INVALID("NULL deque");
        return STL_ERR_INVALID;
    }
    if (pos >= d->size) {
        STL_REPORT_RANGE(pos, d->size);
        return STL_ERR_RANGE;
    }
    if (pos == 0) {
        stl_deque_pop_front(d);
        return STL_OK;
    }
    if (pos == d->size - 1) {
        stl_deque_pop_back(d);
        return STL_OK;
    }
    if (d->elem_dtor != NULL) {
        d->elem_dtor(stl__deque_elem(d, pos));
    }
    if (pos < d->size / 2) {
        for (i = pos; i > 0; --i) {
            memcpy(stl__deque_elem(d, i), stl__deque_elem(d, i - 1), d->elem_size);
        }
        {
            size_t slots = STL_DEQUE_SLOTS(d);
            d->start = (d->start + 1 == slots) ? 0 : (d->start + 1);
        }
    } else {
        for (i = pos; i + 1 < d->size; ++i) {
            memcpy(stl__deque_elem(d, i), stl__deque_elem(d, i + 1), d->elem_size);
        }
    }
    d->size -= 1;
    return STL_OK;
}

int stl_deque_erase_range(stl_deque *d, size_t first, size_t last)
{
    size_t n;

    if (d == NULL) {
        STL_REPORT_INVALID("NULL deque");
        return STL_ERR_INVALID;
    }
    if (first > last || last > d->size) {
        stl__set_error_at(STL_ERR_RANGE, __FILE__, __LINE__, "deque::erase_range(%lu, %lu) size %lu",
                  (unsigned long)first, (unsigned long)last, (unsigned long)d->size);
        return STL_ERR_RANGE;
    }
    n = last - first;
    while (n-- > 0) {
        (void)stl_deque_erase(d, first);
    }
    return STL_OK;
}

void stl_deque_clear(stl_deque *d)
{
    if (d != NULL) {
        d->size = 0;
        d->start = 0;
    }
}

void stl_deque_clear_ex(stl_deque *d)
{
    size_t i;

    if (d == NULL) {
        return;
    }
    if (d->elem_dtor != NULL) {
        for (i = 0; i < d->size; ++i) {
            void *p = stl__deque_elem(d, i);
            if (p != NULL) {
                d->elem_dtor(p);
            }
        }
    }
    d->size = 0;
    d->start = 0;
}

void stl_deque_shrink_to_fit(stl_deque *d)
{
    size_t i;

    if (d == NULL) {
        return;
    }
    /* Re-centre so that the live range starts at ring slot 0, then release the
     * blocks that are no longer referenced. */
    if (d->start != 0) {
        (void)stl__deque_grow(d, d->size);
    }
    for (i = 0; i < d->block_slots; ++i) {
        size_t live_blocks = (d->size + d->block_elems - 1) / d->block_elems;
        if (i >= live_blocks && d->blocks[i] != NULL) {
            stl_mem_free(d->alloc, d->blocks[i]);
            d->blocks[i] = NULL;
        }
    }
}

int stl_deque_reserve(stl_deque *d, size_t n)
{
    if (d == NULL) {
        STL_REPORT_INVALID("NULL deque");
        return STL_ERR_INVALID;
    }
    return stl__deque_grow(d, n);
}

/* ------------------------------------------------------------------ */
/* Algorithms / iteration                                              */
/* ------------------------------------------------------------------ */

void *stl_deque_find(const stl_deque *d, const void *elem, stl_equal_fn eq)
{
    size_t i;

    if (d == NULL || elem == NULL) {
        return NULL;
    }
    if (eq == NULL) {
        eq = stl_eq_mem;
    }
    for (i = 0; i < d->size; ++i) {
        void *p = stl__deque_elem((stl_deque *)d, i);
        if (p != NULL && eq(p, elem)) {
            return p;
        }
    }
    return NULL;
}

void stl_deque_foreach(stl_deque *d, stl_visit_fn fn, void *user)
{
    size_t i;

    if (d == NULL || fn == NULL) {
        return;
    }
    for (i = 0; i < d->size; ++i) {
        if (fn(stl__deque_elem(d, i), user)) {
            break;
        }
    }
}

void stl_deque_reverse(stl_deque *d)
{
    size_t i;

    if (d == NULL || d->size < 2) {
        return;
    }
    for (i = 0; i < d->size / 2; ++i) {
        stl_swap(stl__deque_elem(d, i),
                 stl__deque_elem(d, d->size - 1 - i),
                 d->elem_size);
    }
}

void stl_deque_sort(stl_deque *d, stl_compare_fn cmp)
{
    void *flat;
    size_t i;

    if (d == NULL || d->size < 2) {
        return;
    }
    /* Flatten into a temporary buffer, sort, copy back: predictable and keeps
     * the ring layout untouched. */
    flat = stl_mem_alloc(d->alloc, d->size, d->elem_size);
    if (flat == NULL) {
        return;
    }
    for (i = 0; i < d->size; ++i) {
        memcpy(STL_ELEM_AT(flat, d->elem_size, i), stl__deque_elem(d, i), d->elem_size);
    }
    stl_sort(flat, d->size, d->elem_size, (cmp != NULL) ? cmp : stl_cmp_mem);
    for (i = 0; i < d->size; ++i) {
        memcpy(stl__deque_elem(d, i), STL_ELEM_AT(flat, d->elem_size, i), d->elem_size);
    }
    stl_mem_free(d->alloc, flat);
}

void stl_deque_swap(stl_deque *a, stl_deque *b)
{
    stl_deque tmp;

    if (a == NULL || b == NULL || a == b) {
        return;
    }
    tmp = *a;
    *a = *b;
    *b = tmp;
}

/* ------------------------------------------------------------------ */
/* Iterators                                                           */
/* ------------------------------------------------------------------ */

static stl_iterator stl__deque_iter(stl_deque *d, size_t index, int past_end)
{
    stl_iterator it;
    it.owner = d;
    it.index = index;
    if (d == NULL || (past_end && index >= d->size)) {
        it.elem = NULL;
    } else {
        it.elem = stl__deque_elem(d, index);
    }
    return it;
}

stl_iterator stl_deque_begin(stl_deque *d)  { return stl__deque_iter(d, 0, 0); }
stl_iterator stl_deque_end(stl_deque *d)    { return stl__deque_iter(d, (d != NULL) ? d->size : 0, 1); }
stl_iterator stl_deque_rbegin(stl_deque *d)
{
    return stl__deque_iter(d, (d != NULL && d->size > 0) ? d->size - 1 : 0,
                           (d == NULL || d->size == 0));
}
stl_iterator stl_deque_rend(stl_deque *d)
{
    stl_iterator it = stl__deque_iter(d, 0, 0);
    it.elem = NULL;
    it.index = (size_t)-1;
    return it;
}

stl_iterator stl_deque_iter_next(stl_iterator it)
{
    stl_deque *d = (stl_deque *)it.owner;
    if (d == NULL || it.index + 1 >= d->size) {
        return stl_deque_end(d);
    }
    return stl__deque_iter(d, it.index + 1, 0);
}

stl_iterator stl_deque_iter_prev(stl_iterator it)
{
    stl_deque *d = (stl_deque *)it.owner;
    if (d == NULL) {
        return stl_iter_null();
    }
    if (it.index == 0 || it.index > d->size) {
        return stl__deque_iter(d, 0, 0);
    }
    return stl__deque_iter(d, it.index - 1, 0);
}

ptrdiff_t stl_deque_iter_distance(stl_iterator first, stl_iterator last)
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
