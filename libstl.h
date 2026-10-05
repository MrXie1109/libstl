/*
 * libstl.h -- STL (Standard Template Library) for C.
 *
 * A header + implementation pair that brings the familiar C++ STL
 * vocabulary (vector / deque / list / set / map / stack / queue /
 * priority_queue / string / algorithms) to plain C.
 *
 * Design notes
 * ------------
 *  - This is C, so there are no templates and no transparent monomorphisation.
 *    Containers are heterogeneous: they store *untyped elements* of a fixed
 *    `elem_size` given at construction time.  Every container is therefore
 *    declared with a single opaque type (e.g. `stl_vector`) and the element
 *    type is a compile-time convention between you and the container:
 *
 *        stl_vector *v = stl_vector_new(sizeof(int), NULL);
 *        int x = 42;
 *        stl_vector_push_back(v, &x);
 *        int *p = (int *)stl_vector_at(v, 0);   // -> 42
 *
 *  - Containers own their element *storage* (they memcpy elements in and out)
 *    but never own pointed-to memory.  Destruction is therefore O(1) per
 *    container: if your elements own resources, use the `elem_dtor` callback
 *    (see stl_dtor_fn) or `stl_*_clear_ex()`.
 *
 *  - A single generic ordered associative container (`stl_rbtree`) backs both
 *    `stl_set` and `stl_map`; likewise one hash table (`stl_hashtable`) backs
 *    `stl_hashset` / `stl_hashmap`.
 *
 *  - Iterators are small value types (pointer + container pointer) mirroring
 *    C++ iterator semantics: begin/end, ++/--, deref, and stable invalidation
 *    rules per container.
 *
 *  - Memory can be redirected with a single stl_allocator for the whole
 *    library (see stl_set_allocator); per-container allocators are also
 *    accepted by every `*_new_a()` constructor.
 *
 * Cross-platform
 * --------------
 *  C89-compatible declarations are provided for the whole public API; a few
 *  C99 types (`long long`, `stdint.h`) are used only when available.
 *  Compiles cleanly with GCC/Clang/MSVC, both C and C++ front ends, and is
 *  tested for 32/64-bit and big/little endian builds.  No POSIX-only API is
 *  used except `pthread_rwlock_t` in `stl_rwlock`, and that block is guarded
 *  by STL_HAVE_PTHREAD (and by STL_HAVE_PTHREAD_RWLOCK / STL_HAVE_PTHREAD_SPIN
 *  for the finer-grained cases).
 *
 * License: MIT.  See the LICENSE file in the project root.
 */

#ifndef LIBSTL_H_INCLUDED
#define LIBSTL_H_INCLUDED

#include <stddef.h>
#include <stdarg.h>
#include <string.h>   /* the macro layer uses memset/memcpy directly */

/* ------------------------------------------------------------------ */
/* Version / configuration                                             */
/* ------------------------------------------------------------------ */

#define STL_VERSION_MAJOR 1
#define STL_VERSION_MINOR 0
#define STL_VERSION_PATCH 0
#define STL_VERSION_STRING "1.0.0"
#define STL_VERSION ((STL_VERSION_MAJOR << 16) | (STL_VERSION_MINOR << 8) | STL_VERSION_PATCH)

/* ------------------------------------------------------------------ */
/* Platform detection and compiler portability                         */
/* ------------------------------------------------------------------ */

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32) || defined(_WIN64)
#  define STL_PLATFORM_WINDOWS 1
#else
#  define STL_PLATFORM_WINDOWS 0
#endif

#if defined(_MSC_VER)
#  define STL_PLATFORM_MSVC 1
#else
#  define STL_PLATFORM_MSVC 0
#endif

/* Shared library import/export decoration.
 *
 * Three cases matter:
 *
 *   MSVC, MinGW, clang-cl -- __declspec(dllexport/dllimport).  MSVC's linker
 *       only exports decorated symbols, so nothing internal leaks; the GNU
 *       MinGW driver exports everything by default, so the build additionally
 *       passes --exclude-all-symbols to restrict the export table to the
 *       symbols decorated with STL_API.
 *   ELF (GCC/Clang)       -- symbol visibility attributes.
 *   Anything else         -- no decoration.
 */
#if STL_PLATFORM_MSVC || defined(__MINGW32__) || defined(__MINGW64__)
#  if defined(STL_BUILD_SHARED)
#    define STL_API __declspec(dllexport)
#  elif defined(STL_USE_SHARED)
#    define STL_API __declspec(dllimport)
#  else
#    define STL_API
#  endif
#  define STL_INLINE static __inline__
#else
#  if defined(__GNUC__) || defined(__clang__)
#    define STL_API __attribute__((visibility("default")))
#    define STL_INLINE static __inline__
#  else
#    define STL_API
#    define STL_INLINE static
#  endif
#endif

/* C89 has no `inline` keyword, so STL_INLINE resolves to a plain static
 * function there: correct, merely not inlined.  The __inline__ spelling is a
 * reserved-identifier extension accepted by GCC, Clang and MSVC in every
 * language mode, including -std=c89 -pedantic. */
#if !defined(__cplusplus) && defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
#  undef  STL_INLINE
#  define STL_INLINE static inline
#endif

#if !defined(STL_BUILD_SHARED) && !defined(STL_USE_SHARED)
#  define STL_API_LOCAL
#else
#  define STL_API_LOCAL
#endif

/* Calling convention: override with -DSTL_CALL=__cdecl etc. if needed. */
#ifndef STL_CALL
#  define STL_CALL
#endif

/* Silence unused-parameter/variable warnings in user callbacks. */
#ifndef STL_UNUSED
#  define STL_UNUSED(x) ((void)(x))
#endif

/* Attribute for functions generated by the STL_DEFINE_* macros: they exist so
 * the user *may* reference them, so "defined but not used" is not a problem. */
#if defined(__GNUC__) || defined(__clang__)
#  define stl_maybe_unused __attribute__((unused))
#else
#  define stl_maybe_unused
#endif

/* Deprecation hint. */
#if STL_PLATFORM_MSVC
#  define STL_DEPRECATED(msg) __declspec(deprecated(msg))
#elif defined(__GNUC__) || defined(__clang__)
#  define STL_DEPRECATED(msg) __attribute__((deprecated(msg)))
#else
#  define STL_DEPRECATED(msg)
#endif

/* Feature probes. */
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
#  define STL_HAVE_C99 1
#else
#  define STL_HAVE_C99 0
#endif

#if defined(_MSC_VER) || STL_HAVE_C99
#  define STL_HAVE_INT64 1
#  include <stdint.h>
#else
#  define STL_HAVE_INT64 0
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define STL_HAVE_BUILTIN_EXPECT 1
#else
#  define STL_HAVE_BUILTIN_EXPECT 0
#endif

/* `restrict` is C99-only; C++ has no keyword for it. */
#if STL_HAVE_C99 && !defined(__cplusplus)
#  define STL_RESTRICT restrict
#else
#  define STL_RESTRICT
#endif

/* Threading primitives are optional.  Define STL_DISABLE_THREADS to remove. */
#ifndef STL_DISABLE_THREADS
#  if STL_PLATFORM_WINDOWS
#    define STL_HAVE_PTHREAD 0
#    define STL_HAVE_PTHREAD_RWLOCK 0
#    define STL_HAVE_PTHREAD_SPIN 0
#  elif defined(__has_include)
#    if __has_include(<pthread.h>)
#      define STL_HAVE_PTHREAD 1
#    else
#      define STL_HAVE_PTHREAD 0
#    endif
#  elif defined(__unix__) || defined(__APPLE__) || defined(__linux__)
#    define STL_HAVE_PTHREAD 1
#  else
#    define STL_HAVE_PTHREAD 0
#  endif
#else
#  define STL_HAVE_PTHREAD 0
#endif

#if STL_HAVE_PTHREAD
#  if defined(_POSIX_READER_WRITER_LOCKS) || defined(__USE_XOPEN2K) || defined(__linux__) || defined(__APPLE__)
#    define STL_HAVE_PTHREAD_RWLOCK 1
#  else
#    define STL_HAVE_PTHREAD_RWLOCK 0
#  endif
#  if defined(_POSIX_SPIN_LOCKS) || defined(__USE_XOPEN2K) || defined(__linux__) || defined(__APPLE__)
#    define STL_HAVE_PTHREAD_SPIN 1
#  else
#    define STL_HAVE_PTHREAD_SPIN 0
#  endif
#else
#  define STL_HAVE_PTHREAD_RWLOCK 0
#  define STL_HAVE_PTHREAD_SPIN 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Basic types                                                         */
/* ------------------------------------------------------------------ */

typedef unsigned char  stl_byte;
typedef int            stl_bool;

#ifndef STL_TRUE
#  define STL_TRUE  1
#  define STL_FALSE 0
#endif

#ifndef STL_NULL
#  define STL_NULL ((void *)0)
#endif

/* Sentinel returned by search functions when nothing is found. */
#define STL_NPOS ((size_t)-1)

/* Comparison result, mirroring the three-way ordering of C++20 or the
 * negative/zero/positive convention of memcmp/strcmp. */
typedef int stl_compare_result;
#define STL_LESS    (-1)
#define STL_EQUAL   (0)
#define STL_GREATER (1)

/* The STL's default ordering predicate, for elements of `elem_size` bytes.
 * Override per container when your type is a struct with padding. */
typedef int (STL_CALL *stl_compare_fn)(const void *a, const void *b);

/* Equality predicate: non-zero when *a == *b. */
typedef int (STL_CALL *stl_equal_fn)(const void *a, const void *b);

/* Hash function: may return any size_t. */
typedef size_t (STL_CALL *stl_hash_fn)(const void *key);

/* Destructor / callback invoked with a pointer to an element (may be NULL). */
typedef void (STL_CALL *stl_dtor_fn)(void *elem);

/* Copy hook, used by container copies to deep-copy owned pointers.
 * `dst` already holds a byte copy of `src`; return non-zero on success. */
typedef int (STL_CALL *stl_copy_fn)(void *dst, const void *src, size_t elem_size);

/* Iteration callback: return non-zero to stop early. */
typedef int (STL_CALL *stl_visit_fn)(void *elem, void *user);

/* ------------------------------------------------------------------ */
/* Allocator                                                           */
/* ------------------------------------------------------------------ */

typedef void *(STL_CALL *stl_malloc_fn)(size_t size);
typedef void *(STL_CALL *stl_realloc_fn)(void *ptr, size_t size);
typedef void  (STL_CALL *stl_free_fn)(void *ptr);

typedef struct stl_allocator {
    stl_malloc_fn  malloc_fn;   /* required */
    stl_realloc_fn realloc_fn;  /* required for vector/string; may be NULL  */
    stl_free_fn    free_fn;     /* required */
    void          *user;        /* opaque, for the callbacks above          */
} stl_allocator;

/* The process-wide default allocator (backed by malloc/realloc/free).
 * Pass NULL as an allocator anywhere to get this one. */
STL_API const stl_allocator *stl_default_allocator(void);

/* Replace the process-wide default allocator.  Returns the previous one,
 * or NULL if `alloc` is malformed (missing malloc/free).  Not thread-safe;
 * call before creating containers.  Containers capture the allocator they
 * were created with and are unaffected by later changes. */
STL_API const stl_allocator *stl_set_allocator(const stl_allocator *alloc);

/* Allocate `count` elements of `elem_size` with overflow checking. */
STL_API void *stl_mem_alloc(const stl_allocator *a, size_t count, size_t elem_size);
/* Reallocate with overflow checking; on failure the original block is kept. */
STL_API void *stl_mem_realloc(const stl_allocator *a, void *ptr, size_t count, size_t elem_size);
STL_API void  stl_mem_free(const stl_allocator *a, void *ptr);

/* Usable size for a dynamic array that must hold at least `need` elements. */
STL_API size_t stl_growth_capacity(size_t need);

/* ------------------------------------------------------------------ */
/* Error handling                                                      */
/* ------------------------------------------------------------------ */

typedef enum stl_error_code {
    STL_OK              = 0,  /* no error                                     */
    STL_ERR_NOMEM       = 1,  /* allocation failed                            */
    STL_ERR_RANGE       = 2,  /* index / iterator out of range                */
    STL_ERR_INVALID     = 3,  /* bad argument (NULL, zero elem_size, ...)     */
    STL_ERR_EMPTY       = 4,  /* operation requires a non-empty container     */
    STL_ERR_DUPLICATE   = 5,  /* key already present                          */
    STL_ERR_NOT_FOUND   = 6,  /* key absent                                   */
    STL_ERR_TYPE        = 7,  /* element size mismatch                        */
    STL_ERR_STATE       = 8,  /* container mutated during iteration, etc.     */
    STL_ERR_OVERFLOW    = 9,  /* arithmetic overflow                          */
    STL_ERR_UNSUPPORTED = 10  /* not available on this platform               */
} stl_error_code;

/* Human readable message for an error code (never NULL). */
STL_API const char *stl_error_string(stl_error_code code);

/* Error hook.  Called *before* the container operation returns, and also
 * usable directly via stl_set_error().  The default hook prints to stderr
 * when STL_VERBOSE is defined, and does nothing otherwise. */
typedef void (STL_CALL *stl_error_fn)(stl_error_code code, const char *file, int line, const char *msg);

STL_API stl_error_fn stl_set_error_handler(stl_error_fn handler);
STL_API void stl_set_error(stl_error_code code, const char *file, int line, const char *fmt, ...);
STL_API int  stl_get_error(void);           /* last error code, resets to STL_OK */
STL_API void stl_clear_error(void);

/* Variadic macros are C99; under C89 the message degenerates to a fixed
 * string so that callers need not change. */
#if defined(__cplusplus) || (defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L))
#  define STL_ERROR(code, ...) \
       stl_set_error((code), __FILE__, __LINE__, __VA_ARGS__)
#else
   /* C89 has no variadic macros: route through a real variadic function that
    * prepends the source location. */
#  define STL_ERROR(code, msg) \
       stl_set_error((code), __FILE__, __LINE__, "%s", (msg))
#endif

#if STL_HAVE_BUILTIN_EXPECT
#  define STL_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#  define STL_UNLIKELY(x) (x)
#endif

/* Contract checks used internally by the library; they report through the
 * error hook and are compiled out when NDEBUG is defined. */
#ifdef NDEBUG
#  define STL_CHECK(cond, code, msg) ((void)0)
#else
#  define STL_CHECK(cond, code, msg) \
       do { if (STL_UNLIKELY(!(cond))) STL_ERROR((code), (msg)); } while (0)
#endif

#ifndef STL_STATIC_ASSERT
#  if defined(__cplusplus) && (__cplusplus >= 201103L)
#    define STL_STATIC_ASSERT(cond, msg) static_assert((cond), #msg)
#  elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#    define STL_STATIC_ASSERT(cond, msg) _Static_assert((cond), #msg)
#  else
     /* Pre-C11 fallback.  At file scope a failing assertion shows up as a
      * compile error on the array bound; inside a function it is a warning
      * under -Wall in some compilers, which is why the standard mechanisms
      * above are strongly preferred. */
#    define STL_STATIC_ASSERT(cond, msg) \
         typedef char stl__static_assert_##msg[(cond) ? 1 : -1]
#  endif
#endif

/* ------------------------------------------------------------------ */
/* Common algorithms over raw element arrays                           */
/* ------------------------------------------------------------------ */

/* swap two elements of `elem_size` bytes */
STL_API void stl_swap(void *a, void *b, size_t elem_size);

/* sorted-range helpers (both ranges are ordered by `cmp`) */
STL_API void *stl_lower_bound(const void *key, const void *base, size_t count, size_t elem_size, stl_compare_fn cmp);
STL_API void *stl_upper_bound(const void *key, const void *base, size_t count, size_t elem_size, stl_compare_fn cmp);
STL_API void *stl_equal_range_lo(const void *key, const void *base, size_t count, size_t elem_size, stl_compare_fn cmp);
STL_API void *stl_equal_range_hi(const void *key, const void *base, size_t count, size_t elem_size, stl_compare_fn cmp);
STL_API int   stl_binary_search(const void *key, const void *base, size_t count, size_t elem_size, stl_compare_fn cmp);

/* ------------------------------------------------------------------ */
/* Common predicates and hashes                                        */
/* ------------------------------------------------------------------ */

STL_API int STL_CALL stl_cmp_int8(const void *a, const void *b);
STL_API int STL_CALL stl_cmp_uint8(const void *a, const void *b);
STL_API int STL_CALL stl_cmp_int16(const void *a, const void *b);
STL_API int STL_CALL stl_cmp_uint16(const void *a, const void *b);
STL_API int STL_CALL stl_cmp_int32(const void *a, const void *b);
STL_API int STL_CALL stl_cmp_uint32(const void *a, const void *b);
STL_API int STL_CALL stl_cmp_int64(const void *a, const void *b);
STL_API int STL_CALL stl_cmp_uint64(const void *a, const void *b);
STL_API int STL_CALL stl_cmp_float(const void *a, const void *b);
STL_API int STL_CALL stl_cmp_double(const void *a, const void *b);
STL_API int STL_CALL stl_cmp_cstr(const void *a, const void *b);
/* Lexicographic byte comparison over a known length.  When `len` is 0 the
 * comparison covers only the first byte, which makes the function usable as a
 * "compare arbitrary blobs" placeholder; prefer an explicit comparator when
 * the element type has a natural ordering. */
STL_API int STL_CALL stl_cmp_mem_len(const void *a, const void *b, size_t len);
/* Byte-wise comparison of the first `sizeof(void*)` bytes -- the default
 * ordering for containers created without a comparator. */
STL_API int STL_CALL stl_cmp_mem(const void *a, const void *b);
STL_API int STL_CALL stl_cmp_ptr(const void *a, const void *b);

STL_API int STL_CALL stl_eq_int(const void *a, const void *b);
STL_API int STL_CALL stl_eq_cstr(const void *a, const void *b);
STL_API int STL_CALL stl_eq_mem(const void *a, const void *b);
STL_API int STL_CALL stl_eq_ptr(const void *a, const void *b);

/* null-terminated string keyed by its char*, hashed by content */
STL_API size_t STL_CALL stl_hash_cstr(const void *key);
STL_API size_t STL_CALL stl_hash_mem(const void *key);   /* 32-bit FNV-1a variant */
STL_API size_t STL_CALL stl_hash_int(const void *key);
STL_API size_t STL_CALL stl_hash_int64(const void *key);
STL_API size_t STL_CALL stl_hash_ptr(const void *key);

/* generic byte-string hash for a known length (used by stl_hash_mem) */
STL_API size_t stl_hash_bytes(const void *data, size_t len);

/* ------------------------------------------------------------------ */
/* Iterator primitives                                                 */
/* ------------------------------------------------------------------ */
/*
 * Iterators are lightweight by-value structs.  Each carries a pointer to the
 * element it currently refers to and to the container that produced it, so
 * that ++/--/distance can be implemented generically.  `index` is the
 * ordinal position, which makes unordered containers (list/hashtable) still
 * report a meaningful distance().
 */
typedef struct stl_iterator {
    void *elem;         /* current element (or the past-the-end address)     */
    const void *owner;  /* the container this iterator came from (or NULL)   */
    size_t index;       /* ordinal position of `elem` within the container   */
} stl_iterator;

/* Sentinel iterator (all fields NULL/zero). */
STL_API stl_iterator stl_iter_null(void);

/* True when the iterator is the sentinel produced by stl_iter_null(). */
STL_API int stl_iter_is_null(stl_iterator it);

/* Element pointer, or NULL for a past-the-end / null iterator. */
STL_API void *stl_iter_data(stl_iterator it);

/* Compare two iterators for identity (element and owner). */
STL_API int stl_iter_equal(stl_iterator a, stl_iterator b);

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ================================================================== */
/*  vector                                                            */
/* ================================================================== */

typedef struct stl_vector stl_vector;

#ifdef __cplusplus
extern "C" {
#endif

/* Capacity / element / allocator queries. */
STL_API stl_vector *stl_vector_new(size_t elem_size, stl_dtor_fn elem_dtor);
STL_API stl_vector *stl_vector_new_a(size_t elem_size, stl_dtor_fn elem_dtor, const stl_allocator *a);
STL_API stl_vector *stl_vector_new_cap(size_t elem_size, size_t cap, stl_dtor_fn elem_dtor, const stl_allocator *a);
/* Adopt an existing array (no copy).  `cap` is elements, not bytes. */
STL_API stl_vector *stl_vector_from_array(void *data, size_t count, size_t cap,
                                          size_t elem_size, stl_dtor_fn elem_dtor,
                                          const stl_allocator *a);
STL_API void        stl_vector_free(stl_vector *v);
STL_API stl_vector *stl_vector_copy(const stl_vector *v, stl_copy_fn copy_elem);
STL_API stl_vector *stl_vector_copy_a(const stl_vector *v, stl_copy_fn copy_elem, const stl_allocator *a);

STL_API size_t stl_vector_size(const stl_vector *v);
STL_API size_t stl_vector_capacity(const stl_vector *v);
STL_API size_t stl_vector_elem_size(const stl_vector *v);
STL_API int    stl_vector_empty(const stl_vector *v);
STL_API void  *stl_vector_data(stl_vector *v);
STL_API const void *stl_vector_data_c(const stl_vector *v);
STL_API const stl_allocator *stl_vector_allocator(const stl_vector *v);

/* Capacity management.  reserve() never shrinks; shrink_to_fit() may. */
STL_API int stl_vector_reserve(stl_vector *v, size_t n);
STL_API int stl_vector_resize(stl_vector *v, size_t n);
STL_API int stl_vector_resize_v(stl_vector *v, size_t n, const void *value);
STL_API int stl_vector_shrink_to_fit(stl_vector *v);
STL_API int stl_vector_set_capacity(stl_vector *v, size_t n);   /* explicit shrink */

/* Element access. at() reports STL_ERR_RANGE and returns NULL when OOB;
 * [] is unchecked (returns NULL only for a NULL vector). */
STL_API void *stl_vector_at(stl_vector *v, size_t i);
STL_API const void *stl_vector_at_c(const stl_vector *v, size_t i);
STL_API void *stl_vector_front(stl_vector *v);
STL_API void *stl_vector_back(stl_vector *v);
STL_API void *stl_vector_index(stl_vector *v, ptrdiff_t i);     /* negative = from back */

/* Modifiers.  All return STL_OK or an error code. */
STL_API int stl_vector_push_back(stl_vector *v, const void *elem);
STL_API void stl_vector_pop_back(stl_vector *v);
STL_API int stl_vector_push_front(stl_vector *v, const void *elem);   /* O(n) */
STL_API void stl_vector_pop_front(stl_vector *v);                     /* O(n) */
STL_API int stl_vector_insert(stl_vector *v, size_t pos, const void *elem);
STL_API int stl_vector_insert_n(stl_vector *v, size_t pos, size_t count, const void *elem);
STL_API int stl_vector_insert_array(stl_vector *v, size_t pos, const void *array, size_t count);
STL_API int stl_vector_append_array(stl_vector *v, const void *array, size_t count);
STL_API int stl_vector_erase(stl_vector *v, size_t pos);
STL_API int stl_vector_erase_range(stl_vector *v, size_t first, size_t last);
STL_API void stl_vector_clear(stl_vector *v);
STL_API void stl_vector_clear_ex(stl_vector *v);   /* also calls elem_dtor */

STL_API void *stl_vector_emplace(stl_vector *v);   /* push_back an uninitialised slot, returns it */
STL_API int   stl_vector_assign(stl_vector *v, const void *array, size_t count);
STL_API int   stl_vector_assign_n(stl_vector *v, size_t count, const void *value);

/* Search / iteration. */
STL_API void *stl_vector_find(const stl_vector *v, const void *elem, stl_equal_fn eq);
STL_API size_t stl_vector_index_of(const stl_vector *v, const void *elem, stl_equal_fn eq);
STL_API void stl_vector_foreach(stl_vector *v, stl_visit_fn fn, void *user);
STL_API void stl_vector_foreach_c(const stl_vector *v, stl_visit_fn fn, void *user);

/* Algorithms (in place). */
STL_API void stl_vector_sort(stl_vector *v, stl_compare_fn cmp);
STL_API void stl_vector_stable_sort(stl_vector *v, stl_compare_fn cmp);
STL_API void stl_vector_reverse(stl_vector *v);
STL_API void stl_vector_rotate(stl_vector *v, size_t n);
STL_API void stl_vector_unique(stl_vector *v, stl_equal_fn eq);
STL_API size_t stl_vector_remove_if(stl_vector *v, int (STL_CALL *pred)(const void *, void *), void *user);
STL_API void stl_vector_fill(stl_vector *v, const void *value);
STL_API size_t stl_vector_lower_bound(const stl_vector *v, const void *key, stl_compare_fn cmp);
STL_API size_t stl_vector_upper_bound(const stl_vector *v, const void *key, stl_compare_fn cmp);
STL_API int stl_vector_contains(const stl_vector *v, const void *key, stl_compare_fn cmp);

/* Bounds-checked access helper mirroring C++'s at() throwing behavior, but
 * returning through the error hook instead. */
STL_API void *stl_vector_at_checked(stl_vector *v, size_t i, const char *file, int line);

/* Iterators */
STL_API stl_iterator stl_vector_begin(stl_vector *v);
STL_API stl_iterator stl_vector_end(stl_vector *v);
STL_API stl_iterator stl_vector_rbegin(stl_vector *v);
STL_API stl_iterator stl_vector_rend(stl_vector *v);
STL_API stl_iterator stl_vector_iter_next(stl_iterator it);
STL_API stl_iterator stl_vector_iter_prev(stl_iterator it);
STL_API ptrdiff_t    stl_vector_iter_distance(stl_iterator first, stl_iterator last);

/* Convenience macros for vector iteration; see the "convenience macros"
 * section at the bottom of this header for the complete set. */

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ================================================================== */
/*  deque  (circular buffer of fixed-size blocks)                     */
/* ================================================================== */

typedef struct stl_deque stl_deque;

#ifdef __cplusplus
extern "C" {
#endif

STL_API stl_deque *stl_deque_new(size_t elem_size, stl_dtor_fn elem_dtor);
STL_API stl_deque *stl_deque_new_a(size_t elem_size, stl_dtor_fn elem_dtor, const stl_allocator *a);
STL_API void       stl_deque_free(stl_deque *d);
STL_API stl_deque *stl_deque_copy(const stl_deque *d, stl_copy_fn copy_elem);

STL_API size_t stl_deque_size(const stl_deque *d);
STL_API int    stl_deque_empty(const stl_deque *d);
STL_API size_t stl_deque_elem_size(const stl_deque *d);
STL_API const stl_allocator *stl_deque_allocator(const stl_deque *d);

STL_API void *stl_deque_at(stl_deque *d, size_t i);
STL_API const void *stl_deque_at_c(const stl_deque *d, size_t i);
STL_API void *stl_deque_front(stl_deque *d);
STL_API void *stl_deque_back(stl_deque *d);

STL_API int  stl_deque_push_back(stl_deque *d, const void *elem);
STL_API int  stl_deque_push_front(stl_deque *d, const void *elem);
STL_API void stl_deque_pop_back(stl_deque *d);
STL_API void stl_deque_pop_front(stl_deque *d);
STL_API int  stl_deque_insert(stl_deque *d, size_t pos, const void *elem);
STL_API int  stl_deque_erase(stl_deque *d, size_t pos);
STL_API int  stl_deque_erase_range(stl_deque *d, size_t first, size_t last);
STL_API void stl_deque_clear(stl_deque *d);
STL_API void stl_deque_clear_ex(stl_deque *d);
STL_API void stl_deque_shrink_to_fit(stl_deque *d);
STL_API int  stl_deque_reserve(stl_deque *d, size_t n);

STL_API void *stl_deque_find(const stl_deque *d, const void *elem, stl_equal_fn eq);
STL_API void  stl_deque_foreach(stl_deque *d, stl_visit_fn fn, void *user);
STL_API void  stl_deque_sort(stl_deque *d, stl_compare_fn cmp);
STL_API void  stl_deque_reverse(stl_deque *d);
STL_API void  stl_deque_swap(stl_deque *a, stl_deque *b);   /* swaps contents */

STL_API stl_iterator stl_deque_begin(stl_deque *d);
STL_API stl_iterator stl_deque_end(stl_deque *d);
STL_API stl_iterator stl_deque_rbegin(stl_deque *d);
STL_API stl_iterator stl_deque_rend(stl_deque *d);
STL_API stl_iterator stl_deque_iter_next(stl_iterator it);
STL_API stl_iterator stl_deque_iter_prev(stl_iterator it);
STL_API ptrdiff_t    stl_deque_iter_distance(stl_iterator first, stl_iterator last);

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ================================================================== */
/*  list  (doubly linked list)                                        */
/* ================================================================== */

typedef struct stl_list      stl_list;
typedef struct stl_list_node  stl_list_node;

struct stl_list_node {
    stl_list_node *prev;
    stl_list_node *next;
    int            is_header;   /* non-zero only for the list's sentinel */
    /* element bytes follow the node header */
};

#ifdef __cplusplus
extern "C" {
#endif

STL_API stl_list *stl_list_new(size_t elem_size, stl_dtor_fn elem_dtor);
STL_API stl_list *stl_list_new_a(size_t elem_size, stl_dtor_fn elem_dtor, const stl_allocator *a);
STL_API void      stl_list_free(stl_list *l);
STL_API stl_list *stl_list_copy(const stl_list *l, stl_copy_fn copy_elem);

STL_API size_t stl_list_size(const stl_list *l);
STL_API int    stl_list_empty(const stl_list *l);
STL_API size_t stl_list_elem_size(const stl_list *l);
STL_API const stl_allocator *stl_list_allocator(const stl_list *l);

STL_API void *stl_list_front(stl_list *l);
STL_API void *stl_list_back(stl_list *l);
STL_API void *stl_list_at(stl_list *l, size_t i);       /* O(n) */
STL_API int   stl_list_contains(const stl_list *l, const void *elem, stl_equal_fn eq);

STL_API stl_list_node *stl_list_push_back(stl_list *l, const void *elem);
STL_API stl_list_node *stl_list_push_front(stl_list *l, const void *elem);
STL_API stl_list_node *stl_list_insert_after(stl_list *l, stl_list_node *node, const void *elem);
STL_API stl_list_node *stl_list_insert_before(stl_list *l, stl_list_node *node, const void *elem);
STL_API stl_list_node *stl_list_insert(stl_list *l, size_t pos, const void *elem);
STL_API void stl_list_pop_back(stl_list *l);
STL_API void stl_list_pop_front(stl_list *l);
STL_API int  stl_list_erase(stl_list *l, size_t pos);
STL_API int  stl_list_erase_range(stl_list *l, size_t first, size_t last);
STL_API int  stl_list_erase_node(stl_list *l, stl_list_node *node);
STL_API void stl_list_clear(stl_list *l);
STL_API void stl_list_clear_ex(stl_list *l);

STL_API int stl_list_splice(stl_list *dst, size_t dst_pos, stl_list *src);
STL_API int stl_list_merge(stl_list *a, stl_list *b, stl_compare_fn cmp);
STL_API int stl_list_sort(stl_list *l, stl_compare_fn cmp);
STL_API int stl_list_sort_stable(stl_list *l, stl_compare_fn cmp);
STL_API void stl_list_reverse(stl_list *l);
STL_API void stl_list_unique(stl_list *l, stl_equal_fn eq);
STL_API size_t stl_list_remove_if(stl_list *l, int (STL_CALL *pred)(const void *, void *), void *user);
STL_API stl_list *stl_list_slice(const stl_list *l, size_t first, size_t last);

STL_API void *stl_list_find(const stl_list *l, const void *elem, stl_equal_fn eq);
STL_API void  stl_list_foreach(stl_list *l, stl_visit_fn fn, void *user);
STL_API void  stl_list_foreach_c(const stl_list *l, stl_visit_fn fn, void *user);

/* Stable node handles: an element address obtained from a node stays valid
 * until that node is erased. */
STL_API void       *stl_list_node_data(stl_list_node *node);
STL_API stl_list_node *stl_list_node_next(stl_list_node *node);
STL_API stl_list_node *stl_list_node_prev(stl_list_node *node);
STL_API stl_list_node *stl_list_begin_node(stl_list *l);
STL_API stl_list_node *stl_list_end_node(stl_list *l);

STL_API stl_iterator stl_list_begin(stl_list *l);
STL_API stl_iterator stl_list_end(stl_list *l);
STL_API stl_iterator stl_list_rbegin(stl_list *l);
STL_API stl_iterator stl_list_rend(stl_list *l);
STL_API stl_iterator stl_list_iter_next(stl_iterator it);
STL_API stl_iterator stl_list_iter_prev(stl_iterator it);
STL_API ptrdiff_t    stl_list_iter_distance(stl_iterator first, stl_iterator last);

/* List iteration macros live in the "convenience macros" section below. */

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ================================================================== */
/*  rbtree -- generic ordered container backing set/map               */
/* ================================================================== */

typedef struct stl_rbtree     stl_rbtree;
typedef struct stl_rbtree_node stl_rbtree_node;

#ifdef __cplusplus
extern "C" {
#endif

typedef enum stl_rbtree_policy {
    STL_RBTREE_MULTI   = 0,   /* ordered multiset/multimap semantics */
    STL_RBTREE_UNIQUE  = 1    /* ordered set/map semantics          */
} stl_rbtree_policy;

/* key_offset: byte offset of the key inside an element.
 * key_size:   byte size of the key.
 * When key_size == elem_size and key_offset == 0 the element *is* the key.
 */
STL_API stl_rbtree *stl_rbtree_new(size_t elem_size, size_t key_offset, size_t key_size,
                                   stl_rbtree_policy policy, stl_compare_fn key_cmp,
                                   stl_dtor_fn elem_dtor, const stl_allocator *a);
STL_API void       stl_rbtree_free(stl_rbtree *t);
STL_API stl_rbtree *stl_rbtree_copy(const stl_rbtree *t, stl_copy_fn copy_elem);

STL_API size_t stl_rbtree_size(const stl_rbtree *t);
STL_API int    stl_rbtree_empty(const stl_rbtree *t);
STL_API size_t stl_rbtree_elem_size(const stl_rbtree *t);
STL_API size_t stl_rbtree_key_size(const stl_rbtree *t);

STL_API stl_rbtree_node *stl_rbtree_insert(stl_rbtree *t, const void *elem);
STL_API stl_rbtree_node *stl_rbtree_insert_hint(stl_rbtree *t, stl_rbtree_node *hint, const void *elem);
STL_API void *stl_rbtree_emplace(stl_rbtree *t);              /* insert uninitialised, returns elem */
STL_API int   stl_rbtree_erase(stl_rbtree *t, const void *key);
STL_API int   stl_rbtree_erase_node(stl_rbtree *t, stl_rbtree_node *node);
STL_API int   stl_rbtree_erase_range(stl_rbtree *t, const void *lo, const void *hi);
STL_API void  stl_rbtree_clear(stl_rbtree *t);
STL_API void  stl_rbtree_clear_ex(stl_rbtree *t);

STL_API stl_rbtree_node *stl_rbtree_find(stl_rbtree *t, const void *key);
STL_API const stl_rbtree_node *stl_rbtree_find_c(const stl_rbtree *t, const void *key);
STL_API size_t stl_rbtree_count(const stl_rbtree *t, const void *key);
STL_API int    stl_rbtree_contains(const stl_rbtree *t, const void *key);
STL_API stl_rbtree_node *stl_rbtree_lower_bound(stl_rbtree *t, const void *key);
STL_API stl_rbtree_node *stl_rbtree_upper_bound(stl_rbtree *t, const void *key);
STL_API void  stl_rbtree_equal_range(stl_rbtree *t, const void *key,
                                     stl_rbtree_node **first, stl_rbtree_node **last);
STL_API stl_rbtree_node *stl_rbtree_first(stl_rbtree *t);
STL_API stl_rbtree_node *stl_rbtree_last(stl_rbtree *t);
STL_API stl_rbtree_node *stl_rbtree_next(stl_rbtree_node *node);
STL_API stl_rbtree_node *stl_rbtree_prev(stl_rbtree_node *node);
STL_API void  *stl_rbtree_node_data(stl_rbtree_node *node);
STL_API int    stl_rbtree_node_is_end(const stl_rbtree *t, const stl_rbtree_node *node);

/* Validate red-black invariants (debugging aid); returns 1 when the tree is
 * a well-formed RB tree. */
STL_API int stl_rbtree_validate(const stl_rbtree *t);

STL_API stl_iterator stl_rbtree_begin(stl_rbtree *t);
STL_API stl_iterator stl_rbtree_end(stl_rbtree *t);
STL_API stl_iterator stl_rbtree_rbegin(stl_rbtree *t);
STL_API stl_iterator stl_rbtree_rend(stl_rbtree *t);
STL_API stl_iterator stl_rbtree_iter_next(stl_iterator it);
STL_API stl_iterator stl_rbtree_iter_prev(stl_iterator it);
STL_API ptrdiff_t    stl_rbtree_iter_distance(stl_iterator first, stl_iterator last);

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ================================================================== */
/*  set / multiset                                                     */
/* ================================================================== */

typedef stl_rbtree stl_set;
typedef stl_rbtree_node stl_set_node;
typedef stl_rbtree_policy stl_set_policy;
#define STL_SET_UNIQUE STL_RBTREE_UNIQUE
#define STL_SET_MULTI  STL_RBTREE_MULTI

#ifdef __cplusplus
extern "C" {
#endif

/* cmp may be NULL, in which case the element's raw bytes are compared. */
STL_API stl_set *stl_set_new(size_t elem_size, stl_compare_fn cmp, stl_dtor_fn elem_dtor);
STL_API stl_set *stl_set_new_a(size_t elem_size, stl_compare_fn cmp, stl_dtor_fn elem_dtor,
                               const stl_allocator *a);
STL_API stl_set *stl_set_new_policy(size_t elem_size, stl_compare_fn cmp, stl_set_policy policy,
                                    stl_dtor_fn elem_dtor, const stl_allocator *a);
STL_API stl_set *stl_set_from_array(const void *array, size_t count, size_t elem_size,
                                    stl_compare_fn cmp, stl_set_policy policy, const stl_allocator *a);
STL_API void    stl_set_free(stl_set *s);
STL_API stl_set *stl_set_copy(const stl_set *s, stl_copy_fn copy_elem);

STL_API size_t stl_set_size(const stl_set *s);
STL_API int    stl_set_empty(const stl_set *s);

STL_API const void *stl_set_insert(stl_set *s, const void *elem);   /* returns stored elem or NULL */
STL_API int   stl_set_erase(stl_set *s, const void *elem);
STL_API void  stl_set_clear(stl_set *s);
STL_API void  stl_set_clear_ex(stl_set *s);
STL_API const void *stl_set_find(const stl_set *s, const void *elem);
STL_API size_t stl_set_count(const stl_set *s, const void *elem);
STL_API int    stl_set_contains(const stl_set *s, const void *elem);
STL_API const void *stl_set_lower_bound(const stl_set *s, const void *elem);
STL_API const void *stl_set_upper_bound(const stl_set *s, const void *elem);
STL_API int stl_set_validate(const stl_set *s);

/* Set algebra writing into a freshly allocated destination. */
STL_API stl_set *stl_set_union(const stl_set *a, const stl_set *b, stl_copy_fn copy_elem);
STL_API stl_set *stl_set_intersection(const stl_set *a, const stl_set *b, stl_copy_fn copy_elem);
STL_API stl_set *stl_set_difference(const stl_set *a, const stl_set *b, stl_copy_fn copy_elem);
STL_API stl_set *stl_set_symmetric_difference(const stl_set *a, const stl_set *b, stl_copy_fn copy_elem);
STL_API int stl_set_is_subset(const stl_set *a, const stl_set *b);   /* a ⊆ b */

STL_API void stl_set_foreach(stl_set *s, stl_visit_fn fn, void *user);
STL_API void stl_set_foreach_c(const stl_set *s, stl_visit_fn fn, void *user);

STL_API stl_iterator stl_set_begin(stl_set *s);
STL_API stl_iterator stl_set_end(stl_set *s);
STL_API stl_iterator stl_set_rbegin(stl_set *s);
STL_API stl_iterator stl_set_rend(stl_set *s);
STL_API stl_iterator stl_set_iter_next(stl_iterator it);
STL_API stl_iterator stl_set_iter_prev(stl_iterator it);

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ================================================================== */
/*  map / multimap                                                     */
/* ================================================================== */
/*
 * Map elements are user-visible structs of the form
 *
 *     typedef struct { K key; V value; } pair;
 *
 * and `key_offset` is normally 0.  All map functions take and return the
 * address of such a pair, so `key_offset` must be 0 unless you deliberately
 * use an exotic layout.
 */
typedef stl_rbtree stl_map;
typedef stl_rbtree_node stl_map_node;
typedef stl_rbtree_policy stl_map_policy;
#define STL_MAP_UNIQUE STL_RBTREE_UNIQUE
#define STL_MAP_MULTI  STL_RBTREE_MULTI

#ifdef __cplusplus
extern "C" {
#endif

/* Key/value pair header usable at the front of any map element type. */
typedef struct stl_pair {
    void *first;    /* key   */
    void *second;   /* value */
} stl_pair;

STL_API stl_map *stl_map_new(size_t elem_size, size_t value_offset, size_t key_size,
                             stl_compare_fn key_cmp, stl_dtor_fn elem_dtor);
STL_API stl_map *stl_map_new_a(size_t elem_size, size_t value_offset, size_t key_size,
                               stl_compare_fn key_cmp, stl_dtor_fn elem_dtor,
                               const stl_allocator *a);
STL_API stl_map *stl_map_new_policy(size_t elem_size, size_t value_offset, size_t key_size,
                                    stl_compare_fn key_cmp, stl_map_policy policy,
                                    stl_dtor_fn elem_dtor, const stl_allocator *a);
STL_API void     stl_map_free(stl_map *m);
STL_API stl_map *stl_map_copy(const stl_map *m, stl_copy_fn copy_elem);

STL_API size_t stl_map_size(const stl_map *m);
STL_API int    stl_map_empty(const stl_map *m);

/* insert: `pair` points to a full element (key + value).  Returns the stored
 * element, or NULL when an equal key already exists in a unique map. */
STL_API void *stl_map_insert(stl_map *m, const void *pair);
/* put: insert-or-assign; always succeeds (unless out of memory).  Returns the
 * stored element, or NULL on failure. */
STL_API void *stl_map_put(stl_map *m, const void *key, const void *value);
STL_API int   stl_map_erase(stl_map *m, const void *key);
STL_API void  stl_map_clear(stl_map *m);
STL_API void  stl_map_clear_ex(stl_map *m);

STL_API void       *stl_map_find(stl_map *m, const void *key);
STL_API const void *stl_map_find_c(const stl_map *m, const void *key);
STL_API void       *stl_map_get(stl_map *m, const void *key);        /* value pointer or NULL */
STL_API const void *stl_map_get_c(const stl_map *m, const void *key);
/* get_or_insert: returns the value slot, inserting `default_value` if absent. */
STL_API void  *stl_map_get_or_insert(stl_map *m, const void *key, const void *default_value);
STL_API void  *stl_map_at(stl_map *m, const void *key);              /* unchecked: assumes present */
STL_API size_t stl_map_count(const stl_map *m, const void *key);
STL_API int    stl_map_contains(const stl_map *m, const void *key);
STL_API void  *stl_map_lower_bound(stl_map *m, const void *key);
STL_API void  *stl_map_upper_bound(stl_map *m, const void *key);
STL_API void   stl_map_equal_range(stl_map *m, const void *key, void **first, void **last);
STL_API int    stl_map_validate(const stl_map *m);

STL_API int stl_map_foreach(stl_map *m, stl_visit_fn fn, void *user);
STL_API int stl_map_foreach_c(const stl_map *m, stl_visit_fn fn, void *user);
STL_API int stl_map_foreach_range(stl_map *m, const void *lo, const void *hi,
                                  stl_visit_fn fn, void *user);

STL_API stl_iterator stl_map_begin(stl_map *m);
STL_API stl_iterator stl_map_end(stl_map *m);
STL_API stl_iterator stl_map_rbegin(stl_map *m);
STL_API stl_iterator stl_map_rend(stl_map *m);
STL_API stl_iterator stl_map_iter_next(stl_iterator it);
STL_API stl_iterator stl_map_iter_prev(stl_iterator it);

/* Convenience accessors for a pair element of arbitrary layout. */
STL_API void       *stl_map_key_of(const stl_map *m, void *pair);
STL_API const void *stl_map_key_of_c(const stl_map *m, const void *pair);
STL_API void       *stl_map_value_of(const stl_map *m, void *pair);
STL_API const void *stl_map_value_of_c(const stl_map *m, const void *pair);

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ================================================================== */
/*  hashtable -- generic hash container backing hashset/hashmap       */
/* ================================================================== */

typedef struct stl_hashtable      stl_hashtable;
typedef struct stl_hashtable_node stl_hashtable_node;

#ifdef __cplusplus
extern "C" {
#endif

typedef enum stl_hashtable_policy {
    STL_HASHTABLE_MULTI  = 0,
    STL_HASHTABLE_UNIQUE = 1
} stl_hashtable_policy;

STL_API stl_hashtable *stl_hashtable_new(size_t elem_size, size_t key_offset, size_t key_size,
                                         stl_hashtable_policy policy, stl_hash_fn hash,
                                         stl_equal_fn key_eq, stl_dtor_fn elem_dtor,
                                         const stl_allocator *a);
STL_API void           stl_hashtable_free(stl_hashtable *h);
STL_API stl_hashtable *stl_hashtable_copy(const stl_hashtable *h, stl_copy_fn copy_elem);

STL_API size_t stl_hashtable_size(const stl_hashtable *h);
STL_API size_t stl_hashtable_bucket_count(const stl_hashtable *h);
STL_API size_t stl_hashtable_bucket_size(const stl_hashtable *h, size_t bucket);
STL_API int    stl_hashtable_empty(const stl_hashtable *h);
STL_API double stl_hashtable_load_factor(const stl_hashtable *h);
STL_API int    stl_hashtable_reserve(stl_hashtable *h, size_t count);
STL_API int    stl_hashtable_rehash(stl_hashtable *h, size_t buckets);
STL_API void   stl_hashtable_set_max_load_factor(stl_hashtable *h, double lf);

STL_API stl_hashtable_node *stl_hashtable_insert(stl_hashtable *h, const void *elem);
STL_API void  *stl_hashtable_emplace(stl_hashtable *h);
STL_API int    stl_hashtable_erase(stl_hashtable *h, const void *key);
STL_API int    stl_hashtable_erase_node(stl_hashtable *h, stl_hashtable_node *node);
STL_API void   stl_hashtable_clear(stl_hashtable *h);
STL_API void   stl_hashtable_clear_ex(stl_hashtable *h);

STL_API stl_hashtable_node *stl_hashtable_find(stl_hashtable *h, const void *key);
STL_API const stl_hashtable_node *stl_hashtable_find_c(const stl_hashtable *h, const void *key);
STL_API size_t stl_hashtable_count(const stl_hashtable *h, const void *key);
STL_API int    stl_hashtable_contains(const stl_hashtable *h, const void *key);

STL_API void       *stl_hashtable_node_data(stl_hashtable_node *node);
STL_API stl_hashtable_node *stl_hashtable_first(stl_hashtable *h);
STL_API stl_hashtable_node *stl_hashtable_next(stl_hashtable *h, stl_hashtable_node *node);
STL_API stl_hashtable_node *stl_hashtable_bucket_head(stl_hashtable *h, size_t bucket);
STL_API stl_hashtable_node *stl_hashtable_bucket_next(stl_hashtable_node *node);

STL_API void *stl_hashtable_find_if(const stl_hashtable *h,
                                    int (STL_CALL *pred)(const void *, void *), void *user);
STL_API void  stl_hashtable_foreach(stl_hashtable *h, stl_visit_fn fn, void *user);
STL_API void  stl_hashtable_foreach_c(const stl_hashtable *h, stl_visit_fn fn, void *user);

STL_API stl_iterator stl_hashtable_begin(stl_hashtable *h);
STL_API stl_iterator stl_hashtable_end(stl_hashtable *h);
STL_API stl_iterator stl_hashtable_iter_next(stl_iterator it);
STL_API stl_iterator stl_hashtable_iter_prev(stl_iterator it);

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ================================================================== */
/*  hashset / hashmap                                                  */
/* ================================================================== */

typedef stl_hashtable stl_hashset;
typedef stl_hashtable_node stl_hashset_node;
typedef stl_hashtable stl_hashmap;
typedef stl_hashtable_node stl_hashmap_node;

#ifdef __cplusplus
extern "C" {
#endif

STL_API stl_hashset *stl_hashset_new(size_t elem_size, stl_hash_fn hash, stl_equal_fn eq,
                                     stl_dtor_fn elem_dtor);
STL_API stl_hashset *stl_hashset_new_a(size_t elem_size, stl_hash_fn hash, stl_equal_fn eq,
                                       stl_dtor_fn elem_dtor, const stl_allocator *a);
STL_API stl_hashset *stl_hashset_new_policy(size_t elem_size, stl_hash_fn hash, stl_equal_fn eq,
                                            stl_hashtable_policy policy, stl_dtor_fn elem_dtor,
                                            const stl_allocator *a);
STL_API void        stl_hashset_free(stl_hashset *s);
STL_API stl_hashset *stl_hashset_copy(const stl_hashset *s, stl_copy_fn copy_elem);

STL_API size_t stl_hashset_size(const stl_hashset *s);
STL_API int    stl_hashset_empty(const stl_hashset *s);
STL_API const void *stl_hashset_insert(stl_hashset *s, const void *elem);
STL_API int    stl_hashset_erase(stl_hashset *s, const void *elem);
STL_API void   stl_hashset_clear(stl_hashset *s);
STL_API void   stl_hashset_clear_ex(stl_hashset *s);
STL_API const void *stl_hashset_find(const stl_hashset *s, const void *elem);
STL_API size_t stl_hashset_count(const stl_hashset *s, const void *elem);
STL_API int    stl_hashset_contains(const stl_hashset *s, const void *elem);
STL_API int    stl_hashset_reserve(stl_hashset *s, size_t count);
STL_API void   stl_hashset_foreach(stl_hashset *s, stl_visit_fn fn, void *user);
STL_API void   stl_hashset_foreach_c(const stl_hashset *s, stl_visit_fn fn, void *user);

STL_API stl_hashmap *stl_hashmap_new(size_t elem_size, size_t value_offset, size_t key_size,
                                     stl_hash_fn hash, stl_equal_fn key_eq, stl_dtor_fn elem_dtor);
STL_API stl_hashmap *stl_hashmap_new_a(size_t elem_size, size_t value_offset, size_t key_size,
                                       stl_hash_fn hash, stl_equal_fn key_eq, stl_dtor_fn elem_dtor,
                                       const stl_allocator *a);
STL_API stl_hashmap *stl_hashmap_new_policy(size_t elem_size, size_t value_offset, size_t key_size,
                                            stl_hash_fn hash, stl_equal_fn key_eq,
                                            stl_hashtable_policy policy, stl_dtor_fn elem_dtor,
                                            const stl_allocator *a);
STL_API void        stl_hashmap_free(stl_hashmap *m);
STL_API stl_hashmap *stl_hashmap_copy(const stl_hashmap *m, stl_copy_fn copy_elem);

STL_API size_t stl_hashmap_size(const stl_hashmap *m);
STL_API int    stl_hashmap_empty(const stl_hashmap *m);
STL_API void  *stl_hashmap_insert(stl_hashmap *m, const void *pair);
STL_API void  *stl_hashmap_put(stl_hashmap *m, const void *key, const void *value);
STL_API int    stl_hashmap_erase(stl_hashmap *m, const void *key);
STL_API void   stl_hashmap_clear(stl_hashmap *m);
STL_API void   stl_hashmap_clear_ex(stl_hashmap *m);
STL_API void  *stl_hashmap_find(stl_hashmap *m, const void *key);
STL_API const void *stl_hashmap_find_c(const stl_hashmap *m, const void *key);
STL_API void  *stl_hashmap_get(stl_hashmap *m, const void *key);
STL_API const void *stl_hashmap_get_c(const stl_hashmap *m, const void *key);
STL_API void  *stl_hashmap_get_or_insert(stl_hashmap *m, const void *key, const void *default_value);
STL_API size_t stl_hashmap_count(const stl_hashmap *m, const void *key);
STL_API int    stl_hashmap_contains(const stl_hashmap *m, const void *key);
STL_API int    stl_hashmap_reserve(stl_hashmap *m, size_t count);
STL_API void  *stl_hashmap_key_of(const stl_hashmap *m, void *pair);
STL_API void  *stl_hashmap_value_of(const stl_hashmap *m, void *pair);
STL_API int    stl_hashmap_foreach(stl_hashmap *m, stl_visit_fn fn, void *user);
STL_API int    stl_hashmap_foreach_c(const stl_hashmap *m, stl_visit_fn fn, void *user);

STL_API stl_iterator stl_hashmap_begin(stl_hashmap *m);
STL_API stl_iterator stl_hashmap_end(stl_hashmap *m);
STL_API stl_iterator stl_hashmap_iter_next(stl_iterator it);

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ================================================================== */
/*  container adaptors: stack / queue / priority_queue                */
/* ================================================================== */

typedef struct stl_stack stl_stack;
typedef struct stl_queue stl_queue;
typedef struct stl_priority_queue stl_priority_queue;

#ifdef __cplusplus
extern "C" {
#endif

/* ---- stack (LIFO, backed by a vector) ---- */
STL_API stl_stack *stl_stack_new(size_t elem_size, stl_dtor_fn elem_dtor);
STL_API stl_stack *stl_stack_new_a(size_t elem_size, stl_dtor_fn elem_dtor, const stl_allocator *a);
STL_API void  stl_stack_free(stl_stack *s);
STL_API int   stl_stack_push(stl_stack *s, const void *elem);
STL_API void  stl_stack_pop(stl_stack *s);
STL_API void *stl_stack_top(stl_stack *s);
STL_API const void *stl_stack_top_c(const stl_stack *s);
STL_API size_t stl_stack_size(const stl_stack *s);
STL_API int   stl_stack_empty(const stl_stack *s);
STL_API void  stl_stack_clear(stl_stack *s);
STL_API void  stl_stack_clear_ex(stl_stack *s);
STL_API void  stl_stack_swap(stl_stack *a, stl_stack *b);

/* ---- queue (FIFO, backed by a deque) ---- */
STL_API stl_queue *stl_queue_new(size_t elem_size, stl_dtor_fn elem_dtor);
STL_API stl_queue *stl_queue_new_a(size_t elem_size, stl_dtor_fn elem_dtor, const stl_allocator *a);
STL_API void  stl_queue_free(stl_queue *q);
STL_API int   stl_queue_push(stl_queue *q, const void *elem);
STL_API void  stl_queue_pop(stl_queue *q);
STL_API void *stl_queue_front(stl_queue *q);
STL_API void *stl_queue_back(stl_queue *q);
STL_API const void *stl_queue_front_c(const stl_queue *q);
STL_API const void *stl_queue_back_c(const stl_queue *q);
STL_API size_t stl_queue_size(const stl_queue *q);
STL_API int   stl_queue_empty(const stl_queue *q);
STL_API void  stl_queue_clear(stl_queue *q);
STL_API void  stl_queue_clear_ex(stl_queue *q);
STL_API void  stl_queue_swap(stl_queue *a, stl_queue *b);

/* ---- priority_queue (binary heap; max-heap by default) ----
 * `cmp(a, b) < 0` means a has *higher* priority; the default comparator
 * stl_default_priority_cmp yields a max-heap for numeric element types.
 */
STL_API stl_priority_queue *stl_priority_queue_new(size_t elem_size, stl_compare_fn cmp,
                                                   stl_dtor_fn elem_dtor);
STL_API stl_priority_queue *stl_priority_queue_new_a(size_t elem_size, stl_compare_fn cmp,
                                                     stl_dtor_fn elem_dtor,
                                                     const stl_allocator *a);
STL_API void  stl_priority_queue_free(stl_priority_queue *pq);
STL_API int   stl_priority_queue_push(stl_priority_queue *pq, const void *elem);
STL_API void  stl_priority_queue_pop(stl_priority_queue *pq);
STL_API void *stl_priority_queue_top(stl_priority_queue *pq);
STL_API const void *stl_priority_queue_top_c(const stl_priority_queue *pq);
STL_API size_t stl_priority_queue_size(const stl_priority_queue *pq);
STL_API int   stl_priority_queue_empty(const stl_priority_queue *pq);
STL_API void  stl_priority_queue_clear(stl_priority_queue *pq);
STL_API void  stl_priority_queue_clear_ex(stl_priority_queue *pq);
STL_API int   stl_priority_queue_reserve(stl_priority_queue *pq, size_t n);

/* generic heap operations on raw arrays (cmp: a "less than" b) */
STL_API void stl_heap_make(void *base, size_t count, size_t elem_size, stl_compare_fn cmp);
STL_API void stl_heap_push(void *base, size_t count, size_t elem_size, stl_compare_fn cmp);
STL_API void stl_heap_pop(void *base, size_t count, size_t elem_size, stl_compare_fn cmp);
STL_API void stl_heap_sort(void *base, size_t count, size_t elem_size, stl_compare_fn cmp);
STL_API int  stl_heap_is_heap(const void *base, size_t count, size_t elem_size, stl_compare_fn cmp);
STL_API int  STL_CALL stl_default_priority_cmp(const void *a, const void *b);

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ================================================================== */
/*  string                                                             */
/* ================================================================== */
/*
 * A stl_string owns a NUL-terminated byte buffer and therefore *can* be used
 * directly with printf/strcmp/... .  Length is tracked separately so embedded
 * NUL bytes are preserved by the length-aware API.
 */
typedef struct stl_string stl_string;

#ifdef __cplusplus
extern "C" {
#endif

STL_API stl_string *stl_string_new(void);
STL_API stl_string *stl_string_new_a(const stl_allocator *a);
STL_API stl_string *stl_string_new_from(const char *cstr);
STL_API stl_string *stl_string_new_from_n(const char *data, size_t len);
STL_API stl_string *stl_string_new_fmt(const char *fmt, ...);
STL_API stl_string *stl_string_new_vfmt(const char *fmt, va_list ap);
STL_API stl_string *stl_string_new_cap(size_t cap);
STL_API void        stl_string_free(stl_string *s);
STL_API stl_string *stl_string_copy(const stl_string *s);

STL_API size_t stl_string_size(const stl_string *s);
STL_API size_t stl_string_length(const stl_string *s);
STL_API size_t stl_string_capacity(const stl_string *s);
STL_API int    stl_string_empty(const stl_string *s);
STL_API char  *stl_string_cstr(stl_string *s);
STL_API const char *stl_string_cstr_c(const stl_string *s);
STL_API char  *stl_string_data(stl_string *s);
STL_API const char *stl_string_data_c(const stl_string *s);
STL_API const stl_allocator *stl_string_allocator(const stl_string *s);

/* Character access (write access invalidates previously returned cstr for
 * capacity-changing operations, like std::string). */
STL_API char  stl_string_at(const stl_string *s, size_t i);
STL_API char *stl_string_ref(stl_string *s, size_t i);
STL_API char  stl_string_front(const stl_string *s);
STL_API char  stl_string_back(const stl_string *s);

STL_API int stl_string_reserve(stl_string *s, size_t cap);
STL_API int stl_string_resize(stl_string *s, size_t len);
STL_API int stl_string_shrink_to_fit(stl_string *s);
STL_API void stl_string_clear(stl_string *s);
STL_API void stl_string_clear_ex(stl_string *s);

STL_API int stl_string_assign(stl_string *s, const char *cstr);
STL_API int stl_string_assign_n(stl_string *s, const char *data, size_t len);
STL_API int stl_string_assign_string(stl_string *s, const stl_string *other);
STL_API int stl_string_append(stl_string *s, const char *cstr);
STL_API int stl_string_append_n(stl_string *s, const char *data, size_t len);
STL_API int stl_string_append_string(stl_string *s, const stl_string *other);
STL_API int stl_string_append_char(stl_string *s, char c);
STL_API int stl_string_push_back(stl_string *s, char c);
STL_API void stl_string_pop_back(stl_string *s);
STL_API int stl_string_append_fmt(stl_string *s, const char *fmt, ...);
STL_API int stl_string_append_vfmt(stl_string *s, const char *fmt, va_list ap);
STL_API int stl_string_prepend(stl_string *s, const char *cstr);
STL_API int stl_string_insert(stl_string *s, size_t pos, const char *cstr);
STL_API int stl_string_insert_n(stl_string *s, size_t pos, const char *data, size_t len);
STL_API int stl_string_erase(stl_string *s, size_t pos, size_t len);
STL_API int stl_string_replace(stl_string *s, size_t pos, size_t len, const char *cstr);
STL_API int stl_string_repeat(stl_string *s, const char *cstr, size_t times);

STL_API ptrdiff_t stl_string_compare(const stl_string *a, const stl_string *b);
STL_API int    stl_string_equals(const stl_string *a, const stl_string *b);
STL_API int    stl_string_equals_cstr(const stl_string *a, const char *cstr);
STL_API int    stl_string_compare_cstr(const stl_string *a, const char *cstr);

STL_API size_t stl_string_find(const stl_string *s, const char *needle, size_t pos);
STL_API size_t stl_string_rfind(const stl_string *s, const char *needle, size_t pos);
STL_API size_t stl_string_find_char(const stl_string *s, char c, size_t pos);
STL_API size_t stl_string_rfind_char(const stl_string *s, char c, size_t pos);
STL_API int    stl_string_contains(const stl_string *s, const char *needle);
STL_API int    stl_string_starts_with(const stl_string *s, const char *prefix);
STL_API int    stl_string_ends_with(const stl_string *s, const char *suffix);

STL_API stl_string *stl_string_substr(const stl_string *s, size_t pos, size_t len);
STL_API int    stl_string_substr_into(const stl_string *s, size_t pos, size_t len, stl_string *out);
STL_API char  *stl_string_to_cstr(const stl_string *s);   /* caller frees with stl_string_free_cstr */
STL_API void   stl_string_free_cstr(char *p);
STL_API int    stl_string_trim(stl_string *s);
STL_API int    stl_string_trim_left(stl_string *s);
STL_API int    stl_string_trim_right(stl_string *s);
STL_API int    stl_string_toupper(stl_string *s);
STL_API int    stl_string_tolower(stl_string *s);
STL_API void   stl_string_reverse(stl_string *s);
STL_API int    stl_string_replace_all(stl_string *s, const char *needle, const char *repl);
STL_API size_t stl_string_split(const stl_string *s, const char *sep, stl_vector *out);
STL_API int    stl_string_join(stl_string *out, const stl_vector *parts, const char *sep);
STL_API int    stl_string_foreach_token(const char *cstr, const char *sep,
                                        int (STL_CALL *fn)(const char *token, size_t len, void *user),
                                        void *user);

STL_API stl_iterator stl_string_begin(stl_string *s);
STL_API stl_iterator stl_string_end(stl_string *s);
STL_API stl_iterator stl_string_iter_next(stl_iterator it);
STL_API stl_iterator stl_string_iter_prev(stl_iterator it);

/* printf-compatible formatting into a fresh string / into a caller buffer. */
STL_API stl_string *stl_format(const char *fmt, ...);
STL_API int    stl_vsnprintf_c(char *buf, size_t cap, const char *fmt, va_list ap); /* portable wrapper */
STL_API int    stl_snprintf_c(char *buf, size_t cap, const char *fmt, ...);
STL_API char  *stl_strdup(const char *cstr);
STL_API char  *stl_strndup(const char *data, size_t len);
STL_API int    stl_strcasecmp(const char *a, const char *b);
STL_API int    stl_strncasecmp(const char *a, const char *b, size_t n);

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ================================================================== */
/*  algorithms                                                         */
/* ================================================================== */

#ifdef __cplusplus
extern "C" {
#endif

/* element-wise predicates; return non-zero when the predicate holds */
typedef int (STL_CALL *stl_pred_fn)(const void *elem, void *user);
typedef int (STL_CALL *stl_binary_pred_fn)(const void *a, const void *b, void *user);
typedef void (STL_CALL *stl_unary_op_fn)(void *elem, void *user);
typedef void (STL_CALL *stl_binary_op_fn)(const void *a, const void *b, void *result, void *user);
typedef void (STL_CALL *stl_generator_fn)(void *elem, void *user);

STL_API size_t stl_count_if(const void *base, size_t count, size_t elem_size, stl_pred_fn pred, void *user);
STL_API size_t stl_count(const void *base, size_t count, size_t elem_size, const void *value, stl_equal_fn eq);
STL_API void  *stl_find(const void *base, size_t count, size_t elem_size, const void *value, stl_equal_fn eq);
STL_API void  *stl_find_if(const void *base, size_t count, size_t elem_size, stl_pred_fn pred, void *user);
STL_API int    stl_all_of(const void *base, size_t count, size_t elem_size, stl_pred_fn pred, void *user);
STL_API int    stl_any_of(const void *base, size_t count, size_t elem_size, stl_pred_fn pred, void *user);
STL_API int    stl_none_of(const void *base, size_t count, size_t elem_size, stl_pred_fn pred, void *user);
STL_API void   stl_for_each(void *base, size_t count, size_t elem_size, stl_unary_op_fn op, void *user);
STL_API void  *stl_transform(const void *src, void *dst, size_t count, size_t elem_size, stl_unary_op_fn op, void *user);
STL_API void  *stl_transform2(const void *a, const void *b, void *dst, size_t count, size_t elem_size,
                              stl_binary_op_fn op, void *user);
STL_API void   stl_generate(void *base, size_t count, size_t elem_size, stl_generator_fn gen, void *user);
STL_API void  *stl_copy(const void *src, void *dst, size_t count, size_t elem_size);
STL_API void  *stl_copy_backward(const void *src, void *dst, size_t count, size_t elem_size);
STL_API void  *stl_move(const void *src, void *dst, size_t count, size_t elem_size);
STL_API void   stl_fill(void *base, size_t count, size_t elem_size, const void *value);
STL_API void   stl_fill_n(void *base, size_t count, size_t elem_size, const void *value); /* alias */
STL_API void   stl_iota(void *base, size_t count, size_t elem_size, const void *start, const void *step);
STL_API int    stl_equal(const void *a, const void *b, size_t count, size_t elem_size, stl_equal_fn eq);
STL_API void  *stl_find_first_of(const void *base, size_t count, size_t elem_size,
                                 const void *values, size_t vcount, stl_equal_fn eq);
STL_API void  *stl_adjacent_find(const void *base, size_t count, size_t elem_size, stl_equal_fn eq);
STL_API int    stl_lexicographical_compare(const void *a, size_t acount, const void *b, size_t bcount,
                                           size_t elem_size, stl_compare_fn cmp);
STL_API void  *stl_min_element(const void *base, size_t count, size_t elem_size, stl_compare_fn cmp);
STL_API void  *stl_max_element(const void *base, size_t count, size_t elem_size, stl_compare_fn cmp);
STL_API void   stl_min_max_element(const void *base, size_t count, size_t elem_size, stl_compare_fn cmp,
                                   void **min_out, void **max_out);
STL_API void  *stl_reverse(void *base, size_t count, size_t elem_size);
STL_API void  *stl_rotate(void *base, size_t count, size_t elem_size, size_t n);
STL_API void  *stl_unique(void *base, size_t count, size_t elem_size, stl_equal_fn eq); /* returns new end */
STL_API void  *stl_remove(void *base, size_t count, size_t elem_size, const void *value, stl_equal_fn eq);
STL_API void  *stl_remove_if(void *base, size_t count, size_t elem_size, stl_pred_fn pred, void *user);
STL_API void  *stl_remove_copy_if(const void *src, size_t count, void *dst, size_t elem_size,
                                  stl_pred_fn pred, void *user);

STL_API void   stl_sort(void *base, size_t count, size_t elem_size, stl_compare_fn cmp);
STL_API void   stl_stable_sort(void *base, size_t count, size_t elem_size, stl_compare_fn cmp);
STL_API void   stl_partial_sort(void *base, size_t count, size_t elem_size, size_t n, stl_compare_fn cmp);
STL_API void   stl_nth_element(void *base, size_t count, size_t elem_size, size_t n, stl_compare_fn cmp);
STL_API int    stl_is_sorted(const void *base, size_t count, size_t elem_size, stl_compare_fn cmp);
STL_API void  *stl_merge(const void *a, size_t acount, const void *b, size_t bcount,
                         void *dst, size_t elem_size, stl_compare_fn cmp);
STL_API void   stl_inplace_merge(void *base, size_t count, size_t mid, size_t elem_size, stl_compare_fn cmp);
STL_API void  *stl_set_union_raw(const void *a, size_t acount, const void *b, size_t bcount,
                                 void *dst, size_t elem_size, stl_compare_fn cmp);
STL_API void  *stl_set_intersection_raw(const void *a, size_t acount, const void *b, size_t bcount,
                                        void *dst, size_t elem_size, stl_compare_fn cmp);
STL_API void  *stl_set_difference_raw(const void *a, size_t acount, const void *b, size_t bcount,
                                      void *dst, size_t elem_size, stl_compare_fn cmp);
STL_API void  *stl_set_symmetric_difference_raw(const void *a, size_t acount, const void *b, size_t bcount,
                                                void *dst, size_t elem_size, stl_compare_fn cmp);
STL_API int    stl_includes(const void *a, size_t acount, const void *b, size_t bcount,
                            size_t elem_size, stl_compare_fn cmp);
STL_API void  *stl_accumulate(const void *base, size_t count, size_t elem_size,
                              void *result, stl_binary_op_fn op, void *user);
STL_API void  *stl_reduce(const void *base, size_t count, size_t elem_size,
                          const void *init, void *result, stl_binary_op_fn op, void *user);

/* shuffle: `seed` == 0 selects an arbitrary seed */
STL_API void stl_shuffle(void *base, size_t count, size_t elem_size, unsigned int seed);

/* Insertion sort, used internally and exposed for small ranges. */
STL_API void stl_insertion_sort(void *base, size_t count, size_t elem_size, stl_compare_fn cmp);

/* Convenience numeric comparators (aliases documented in libstl.c). */
STL_API int STL_CALL stl_cmp_long(const void *a, const void *b);
STL_API int STL_CALL stl_cmp_ulong(const void *a, const void *b);
STL_API int STL_CALL stl_cmp_size(const void *a, const void *b);
STL_API int STL_CALL stl_cmp_double_desc(const void *a, const void *b); /* reverse order */

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ================================================================== */
/*  functors / binders (poor man's <functional>)                       */
/* ================================================================== */

#ifdef __cplusplus
extern "C" {
#endif

/* A generic callable: either a C function pointer plus user data, or a
 * comparison target.  Useful as the `user` argument of foreach helpers. */
typedef struct stl_functor {
    void *fn;
    void *user;
} stl_functor;

/* Simple numeric algorithms on raw arrays (element size is the natural type
 * size; results are written as pointers, like stl_accumulate). */
STL_API long   stl_sum_int(const void *base, size_t count);
STL_API double stl_sum_double(const void *base, size_t count);
STL_API double stl_mean_double(const void *base, size_t count);

/* Function objects for the common raw-array algorithms. */
STL_API int STL_CALL stl_pred_is_even_int(const void *elem, void *user);
STL_API int STL_CALL stl_pred_is_odd_int(const void *elem, void *user);
STL_API int STL_CALL stl_pred_is_positive_double(const void *elem, void *user);
STL_API int STL_CALL stl_pred_greater_than_int(const void *elem, void *user); /* user -> int* */
STL_API void STL_CALL stl_op_negate_int(void *elem, void *user);
STL_API void STL_CALL stl_op_double_int(void *elem, void *user);
STL_API void STL_CALL stl_op_square_int(void *elem, void *user);
STL_API void STL_CALL stl_op_add_int(const void *a, const void *b, void *result, void *user);
STL_API void STL_CALL stl_op_add_double(const void *a, const void *b, void *result, void *user);
STL_API void STL_CALL stl_op_max_int(const void *a, const void *b, void *result, void *user);
STL_API void STL_CALL stl_gen_rand_int(void *elem, void *user);   /* user -> unsigned* seed */
STL_API void STL_CALL stl_gen_index_int(void *elem, void *user);  /* user -> int* counter   */

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ================================================================== */
/*  bit operations on dynamic bitsets                                  */
/* ================================================================== */

typedef struct stl_bitset stl_bitset;

#ifdef __cplusplus
extern "C" {
#endif

STL_API stl_bitset *stl_bitset_new(size_t nbits);
STL_API stl_bitset *stl_bitset_new_a(size_t nbits, const stl_allocator *a);
STL_API void stl_bitset_free(stl_bitset *b);
STL_API stl_bitset *stl_bitset_copy(const stl_bitset *b);

STL_API size_t stl_bitset_size(const stl_bitset *b);       /* number of bits     */
STL_API size_t stl_bitset_count(const stl_bitset *b);      /* number of set bits */
STL_API void   stl_bitset_set(stl_bitset *b, size_t i);
STL_API void   stl_bitset_reset(stl_bitset *b, size_t i);
STL_API void   stl_bitset_flip(stl_bitset *b, size_t i);
STL_API void   stl_bitset_set_all(stl_bitset *b);
STL_API void   stl_bitset_reset_all(stl_bitset *b);
STL_API void   stl_bitset_flip_all(stl_bitset *b);
STL_API int    stl_bitset_test(const stl_bitset *b, size_t i);
STL_API int    stl_bitset_none(const stl_bitset *b);
STL_API int    stl_bitset_any(const stl_bitset *b);
STL_API int    stl_bitset_all(const stl_bitset *b);
STL_API int    stl_bitset_equals(const stl_bitset *a, const stl_bitset *b);
STL_API int    stl_bitset_and(stl_bitset *r, const stl_bitset *a, const stl_bitset *b);
STL_API int    stl_bitset_or(stl_bitset *r, const stl_bitset *a, const stl_bitset *b);
STL_API int    stl_bitset_xor(stl_bitset *r, const stl_bitset *a, const stl_bitset *b);
STL_API int    stl_bitset_not(stl_bitset *r, const stl_bitset *a);
STL_API size_t stl_bitset_find_first(const stl_bitset *b);
STL_API size_t stl_bitset_find_next(const stl_bitset *b, size_t from);
STL_API void   stl_bitset_foreach(const stl_bitset *b, int (STL_CALL *fn)(size_t index, void *user), void *user);
STL_API stl_string *stl_bitset_to_string(const stl_bitset *b);

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ================================================================== */
/*  threaded helpers: thread-safe wrappers and parallel algorithms     */
/* ================================================================== */

typedef struct stl_spinlock stl_spinlock;
typedef struct stl_rwlock   stl_rwlock;

#ifdef __cplusplus
extern "C" {
#endif

/* --- spinlock (atomic test-and-set, no OS dependency) --- */
STL_API stl_spinlock *stl_spinlock_new(void);
STL_API void stl_spinlock_free(stl_spinlock *lock);
STL_API void stl_spinlock_lock(stl_spinlock *lock);
STL_API void stl_spinlock_unlock(stl_spinlock *lock);
STL_API int  stl_spinlock_trylock(stl_spinlock *lock);
STL_API void stl_spinlock_lock_scoped_begin(stl_spinlock *lock);   /* alias of lock   */
STL_API void stl_spinlock_lock_scoped_end(stl_spinlock *lock);     /* alias of unlock */

/* --- mutex / rwlock; on platforms without pthreads these degrade to
 *     spinlock semantics and set STL_ERR_UNSUPPORTED from the *_init path
 *     only when queried through stl_rwlock_is_native(). --- */
STL_API int  stl_rwlock_init(stl_rwlock *lock);
STL_API void stl_rwlock_destroy(stl_rwlock *lock);
STL_API void stl_rwlock_rdlock(stl_rwlock *lock);
STL_API void stl_rwlock_wrlock(stl_rwlock *lock);
STL_API int  stl_rwlock_tryrdlock(stl_rwlock *lock);
STL_API int  stl_rwlock_trywrlock(stl_rwlock *lock);
STL_API void stl_rwlock_unlock(stl_rwlock *lock);
STL_API int  stl_rwlock_is_native(void);
/* Allocate a heap rwlock; returns NULL when unavailable. */
STL_API stl_rwlock *stl_rwlock_new(void);
STL_API void stl_rwlock_free(stl_rwlock *lock);

/* --- thread-safe container wrappers --- */
typedef struct stl_safe_vector stl_safe_vector;
typedef struct stl_safe_map    stl_safe_map;

STL_API stl_safe_vector *stl_safe_vector_new(size_t elem_size);
STL_API void  stl_safe_vector_free(stl_safe_vector *v);
STL_API int   stl_safe_vector_push_back(stl_safe_vector *v, const void *elem);
STL_API int   stl_safe_vector_pop_back(stl_safe_vector *v, void *out);
STL_API size_t stl_safe_vector_size(stl_safe_vector *v);
STL_API int   stl_safe_vector_at(stl_safe_vector *v, size_t i, void *out);
STL_API int   stl_safe_vector_clear(stl_safe_vector *v);
STL_API int   stl_safe_vector_snapshot(stl_safe_vector *v, void *out, size_t count);

/* thread-safe int->int map keyed by an integer key */
STL_API stl_safe_map *stl_safe_map_new(void);
STL_API void  stl_safe_map_free(stl_safe_map *m);
STL_API int   stl_safe_map_put(stl_safe_map *m, int key, int value);
STL_API int   stl_safe_map_get(stl_safe_map *m, int key, int *out);
STL_API int   stl_safe_map_erase(stl_safe_map *m, int key);
STL_API size_t stl_safe_map_size(stl_safe_map *m);

/* --- parallel algorithms (no-ops running single threaded when threads are
 *     unavailable) --- */
typedef void (STL_CALL *stl_range_fn)(void *base, size_t begin, size_t end, void *user);

/* Run fn(base, i0, i1) over `count` elements split across `nthreads` threads;
 * pass 0 to use stl_hardware_concurrency().  Returns the number of threads
 * actually used. */
STL_API int stl_parallel_for_each(void *base, size_t count, size_t elem_size,
                                  stl_range_fn fn, void *user, int nthreads);
STL_API int stl_parallel_sort(void *base, size_t count, size_t elem_size,
                              stl_compare_fn cmp, int nthreads);
STL_API int stl_parallel_merge_sort(void *base, size_t count, size_t elem_size,
                                    stl_compare_fn cmp, int nthreads);
STL_API int stl_parallel_transform(const void *src, void *dst, size_t count, size_t elem_size,
                                   stl_unary_op_fn op, void *user, int nthreads);

/* --- misc utilities --- */
STL_API int         stl_hardware_concurrency(void);
STL_API unsigned long stl_thread_id(void);
STL_API unsigned int  stl_random_seed(void);
STL_API unsigned int  stl_rand_next(unsigned int *state);      /* xorshift32 */
STL_API double        stl_rand_double(unsigned int *state);    /* [0,1)      */
STL_API int           stl_rand_range(unsigned int *state, int lo, int hi);
STL_API void          stl_sleep_ms(unsigned int ms);

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ================================================================== */
/*  version / diagnostics                                              */
/* ================================================================== */

#ifdef __cplusplus
extern "C" {
#endif

STL_API const char *stl_version_string(void);
STL_API int         stl_version(void);
STL_API int         stl_is_thread_supported(void);
STL_API size_t      stl_size_of_pointer(void);
/* Fill `buf` with a dump of the internal representation of `container`,
 * dispatching on `kind`.  Returns the number of bytes written (excluding the
 * terminating NUL) or (size_t)-1 on error. */
STL_API size_t stl_dump(void *container, const char *kind, char *buf, size_t cap);

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ================================================================== */
/*  convenience macros                                                 */
/* ================================================================== */
/*
 * Requirements: the macro layer below is written in C99 (declarations inside
 * `for` initialisers and, for a few helpers, statement expressions are
 * avoided on purpose).  The *library* API works from C89 onward; only these
 * macros need C99.  Build with -DSTL_NO_CONVENIENCE_MACROS to exclude them
 * entirely when compiling as C89.
 */
#if defined(__cplusplus)
#  define STL_HAVE_CONVENIENCE_MACROS 1
#elif !defined(__STDC_VERSION__) || (__STDC_VERSION__ < 199901L)
   /* C89/C90: no declarations in `for` initialisers. */
#  define STL_HAVE_CONVENIENCE_MACROS 0
#else
#  define STL_HAVE_CONVENIENCE_MACROS 1
#endif

#ifdef STL_NO_CONVENIENCE_MACROS
#  undef STL_HAVE_CONVENIENCE_MACROS
#  define STL_HAVE_CONVENIENCE_MACROS 0
#endif

#if STL_HAVE_CONVENIENCE_MACROS
/*
 * Everything below is a macro, so nothing here adds symbols to the library.
 * The design goals are:
 *
 *   1. every macro is a single statement or a `for` loop, so it is safe in an
 *      unbraced `if`/`else`;
 *   2. arguments are evaluated exactly once, so side effects such as `*p++`
 *      or `f()` behave predictably;
 *   3. the loop/statement forms bind a fresh scope and use names prefixed with
 *      `stl__`, which keeps them out of the way of user identifiers.
 *
 * The `*_FOREACH` family iterates the *current* elements.  Appending to the
 * container you are iterating is fine (the end is recomputed each step);
 * erasing from a vector or deque while iterating is not, because those
 * containers move their elements.
 */

/* ------------------------------------------------------------------ */
/* Optional scope-exit cleanup                                         */
/* ------------------------------------------------------------------ */
/*
 * `cleanup` is a GCC/Clang extension with no portable equivalent, so it is
 * strictly opt-in: define STL_ENABLE_CLEANUP to turn it on.  Without that
 * switch -- and therefore on MSVC or any other compiler -- `stl_autofree`
 * expands to nothing and the variable behaves like an ordinary pointer that
 * the caller must free.  Do not rely on automatic release unless the build is
 * known to enable it; stl_free_all(), STL_FREE_ALL() and the container
 * destructors work everywhere.
 *
 *     #define STL_ENABLE_CLEANUP
 *     #include "libstl.h"
 *     ...
 *     int *buf stl_autofree = stl_new_array(int, 128);
 */
#if defined(STL_ENABLE_CLEANUP) && (defined(__GNUC__) || defined(__clang__))
#  define STL_HAVE_CLEANUP 1
#  define stl_cleanup(fn)   __attribute__((cleanup(fn)))
#  define stl_autofree      stl_cleanup(stl__autofree_shim)
#else
#  define STL_HAVE_CLEANUP 0
#  define stl_cleanup(fn)
#  define stl_autofree
#endif

/* ------------------------------------------------------------------ */
/* Boilerplate reduction                                               */
/* ------------------------------------------------------------------ */

/* stl_new(T, ...)              -- C++-ish "new T": allocates and zeroes one T.
 * stl_new_array(T, n)          -- allocates a zeroed array of n elements of T.
 * stl_delete(p)                -- counterpart of stl_new / stl_new_array.
 * stl_offsetof(T, member)      -- offsetof with a cast to size_t.
 *
 * stl_new uses stl_mem_alloc, so it honours the library's default allocator
 * and gets the overflow checks for free.  stl_delete must only be used on
 * memory that came from stl_new / stl_new_array (it pairs with the default
 * allocator). */
#define stl_new(T)               stl__new_impl(sizeof(T))
#define stl_new_array(T, n)      stl__new_array_impl((size_t)(n), sizeof(T))
#define stl_delete(p)            stl_mem_free(NULL, (void *)(p))
#define stl_offsetof(T, member)  ((size_t)(offsetof(T, member)))



/* stl_free_all(...) -- free several heap blocks at once; NULLs are ignored.
 * Note that C passes arguments by value, so this cannot reset the caller's
 * variables: assign NULL yourself if the pointers outlive the call. */
#define stl_free_all(...)                                                   \
    do {                                                                    \
        void *stl__ptrs[] = { __VA_ARGS__ };                               \
        size_t stl__i;                                                      \
        for (stl__i = 0; stl__i < sizeof(stl__ptrs) / sizeof(stl__ptrs[0]); \
             ++stl__i) {                                                    \
            if (stl__ptrs[stl__i] != NULL) {                                \
                stl_mem_free(NULL, stl__ptrs[stl__i]);                      \
            }                                                               \
        }                                                                   \
    } while (0)

/* stl_delete_all(p, ...) -- delete and clear up to 8 pointers in place. */
#define stl_delete_all(...)                                                 \
    do {                                                                    \
        void *stl__ptrs[] = { __VA_ARGS__ };                               \
        size_t stl__i;                                                      \
        for (stl__i = 0; stl__i < sizeof(stl__ptrs) / sizeof(stl__ptrs[0]); \
             ++stl__i) {                                                    \
            stl_mem_free(NULL, stl__ptrs[stl__i]);                          \
        }                                                                   \
    } while (0)

/* Backing helpers for stl_new / stl_new_array.  They allocate through the
 * library allocator and zero the block, so `stl_new(T)` behaves like C++'s
 * `new T` for plain data types. */
STL_INLINE void *stl__new_impl(size_t size)
{
    void *p = stl_mem_alloc(NULL, 1, size);
    if (p != NULL) {
        memset(p, 0, size);
    }
    return p;
}

STL_INLINE void *stl__new_array_impl(size_t count, size_t size)
{
    void *p = stl_mem_alloc(NULL, count, size);
    if (p != NULL) {
        memset(p, 0, count * size);
    }
    return p;
}

/* ------------------------------------------------------------------ */
/* Bounds-checked, type-checked accessors                              */
/* ------------------------------------------------------------------ */

/* The container element size must match sizeof(T); otherwise the generic
 * accessor is skipped and the macro yields NULL.  This catches the most common
 * C-with-STL mistake -- iterating with the wrong element type. */
#define stl_vector_at_t(v, T, i)                                              \
    ((sizeof(T) == stl_vector_elem_size(v)) ? (T *)stl_vector_at((v), (i)) : (T *)0)

#define stl_vector_at_checked_t(v, T, i)                                      \
    ((sizeof(T) == stl_vector_elem_size(v))                                 \
     ? (T *)stl_vector_at_checked((v), (i), __FILE__, __LINE__)             \
     : (T *)0)

#define stl_deque_at_t(d, T, i)   ((T *)stl_deque_at((d), (i)))
#define stl_list_at_t(l, T, i)    ((T *)stl_list_at((l), (i)))

#define stl_vector_front_t(v, T)  ((T *)stl_vector_front(v))
#define stl_vector_back_t(v, T)   ((T *)stl_vector_back(v))
#define stl_deque_front_t(d, T)   ((T *)stl_deque_front(d))
#define stl_deque_back_t(d, T)    ((T *)stl_deque_back(d))
#define stl_list_front_t(l, T)    ((T *)stl_list_front(l))
#define stl_list_back_t(l, T)     ((T *)stl_list_back(l))
#define stl_stack_top_t(s, T)     ((T *)stl_stack_top(s))
#define stl_queue_front_t(q, T)   ((T *)stl_queue_front(q))
#define stl_queue_back_t(q, T)    ((T *)stl_queue_back(q))
#define stl_pq_top_t(pq, T)       ((T *)stl_priority_queue_top(pq))

/* ------------------------------------------------------------------ */
/* Push helpers: build a temporary T from a value expression           */
/* ------------------------------------------------------------------ */

/* stl_vector_push(v, T, value) / stl_vector_push_literal(v, T, ...)
 *
 * The first form is for any expression assignable to T; the second builds the
 * temporary from a brace initialiser, which is handy for struct elements:
 *
 *     stl_vector_push_literal(v, point, { .x = 1, .y = 2 });
 */
#define stl_vector_push(v, T, value)                                        \
    do {                                                                    \
        T stl__tmp = (value);                                               \
        (void)stl_vector_push_back((v), &stl__tmp);                         \
    } while (0)

#define stl_vector_push_literal(v, T, ...)                                  \
    do {                                                                    \
        T stl__tmp = __VA_ARGS__;                                           \
        (void)stl_vector_push_back((v), &stl__tmp);                         \
    } while (0)

#define stl_deque_push_back_t(d, T, value)                                    \
    do { T stl__tmp = (value); (void)stl_deque_push_back((d), &stl__tmp); } while (0)
#define stl_deque_push_front_t(d, T, value)                                   \
    do { T stl__tmp = (value); (void)stl_deque_push_front((d), &stl__tmp); } while (0)
#define stl_list_push_back_t(l, T, value)                                     \
    do { T stl__tmp = (value); (void)stl_list_push_back((l), &stl__tmp); } while (0)
#define stl_list_push_front_t(l, T, value)                                    \
    do { T stl__tmp = (value); (void)stl_list_push_front((l), &stl__tmp); } while (0)
#define stl_stack_push_t(s, T, value)                                         \
    do { T stl__tmp = (value); (void)stl_stack_push((s), &stl__tmp); } while (0)
#define stl_queue_push_t(q, T, value)                                         \
    do { T stl__tmp = (value); (void)stl_queue_push((q), &stl__tmp); } while (0)
#define stl_pq_push_t(pq, T, value)                                           \
    do { T stl__tmp = (value); (void)stl_priority_queue_push((pq), &stl__tmp); } while (0)

/* Pop wrappers that also hand the element back (where the container can). */
#define stl_stack_pop_t(s, T, out)                                         \
    do {                                                                    \
        T *stl__p = (T *)stl_stack_top(s);                                  \
        if (stl__p != NULL) { (out) = *stl__p; stl_stack_pop(s); }          \
    } while (0)

#define stl_queue_pop_t(q, T, out)                                         \
    do {                                                                    \
        T *stl__p = (T *)stl_queue_front(q);                                \
        if (stl__p != NULL) { (out) = *stl__p; stl_queue_pop(q); }          \
    } while (0)

#define stl_pq_pop_t(pq, T, out)                                           \
    do {                                                                    \
        T *stl__p = (T *)stl_priority_queue_top(pq);                        \
        if (stl__p != NULL) { (out) = *stl__p; stl_priority_queue_pop(pq); } \
    } while (0)

/* ------------------------------------------------------------------ */
/* Construction helpers for associative containers                     */
/* ------------------------------------------------------------------ */

/* Comparator selector: given a field type of a known size, expand to the
 * matching built-in comparator.  Written as a chain of `sizeof` comparisons so
 * it works with any struct member without needing a type name.
 *
 * stl_cmp_field(T, field)     -- compare only that field
 * stl_cmp_elem(T)             -- compare the whole element (integral/float)
 */
#define stl_cmp_field(T, field)                                             \
    stl__cmp_select(sizeof(((T *)0)->field) == 1 ? 1 :                      \
                    sizeof(((T *)0)->field) == 2 ? 2 :                      \
                    sizeof(((T *)0)->field) == 4 ? 4 :                      \
                    sizeof(((T *)0)->field) == 8 ? 8 : 0)

#define stl_cmp_elem(T)         stl__cmp_select(sizeof(T))

/* The built-in comparators routed through a single inline dispatcher, so that
 * the macro above never needs a cast between function pointer types. */
STL_INLINE stl_compare_fn stl__cmp_select(int width)
{
    switch (width) {
    case 1:  return stl_cmp_int8;
    case 2:  return stl_cmp_int16;
    case 4:  return stl_cmp_int32;
    case 8:  return stl_cmp_int64;
    default: return stl_cmp_mem;
    }
}

/* Comparator that treats the element as a char* and orders by string content. */
#define STL_CMP_STR  ((stl_compare_fn)stl_cmp_cstr)
#define STL_EQ_STR   ((stl_equal_fn)stl_eq_cstr)
#define STL_HASH_STR ((stl_hash_fn)stl_hash_cstr)

#define stl_set_new_t(T, field)                                               \
    stl_set_new(sizeof(T), stl_cmp_field(T, field), NULL)

#define stl_set_new_alloc(T, field, alloc)                                  \
    stl_set_new_a(sizeof(T), stl_cmp_field(T, field), NULL, (alloc))

/* Set whose elements are themselves NUL-terminated strings. */
#define stl_strset_new()                                                    \
    stl_set_new(sizeof(char *), STL_CMP_STR, NULL)

/* Ordered map from K to V, keyed by the first member. */
#define STL_MAP_NEW(K, V)                                                   \
    stl_map_new(sizeof(struct { K stl__k; V stl__v; }),                     \
                stl_offsetof(struct { K stl__k; V stl__v; }, stl__v),       \
                sizeof(K), stl_cmp_field(struct { K stl__k; V stl__v; }, stl__k), NULL)

/* The macro above cannot be used for `put` because the anonymous struct type
 * differs at each use site.  Declare a named pair type instead and use the
 * STL_PAIR / STL_MAP_* macros below -- that is the supported pattern. */
#define STL_PAIR(K, V)          struct { K key; V value; }

/* Define a named pair type together with its comparison and hash helpers.
 * Usage:
 *
 *     stl_define_pair(int_pair, int, int);
 *     stl_map *m = stl_pair_map_new(int_pair, int);
 *     int_pair p = stl_pair_make(int_pair, 1, 2);
 *     stl_map_put(m, &p.key, &p.value);
 */
#define stl_define_pair(name, K, V)                                         \
    typedef struct name { K key; V value; } name;                           \
    static int STL_CALL name##_cmp(const void *a, const void *b)            \
        stl_maybe_unused;                                                   \
    static int STL_CALL name##_cmp(const void *a, const void *b)            \
    {                                                                       \
        return stl__cmp_select((int)sizeof(((name *)0)->key))(a, b);        \
    }                                                                       \
    static stl_compare_fn name##_key_cmp(void) stl_maybe_unused;            \
    static stl_compare_fn name##_key_cmp(void) { return name##_cmp; }

#define stl_pair_make(type, k, v)   ((type){ (k), (v) })

/* Map over a named pair type whose key is the `key` member. */
#define stl_pair_map_new(type, K)                                           \
    stl_map_new(sizeof(type), stl_offsetof(type, value), sizeof(K),         \
                stl_cmp_field(type, key), NULL)

#define stl_pair_hashmap_new(type, K, hash_fn, eq_fn)                       \
    stl_hashmap_new(sizeof(type), stl_offsetof(type, value), sizeof(K),     \
                    (hash_fn), (eq_fn), NULL)

/* Convenience: put a value using a plain key expression (named pair type). */
#define stl_map_put_pair(m, type, k, v)                                     \
    do {                                                                    \
        type stl__tmp;                                                      \
        stl__tmp.key = (k);                                                 \
        stl__tmp.value = (v);                                               \
        (void)stl_map_put((m), &stl__tmp.key, &stl__tmp.value);             \
    } while (0)

#define stl_hashmap_put_pair(m, type, k, v)                                 \
    do {                                                                    \
        type stl__tmp;                                                      \
        stl__tmp.key = (k);                                                 \
        stl__tmp.value = (v);                                               \
        (void)stl_hashmap_put((m), &stl__tmp.key, &stl__tmp.value);         \
    } while (0)

/* ------------------------------------------------------------------ */
/* Iteration                                                           */
/* ------------------------------------------------------------------ */

/* Range/keyword-for style iteration over every container kind.  The variable
 * is a T* pointing at the live element, and mutating the element through it is
 * supported. */
#define stl_vector_foreach_t(v, T, var)                                       \
    for (T *var = (T *)stl_vector_data(v);                                  \
         (var) != (T *)stl_vector_data(v) + stl_vector_size(v);             \
         ++(var))

/* Indexed variant; `idx` is a size_t holding the current position. */
#define stl_vector_foreach_idx(v, T, var, idx)                              \
    for (size_t idx = 0;                                                    \
         idx < stl_vector_size(v) && ((var) = (T *)stl_vector_at(v, idx)) != NULL; \
         ++idx)

/* Iterate with a copy of the element, so the loop body may mutate or erase. */
#define stl_vector_foreach_copy(v, T, var)                                  \
    for (size_t stl__i_##var = 0;                                           \
         stl__i_##var < stl_vector_size(v) &&                               \
             ((var) = *(T *)stl_vector_at(v, stl__i_##var), 1);             \
         ++stl__i_##var)

#define stl_deque_foreach_t(d, T, var)                                        \
    for (size_t stl__i_##var = 0;                                           \
         stl__i_##var < stl_deque_size(d) &&                                \
             ((var) = (T *)stl_deque_at(d, stl__i_##var)) != NULL;          \
         ++stl__i_##var)

#define stl_list_foreach_t(l, T, var)                                         \
    for (stl_list_node *stl__n_##var = stl_list_begin_node(l);              \
         stl__n_##var != NULL && ((var) = (T *)stl_list_node_data(stl__n_##var)) != NULL; \
         stl__n_##var = stl_list_node_next(stl__n_##var))

/* Iterate while allowing `stl_list_erase_node(l, node)` inside the body: the
 * successor node is cached *before* the body runs, so erasing `node` is safe.
 *
 * `node` is the current stl_list_node* and `var` is a T* to its element.  When
 * the next node is NULL the loop ends, so the final iteration is the last
 * element -- exactly the semantics of the plain macro. */
#define stl_list_foreach_safe(l, T, var, node)                              \
    for (stl_list_node *node = stl_list_begin_node(l), *stl__next_##node = NULL; \
         node != NULL && ((var) = (T *)stl_list_node_data(node)) != NULL && \
             (stl__next_##node = stl_list_node_next(node), 1);              \
         node = stl__next_##node)

/* Same, but also tolerates inserting before/after the current node. */
#define stl_list_foreach_mutable(l, T, var, node)                           \
    stl_list_foreach_safe(l, T, var, node)

#define stl_set_foreach_t(s, T, var)                                          \
    for (stl_iterator stl__it_##var = stl_set_begin(s);                     \
         ((var) = (T *)stl_iter_data(stl__it_##var)) != NULL;               \
         stl__it_##var = stl_set_iter_next(stl__it_##var))

#define stl_map_foreach_t(m, T, var)                                          \
    for (stl_iterator stl__it_##var = stl_map_begin(m);                     \
         ((var) = (T *)stl_iter_data(stl__it_##var)) != NULL;               \
         stl__it_##var = stl_map_iter_next(stl__it_##var))

#define stl_hashset_foreach_t(s, T, var)                                      \
    for (stl_iterator stl__it_##var = stl_hashset_begin(s);                 \
         ((var) = (T *)stl_iter_data(stl__it_##var)) != NULL;               \
         stl__it_##var = stl_hashtable_iter_next(stl__it_##var))

#define stl_hashmap_foreach_t(m, T, var)                                      \
    for (stl_iterator stl__it_##var = stl_hashmap_begin(m);                 \
         ((var) = (T *)stl_iter_data(stl__it_##var)) != NULL;               \
         stl__it_##var = stl_hashmap_iter_next(stl__it_##var))

/* Reverse iteration.  end() is not dereferenceable, so the iterator is stepped
 * to the last element before the first test. */
#define stl_vector_foreach_rev(v, T, var)                               \
    for (size_t stl__i_##var = stl_vector_size(v);                          \
         stl__i_##var-- > 0 && ((var) = (T *)stl_vector_at(v, stl__i_##var)) != NULL; )

#define stl_deque_foreach_rev(d, T, var)                                \
    for (size_t stl__i_##var = stl_deque_size(d);                           \
         stl__i_##var-- > 0 && ((var) = (T *)stl_deque_at(d, stl__i_##var)) != NULL; )

#define stl_set_foreach_rev(s, T, var)                                  \
    for (stl_iterator stl__it_##var = stl_set_rbegin(s);                    \
         ((var) = (T *)stl_iter_data(stl__it_##var)) != NULL;               \
         stl__it_##var = stl_set_iter_prev(stl__it_##var))

#define stl_map_foreach_rev(m, T, var)                                  \
    for (stl_iterator stl__it_##var = stl_map_rbegin(m);                    \
         ((var) = (T *)stl_iter_data(stl__it_##var)) != NULL;               \
         stl__it_##var = stl_map_iter_prev(stl__it_##var))

/* Map iteration that names the key and the value directly:
 *
 *     stl_map_foreach_kv(m, int_pair, it) {
 *         printf("%d -> %d\n", it->key, it->value);
 *     }
 *
 * It walks the map exactly like stl_map_foreach_t; the name exists so that the
 * intent (iterating pairs, not raw elements) is obvious at the call site. */
#define stl_map_foreach_kv(m, T, pair_var) \
    stl_map_foreach_t(m, T, pair_var)

/* Iterate a key/value map binding both members at once:
 *
 *     stl_map_foreach_entry(m, int_pair, entry) {
 *         printf("%d -> %d\n", *(int *)entry_key, *(int *)entry_value);
 *     }
 *
 * This introduces `entry`, `entry_key` and `entry_value`.  The key/value are
 * void* because the macro cannot know their types; cast as needed. */
#define stl_map_foreach_entry(m, T, entry)                                  \
    for (stl_iterator stl__it_##entry = stl_map_begin(m);                   \
         ((entry) = (T *)stl_iter_data(stl__it_##entry)) != NULL;           \
         stl__it_##entry = stl_map_iter_next(stl__it_##entry))              \
        for (void *entry_key = (void *)&(entry)->key,                       \
                  *entry_value = (void *)&(entry)->value,                   \
                  *stl__once_##entry = entry_key;                           \
             stl__once_##entry != NULL;                                     \
             stl__once_##entry = NULL)


/* ------------------------------------------------------------------ */
/* String helpers                                                      */
/* ------------------------------------------------------------------ */

/* stl_string_scope(s, "literal") declares `s` as a stl_string* that is freed
 * automatically when the enclosing block exits (GCC/Clang only; elsewhere this
 * degenerates to a plain declaration and you must call stl_string_free). */
/* Both forms always declare a stl_string*; whether it is released at scope
 * exit depends on STL_ENABLE_CLEANUP (see above). */
#if STL_HAVE_CLEANUP
#  define stl_string_scope(name, init)                                      \
      stl_string *name stl_cleanup(stl__autofree_string) = stl_string_new_from(init)
#  define stl_string_scope_fmt(name, fmt, ...)                              \
      stl_string *name stl_cleanup(stl__autofree_string) = stl_string_new_fmt(fmt, __VA_ARGS__)
#else
#  define stl_string_scope(name, init)         stl_string *name = stl_string_new_from(init)
#  define stl_string_scope_fmt(name, fmt, ...) stl_string *name = stl_string_new_fmt(fmt, __VA_ARGS__)
#endif

#define stl_cstr(s)             stl_string_cstr(s)
#define stl_strlen(s)           stl_string_size(s)
#define stl_str_eq(a, b)        stl_string_equals((a), (b))

/* printf into a stl_string, discarding the result: convenient in loops. */
#define stl_str_append_fmt(s, fmt, ...)                                     \
    ((void)stl_string_append_fmt((s), (fmt), __VA_ARGS__))

/* ------------------------------------------------------------------ */
/* Small utilities                                                     */
/* ------------------------------------------------------------------ */

#define stl_min_of(a, b)        ((a) < (b) ? (a) : (b))
#define stl_max_of(a, b)        ((a) > (b) ? (a) : (b))
#define stl_array_len(a)        (sizeof(a) / sizeof((a)[0]))
#define stl_swap_t(T, a, b)       do { T stl__t = (a); (a) = (b); (b) = stl__t; } while (0)
#define stl_swap_generic(a, b)  stl_swap(&(a), &(b), sizeof(a))
#define stl_clamp(x, lo, hi)    ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#define stl_zero(obj)           memset(&(obj), 0, sizeof(obj))

/* Compile-time assertion usable at file scope and inside functions.
 * C11/C++11 have _Static_assert/static_assert; older compilers get the classic
 * negative-array-size trick, which produces a link/compile error at file scope
 * and a clean compile error inside a function. */

/* Add a comparator and hash to a user struct type in one line. */
#define stl_define_cmp_fn(name, T, field)                                   \
    static int STL_CALL name(const void *stl__a, const void *stl__b) \
        stl_maybe_unused;                                                   \
    static int STL_CALL name(const void *stl__a, const void *stl__b) \
        stl_maybe_unused;                                                   \
    static int STL_CALL name(const void *stl__a, const void *stl__b)        \
    {                                                                       \
        const T *a = (const T *)stl__a;                                     \
        const T *b = (const T *)stl__b;                                     \
        return stl__cmp_select((int)sizeof(a->field))(&a->field, &b->field); \
    }

#define stl_define_eq_fn(name, T, field)                                    \
    static int STL_CALL name(const void *stl__a, const void *stl__b) \
        stl_maybe_unused;                                                   \
    static int STL_CALL name(const void *stl__a, const void *stl__b) \
        stl_maybe_unused;                                                   \
    static int STL_CALL name(const void *stl__a, const void *stl__b)        \
    {                                                                       \
        const T *a = (const T *)stl__a;                                     \
        const T *b = (const T *)stl__b;                                     \
        return a->field == b->field;                                        \
    }

/* stl_free_field(p) -- free a pointer field and clear it in one step, so a
 * destructor never leaves a dangling pointer behind. */
#define stl_free_field(p)                                                   \
    do {                                                                    \
        stl_mem_free(NULL, (void *)(p));                                    \
        (p) = NULL;                                                         \
    } while (0)

/* Declare a container element destructor.  `cleanup` is a statement (or a
 * comma expression) that releases whatever the element owns, e.g.
 *
 *     stl_define_dtor_fn(blob_dtor, blob, stl_free_field(e->data));
 */
#define stl_define_dtor_fn(name, T, cleanup)                                \
    static void STL_CALL name(void *stl__elem) stl_maybe_unused;            \
    static void STL_CALL name(void *stl__elem)                              \
    {                                                                       \
        T *e = (T *)stl__elem;                                              \
        (void)e;                                                            \
        cleanup;                                                            \
    }

/* ------------------------------------------------------------------ */
/* Support functions referenced by the macros above                    */
/* ------------------------------------------------------------------ */

#if STL_HAVE_CLEANUP
/* Cleanup shims: declared as static inline so they cost nothing when unused. */
STL_INLINE void stl__autofree_shim(void *pp)
{
    void **p = (void **)pp;
    if (p != NULL && *p != NULL) {
        stl_mem_free(NULL, *p);
        *p = NULL;
    }
}

STL_INLINE void stl__autofree_string(stl_string **pp)
{
    if (pp != NULL && *pp != NULL) {
        stl_string_free(*pp);
        *pp = NULL;
    }
}
#endif


#endif /* STL_HAVE_CONVENIENCE_MACROS */

/* ------------------------------------------------------------------ */
/*  deprecated spellings                                               */
/* ------------------------------------------------------------------ */
/* The macro layer used to be spelled in SCREAMING_CASE.  The function-style
 * lower-case names above read better next to real functions, so the old
 * spellings are kept as aliases and will be removed in a future major
 * version.  Define STL_NO_LEGACY_MACROS to leave them out. */
#ifndef STL_NO_LEGACY_MACROS
#  define STL_NEW                    stl_new
#  define STL_NEW_ARRAY              stl_new_array
#  define STL_DELETE                 stl_delete
#  define STL_DELETE_SET             stl_delete_all
#  define STL_FREE_ALL               stl_free_all
#  define STL_OFFSETOF               stl_offsetof
#  define STL_VECTOR_AT              stl_vector_at_t
#  define STL_VECTOR_AT_CHECKED      stl_vector_at_checked_t
#  define STL_DEQUE_AT               stl_deque_at_t
#  define STL_LIST_AT                stl_list_at_t
#  define STL_VECTOR_FRONT           stl_vector_front_t
#  define STL_VECTOR_BACK            stl_vector_back_t
#  define STL_DEQUE_FRONT            stl_deque_front_t
#  define STL_DEQUE_BACK             stl_deque_back_t
#  define STL_LIST_FRONT             stl_list_front_t
#  define STL_LIST_BACK              stl_list_back_t
#  define STL_STACK_TOP              stl_stack_top_t
#  define STL_QUEUE_FRONT            stl_queue_front_t
#  define STL_QUEUE_BACK             stl_queue_back_t
#  define STL_PQ_TOP                 stl_pq_top_t
#  define STL_VECTOR_PUSH            stl_vector_push
#  define STL_VECTOR_PUSH_LITERAL    stl_vector_push_literal
#  define STL_DEQUE_PUSH_BACK        stl_deque_push_back_t
#  define STL_DEQUE_PUSH_FRONT       stl_deque_push_front_t
#  define STL_LIST_PUSH_BACK         stl_list_push_back_t
#  define STL_LIST_PUSH_FRONT        stl_list_push_front_t
#  define STL_STACK_PUSH             stl_stack_push_t
#  define STL_QUEUE_PUSH             stl_queue_push_t
#  define STL_PQ_PUSH                stl_pq_push_t
#  define STL_STACK_POP_TO           stl_stack_pop_t
#  define STL_QUEUE_POP_TO           stl_queue_pop_t
#  define STL_PQ_POP_TO              stl_pq_pop_t
#  define STL_SET_NEW                stl_set_new_t
#  define STL_SET_NEW_A              stl_set_new_alloc
#  define STL_STRSET_NEW             stl_strset_new
#  define STL_PAIR_MAP_NEW           stl_pair_map_new
#  define STL_PAIR_HASHMAP_NEW       stl_pair_hashmap_new
#  define STL_MAP_PUT_PAIR           stl_map_put_pair
#  define STL_HASHMAP_PUT_PAIR       stl_hashmap_put_pair
#  define STL_PAIR_MAKE              stl_pair_make
#  define STL_VECTOR_FOREACH         stl_vector_foreach_t
#  define STL_VECTOR_FOREACH_IDX     stl_vector_foreach_idx
#  define STL_VECTOR_FOREACH_COPY    stl_vector_foreach_copy
#  define STL_VECTOR_FOREACH_REVERSE stl_vector_foreach_rev
#  define STL_DEQUE_FOREACH          stl_deque_foreach_t
#  define STL_DEQUE_FOREACH_REVERSE  stl_deque_foreach_rev
#  define STL_LIST_FOREACH           stl_list_foreach_t
#  define STL_LIST_FOREACH_SAFE      stl_list_foreach_safe
#  define STL_SET_FOREACH            stl_set_foreach_t
#  define STL_SET_FOREACH_REVERSE    stl_set_foreach_rev
#  define STL_MAP_FOREACH            stl_map_foreach_t
#  define STL_MAP_FOREACH_KV         stl_map_foreach_kv
#  define STL_MAP_FOREACH_REVERSE    stl_map_foreach_rev
#  define STL_HASHSET_FOREACH        stl_hashset_foreach_t
#  define STL_HASHMAP_FOREACH        stl_hashmap_foreach_t
#  define STL_STRING_SCOPE           stl_string_scope
#  define STL_STRING_SCOPE_FMT       stl_string_scope_fmt
#  define STL_CSTR                   stl_cstr
#  define STL_STRLEN                 stl_strlen
#  define STL_STR_EQ                 stl_str_eq
#  define STL_STR_APPEND_FMT         stl_str_append_fmt
#  define STL_MIN_OF                 stl_min_of
#  define STL_MAX_OF                 stl_max_of
#  define STL_ARRAY_LEN              stl_array_len
#  define STL_SWAP                   stl_swap_t
#  define STL_SWAP_GENERIC           stl_swap_generic
#  define STL_CLAMP                  stl_clamp
#  define STL_ZERO                   stl_zero
#  define STL_DEFINE_PAIR            stl_define_pair
#  define STL_DEFINE_CMP_FN          stl_define_cmp_fn
#  define STL_DEFINE_EQ_FN           stl_define_eq_fn
#  define STL_DEFINE_DTOR_FN         stl_define_dtor_fn
#  define STL_MAYBE_UNUSED           stl_maybe_unused
#endif /* STL_NO_LEGACY_MACROS */

/* End of public interface. */

#endif /* LIBSTL_H_INCLUDED */
