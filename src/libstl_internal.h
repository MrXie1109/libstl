/*
 * libstl_internal.h -- private plumbing shared by the libstl implementation.
 *
 * Not installed, not part of the public interface.  Everything in here may
 * change without notice; do not include it from user code.
 */

#ifndef LIBSTL_INTERNAL_H_INCLUDED
#define LIBSTL_INTERNAL_H_INCLUDED

/* Feature-test macros must precede any system header, so request the POSIX
 * 2008 namespace (rwlocks, nanosleep, sysconf) before including anything. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif
#if !defined(_WIN32) && !defined(_XOPEN_SOURCE)
#  define _XOPEN_SOURCE 700
#endif

/* Internal symbols -- uniformly prefixed with `stl__` -- are hidden so that
 * only the documented interface is exported from the shared library.  Symbol
 * visibility is an ELF concept: GCC and Clang accept the attribute on ELF
 * targets but warn and ignore it for Mach-O and PE/COFF, and MSVC has no
 * equivalent at all.  Gate it on the object format rather than on the
 * compiler, so MinGW and clang-cl stay warning-free. */
#if (defined(__GNUC__) || defined(__clang__)) && \
    !defined(_WIN32) && !defined(__CYGWIN__) && !defined(__APPLE__)
#  define STL_PRIVATE __attribute__((visibility("hidden")))
#else
#  define STL_PRIVATE
#endif

#include "libstl.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <limits.h>
#include <assert.h>
#include <float.h>
#include <errno.h>
#include <ctype.h>
#include <math.h>
#include <time.h>

#if STL_HAVE_PTHREAD
#  include <pthread.h>
#  include <unistd.h>
#  include <time.h>
#  include <sys/time.h>
#endif

#if STL_PLATFORM_WINDOWS
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

/* STL_STATIC_ASSERT comes from the public header. */

/* STL_UNUSED is defined in the public header. */

/* va_copy is C99/C++11; pre-standard compilers only have __va_copy (or a plain
 * assignment, which happens to work on every ABI we target). */
#if !defined(va_copy)
#  if defined(__va_copy)
#    define stl_va_copy(dst, src) __va_copy(dst, src)
#  else
#    define stl_va_copy(dst, src) ((dst) = (src))
#  endif
#else
#  define stl_va_copy(dst, src) va_copy(dst, src)
#endif

STL_STATIC_ASSERT(sizeof(char) == 1, char_is_one_byte);

/* Number of elements of `elem_size` bytes that fit in a block of `bytes`. */
#define STL_ELEMS(bytes, elem_size) ((size_t)((bytes) / (elem_size)))

/* Byte pointer arithmetic on an element array. */
#define STL_ELEM_AT(base, elem_size, i) ((void *)((stl_byte *)(base) + (size_t)(i) * (size_t)(elem_size)))

/* The address of a node's payload given the node base pointer. */
#define STL_LIST_PAYLOAD(l, node) ((void *)((stl_byte *)(node) + (l)->node_offset))

/* Min/max on size_t. */
#define STL_MIN(a, b) ((a) < (b) ? (a) : (b))
#define STL_MAX(a, b) ((a) > (b) ? (a) : (b))

/* Maximum array length we allow before refusing to allocate.  PTRDIFF_MAX is
 * C99/C++11 only, so fall back to something safe when it is unavailable. */
#if defined(PTRDIFF_MAX)
#  define STL_PTRDIFF_MAX ((ptrdiff_t)PTRDIFF_MAX)
#elif defined(SIZE_MAX)
#  define STL_PTRDIFF_MAX ((ptrdiff_t)(SIZE_MAX >> 1))
#else
#  define STL_PTRDIFF_MAX ((ptrdiff_t)0x7fffffff)
#endif
#define STL_MAX_ELEM_COUNT ((size_t)(STL_PTRDIFF_MAX / 2))

/* Byte-size for a container payload: node header + element. */
#define STL_NODE_BYTES(node_offset, elem_size) ((node_offset) + (elem_size))

/* ------------------------------------------------------------------ */
/* Default allocator shims                                             */
/* ------------------------------------------------------------------ */

STL_PRIVATE void *stl__default_malloc(size_t size);
STL_PRIVATE void *stl__default_realloc(void *ptr, size_t size);
STL_PRIVATE void  stl__default_free(void *ptr);

/* Resolve a possibly-NULL allocator to the process default. */
STL_PRIVATE const stl_allocator *stl__allocator_or_default(const stl_allocator *a);

/* Typed comparators used by containers created without an explicit one.
 * These are generic over all element sizes; see also the fixed-width builtins
 * in libstl.h (stl_cmp_int32, stl_cmp_cstr, ...). */
STL_PRIVATE int STL_CALL stl__cmp_u8(const void *a, const void *b);
STL_PRIVATE int STL_CALL stl__cmp_i32(const void *a, const void *b);
STL_PRIVATE int STL_CALL stl__cmp_i64(const void *a, const void *b);
STL_PRIVATE int STL_CALL stl__cmp_f32(const void *a, const void *b);
STL_PRIVATE int STL_CALL stl__cmp_f64(const void *a, const void *b);
/* Key elements that are themselves char* strings. */
STL_PRIVATE int STL_CALL stl__cmp_strptr(const void *a, const void *b);
STL_PRIVATE int STL_CALL stl__eq_strptr(const void *a, const void *b);
STL_PRIVATE size_t STL_CALL stl__hash_strptr(const void *key);

/* ------------------------------------------------------------------ */
/* Internal error reporting helpers                                    */
/* ------------------------------------------------------------------ */

/* Report a range/index error and return NULL / an error code. */
/* In C89 there are no variadic macros, so the formatter is reached through a
 * real function instead: stl__set_error_at() is a thin wrapper that adds the
 * source location. */
STL_PRIVATE void stl__set_error_at(stl_error_code code, const char *file, int line,
                                   const char *fmt, ...);

#if defined(__cplusplus) || (defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L))
#  define STL_REPORT_RANGE(idx, size) \
       STL_ERROR(STL_ERR_RANGE, "index %lu out of range (size %lu)", \
                 (unsigned long)(idx), (unsigned long)(size))
#  define STL_REPORT_NOMEM() \
       STL_ERROR(STL_ERR_NOMEM, "out of memory")
#  define STL_REPORT_INVALID(msg) \
       STL_ERROR(STL_ERR_INVALID, "%s", (msg))
#else
#  define STL_REPORT_RANGE(idx, size) \
       stl__set_error_at(STL_ERR_RANGE, __FILE__, __LINE__, \
                         "index %lu out of range (size %lu)", \
                         (unsigned long)(idx), (unsigned long)(size))
#  define STL_REPORT_NOMEM() \
       stl__set_error_at(STL_ERR_NOMEM, __FILE__, __LINE__, "out of memory")
#  define STL_REPORT_INVALID(msg) \
       stl__set_error_at(STL_ERR_INVALID, __FILE__, __LINE__, "%s", (msg))
#endif

/* ------------------------------------------------------------------ */
/* Sorted-array primitives shared by vector/deque sort helpers         */
/* ------------------------------------------------------------------ */

/* Merge two adjacent sorted runs [0,mid) and [mid,count) of `base`.
 * `tmp` must provide room for `mid` elements. */
void stl__merge_runs(void *base, void *tmp, size_t count, size_t mid,
                     size_t elem_size, stl_compare_fn cmp);

/* introsort core used by stl_sort and vector/deque sort. */
STL_PRIVATE void stl__introsort(void *base, size_t count, size_t elem_size, stl_compare_fn cmp);

/* Binary insertion sort: sorts [0,count) using `tmp` scratch of one element. */
STL_PRIVATE void stl__insertion_sort(void *base, size_t count, size_t elem_size, stl_compare_fn cmp);

/* Bottom-up merge sort (stable) using scratch of `count` elements. */
STL_PRIVATE void stl__merge_sort(void *base, void *tmp, size_t count, size_t elem_size, stl_compare_fn cmp);

/* Quickselect: places the n-th smallest element at position n. */
STL_PRIVATE void stl__nth_element(void *base, size_t count, size_t n, size_t elem_size, stl_compare_fn cmp);

/* Reverses an array of `count` elements. */
STL_PRIVATE void stl__reverse_array(void *base, size_t count, size_t elem_size);

/* Rotates an array left by `n` positions. */
STL_PRIVATE void stl__rotate_array(void *base, size_t count, size_t n, size_t elem_size);

/* ------------------------------------------------------------------ */
/* Hash table tuning                                                   */
/* ------------------------------------------------------------------ */

#define STL_HASH_MIN_BUCKETS 8u
#define STL_HASH_MAX_LOAD    1.0

/* SipHash-free string hashing helpers. */
STL_PRIVATE size_t stl__hash_bytes_generic(const void *data, size_t len);

/* ------------------------------------------------------------------ */
/* Cross-module accessors for the generic backing containers            */
/* ------------------------------------------------------------------ */
/*
 * set/map are thin typed layers over stl_rbtree, and hashset/hashmap over
 * stl_hashtable.  Both layers need to reach into the backing container's
 * configuration (key offset/size, comparator, policy, allocator, destructor)
 * and to convert between a node pointer and its payload.  Those helpers are
 * defined in libstl_tree.c / libstl_hash.c and declared here so that every
 * translation unit sees a prototype.
 */

STL_PRIVATE size_t               stl__rbtree_node_offset(const stl_rbtree *t);
STL_PRIVATE stl_rbtree_node     *stl__rbtree_node_from_data(const stl_rbtree *t, const void *data);
STL_PRIVATE void                *stl__rbtree_data_of(const stl_rbtree *t, stl_rbtree_node *node);
STL_PRIVATE size_t               stl__rbtree_key_offset(const stl_rbtree *t);
STL_PRIVATE size_t               stl__rbtree_key_size(const stl_rbtree *t);
STL_PRIVATE stl_compare_fn       stl__rbtree_key_cmp(const stl_rbtree *t);
STL_PRIVATE stl_rbtree_policy    stl__rbtree_policy(const stl_rbtree *t);
STL_PRIVATE const stl_allocator *stl__rbtree_allocator(const stl_rbtree *t);
STL_PRIVATE stl_dtor_fn          stl__rbtree_dtor(const stl_rbtree *t);

STL_PRIVATE size_t                 stl__map_value_offset(const stl_map *m);

STL_PRIVATE size_t                 stl__hashtable_node_offset(const stl_hashtable *h);
STL_PRIVATE stl_hashtable_node    *stl__hashtable_node_from_data(const stl_hashtable *h, const void *data);
STL_PRIVATE size_t                 stl__hashtable_key_offset(const stl_hashtable *h);
STL_PRIVATE size_t                 stl__hashtable_key_size(const stl_hashtable *h);
STL_PRIVATE size_t                 stl__hashtable_elem_size(const stl_hashtable *h);
STL_PRIVATE stl_hash_fn            stl__hashtable_hash(const stl_hashtable *h);
STL_PRIVATE stl_equal_fn           stl__hashtable_eq(const stl_hashtable *h);
STL_PRIVATE stl_hashtable_policy   stl__hashtable_policy(const stl_hashtable *h);
STL_PRIVATE const stl_allocator   *stl__hashtable_allocator(const stl_hashtable *h);
STL_PRIVATE stl_dtor_fn            stl__hashtable_dtor(const stl_hashtable *h);

/* ------------------------------------------------------------------ */
/* Debug-only structural checks                                        */
/* ------------------------------------------------------------------ */

#if !defined(NDEBUG) && defined(STL_DEBUG_INVARIANTS)
#  define STL_ASSERT_INVARIANT(cond) assert(cond)
#else
#  define STL_ASSERT_INVARIANT(cond) ((void)0)
#endif

#endif /* LIBSTL_INTERNAL_H_INCLUDED */
