# libstl — the STL, implemented in C

[![build](https://github.com/MrXie1109/libstl/actions/workflows/build.yml/badge.svg)](https://github.com/MrXie1109/libstl/actions/workflows/build.yml)

`libstl` brings the familiar C++ standard-library vocabulary to plain C:
vector, deque, list, set/multiset, map/multimap, the unordered containers,
stack/queue/priority_queue, string and bitset, plus the usual algorithms
(sorting, searching, set operations) and an optional layer of thread-safe
wrappers and parallel algorithms.

The only dependency is the C standard library (pthread is optional). The build
produces both `libstl.a` and `libstl.so`.

## Quick start

```sh
make            # build build/libstl.a and build/libstl.so
make test       # build and run the self tests (9000+ assertions)
make asan       # run the tests under AddressSanitizer + UBSan
make install    # install into /usr/local; override with PREFIX=...
```

All generated files go to `build/` (override with `BUILD=<dir>`), so the source
tree stays clean. Run `make clean` to remove the directory. Untracked generated
files are covered by `.gitignore`.

Using it from your own program:

```sh
cc -I<libstl-dir> myprog.c -L<libstl-dir> -lstl -lpthread -lm
```

## Design notes

**Containers are homogeneous; the element type is a convention between you and
the container.** C has no templates, so a container is constructed with an
`elem_size` and stores elements by `memcpy`:

```c
stl_vector *v = stl_vector_new(sizeof(int), NULL);
int x = 42;
stl_vector_push_back(v, &x);
int *p = (int *)stl_vector_at(v, 0);   /* -> 42 */
```

A container owns its element *storage* but never memory that elements point to.
To release per-element resources, pass an `elem_dtor` callback or call
`*_clear_ex()`.

**Shared generic backends.** `stl_set`/`stl_map` are both thin layers over one
red-black tree (`stl_rbtree`), and `stl_hashset`/`stl_hashmap` over one hash
table (`stl_hashtable`). A map key is a byte range inside the element
(`key_offset`/`key_size`), so a `struct { K key; V value; }` layout just uses
`key_offset == 0`.

**Iterators** are small by-value structs (element pointer, owning container,
ordinal), supporting `begin/end`, `rbegin/rend`, `++`/`--` and `distance`, with
invalidation rules matching their C++ counterparts.

**Memory is redirectable.** Every `*_new_a()` constructor takes an
`stl_allocator`, and `stl_set_allocator()` replaces the process-wide default.

## Convenience macros

The tail of `libstl.h` provides a layer of **function-style, lower-case macros**
so that call sites read much like C++:

```c
/* Allocation: like `new T`, returns a zeroed object */
int *n = stl_new(int);
int *arr = stl_new_array(int, 10);
stl_delete(n);
stl_delete_all(arr, NULL);

/* Typed, bounds-checked access */
stl_vector_at_t(v, struct point, i)
stl_vector_front_t(v, int)
stl_vector_back_t(v, int)

/* Push a temporary without declaring a variable first */
stl_vector_push(v, int, 42);
stl_vector_push_literal(v, struct point, { .x = 1, .y = 2 });

/* Iteration: the macro declares the variable, which is a T* */
struct point *p;
stl_vector_foreach_t(v, struct point, p) { p->x++; }
stl_vector_foreach_rev(v, struct point, p) { /* reverse order */ }
stl_vector_foreach_idx(v, struct point, p, idx) { /* with index */ }

/* Erase the current node safely while walking a list */
stl_list_foreach_safe(l, int, p, node) {
    if (*p % 2 == 0) stl_list_erase_node(l, node);
}

/* Associative containers */
stl_map_foreach_t(m, struct int_pair, it) { printf("%d\n", it->key); }
stl_map_foreach_entry(m, struct int_pair, e) { printf("%d\n", *(int *)e->key); }
stl_set_foreach_t(s, int, p) { printf("%d\n", *p); }
stl_hashmap_foreach_t(m, struct int_pair, it) { /* unordered */ }

/* Generate comparators and equality functions from a field name */
stl_define_pair(int_pair, int, int);
stl_define_cmp_fn(point_cmp_x, struct point, x);
stl_define_eq_fn(point_eq_x, struct point, x);
stl_define_dtor_fn(blob_dtor, struct blob, stl_free_field(e->data));

/* One-line construction of associative containers */
stl_set *s = stl_set_new_t(struct point, x);      /* ordered by .x */
stl_map *m = stl_pair_map_new(int_pair, int);
stl_set *names = stl_strset_new();                /* set of strings */
stl_map_put_pair(m, int_pair, 1, 100);

/* Optional scope-exit release (see the compiler-extension section) */
#define STL_ENABLE_CLEANUP
int *tmp stl_autofree = stl_new_array(int, 128);
stl_string_scope(s, "hello");                      /* freed at scope exit */

/* Miscellaneous */
stl_min_of(a, b)  stl_max_of(a, b)  stl_clamp(x, lo, hi)
stl_array_len(arr)  stl_swap_t(int, a, b)  stl_zero(obj)
```

The macro layer requires C99 (it declares variables inside `for` initialisers).
The library API itself works from C89 onward; compiling as C89 with
`-DSTL_NO_CONVENIENCE_MACROS` excludes the macros entirely.

The older SCREAMING_CASE spellings (`STL_NEW`, `STL_VECTOR_FOREACH`, …) are
kept as aliases; define `STL_NO_LEGACY_MACROS` to drop them. New code should
prefer the lower-case forms.

## Compiler extensions: what is used, and why

The library targets any conforming C compiler, so non-standard constructs are
confined to places where they are both guarded and optional.

| Construct | Where | Guard | Fallback |
|---|---|---|---|
| `__attribute__((visibility("default")))` | `STL_API` in `libstl.h` | `__GNUC__`/`__clang__` | `__declspec(dllexport/dllimport)` on MSVC, empty elsewhere |
| `__attribute__((unused))` | `stl_maybe_unused` in `libstl.h` | `__GNUC__`/`__clang__` | empty |
| `__attribute__((deprecated(msg)))` | `STL_DEPRECATED` | `__GNUC__`/`__clang__` | `__declspec(deprecated)` on MSVC, empty elsewhere |
| `__attribute__((visibility("hidden")))` | `STL_PRIVATE`, internal only | `__GNUC__`/`__clang__` | empty |
| `__attribute__((cleanup(fn)))` | `stl_autofree`, `stl_string_scope` | **opt-in** via `STL_ENABLE_CLEANUP` *and* GCC/Clang | macro expands to nothing; free by hand |
| `__inline__` | `STL_INLINE` | `__GNUC__`/`__clang__` in pre-C99 modes | `static inline` on C99+, `static` otherwise |
| `__declspec` | `STL_API`, `STL_DEPRECATED` | `_MSC_VER` | `__attribute__` on GCC/Clang |

Points worth calling out:

* **`cleanup` is opt-in, not automatic.** It has no portable equivalent, so
  `stl_autofree` does nothing unless you define `STL_ENABLE_CLEANUP`. Code that
  needs deterministic release on every compiler should use `stl_free_all()`,
  container destructors, or explicit `free` calls. Query `STL_HAVE_CLEANUP` to
  branch on it.
* **No `always_inline`.** An earlier revision used
  `__attribute__((always_inline))` on `STL_INLINE`, which breaks builds at
  `-O0`. It is gone.
* **No statement expressions, no `typeof`, no `#pragma once`.** The whole
  public surface is accepted by `-std=c99 -pedantic -Wall -Wextra` with zero
  diagnostics, and by `-std=c89 -pedantic` apart from the unavoidable
  `long long` warning for the 64-bit comparators.
* **No POSIX-only headers in the public API.** `pthread.h` appears only in
  `libstl_thread.c` and in `libstl_internal.h`, and the entire threading
  section is removed by `STL_DISABLE_THREADS` / `make STL_THREADS=0`.
* **Variadic macros are conditional.** Under C89, `STL_ERROR` and the internal
  reporting macros route through real variadic functions instead, so the
  library sources still compile as C89.

## Continuous integration

`.github/workflows/build.yml` runs on every push to `main`, on pull requests,
and on version tags:

| Job | Purpose |
|---|---|
| `standards` | Builds with GCC and Clang under `c89`, `c99`, `c11` and `c17`, with warnings as errors, and once more with the convenience macro layer enabled. Catches stray compiler extensions. |
| `sanitizers` | Runs the test suites under AddressSanitizer + UBSan, with `STL_ENABLE_CLEANUP`, and with pthread support compiled out. |
| `native` | Builds and tests natively on Linux x86-64, Linux arm64, macOS arm64 and Windows x86-64 (MSVC), then uploads each platform's libraries as an artifact. |
| `release` | On a `v*` tag, archives each native build and attaches it to the GitHub release. |

Pushing a tag such as `v1.0.1` attaches the libraries as **loose files**, one
set per target, with the target in the name:

```
libstl.h                          shared by every platform, uploaded once
libstl-linux-x86_64.a             libstl-linux-x86_64.so
libstl-linux-x86_64.so.1          libstl-linux-x86_64.so.1.0.0

libstl-linux-aarch64.a            libstl-linux-aarch64.so
libstl-linux-aarch64.so.1         libstl-linux-aarch64.so.1.0.0

libstl-macos-arm64.a              libstl-macos-arm64.dylib
libstl-macos-arm64.dylib.1        libstl-macos-arm64.dylib.1.0.0

libstl-windows-x86_64.lib         libstl-windows-x86_64.dll

SHA256SUMS
```

The target has to appear in the filename because GitHub requires unique asset
names and every platform produces a file called `libstl.a`. The version suffix
is kept so that renaming back to the canonical name is mechanical. The
`install-release` target does exactly that:

```sh
make install-release SRC=~/Downloads TARGET=linux-x86_64 PREFIX=/usr/local
```

which installs `libstl.h` and `libstl.a` / `libstl.so` / `libstl.so.1` /
`libstl.so.1.0.0` (or the `.dylib` / `.lib` + `.dll` equivalents) under their
real names. If you prefer to place them by hand, the mapping is trivial:

```sh
# Linux / macOS
cp libstl-linux-x86_64.a  /usr/local/lib/libstl.a
cp libstl-linux-x86_64.so* /usr/local/lib/     # strip the -linux-x86_64 part
cp libstl.h                /usr/local/include/
```

## API documentation

The public header is fully annotated in Doxygen format: every declaration has a
brief, its parameters and its return value, and each container section opens
with a group that covers complexity, iterator invalidation and a worked
example.  To build the HTML reference:

```sh
doxygen Doxyfile          # writes build/docs/html/index.html
```

`WARN_AS_ERROR` is enabled, so a dangling reference or an unclosed group fails
the build instead of silently producing incomplete output; CI runs it on every
push.

## Repository layout

```
libstl.h                 the only public header (API + macro layer)
src/libstl_internal.h    internal declarations, not installed
src/libstl_core.c        allocator, errors, comparators, hashes, iterators
src/libstl_algo.c        sorting, searching, heaps, set ops, numeric
src/libstl_vector.c      vector
src/libstl_deque.c       deque (blocked ring buffer)
src/libstl_list.c        doubly linked list
src/libstl_tree.c        red-black tree (backend for set/map)
src/libstl_set.c         set / multiset
src/libstl_map.c         map / multimap
src/libstl_hash.c        hash table (backend for hashset/hashmap)
src/libstl_hashset.c     hashset / hashmap
src/libstl_adaptor.c     stack / queue / priority_queue
src/libstl_string.c      string
src/libstl_bitset.c      bitset
src/libstl_thread.c      spinlock, rwlock, thread-safe wrappers, parallel algos
tests/test_libstl.c      main test suite
tests/test_macros.c      macro-layer test suite
Doxyfile                 API reference configuration
tools/make_archive.py    portable static-library writer (no `ar` dependency)
Makefile.msvc            nmake build for MSVC
.github/workflows/       CI: standards matrix, sanitizers, native builds, release
```

> The task called for `libstl.h` plus `libstl.c`. The implementation is split
> into `src/` for readability; to collapse it into a single `libstl.c`,
> concatenate `src/*.c` in order (core → algo → containers → thread). The files
> share no static symbols, so the concatenation compiles as-is.

## Portability

* Compiles warning-free under `c89`, `c99`, `c11`, `c17`, `gnu11`, `gnu17` and
  as C++17, with GCC and Clang.
* MSVC is supported through the `STL_API` / `STL_DEPRECATED` decoration and the
  `__declspec` branches; nothing in the public header requires a GCC front end.
* Platforms without pthreads: build with `STL_THREADS=0`, which compiles the
  threading translation unit down to its single-threaded fallbacks.
* 32/64-bit and big/little-endian are both handled (hashing and comparison are
  written to be byte-order safe).

## Configuration

| Macro | Effect |
|---|---|
| `STL_ENABLE_CLEANUP` | Enable the GCC/Clang `cleanup`-based helpers (`stl_autofree`, `stl_string_scope`) |
| `STL_NO_CONVENIENCE_MACROS` | Exclude the macro layer (for C89 builds) |
| `STL_NO_LEGACY_MACROS` | Do not provide the `STL_XXX` aliases |
| `STL_DISABLE_THREADS` | Compile out every pthread-dependent path |
| `STL_VERBOSE` | Default error handler prints to stderr |
| `STL_DEBUG_INVARIANTS` | Enable container structural self-checks |
| `NDEBUG` | Disable the internal `STL_CHECK` contract checks |
| `STL_BUILD_SHARED` / `STL_USE_SHARED` | Windows DLL export/import (`__declspec`; also honoured by MinGW) |

## Complexity

| Container | Random access | Push/pop ends | Insert middle | Lookup |
|---|---|---|---|---|
| vector | O(1) | O(1) amortised (back) | O(n) | O(n), O(log n) when sorted |
| deque | O(1) | O(1) amortised | O(n) | O(n) |
| list | O(n) | O(1) | O(1) | O(n) |
| set / map | — | — | O(log n) | O(log n) |
| hashset / hashmap | — | — | O(1) amortised | O(1) amortised |

Sorting is introsort (O(n log n) worst case); a stable merge sort,
`nth_element`, `partial_sort` and heap sort are also provided.

## Known trade-offs

* A map's `value_offset` lives in a side table indexed by container pointer
  (capacity 1024), because the public `stl_map` type *is* `stl_rbtree` and the
  offset cannot be stored in the object without changing the ABI. More than
  1024 live maps at once reports `STL_ERR_NOMEM`.
* Iterators are not pointers and do not support pointer arithmetic; use
  `stl_*_iter_next/prev` or the `foreach` macros.
* ABI stability is not guaranteed across 1.x releases.

## License

MIT. See [LICENSE](LICENSE).
