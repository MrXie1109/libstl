/*
 * libstl_algo.c -- sorting, searching, set operations and numeric helpers.
 *
 * Everything here operates on raw arrays of fixed-size elements, which is the
 * lowest common denominator every container in libstl is built on.
 */

#include "libstl_internal.h"

/* ------------------------------------------------------------------ */
/* Insertion sort                                                      */
/* ------------------------------------------------------------------ */

STL_PRIVATE void stl__insertion_sort(void *base, size_t count, size_t elem_size, stl_compare_fn cmp)
{
    stl_byte *arr = (stl_byte *)base;
    stl_byte *key;
    size_t i;
    ptrdiff_t j;

    if (base == NULL || count < 2 || elem_size == 0 || cmp == NULL) {
        return;
    }

    key = (stl_byte *)malloc(elem_size);
    if (key == NULL) {
        /* Fall back to a swap-based insertion sort that needs no scratch. */
        for (i = 1; i < count; ++i) {
            size_t k = i;
            while (k > 0 && cmp(arr + k * elem_size, arr + (k - 1) * elem_size) < 0) {
                stl_swap(arr + k * elem_size, arr + (k - 1) * elem_size, elem_size);
                --k;
            }
        }
        return;
    }

    for (i = 1; i < count; ++i) {
        stl_byte *cur = arr + i * elem_size;
        memcpy(key, cur, elem_size);
        j = (ptrdiff_t)i - 1;
        while (j >= 0 && cmp(arr + (size_t)j * elem_size, key) > 0) {
            memcpy(arr + (size_t)(j + 1) * elem_size, arr + (size_t)j * elem_size, elem_size);
            --j;
        }
        memcpy(arr + (size_t)(j + 1) * elem_size, key, elem_size);
    }
    free(key);
}

void stl_insertion_sort(void *base, size_t count, size_t elem_size, stl_compare_fn cmp)
{
    stl__insertion_sort(base, count, elem_size, cmp);
}

/* ------------------------------------------------------------------ */
/* Introsort                                                           */
/* ------------------------------------------------------------------ */

#define STL_INSERTION_THRESHOLD 16

static void stl__median_of_three(stl_byte *a, stl_byte *b, stl_byte *c,
                                 size_t elem_size, stl_compare_fn cmp)
{
    /* Sort the three samples so that *b holds the median. */
    if (cmp(a, b) > 0) stl_swap(a, b, elem_size);
    if (cmp(b, c) > 0) {
        stl_swap(b, c, elem_size);
        if (cmp(a, b) > 0) stl_swap(a, b, elem_size);
    }
}

static void stl__introsort_loop(stl_byte *arr, size_t count, size_t elem_size,
                                stl_compare_fn cmp, int depth)
{
    while (count > STL_INSERTION_THRESHOLD) {
        stl_byte *pivot;
        size_t i, j;

        if (depth-- <= 0) {
            /* Too deep: switch to heapsort to guarantee O(n log n). */
            stl_heap_sort(arr, count, elem_size, cmp);
            return;
        }

        /* Median-of-three pivot, moved to position 0. */
        stl__median_of_three(arr,
                             arr + (count / 2) * elem_size,
                             arr + (count - 1) * elem_size,
                             elem_size, cmp);
        stl_swap(arr, arr + (count / 2) * elem_size, elem_size);
        pivot = arr;

        /* Hoare partition around *pivot; the pivot itself stays at index 0. */
        i = 1;
        j = count - 1;
        for (;;) {
            while (i <= j && cmp(arr + i * elem_size, pivot) <= 0) {
                ++i;
            }
            while (j > 0 && cmp(arr + j * elem_size, pivot) > 0) {
                --j;
            }
            if (i >= j) {
                break;
            }
            stl_swap(arr + i * elem_size, arr + j * elem_size, elem_size);
            ++i;
            --j;
        }
        stl_swap(pivot, arr + j * elem_size, elem_size);
        /* Now [0, j) <= [j] < [j+1, count). */

        /* Recurse into the smaller side, loop on the larger side. */
        if (j < count - j - 1) {
            stl__introsort_loop(arr, j, elem_size, cmp, depth);
            arr += (j + 1) * elem_size;
            count -= (j + 1);
        } else {
            stl__introsort_loop(arr + (j + 1) * elem_size, count - j - 1, elem_size, cmp, depth);
            count = j;
        }
    }
    stl__insertion_sort(arr, count, elem_size, cmp);
}

/* log2 approximation used for the depth limit. */
static int stl__log2_size(size_t n)
{
    int r = 0;
    while (n > 1) {
        n >>= 1;
        ++r;
    }
    return r;
}

STL_PRIVATE void stl__introsort(void *base, size_t count, size_t elem_size, stl_compare_fn cmp)
{
    if (base == NULL || count < 2 || elem_size == 0 || cmp == NULL) {
        return;
    }
    stl__introsort_loop((stl_byte *)base, count, elem_size, cmp,
                        2 * stl__log2_size(count) + 2);
}

void stl_sort(void *base, size_t count, size_t elem_size, stl_compare_fn cmp)
{
    stl__introsort(base, count, elem_size, cmp);
}

/* ------------------------------------------------------------------ */
/* Merge sort (stable)                                                 */
/* ------------------------------------------------------------------ */

STL_PRIVATE void stl__merge_runs(void *base, void *tmp, size_t count, size_t mid,
                     size_t elem_size, stl_compare_fn cmp)
{
    stl_byte *arr = (stl_byte *)base;
    stl_byte *buf = (stl_byte *)tmp;
    size_t left = 0;
    size_t right = mid;
    size_t out = 0;

    if (tmp == NULL || mid == 0 || mid >= count) {
        return;
    }
    /* `tmp` must hold the whole left run, i.e. `mid` elements. */
    memcpy(buf, arr, mid * elem_size);

    while (left < mid && right < count) {
        if (cmp(buf + left * elem_size, arr + right * elem_size) <= 0) {
            memcpy(arr + out * elem_size, buf + left * elem_size, elem_size);
            ++left;
        } else {
            memcpy(arr + out * elem_size, arr + right * elem_size, elem_size);
            ++right;
        }
        ++out;
    }
    if (left < mid) {
        memcpy(arr + out * elem_size, buf + left * elem_size, (mid - left) * elem_size);
    }
}

STL_PRIVATE void stl__merge_sort(void *base, void *tmp, size_t count, size_t elem_size, stl_compare_fn cmp)
{
    size_t width;

    if (base == NULL || count < 2 || tmp == NULL) {
        return;
    }
    /* `width` doubles each pass and is always a power of two below count, so
     * the widest left run is just under `count`; `tmp` must therefore hold
     * `count` elements.  Merge sort is inherently O(n) in scratch space. */
    for (width = 1; width < count; width *= 2) {
        size_t i;
        for (i = 0; i < count; i += 2 * width) {
            size_t mid = i + width;
            size_t end = i + 2 * width;
            if (mid >= count) {
                break;
            }
            if (end > count) {
                end = count;
            }
            stl__merge_runs((stl_byte *)base + i * elem_size, tmp,
                            end - i, mid - i, elem_size, cmp);
        }
        if (width > count / 2) {
            break;      /* guard against overflow of 2*width */
        }
    }
}

void stl_stable_sort(void *base, size_t count, size_t elem_size, stl_compare_fn cmp)
{
    void *tmp;

    if (base == NULL || count < 2 || elem_size == 0 || cmp == NULL) {
        return;
    }
    if (count <= STL_INSERTION_THRESHOLD) {
        stl__insertion_sort(base, count, elem_size, cmp);
        return;
    }
    /* A left run can be as wide as the largest power of two below `count`,
     * so reserve the full length. */
    tmp = stl_mem_alloc(NULL, count, elem_size);
    if (tmp == NULL) {
        /* Out of memory: fall back to introsort so the result is at least
         * ordered (though not guaranteed stable). */
        stl__introsort(base, count, elem_size, cmp);
        return;
    }
    stl__merge_sort(base, tmp, count, elem_size, cmp);
    stl_mem_free(NULL, tmp);
}

/* ------------------------------------------------------------------ */
/* Quickselect / nth_element / partial_sort                            */
/* ------------------------------------------------------------------ */

STL_PRIVATE void stl__nth_element(void *base, size_t count, size_t n, size_t elem_size, stl_compare_fn cmp)
{
    stl_byte *arr = (stl_byte *)base;

    if (base == NULL || count == 0 || n >= count) {
        return;
    }
    while (count > STL_INSERTION_THRESHOLD) {
        size_t i, j;
        stl__median_of_three(arr, arr + (count / 2) * elem_size,
                             arr + (count - 1) * elem_size, elem_size, cmp);
        stl_swap(arr, arr + (count / 2) * elem_size, elem_size);

        i = 1;
        j = count - 1;
        for (;;) {
            while (i <= j && cmp(arr + i * elem_size, arr) <= 0) ++i;
            while (j > 0 && cmp(arr + j * elem_size, arr) > 0) --j;
            if (i >= j) break;
            stl_swap(arr + i * elem_size, arr + j * elem_size, elem_size);
            ++i;
            --j;
        }
        stl_swap(arr, arr + j * elem_size, elem_size);

        if (n == j) {
            return;
        } else if (n < j) {
            count = j;
        } else {
            arr += (j + 1) * elem_size;
            count -= (j + 1);
            n -= (j + 1);
        }
    }
    stl__insertion_sort(arr, count, elem_size, cmp);
}

void stl_nth_element(void *base, size_t count, size_t elem_size, size_t n, stl_compare_fn cmp)
{
    if (n >= count) {
        STL_REPORT_RANGE(n, count);
        return;
    }
    stl__nth_element(base, count, n, elem_size, cmp);
}

void stl_partial_sort(void *base, size_t count, size_t elem_size, size_t n, stl_compare_fn cmp)
{
    stl_byte *arr = (stl_byte *)base;

    if (base == NULL || elem_size == 0 || cmp == NULL) {
        return;
    }
    if (n == 0) {
        return;
    }
    if (n >= count) {
        stl__introsort(base, count, elem_size, cmp);
        return;
    }
    stl__nth_element(arr, count, n, elem_size, cmp);
    stl__introsort(arr, n, elem_size, cmp);
}

/* ------------------------------------------------------------------ */
/* Heap operations                                                     */
/* ------------------------------------------------------------------ */

static void stl__sift_down(stl_byte *base, size_t start, size_t count,
                           size_t elem_size, stl_compare_fn cmp)
{
    size_t root = start;

    for (;;) {
        size_t child = 2 * root + 1;
        size_t swap_with = root;

        if (child >= count) {
            break;
        }
        if (cmp(base + child * elem_size, base + swap_with * elem_size) > 0) {
            swap_with = child;
        }
        if (child + 1 < count &&
            cmp(base + (child + 1) * elem_size, base + swap_with * elem_size) > 0) {
            swap_with = child + 1;
        }
        if (swap_with == root) {
            break;
        }
        stl_swap(base + root * elem_size, base + swap_with * elem_size, elem_size);
        root = swap_with;
    }
}

void stl_heap_make(void *base, size_t count, size_t elem_size, stl_compare_fn cmp)
{
    stl_byte *arr = (stl_byte *)base;
    size_t i;

    if (base == NULL || count < 2 || elem_size == 0 || cmp == NULL) {
        return;
    }
    for (i = count / 2; i > 0; --i) {
        stl__sift_down(arr, i - 1, count, elem_size, cmp);
    }
}

void stl_heap_push(void *base, size_t count, size_t elem_size, stl_compare_fn cmp)
{
    stl_byte *arr = (stl_byte *)base;
    size_t child;

    if (base == NULL || count < 2 || elem_size == 0 || cmp == NULL) {
        return;
    }
    child = count - 1;
    while (child > 0) {
        size_t parent = (child - 1) / 2;
        if (cmp(arr + child * elem_size, arr + parent * elem_size) > 0) {
            stl_swap(arr + child * elem_size, arr + parent * elem_size, elem_size);
            child = parent;
        } else {
            break;
        }
    }
}

void stl_heap_pop(void *base, size_t count, size_t elem_size, stl_compare_fn cmp)
{
    if (base == NULL || count < 2 || elem_size == 0 || cmp == NULL) {
        return;
    }
    /* Move the last element to the root and sift it back down. */
    stl_swap(base, (stl_byte *)base + (count - 1) * elem_size, elem_size);
    stl__sift_down((stl_byte *)base, 0, count - 1, elem_size, cmp);
}

void stl_heap_sort(void *base, size_t count, size_t elem_size, stl_compare_fn cmp)
{
    size_t end;

    if (base == NULL || count < 2 || elem_size == 0 || cmp == NULL) {
        return;
    }
    stl_heap_make(base, count, elem_size, cmp);
    for (end = count; end > 1; --end) {
        stl_swap(base, (stl_byte *)base + (end - 1) * elem_size, elem_size);
        stl__sift_down((stl_byte *)base, 0, end - 1, elem_size, cmp);
    }
}

int stl_heap_is_heap(const void *base, size_t count, size_t elem_size, stl_compare_fn cmp)
{
    const stl_byte *arr = (const stl_byte *)base;
    size_t i;

    if (count < 2) {
        return 1;
    }
    for (i = 1; i < count; ++i) {
        size_t parent = (i - 1) / 2;
        if (cmp(arr + parent * elem_size, arr + i * elem_size) < 0) {
            return 0;
        }
    }
    return 1;
}

int STL_CALL stl_default_priority_cmp(const void *a, const void *b)
{
    /* Max-heap ordering for a primitive element: compare by size_t-width.
     * Callers with a specific element type should supply their own. */
    return stl_cmp_size(a, b);
}

/* ------------------------------------------------------------------ */
/* Basic sequence algorithms                                           */
/* ------------------------------------------------------------------ */

STL_PRIVATE void stl__reverse_array(void *base, size_t count, size_t elem_size)
{
    stl_byte *arr = (stl_byte *)base;
    size_t i;

    if (base == NULL || count < 2) {
        return;
    }
    for (i = 0; i < count / 2; ++i) {
        stl_swap(arr + i * elem_size, arr + (count - 1 - i) * elem_size, elem_size);
    }
}

void *stl_reverse(void *base, size_t count, size_t elem_size)
{
    stl__reverse_array(base, count, elem_size);
    return base;
}

STL_PRIVATE void stl__rotate_array(void *base, size_t count, size_t n, size_t elem_size)
{
    stl_byte *arr = (stl_byte *)base;

    if (base == NULL || count == 0) {
        return;
    }
    n %= count;
    if (n == 0) {
        return;
    }
    /* Classic three-reversal rotation: always correct, O(n) swaps, no scratch. */
    stl__reverse_array(arr, n, elem_size);
    stl__reverse_array(arr + n * elem_size, count - n, elem_size);
    stl__reverse_array(arr, count, elem_size);
}

void *stl_rotate(void *base, size_t count, size_t elem_size, size_t n)
{
    stl__rotate_array(base, count, n, elem_size);
    return base;
}
void *stl_unique(void *base, size_t count, size_t elem_size, stl_equal_fn eq)
{
    stl_byte *arr = (stl_byte *)base;
    size_t out = 1;
    size_t i;

    if (base == NULL || count < 2 || elem_size == 0 || eq == NULL) {
        return (base == NULL) ? NULL : (void *)(arr + count * elem_size);
    }
    for (i = 1; i < count; ++i) {
        if (!eq(arr + out * elem_size - elem_size, arr + i * elem_size)) {
            if (out != i) {
                memcpy(arr + out * elem_size, arr + i * elem_size, elem_size);
            }
            ++out;
        }
    }
    return (void *)(arr + out * elem_size);
}

void *stl_remove_if(void *base, size_t count, size_t elem_size, stl_pred_fn pred, void *user)
{
    stl_byte *arr = (stl_byte *)base;
    size_t out = 0;
    size_t i;

    if (base == NULL || count == 0 || elem_size == 0 || pred == NULL) {
        return (base == NULL) ? NULL : (void *)(arr + count * elem_size);
    }
    for (i = 0; i < count; ++i) {
        if (!pred(arr + i * elem_size, user)) {
            if (out != i) {
                memcpy(arr + out * elem_size, arr + i * elem_size, elem_size);
            }
            ++out;
        }
    }
    return (void *)(arr + out * elem_size);
}

void *stl_remove(void *base, size_t count, size_t elem_size, const void *value, stl_equal_fn eq)
{
    stl_byte *arr = (stl_byte *)base;
    size_t out = 0;
    size_t i;

    if (base == NULL || count == 0 || elem_size == 0 || eq == NULL) {
        return (base == NULL) ? NULL : (void *)(arr + count * elem_size);
    }
    for (i = 0; i < count; ++i) {
        if (!eq(arr + i * elem_size, value)) {
            if (out != i) {
                memcpy(arr + out * elem_size, arr + i * elem_size, elem_size);
            }
            ++out;
        }
    }
    return (void *)(arr + out * elem_size);
}

void *stl_remove_copy_if(const void *src, size_t count, void *dst, size_t elem_size,
                         stl_pred_fn pred, void *user)
{
    const stl_byte *in = (const stl_byte *)src;
    stl_byte *out = (stl_byte *)dst;
    size_t i, w = 0;

    if (src == NULL || dst == NULL || elem_size == 0 || pred == NULL) {
        return dst;
    }
    for (i = 0; i < count; ++i) {
        if (!pred(in + i * elem_size, user)) {
            if (w != i) {
                memcpy(out + w * elem_size, in + i * elem_size, elem_size);
            }
            ++w;
        }
    }
    return out + w * elem_size;
}

void *stl_copy(const void *src, void *dst, size_t count, size_t elem_size)
{
    if (src == NULL || dst == NULL || count == 0) {
        return dst;
    }
    memmove(dst, src, count * elem_size);
    return (stl_byte *)dst + count * elem_size;
}

void *stl_copy_backward(const void *src, void *dst, size_t count, size_t elem_size)
{
    stl_byte *d;
    const stl_byte *s;

    if (src == NULL || dst == NULL || count == 0) {
        return dst;
    }
    d = (stl_byte *)dst + count * elem_size;
    s = (const stl_byte *)src + count * elem_size;
    /* memmove handles overlap correctly; back-to-front semantics preserved. */
    memmove((stl_byte *)dst, src, count * elem_size);
    (void)d;
    (void)s;
    return dst;
}

void *stl_move(const void *src, void *dst, size_t count, size_t elem_size)
{
    if (src == NULL || dst == NULL || count == 0) {
        return dst;
    }
    memmove(dst, src, count * elem_size);
    return (stl_byte *)dst + count * elem_size;
}

void stl_fill(void *base, size_t count, size_t elem_size, const void *value)
{
    stl_byte *arr = (stl_byte *)base;
    size_t i;

    if (base == NULL || elem_size == 0 || value == NULL) {
        return;
    }
    /* Fill the first element, then double the filled prefix each round. */
    if (count == 0) {
        return;
    }
    memcpy(arr, value, elem_size);
    for (i = 1; i < count; i *= 2) {
        size_t chunk = STL_MIN(i, count - i);
        memcpy(arr + i * elem_size, arr, chunk * elem_size);
    }
}

void stl_fill_n(void *base, size_t count, size_t elem_size, const void *value)
{
    stl_fill(base, count, elem_size, value);
}

void stl_iota(void *base, size_t count, size_t elem_size, const void *start, const void *step)
{
    stl_byte *arr = (stl_byte *)base;
    size_t i;

    if (base == NULL || count == 0) {
        return;
    }
    if (elem_size == sizeof(int)) {
        int v = *(const int *)start;
        int s = (step != NULL) ? *(const int *)step : 1;
        int *out = (int *)(void *)arr;
        for (i = 0; i < count; ++i) {
            out[i] = v;
            v += s;
        }
    } else if (elem_size == sizeof(double)) {
        double v = *(const double *)start;
        double s = (step != NULL) ? *(const double *)step : 1.0;
        double *out = (double *)(void *)arr;
        for (i = 0; i < count; ++i) {
            out[i] = v;
            v += s;
        }
    } else {
        /* Generic path: value + i*step using byte arithmetic on ints is not
         * meaningful, so we only support the repeated-value case. */
        for (i = 0; i < count; ++i) {
            memcpy(arr + i * elem_size, start, elem_size);
        }
    }
}

void stl_generate(void *base, size_t count, size_t elem_size, stl_generator_fn gen, void *user)
{
    stl_byte *arr = (stl_byte *)base;
    size_t i;

    if (base == NULL || gen == NULL) {
        return;
    }
    for (i = 0; i < count; ++i) {
        gen(arr + i * elem_size, user);
    }
}

int stl_equal(const void *a, const void *b, size_t count, size_t elem_size, stl_equal_fn eq)
{
    const stl_byte *pa = (const stl_byte *)a;
    const stl_byte *pb = (const stl_byte *)b;
    size_t i;

    if (a == b) {
        return 1;
    }
    if (a == NULL || b == NULL) {
        return 0;
    }
    if (eq == NULL) {
        return memcmp(a, b, count * elem_size) == 0;
    }
    for (i = 0; i < count; ++i) {
        if (!eq(pa + i * elem_size, pb + i * elem_size)) {
            return 0;
        }
    }
    return 1;
}

int stl_lexicographical_compare(const void *a, size_t acount, const void *b, size_t bcount,
                                size_t elem_size, stl_compare_fn cmp)
{
    const stl_byte *pa = (const stl_byte *)a;
    const stl_byte *pb = (const stl_byte *)b;
    size_t n = STL_MIN(acount, bcount);
    size_t i;

    if (cmp == NULL) {
        cmp = stl_cmp_mem;
    }
    for (i = 0; i < n; ++i) {
        int c = cmp(pa + i * elem_size, pb + i * elem_size);
        if (c != 0) {
            return (c < 0) ? 1 : 0;
        }
    }
    return (acount < bcount) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Searching algorithms                                                */
/* ------------------------------------------------------------------ */

size_t stl_count_if(const void *base, size_t count, size_t elem_size, stl_pred_fn pred, void *user)
{
    const stl_byte *arr = (const stl_byte *)base;
    size_t i, n = 0;

    if (base == NULL || pred == NULL) {
        return 0;
    }
    for (i = 0; i < count; ++i) {
        if (pred(arr + i * elem_size, user)) {
            ++n;
        }
    }
    return n;
}

size_t stl_count(const void *base, size_t count, size_t elem_size, const void *value, stl_equal_fn eq)
{
    const stl_byte *arr = (const stl_byte *)base;
    size_t i, n = 0;

    if (base == NULL) {
        return 0;
    }
    if (eq == NULL) {
        eq = stl_eq_mem;
    }
    for (i = 0; i < count; ++i) {
        if (eq(arr + i * elem_size, value)) {
            ++n;
        }
    }
    return n;
}

void *stl_find(const void *base, size_t count, size_t elem_size, const void *value, stl_equal_fn eq)
{
    const stl_byte *arr = (const stl_byte *)base;
    size_t i;

    if (base == NULL) {
        return NULL;
    }
    if (eq == NULL) {
        eq = stl_eq_mem;
    }
    for (i = 0; i < count; ++i) {
        if (eq(arr + i * elem_size, value)) {
            return (void *)(arr + i * elem_size);
        }
    }
    return NULL;
}

void *stl_find_if(const void *base, size_t count, size_t elem_size, stl_pred_fn pred, void *user)
{
    const stl_byte *arr = (const stl_byte *)base;
    size_t i;

    if (base == NULL || pred == NULL) {
        return NULL;
    }
    for (i = 0; i < count; ++i) {
        if (pred(arr + i * elem_size, user)) {
            return (void *)(arr + i * elem_size);
        }
    }
    return NULL;
}

int stl_all_of(const void *base, size_t count, size_t elem_size, stl_pred_fn pred, void *user)
{
    return stl_find_if(base, count, elem_size, pred, user) == NULL;
}

int stl_any_of(const void *base, size_t count, size_t elem_size, stl_pred_fn pred, void *user)
{
    if (count == 0 || base == NULL) {
        return 0;
    }
    return stl_find_if(base, count, elem_size, pred, user) != NULL;
}

int stl_none_of(const void *base, size_t count, size_t elem_size, stl_pred_fn pred, void *user)
{
    return !stl_any_of(base, count, elem_size, pred, user);
}

void *stl_find_first_of(const void *base, size_t count, size_t elem_size,
                        const void *values, size_t vcount, stl_equal_fn eq)
{
    const stl_byte *arr = (const stl_byte *)base;
    const stl_byte *vals = (const stl_byte *)values;
    size_t i, j;

    if (base == NULL || values == NULL) {
        return NULL;
    }
    if (eq == NULL) {
        eq = stl_eq_mem;
    }
    for (i = 0; i < count; ++i) {
        for (j = 0; j < vcount; ++j) {
            if (eq(arr + i * elem_size, vals + j * elem_size)) {
                return (void *)(arr + i * elem_size);
            }
        }
    }
    return NULL;
}

void *stl_adjacent_find(const void *base, size_t count, size_t elem_size, stl_equal_fn eq)
{
    const stl_byte *arr = (const stl_byte *)base;
    size_t i;

    if (base == NULL || count < 2) {
        return NULL;
    }
    if (eq == NULL) {
        eq = stl_eq_mem;
    }
    for (i = 0; i + 1 < count; ++i) {
        if (eq(arr + i * elem_size, arr + (i + 1) * elem_size)) {
            return (void *)(arr + i * elem_size);
        }
    }
    return NULL;
}

void *stl_min_element(const void *base, size_t count, size_t elem_size, stl_compare_fn cmp)
{
    const stl_byte *arr = (const stl_byte *)base;
    size_t i, best = 0;

    if (base == NULL || count == 0 || cmp == NULL) {
        return NULL;
    }
    for (i = 1; i < count; ++i) {
        if (cmp(arr + i * elem_size, arr + best * elem_size) < 0) {
            best = i;
        }
    }
    return (void *)(arr + best * elem_size);
}

void *stl_max_element(const void *base, size_t count, size_t elem_size, stl_compare_fn cmp)
{
    const stl_byte *arr = (const stl_byte *)base;
    size_t i, best = 0;

    if (base == NULL || count == 0 || cmp == NULL) {
        return NULL;
    }
    for (i = 1; i < count; ++i) {
        if (cmp(arr + best * elem_size, arr + i * elem_size) < 0) {
            best = i;
        }
    }
    return (void *)(arr + best * elem_size);
}

void stl_min_max_element(const void *base, size_t count, size_t elem_size, stl_compare_fn cmp,
                         void **min_out, void **max_out)
{
    const stl_byte *arr = (const stl_byte *)base;
    size_t i, lo = 0, hi = 0;

    if (min_out != NULL) *min_out = NULL;
    if (max_out != NULL) *max_out = NULL;
    if (base == NULL || count == 0 || cmp == NULL) {
        return;
    }
    for (i = 1; i < count; ++i) {
        if (cmp(arr + i * elem_size, arr + lo * elem_size) < 0) {
            lo = i;
        }
        if (cmp(arr + hi * elem_size, arr + i * elem_size) < 0) {
            hi = i;
        }
    }
    if (min_out != NULL) *min_out = (void *)(arr + lo * elem_size);
    if (max_out != NULL) *max_out = (void *)(arr + hi * elem_size);
}

int stl_is_sorted(const void *base, size_t count, size_t elem_size, stl_compare_fn cmp)
{
    const stl_byte *arr = (const stl_byte *)base;
    size_t i;

    if (count < 2) {
        return 1;
    }
    if (cmp == NULL) {
        cmp = stl_cmp_mem;
    }
    for (i = 1; i < count; ++i) {
        if (cmp(arr + (i - 1) * elem_size, arr + i * elem_size) > 0) {
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Transform / for_each                                                */
/* ------------------------------------------------------------------ */

void stl_for_each(void *base, size_t count, size_t elem_size, stl_unary_op_fn op, void *user)
{
    stl_byte *arr = (stl_byte *)base;
    size_t i;

    if (base == NULL || op == NULL) {
        return;
    }
    for (i = 0; i < count; ++i) {
        op(arr + i * elem_size, user);
    }
}

void *stl_transform(const void *src, void *dst, size_t count, size_t elem_size,
                    stl_unary_op_fn op, void *user)
{
    const stl_byte *in = (const stl_byte *)src;
    stl_byte *out = (stl_byte *)dst;
    size_t i;

    if (src == NULL || dst == NULL || op == NULL) {
        return dst;
    }
    for (i = 0; i < count; ++i) {
        if (in == out) {
            op(out + i * elem_size, user);
        } else {
            memcpy(out + i * elem_size, in + i * elem_size, elem_size);
            op(out + i * elem_size, user);
        }
    }
    return out + count * elem_size;
}

void *stl_transform2(const void *a, const void *b, void *dst, size_t count, size_t elem_size,
                     stl_binary_op_fn op, void *user)
{
    const stl_byte *pa = (const stl_byte *)a;
    const stl_byte *pb = (const stl_byte *)b;
    stl_byte *out = (stl_byte *)dst;
    size_t i;

    if (op == NULL || dst == NULL) {
        return dst;
    }
    for (i = 0; i < count; ++i) {
        op(pa + i * elem_size, pb + i * elem_size, out + i * elem_size, user);
    }
    return out + count * elem_size;
}

/* ------------------------------------------------------------------ */
/* Merge / set operations on sorted ranges                             */
/* ------------------------------------------------------------------ */

void *stl_merge(const void *a, size_t acount, const void *b, size_t bcount,
                void *dst, size_t elem_size, stl_compare_fn cmp)
{
    const stl_byte *pa = (const stl_byte *)a;
    const stl_byte *pb = (const stl_byte *)b;
    stl_byte *out = (stl_byte *)dst;
    size_t i = 0, j = 0, k = 0;

    if (dst == NULL) {
        return NULL;
    }
    if (cmp == NULL) {
        cmp = stl_cmp_mem;
    }
    while (i < acount && j < bcount) {
        if (cmp(pa + i * elem_size, pb + j * elem_size) <= 0) {
            memcpy(out + k * elem_size, pa + i * elem_size, elem_size);
            ++i;
        } else {
            memcpy(out + k * elem_size, pb + j * elem_size, elem_size);
            ++j;
        }
        ++k;
    }
    while (i < acount) {
        memcpy(out + k * elem_size, pa + i * elem_size, elem_size);
        ++i; ++k;
    }
    while (j < bcount) {
        memcpy(out + k * elem_size, pb + j * elem_size, elem_size);
        ++j; ++k;
    }
    return out + k * elem_size;
}

void stl_inplace_merge(void *base, size_t count, size_t mid, size_t elem_size, stl_compare_fn cmp)
{
    void *tmp;

    if (base == NULL || count < 2 || mid == 0 || mid >= count || elem_size == 0) {
        return;
    }
    if (cmp == NULL) {
        cmp = stl_cmp_mem;
    }
    tmp = stl_mem_alloc(NULL, mid, elem_size);
    if (tmp == NULL) {
        /* Fall back to sorting the whole range (not stable, but ordered). */
        stl__introsort(base, count, elem_size, cmp);
        return;
    }
    stl__merge_runs(base, tmp, count, mid, elem_size, cmp);
    stl_mem_free(NULL, tmp);
}

void *stl_set_union_raw(const void *a, size_t acount, const void *b, size_t bcount,
                        void *dst, size_t elem_size, stl_compare_fn cmp)
{
    const stl_byte *pa = (const stl_byte *)a;
    const stl_byte *pb = (const stl_byte *)b;
    stl_byte *out = (stl_byte *)dst;
    size_t i = 0, j = 0, k = 0;

    if (dst == NULL) return NULL;
    if (cmp == NULL) cmp = stl_cmp_mem;
    while (i < acount && j < bcount) {
        int c = cmp(pa + i * elem_size, pb + j * elem_size);
        if (c < 0) {
            memcpy(out + k * elem_size, pa + i * elem_size, elem_size); ++i;
        } else if (c > 0) {
            memcpy(out + k * elem_size, pb + j * elem_size, elem_size); ++j;
        } else {
            memcpy(out + k * elem_size, pa + i * elem_size, elem_size); ++i; ++j;
        }
        ++k;
    }
    while (i < acount) { memcpy(out + k * elem_size, pa + i * elem_size, elem_size); ++i; ++k; }
    while (j < bcount) { memcpy(out + k * elem_size, pb + j * elem_size, elem_size); ++j; ++k; }
    return out + k * elem_size;
}

void *stl_set_intersection_raw(const void *a, size_t acount, const void *b, size_t bcount,
                               void *dst, size_t elem_size, stl_compare_fn cmp)
{
    const stl_byte *pa = (const stl_byte *)a;
    const stl_byte *pb = (const stl_byte *)b;
    stl_byte *out = (stl_byte *)dst;
    size_t i = 0, j = 0, k = 0;

    if (dst == NULL) return NULL;
    if (cmp == NULL) cmp = stl_cmp_mem;
    while (i < acount && j < bcount) {
        int c = cmp(pa + i * elem_size, pb + j * elem_size);
        if (c < 0) {
            ++i;
        } else if (c > 0) {
            ++j;
        } else {
            memcpy(out + k * elem_size, pa + i * elem_size, elem_size);
            ++i; ++j; ++k;
        }
    }
    return out + k * elem_size;
}

void *stl_set_difference_raw(const void *a, size_t acount, const void *b, size_t bcount,
                             void *dst, size_t elem_size, stl_compare_fn cmp)
{
    const stl_byte *pa = (const stl_byte *)a;
    const stl_byte *pb = (const stl_byte *)b;
    stl_byte *out = (stl_byte *)dst;
    size_t i = 0, j = 0, k = 0;

    if (dst == NULL) return NULL;
    if (cmp == NULL) cmp = stl_cmp_mem;
    while (i < acount && j < bcount) {
        int c = cmp(pa + i * elem_size, pb + j * elem_size);
        if (c < 0) {
            memcpy(out + k * elem_size, pa + i * elem_size, elem_size); ++i; ++k;
        } else if (c > 0) {
            ++j;
        } else {
            ++i; ++j;
        }
    }
    while (i < acount) {
        memcpy(out + k * elem_size, pa + i * elem_size, elem_size); ++i; ++k;
    }
    return out + k * elem_size;
}

void *stl_set_symmetric_difference_raw(const void *a, size_t acount, const void *b, size_t bcount,
                                       void *dst, size_t elem_size, stl_compare_fn cmp)
{
    const stl_byte *pa = (const stl_byte *)a;
    const stl_byte *pb = (const stl_byte *)b;
    stl_byte *out = (stl_byte *)dst;
    size_t i = 0, j = 0, k = 0;

    if (dst == NULL) return NULL;
    if (cmp == NULL) cmp = stl_cmp_mem;
    while (i < acount && j < bcount) {
        int c = cmp(pa + i * elem_size, pb + j * elem_size);
        if (c < 0) {
            memcpy(out + k * elem_size, pa + i * elem_size, elem_size); ++i; ++k;
        } else if (c > 0) {
            memcpy(out + k * elem_size, pb + j * elem_size, elem_size); ++j; ++k;
        } else {
            ++i; ++j;
        }
    }
    while (i < acount) { memcpy(out + k * elem_size, pa + i * elem_size, elem_size); ++i; ++k; }
    while (j < bcount) { memcpy(out + k * elem_size, pb + j * elem_size, elem_size); ++j; ++k; }
    return out + k * elem_size;
}

int stl_includes(const void *a, size_t acount, const void *b, size_t bcount,
                 size_t elem_size, stl_compare_fn cmp)
{
    size_t i = 0, j = 0;
    const stl_byte *pa = (const stl_byte *)a;
    const stl_byte *pb = (const stl_byte *)b;

    if (cmp == NULL) cmp = stl_cmp_mem;
    while (i < acount && j < bcount) {
        int c = cmp(pa + i * elem_size, pb + j * elem_size);
        if (c < 0) {
            ++i;
        } else if (c == 0) {
            ++i; ++j;
        } else {
            return 0;
        }
    }
    return (j == bcount) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Accumulate / reduce                                                 */
/* ------------------------------------------------------------------ */

void *stl_accumulate(const void *base, size_t count, size_t elem_size,
                     void *result, stl_binary_op_fn op, void *user)
{
    const stl_byte *arr = (const stl_byte *)base;
    size_t i;

    if (result == NULL || op == NULL || base == NULL) {
        return result;
    }
    for (i = 0; i < count; ++i) {
        op(result, arr + i * elem_size, result, user);
    }
    return result;
}

void *stl_reduce(const void *base, size_t count, size_t elem_size,
                 const void *init, void *result, stl_binary_op_fn op, void *user)
{
    if (result == NULL) {
        return NULL;
    }
    if (init != NULL) {
        memcpy(result, init, elem_size);
    }
    if (count == 0) {
        return result;
    }
    return stl_accumulate(base, count, elem_size, result, op, user);
}

/* ------------------------------------------------------------------ */
/* Shuffle                                                             */
/* ------------------------------------------------------------------ */

void stl_shuffle(void *base, size_t count, size_t elem_size, unsigned int seed)
{
    stl_byte *arr = (stl_byte *)base;
    size_t i;

    if (base == NULL || count < 2 || elem_size == 0) {
        return;
    }
    if (seed == 0) {
        seed = stl_random_seed();
    }
    for (i = count - 1; i > 0; --i) {
        size_t j = (size_t)(stl_rand_next(&seed) % (unsigned int)(i + 1));
        stl_swap(arr + i * elem_size, arr + j * elem_size, elem_size);
    }
}

/* ------------------------------------------------------------------ */
/* Functor / numeric helpers                                           */
/* ------------------------------------------------------------------ */

long stl_sum_int(const void *base, size_t count)
{
    const int *p = (const int *)base;
    long sum = 0;
    size_t i;
    if (p == NULL) return 0;
    for (i = 0; i < count; ++i) {
        sum += p[i];
    }
    return sum;
}

double stl_sum_double(const void *base, size_t count)
{
    const double *p = (const double *)base;
    double sum = 0.0;
    size_t i;
    if (p == NULL) return 0.0;
    for (i = 0; i < count; ++i) {
        sum += p[i];
    }
    return sum;
}

double stl_mean_double(const void *base, size_t count)
{
    if (count == 0) {
        return 0.0;
    }
    return stl_sum_double(base, count) / (double)count;
}

int STL_CALL stl_pred_is_even_int(const void *elem, void *user)
{
    STL_UNUSED(user);
    return ((*(const int *)elem) % 2) == 0;
}

int STL_CALL stl_pred_is_odd_int(const void *elem, void *user)
{
    STL_UNUSED(user);
    return ((*(const int *)elem) % 2) != 0;
}

int STL_CALL stl_pred_is_positive_double(const void *elem, void *user)
{
    STL_UNUSED(user);
    return (*(const double *)elem) > 0.0;
}

int STL_CALL stl_pred_greater_than_int(const void *elem, void *user)
{
    int threshold = (user != NULL) ? *(const int *)user : 0;
    return (*(const int *)elem) > threshold;
}

void STL_CALL stl_op_negate_int(void *elem, void *user)
{
    STL_UNUSED(user);
    *(int *)elem = -(*(int *)elem);
}

void STL_CALL stl_op_double_int(void *elem, void *user)
{
    STL_UNUSED(user);
    *(int *)elem = (*(int *)elem) * 2;
}

void STL_CALL stl_op_square_int(void *elem, void *user)
{
    int v;
    STL_UNUSED(user);
    v = *(int *)elem;
    *(int *)elem = v * v;
}

void STL_CALL stl_op_add_int(const void *a, const void *b, void *result, void *user)
{
    STL_UNUSED(user);
    *(int *)result = *(const int *)a + *(const int *)b;
}

void STL_CALL stl_op_add_double(const void *a, const void *b, void *result, void *user)
{
    STL_UNUSED(user);
    *(double *)result = *(const double *)a + *(const double *)b;
}

void STL_CALL stl_op_max_int(const void *a, const void *b, void *result, void *user)
{
    int x = *(const int *)a;
    int y = *(const int *)b;
    STL_UNUSED(user);
    *(int *)result = (x > y) ? x : y;
}

void STL_CALL stl_gen_rand_int(void *elem, void *user)
{
    unsigned int *state = (unsigned int *)user;
    unsigned int local;
    if (state == NULL) {
        local = stl_random_seed();
        state = &local;
    }
    *(int *)elem = (int)(stl_rand_next(state) & 0x7fffffffu);
}

void STL_CALL stl_gen_index_int(void *elem, void *user)
{
    int *counter = (int *)user;
    if (counter == NULL) {
        *(int *)elem = 0;
        return;
    }
    *(int *)elem = (*counter)++;
}
