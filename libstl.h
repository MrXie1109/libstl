/**
 * @file libstl.h
 * @brief The STL (Standard Template Library) for C.
 *
 * A header and implementation pair that bring the familiar C++ STL vocabulary
 * -- vector, deque, list, set, map, the unordered containers, stack, queue,
 * priority_queue, string and bitset, plus the usual algorithms -- to plain C.
 *
 * @par Design notes
 *
 * -# This is C, so there are no templates and no transparent monomorphisation.
 *    Containers are heterogeneous: they store untyped elements whose size is
 *    fixed at construction time.  Each container is declared with one opaque
 *    type, and the element type is a convention between you and the container:
 *    @code
 *    stl_vector *v = stl_vector_new(sizeof(int), NULL);
 *    int x = 42;
 *    stl_vector_push_back(v, &x);
 *    int *p = (int *)stl_vector_at(v, 0);   // -> 42
 *    @endcode
 *
 * -# Containers own their element @e storage (elements are copied in and out
 *    by value) but never own memory that elements point to.  Destroying a
 *    container is therefore O(1) per element.  If your elements own resources,
 *    supply an @ref stl_dtor_fn or call a @c _clear_ex() variant.
 *
 * -# One red-black tree (@ref stl_rbtree) backs both @ref stl_set and
 *    @ref stl_map; one hash table (@ref stl_hashtable) backs both
 *    @ref stl_hashset and @ref stl_hashmap.
 *
 * -# Iterators are small value types -- element pointer, owning container and
 *    ordinal -- mirroring C++ iterator semantics, with invalidation rules that
 *    match the corresponding C++ container.
 *
 * -# Allocation can be redirected globally with @ref stl_set_allocator, or per
 *    container through the @c _new_a() constructors.
 *
 * @par Cross-platform
 * C89-compatible declarations are provided for the whole public API; a few C99
 * types (@c long @c long, @c stdint.h) are used when available.  The library
 * compiles cleanly with GCC, Clang and MSVC, from both C and C++ front ends,
 * and is tested for 32- and 64-bit as well as big- and little-endian builds.
 * The only POSIX-specific API is @c pthread_rwlock_t inside @ref stl_rwlock,
 * and that block is guarded by @c STL_HAVE_PTHREAD.
 *
 * @par Thread safety
 * Containers are not synchronised.  Concurrent access must be serialised by
 * the caller; @ref stl_safe_vector and @ref stl_safe_map provide ready-made
 * locked wrappers.  The error-handling slot and the default allocator are
 * process-wide.
 *
 * @par Example
 * @code
 * #include "libstl.h"
 *
 * stl_vector *v = stl_vector_new(sizeof(int), NULL);
 * int i;
 * for (i = 0; i < 10; ++i) {
 *     stl_vector_push_back(v, &i);
 * }
 * stl_vector_sort(v, stl_cmp_int32);
 * printf("smallest = %d\n", *(int *)stl_vector_front(v));
 * stl_vector_free(v);
 * @endcode
 *
 * @par License
 * MIT.  See the LICENSE file in the project root.
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

/** @addtogroup libstl
 *  @{ */

/** @brief An untyped byte, used for pointer arithmetic on element storage. */
typedef unsigned char  stl_byte;

/** @brief The library's boolean type; @ref STL_TRUE or @ref STL_FALSE.
 *
 * Plain @c int is used rather than @c bool so that the header stays valid in
 * C89 and in C++ without pulling in @c stdbool.h. */
typedef int            stl_bool;

#ifndef STL_TRUE
#  define STL_TRUE  1   /**< Truth value of a @ref stl_bool. */
#  define STL_FALSE 0   /**< Falsehood value of a @ref stl_bool. */
#endif

#ifndef STL_NULL
#  define STL_NULL ((void *)0)  /**< Portable null pointer constant. */
#endif

/** @brief Value returned by search functions when nothing is found.
 *
 * It is @c (size_t)-1, so it can never collide with a valid index. */
#define STL_NPOS ((size_t)-1)

/** @brief Result of a three-way comparison.
 *
 * Either the C++20 ordering convention or the negative/zero/positive
 * convention of @c memcmp; the two agree, so comparators may return any
 * negative or positive value. */
typedef int stl_compare_result;

#define STL_LESS    (-1)    /**< The first argument orders before the second. */
#define STL_EQUAL   (0)     /**< The two arguments are equivalent. */
#define STL_GREATER (1)     /**< The first argument orders after the second. */

/** @brief Ordering predicate over two elements.
 *
 * @param a pointer to the left-hand element
 * @param b pointer to the right-hand element
 * @return a negative value when @p a orders before @p b, zero when they are
 *         equivalent, a positive value otherwise
 *
 * @warning The predicate must impose a strict weak ordering.  Comparing a
 *          struct with memcmp is only valid when it has no padding;
 *          @c stl_cmp_field and the fixed-width comparators avoid the trap.
 * @see stl_cmp_int32, stl_cmp_cstr, stl_cmp_double
 */
typedef int (STL_CALL *stl_compare_fn)(const void *a, const void *b);

/** @brief Equality predicate over two elements.
 *
 * @param a pointer to the left-hand element
 * @param b pointer to the right-hand element
 * @return non-zero when the elements are equal
 *
 * Used by @c find, @c erase, @c unique and the unordered containers' second
 * level of key comparison.  When NULL is passed where an @ref stl_equal_fn is
 * expected, the library falls back to comparing the leading machine word, which
 * is only meaningful for scalar or pointer elements.
 */
typedef int (STL_CALL *stl_equal_fn)(const void *a, const void *b);

/** @brief Hash function over a key.
 *
 * @param key pointer to the key to hash
 * @return any @c size_t; equal keys must hash equally
 *
 * A poor hash degrades the unordered containers to linear search but never
 * breaks correctness.
 * @see stl_hash_int, stl_hash_cstr, stl_hash_bytes
 */
typedef size_t (STL_CALL *stl_hash_fn)(const void *key);

/** @brief Element destructor.
 *
 * @param elem pointer to the element about to be destroyed
 *
 * Invoked by the @c _clear_ex() and @c _free() functions, and by the
 * operations that remove elements.  Use it to release anything the element
 * owns; the container then releases the element storage itself.  May be NULL.
 *
 * @warning Must not modify the container it is called from.
 */
typedef void (STL_CALL *stl_dtor_fn)(void *elem);

/** @brief Deep-copy hook for element contents.
 *
 * @param dst destination element, already holding a byte copy of @p src
 * @param src source element
 * @param elem_size size of one element in bytes
 * @return non-zero on success; returning zero aborts the copy
 *
 * Container copies are byte copies, which is enough for scalar elements.  When
 * an element owns a pointer, this hook is the place to duplicate what it
 * points at.  May be NULL.
 */
typedef int (STL_CALL *stl_copy_fn)(void *dst, const void *src, size_t elem_size);

/** @brief Iteration callback.
 *
 * @param elem pointer to the current element
 * @param user the opaque pointer passed to the @c foreach function
 * @return non-zero to stop the iteration early
 */
typedef int (STL_CALL *stl_visit_fn)(void *elem, void *user);

/** @} */ /* end of group libstl */

/* ------------------------------------------------------------------ */
/* Allocator                                                           */
/* ------------------------------------------------------------------ */

/** @addtogroup allocators
 *  @{ */

/** @brief Allocation callback, matching the signature of @c malloc. */
typedef void *(STL_CALL *stl_malloc_fn)(size_t size);

/** @brief Reallocation callback, matching the signature of @c realloc.
 *
 * Required by the containers that grow a contiguous buffer (vector, string).
 * When NULL, the library emulates it by allocating and freeing, which loses
 * the existing contents. */
typedef void *(STL_CALL *stl_realloc_fn)(void *ptr, size_t size);

/** @brief Deallocation callback, matching the signature of @c free. */
typedef void  (STL_CALL *stl_free_fn)(void *ptr);

/** @brief A pluggable memory source.
 *
 * Pass one of these to any @c _new_a() constructor, or install it process-wide
 * with @ref stl_set_allocator.  Passing NULL anywhere an allocator is expected
 * selects the default allocator, so the common case needs no setup.
 *
 * @code
 * static void *arena_malloc(size_t n) { return arena_alloc(&g_arena, n); }
 * static void  arena_free(void *p)    { arena_release(&g_arena, p); }
 *
 * stl_allocator arena = { arena_malloc, NULL, arena_free, NULL };
 * stl_vector *v = stl_vector_new_a(sizeof(int), NULL, &arena);
 * @endcode
 */
typedef struct stl_allocator {
    stl_malloc_fn  malloc_fn;   /**< Required. */
    stl_realloc_fn realloc_fn;  /**< Optional; see @ref stl_realloc_fn. */
    stl_free_fn    free_fn;     /**< Required. */
    void          *user;        /**< Opaque; not used by the library itself,
                                     available to callbacks that need context. */
} stl_allocator;

/** @brief Return the process-wide default allocator, backed by @c malloc.
 *
 * Passing NULL wherever an allocator is accepted is equivalent to passing this
 * one, which is the usual way to write portable code. */
STL_API const stl_allocator *stl_default_allocator(void);

/** @brief Install a new process-wide default allocator.
 *
 * @param alloc the allocator to install; must provide @c malloc_fn and
 *              @c free_fn
 * @return the allocator that was previously installed, or NULL if @p alloc was
 *         rejected for missing callbacks
 *
 * Containers capture the allocator they were created with, so changing the
 * default affects only containers created afterwards.
 *
 * @warning Not thread-safe.  Call it during start-up, before creating any
 *          container from more than one thread.
 */
STL_API const stl_allocator *stl_set_allocator(const stl_allocator *alloc);

/** @brief Allocate storage for an array of elements.
 *
 * @param a allocator, or NULL for the default
 * @param count number of elements
 * @param elem_size size of one element in bytes
 * @return the new block, or NULL on failure or when the size would overflow
 *
 * The product @p count @c * @p elem_size is checked before allocating, so an
 * overflowing multiplication is reported as @ref STL_ERR_OVERFLOW rather than
 * silently under-allocating.
 */
STL_API void *stl_mem_alloc(const stl_allocator *a, size_t count, size_t elem_size);

/** @brief Resize an allocation obtained from @ref stl_mem_alloc.
 *
 * @param a allocator, or NULL for the default
 * @param ptr existing block, or NULL to allocate a fresh one
 * @param count new element count
 * @param elem_size size of one element in bytes
 * @return the resized block, or NULL on failure
 *
 * On failure the original block is left untouched and remains valid, so the
 * caller can keep using it.
 */
STL_API void *stl_mem_realloc(const stl_allocator *a, void *ptr, size_t count, size_t elem_size);

/** @brief Release a block obtained from this library's allocator.
 *
 * @param a allocator, or NULL for the default
 * @param ptr the block to release; NULL is ignored
 */
STL_API void  stl_mem_free(const stl_allocator *a, void *ptr);

/** @brief Suggest a capacity for a growing dynamic array.
 *
 * @param need the minimum number of elements that must fit
 * @return a capacity of at least @p need, rounded up for amortised growth
 *
 * Uses a 1.5x growth policy, which keeps reallocation amortised O(1) while
 * wasting less memory than doubling.  Exposed mainly for custom containers.
 */
STL_API size_t stl_growth_capacity(size_t need);

/** @} */ /* end of group allocators */

/* ------------------------------------------------------------------ */
/* Error handling                                                      */
/* ------------------------------------------------------------------ */

/** @addtogroup errors
 *  @{ */

/** @brief Error codes reported through the error hook.
 *
 * Functions that cannot return a value (such as @c void pop operations) report
 * problems by passing one of these to the installed handler.  Functions that
 * can return a value usually return the code directly, and also record it for
 * @ref stl_get_error.
 */
typedef enum stl_error_code {
    STL_OK              = 0,  /**< No error. */
    STL_ERR_NOMEM       = 1,  /**< An allocation failed. */
    STL_ERR_RANGE       = 2,  /**< An index or iterator was out of range. */
    STL_ERR_INVALID     = 3,  /**< A bad argument: NULL, zero @c elem_size, ... */
    STL_ERR_EMPTY       = 4,  /**< The operation requires a non-empty container. */
    STL_ERR_DUPLICATE   = 5,  /**< The key is already present. */
    STL_ERR_NOT_FOUND   = 6,  /**< The key is absent. */
    STL_ERR_TYPE        = 7,  /**< Element sizes are incompatible. */
    STL_ERR_STATE       = 8,  /**< The container is in an unusable state. */
    STL_ERR_OVERFLOW    = 9,  /**< An arithmetic or allocation size overflowed. */
    STL_ERR_UNSUPPORTED = 10  /**< Not available in this build or on this platform. */
} stl_error_code;

/** @brief Describe an error code.
 *
 * @param code the code to describe
 * @return a static, human-readable string; never NULL, even for unknown codes
 */
STL_API const char *stl_error_string(stl_error_code code);

/** @brief Callback invoked whenever the library reports an error.
 *
 * @param code the error code
 * @param file source file that reported it
 * @param line source line that reported it
 * @param msg formatted, human-readable description; never NULL
 *
 * The default handler does nothing, or prints to stderr when @c STL_VERBOSE is
 * defined at compile time.  Install your own to log, assert, or convert errors
 * into exceptions in a C++ wrapper.
 *
 * @warning Must not call back into the library operation that reported the
 *          error, and must not throw.
 */
typedef void (STL_CALL *stl_error_fn)(stl_error_code code, const char *file, int line, const char *msg);

/** @brief Install an error handler.
 *
 * @param handler the new handler, or NULL to restore the default
 * @return the handler that was previously installed
 *
 * @warning The handler is process-wide and not synchronised.  Install it
 *          during start-up.
 */
STL_API stl_error_fn stl_set_error_handler(stl_error_fn handler);

/** @brief Report an error through the installed handler.
 *
 * @param code the error code
 * @param file source file, normally @c __FILE__
 * @param line source line, normally @c __LINE__
 * @param fmt printf-style format describing the problem
 *
 * Usually reached through the @ref STL_ERROR macro, which fills in the source
 * location.  Formatted text is truncated to 512 bytes.
 */
STL_API void stl_set_error(stl_error_code code, const char *file, int line, const char *fmt, ...);

/** @brief Read and clear the most recently reported error code.
 *
 * @return the last code, or @ref STL_OK when nothing has been reported
 *
 * Because reading resets the slot, this is a one-shot check: call it
 * immediately after the operation whose outcome you care about.  It is chiefly
 * useful for the functions that report errors but cannot return a code, such
 * as the @c pop operations.
 */
STL_API int  stl_get_error(void);

/** @brief Discard the recorded error without reading it. */
STL_API void stl_clear_error(void);

/** @brief Report an error with the source location filled in automatically.
 *
 * @param code the error code
 * @param ... printf-style format and arguments
 */
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
#  define STL_UNLIKELY(x) __builtin_expect(!!(x), 0)   /**< Branch hint. */
#else
#  define STL_UNLIKELY(x) (x)                          /**< Branch hint. */
#endif

/** @brief Internal contract check, compiled out under @c NDEBUG.
 *
 * @param cond the condition that must hold
 * @param code error code to report when it does not
 * @param msg message to report when it does not
 */
#ifdef NDEBUG
#  define STL_CHECK(cond, code, msg) ((void)0)
#else
#  define STL_CHECK(cond, code, msg) \
       do { if (STL_UNLIKELY(!(cond))) STL_ERROR((code), (msg)); } while (0)
#endif

/** @} */ /* end of group errors */

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

/** @addtogroup algorithms
 *  @{ */

/** @brief Exchange the contents of two elements.
 *
 * @param a first element
 * @param b second element
 * @param elem_size size of one element in bytes
 *
 * Works for any element size: small elements are swapped through a stack
 * buffer, larger ones are swapped in chunks.
 */
STL_API void stl_swap(void *a, void *b, size_t elem_size);

/** @brief Locate the first element of a sorted range that is not less than a key.
 *
 * @param key pointer to the key to search for
 * @param base start of the sorted array
 * @param count number of elements in the array
 * @param elem_size size of one element in bytes
 * @param cmp ordering predicate
 * @return pointer to the first element not less than @p key; points one past
 *         the end when every element is smaller
 *
 * O(log @p count) comparisons, but requires the range to be sorted.
 */
STL_API void *stl_lower_bound(const void *key, const void *base, size_t count, size_t elem_size, stl_compare_fn cmp);

/** @brief Locate the first element of a sorted range that is greater than a key.
 *
 * @param key pointer to the key to search for
 * @param base start of the sorted array
 * @param count number of elements in the array
 * @param elem_size size of one element in bytes
 * @param cmp ordering predicate
 * @return pointer to the first element greater than @p key; points one past the
 *         end when no such element exists
 *
 * Together with @ref stl_lower_bound this brackets the run of elements equal
 * to @p key.
 */
STL_API void *stl_upper_bound(const void *key, const void *base, size_t count, size_t elem_size, stl_compare_fn cmp);

/** @brief Start of the range of elements equal to a key.
 *  @see stl_lower_bound */
STL_API void *stl_equal_range_lo(const void *key, const void *base, size_t count, size_t elem_size, stl_compare_fn cmp);

/** @brief End of the range of elements equal to a key.
 *  @see stl_upper_bound */
STL_API void *stl_equal_range_hi(const void *key, const void *base, size_t count, size_t elem_size, stl_compare_fn cmp);

/** @brief Test whether a sorted range contains a key.
 *
 * @param key pointer to the key to search for
 * @param base start of the sorted array
 * @param count number of elements in the array
 * @param elem_size size of one element in bytes
 * @param cmp ordering predicate
 * @return non-zero when an element equivalent to @p key is present
 */
STL_API int   stl_binary_search(const void *key, const void *base, size_t count, size_t elem_size, stl_compare_fn cmp);

/** @} */ /* end of group algorithms */

/* ------------------------------------------------------------------ */
/* Common predicates and hashes                                        */
/* ------------------------------------------------------------------ */

/** @addtogroup comparators
 *  @{ */

/** @name Fixed-width ordering predicates
 *
 * Each compares two elements of the named type.  They exist because the
 * default ordering treats an element as an opaque word, which is wrong for
 * every type wider or narrower than that.
 *  @{ */
STL_API int STL_CALL stl_cmp_int8(const void *a, const void *b);     /**< @brief Order two @c signed @c char elements. */
STL_API int STL_CALL stl_cmp_uint8(const void *a, const void *b);    /**< @brief Order two @c unsigned @c char elements. */
STL_API int STL_CALL stl_cmp_int16(const void *a, const void *b);    /**< @brief Order two @c short elements. */
STL_API int STL_CALL stl_cmp_uint16(const void *a, const void *b);   /**< @brief Order two @c unsigned @c short elements. */
STL_API int STL_CALL stl_cmp_int32(const void *a, const void *b);    /**< @brief Order two @c int elements. */
STL_API int STL_CALL stl_cmp_uint32(const void *a, const void *b);   /**< @brief Order two @c unsigned @c int elements. */
STL_API int STL_CALL stl_cmp_int64(const void *a, const void *b);    /**< @brief Order two 64-bit signed elements. */
STL_API int STL_CALL stl_cmp_uint64(const void *a, const void *b);   /**< @brief Order two 64-bit unsigned elements. */
STL_API int STL_CALL stl_cmp_float(const void *a, const void *b);    /**< @brief Order two @c float elements. */
STL_API int STL_CALL stl_cmp_double(const void *a, const void *b);   /**< @brief Order two @c double elements. */
/** @brief Order two elements by the NUL-terminated strings they point at.
 *
 * The elements themselves are @c char @c * values; the comparison follows the
 * pointed-to text, so this is the comparator for a container of strings. */
STL_API int STL_CALL stl_cmp_cstr(const void *a, const void *b);
/** @brief Compare two byte strings of a known length lexicographically.
 *
 * @param a first byte string
 * @param b second byte string
 * @param len number of bytes to compare; 0 is treated as 1
 * @return the ordering of the two byte strings
 */
STL_API int STL_CALL stl_cmp_mem_len(const void *a, const void *b, size_t len);
/** @brief Default ordering: compare the leading machine word of each element.
 *
 * This is what a container uses when no comparator is supplied.  It is only
 * meaningful for scalar elements of exactly @c sizeof(void*) bytes; supply a
 * real comparator for anything else. */
STL_API int STL_CALL stl_cmp_mem(const void *a, const void *b);
/** @brief Order two elements by the pointer values they hold. */
STL_API int STL_CALL stl_cmp_ptr(const void *a, const void *b);
/** @} */

/** @name Equality predicates
 *  @{ */
STL_API int STL_CALL stl_eq_int(const void *a, const void *b);   /**< @brief Test two @c int elements for equality. */
STL_API int STL_CALL stl_eq_cstr(const void *a, const void *b);  /**< @brief Test two @c char @c * elements for string equality. */
STL_API int STL_CALL stl_eq_mem(const void *a, const void *b);   /**< @brief Default equality: compare the leading machine word. */
STL_API int STL_CALL stl_eq_ptr(const void *a, const void *b);   /**< @brief Test two pointer elements for equality. */
/** @} */

/** @name Hash functions
 *  @{ */
/** @brief Hash a @c char @c * element by the content of the string it points to. */
STL_API size_t STL_CALL stl_hash_cstr(const void *key);
/** @brief Default hash: treat the element as a NUL-terminated byte string. */
STL_API size_t STL_CALL stl_hash_mem(const void *key);
/** @brief Hash an @c int element. */
STL_API size_t STL_CALL stl_hash_int(const void *key);
/** @brief Hash a 64-bit integer element. */
STL_API size_t STL_CALL stl_hash_int64(const void *key);
/** @brief Hash a pointer element. */
STL_API size_t STL_CALL stl_hash_ptr(const void *key);
/** @} */

/** @brief Hash a byte string of a known length.
 *
 * @param data pointer to the bytes to hash
 * @param len number of bytes
 * @return a hash folded to the width of @c size_t
 *
 * This is the primitive the other hash functions are built on, and the one to
 * call when hashing a key whose length is known but which is not
 * NUL-terminated.
 */
STL_API size_t stl_hash_bytes(const void *data, size_t len);

/** @} */ /* end of group comparators */

/* ------------------------------------------------------------------ */
/* Iterator primitives                                                 */
/* ------------------------------------------------------------------ */

/** @addtogroup iterators
 *  @{ */

/** @brief A lightweight, by-value iterator over any container.
 *
 * An iterator records where it points, which container it came from, and its
 * ordinal position.  The owner and index together let @c next, @c prev and
 * @c distance be implemented once for every container, including the ones
 * whose elements are not contiguous.
 *
 * Iterators are plain values: copy them freely, and compare with
 * @ref stl_iter_equal rather than @c ==.
 *
 * @par Validity
 * An iterator stays valid until the container is structurally modified.  The
 * rules follow the C++ container of the same name: @c push_back on a vector
 * may invalidate every iterator, whereas nodes in a list, set, map or hash
 * table keep their addresses until they are erased.
 *
 * @par Example
 * @code
 * stl_iterator it = stl_vector_begin(v);
 * while (!stl_iter_equal(it, stl_vector_end(v))) {
 *     int *p = (int *)stl_iter_data(it);
 *     printf("%d\n", *p);
 *     it = stl_vector_iter_next(it);
 * }
 * @endcode
 *
 * @note Iterators are not pointers, so they do not support pointer
 *       arithmetic.  Use the per-container @c iter_next and @c iter_prev
 *       functions, or the @c foreach macros.
 */
typedef struct stl_iterator {
    void *elem;         /**< Current element, or NULL when past the end. */
    const void *owner;  /**< The container this iterator came from, or NULL. */
    size_t index;       /**< Ordinal position of @c elem within the container. */
} stl_iterator;

/** @brief Construct the sentinel iterator that represents "no position".
 *
 * @return an iterator whose fields are all zero
 *
 * Useful as a "not found" result and as the initial value for a variable that
 * will be assigned by a lookup.
 */
STL_API stl_iterator stl_iter_null(void);

/** @brief Test whether an iterator is the sentinel.
 *
 * @param it the iterator to test
 * @return non-zero when @p it came from @ref stl_iter_null
 */
STL_API int stl_iter_is_null(stl_iterator it);

/** @brief Dereference an iterator.
 *
 * @param it the iterator to dereference
 * @return pointer to the element, or NULL for a past-the-end or sentinel
 *         iterator
 */
STL_API void *stl_iter_data(stl_iterator it);

/** @brief Compare two iterators for identity.
 *
 * @param a first iterator
 * @param b second iterator
 * @return non-zero when both refer to the same element of the same container
 *
 * This is the test to use in loop conditions; comparing the struct fields by
 * hand would also compare the owner pointer, which is easy to get wrong.
 */
STL_API int stl_iter_equal(stl_iterator a, stl_iterator b);

/** @} */ /* end of group iterators */

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ================================================================== */
/*  vector                                                            */
/* ================================================================== */

/** @addtogroup vector
 *  @{
 */

/** @brief A contiguous, growable array of fixed-size elements.
 *
 * The counterpart of @c std::vector.  Elements live in one contiguous block,
 * so random access is O(1) and the storage can be handed to any function that
 * expects a plain array.
 *
 * @par Complexity
 * Random access O(1); appending amortised O(1); inserting or erasing anywhere
 * but the end O(n), because the tail has to move.
 *
 * @par Iterator invalidation
 * Any operation that grows the capacity reallocates the block, invalidating
 * every pointer and iterator into the vector.  @c push_back therefore
 * invalidates everything, exactly as in C++.  Erasing shifts the tail, so
 * iterators at or after the erased position are invalidated too.
 *
 * @par Example
 * @code
 * stl_vector *v = stl_vector_new(sizeof(int), NULL);
 * int i;
 * for (i = 0; i < 100; ++i) {
 *     stl_vector_push_back(v, &i);      // amortised O(1)
 * }
 * stl_vector_sort(v, stl_cmp_int32);
 * int key = 42;
 * if (stl_vector_contains(v, &key, stl_cmp_int32)) {
 *     printf("found at %lu\n",
 *            (unsigned long)stl_vector_lower_bound(v, &key, stl_cmp_int32));
 * }
 * stl_vector_free(v);
 * @endcode
 */
typedef struct stl_vector stl_vector;

#ifdef __cplusplus
extern "C" {
#endif

/** @name Construction and destruction
 *  @{ */

/** @brief Create a vector.
 *
 * @param elem_size size of one element in bytes; must be non-zero
 * @param elem_dtor optional destructor, or NULL
 * @return the new vector, or NULL on failure
 */
STL_API stl_vector *stl_vector_new(size_t elem_size, stl_dtor_fn elem_dtor);

/** @brief Create a vector that allocates through a specific allocator.
 *
 * @param elem_size size of one element in bytes
 * @param elem_dtor optional destructor, or NULL
 * @param a allocator, or NULL for the default
 * @return the new vector, or NULL on failure
 */
STL_API stl_vector *stl_vector_new_a(size_t elem_size, stl_dtor_fn elem_dtor, const stl_allocator *a);

/** @brief Create a vector with room for @p cap elements already reserved.
 *
 * @param elem_size size of one element in bytes
 * @param cap initial capacity in elements
 * @param elem_dtor optional destructor, or NULL
 * @param a allocator, or NULL for the default
 * @return the new vector, or NULL on failure
 *
 * Use this when the final size is roughly known, to avoid the reallocation
 * and copying that repeated @c push_back would otherwise cause.
 */
STL_API stl_vector *stl_vector_new_cap(size_t elem_size, size_t cap, stl_dtor_fn elem_dtor, const stl_allocator *a);

/** @brief Adopt an existing array as a vector, without copying it.
 *
 * @param data the array to adopt; must have been allocated compatibly
 * @param count number of elements currently in use
 * @param cap capacity in elements; values below @p count are raised to it
 * @param elem_size size of one element in bytes
 * @param elem_dtor optional destructor, or NULL
 * @param a allocator, or NULL for the default
 * @return the new vector, or NULL on failure
 *
 * The vector takes ownership and will release @p data with the given
 * allocator, so the array must not be freed separately.
 *
 * @warning @p cap counts elements, not bytes -- a common source of
 *          over-allocation bugs.
 */
STL_API stl_vector *stl_vector_from_array(void *data, size_t count, size_t cap,
                                          size_t elem_size, stl_dtor_fn elem_dtor,
                                          const stl_allocator *a);

/** @brief Destroy a vector.
 *
 * @param v the vector; NULL is ignored
 *
 * Calls @c elem_dtor on every element when one was supplied, then releases the
 * storage.
 */
STL_API void        stl_vector_free(stl_vector *v);

/** @brief Deep-copy a vector.
 *
 * @param v the vector to copy
 * @param copy_elem optional hook to duplicate data the elements point to
 * @return the new vector, or NULL on failure
 */
STL_API stl_vector *stl_vector_copy(const stl_vector *v, stl_copy_fn copy_elem);

/** @brief Deep-copy a vector into a specific allocator.
 *
 * @param v the vector to copy
 * @param copy_elem optional element copy hook
 * @param a allocator for the new vector, or NULL for the default
 * @return the new vector, or NULL on failure
 */
STL_API stl_vector *stl_vector_copy_a(const stl_vector *v, stl_copy_fn copy_elem, const stl_allocator *a);

/** @} */

/** @name Size and capacity
 *  @{ */

/** @brief Return the number of elements.
 *  @param v the vector
 *  @return the element count */
STL_API size_t stl_vector_size(const stl_vector *v);

/** @brief Return the number of elements the vector can hold before it must
 *         reallocate.
 *  @param v the vector
 *  @return the capacity in elements */
STL_API size_t stl_vector_capacity(const stl_vector *v);

/** @brief Return the size of one element in bytes.
 *  @param v the vector */
STL_API size_t stl_vector_elem_size(const stl_vector *v);

/** @brief Test whether the vector holds no elements.
 *  @param v the vector
 *  @return non-zero when empty */
STL_API int    stl_vector_empty(const stl_vector *v);

/** @brief Return the underlying array.
 *
 * @param v the vector
 * @return pointer to the first element, or NULL when the vector is empty
 *
 * The storage is contiguous, so the result can be passed to any function
 * expecting @c T @c * together with @ref stl_vector_size.
 */
STL_API void  *stl_vector_data(stl_vector *v);

/** @brief Return the underlying array of a read-only vector.
 *  @param v the vector
 *  @return pointer to the first element, or NULL when empty */
STL_API const void *stl_vector_data_c(const stl_vector *v);

/** @brief Return the allocator the vector was created with.
 *  @param v the vector */
STL_API const stl_allocator *stl_vector_allocator(const stl_vector *v);

/** @} */

/** @name Capacity management
 *  @{ */

/** @brief Ensure room for at least @p n elements.
 *
 * @param v the vector
 * @param n the required capacity
 * @return @ref STL_OK, or @ref STL_ERR_NOMEM
 *
 * Never shrinks.  Reserving up front is the standard way to make a run of
 * @c push_back calls allocation-free.
 */
STL_API int stl_vector_reserve(stl_vector *v, size_t n);

/** @brief Change the element count, zero-filling any new elements.
 *
 * @param v the vector
 * @param n the new size
 * @return @ref STL_OK, or an error code
 *
 * Growing zero-fills the new slots; shrinking calls @c elem_dtor on the
 * elements that are dropped.
 */
STL_API int stl_vector_resize(stl_vector *v, size_t n);

/** @brief Change the element count, filling new elements with a value.
 *
 * @param v the vector
 * @param n the new size
 * @param value pointer to the value copied into every new slot
 * @return @ref STL_OK, or an error code
 */
STL_API int stl_vector_resize_v(stl_vector *v, size_t n, const void *value);

/** @brief Release unused capacity.
 *
 * @param v the vector
 * @return @ref STL_OK, or an error code
 *
 * Reduces capacity to exactly the current size.  Useful to hand memory back
 * after a vector has shrunk a lot.
 */
STL_API int stl_vector_shrink_to_fit(stl_vector *v);

/** @brief Set the capacity explicitly, dropping elements beyond @p n.
 *
 * @param v the vector
 * @param n the new capacity
 * @return @ref STL_OK, or an error code
 *
 * Unlike @ref stl_vector_reserve this can shrink.  When @p n is smaller than
 * the current size, the excess elements are destroyed and the size follows.
 */
STL_API int stl_vector_set_capacity(stl_vector *v, size_t n);

/** @} */

/** @name Element access
 *  @{ */

/** @brief Access an element with a bounds check.
 *
 * @param v the vector
 * @param i zero-based index
 * @return pointer to the element, or NULL when @p i is out of range
 *
 * An out-of-range access reports @ref STL_ERR_RANGE through the error hook
 * rather than reading past the end.
 */
STL_API void *stl_vector_at(stl_vector *v, size_t i);

/** @brief Access an element of a read-only vector, with a bounds check.
 *  @param v the vector
 *  @param i zero-based index
 *  @return pointer to the element, or NULL when out of range */
STL_API const void *stl_vector_at_c(const stl_vector *v, size_t i);

/** @brief Access the first element.
 *  @param v the vector
 *  @return pointer to the element, or NULL when the vector is empty */
STL_API void *stl_vector_front(stl_vector *v);

/** @brief Access the last element.
 *  @param v the vector
 *  @return pointer to the element, or NULL when the vector is empty */
STL_API void *stl_vector_back(stl_vector *v);

/** @brief Access an element by signed index, counting from the back when negative.
 *
 * @param v the vector
 * @param i index; negative values address from the end, so -1 is the last
 *          element
 * @return pointer to the element, or NULL when the index is out of range
 */
STL_API void *stl_vector_index(stl_vector *v, ptrdiff_t i);

/** @} */

/** @name Modifiers
 *
 * Every function in this group returns @ref STL_OK on success or an error code
 * on failure, except the @c pop operations, which cannot fail usefully and
 * simply report an empty vector through the error hook.
 *  @{ */

/** @brief Append a copy of an element.
 *
 * @param v the vector
 * @param elem pointer to the element to copy in
 * @return @ref STL_OK, or an error code
 *
 * Amortised O(1).  May reallocate, which invalidates every outstanding pointer
 * and iterator into the vector.
 */
STL_API int stl_vector_push_back(stl_vector *v, const void *elem);

/** @brief Remove the last element.
 *
 * @param v the vector
 *
 * Reports @ref STL_ERR_EMPTY when the vector is already empty.  Calls
 * @c elem_dtor on the removed element.
 */
STL_API void stl_vector_pop_back(stl_vector *v);

/** @brief Prepend a copy of an element.
 *
 * @param v the vector
 * @param elem pointer to the element to copy in
 * @return @ref STL_OK, or an error code
 *
 * @warning O(n): every existing element is shifted up by one.  A vector is
 *          the wrong container if you need this often.
 */
STL_API int stl_vector_push_front(stl_vector *v, const void *elem);

/** @brief Remove the first element.
 *  @param v the vector
 *  @warning O(n), for the same reason as @ref stl_vector_push_front. */
STL_API void stl_vector_pop_front(stl_vector *v);

/** @brief Insert a copy of an element at a position.
 *
 * @param v the vector
 * @param pos index at which the element is inserted; must be at most the size
 * @param elem pointer to the element to copy in
 * @return @ref STL_OK, or an error code
 */
STL_API int stl_vector_insert(stl_vector *v, size_t pos, const void *elem);

/** @brief Insert several copies of an element at a position.
 *
 * @param v the vector
 * @param pos index at which the copies are inserted
 * @param count how many copies to insert
 * @param elem pointer to the element to copy
 * @return @ref STL_OK, or an error code
 */
STL_API int stl_vector_insert_n(stl_vector *v, size_t pos, size_t count, const void *elem);

/** @brief Insert a block of elements from an array.
 *
 * @param v the vector
 * @param pos index at which the block is inserted
 * @param array pointer to the source elements
 * @param count number of elements to insert
 * @return @ref STL_OK, or an error code
 *
 * Safe when @p array points into @p v itself: the block is taken from a
 * temporary first.
 */
STL_API int stl_vector_insert_array(stl_vector *v, size_t pos, const void *array, size_t count);

/** @brief Append a block of elements from an array.
 *
 * @param v the vector
 * @param array pointer to the source elements
 * @param count number of elements to append
 * @return @ref STL_OK, or an error code
 */
STL_API int stl_vector_append_array(stl_vector *v, const void *array, size_t count);

/** @brief Erase one element.
 *
 * @param v the vector
 * @param pos index of the element to erase
 * @return @ref STL_OK, or @ref STL_ERR_RANGE
 */
STL_API int stl_vector_erase(stl_vector *v, size_t pos);

/** @brief Erase a half-open range of elements.
 *
 * @param v the vector
 * @param first index of the first element to erase
 * @param last index one past the last element to erase
 * @return @ref STL_OK, or @ref STL_ERR_RANGE
 */
STL_API int stl_vector_erase_range(stl_vector *v, size_t first, size_t last);

/** @brief Remove every element, keeping the allocated storage.
 *  @param v the vector
 *  @note Does not call @c elem_dtor; use @ref stl_vector_clear_ex for that. */
STL_API void stl_vector_clear(stl_vector *v);

/** @brief Remove every element and destroy each one.
 *
 * @param v the vector
 *
 * This is the variant to use when elements own resources.
 */
STL_API void stl_vector_clear_ex(stl_vector *v);

/** @brief Append a default-initialised slot and return it.
 *
 * @param v the vector
 * @return pointer to the new slot, already zeroed, or NULL on failure
 *
 * The size grows by one.  Fill in the slot through the returned pointer; this
 * avoids building a temporary when the element is expensive to construct.
 */
STL_API void *stl_vector_emplace(stl_vector *v);

/** @brief Replace the contents with a copy of an array.
 *
 * @param v the vector
 * @param array pointer to the new elements
 * @param count number of elements
 * @return @ref STL_OK, or an error code
 */
STL_API int   stl_vector_assign(stl_vector *v, const void *array, size_t count);

/** @brief Replace the contents with repeated copies of one value.
 *
 * @param v the vector
 * @param count number of copies
 * @param value pointer to the value to repeat
 * @return @ref STL_OK, or an error code
 */
STL_API int   stl_vector_assign_n(stl_vector *v, size_t count, const void *value);

/** @} */

/** @name Search and iteration
 *  @{ */

/** @brief Find an element by linear search.
 *
 * @param v the vector
 * @param elem pointer to the value to look for
 * @param eq equality predicate, or NULL for the default
 * @return pointer to the first match, or NULL
 */
STL_API void *stl_vector_find(const stl_vector *v, const void *elem, stl_equal_fn eq);

/** @brief Find the index of an element.
 *
 * @param v the vector
 * @param elem pointer to the value to look for
 * @param eq equality predicate, or NULL for the default
 * @return the index of the first match, or @ref STL_NPOS
 */
STL_API size_t stl_vector_index_of(const stl_vector *v, const void *elem, stl_equal_fn eq);

/** @brief Invoke a callback on every element.
 *
 * @param v the vector
 * @param fn callback; returning non-zero stops the walk
 * @param user opaque pointer forwarded to @p fn
 *
 * @warning The callback must not structurally modify the vector.
 */
STL_API void stl_vector_foreach(stl_vector *v, stl_visit_fn fn, void *user);

/** @brief Invoke a callback on every element of a read-only vector.
 *  @param v the vector
 *  @param fn callback
 *  @param user opaque pointer forwarded to @p fn */
STL_API void stl_vector_foreach_c(const stl_vector *v, stl_visit_fn fn, void *user);

/** @} */

/** @name Algorithms
 *
 * In-place operations on the vector's own storage.  The @c lower_bound,
 * @c upper_bound and @c contains family binary-searches, so it requires the
 * vector to be sorted by the same predicate.
 *  @{ */

/** @brief Sort the vector in place.
 *
 * @param v the vector
 * @param cmp ordering predicate
 *
 * Uses introsort: O(n log n) in the worst case, and not stable.  Use
 * @ref stl_vector_stable_sort when the relative order of equal elements
 * matters.
 */
STL_API void stl_vector_sort(stl_vector *v, stl_compare_fn cmp);

/** @brief Sort the vector in place, preserving the order of equal elements.
 *
 * @param v the vector
 * @param cmp ordering predicate
 *
 * Costs O(n) extra memory for the merge buffer.
 */
STL_API void stl_vector_stable_sort(stl_vector *v, stl_compare_fn cmp);

/** @brief Reverse the elements in place.
 *  @param v the vector */
STL_API void stl_vector_reverse(stl_vector *v);

/** @brief Rotate the elements left by @p n positions.
 *
 * @param v the vector
 * @param n rotation amount; values beyond the size wrap around
 */
STL_API void stl_vector_rotate(stl_vector *v, size_t n);

/** @brief Collapse each run of equal elements down to one.
 *
 * @param v the vector
 * @param eq equality predicate
 *
 * Only adjacent duplicates are removed, so sort first unless the vector is
 * already ordered.  Runs in O(n).
 */
STL_API void stl_vector_unique(stl_vector *v, stl_equal_fn eq);

/** @brief Erase every element matching a predicate, preserving order.
 *
 * @param v the vector
 * @param pred predicate; a non-zero result marks the element for removal
 * @param user opaque pointer forwarded to @p pred
 * @return how many elements were removed
 */
STL_API size_t stl_vector_remove_if(stl_vector *v, int (STL_CALL *pred)(const void *, void *), void *user);

/** @brief Overwrite every element with a value.
 *
 * @param v the vector
 * @param value pointer to the value to copy into each slot
 */
STL_API void stl_vector_fill(stl_vector *v, const void *value);

/** @brief Binary-search for the first element not less than a key.
 *
 * @param v the sorted vector
 * @param key pointer to the key
 * @param cmp ordering predicate, matching the one the vector was sorted with
 * @return the index of the first such element, or the size when none exists
 */
STL_API size_t stl_vector_lower_bound(const stl_vector *v, const void *key, stl_compare_fn cmp);

/** @brief Binary-search for the first element greater than a key.
 *
 * @param v the sorted vector
 * @param key pointer to the key
 * @param cmp ordering predicate
 * @return the index of the first such element, or the size when none exists
 */
STL_API size_t stl_vector_upper_bound(const stl_vector *v, const void *key, stl_compare_fn cmp);

/** @brief Test whether a sorted vector contains a key.
 *
 * @param v the sorted vector
 * @param key pointer to the key
 * @param cmp ordering predicate
 * @return non-zero when an equivalent element is present
 */
STL_API int stl_vector_contains(const stl_vector *v, const void *key, stl_compare_fn cmp);

/** @} */

/** @brief Bounds-checked access that blames the caller's source line.
 *
 * @param v the vector
 * @param i zero-based index
 * @param file source file, normally @c __FILE__
 * @param line source line, normally @c __LINE__
 * @return pointer to the element, or NULL when out of range
 *
 * Behaves like @ref stl_vector_at but reports the call site rather than the
 * library's own location, which makes a failing access much easier to trace.
 */
STL_API void *stl_vector_at_checked(stl_vector *v, size_t i, const char *file, int line);

/** @name Iterators
 *  @{ */

/** @brief Return an iterator to the first element.
 *  @param v the vector */
STL_API stl_iterator stl_vector_begin(stl_vector *v);

/** @brief Return the past-the-end iterator.
 *  @param v the vector */
STL_API stl_iterator stl_vector_end(stl_vector *v);

/** @brief Return an iterator to the last element.
 *  @param v the vector
 *  @return the reverse begin iterator; equal to @ref stl_vector_end when empty */
STL_API stl_iterator stl_vector_rbegin(stl_vector *v);

/** @brief Return the before-the-first iterator used to terminate a reverse walk.
 *  @param v the vector */
STL_API stl_iterator stl_vector_rend(stl_vector *v);

/** @brief Advance an iterator by one element.
 *  @param it the iterator
 *  @return the next iterator, clamped to @ref stl_vector_end */
STL_API stl_iterator stl_vector_iter_next(stl_iterator it);

/** @brief Step an iterator back by one element.
 *  @param it the iterator
 *  @return the previous iterator, clamped to the first element */
STL_API stl_iterator stl_vector_iter_prev(stl_iterator it);

/** @brief Count the elements between two iterators.
 *  @param first start iterator
 *  @param last end iterator
 *  @return the signed distance; negative when @p last precedes @p first
 *  @note Both iterators must belong to the same vector. */
STL_API ptrdiff_t    stl_vector_iter_distance(stl_iterator first, stl_iterator last);

/** @} */

/** @} */ /* end of group vector */

/* Convenience macros for vector iteration; see the "convenience macros"
 * section at the bottom of this header for the complete set. */

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ================================================================== */
/*  deque  (circular buffer of fixed-size blocks)                     */
/* ================================================================== */

/** @addtogroup deque
 *  @{
 */

/** @brief A double-ended queue backed by a ring of fixed-size blocks.
 *
 * The counterpart of @c std::deque.  Blocks are allocated as needed and linked
 * through a ring of pointers, so pushing at either end never moves an existing
 * element and never reallocates the whole container.
 *
 * @par Complexity
 * Random access O(1) with one extra indirection compared to a vector;
 * @c push_back and @c push_front amortised O(1); insertion or erasure in the
 * middle O(n), shifting whichever side is shorter.
 *
 * @par Iterator invalidation
 * Adding or removing at either end keeps existing elements where they are, so
 * pointers and iterators to them survive -- unlike a vector.  Erasing in the
 * middle shifts one of the halves, invalidating iterators on that side.
 *
 * @par When to use it
 * Prefer a deque over a vector when you need cheap insertion at the front, and
 * over a list when you also need random access.
 */
typedef struct stl_deque stl_deque;

#ifdef __cplusplus
extern "C" {
#endif

/** @name Construction and destruction
 *  @{ */

/** @brief Create a deque.
 *  @param elem_size size of one element in bytes
 *  @param elem_dtor optional destructor, or NULL
 *  @return the new deque, or NULL on failure */
STL_API stl_deque *stl_deque_new(size_t elem_size, stl_dtor_fn elem_dtor);

/** @brief Create a deque that allocates through a specific allocator.
 *  @param elem_size size of one element in bytes
 *  @param elem_dtor optional destructor, or NULL
 *  @param a allocator, or NULL for the default
 *  @return the new deque, or NULL on failure */
STL_API stl_deque *stl_deque_new_a(size_t elem_size, stl_dtor_fn elem_dtor, const stl_allocator *a);

/** @brief Destroy a deque.
 *  @param d the deque; NULL is ignored */
STL_API void       stl_deque_free(stl_deque *d);

/** @brief Deep-copy a deque.
 *  @param d the deque to copy
 *  @param copy_elem optional element copy hook
 *  @return the new deque, or NULL on failure */
STL_API stl_deque *stl_deque_copy(const stl_deque *d, stl_copy_fn copy_elem);

/** @} */

/** @name Size and access
 *  @{ */

/** @brief Return the number of elements.
 *  @param d the deque */
STL_API size_t stl_deque_size(const stl_deque *d);

/** @brief Test whether the deque is empty.
 *  @param d the deque */
STL_API int    stl_deque_empty(const stl_deque *d);

/** @brief Return the size of one element in bytes.
 *  @param d the deque */
STL_API size_t stl_deque_elem_size(const stl_deque *d);

/** @brief Return the allocator the deque was created with.
 *  @param d the deque */
STL_API const stl_allocator *stl_deque_allocator(const stl_deque *d);

/** @brief Access an element by index, with a bounds check.
 *  @param d the deque
 *  @param i zero-based index
 *  @return pointer to the element, or NULL when out of range */
STL_API void *stl_deque_at(stl_deque *d, size_t i);

/** @brief Access an element of a read-only deque.
 *  @param d the deque
 *  @param i zero-based index
 *  @return pointer to the element, or NULL when out of range */
STL_API const void *stl_deque_at_c(const stl_deque *d, size_t i);

/** @brief Access the first element.
 *  @param d the deque
 *  @return pointer to the element, or NULL when empty */
STL_API void *stl_deque_front(stl_deque *d);

/** @brief Access the last element.
 *  @param d the deque
 *  @return pointer to the element, or NULL when empty */
STL_API void *stl_deque_back(stl_deque *d);

/** @} */

/** @name Modifiers
 *  @{ */

/** @brief Append a copy of an element.
 *  @param d the deque
 *  @param elem pointer to the element to copy in
 *  @return @ref STL_OK, or an error code */
STL_API int  stl_deque_push_back(stl_deque *d, const void *elem);

/** @brief Prepend a copy of an element.
 *  @param d the deque
 *  @param elem pointer to the element to copy in
 *  @return @ref STL_OK, or an error code
 *
 * Amortised O(1), and existing elements are not moved. */
STL_API int  stl_deque_push_front(stl_deque *d, const void *elem);

/** @brief Remove the last element.
 *  @param d the deque */
STL_API void stl_deque_pop_back(stl_deque *d);

/** @brief Remove the first element.
 *  @param d the deque */
STL_API void stl_deque_pop_front(stl_deque *d);

/** @brief Insert a copy of an element at a position.
 *  @param d the deque
 *  @param pos index at which to insert; must be at most the size
 *  @param elem pointer to the element to copy in
 *  @return @ref STL_OK, or an error code */
STL_API int  stl_deque_insert(stl_deque *d, size_t pos, const void *elem);

/** @brief Erase one element.
 *  @param d the deque
 *  @param pos index of the element to erase
 *  @return @ref STL_OK, or @ref STL_ERR_RANGE */
STL_API int  stl_deque_erase(stl_deque *d, size_t pos);

/** @brief Erase a half-open range of elements.
 *  @param d the deque
 *  @param first index of the first element to erase
 *  @param last index one past the last element to erase
 *  @return @ref STL_OK, or @ref STL_ERR_RANGE */
STL_API int  stl_deque_erase_range(stl_deque *d, size_t first, size_t last);

/** @brief Remove every element, keeping the allocated blocks.
 *  @param d the deque */
STL_API void stl_deque_clear(stl_deque *d);

/** @brief Remove every element and destroy each one.
 *  @param d the deque */
STL_API void stl_deque_clear_ex(stl_deque *d);

/** @brief Release blocks that no longer hold live elements.
 *  @param d the deque */
STL_API void stl_deque_shrink_to_fit(stl_deque *d);

/** @brief Ensure room for at least @p n elements.
 *  @param d the deque
 *  @param n the required element count
 *  @return @ref STL_OK, or @ref STL_ERR_NOMEM */
STL_API int  stl_deque_reserve(stl_deque *d, size_t n);

/** @} */

/** @name Search and algorithms
 *  @{ */

/** @brief Find an element by linear search.
 *  @param d the deque
 *  @param elem pointer to the value to look for
 *  @param eq equality predicate, or NULL for the default
 *  @return pointer to the first match, or NULL */
STL_API void *stl_deque_find(const stl_deque *d, const void *elem, stl_equal_fn eq);

/** @brief Invoke a callback on every element.
 *  @param d the deque
 *  @param fn callback; a non-zero result stops the walk
 *  @param user opaque pointer forwarded to @p fn */
STL_API void  stl_deque_foreach(stl_deque *d, stl_visit_fn fn, void *user);
/** @brief Sort the deque in place.
 *  @param d the deque
 *  @param cmp ordering predicate
 *
 * Sorts by copying the elements into a flat buffer, sorting that, and copying
 * back, so it costs O(n) extra memory. */
STL_API void  stl_deque_sort(stl_deque *d, stl_compare_fn cmp);

/** @brief Reverse the elements in place.
 *  @param d the deque */
STL_API void  stl_deque_reverse(stl_deque *d);

/** @brief Exchange the contents of two deques.
 *  @param a first deque
 *  @param b second deque
 *
 * O(1): only the internal pointers are swapped, so outstanding iterators
 * follow their elements. */
STL_API void  stl_deque_swap(stl_deque *a, stl_deque *b);

/** @} */

/** @name Iterators
 *  @{ */

/** @brief Return an iterator to the first element.
 *  @param d the deque */
STL_API stl_iterator stl_deque_begin(stl_deque *d);

/** @brief Return the past-the-end iterator.
 *  @param d the deque */
STL_API stl_iterator stl_deque_end(stl_deque *d);

/** @brief Return an iterator to the last element.
 *  @param d the deque */
STL_API stl_iterator stl_deque_rbegin(stl_deque *d);

/** @brief Return the before-the-first iterator.
 *  @param d the deque */
STL_API stl_iterator stl_deque_rend(stl_deque *d);

/** @brief Advance an iterator by one element.
 *  @param it the iterator */
STL_API stl_iterator stl_deque_iter_next(stl_iterator it);

/** @brief Step an iterator back by one element.
 *  @param it the iterator */
STL_API stl_iterator stl_deque_iter_prev(stl_iterator it);

/** @brief Count the elements between two iterators.
 *  @param first start iterator
 *  @param last end iterator */
STL_API ptrdiff_t    stl_deque_iter_distance(stl_iterator first, stl_iterator last);

/** @} */

/** @} */ /* end of group deque */

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ================================================================== */
/*  list  (doubly linked list)                                        */
/* ================================================================== */

/** @addtogroup list
 *  @{
 */

/** @brief The list container type. */
typedef struct stl_list      stl_list;

/** @brief A node handle, usable as a stable position within a list.
 *
 * The element is stored immediately after this header.  Handles are opaque:
 * pass them back to the @c _node functions rather than reading the fields,
 * which exist only so that the header can be embedded.
 */
typedef struct stl_list_node  stl_list_node;

/** @brief Internal node layout.
 *
 * @warning Do not read or write these fields directly; the layout is an
 *          implementation detail and may change.  Use the accessor functions.
 */
struct stl_list_node {
    stl_list_node *prev;        /**< Previous node, or the sentinel. */
    stl_list_node *next;        /**< Next node, or the sentinel. */
    int            is_header;   /**< Non-zero only for the list's sentinel. */
    /* element bytes follow the node header */
};

#ifdef __cplusplus
extern "C" {
#endif

/** @name Construction and destruction
 *  @{ */

/** @brief Create a list.
 *  @param elem_size size of one element in bytes
 *  @param elem_dtor optional destructor, or NULL
 *  @return the new list, or NULL on failure */
STL_API stl_list *stl_list_new(size_t elem_size, stl_dtor_fn elem_dtor);

/** @brief Create a list that allocates through a specific allocator.
 *  @param elem_size size of one element in bytes
 *  @param elem_dtor optional destructor, or NULL
 *  @param a allocator, or NULL for the default
 *  @return the new list, or NULL on failure */
STL_API stl_list *stl_list_new_a(size_t elem_size, stl_dtor_fn elem_dtor, const stl_allocator *a);

/** @brief Destroy a list and every node in it.
 *  @param l the list; NULL is ignored */
STL_API void      stl_list_free(stl_list *l);

/** @brief Deep-copy a list.
 *  @param l the list to copy
 *  @param copy_elem optional element copy hook
 *  @return the new list, or NULL on failure */
STL_API stl_list *stl_list_copy(const stl_list *l, stl_copy_fn copy_elem);

/** @} */

/** @name Size and access
 *  @{ */

/** @brief Return the number of elements.
 *  @param l the list */
STL_API size_t stl_list_size(const stl_list *l);

/** @brief Test whether the list is empty.
 *  @param l the list */
STL_API int    stl_list_empty(const stl_list *l);

/** @brief Return the size of one element in bytes.
 *  @param l the list */
STL_API size_t stl_list_elem_size(const stl_list *l);

/** @brief Return the allocator the list was created with.
 *  @param l the list */
STL_API const stl_allocator *stl_list_allocator(const stl_list *l);

/** @brief Access the first element.
 *  @param l the list
 *  @return pointer to the element, or NULL when empty */
STL_API void *stl_list_front(stl_list *l);

/** @brief Access the last element.
 *  @param l the list
 *  @return pointer to the element, or NULL when empty */
STL_API void *stl_list_back(stl_list *l);

/** @brief Access the element at an index.
 *  @param l the list
 *  @param i zero-based index
 *  @return pointer to the element, or NULL when out of range
 *  @warning O(n): the list has to be walked.  A list is the wrong container
 *           if you need indexed access. */
STL_API void *stl_list_at(stl_list *l, size_t i);

/** @brief Test whether an element is present.
 *  @param l the list
 *  @param elem pointer to the value to look for
 *  @param eq equality predicate, or NULL for the default
 *  @return non-zero when found */
STL_API int   stl_list_contains(const stl_list *l, const void *elem, stl_equal_fn eq);

/** @} */

/** @name Modifiers
 *
 * The @c push and @c insert functions return the new node so that the caller
 * can keep a stable handle to the element.  That handle stays valid until the
 * node is erased, regardless of what happens to the rest of the list.
 *  @{ */

/** @brief Append a copy of an element.
 *  @param l the list
 *  @param elem pointer to the element to copy in
 *  @return the new node, or NULL on failure */
STL_API stl_list_node *stl_list_push_back(stl_list *l, const void *elem);

/** @brief Prepend a copy of an element.
 *  @param l the list
 *  @param elem pointer to the element to copy in
 *  @return the new node, or NULL on failure */
STL_API stl_list_node *stl_list_push_front(stl_list *l, const void *elem);

/** @brief Insert a copy of an element directly after a node.
 *  @param l the list
 *  @param node the node to insert after; NULL inserts at the front
 *  @param elem pointer to the element to copy in
 *  @return the new node, or NULL on failure */
STL_API stl_list_node *stl_list_insert_after(stl_list *l, stl_list_node *node, const void *elem);

/** @brief Insert a copy of an element directly before a node.
 *  @param l the list
 *  @param node the node to insert before; NULL appends at the back
 *  @param elem pointer to the element to copy in
 *  @return the new node, or NULL on failure */
STL_API stl_list_node *stl_list_insert_before(stl_list *l, stl_list_node *node, const void *elem);

/** @brief Insert a copy of an element at an index.
 *  @param l the list
 *  @param pos zero-based insertion position; must be at most the size
 *  @param elem pointer to the element to copy in
 *  @return the new node, or NULL on failure */
STL_API stl_list_node *stl_list_insert(stl_list *l, size_t pos, const void *elem);

/** @brief Remove the last element.
 *  @param l the list */
STL_API void stl_list_pop_back(stl_list *l);

/** @brief Remove the first element.
 *  @param l the list */
STL_API void stl_list_pop_front(stl_list *l);

/** @brief Erase the element at an index.
 *  @param l the list
 *  @param pos zero-based index
 *  @return @ref STL_OK, or @ref STL_ERR_RANGE */
STL_API int  stl_list_erase(stl_list *l, size_t pos);

/** @brief Erase a half-open range of elements.
 *  @param l the list
 *  @param first index of the first element to erase
 *  @param last index one past the last element to erase
 *  @return @ref STL_OK, or @ref STL_ERR_RANGE */
STL_API int  stl_list_erase_range(stl_list *l, size_t first, size_t last);

/** @brief Unlink and destroy one node.
 *  @param l the list
 *  @param node the node to erase
 *  @return @ref STL_OK, or @ref STL_ERR_INVALID
 *
 * O(1), and no other node moves.  This is the function to call from inside
 * @c stl_list_foreach_safe. */
STL_API int  stl_list_erase_node(stl_list *l, stl_list_node *node);

/** @brief Remove every element, keeping the list structure.
 *  @param l the list */
STL_API void stl_list_clear(stl_list *l);

/** @brief Remove every element and destroy each one.
 *  @param l the list */
STL_API void stl_list_clear_ex(stl_list *l);

/** @} */

/** @name Whole-list operations
 *  @{ */

/** @brief Move every element of one list into another.
 *
 * @param dst the destination list
 * @param dst_pos index in @p dst at which to insert
 * @param src the source list, left empty
 * @return @ref STL_OK, or an error code
 *
 * O(1): the nodes are relinked, not copied, so element pointers stay valid.
 */
STL_API int stl_list_splice(stl_list *dst, size_t dst_pos, stl_list *src);

/** @brief Merge two sorted lists into one.
 *
 * @param a the destination list, which must be sorted
 * @param b the source list, also sorted; left empty
 * @param cmp ordering predicate
 * @return @ref STL_OK, or an error code
 *
 * Stable with respect to @p a: elements of @p a come first among equals.
 */
STL_API int stl_list_merge(stl_list *a, stl_list *b, stl_compare_fn cmp);

/** @brief Sort the list in place.
 *  @param l the list
 *  @param cmp ordering predicate
 *  @return @ref STL_OK, or an error code
 *  @note The node handles survive the sort; only the elements move between
 *        nodes. */
STL_API int stl_list_sort(stl_list *l, stl_compare_fn cmp);

/** @brief Sort the list in place, preserving the order of equal elements.
 *  @param l the list
 *  @param cmp ordering predicate
 *  @return @ref STL_OK, or an error code */
STL_API int stl_list_sort_stable(stl_list *l, stl_compare_fn cmp);

/** @brief Reverse the list in place.
 *  @param l the list */
STL_API void stl_list_reverse(stl_list *l);

/** @brief Collapse each run of equal elements down to one.
 *  @param l the list
 *  @param eq equality predicate
 *
 * Only adjacent duplicates are removed; sort first if needed. */
STL_API void stl_list_unique(stl_list *l, stl_equal_fn eq);

/** @brief Erase every element matching a predicate.
 *  @param l the list
 *  @param pred predicate; a non-zero result marks the element for removal
 *  @param user opaque pointer forwarded to @p pred
 *  @return how many elements were removed */
STL_API size_t stl_list_remove_if(stl_list *l, int (STL_CALL *pred)(const void *, void *), void *user);

/** @brief Copy a range of elements into a new list.
 *  @param l the source list
 *  @param first index of the first element to copy
 *  @param last index one past the last element to copy
 *  @return the new list, or NULL on failure */
STL_API stl_list *stl_list_slice(const stl_list *l, size_t first, size_t last);

/** @} */

/** @name Search and iteration
 *  @{ */

/** @brief Find an element by linear search.
 *  @param l the list
 *  @param elem pointer to the value to look for
 *  @param eq equality predicate, or NULL for the default
 *  @return pointer to the first match, or NULL */
STL_API void *stl_list_find(const stl_list *l, const void *elem, stl_equal_fn eq);

/** @brief Invoke a callback on every element.
 *  @param l the list
 *  @param fn callback; a non-zero result stops the walk
 *  @param user opaque pointer forwarded to @p fn
 *
 * The successor is computed before the callback runs, so erasing the current
 * node from inside the callback is safe.  The @c stl_list_foreach_safe macro
 * exposes that guarantee explicitly. */
STL_API void  stl_list_foreach(stl_list *l, stl_visit_fn fn, void *user);

/** @brief Invoke a callback on every element of a read-only list.
 *  @param l the list
 *  @param fn callback
 *  @param user opaque pointer forwarded to @p fn */
STL_API void  stl_list_foreach_c(const stl_list *l, stl_visit_fn fn, void *user);

/** @} */

/** @name Node handles
 *
 * An element address obtained from a node stays valid until that node is
 * erased, whatever else happens to the list.  These functions are what the
 * @c foreach macros are built on, and are useful for hand-written walks.
 *  @{ */

/** @brief Return the element stored in a node.
 *  @param node the node
 *  @return pointer to the element, or NULL for a sentinel or detached node */
STL_API void       *stl_list_node_data(stl_list_node *node);

/** @brief Return the successor of a node.
 *  @param node the node
 *  @return the next node, or NULL when @p node is the last one */
STL_API stl_list_node *stl_list_node_next(stl_list_node *node);

/** @brief Return the predecessor of a node.
 *  @param node the node
 *  @return the previous node, or NULL when @p node is the first one */
STL_API stl_list_node *stl_list_node_prev(stl_list_node *node);

/** @brief Return the first node of a list.
 *  @param l the list
 *  @return the first node, or NULL when the list is empty */
STL_API stl_list_node *stl_list_begin_node(stl_list *l);

/** @brief Return the sentinel that terminates a node walk.
 *  @param l the list
 *  @return NULL, which is what the traversal functions return at the end */
STL_API stl_list_node *stl_list_end_node(stl_list *l);

/** @} */

/** @name Iterators
 *  @{ */

/** @brief Return an iterator to the first element.
 *  @param l the list */
STL_API stl_iterator stl_list_begin(stl_list *l);

/** @brief Return the past-the-end iterator.
 *  @param l the list */
STL_API stl_iterator stl_list_end(stl_list *l);

/** @brief Return an iterator to the last element.
 *  @param l the list */
STL_API stl_iterator stl_list_rbegin(stl_list *l);

/** @brief Return the before-the-first iterator.
 *  @param l the list */
STL_API stl_iterator stl_list_rend(stl_list *l);

/** @brief Advance an iterator by one element.
 *  @param it the iterator */
STL_API stl_iterator stl_list_iter_next(stl_iterator it);

/** @brief Step an iterator back by one element.
 *  @param it the iterator */
STL_API stl_iterator stl_list_iter_prev(stl_iterator it);

/** @brief Count the elements between two iterators.
 *  @param first start iterator
 *  @param last end iterator
 *  @note O(1) for a list, because the iterator carries its ordinal position. */
STL_API ptrdiff_t    stl_list_iter_distance(stl_iterator first, stl_iterator last);

/** @} */

/** @} */ /* end of group list */

/* List iteration macros live in the "convenience macros" section below. */

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ================================================================== */
/*  rbtree -- generic ordered container backing set/map               */
/* ================================================================== */

/** @addtogroup rbtree
 *  @{
 */

/** @brief A balanced binary search tree of fixed-size elements. */
typedef struct stl_rbtree     stl_rbtree;

/** @brief A handle to one tree node.
 *
 * Stays valid until that node is erased, so it can be held across insertions
 * and other erasures. */
typedef struct stl_rbtree_node stl_rbtree_node;

#ifdef __cplusplus
extern "C" {
#endif

/** @brief How the tree treats elements with equivalent keys. */
typedef enum stl_rbtree_policy {
    STL_RBTREE_MULTI   = 0,   /**< Keep every element; multiset/multimap semantics. */
    STL_RBTREE_UNIQUE  = 1    /**< Keep one per key; set/map semantics. */
} stl_rbtree_policy;

/** @brief Create a tree.
 *
 * @param elem_size size of one element in bytes
 * @param key_offset byte offset of the key within an element
 * @param key_size byte size of the key; 0 means @p elem_size
 * @param policy whether duplicate keys are allowed
 * @param key_cmp ordering predicate over keys, or NULL for the default
 * @param elem_dtor optional destructor, or NULL
 * @param a allocator, or NULL for the default
 * @return the new tree, or NULL on failure
 *
 * When @p key_size equals @p elem_size and @p key_offset is 0, the element
 * @e is the key.  That is the case @ref stl_set uses; @ref stl_map instead
 * places a value after the key within the same element.
 *
 * @note Prefer @ref stl_set_new or @ref stl_map_new; this constructor is for
 *       code that needs direct control over the key layout.
 */
STL_API stl_rbtree *stl_rbtree_new(size_t elem_size, size_t key_offset, size_t key_size,
                                   stl_rbtree_policy policy, stl_compare_fn key_cmp,
                                   stl_dtor_fn elem_dtor, const stl_allocator *a);

/** @brief Destroy a tree.
 *  @param t the tree; NULL is ignored */
STL_API void       stl_rbtree_free(stl_rbtree *t);

/** @brief Deep-copy a tree.
 *  @param t the tree to copy
 *  @param copy_elem optional element copy hook
 *  @return the new tree, or NULL on failure */
STL_API stl_rbtree *stl_rbtree_copy(const stl_rbtree *t, stl_copy_fn copy_elem);

/** @brief Return the number of elements.
 *  @param t the tree */
STL_API size_t stl_rbtree_size(const stl_rbtree *t);

/** @brief Test whether the tree is empty.
 *  @param t the tree */
STL_API int    stl_rbtree_empty(const stl_rbtree *t);

/** @brief Return the size of one element in bytes.
 *  @param t the tree */
STL_API size_t stl_rbtree_elem_size(const stl_rbtree *t);

/** @brief Return the size of a key in bytes.
 *  @param t the tree */
STL_API size_t stl_rbtree_key_size(const stl_rbtree *t);

/** @name Insertion
 *  @{ */

/** @brief Insert a copy of an element.
 *
 * @param t the tree
 * @param elem pointer to the element to copy in
 * @return the node holding the element; for a unique tree, the existing node
 *         when an equivalent key is already present
 *
 * O(log n).  Check the size before and after to tell whether a unique-key
 * insert actually happened, or use the @ref stl_set and @ref stl_map wrappers,
 * which do that for you.
 */
STL_API stl_rbtree_node *stl_rbtree_insert(stl_rbtree *t, const void *elem);

/** @brief Erase one element by key.
 *
 * @param t the tree
 * @param key pointer to the key to erase
 * @return @ref STL_OK, or @ref STL_ERR_NOT_FOUND
 *
 * In a multi-tree, erases one of the matching elements, walking the run from
 * the leftmost so that repeated calls remove them in a predictable order.
 */
STL_API int   stl_rbtree_erase(stl_rbtree *t, const void *key);

/** @brief Erase one node.
 *
 * @param t the tree
 * @param node the node to erase
 * @return @ref STL_OK, or @ref STL_ERR_INVALID
 */
STL_API int   stl_rbtree_erase_node(stl_rbtree *t, stl_rbtree_node *node);

/** @brief Erase every element whose key lies in a half-open range.
 *
 * @param t the tree
 * @param lo inclusive lower key bound; NULL means unbounded below
 * @param hi exclusive upper key bound; NULL means unbounded above
 * @return how many elements were erased
 */
STL_API int   stl_rbtree_erase_range(stl_rbtree *t, const void *lo, const void *hi);

/** @brief Remove every element, keeping the tree structure.
 *  @param t the tree */
STL_API void  stl_rbtree_clear(stl_rbtree *t);

/** @brief Remove every element and destroy each one.
 *  @param t the tree */
STL_API void  stl_rbtree_clear_ex(stl_rbtree *t);

/** @} */

/** @name Lookup
 *  @{ */

/** @brief Find an element by key.
 *  @param t the tree
 *  @param key pointer to the key to look up
 *  @return the matching node, or NULL */
STL_API stl_rbtree_node *stl_rbtree_find(stl_rbtree *t, const void *key);

/** @brief Find an element by key in a read-only tree.
 *  @param t the tree
 *  @param key pointer to the key to look up
 *  @return the matching node, or NULL */
STL_API const stl_rbtree_node *stl_rbtree_find_c(const stl_rbtree *t, const void *key);

/** @brief Count the elements matching a key.
 *  @param t the tree
 *  @param key pointer to the key
 *  @return 0 or 1 for a unique tree; the run length for a multi-tree */
STL_API size_t stl_rbtree_count(const stl_rbtree *t, const void *key);

/** @brief Test whether a key is present.
 *  @param t the tree
 *  @param key pointer to the key
 *  @return non-zero when present */
STL_API int    stl_rbtree_contains(const stl_rbtree *t, const void *key);

/** @brief Find the first element not ordered before a key.
 *  @param t the tree
 *  @param key pointer to the key
 *  @return the node, or NULL when every key orders before @p key */
STL_API stl_rbtree_node *stl_rbtree_lower_bound(stl_rbtree *t, const void *key);

/** @brief Find the first element ordered after a key.
 *  @param t the tree
 *  @param key pointer to the key
 *  @return the node, or NULL */
STL_API stl_rbtree_node *stl_rbtree_upper_bound(stl_rbtree *t, const void *key);

/** @brief Return the half-open range of elements matching a key.
 *
 * @param t the tree
 * @param key pointer to the key
 * @param first receives the start of the run, or NULL
 * @param last receives one past the end of the run, or NULL
 */
STL_API void  stl_rbtree_equal_range(stl_rbtree *t, const void *key,
                                     stl_rbtree_node **first, stl_rbtree_node **last);

/** @brief Return the leftmost node, which holds the smallest key.
 *  @param t the tree
 *  @return the node, or NULL when the tree is empty */
STL_API stl_rbtree_node *stl_rbtree_first(stl_rbtree *t);

/** @brief Return the rightmost node, which holds the largest key.
 *  @param t the tree
 *  @return the node, or NULL when the tree is empty */
STL_API stl_rbtree_node *stl_rbtree_last(stl_rbtree *t);

/** @brief Return the next node in ascending key order.
 *  @param node the current node
 *  @return the successor, or NULL at the last element */
STL_API stl_rbtree_node *stl_rbtree_next(stl_rbtree_node *node);

/** @brief Return the previous node in descending key order.
 *  @param node the current node
 *  @return the predecessor, or NULL at the first element */
STL_API stl_rbtree_node *stl_rbtree_prev(stl_rbtree_node *node);

/** @brief Return the element stored in a node.
 *  @param node the node
 *  @return pointer to the element, or NULL for a NULL node */
STL_API void  *stl_rbtree_node_data(stl_rbtree_node *node);

/** @brief Test whether a node is the past-the-end sentinel.
 *  @param t the tree
 *  @param node the node to test
 *  @return non-zero when @p node does not refer to a real element */
STL_API int    stl_rbtree_node_is_end(const stl_rbtree *t, const stl_rbtree_node *node);

/** @brief Check the red-black invariants.
 *
 * @param t the tree
 * @return non-zero when the tree is well formed
 *
 * Verifies that node colours and black heights are consistent, that parent
 * links agree, that the in-order walk is sorted, and that the walk visits
 * exactly @ref stl_rbtree_size nodes.  Intended for tests and debugging; it is
 * O(n).
 */
STL_API int stl_rbtree_validate(const stl_rbtree *t);

/** @} */

/** @name Iterators
 *  @{ */

/** @brief Return an iterator to the smallest element.
 *  @param t the tree */
STL_API stl_iterator stl_rbtree_begin(stl_rbtree *t);

/** @brief Return the past-the-end iterator.
 *  @param t the tree */
STL_API stl_iterator stl_rbtree_end(stl_rbtree *t);

/** @brief Return an iterator to the largest element.
 *  @param t the tree */
STL_API stl_iterator stl_rbtree_rbegin(stl_rbtree *t);

/** @brief Return the before-the-first iterator.
 *  @param t the tree */
STL_API stl_iterator stl_rbtree_rend(stl_rbtree *t);

/** @brief Advance an iterator in ascending key order.
 *  @param it the iterator */
STL_API stl_iterator stl_rbtree_iter_next(stl_iterator it);

/** @brief Step an iterator back in descending key order.
 *  @param it the iterator */
STL_API stl_iterator stl_rbtree_iter_prev(stl_iterator it);

/** @brief Count the elements between two iterators.
 *  @param first start iterator
 *  @param last end iterator */
STL_API ptrdiff_t    stl_rbtree_iter_distance(stl_iterator first, stl_iterator last);

/** @} */

/** @} */ /* end of group rbtree */

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ================================================================== */
/* ================================================================== */
/*  set / multiset                                                     */
/* ================================================================== */

/** @addtogroup set
 *  @{
 */

/** @brief An ordered set of unique elements. */
typedef stl_rbtree stl_set;

/** @brief A handle to one element of a set. */
typedef stl_rbtree_node stl_set_node;

/** @brief Whether a set keeps duplicates; see @ref stl_rbtree_policy. */
typedef stl_rbtree_policy stl_set_policy;

#define STL_SET_UNIQUE STL_RBTREE_UNIQUE  /**< Duplicate elements are rejected. */
#define STL_SET_MULTI  STL_RBTREE_MULTI   /**< Duplicate elements are kept. */

#ifdef __cplusplus
extern "C" {
#endif

/** @name Construction and destruction
 *  @{ */

/** @brief Create an ordered set.
 *
 * @param elem_size size of one element in bytes
 * @param cmp ordering predicate over elements; NULL selects the default, which
 *            is only correct for scalar elements of pointer width
 * @param elem_dtor optional destructor, or NULL
 * @return the new set, or NULL on failure
 *
 * @code
 * stl_set *s = stl_set_new(sizeof(int), stl_cmp_int32, NULL);
 * @endcode
 */
STL_API stl_set *stl_set_new(size_t elem_size, stl_compare_fn cmp, stl_dtor_fn elem_dtor);

/** @brief Create an ordered set that allocates through a specific allocator.
 *  @param elem_size size of one element in bytes
 *  @param cmp ordering predicate, or NULL for the default
 *  @param elem_dtor optional destructor, or NULL
 *  @param a allocator, or NULL for the default
 *  @return the new set, or NULL on failure */
STL_API stl_set *stl_set_new_a(size_t elem_size, stl_compare_fn cmp, stl_dtor_fn elem_dtor,
                               const stl_allocator *a);

/** @brief Create an ordered set or multiset.
 *  @param elem_size size of one element in bytes
 *  @param cmp ordering predicate, or NULL for the default
 *  @param policy whether duplicate elements are kept
 *  @param elem_dtor optional destructor, or NULL
 *  @param a allocator, or NULL for the default
 *  @return the new set, or NULL on failure */
STL_API stl_set *stl_set_new_policy(size_t elem_size, stl_compare_fn cmp, stl_set_policy policy,
                                    stl_dtor_fn elem_dtor, const stl_allocator *a);

/** @brief Build a set by inserting every element of an array.
 *  @param array pointer to the source elements
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param cmp ordering predicate, or NULL for the default
 *  @param policy whether duplicate elements are kept
 *  @param a allocator, or NULL for the default
 *  @return the new set, or NULL on failure */
STL_API stl_set *stl_set_from_array(const void *array, size_t count, size_t elem_size,
                                    stl_compare_fn cmp, stl_set_policy policy, const stl_allocator *a);

/** @brief Destroy a set.
 *  @param s the set; NULL is ignored */
STL_API void    stl_set_free(stl_set *s);

/** @brief Deep-copy a set.
 *  @param s the set to copy
 *  @param copy_elem optional element copy hook
 *  @return the new set, or NULL on failure */
STL_API stl_set *stl_set_copy(const stl_set *s, stl_copy_fn copy_elem);

/** @} */

/** @name Size and membership
 *  @{ */

/** @brief Return the number of elements.
 *  @param s the set */
STL_API size_t stl_set_size(const stl_set *s);

/** @brief Test whether the set is empty.
 *  @param s the set */
STL_API int    stl_set_empty(const stl_set *s);

/** @brief Insert an element.
 *
 * @param s the set
 * @param elem pointer to the element to copy in
 * @return pointer to the stored element, or NULL when a unique set already
 *         held an equivalent key
 *
 * The returned pointer is to the set's own copy, not to @p elem.  In a
 * multiset the insert always succeeds and the new element is returned.
 */
STL_API const void *stl_set_insert(stl_set *s, const void *elem);

/** @brief Erase an element.
 *
 * @param s the set
 * @param elem pointer to a key-equivalent element
 * @return @ref STL_OK, or @ref STL_ERR_NOT_FOUND
 *
 * In a multiset this erases one matching element; call it repeatedly to drain
 * a run.
 */
STL_API int   stl_set_erase(stl_set *s, const void *elem);

/** @brief Remove every element.
 *  @param s the set
 *  @note Does not call @c elem_dtor. */
STL_API void  stl_set_clear(stl_set *s);

/** @brief Remove every element and destroy each one.
 *  @param s the set */
STL_API void  stl_set_clear_ex(stl_set *s);

/** @brief Look up an element.
 *  @param s the set
 *  @param elem pointer to a key-equivalent element
 *  @return pointer to the stored element, or NULL when absent */
STL_API const void *stl_set_find(const stl_set *s, const void *elem);

/** @brief Count the elements equivalent to a key.
 *  @param s the set
 *  @param elem pointer to the key
 *  @return 0 or 1 in a unique set; the run length in a multiset */
STL_API size_t stl_set_count(const stl_set *s, const void *elem);

/** @brief Test whether an element is present.
 *  @param s the set
 *  @param elem pointer to the key
 *  @return non-zero when present */
STL_API int    stl_set_contains(const stl_set *s, const void *elem);

/** @brief Find the first element not ordered before a key.
 *  @param s the set
 *  @param elem pointer to the key
 *  @return pointer to the element, or NULL */
STL_API const void *stl_set_lower_bound(const stl_set *s, const void *elem);

/** @brief Find the first element ordered after a key.
 *  @param s the set
 *  @param elem pointer to the key
 *  @return pointer to the element, or NULL */
STL_API const void *stl_set_upper_bound(const stl_set *s, const void *elem);

/** @brief Verify the set's internal invariants.
 *  @param s the set
 *  @return non-zero when the underlying tree is well formed
 *  @see stl_rbtree_validate */
STL_API int stl_set_validate(const stl_set *s);

/** @} */

/** @name Set algebra
 *
 * Each operation allocates a new set holding the result; the operands are left
 * untouched.  All of them require both sets to use the same element size and
 * ordering predicate.
 *  @{ */

/** @brief Build the union of two sets.
 *  @param a first set
 *  @param b second set
 *  @param copy_elem optional element copy hook, or NULL
 *  @return the new set, or NULL on failure */
STL_API stl_set *stl_set_union(const stl_set *a, const stl_set *b, stl_copy_fn copy_elem);

/** @brief Build the intersection of two sets.
 *  @param a first set
 *  @param b second set
 *  @param copy_elem optional element copy hook, or NULL
 *  @return the new set, or NULL on failure */
STL_API stl_set *stl_set_intersection(const stl_set *a, const stl_set *b, stl_copy_fn copy_elem);

/** @brief Build the elements of @p a that are absent from @p b.
 *  @param a first set
 *  @param b second set
 *  @param copy_elem optional element copy hook, or NULL
 *  @return the new set, or NULL on failure */
STL_API stl_set *stl_set_difference(const stl_set *a, const stl_set *b, stl_copy_fn copy_elem);

/** @brief Build the elements present in exactly one of two sets.
 *  @param a first set
 *  @param b second set
 *  @param copy_elem optional element copy hook, or NULL
 *  @return the new set, or NULL on failure */
STL_API stl_set *stl_set_symmetric_difference(const stl_set *a, const stl_set *b, stl_copy_fn copy_elem);

/** @brief Test whether one set is contained in another.
 *  @param a the candidate subset
 *  @param b the candidate superset
 *  @return non-zero when every element of @p a is also in @p b */
STL_API int stl_set_is_subset(const stl_set *a, const stl_set *b);

/** @} */

/** @name Iteration
 *  @{ */

/** @brief Invoke a callback on every element, in ascending order.
 *  @param s the set
 *  @param fn callback; a non-zero result stops the walk
 *  @param user opaque pointer forwarded to @p fn */
STL_API void stl_set_foreach(stl_set *s, stl_visit_fn fn, void *user);

/** @brief Invoke a callback on every element of a read-only set.
 *  @param s the set
 *  @param fn callback
 *  @param user opaque pointer forwarded to @p fn */
STL_API void stl_set_foreach_c(const stl_set *s, stl_visit_fn fn, void *user);

/** @brief Return an iterator to the smallest element.
 *  @param s the set */
STL_API stl_iterator stl_set_begin(stl_set *s);

/** @brief Return the past-the-end iterator.
 *  @param s the set */
STL_API stl_iterator stl_set_end(stl_set *s);

/** @brief Return an iterator to the largest element.
 *  @param s the set */
STL_API stl_iterator stl_set_rbegin(stl_set *s);

/** @brief Return the before-the-first iterator.
 *  @param s the set */
STL_API stl_iterator stl_set_rend(stl_set *s);

/** @brief Advance an iterator in ascending order.
 *  @param it the iterator */
STL_API stl_iterator stl_set_iter_next(stl_iterator it);

/** @brief Step an iterator back in descending order.
 *  @param it the iterator */
STL_API stl_iterator stl_set_iter_prev(stl_iterator it);

/** @} */

/** @} */ /* end of group set */

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ================================================================== */
/* ================================================================== */
/*  map / multimap                                                     */
/* ================================================================== */

/** @addtogroup map
 *  @{
 */

/** @brief An ordered map from keys to values.
 *
 * The map owns a red-black tree and remembers the element layout it was
 * created with, so the offset of the value within an element travels with the
 * container.  There is no limit on how many maps may exist at once.
 */
typedef struct stl_map stl_map;

/** @brief A handle to one key/value entry of a map. */
typedef stl_rbtree_node stl_map_node;

/** @brief Whether a map keeps duplicate keys; see @ref stl_rbtree_policy. */
typedef stl_rbtree_policy stl_map_policy;

#define STL_MAP_UNIQUE STL_RBTREE_UNIQUE  /**< One entry per key. */
#define STL_MAP_MULTI  STL_RBTREE_MULTI   /**< Multiple entries per key. */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Key/value pair header, usable at the front of any map element type.
 *
 * The map itself never uses this type: it works with the caller's own struct,
 * which must begin with the key.  Declaring the element as
 *
 *     typedef struct { int key; int value; } int_pair;
 *
 * and constructing the map with @c STL_OFFSETOF(int_pair, @c value) is the
 * usual pattern.
 */
typedef struct stl_pair {
    void *first;    /**< Key.   */
    void *second;   /**< Value. */
} stl_pair;

/** @name Construction and destruction
 *  @{ */

/** @brief Create an ordered map.
 *
 * @param elem_size size of one key/value element in bytes
 * @param value_offset byte offset of the value within that element
 * @param key_size byte size of the key, which starts at offset 0
 * @param key_cmp ordering predicate over keys, or NULL for the default
 * @param elem_dtor optional destructor, or NULL
 * @return the new map, or NULL on failure
 *
 * @code
 * typedef struct { int key; const char *value; } entry;
 * stl_map *m = stl_map_new(sizeof(entry), STL_OFFSETOF(entry, value),
 *                          sizeof(int), stl_cmp_int32, NULL);
 * @endcode
 */
STL_API stl_map *stl_map_new(size_t elem_size, size_t value_offset, size_t key_size,
                             stl_compare_fn key_cmp, stl_dtor_fn elem_dtor);

/** @brief Create an ordered map that allocates through a specific allocator.
 *  @param elem_size size of one key/value element in bytes
 *  @param value_offset byte offset of the value within that element
 *  @param key_size byte size of the key
 *  @param key_cmp ordering predicate over keys, or NULL for the default
 *  @param elem_dtor optional destructor, or NULL
 *  @param a allocator, or NULL for the default
 *  @return the new map, or NULL on failure */
STL_API stl_map *stl_map_new_a(size_t elem_size, size_t value_offset, size_t key_size,
                               stl_compare_fn key_cmp, stl_dtor_fn elem_dtor,
                               const stl_allocator *a);

/** @brief Create an ordered map or multimap.
 *  @param elem_size size of one key/value element in bytes
 *  @param value_offset byte offset of the value within that element
 *  @param key_size byte size of the key
 *  @param key_cmp ordering predicate over keys, or NULL for the default
 *  @param policy whether duplicate keys are kept
 *  @param elem_dtor optional destructor, or NULL
 *  @param a allocator, or NULL for the default
 *  @return the new map, or NULL on failure */
STL_API stl_map *stl_map_new_policy(size_t elem_size, size_t value_offset, size_t key_size,
                                    stl_compare_fn key_cmp, stl_map_policy policy,
                                    stl_dtor_fn elem_dtor, const stl_allocator *a);

/** @brief Destroy a map.
 *  @param m the map; NULL is ignored */
STL_API void     stl_map_free(stl_map *m);

/** @brief Deep-copy a map.
 *  @param m the map to copy
 *  @param copy_elem optional element copy hook
 *  @return the new map, or NULL on failure
 *
 */
STL_API stl_map *stl_map_copy(const stl_map *m, stl_copy_fn copy_elem);

/** @} */

/** @name Size and lookup
 *  @{ */

/** @brief Return the number of entries.
 *  @param m the map */
STL_API size_t stl_map_size(const stl_map *m);

/** @brief Test whether the map is empty.
 *  @param m the map */
STL_API int    stl_map_empty(const stl_map *m);

/** @brief Find the entry for a key.
 *  @param m the map
 *  @param key pointer to the key to look up
 *  @return pointer to the stored key/value element, or NULL */
STL_API void       *stl_map_find(stl_map *m, const void *key);

/** @brief Find the entry for a key in a read-only map.
 *  @param m the map
 *  @param key pointer to the key
 *  @return pointer to the stored element, or NULL */
STL_API const void *stl_map_find_c(const stl_map *m, const void *key);

/** @brief Return the value for a key.
 *  @param m the map
 *  @param key pointer to the key
 *  @return pointer to the value inside the map, or NULL when the key is absent
 *
 * The pointer aliases the map's own storage and stays valid until the entry is
 * erased. */
STL_API void       *stl_map_get(stl_map *m, const void *key);

/** @brief Return the value for a key in a read-only map.
 *  @param m the map
 *  @param key pointer to the key
 *  @return pointer to the value, or NULL */
STL_API const void *stl_map_get_c(const stl_map *m, const void *key);

/** @brief Return the value slot for a key, inserting a default when needed.
 *
 * @param m the map
 * @param key pointer to the key
 * @param default_value pointer to the value to install when the key is new,
 *                      or NULL to zero-fill
 * @return pointer to the value slot, or NULL on failure
 *
 * The usual way to accumulate into a map without a separate existence check.
 */
STL_API void  *stl_map_get_or_insert(stl_map *m, const void *key, const void *default_value);

/** @brief Return the value for a key that is known to be present.
 *
 * @param m the map
 * @param key pointer to the key
 * @return pointer to the value, or NULL when the key is absent
 *
 * @warning Unlike @c std::map::at this does not report a missing key
 *          distinctly; it simply returns NULL.  Use @ref stl_map_get when the
 *          key may legitimately be absent.
 */
STL_API void  *stl_map_at(stl_map *m, const void *key);

/** @brief Count the entries matching a key.
 *  @param m the map
 *  @param key pointer to the key
 *  @return 0 or 1 for a unique map; the run length for a multimap */
STL_API size_t stl_map_count(const stl_map *m, const void *key);

/** @brief Test whether a key is present.
 *  @param m the map
 *  @param key pointer to the key
 *  @return non-zero when present */
STL_API int    stl_map_contains(const stl_map *m, const void *key);

/** @brief Find the first entry whose key is not ordered before @p key.
 *  @param m the map
 *  @param key pointer to the key
 *  @return pointer to the element, or NULL */
STL_API void  *stl_map_lower_bound(stl_map *m, const void *key);
/** @brief Find the first entry whose key is ordered after @p key.
 *  @param m the map
 *  @param key pointer to the key
 *  @return pointer to the element, or NULL */
STL_API void  *stl_map_upper_bound(stl_map *m, const void *key);

/** @brief Return the half-open range of entries matching a key.
 *  @param m the map
 *  @param key pointer to the key
 *  @param first receives the start of the run, or NULL
 *  @param last receives one past the end of the run, or NULL */
STL_API void   stl_map_equal_range(stl_map *m, const void *key, void **first, void **last);

/** @brief Verify the map's internal invariants.
 *  @param m the map
 *  @return non-zero when the underlying tree is well formed */
STL_API int    stl_map_validate(const stl_map *m);

/** @} */

/** @name Modifiers
 *  @{ */

/** @brief Insert a complete entry.
 *
 * @param m the map
 * @param pair pointer to the key/value element to copy in
 * @return pointer to the stored element, or NULL when a unique map already
 *         held the key
 *
 * The existing value is @e not overwritten on a duplicate key; use
 * @ref stl_map_put when you want assignment semantics.
 */
STL_API void *stl_map_insert(stl_map *m, const void *pair);

/** @brief Insert or assign.
 *
 * @param m the map
 * @param key pointer to the key
 * @param value pointer to the value to store, or NULL to zero-fill it
 * @return pointer to the stored element, or NULL on failure
 *
 * Equivalent to C++'s @c operator[] assignment: an existing entry has its value
 * replaced, a missing one is created.  Never fails on a duplicate key.
 */
STL_API void *stl_map_put(stl_map *m, const void *key, const void *value);

/** @brief Erase the entry for a key.
 *  @param m the map
 *  @param key pointer to the key
 *  @return @ref STL_OK, or @ref STL_ERR_NOT_FOUND */
STL_API int   stl_map_erase(stl_map *m, const void *key);

/** @brief Remove every entry.
 *  @param m the map
 *  @note Does not call @c elem_dtor. */
STL_API void  stl_map_clear(stl_map *m);

/** @brief Remove every entry and destroy each one.
 *  @param m the map */
STL_API void  stl_map_clear_ex(stl_map *m);

/** @} */

/** @name Iteration
 *  @{ */

/** @brief Invoke a callback on every entry, in ascending key order.
 *  @param m the map
 *  @param fn callback receiving a pointer to the whole key/value element; a
 *            non-zero result stops the walk
 *  @param user opaque pointer forwarded to @p fn
 *  @return the number of entries visited */
STL_API int stl_map_foreach(stl_map *m, stl_visit_fn fn, void *user);

/** @brief Invoke a callback on every entry of a read-only map.
 *  @param m the map
 *  @param fn callback
 *  @param user opaque pointer forwarded to @p fn
 *  @return the number of entries visited */
STL_API int stl_map_foreach_c(const stl_map *m, stl_visit_fn fn, void *user);

/** @brief Invoke a callback on every entry whose key lies in a half-open range.
 *  @param m the map
 *  @param lo inclusive lower key bound; NULL means unbounded below
 *  @param hi exclusive upper key bound; NULL means unbounded above
 *  @param fn callback
 *  @param user opaque pointer forwarded to @p fn
 *  @return the number of entries visited */
STL_API int stl_map_foreach_range(stl_map *m, const void *lo, const void *hi,
                                  stl_visit_fn fn, void *user);

/** @brief Return an iterator to the entry with the smallest key.
 *  @param m the map */
STL_API stl_iterator stl_map_begin(stl_map *m);

/** @brief Return the past-the-end iterator.
 *  @param m the map */
STL_API stl_iterator stl_map_end(stl_map *m);

/** @brief Return an iterator to the entry with the largest key.
 *  @param m the map */
STL_API stl_iterator stl_map_rbegin(stl_map *m);

/** @brief Return the before-the-first iterator.
 *  @param m the map */
STL_API stl_iterator stl_map_rend(stl_map *m);

/** @brief Advance an iterator in ascending key order.
 *  @param it the iterator */
STL_API stl_iterator stl_map_iter_next(stl_iterator it);

/** @brief Step an iterator back in descending key order.
 *  @param it the iterator */
STL_API stl_iterator stl_map_iter_prev(stl_iterator it);

/** @} */

/** @name Element projection
 *
 * A map element is the caller's own struct, so these helpers apply the layout
 * the map was created with to reach the key or the value inside it.
 *  @{ */

/** @brief Return a pointer to the key part of an element.
 *  @param m the map
 *  @param pair pointer to a stored element
 *  @return pointer to the key */
STL_API void       *stl_map_key_of(const stl_map *m, void *pair);

/** @brief Return a read-only pointer to the key part of an element.
 *  @param m the map
 *  @param pair pointer to a stored element */
STL_API const void *stl_map_key_of_c(const stl_map *m, const void *pair);

/** @brief Return a pointer to the value part of an element.
 *  @param m the map
 *  @param pair pointer to a stored element
 *  @return pointer to the value */
STL_API void       *stl_map_value_of(const stl_map *m, void *pair);

/** @brief Return a read-only pointer to the value part of an element.
 *  @param m the map
 *  @param pair pointer to a stored element */
STL_API const void *stl_map_value_of_c(const stl_map *m, const void *pair);

/** @} */

/** @} */ /* end of group map */

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ================================================================== */
/* ================================================================== */
/*  hashtable -- generic hash container backing hashset/hashmap       */
/* ================================================================== */

/** @addtogroup hashtable
 *  @{
 */

/** @brief A separate-chaining hash table of fixed-size elements. */
typedef struct stl_hashtable      stl_hashtable;

/** @brief A handle to one table node, stable until it is erased. */
typedef struct stl_hashtable_node stl_hashtable_node;

#ifdef __cplusplus
extern "C" {
#endif

/** @brief How the table treats elements with equal keys. */
typedef enum stl_hashtable_policy {
    STL_HASHTABLE_MULTI  = 0,   /**< Keep every element; multiset/multimap semantics. */
    STL_HASHTABLE_UNIQUE = 1    /**< Keep one per key; set/map semantics. */
} stl_hashtable_policy;

/** @brief Create a hash table.
 *
 * @param elem_size size of one element in bytes
 * @param key_offset byte offset of the key within an element
 * @param key_size byte size of the key; 0 means @p elem_size
 * @param policy whether duplicate keys are allowed
 * @param hash hash function, or NULL for the default
 * @param key_eq equality predicate over keys, or NULL for the default
 * @param elem_dtor optional destructor, or NULL
 * @param a allocator, or NULL for the default
 * @return the new table, or NULL on failure
 *
 * Elements are chained per bucket, and the table also threads an intrusive
 * list through the nodes so that iteration is O(size) rather than
 * O(bucket_count).
 *
 * @note Prefer @ref stl_hashset_new or @ref stl_hashmap_new.
 */
STL_API stl_hashtable *stl_hashtable_new(size_t elem_size, size_t key_offset, size_t key_size,
                                         stl_hashtable_policy policy, stl_hash_fn hash,
                                         stl_equal_fn key_eq, stl_dtor_fn elem_dtor,
                                         const stl_allocator *a);

/** @brief Destroy a hash table.
 *  @param h the table; NULL is ignored */
STL_API void           stl_hashtable_free(stl_hashtable *h);

/** @brief Deep-copy a hash table.
 *  @param h the table to copy
 *  @param copy_elem optional element copy hook
 *  @return the new table, or NULL on failure */
STL_API stl_hashtable *stl_hashtable_copy(const stl_hashtable *h, stl_copy_fn copy_elem);

/** @brief Return the number of elements.
 *  @param h the table */
STL_API size_t stl_hashtable_size(const stl_hashtable *h);

/** @brief Return the number of buckets.
 *  @param h the table */
STL_API size_t stl_hashtable_bucket_count(const stl_hashtable *h);

/** @brief Return the number of elements in one bucket.
 *  @param h the table
 *  @param bucket bucket index */
STL_API size_t stl_hashtable_bucket_size(const stl_hashtable *h, size_t bucket);

/** @brief Test whether the table is empty.
 *  @param h the table */
STL_API int    stl_hashtable_empty(const stl_hashtable *h);

/** @brief Return the average number of elements per bucket.
 *  @param h the table */
STL_API double stl_hashtable_load_factor(const stl_hashtable *h);

/** @brief Reserve room for a number of elements.
 *  @param h the table
 *  @param count the expected element count
 *  @return @ref STL_OK, or @ref STL_ERR_NOMEM
 *
 * Grows the bucket array so that @p count elements fit without exceeding the
 * maximum load factor.  Reserving up front avoids repeated rehashing while
 * filling a table. */
STL_API int    stl_hashtable_reserve(stl_hashtable *h, size_t count);

/** @brief Rebuild the table with a given number of buckets.
 *  @param h the table
 *  @param buckets requested bucket count; rounded up to a power of two
 *  @return @ref STL_OK, or @ref STL_ERR_NOMEM */
STL_API int    stl_hashtable_rehash(stl_hashtable *h, size_t buckets);

/** @brief Set the load factor at which the table grows.
 *  @param h the table
 *  @param lf the new maximum, clamped to the range [0.25, 16]
 *
 * Lower values trade memory for shorter chains.  If the current load already
 * exceeds the new limit, the table is rehashed immediately. */
STL_API void   stl_hashtable_set_max_load_factor(stl_hashtable *h, double lf);

/** @brief Insert a copy of an element.
 *  @param h the table
 *  @param elem pointer to the element to copy in
 *  @return the node holding the element; for a unique table, the existing node
 *          when the key is already present */
STL_API stl_hashtable_node *stl_hashtable_insert(stl_hashtable *h, const void *elem);

/** @brief Erase one element by key.
 *  @param h the table
 *  @param key pointer to the key to erase
 *  @return @ref STL_OK, or @ref STL_ERR_NOT_FOUND */
STL_API int    stl_hashtable_erase(stl_hashtable *h, const void *key);

/** @brief Erase one node.
 *  @param h the table
 *  @param node the node to erase
 *  @return @ref STL_OK, or @ref STL_ERR_INVALID */
STL_API int    stl_hashtable_erase_node(stl_hashtable *h, stl_hashtable_node *node);

/** @brief Remove every element, keeping the bucket array.
 *  @param h the table */
STL_API void   stl_hashtable_clear(stl_hashtable *h);

/** @brief Remove every element and destroy each one.
 *  @param h the table */
STL_API void   stl_hashtable_clear_ex(stl_hashtable *h);

/** @brief Find an element by key.
 *  @param h the table
 *  @param key pointer to the key
 *  @return the node, or NULL */
STL_API stl_hashtable_node *stl_hashtable_find(stl_hashtable *h, const void *key);

/** @brief Find an element by key in a read-only table.
 *  @param h the table
 *  @param key pointer to the key
 *  @return the node, or NULL */
STL_API const stl_hashtable_node *stl_hashtable_find_c(const stl_hashtable *h, const void *key);

/** @brief Count the elements matching a key.
 *  @param h the table
 *  @param key pointer to the key
 *  @return 0 or 1 for a unique table; the run length for a multi-table */
STL_API size_t stl_hashtable_count(const stl_hashtable *h, const void *key);

/** @brief Test whether a key is present.
 *  @param h the table
 *  @param key pointer to the key
 *  @return non-zero when present */
STL_API int    stl_hashtable_contains(const stl_hashtable *h, const void *key);

/** @brief Return the element stored in a node.
 *  @param node the node
 *  @return pointer to the element, or NULL for a NULL node */
STL_API void       *stl_hashtable_node_data(stl_hashtable_node *node);

/** @brief Return the first node in iteration order.
 *  @param h the table
 *  @return the node, or NULL when empty
 *
 * Iteration order is the insertion order, not hash order, and is not sorted. */
STL_API stl_hashtable_node *stl_hashtable_first(stl_hashtable *h);

/** @brief Return the next node in iteration order.
 *  @param h the table
 *  @param node the current node
 *  @return the successor, or NULL at the last element */
STL_API stl_hashtable_node *stl_hashtable_next(stl_hashtable *h, stl_hashtable_node *node);

/** @brief Return the first node of one bucket.
 *  @param h the table
 *  @param bucket bucket index
 *  @return the node, or NULL when the bucket is empty */
STL_API stl_hashtable_node *stl_hashtable_bucket_head(stl_hashtable *h, size_t bucket);

/** @brief Return the next node within the same bucket.
 *  @param node the current node */
STL_API stl_hashtable_node *stl_hashtable_bucket_next(stl_hashtable_node *node);

/** @brief Find the first element matching a predicate.
 *  @param h the table
 *  @param pred predicate; a non-zero result selects the element
 *  @param user opaque pointer forwarded to @p pred
 *  @return pointer to the element, or NULL */
STL_API void *stl_hashtable_find_if(const stl_hashtable *h,
                                    int (STL_CALL *pred)(const void *, void *), void *user);

/** @brief Invoke a callback on every element.
 *  @param h the table
 *  @param fn callback; a non-zero result stops the walk
 *  @param user opaque pointer forwarded to @p fn */
STL_API void  stl_hashtable_foreach(stl_hashtable *h, stl_visit_fn fn, void *user);

/** @brief Invoke a callback on every element of a read-only table.
 *  @param h the table
 *  @param fn callback
 *  @param user opaque pointer forwarded to @p fn */
STL_API void  stl_hashtable_foreach_c(const stl_hashtable *h, stl_visit_fn fn, void *user);

/** @brief Return an iterator to the first element in iteration order.
 *  @param h the table */
STL_API stl_iterator stl_hashtable_begin(stl_hashtable *h);

/** @brief Return the past-the-end iterator.
 *  @param h the table */
STL_API stl_iterator stl_hashtable_end(stl_hashtable *h);
STL_API stl_iterator stl_hashtable_iter_next(stl_iterator it);
STL_API stl_iterator stl_hashtable_iter_prev(stl_iterator it);

#ifdef __cplusplus
} /* extern "C" */
#endif

/** @} */ /* end of group hashtable */

/* ================================================================== */
/*  hashset / hashmap                                                  */
/* ================================================================== */

/** @addtogroup hashset
 *  @{
 */

/** @brief An unordered set of unique elements. */
typedef stl_hashtable stl_hashset;

/** @brief A handle to one element of a hash set. */
typedef stl_hashtable_node stl_hashset_node;

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Create an unordered set.
 *
 * @param elem_size size of one element in bytes
 * @param hash hash function, or NULL for the default
 * @param eq equality predicate, or NULL for the default
 * @param elem_dtor optional destructor, or NULL
 * @return the new set, or NULL on failure
 *
 * @code
 * stl_hashset *s = stl_hashset_new(sizeof(int), stl_hash_int, stl_eq_int, NULL);
 * @endcode
 */
STL_API stl_hashset *stl_hashset_new(size_t elem_size, stl_hash_fn hash, stl_equal_fn eq,
                                     stl_dtor_fn elem_dtor);

/** @brief Create an unordered set that allocates through a specific allocator.
 *  @param elem_size size of one element in bytes
 *  @param hash hash function, or NULL for the default
 *  @param eq equality predicate, or NULL for the default
 *  @param elem_dtor optional destructor, or NULL
 *  @param a allocator, or NULL for the default
 *  @return the new set, or NULL on failure */
STL_API stl_hashset *stl_hashset_new_a(size_t elem_size, stl_hash_fn hash, stl_equal_fn eq,
                                       stl_dtor_fn elem_dtor, const stl_allocator *a);

/** @brief Create an unordered set or multiset.
 *  @param elem_size size of one element in bytes
 *  @param hash hash function, or NULL for the default
 *  @param eq equality predicate, or NULL for the default
 *  @param policy whether duplicate elements are kept
 *  @param elem_dtor optional destructor, or NULL
 *  @param a allocator, or NULL for the default
 *  @return the new set, or NULL on failure */
STL_API stl_hashset *stl_hashset_new_policy(size_t elem_size, stl_hash_fn hash, stl_equal_fn eq,
                                            stl_hashtable_policy policy, stl_dtor_fn elem_dtor,
                                            const stl_allocator *a);

/** @brief Destroy an unordered set.
 *  @param s the set; NULL is ignored */
STL_API void        stl_hashset_free(stl_hashset *s);

/** @brief Deep-copy an unordered set.
 *  @param s the set to copy
 *  @param copy_elem optional element copy hook
 *  @return the new set, or NULL on failure */
STL_API stl_hashset *stl_hashset_copy(const stl_hashset *s, stl_copy_fn copy_elem);

/** @brief Return the number of elements.
 *  @param s the set */
STL_API size_t stl_hashset_size(const stl_hashset *s);

/** @brief Test whether the set is empty.
 *  @param s the set */
STL_API int    stl_hashset_empty(const stl_hashset *s);

/** @brief Insert an element.
 *  @param s the set
 *  @param elem pointer to the element to copy in
 *  @return pointer to the stored element, or NULL when it was already present */
STL_API const void *stl_hashset_insert(stl_hashset *s, const void *elem);

/** @brief Erase an element.
 *  @param s the set
 *  @param elem pointer to a key-equivalent element
 *  @return @ref STL_OK, or @ref STL_ERR_NOT_FOUND */
STL_API int    stl_hashset_erase(stl_hashset *s, const void *elem);

/** @brief Remove every element.
 *  @param s the set */
STL_API void   stl_hashset_clear(stl_hashset *s);

/** @brief Remove every element and destroy each one.
 *  @param s the set */
STL_API void   stl_hashset_clear_ex(stl_hashset *s);

/** @brief Look up an element.
 *  @param s the set
 *  @param elem pointer to a key-equivalent element
 *  @return pointer to the stored element, or NULL */
STL_API const void *stl_hashset_find(const stl_hashset *s, const void *elem);

/** @brief Count the elements equivalent to a key.
 *  @param s the set
 *  @param elem pointer to the key */
STL_API size_t stl_hashset_count(const stl_hashset *s, const void *elem);

/** @brief Test whether an element is present.
 *  @param s the set
 *  @param elem pointer to the key */
STL_API int    stl_hashset_contains(const stl_hashset *s, const void *elem);

/** @brief Reserve room for a number of elements.
 *  @param s the set
 *  @param count the expected element count
 *  @return @ref STL_OK, or @ref STL_ERR_NOMEM
 *
 * Saves the repeated rehashing that growing a table one element at a time
 * would cause. */
STL_API int    stl_hashset_reserve(stl_hashset *s, size_t count);

/** @brief Invoke a callback on every element, in unspecified order.
 *  @param s the set
 *  @param fn callback; a non-zero result stops the walk
 *  @param user opaque pointer forwarded to @p fn */
STL_API void   stl_hashset_foreach(stl_hashset *s, stl_visit_fn fn, void *user);

/** @brief Invoke a callback on every element of a read-only set.
 *  @param s the set
 *  @param fn callback
 *  @param user opaque pointer forwarded to @p fn */
STL_API void   stl_hashset_foreach_c(const stl_hashset *s, stl_visit_fn fn, void *user);

/** @} */ /* end of group hashset */

/** @addtogroup hashmap
 *  @{
 */

/** @brief An unordered map from keys to values.
 *
 * Owns a hash table and remembers the element layout, so the value offset
 * travels with the container.  There is no limit on how many maps may exist at
 * once.
 */
typedef struct stl_hashmap stl_hashmap;

/** @brief A handle to one key/value entry of a hash map. */
typedef stl_hashtable_node stl_hashmap_node;

/*
 * stl_hashmap shares the `extern "C"` block opened above for stl_hashset, so
 * there is no second opening here; both sections close together at the end of
 * the hashmap group.
 */

/** @brief Create an unordered map.
 *
 * @param elem_size size of one key/value element in bytes
 * @param value_offset byte offset of the value within that element
 * @param key_size byte size of the key, which starts at offset 0
 * @param hash hash function over keys, or NULL for the default
 * @param key_eq equality predicate over keys, or NULL for the default
 * @param elem_dtor optional destructor, or NULL
 * @return the new map, or NULL on failure
 *
 * The element layout is the same as for @ref stl_map_new, so the same pair
 * struct works with either container.
 */
STL_API stl_hashmap *stl_hashmap_new(size_t elem_size, size_t value_offset, size_t key_size,
                                     stl_hash_fn hash, stl_equal_fn key_eq, stl_dtor_fn elem_dtor);

/** @brief Create an unordered map that allocates through a specific allocator.
 *  @param elem_size size of one key/value element in bytes
 *  @param value_offset byte offset of the value within that element
 *  @param key_size byte size of the key
 *  @param hash hash function over keys, or NULL for the default
 *  @param key_eq equality predicate over keys, or NULL for the default
 *  @param elem_dtor optional destructor, or NULL
 *  @param a allocator, or NULL for the default
 *  @return the new map, or NULL on failure */
STL_API stl_hashmap *stl_hashmap_new_a(size_t elem_size, size_t value_offset, size_t key_size,
                                       stl_hash_fn hash, stl_equal_fn key_eq, stl_dtor_fn elem_dtor,
                                       const stl_allocator *a);

/** @brief Create an unordered map or multimap.
 *  @param elem_size size of one key/value element in bytes
 *  @param value_offset byte offset of the value within that element
 *  @param key_size byte size of the key
 *  @param hash hash function over keys, or NULL for the default
 *  @param key_eq equality predicate over keys, or NULL for the default
 *  @param policy whether duplicate keys are kept
 *  @param elem_dtor optional destructor, or NULL
 *  @param a allocator, or NULL for the default
 *  @return the new map, or NULL on failure */
STL_API stl_hashmap *stl_hashmap_new_policy(size_t elem_size, size_t value_offset, size_t key_size,
                                            stl_hash_fn hash, stl_equal_fn key_eq,
                                            stl_hashtable_policy policy, stl_dtor_fn elem_dtor,
                                            const stl_allocator *a);

/** @brief Destroy an unordered map.
 *  @param m the map; NULL is ignored */
STL_API void        stl_hashmap_free(stl_hashmap *m);

/** @brief Deep-copy an unordered map.
 *  @param m the map to copy
 *  @param copy_elem optional element copy hook
 *  @return the new map, or NULL on failure */
STL_API stl_hashmap *stl_hashmap_copy(const stl_hashmap *m, stl_copy_fn copy_elem);

/** @brief Return the number of entries.
 *  @param m the map */
STL_API size_t stl_hashmap_size(const stl_hashmap *m);

/** @brief Test whether the map is empty.
 *  @param m the map */
STL_API int    stl_hashmap_empty(const stl_hashmap *m);

/** @brief Insert a complete entry.
 *  @param m the map
 *  @param pair pointer to the key/value element to copy in
 *  @return pointer to the stored element, or NULL when the key was present */
STL_API void  *stl_hashmap_insert(stl_hashmap *m, const void *pair);

/** @brief Insert or assign.
 *  @param m the map
 *  @param key pointer to the key
 *  @param value pointer to the value to store, or NULL to zero-fill it
 *  @return pointer to the stored element, or NULL on failure */
STL_API void  *stl_hashmap_put(stl_hashmap *m, const void *key, const void *value);
/** @brief Erase the entry for a key.
 *  @param m the map
 *  @param key pointer to the key
 *  @return @ref STL_OK, or @ref STL_ERR_NOT_FOUND */
STL_API int    stl_hashmap_erase(stl_hashmap *m, const void *key);

/** @brief Remove every entry.
 *  @param m the map */
STL_API void   stl_hashmap_clear(stl_hashmap *m);

/** @brief Remove every entry and destroy each one.
 *  @param m the map */
STL_API void   stl_hashmap_clear_ex(stl_hashmap *m);

/** @brief Find the entry for a key.
 *  @param m the map
 *  @param key pointer to the key
 *  @return pointer to the stored element, or NULL */
STL_API void  *stl_hashmap_find(stl_hashmap *m, const void *key);

/** @brief Find the entry for a key in a read-only map.
 *  @param m the map
 *  @param key pointer to the key
 *  @return pointer to the stored element, or NULL */
STL_API const void *stl_hashmap_find_c(const stl_hashmap *m, const void *key);

/** @brief Return the value for a key.
 *  @param m the map
 *  @param key pointer to the key
 *  @return pointer to the value, or NULL when the key is absent */
STL_API void  *stl_hashmap_get(stl_hashmap *m, const void *key);

/** @brief Return the value for a key in a read-only map.
 *  @param m the map
 *  @param key pointer to the key
 *  @return pointer to the value, or NULL */
STL_API const void *stl_hashmap_get_c(const stl_hashmap *m, const void *key);

/** @brief Return the value slot for a key, inserting a default when needed.
 *  @param m the map
 *  @param key pointer to the key
 *  @param default_value pointer to the value to install, or NULL to zero-fill
 *  @return pointer to the value slot, or NULL on failure */
STL_API void  *stl_hashmap_get_or_insert(stl_hashmap *m, const void *key, const void *default_value);

/** @brief Count the entries matching a key.
 *  @param m the map
 *  @param key pointer to the key */
STL_API size_t stl_hashmap_count(const stl_hashmap *m, const void *key);

/** @brief Test whether a key is present.
 *  @param m the map
 *  @param key pointer to the key */
STL_API int    stl_hashmap_contains(const stl_hashmap *m, const void *key);

/** @brief Reserve room for a number of entries.
 *  @param m the map
 *  @param count the expected entry count
 *  @return @ref STL_OK, or @ref STL_ERR_NOMEM */
STL_API int    stl_hashmap_reserve(stl_hashmap *m, size_t count);

/** @brief Return a pointer to the key part of an entry.
 *  @param m the map
 *  @param pair pointer to a stored element
 *  @return pointer to the key, which is always at offset 0 */
STL_API void  *stl_hashmap_key_of(const stl_hashmap *m, void *pair);

/** @brief Return a pointer to the value part of an entry.
 *  @param m the map
 *  @param pair pointer to a stored element
 *  @return pointer to the value */
STL_API void  *stl_hashmap_value_of(const stl_hashmap *m, void *pair);

/** @brief Invoke a callback on every entry, in unspecified order.
 *  @param m the map
 *  @param fn callback; a non-zero result stops the walk
 *  @param user opaque pointer forwarded to @p fn
 *  @return the number of entries visited */
STL_API int    stl_hashmap_foreach(stl_hashmap *m, stl_visit_fn fn, void *user);

/** @brief Invoke a callback on every entry of a read-only map.
 *  @param m the map
 *  @param fn callback
 *  @param user opaque pointer forwarded to @p fn
 *  @return the number of entries visited */
STL_API int    stl_hashmap_foreach_c(const stl_hashmap *m, stl_visit_fn fn, void *user);

/** @brief Return an iterator to the first entry in iteration order.
 *  @param m the map */
STL_API stl_iterator stl_hashmap_begin(stl_hashmap *m);

/** @brief Return the past-the-end iterator.
 *  @param m the map */
STL_API stl_iterator stl_hashmap_end(stl_hashmap *m);

/** @brief Advance an iterator.
 *  @param it the iterator */
STL_API stl_iterator stl_hashmap_iter_next(stl_iterator it);

/** @} */ /* end of group hashmap */

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ================================================================== */
/*  container adaptors: stack / queue / priority_queue                */
/* ================================================================== */

/** @addtogroup adaptors
 *
 * Adaptors restrict a container to one access pattern.  Each is a thin
 * wrapper over one of the sequence containers, so the underlying storage and
 * complexity guarantees carry over.
 *  @{
 */

/** @brief A last-in, first-out container. */
typedef struct stl_stack stl_stack;

/** @brief A first-in, first-out container. */
typedef struct stl_queue stl_queue;

/** @brief A container that always yields its largest element. */
typedef struct stl_priority_queue stl_priority_queue;

#ifdef __cplusplus
extern "C" {
#endif

/** @name stack
 *
 * Backed by a @ref stl_vector, so the top element is always at the end of a
 * contiguous block.
 *  @{ */

/** @brief Create a stack.
 *  @param elem_size size of one element in bytes
 *  @param elem_dtor optional destructor, or NULL
 *  @return the new stack, or NULL on failure */
STL_API stl_stack *stl_stack_new(size_t elem_size, stl_dtor_fn elem_dtor);

/** @brief Create a stack that allocates through a specific allocator.
 *  @param elem_size size of one element in bytes
 *  @param elem_dtor optional destructor, or NULL
 *  @param a allocator, or NULL for the default
 *  @return the new stack, or NULL on failure */
STL_API stl_stack *stl_stack_new_a(size_t elem_size, stl_dtor_fn elem_dtor, const stl_allocator *a);

/** @brief Destroy a stack.
 *  @param s the stack; NULL is ignored */
STL_API void  stl_stack_free(stl_stack *s);

/** @brief Push an element onto the stack.
 *  @param s the stack
 *  @param elem pointer to the element to copy in
 *  @return @ref STL_OK, or an error code */
STL_API int   stl_stack_push(stl_stack *s, const void *elem);

/** @brief Remove the top element.
 *  @param s the stack */
STL_API void  stl_stack_pop(stl_stack *s);

/** @brief Return the top element.
 *  @param s the stack
 *  @return pointer to the element, or NULL when empty */
STL_API void *stl_stack_top(stl_stack *s);

/** @brief Return the top element of a read-only stack.
 *  @param s the stack */
STL_API const void *stl_stack_top_c(const stl_stack *s);

/** @brief Return the number of elements.
 *  @param s the stack */
STL_API size_t stl_stack_size(const stl_stack *s);

/** @brief Test whether the stack is empty.
 *  @param s the stack */
STL_API int   stl_stack_empty(const stl_stack *s);

/** @brief Remove every element.
 *  @param s the stack */
STL_API void  stl_stack_clear(stl_stack *s);

/** @brief Remove every element and destroy each one.
 *  @param s the stack */
STL_API void  stl_stack_clear_ex(stl_stack *s);

/** @brief Exchange the contents of two stacks.
 *  @param a first stack
 *  @param b second stack */
STL_API void  stl_stack_swap(stl_stack *a, stl_stack *b);

/** @} */

/** @name queue
 *
 * Backed by a @ref stl_deque, so both ends are cheap.
 *  @{ */

/** @brief Create a queue.
 *  @param elem_size size of one element in bytes
 *  @param elem_dtor optional destructor, or NULL
 *  @return the new queue, or NULL on failure */
STL_API stl_queue *stl_queue_new(size_t elem_size, stl_dtor_fn elem_dtor);

/** @brief Create a queue that allocates through a specific allocator.
 *  @param elem_size size of one element in bytes
 *  @param elem_dtor optional destructor, or NULL
 *  @param a allocator, or NULL for the default
 *  @return the new queue, or NULL on failure */
STL_API stl_queue *stl_queue_new_a(size_t elem_size, stl_dtor_fn elem_dtor, const stl_allocator *a);

/** @brief Destroy a queue.
 *  @param q the queue; NULL is ignored */
STL_API void  stl_queue_free(stl_queue *q);

/** @brief Append an element at the back.
 *  @param q the queue
 *  @param elem pointer to the element to copy in
 *  @return @ref STL_OK, or an error code */
STL_API int   stl_queue_push(stl_queue *q, const void *elem);

/** @brief Remove the element at the front.
 *  @param q the queue */
STL_API void  stl_queue_pop(stl_queue *q);

/** @brief Return the element at the front.
 *  @param q the queue
 *  @return pointer to the element, or NULL when empty */
STL_API void *stl_queue_front(stl_queue *q);

/** @brief Return the element at the back.
 *  @param q the queue
 *  @return pointer to the element, or NULL when empty */
STL_API void *stl_queue_back(stl_queue *q);

/** @brief Return the front element of a read-only queue.
 *  @param q the queue */
STL_API const void *stl_queue_front_c(const stl_queue *q);

/** @brief Return the back element of a read-only queue.
 *  @param q the queue */
STL_API const void *stl_queue_back_c(const stl_queue *q);

/** @brief Return the number of elements.
 *  @param q the queue */
STL_API size_t stl_queue_size(const stl_queue *q);

/** @brief Test whether the queue is empty.
 *  @param q the queue */
STL_API int   stl_queue_empty(const stl_queue *q);

/** @brief Remove every element.
 *  @param q the queue */
STL_API void  stl_queue_clear(stl_queue *q);

/** @brief Remove every element and destroy each one.
 *  @param q the queue */
STL_API void  stl_queue_clear_ex(stl_queue *q);

/** @brief Exchange the contents of two queues.
 *  @param a first queue
 *  @param b second queue */
STL_API void  stl_queue_swap(stl_queue *a, stl_queue *b);

/** @} */

/** @name priority_queue
 *
 * Backed by a @ref stl_vector arranged as a binary heap, so the largest
 * element is always at index 0.
 *  @{ */

/** @brief Create a priority queue.
 *
 * @param elem_size size of one element in bytes
 * @param cmp ordering predicate, or NULL for @ref stl_default_priority_cmp
 * @param elem_dtor optional destructor, or NULL
 * @return the new queue, or NULL on failure
 *
 * The queue yields the element that compares @e greatest, making it a max-heap.
 * Pass the reverse of the usual comparator, such as @ref stl_cmp_double_desc,
 * to get a min-heap.
 */
STL_API stl_priority_queue *stl_priority_queue_new(size_t elem_size, stl_compare_fn cmp,
                                                   stl_dtor_fn elem_dtor);

/** @brief Create a priority queue that allocates through a specific allocator.
 *  @param elem_size size of one element in bytes
 *  @param cmp ordering predicate, or NULL for the default
 *  @param elem_dtor optional destructor, or NULL
 *  @param a allocator, or NULL for the default
 *  @return the new queue, or NULL on failure */
STL_API stl_priority_queue *stl_priority_queue_new_a(size_t elem_size, stl_compare_fn cmp,
                                                     stl_dtor_fn elem_dtor,
                                                     const stl_allocator *a);

/** @brief Destroy a priority queue.
 *  @param pq the queue; NULL is ignored */
STL_API void  stl_priority_queue_free(stl_priority_queue *pq);

/** @brief Insert an element, restoring the heap property.
 *  @param pq the queue
 *  @param elem pointer to the element to copy in
 *  @return @ref STL_OK, or an error code
 *  @note O(log n). */
STL_API int   stl_priority_queue_push(stl_priority_queue *pq, const void *elem);

/** @brief Remove the greatest element.
 *  @param pq the queue
 *  @note O(log n). */
STL_API void  stl_priority_queue_pop(stl_priority_queue *pq);

/** @brief Return the greatest element.
 *  @param pq the queue
 *  @return pointer to the element, or NULL when empty
 *  @note O(1). */
STL_API void *stl_priority_queue_top(stl_priority_queue *pq);

/** @brief Return the greatest element of a read-only queue.
 *  @param pq the queue */
STL_API const void *stl_priority_queue_top_c(const stl_priority_queue *pq);

/** @brief Return the number of elements.
 *  @param pq the queue */
STL_API size_t stl_priority_queue_size(const stl_priority_queue *pq);

/** @brief Test whether the queue is empty.
 *  @param pq the queue */
STL_API int   stl_priority_queue_empty(const stl_priority_queue *pq);

/** @brief Remove every element.
 *  @param pq the queue */
STL_API void  stl_priority_queue_clear(stl_priority_queue *pq);

/** @brief Remove every element and destroy each one.
 *  @param pq the queue */
STL_API void  stl_priority_queue_clear_ex(stl_priority_queue *pq);

/** @brief Reserve room for a number of elements.
 *  @param pq the queue
 *  @param n the expected element count
 *  @return @ref STL_OK, or an error code */
STL_API int   stl_priority_queue_reserve(stl_priority_queue *pq, size_t n);

/** @} */

/** @name Heap operations on raw arrays
 *
 * The primitives behind @ref stl_priority_queue, exposed for callers that want
 * to keep a heap inside their own array.  All of them use the same convention:
 * @c cmp(a, @c b) @c < @c 0 means @p a has the @e higher priority, so the
 * element at index 0 is the greatest by @p cmp.
 *  @{ */

/** @brief Rearrange an array in place into a binary heap.
 *  @param base start of the array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param cmp ordering predicate
 *  @note O(n), not O(n log n). */
STL_API void stl_heap_make(void *base, size_t count, size_t elem_size, stl_compare_fn cmp);

/** @brief Sift a newly appended element up into place.
 *  @param base start of the array, whose first @c count-1 elements already form
 *              a heap
 *  @param count the element count @e including the new last element
 *  @param elem_size size of one element in bytes
 *  @param cmp ordering predicate */
STL_API void stl_heap_push(void *base, size_t count, size_t elem_size, stl_compare_fn cmp);

/** @brief Remove the root, leaving the remaining elements a heap.
 *  @param base start of the heap array
 *  @param count the element count @e including the element to remove
 *  @param elem_size size of one element in bytes
 *  @param cmp ordering predicate
 *
 * The removed element is moved to the end of the array, so the caller decides
 * what to do with it. */
STL_API void stl_heap_pop(void *base, size_t count, size_t elem_size, stl_compare_fn cmp);

/** @brief Sort an array by way of a heap.
 *  @param base start of the array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param cmp ordering predicate
 *  @note O(n log n) worst case, and not stable. */
STL_API void stl_heap_sort(void *base, size_t count, size_t elem_size, stl_compare_fn cmp);

/** @brief Test whether an array satisfies the heap property.
 *  @param base start of the array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param cmp ordering predicate
 *  @return non-zero when the array is a valid heap */
STL_API int  stl_heap_is_heap(const void *base, size_t count, size_t elem_size, stl_compare_fn cmp);

/** @brief Default priority ordering, yielding a max-heap.
 *
 * @param a pointer to the left-hand element
 * @param b pointer to the right-hand element
 * @return the ordering of the two elements
 *
 * @warning This compares elements by machine word, so it is only correct for
 *          integer elements of pointer width.  Supply a typed comparator such
 *          as @ref stl_cmp_double for anything else.
 */
STL_API int  STL_CALL stl_default_priority_cmp(const void *a, const void *b);

/** @} */

/** @} */ /* end of group adaptors */

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ================================================================== */
/* ================================================================== */
/*  string                                                             */
/* ================================================================== */

/** @addtogroup string
 *  @{
 */

/** @brief An owned, growable, NUL-terminated byte string.
 *
 * The counterpart of @c std::string.  Because the buffer is always
 * NUL-terminated it can be handed directly to @c printf, @c strcmp and any
 * other C API, while the length is tracked separately so that embedded NUL
 * bytes survive the length-aware functions.
 *
 * @par Complexity
 * @c append is amortised O(1); @c insert, @c erase and @c replace are O(n) in
 * the length of the tail; @c find is O(n*m) in the worst case.
 *
 * @par Example
 * @code
 * stl_string *s = stl_string_new_from("Hello");
 * stl_string_append(s, ", World");
 * stl_string_append_fmt(s, " (%d chars)", (int)stl_string_size(s));
 * printf("%s\n", stl_string_cstr(s));   // Hello, World (12 chars)
 * stl_string_free(s);
 * @endcode
 */
typedef struct stl_string stl_string;

#ifdef __cplusplus
extern "C" {
#endif

/** @name Construction and destruction
 *  @{ */

/** @brief Create an empty string.
 *  @return the new string, or NULL on failure */
STL_API stl_string *stl_string_new(void);

/** @brief Create an empty string that allocates through a specific allocator.
 *  @param a allocator, or NULL for the default
 *  @return the new string, or NULL on failure */
STL_API stl_string *stl_string_new_a(const stl_allocator *a);

/** @brief Create a string by copying a C string.
 *  @param cstr the source text; NULL yields an empty string
 *  @return the new string, or NULL on failure */
STL_API stl_string *stl_string_new_from(const char *cstr);

/** @brief Create a string from a byte range.
 *  @param data the source bytes
 *  @param len how many bytes to copy; embedded NULs are preserved
 *  @return the new string, or NULL on failure */
STL_API stl_string *stl_string_new_from_n(const char *data, size_t len);

/** @brief Create a string by formatting.
 *  @param fmt printf-style format
 *  @param ... format arguments
 *  @return the new string, or NULL on failure */
STL_API stl_string *stl_string_new_fmt(const char *fmt, ...);

/** @brief Create a string by formatting a @c va_list.
 *  @param fmt printf-style format
 *  @param ap the argument list
 *  @return the new string, or NULL on failure */
STL_API stl_string *stl_string_new_vfmt(const char *fmt, va_list ap);

/** @brief Create an empty string with room for @p cap bytes.
 *  @param cap initial byte capacity
 *  @return the new string, or NULL on failure */
STL_API stl_string *stl_string_new_cap(size_t cap);

/** @brief Destroy a string.
 *  @param s the string; NULL is ignored */
STL_API void        stl_string_free(stl_string *s);

/** @brief Duplicate a string.
 *  @param s the string to copy
 *  @return the new string, or NULL on failure */
STL_API stl_string *stl_string_copy(const stl_string *s);

/** @} */

/** @name Size and representation
 *  @{ */

/** @brief Return the number of bytes, excluding the terminator.
 *  @param s the string */
STL_API size_t stl_string_size(const stl_string *s);

/** @brief Return the allocated capacity in bytes, including the terminator.
 *  @param s the string */
STL_API size_t stl_string_capacity(const stl_string *s);

/** @brief Test whether the string is empty.
 *  @param s the string */
STL_API int    stl_string_empty(const stl_string *s);

/** @brief Return the string as a NUL-terminated C string.
 *  @param s the string
 *  @return a pointer to the internal buffer; never NULL for a valid string
 *  @warning The pointer is invalidated by any operation that grows the string. */
STL_API char  *stl_string_cstr(stl_string *s);

/** @brief Return a read-only C string view.
 *  @param s the string */
STL_API const char *stl_string_cstr_c(const stl_string *s);

/** @brief Return a writable pointer to the string's bytes.
 *  @param s the string */
STL_API char  *stl_string_data(stl_string *s);

/** @brief Return a read-only pointer to the string's bytes.
 *  @param s the string */
STL_API const char *stl_string_data_c(const stl_string *s);

/** @brief Return the allocator the string was created with.
 *  @param s the string */
STL_API const stl_allocator *stl_string_allocator(const stl_string *s);

/** @} */

/** @name Character access
 *
 * Write access through these functions does not change the length, so any
 * previously obtained @c cstr pointer stays valid.
 *  @{ */

/** @brief Return the character at an index.
 *  @param s the string
 *  @param i zero-based index
 *  @return the character, or @c '\\0' when out of range */
STL_API char  stl_string_at(const stl_string *s, size_t i);

/** @brief Return a writable pointer to the character at an index.
 *  @param s the string
 *  @param i zero-based index
 *  @return pointer to the character, or NULL when out of range */
STL_API char *stl_string_ref(stl_string *s, size_t i);

/** @brief Return the first character.
 *  @param s the string */
STL_API char  stl_string_front(const stl_string *s);

/** @brief Return the last character.
 *  @param s the string */
STL_API char  stl_string_back(const stl_string *s);

/** @} */

/** @name Capacity
 *  @{ */

/** @brief Ensure room for at least @p cap bytes.
 *  @param s the string
 *  @param cap the required byte capacity
 *  @return @ref STL_OK, or @ref STL_ERR_NOMEM */
STL_API int stl_string_reserve(stl_string *s, size_t cap);

/** @brief Change the length, zero-filling or truncating as needed.
 *  @param s the string
 *  @param len the new length in bytes
 *  @return @ref STL_OK, or an error code */
STL_API int stl_string_resize(stl_string *s, size_t len);

/** @brief Release unused capacity.
 *  @param s the string
 *  @return @ref STL_OK, or an error code */
STL_API int stl_string_shrink_to_fit(stl_string *s);

/** @brief Empty the string, keeping the allocated buffer.
 *  @param s the string */
STL_API void stl_string_clear(stl_string *s);

/** @brief Empty the string, wiping the bytes it held.
 *  @param s the string
 *
 * Like @ref stl_string_clear but zeroes the buffer first, which is the version
 * to use when the string held something sensitive. */
STL_API void stl_string_clear_ex(stl_string *s);

/** @} */

/** @name Assignment and appending
 *  @{ */

/** @brief Replace the contents with a copy of a C string.
 *  @param s the string
 *  @param cstr the new contents; NULL clears the string
 *  @return @ref STL_OK, or an error code */
STL_API int stl_string_assign(stl_string *s, const char *cstr);

/** @brief Replace the contents with a byte range.
 *  @param s the string
 *  @param data the source bytes
 *  @param len how many bytes to copy
 *  @return @ref STL_OK, or an error code */
STL_API int stl_string_assign_n(stl_string *s, const char *data, size_t len);

/** @brief Replace the contents with a copy of another string.
 *  @param s the string
 *  @param other the source; NULL clears the string
 *  @return @ref STL_OK, or an error code */
STL_API int stl_string_assign_string(stl_string *s, const stl_string *other);

/** @brief Append a C string.
 *  @param s the string
 *  @param cstr the text to append; NULL is a no-op
 *  @return @ref STL_OK, or an error code */
STL_API int stl_string_append(stl_string *s, const char *cstr);

/** @brief Append a byte range.
 *  @param s the string
 *  @param data the bytes to append
 *  @param len how many bytes
 *  @return @ref STL_OK, or an error code
 *
 * Embedded NUL bytes survive.  Self-append is handled correctly. */
STL_API int stl_string_append_n(stl_string *s, const char *data, size_t len);

/** @brief Append another string.
 *  @param s the string
 *  @param other the source; NULL is a no-op
 *  @return @ref STL_OK, or an error code
 *  @note Appending a string to itself works. */
STL_API int stl_string_append_string(stl_string *s, const stl_string *other);

/** @brief Append one character.
 *  @param s the string
 *  @param c the character
 *  @return @ref STL_OK, or an error code */
STL_API int stl_string_append_char(stl_string *s, char c);

/** @brief Remove the last character.
 *  @param s the string
 *  @note Reports @ref STL_ERR_EMPTY when the string is already empty. */
STL_API void stl_string_pop_back(stl_string *s);

/** @brief Append formatted text.
 *  @param s the string
 *  @param fmt printf-style format
 *  @param ... format arguments
 *  @return @ref STL_OK, or an error code */
STL_API int stl_string_append_fmt(stl_string *s, const char *fmt, ...);

/** @brief Append formatted text from a @c va_list.
 *  @param s the string
 *  @param fmt printf-style format
 *  @param ap the argument list
 *  @return @ref STL_OK, or an error code */
STL_API int stl_string_append_vfmt(stl_string *s, const char *fmt, va_list ap);

/** @brief Insert text at the front.
 *  @param s the string
 *  @param cstr the text to insert
 *  @return @ref STL_OK, or an error code */
STL_API int stl_string_prepend(stl_string *s, const char *cstr);

/** @brief Insert a C string at a position.
 *  @param s the string
 *  @param pos byte offset at which to insert
 *  @param cstr the text to insert
 *  @return @ref STL_OK, or @ref STL_ERR_RANGE */
STL_API int stl_string_insert(stl_string *s, size_t pos, const char *cstr);

/** @brief Insert a byte range at a position.
 *  @param s the string
 *  @param pos byte offset at which to insert
 *  @param data the bytes to insert
 *  @param len how many bytes
 *  @return @ref STL_OK, or @ref STL_ERR_RANGE */
STL_API int stl_string_insert_n(stl_string *s, size_t pos, const char *data, size_t len);

/** @brief Erase a range of bytes.
 *  @param s the string
 *  @param pos byte offset of the first byte to erase
 *  @param len how many bytes to erase; clamped to the remaining length
 *  @return @ref STL_OK, or @ref STL_ERR_RANGE */
STL_API int stl_string_erase(stl_string *s, size_t pos, size_t len);

/** @brief Replace a range of bytes with new text.
 *  @param s the string
 *  @param pos byte offset of the first byte to replace
 *  @param len how many bytes to remove
 *  @param cstr the replacement text
 *  @return @ref STL_OK, or @ref STL_ERR_RANGE */
STL_API int stl_string_replace(stl_string *s, size_t pos, size_t len, const char *cstr);

/** @brief Append a string repeatedly.
 *  @param s the string
 *  @param cstr the text to repeat
 *  @param times how many copies to append
 *  @return @ref STL_OK, or @ref STL_ERR_OVERFLOW */
STL_API int stl_string_repeat(stl_string *s, const char *cstr, size_t times);

/** @} */

/** @name Comparison
 *  @{ */

/** @brief Compare two strings lexicographically.
 *  @param a first string
 *  @param b second string
 *  @return a negative value, zero, or a positive value
 *  @note Compares by byte value, so the result is locale-independent and
 *        embedded NULs are handled correctly. */
STL_API ptrdiff_t stl_string_compare(const stl_string *a, const stl_string *b);

/** @brief Test two strings for equality.
 *  @param a first string
 *  @param b second string
 *  @return non-zero when equal */
STL_API int    stl_string_equals(const stl_string *a, const stl_string *b);

/** @brief Test a string for equality with a C string.
 *  @param a the string
 *  @param cstr the C string
 *  @return non-zero when equal */
STL_API int    stl_string_equals_cstr(const stl_string *a, const char *cstr);

/** @brief Compare a string with a C string.
 *  @param a the string
 *  @param cstr the C string
 *  @return a negative value, zero, or a positive value */
STL_API int    stl_string_compare_cstr(const stl_string *a, const char *cstr);

/** @} */

/** @name Searching
 *  @{ */

/** @brief Find the first occurrence of a substring.
 *  @param s the string
 *  @param needle the substring to look for
 *  @param pos byte offset to start searching from
 *  @return the offset of the match, or @ref STL_NPOS */
STL_API size_t stl_string_find(const stl_string *s, const char *needle, size_t pos);

/** @brief Find the last occurrence of a substring at or before a position.
 *  @param s the string
 *  @param needle the substring to look for
 *  @param pos byte offset at which to stop searching backwards; @ref STL_NPOS
 *             searches the whole string
 *  @return the offset of the match, or @ref STL_NPOS */
STL_API size_t stl_string_rfind(const stl_string *s, const char *needle, size_t pos);

/** @brief Find the first occurrence of a character.
 *  @param s the string
 *  @param c the character
 *  @param pos byte offset to start searching from
 *  @return the offset, or @ref STL_NPOS */
STL_API size_t stl_string_find_char(const stl_string *s, char c, size_t pos);

/** @brief Find the last occurrence of a character at or before a position.
 *  @param s the string
 *  @param c the character
 *  @param pos byte offset at which to stop searching backwards
 *  @return the offset, or @ref STL_NPOS */
STL_API size_t stl_string_rfind_char(const stl_string *s, char c, size_t pos);
/** @brief Test whether a substring occurs anywhere in the string.
 *  @param s the string
 *  @param needle the substring to look for
 *  @return non-zero when present */
STL_API int    stl_string_contains(const stl_string *s, const char *needle);

/** @brief Test whether the string begins with a prefix.
 *  @param s the string
 *  @param prefix the prefix to test for
 *  @return non-zero when the string starts with @p prefix */
STL_API int    stl_string_starts_with(const stl_string *s, const char *prefix);

/** @brief Test whether the string ends with a suffix.
 *  @param s the string
 *  @param suffix the suffix to test for
 *  @return non-zero when the string ends with @p suffix */
STL_API int    stl_string_ends_with(const stl_string *s, const char *suffix);

/** @} */

/** @name Transformation
 *  @{ */

/** @brief Extract a substring into a new string.
 *  @param s the string
 *  @param pos byte offset of the first byte to copy
 *  @param len how many bytes; clamped to the remaining length
 *  @return the new string, or NULL on failure or when @p pos is out of range */
STL_API stl_string *stl_string_substr(const stl_string *s, size_t pos, size_t len);

/** @brief Extract a substring into an existing string.
 *  @param s the string
 *  @param pos byte offset of the first byte to copy
 *  @param len how many bytes
 *  @param out the destination, whose previous contents are replaced
 *  @return @ref STL_OK, or @ref STL_ERR_RANGE */
STL_API int    stl_string_substr_into(const stl_string *s, size_t pos, size_t len, stl_string *out);

/** @brief Duplicate the string as a plain heap C string.
 *  @param s the string
 *  @return a newly allocated copy the caller must release with
 *          @ref stl_string_free_cstr, or NULL on failure */
STL_API char  *stl_string_to_cstr(const stl_string *s);

/** @brief Release a C string obtained from @ref stl_string_to_cstr.
 *  @param p the string to release; NULL is ignored */
STL_API void   stl_string_free_cstr(char *p);

/** @brief Strip leading and trailing whitespace in place.
 *  @param s the string
 *  @return @ref STL_OK, or @ref STL_ERR_INVALID */
STL_API int    stl_string_trim(stl_string *s);

/** @brief Strip leading whitespace in place.
 *  @param s the string
 *  @return @ref STL_OK, or @ref STL_ERR_INVALID */
STL_API int    stl_string_trim_left(stl_string *s);

/** @brief Strip trailing whitespace in place.
 *  @param s the string
 *  @return @ref STL_OK, or @ref STL_ERR_INVALID */
STL_API int    stl_string_trim_right(stl_string *s);

/** @brief Convert every character to upper case in place.
 *  @param s the string
 *  @return @ref STL_OK, or @ref STL_ERR_INVALID
 *  @note Uses the C locale, so only ASCII is affected. */
STL_API int    stl_string_toupper(stl_string *s);

/** @brief Convert every character to lower case in place.
 *  @param s the string
 *  @return @ref STL_OK, or @ref STL_ERR_INVALID
 *  @note Uses the C locale, so only ASCII is affected. */
STL_API int    stl_string_tolower(stl_string *s);

/** @brief Reverse the bytes in place.
 *  @param s the string
 *  @warning Reverses bytes, not characters, so it corrupts multi-byte text. */
STL_API void   stl_string_reverse(stl_string *s);

/** @brief Replace every occurrence of a substring.
 *  @param s the string
 *  @param needle the text to replace
 *  @param repl the replacement
 *  @return @ref STL_OK, or an error code
 *
 * The scan resumes after the replacement, so a @p repl that contains
 * @p needle is not rewritten again. */
STL_API int    stl_string_replace_all(stl_string *s, const char *needle, const char *repl);

/** @brief Split the string on a separator.
 *  @param s the string
 *  @param sep the separator
 *  @param out a vector of @c stl_string @c * elements that receives the pieces
 *  @return how many pieces were produced
 *
 * The pieces are newly allocated; the caller owns them and must free each one
 * before freeing the vector.  An empty string yields one empty piece, matching
 * the behaviour of most split implementations.
 */
STL_API size_t stl_string_split(const stl_string *s, const char *sep, stl_vector *out);

/** @brief Join pieces into one string.
 *  @param out the destination, whose previous contents are replaced
 *  @param parts a vector of @c stl_string @c * elements
 *  @param sep the separator to place between pieces
 *  @return @ref STL_OK, or an error code */
STL_API int    stl_string_join(stl_string *out, const stl_vector *parts, const char *sep);

/** @brief Walk the tokens of a C string.
 *  @param cstr the text to split
 *  @param sep the separator
 *  @param fn callback invoked with each token; returning non-zero stops the walk
 *  @param user opaque pointer forwarded to @p fn
 *  @return @ref STL_OK on success
 *
 * The callback receives a pointer into @p cstr and a length rather than a
 * NUL-terminated copy, so no allocation happens during the walk. */
STL_API int    stl_string_foreach_token(const char *cstr, const char *sep,
                                        int (STL_CALL *fn)(const char *token, size_t len, void *user),
                                        void *user);

/** @} */

/** @name Iterators
 *
 * Iteration walks bytes, so each dereference yields a @c char @c * into the
 * string's buffer.  Unlike a container iterator, a @c stl_string iterator is
 * invalidated by any operation that grows the string.
 *  @{ */

/** @brief Return an iterator to the first byte.
 *  @param s the string */
STL_API stl_iterator stl_string_begin(stl_string *s);

/** @brief Return the past-the-end iterator.
 *  @param s the string */
STL_API stl_iterator stl_string_end(stl_string *s);

/** @brief Advance an iterator by one byte.
 *  @param it the iterator */
STL_API stl_iterator stl_string_iter_next(stl_iterator it);

/** @brief Step an iterator back by one byte.
 *  @param it the iterator */
STL_API stl_iterator stl_string_iter_prev(stl_iterator it);

/** @} */

/** @name C string helpers
 *
 * Small utilities the library needs itself and that are useful when writing
 * portable code, because they do not depend on POSIX-only functions such as
 * @c strdup or @c strcasecmp.
 *  @{ */

/** @brief Format into a newly allocated string.
 *  @param fmt printf-style format
 *  @param ... format arguments
 *  @return the new string, or NULL on failure
 *  @see stl_string_new_fmt */
STL_API stl_string *stl_format(const char *fmt, ...);

/** @brief Format into a fixed-size buffer, always NUL-terminating.
 *  @param buf the output buffer
 *  @param cap capacity of @p buf in bytes
 *  @param fmt printf-style format
 *  @param ap the argument list
 *  @return the number of bytes that would have been written, or -1 on error
 *
 * A portable wrapper over @c vsnprintf that also clears @p buf when formatting
 * fails, so the result is never left unterminated. */
STL_API int    stl_vsnprintf_c(char *buf, size_t cap, const char *fmt, va_list ap);

/** @brief Format into a fixed-size buffer, always NUL-terminating.
 *  @param buf the output buffer
 *  @param cap capacity of @p buf in bytes
 *  @param fmt printf-style format
 *  @param ... format arguments
 *  @return the number of bytes that would have been written, or -1 on error */
STL_API int    stl_snprintf_c(char *buf, size_t cap, const char *fmt, ...);

/** @brief Duplicate a C string into freshly allocated memory.
 *  @param cstr the string to copy
 *  @return the copy, which the caller releases with @ref stl_string_free_cstr,
 *          or NULL on failure */
STL_API char  *stl_strdup(const char *cstr);

/** @brief Duplicate at most @p len bytes of a C string.
 *  @param data the bytes to copy
 *  @param len how many bytes
 *  @return the copy, which the caller releases with @ref stl_string_free_cstr,
 *          or NULL on failure */
STL_API char  *stl_strndup(const char *data, size_t len);

/** @brief Compare two C strings case-insensitively.
 *  @param a first string
 *  @param b second string
 *  @return a negative value, zero, or a positive value */
STL_API int    stl_strcasecmp(const char *a, const char *b);

/** @brief Compare at most @p n bytes of two C strings, case-insensitively.
 *  @param a first string
 *  @param b second string
 *  @param n maximum number of bytes to compare
 *  @return a negative value, zero, or a positive value */
STL_API int    stl_strncasecmp(const char *a, const char *b, size_t n);

/** @} */

/** @} */ /* end of group string */

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ================================================================== */
/* ================================================================== */
/*  algorithms                                                         */
/* ================================================================== */

/** @addtogroup algorithms
 *  @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Predicate over one element.
 *
 * @param elem pointer to the element to test
 * @param user opaque pointer supplied by the caller
 * @return non-zero when the predicate holds
 */
typedef int (STL_CALL *stl_pred_fn)(const void *elem, void *user);

/** @brief Predicate over two elements.
 *
 * @param a pointer to the left-hand element
 * @param b pointer to the right-hand element
 * @param user opaque pointer supplied by the caller
 * @return non-zero when the predicate holds
 */
typedef int (STL_CALL *stl_binary_pred_fn)(const void *a, const void *b, void *user);

/** @brief Operation applied to one element, in place.
 *
 * @param elem pointer to the element to modify
 * @param user opaque pointer supplied by the caller
 */
typedef void (STL_CALL *stl_unary_op_fn)(void *elem, void *user);

/** @brief Operation combining two elements into a third.
 *
 * @param a pointer to the left operand
 * @param b pointer to the right operand
 * @param result where to write the outcome; may alias @p a
 * @param user opaque pointer supplied by the caller
 */
typedef void (STL_CALL *stl_binary_op_fn)(const void *a, const void *b, void *result, void *user);

/** @brief Generator that produces one element.
 *
 * @param elem pointer to the element to fill in
 * @param user opaque pointer supplied by the caller
 */
typedef void (STL_CALL *stl_generator_fn)(void *elem, void *user);

/** @name Counting and searching
 *  @{ */

/** @brief Count the elements satisfying a predicate.
 *  @param base start of the array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param pred predicate
 *  @param user opaque pointer forwarded to @p pred
 *  @return how many elements matched */
STL_API size_t stl_count_if(const void *base, size_t count, size_t elem_size, stl_pred_fn pred, void *user);

/** @brief Count the elements equal to a value.
 *  @param base start of the array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param value pointer to the value to compare against
 *  @param eq equality predicate, or NULL for the default
 *  @return how many elements matched */
STL_API size_t stl_count(const void *base, size_t count, size_t elem_size, const void *value, stl_equal_fn eq);

/** @brief Find the first element equal to a value.
 *  @param base start of the array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param value pointer to the value to look for
 *  @param eq equality predicate, or NULL for the default
 *  @return pointer to the element, or NULL */
STL_API void  *stl_find(const void *base, size_t count, size_t elem_size, const void *value, stl_equal_fn eq);

/** @brief Find the first element satisfying a predicate.
 *  @param base start of the array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param pred predicate
 *  @param user opaque pointer forwarded to @p pred
 *  @return pointer to the element, or NULL */
STL_API void  *stl_find_if(const void *base, size_t count, size_t elem_size, stl_pred_fn pred, void *user);

/** @brief Test whether a predicate holds for every element.
 *  @param base start of the array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param pred predicate
 *  @param user opaque pointer forwarded to @p pred
 *  @return non-zero when all elements match
 *  @note Vacuously true for an empty range, as in C++. */
STL_API int    stl_all_of(const void *base, size_t count, size_t elem_size, stl_pred_fn pred, void *user);

/** @brief Test whether a predicate holds for at least one element.
 *  @param base start of the array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param pred predicate
 *  @param user opaque pointer forwarded to @p pred
 *  @return non-zero when at least one element matches */
STL_API int    stl_any_of(const void *base, size_t count, size_t elem_size, stl_pred_fn pred, void *user);

/** @brief Test whether a predicate holds for no element.
 *  @param base start of the array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param pred predicate
 *  @param user opaque pointer forwarded to @p pred
 *  @return non-zero when no element matches */
STL_API int    stl_none_of(const void *base, size_t count, size_t elem_size, stl_pred_fn pred, void *user);

/** @brief Find the first element that also appears in another array.
 *  @param base start of the array to search
 *  @param count number of elements in it
 *  @param elem_size size of one element in bytes
 *  @param values array of candidate values
 *  @param vcount number of candidates
 *  @param eq equality predicate, or NULL for the default
 *  @return pointer to the element, or NULL */
STL_API void  *stl_find_first_of(const void *base, size_t count, size_t elem_size,
                                 const void *values, size_t vcount, stl_equal_fn eq);

/** @brief Find the first pair of adjacent equal elements.
 *  @param base start of the array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param eq equality predicate, or NULL for the default
 *  @return pointer to the first element of the pair, or NULL */
STL_API void  *stl_adjacent_find(const void *base, size_t count, size_t elem_size, stl_equal_fn eq);

/** @brief Find the smallest element.
 *  @param base start of the array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param cmp ordering predicate
 *  @return pointer to the element, or NULL for an empty range
 *  @note Returns the first of several equal minima. */
STL_API void  *stl_min_element(const void *base, size_t count, size_t elem_size, stl_compare_fn cmp);

/** @brief Find the largest element.
 *  @param base start of the array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param cmp ordering predicate
 *  @return pointer to the element, or NULL for an empty range */
STL_API void  *stl_max_element(const void *base, size_t count, size_t elem_size, stl_compare_fn cmp);

/** @brief Find both the smallest and the largest element in one pass.
 *  @param base start of the array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param cmp ordering predicate
 *  @param min_out receives a pointer to the smallest element
 *  @param max_out receives a pointer to the largest element */
STL_API void   stl_min_max_element(const void *base, size_t count, size_t elem_size, stl_compare_fn cmp,
                                   void **min_out, void **max_out);

/** @} */

/** @name Applying and generating
 *  @{ */

/** @brief Apply an operation to every element, in place.
 *  @param base start of the array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param op operation
 *  @param user opaque pointer forwarded to @p op */
STL_API void   stl_for_each(void *base, size_t count, size_t elem_size, stl_unary_op_fn op, void *user);

/** @brief Apply an operation to each element, writing the results elsewhere.
 *  @param src source array
 *  @param dst destination array, which may equal @p src
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param op operation applied to each element
 *  @param user opaque pointer forwarded to @p op
 *  @return pointer one past the last written element */
STL_API void  *stl_transform(const void *src, void *dst, size_t count, size_t elem_size, stl_unary_op_fn op, void *user);

/** @brief Combine two arrays elementwise into a third.
 *  @param a first source array
 *  @param b second source array
 *  @param dst destination array, which may alias @p a
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param op operation receiving both operands and the output slot
 *  @param user opaque pointer forwarded to @p op
 *  @return pointer one past the last written element */
STL_API void  *stl_transform2(const void *a, const void *b, void *dst, size_t count, size_t elem_size,
                              stl_binary_op_fn op, void *user);

/** @brief Fill an array by calling a generator once per element.
 *  @param base start of the array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param gen generator
 *  @param user opaque pointer forwarded to @p gen */
STL_API void   stl_generate(void *base, size_t count, size_t elem_size, stl_generator_fn gen, void *user);

/** @brief Fill an array with a repeated value.
 *  @param base start of the array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param value pointer to the value to copy into each slot */
STL_API void   stl_fill(void *base, size_t count, size_t elem_size, const void *value);

/** @brief Fill an array with an arithmetic sequence.
 *  @param base start of the array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes; only 4 and 8 are supported
 *  @param start pointer to the first value
 *  @param step pointer to the increment, or NULL to step by one
 *
 * Elements are assigned @c start @c + @c i @c * @c step.  Element sizes other
 * than @c int and @c double fall back to writing @p start into every slot,
 * because there is no way to do arithmetic on an untyped element.
 */
STL_API void   stl_iota(void *base, size_t count, size_t elem_size, const void *start, const void *step);

/** @} */

/** @name Copying and moving
 *  @{ */

/** @brief Copy a range of elements.
 *  @param src source array
 *  @param dst destination array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @return pointer one past the last written element
 *  @note The ranges may overlap. */
STL_API void  *stl_copy(const void *src, void *dst, size_t count, size_t elem_size);

/** @brief Copy a range backwards, element by element.
 *  @param src source array
 *  @param dst destination array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @return @p dst
 *  @note Provided for symmetry with C++; for overlap, @ref stl_copy is
 *        equally safe. */
STL_API void  *stl_copy_backward(const void *src, void *dst, size_t count, size_t elem_size);

/** @brief Move a range of elements.
 *  @param src source array
 *  @param dst destination array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @return pointer one past the last written element
 *  @note A byte move; elements are not "emptied" as in C++. */
STL_API void  *stl_move(const void *src, void *dst, size_t count, size_t elem_size);

/** @brief Test two ranges for elementwise equality.
 *  @param a first array
 *  @param b second array
 *  @param count number of elements in each
 *  @param elem_size size of one element in bytes
 *  @param eq equality predicate, or NULL to compare raw bytes
 *  @return non-zero when the ranges are equal */
STL_API int    stl_equal(const void *a, const void *b, size_t count, size_t elem_size, stl_equal_fn eq);

/** @brief Compare two ranges lexicographically.
 *  @param a first array
 *  @param acount number of elements in @p a
 *  @param b second array
 *  @param bcount number of elements in @p b
 *  @param elem_size size of one element in bytes
 *  @param cmp ordering predicate
 *  @return non-zero when @p a orders before @p b
 *
 * A prefix orders before the longer range that extends it. */
STL_API int    stl_lexicographical_compare(const void *a, size_t acount, const void *b, size_t bcount,
                                           size_t elem_size, stl_compare_fn cmp);

/** @} */

/** @name Reordering in place
 *  @{ */

/** @brief Reverse the elements.
 *  @param base start of the array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @return @p base */
STL_API void  *stl_reverse(void *base, size_t count, size_t elem_size);

/** @brief Rotate the elements left.
 *  @param base start of the array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param n rotation amount; values at or beyond @p count wrap around
 *  @return @p base */
STL_API void  *stl_rotate(void *base, size_t count, size_t elem_size, size_t n);

/** @brief Collapse each run of equal elements down to one.
 *  @param base start of the array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param eq equality predicate, or NULL for the default
 *  @return pointer to the new end of the range
 *
 * Only adjacent duplicates are removed.  The range [return, base + count) is
 * left holding unspecified values. */
STL_API void  *stl_unique(void *base, size_t count, size_t elem_size, stl_equal_fn eq);

/** @brief Move the elements equal to a value to the end.
 *  @param base start of the array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param value pointer to the value to remove
 *  @param eq equality predicate, or NULL for the default
 *  @return pointer to the new end of the retained range */
STL_API void  *stl_remove(void *base, size_t count, size_t elem_size, const void *value, stl_equal_fn eq);

/** @brief Move the elements matching a predicate to the end.
 *  @param base start of the array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param pred predicate; a non-zero result marks the element for removal
 *  @param user opaque pointer forwarded to @p pred
 *  @return pointer to the new end of the retained range */
STL_API void  *stl_remove_if(void *base, size_t count, size_t elem_size, stl_pred_fn pred, void *user);

/** @brief Copy the elements that do not match a predicate.
 *  @param src source array
 *  @param count number of elements
 *  @param dst destination array
 *  @param elem_size size of one element in bytes
 *  @param pred predicate; a non-zero result skips the element
 *  @param user opaque pointer forwarded to @p pred
 *  @return pointer one past the last element written */
STL_API void  *stl_remove_copy_if(const void *src, size_t count, void *dst, size_t elem_size,
                                  stl_pred_fn pred, void *user);

/** @brief Shuffle the elements into a random order.
 *  @param base start of the array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param seed PRNG seed; 0 selects an arbitrary one
 *
 * Uses an unbiased Fisher-Yates shuffle driven by a xorshift32 generator, so
 * the result is deterministic for a given non-zero @p seed. */
STL_API void stl_shuffle(void *base, size_t count, size_t elem_size, unsigned int seed);

/** @} */

/** @name Sorting
 *  @{ */

/** @brief Sort an array ascending.
 *  @param base start of the array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param cmp ordering predicate
 *
 * Introsort: quicksort with a median-of-three pivot, falling back to heapsort
 * once the recursion gets too deep.  O(n log n) worst case; not stable.
 */
STL_API void   stl_sort(void *base, size_t count, size_t elem_size, stl_compare_fn cmp);

/** @brief Sort an array, preserving the order of equal elements.
 *  @param base start of the array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param cmp ordering predicate
 *  @note Costs O(n) scratch space. */
STL_API void   stl_stable_sort(void *base, size_t count, size_t elem_size, stl_compare_fn cmp);

/** @brief Sort only the first @p n elements of an array.
 *  @param base start of the array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param n how many elements to order; the rest are left unspecified
 *  @param cmp ordering predicate */
STL_API void   stl_partial_sort(void *base, size_t count, size_t elem_size, size_t n, stl_compare_fn cmp);

/** @brief Partially order an array around one position.
 *  @param base start of the array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param n index of the element that ends up in its sorted place
 *  @param cmp ordering predicate
 *
 * On return, element @p n is the one that belongs there in sorted order, every
 * element before it compares less, and every element after it compares greater.
 * Cheaper than a full sort when only one order statistic is needed. */
STL_API void   stl_nth_element(void *base, size_t count, size_t elem_size, size_t n, stl_compare_fn cmp);

/** @brief Test whether an array is in non-decreasing order.
 *  @param base start of the array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param cmp ordering predicate
 *  @return non-zero when sorted */
STL_API int    stl_is_sorted(const void *base, size_t count, size_t elem_size, stl_compare_fn cmp);

/** @brief Sort with insertion sort.
 *  @param base start of the array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param cmp ordering predicate
 *
 * O(n^2) but with a very small constant, so it wins on short or nearly sorted
 * ranges.  This is the function @ref stl_sort uses below its cutoff. */
STL_API void stl_insertion_sort(void *base, size_t count, size_t elem_size, stl_compare_fn cmp);

/** @brief Merge two sorted ranges into a third.
 *  @param a first sorted array
 *  @param acount number of elements in @p a
 *  @param b second sorted array
 *  @param bcount number of elements in @p b
 *  @param dst destination array, which must not overlap either source
 *  @param elem_size size of one element in bytes
 *  @param cmp ordering predicate
 *  @return pointer one past the last element written
 *
 * Stable: elements of @p a precede equal elements of @p b. */
STL_API void  *stl_merge(const void *a, size_t acount, const void *b, size_t bcount,
                         void *dst, size_t elem_size, stl_compare_fn cmp);

/** @brief Merge two adjacent sorted runs within one array.
 *  @param base start of the array
 *  @param count total number of elements
 *  @param mid index splitting the array into the two runs
 *  @param elem_size size of one element in bytes
 *  @param cmp ordering predicate
 *  @note Stable, and costs O(mid) scratch space. */
STL_API void   stl_inplace_merge(void *base, size_t count, size_t mid, size_t elem_size, stl_compare_fn cmp);

/** @} */

/** @name Set operations on sorted ranges
 *
 * Each writes into a caller-provided buffer, which must be large enough: the
 * sizes below are exact upper bounds.
 *
 * @par Output size
 * - union and symmetric difference: @c acount @c + @c bcount
 * - intersection: @c min(acount, @c bcount)
 * - difference: @c acount
 *  @{ */

/** @brief Write the union of two sorted ranges.
 *  @param a first sorted range
 *  @param acount number of elements in @p a
 *  @param b second sorted range
 *  @param bcount number of elements in @p b
 *  @param dst output buffer of at least @c acount @c + @c bcount elements
 *  @param elem_size size of one element in bytes
 *  @param cmp ordering predicate
 *  @return pointer one past the last element written */
STL_API void  *stl_set_union_raw(const void *a, size_t acount, const void *b, size_t bcount,
                                 void *dst, size_t elem_size, stl_compare_fn cmp);

/** @brief Write the intersection of two sorted ranges.
 *  @param a first sorted range
 *  @param acount number of elements in @p a
 *  @param b second sorted range
 *  @param bcount number of elements in @p b
 *  @param dst output buffer
 *  @param elem_size size of one element in bytes
 *  @param cmp ordering predicate
 *  @return pointer one past the last element written */
STL_API void  *stl_set_intersection_raw(const void *a, size_t acount, const void *b, size_t bcount,
                                        void *dst, size_t elem_size, stl_compare_fn cmp);
/** @brief Write the elements of a sorted range that are absent from another.
 *  @param a first sorted range
 *  @param acount number of elements in @p a
 *  @param b second sorted range
 *  @param bcount number of elements in @p b
 *  @param dst output buffer of at least @p acount elements
 *  @param elem_size size of one element in bytes
 *  @param cmp ordering predicate
 *  @return pointer one past the last element written */
STL_API void  *stl_set_difference_raw(const void *a, size_t acount, const void *b, size_t bcount,
                                      void *dst, size_t elem_size, stl_compare_fn cmp);

/** @brief Write the elements present in exactly one of two sorted ranges.
 *  @param a first sorted range
 *  @param acount number of elements in @p a
 *  @param b second sorted range
 *  @param bcount number of elements in @p b
 *  @param dst output buffer of at least @c acount @c + @c bcount elements
 *  @param elem_size size of one element in bytes
 *  @param cmp ordering predicate
 *  @return pointer one past the last element written */
STL_API void  *stl_set_symmetric_difference_raw(const void *a, size_t acount, const void *b, size_t bcount,
                                                void *dst, size_t elem_size, stl_compare_fn cmp);

/** @brief Test whether one sorted range contains every element of another.
 *  @param a the candidate superset, sorted
 *  @param acount number of elements in @p a
 *  @param b the candidate subset, sorted
 *  @param bcount number of elements in @p b
 *  @param elem_size size of one element in bytes
 *  @param cmp ordering predicate
 *  @return non-zero when @p b is contained in @p a */
STL_API int    stl_includes(const void *a, size_t acount, const void *b, size_t bcount,
                            size_t elem_size, stl_compare_fn cmp);

/** @brief Fold an operation over a range.
 *
 * @param base start of the array
 * @param count number of elements
 * @param elem_size size of one element in bytes
 * @param result the accumulator, updated in place
 * @param op operation invoked as @c op(result, @c elem, @c result, @c user)
 * @param user opaque pointer forwarded to @p op
 * @return @p result
 *
 * The classic use is a running total:
 * @code
 * int sum = 0;
 * stl_accumulate(values, n, sizeof(int), &sum, stl_op_add_int, NULL);
 * @endcode
 */
STL_API void  *stl_accumulate(const void *base, size_t count, size_t elem_size,
                              void *result, stl_binary_op_fn op, void *user);

/** @brief Fold an operation over a range, starting from an initial value.
 *  @param base start of the array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param init pointer to the initial accumulator value, or NULL to start from
 *              whatever @p result already holds
 *  @param result the accumulator, updated in place
 *  @param op operation invoked as @c op(result, @c elem, @c result, @c user)
 *  @param user opaque pointer forwarded to @p op
 *  @return @p result */
STL_API void  *stl_reduce(const void *base, size_t count, size_t elem_size,
                          const void *init, void *result, stl_binary_op_fn op, void *user);

/** @brief Write the union of two sorted ranges into @p dst.
 *  @param a first sorted range
 *  @param acount number of elements in @p a
 *  @param b second sorted range
 *  @param bcount number of elements in @p b
 *  @param dst output buffer of at least @c acount @c + @c bcount elements
 *  @param elem_size size of one element in bytes
 *  @param cmp ordering predicate
 *  @return pointer one past the last element written
 *  @note The typed counterpart for sets is @ref stl_set_union. */

/** @} */

/** @name Convenience comparators
 *
 * The fixed-width predicates declared with the other comparators cover most
 * cases; these fill the remaining gaps.
 *  @{ */

/** @brief Order two @c long elements.
 *  @param a pointer to the left element
 *  @param b pointer to the right element */
STL_API int STL_CALL stl_cmp_long(const void *a, const void *b);

/** @brief Order two @c unsigned @c long elements.
 *  @param a pointer to the left element
 *  @param b pointer to the right element */
STL_API int STL_CALL stl_cmp_ulong(const void *a, const void *b);

/** @brief Order two @c size_t elements.
 *  @param a pointer to the left element
 *  @param b pointer to the right element */
STL_API int STL_CALL stl_cmp_size(const void *a, const void *b);

/** @brief Order two @c double elements in descending order.
 *
 * @param a pointer to the left element
 * @param b pointer to the right element
 *
 * Pass this to @ref stl_priority_queue_new to build a min-heap, or to
 * @ref stl_sort to sort descending.
 */
STL_API int STL_CALL stl_cmp_double_desc(const void *a, const void *b);

/** @} */

/** @} */ /* end of group algorithms */

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ================================================================== */
/* ================================================================== */
/*  functors / binders (poor man's <functional>)                       */
/* ================================================================== */

/** @addtogroup comparators
 *  @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A callable: a function pointer together with its context.
 *
 * A convenience for code that wants to store "an operation and its argument"
 * as one value, for example to pass both through an API that only accepts a
 * single @c void @c *.
 */
typedef struct stl_functor {
    void *fn;    /**< The function pointer. */
    void *user;  /**< Context passed to it.   */
} stl_functor;

/** @name Numeric helpers for @c int and @c double arrays
 *  @{ */

/** @brief Sum the elements of an @c int array.
 *  @param base start of the array
 *  @param count number of elements
 *  @return the sum; 0 for an empty range
 *  @note The accumulator is a @c long, so it does not overflow as early as the
 *        element type. */
STL_API long   stl_sum_int(const void *base, size_t count);

/** @brief Sum the elements of a @c double array.
 *  @param base start of the array
 *  @param count number of elements
 *  @return the sum */
STL_API double stl_sum_double(const void *base, size_t count);

/** @brief Compute the arithmetic mean of a @c double array.
 *  @param base start of the array
 *  @param count number of elements
 *  @return the mean, or 0.0 for an empty range */
STL_API double stl_mean_double(const void *base, size_t count);

/** @} */

/** @name Ready-made predicates
 *  @{ */

/** @brief Predicate: the @c int element is even.
 *  @param elem pointer to the element
 *  @param user ignored */
STL_API int STL_CALL stl_pred_is_even_int(const void *elem, void *user);

/** @brief Predicate: the @c int element is odd.
 *  @param elem pointer to the element
 *  @param user ignored */
STL_API int STL_CALL stl_pred_is_odd_int(const void *elem, void *user);

/** @brief Predicate: the @c double element is greater than zero.
 *  @param elem pointer to the element
 *  @param user ignored */
STL_API int STL_CALL stl_pred_is_positive_double(const void *elem, void *user);

/** @brief Predicate: the @c int element exceeds a threshold.
 *  @param elem pointer to the element
 *  @param user pointer to the @c int threshold */
STL_API int STL_CALL stl_pred_greater_than_int(const void *elem, void *user);

/** @} */

/** @name Ready-made operations
 *  @{ */

/** @brief Negate an @c int element in place.
 *  @param elem pointer to the element
 *  @param user ignored */
STL_API void STL_CALL stl_op_negate_int(void *elem, void *user);

/** @brief Double an @c int element in place.
 *  @param elem pointer to the element
 *  @param user ignored */
STL_API void STL_CALL stl_op_double_int(void *elem, void *user);

/** @brief Square an @c int element in place.
 *  @param elem pointer to the element
 *  @param user ignored */
STL_API void STL_CALL stl_op_square_int(void *elem, void *user);

/** @brief Add two @c int operands.
 *  @param a pointer to the left operand
 *  @param b pointer to the right operand
 *  @param result where to write the sum
 *  @param user ignored */
STL_API void STL_CALL stl_op_add_int(const void *a, const void *b, void *result, void *user);

/** @brief Add two @c double operands.
 *  @param a pointer to the left operand
 *  @param b pointer to the right operand
 *  @param result where to write the sum
 *  @param user ignored */
STL_API void STL_CALL stl_op_add_double(const void *a, const void *b, void *result, void *user);

/** @brief Write the larger of two @c int operands.
 *  @param a pointer to the left operand
 *  @param b pointer to the right operand
 *  @param result where to write the maximum
 *  @param user ignored */
STL_API void STL_CALL stl_op_max_int(const void *a, const void *b, void *result, void *user);

/** @} */

/** @name Ready-made generators
 *  @{ */

/** @brief Fill an element with a pseudo-random @c int.
 *  @param elem pointer to the element
 *  @param user pointer to an @c unsigned @c int PRNG state, or NULL to use a
 *              seed derived from the clock */
STL_API void STL_CALL stl_gen_rand_int(void *elem, void *user);

/** @brief Fill an element with an incrementing counter.
 *  @param elem pointer to the element
 *  @param user pointer to an @c int counter that is incremented on each call */
STL_API void STL_CALL stl_gen_index_int(void *elem, void *user);

/** @} */

/** @} */ /* end of group comparators */

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ================================================================== */
/* ================================================================== */
/*  bit operations on dynamic bitsets                                  */
/* ================================================================== */

/** @addtogroup bitset
 *  @{
 */

/** @brief A fixed-size, dynamically allocated array of bits.
 *
 * The counterpart of @c std::bitset, except that the size is chosen at run
 * time rather than at compile time.  Bits are packed into machine words, so a
 * bitset of @e n bits costs about @e n/8 bytes.
 *
 * @par Complexity
 * Single-bit operations are O(1).  The set operations are O(n/8), that is,
 * linear in the number of words rather than the number of bits.
 *
 * @par Example
 * @code
 * stl_bitset *seen = stl_bitset_new(1000);
 * stl_bitset_set(seen, 42);
 * if (stl_bitset_test(seen, 42)) {
 *     printf("%lu bits set\n", (unsigned long)stl_bitset_count(seen));
 * }
 * stl_bitset_free(seen);
 * @endcode
 */
typedef struct stl_bitset stl_bitset;

#ifdef __cplusplus
extern "C" {
#endif

/** @name Construction and destruction
 *  @{ */

/** @brief Create a bitset with every bit cleared.
 *  @param nbits number of bits
 *  @return the new bitset, or NULL on failure */
STL_API stl_bitset *stl_bitset_new(size_t nbits);

/** @brief Create a bitset that allocates through a specific allocator.
 *  @param nbits number of bits
 *  @param a allocator, or NULL for the default
 *  @return the new bitset, or NULL on failure */
STL_API stl_bitset *stl_bitset_new_a(size_t nbits, const stl_allocator *a);

/** @brief Destroy a bitset.
 *  @param b the bitset; NULL is ignored */
STL_API void stl_bitset_free(stl_bitset *b);

/** @brief Duplicate a bitset.
 *  @param b the bitset to copy
 *  @return the new bitset, or NULL on failure */
STL_API stl_bitset *stl_bitset_copy(const stl_bitset *b);

/** @} */

/** @name Size and queries
 *  @{ */

/** @brief Return the number of bits.
 *  @param b the bitset */
STL_API size_t stl_bitset_size(const stl_bitset *b);

/** @brief Count the set bits.
 *  @param b the bitset
 *  @return the population count */
STL_API size_t stl_bitset_count(const stl_bitset *b);

/** @brief Test one bit.
 *  @param b the bitset
 *  @param i zero-based bit index
 *  @return non-zero when the bit is set; 0 for an out-of-range index */
STL_API int    stl_bitset_test(const stl_bitset *b, size_t i);

/** @brief Test whether no bit is set.
 *  @param b the bitset */
STL_API int    stl_bitset_none(const stl_bitset *b);

/** @brief Test whether at least one bit is set.
 *  @param b the bitset */
STL_API int    stl_bitset_any(const stl_bitset *b);

/** @brief Test whether every bit is set.
 *  @param b the bitset */
STL_API int    stl_bitset_all(const stl_bitset *b);

/** @brief Test two bitsets for equality.
 *  @param a first bitset
 *  @param b second bitset
 *  @return non-zero when the sizes and all bits match */
STL_API int    stl_bitset_equals(const stl_bitset *a, const stl_bitset *b);

/** @} */

/** @name Modifying bits
 *  @{ */

/** @brief Set one bit.
 *  @param b the bitset
 *  @param i zero-based bit index; out-of-range reports @ref STL_ERR_RANGE */
STL_API void   stl_bitset_set(stl_bitset *b, size_t i);

/** @brief Clear one bit.
 *  @param b the bitset
 *  @param i zero-based bit index */
STL_API void   stl_bitset_reset(stl_bitset *b, size_t i);

/** @brief Toggle one bit.
 *  @param b the bitset
 *  @param i zero-based bit index */
STL_API void   stl_bitset_flip(stl_bitset *b, size_t i);

/** @brief Set every bit.
 *  @param b the bitset */
STL_API void   stl_bitset_set_all(stl_bitset *b);

/** @brief Clear every bit.
 *  @param b the bitset */
STL_API void   stl_bitset_reset_all(stl_bitset *b);

/** @brief Toggle every bit.
 *  @param b the bitset */
STL_API void   stl_bitset_flip_all(stl_bitset *b);

/** @} */

/** @name Bitwise operations
 *
 * Each writes its result into @p r, which may alias either operand.  All three
 * bitsets must have the same size, otherwise @ref STL_ERR_INVALID is reported.
 *  @{ */

/** @brief Write the bitwise AND of two bitsets.
 *  @param r receives the result
 *  @param a first operand
 *  @param b second operand
 *  @return @ref STL_OK, or @ref STL_ERR_INVALID on a size mismatch */
STL_API int    stl_bitset_and(stl_bitset *r, const stl_bitset *a, const stl_bitset *b);

/** @brief Write the bitwise OR of two bitsets.
 *  @param r receives the result
 *  @param a first operand
 *  @param b second operand
 *  @return @ref STL_OK, or @ref STL_ERR_INVALID on a size mismatch */
STL_API int    stl_bitset_or(stl_bitset *r, const stl_bitset *a, const stl_bitset *b);

/** @brief Write the bitwise XOR of two bitsets.
 *  @param r receives the result
 *  @param a first operand
 *  @param b second operand
 *  @return @ref STL_OK, or @ref STL_ERR_INVALID on a size mismatch */
STL_API int    stl_bitset_xor(stl_bitset *r, const stl_bitset *a, const stl_bitset *b);

/** @brief Write the complement of a bitset.
 *  @param r receives the result; may alias @p a
 *  @param a the operand
 *  @return @ref STL_OK, or @ref STL_ERR_INVALID on a size mismatch */
STL_API int    stl_bitset_not(stl_bitset *r, const stl_bitset *a);
/** @} */

/** @name Searching and iteration
 *  @{ */

/** @brief Return the index of the lowest set bit.
 *  @param b the bitset
 *  @return the index, or @ref STL_NPOS when no bit is set */
STL_API size_t stl_bitset_find_first(const stl_bitset *b);

/** @brief Return the index of the next set bit.
 *  @param b the bitset
 *  @param from index to start searching from, exclusive
 *  @return the index, or @ref STL_NPOS when nothing is set at or after @p from */
STL_API size_t stl_bitset_find_next(const stl_bitset *b, size_t from);

/** @brief Invoke a callback once per set bit.
 *  @param b the bitset
 *  @param fn callback receiving the bit index; a non-zero result stops the walk
 *  @param user opaque pointer forwarded to @p fn */
STL_API void   stl_bitset_foreach(const stl_bitset *b, int (STL_CALL *fn)(size_t index, void *user), void *user);

/** @brief Render the bitset as a string of @c '0' and @c '1'.
 *  @param b the bitset
 *  @return a new string with one character per bit, index 0 first, or NULL on
 *          failure */
STL_API stl_string *stl_bitset_to_string(const stl_bitset *b);

/** @} */

/** @} */ /* end of group bitset */

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ================================================================== */
/* ================================================================== */
/*  threaded helpers: thread-safe wrappers and parallel algorithms     */
/* ================================================================== */

/** @addtogroup threading
 *  @{
 */

/** @brief A mutual-exclusion lock that spins instead of sleeping. */
typedef struct stl_spinlock stl_spinlock;

/** @brief A lock permitting either many readers or one writer. */
typedef struct stl_rwlock   stl_rwlock;

#ifdef __cplusplus
extern "C" {
#endif

/** @name Spinlock
 *
 * Built on an atomic test-and-set, so it needs no operating system support and
 * works on every platform the library targets.  Best for very short critical
 * sections: a thread waiting on a contended spinlock burns CPU rather than
 * yielding.
 *  @{ */

/** @brief Allocate a spinlock.
 *  @return the new lock, already unlocked, or NULL on failure */
STL_API stl_spinlock *stl_spinlock_new(void);

/** @brief Release a spinlock.
 *  @param lock the lock; NULL is ignored
 *  @warning The lock must not be held by any thread. */
STL_API void stl_spinlock_free(stl_spinlock *lock);

/** @brief Acquire the lock, spinning until it is free.
 *  @param lock the lock */
STL_API void stl_spinlock_lock(stl_spinlock *lock);

/** @brief Release the lock.
 *  @param lock the lock */
STL_API void stl_spinlock_unlock(stl_spinlock *lock);

/** @brief Try to acquire the lock without spinning.
 *  @param lock the lock
 *  @return non-zero when the lock was acquired */
STL_API int  stl_spinlock_trylock(stl_spinlock *lock);

/** @} */

/** @name Reader/writer lock
 *
 * Maps to @c pthread_rwlock_t where that exists.  On platforms without it the
 * type degrades to spinlock semantics -- correct, but without the benefit of
 * concurrent readers.  Query @ref stl_rwlock_is_native to find out which one
 * you got.
 *  @{ */

/** @brief Initialise a caller-provided reader/writer lock.
 *  @param lock storage for the lock
 *  @return @ref STL_OK, or an error code
 *  @note For a heap-allocated lock, @ref stl_rwlock_new is more convenient. */
STL_API int  stl_rwlock_init(stl_rwlock *lock);

/** @brief Release the resources of an initialised lock.
 *  @param lock the lock */
STL_API void stl_rwlock_destroy(stl_rwlock *lock);

/** @brief Allocate and initialise a reader/writer lock.
 *  @return the new lock, or NULL when unavailable */
STL_API stl_rwlock *stl_rwlock_new(void);

/** @brief Destroy a lock allocated by @ref stl_rwlock_new.
 *  @param lock the lock; NULL is ignored */
STL_API void stl_rwlock_free(stl_rwlock *lock);

/** @brief Acquire the lock for shared reading, blocking until available.
 *  @param lock the lock */
STL_API void stl_rwlock_rdlock(stl_rwlock *lock);

/** @brief Acquire the lock for exclusive writing, blocking until available.
 *  @param lock the lock */
STL_API void stl_rwlock_wrlock(stl_rwlock *lock);

/** @brief Try to acquire the lock for reading without blocking.
 *  @param lock the lock
 *  @return non-zero when the lock was acquired */
STL_API int  stl_rwlock_tryrdlock(stl_rwlock *lock);

/** @brief Try to acquire the lock for writing without blocking.
 *  @param lock the lock
 *  @return non-zero when the lock was acquired */
STL_API int  stl_rwlock_trywrlock(stl_rwlock *lock);

/** @brief Release the lock, whether held for reading or for writing.
 *  @param lock the lock */
STL_API void stl_rwlock_unlock(stl_rwlock *lock);

/** @brief Report which implementation backs the lock.
 *  @return non-zero when @c pthread_rwlock_t is in use, zero when the lock
 *          degrades to a spinlock */
STL_API int  stl_rwlock_is_native(void);

/** @} */

/** @name Thread-safe containers
 *
 * Thin wrappers that take the lock around every operation.  They are the quick
 * way to share a container between threads; a program that needs compound
 * operations to be atomic should hold its own lock instead, because two
 * separate calls are not atomic together.
 *  @{ */

/** @brief A vector protected by a reader/writer lock. */
typedef struct stl_safe_vector stl_safe_vector;

/** @brief A map from @c int to @c int protected by a reader/writer lock. */
typedef struct stl_safe_map    stl_safe_map;

/** @brief Create a thread-safe vector.
 *  @param elem_size size of one element in bytes
 *  @return the new vector, or NULL on failure */
STL_API stl_safe_vector *stl_safe_vector_new(size_t elem_size);

/** @brief Destroy a thread-safe vector.
 *  @param v the vector; NULL is ignored
 *  @warning The caller must ensure no other thread is using it. */
STL_API void  stl_safe_vector_free(stl_safe_vector *v);

/** @brief Append an element under the write lock.
 *  @param v the vector
 *  @param elem pointer to the element to copy in
 *  @return @ref STL_OK, or an error code */
STL_API int   stl_safe_vector_push_back(stl_safe_vector *v, const void *elem);

/** @brief Remove and return the last element under the write lock.
 *  @param v the vector
 *  @param out receives the element, or NULL to discard it
 *  @return @ref STL_OK, or @ref STL_ERR_EMPTY */
STL_API int   stl_safe_vector_pop_back(stl_safe_vector *v, void *out);

/** @brief Return the element count under the read lock.
 *  @param v the vector */
STL_API size_t stl_safe_vector_size(stl_safe_vector *v);

/** @brief Copy one element out under the read lock.
 *  @param v the vector
 *  @param i zero-based index
 *  @param out receives the element
 *  @return @ref STL_OK, or @ref STL_ERR_RANGE */
STL_API int   stl_safe_vector_at(stl_safe_vector *v, size_t i, void *out);

/** @brief Remove every element under the write lock.
 *  @param v the vector
 *  @return @ref STL_OK, or an error code */
STL_API int   stl_safe_vector_clear(stl_safe_vector *v);

/** @brief Copy several elements out in one locked pass.
 *  @param v the vector
 *  @param out destination buffer
 *  @param count how many elements to copy at most
 *  @return the number of elements actually copied
 *
 * Cheaper and more consistent than repeated @ref stl_safe_vector_at calls,
 * which could interleave with a writer. */
STL_API int   stl_safe_vector_snapshot(stl_safe_vector *v, void *out, size_t count);

/** @brief Create a thread-safe integer map.
 *  @return the new map, or NULL on failure */
STL_API stl_safe_map *stl_safe_map_new(void);

/** @brief Destroy a thread-safe map.
 *  @param m the map; NULL is ignored */
STL_API void  stl_safe_map_free(stl_safe_map *m);

/** @brief Insert or overwrite an entry under the write lock.
 *  @param m the map
 *  @param key the key
 *  @param value the value
 *  @return @ref STL_OK, or an error code */
STL_API int   stl_safe_map_put(stl_safe_map *m, int key, int value);

/** @brief Look up a key under the read lock.
 *  @param m the map
 *  @param key the key
 *  @param out receives the value, or NULL to discard it
 *  @return @ref STL_OK, or @ref STL_ERR_NOT_FOUND */
STL_API int   stl_safe_map_get(stl_safe_map *m, int key, int *out);

/** @brief Remove a key under the write lock.
 *  @param m the map
 *  @param key the key
 *  @return @ref STL_OK, or @ref STL_ERR_NOT_FOUND */
STL_API int   stl_safe_map_erase(stl_safe_map *m, int key);

/** @brief Return the entry count under the read lock.
 *  @param m the map */
STL_API size_t stl_safe_map_size(stl_safe_map *m);

/** @} */

/** @name Parallel algorithms
 *
 * Each splits the work across threads and falls back to a single-threaded pass
 * when the library was built without pthread support, so the results are
 * correct either way.
 *  @{ */

/** @brief Callback invoked on one slice of an array.
 *
 * @param base start of the slice
 * @param begin index of the slice's first element in the whole array
 * @param end index one past the slice's last element
 * @param user opaque pointer supplied by the caller
 */
typedef void (STL_CALL *stl_range_fn)(void *base, size_t begin, size_t end, void *user);

/** @brief Apply a callback to each element, in parallel.
 *
 * @param base start of the array
 * @param count number of elements
 * @param elem_size size of one element in bytes
 * @param fn callback, invoked once per slice
 * @param user opaque pointer forwarded to @p fn
 * @param nthreads worker count; 0 selects @ref stl_hardware_concurrency
 * @return the number of threads actually used
 *
 * The array is split into contiguous slices, one per thread. */
STL_API int stl_parallel_for_each(void *base, size_t count, size_t elem_size,
                                  stl_range_fn fn, void *user, int nthreads);

/** @brief Sort an array using several threads.
 *  @param base start of the array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param cmp ordering predicate
 *  @param nthreads worker count; 0 selects @ref stl_hardware_concurrency
 *  @return the number of threads actually used
 *  @note Correctness never depends on the thread count; the function verifies
 *        the result and falls back to a serial sort if the merge left anything
 *        out of order. */
STL_API int stl_parallel_sort(void *base, size_t count, size_t elem_size,
                              stl_compare_fn cmp, int nthreads);

/** @brief Sort an array with a stable parallel merge sort.
 *  @param base start of the array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param cmp ordering predicate
 *  @param nthreads worker count; 0 selects @ref stl_hardware_concurrency
 *  @return the number of threads actually used */
STL_API int stl_parallel_merge_sort(void *base, size_t count, size_t elem_size,
                                    stl_compare_fn cmp, int nthreads);

/** @brief Apply an operation to each element, in parallel.
 *  @param src source array
 *  @param dst destination array
 *  @param count number of elements
 *  @param elem_size size of one element in bytes
 *  @param op operation applied to each element
 *  @param user opaque pointer forwarded to @p op
 *  @param nthreads worker count; 0 selects @ref stl_hardware_concurrency
 *  @return the number of threads actually used */
STL_API int stl_parallel_transform(const void *src, void *dst, size_t count, size_t elem_size,
                                   stl_unary_op_fn op, void *user, int nthreads);

/** @} */

/** @name Platform and random
 *  @{ */

/** @brief Return the number of processors available to the process.
 *  @return the processor count, never less than 1 */
STL_API int         stl_hardware_concurrency(void);

/** @brief Return an opaque identifier for the calling thread.
 *  @return a value usable for logging and debugging, not for synchronisation */
STL_API unsigned long stl_thread_id(void);

/** @brief Produce a seed for the library's pseudo-random generator.
 *  @return a value derived from the clock, the process and the thread */
STL_API unsigned int  stl_random_seed(void);

/** @brief Draw the next value from an xorshift32 generator.
 *  @param state generator state, updated in place; a state of zero is replaced
 *               by a fixed non-zero seed
 *  @return the next pseudo-random value */
STL_API unsigned int  stl_rand_next(unsigned int *state);
/** @brief Draw a pseudo-random value in [0, 1).
 *  @param state generator state, updated in place
 *  @return a double in the half-open interval [0, 1) */
STL_API double        stl_rand_double(unsigned int *state);

/** @brief Draw a pseudo-random integer from an inclusive range.
 *  @param state generator state, updated in place
 *  @param lo inclusive lower bound
 *  @param hi inclusive upper bound
 *  @return a value in [lo, hi]; @p lo when the range is empty */
STL_API int           stl_rand_range(unsigned int *state, int lo, int hi);

/** @brief Suspend the calling thread.
 *  @param ms how long to sleep, in milliseconds
 *
 * Uses @c nanosleep, @c Sleep or a busy wait depending on the platform, so it
 * works even in builds without pthread support. */
STL_API void          stl_sleep_ms(unsigned int ms);

/** @} */

/** @} */ /* end of group threading */

#ifdef __cplusplus
} /* extern "C" */
#endif

/* ================================================================== */
/*  version / diagnostics                                              */
/* ================================================================== */

/** @addtogroup introspection
 *  @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Return the library version as a string.
 *  @return a static string such as @c "1.0.0" */
STL_API const char *stl_version_string(void);

/** @brief Return the library version as an integer.
 *  @return @c (major @c << @c 16) @c | @c (minor @c << @c 8) @c | @c patch
 *
 * Compare against the @c STL_VERSION macro to detect a header/library
 * mismatch at run time. */
STL_API int         stl_version(void);

/** @brief Report whether the library was built with threading support.
 *  @return non-zero when pthread support was compiled in */
STL_API int         stl_is_thread_supported(void);

/** @brief Return the size of a pointer in this build.
 *  @return 4 or 8
 *  @note Useful for diagnosing a header/library mismatch across ABIs. */
STL_API size_t      stl_size_of_pointer(void);

/** @} */ /* end of group introspection */

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


/* End of public interface. */

#endif /* LIBSTL_H_INCLUDED */
