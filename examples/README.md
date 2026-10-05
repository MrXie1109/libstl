# libstl examples

Runnable programs that demonstrate the library a topic at a time. Each one
prints what it is doing as it goes, so the output doubles as a description.

## Running them

Build the library first, then the examples:

```sh
cd ..            && make          # produces build/libstl.a
cd examples      && make run      # builds and runs all six
```

`make` alone builds into `examples/build/` without running anything. The
examples link against `../build/libstl.a`, and the Makefile builds that for you
if it is missing.

```
make            build every example
make run        build, then run them in order
make 03         build just examples/03_algorithms.c
make asan       run everything under AddressSanitizer + UBSan
make cleanup    run the macro example with STL_ENABLE_CLEANUP
make clean      remove examples/build/
```

Any single example can also be compiled on its own:

```sh
cc -std=c11 -I.. -o demo 03_algorithms.c ../build/libstl.a -lpthread -lm
```

## The examples

| File | Covers |
|---|---|
| `01_containers.c` | vector, deque, list and string on the same task, and why the choice between the three sequence containers matters |
| `02_associative.c` | set, map and hashmap; the element layout a map expects, and how ordered and hashed lookup differ |
| `03_algorithms.c` | sorting and searching arrays and arrays of structs, set operations, folding, and elements that own memory |
| `04_adaptors.c` | stack, queue, priority queue and bitset, including the priority queue's ordering convention |
| `05_macros.c` | the function-style macro layer: typed access, iteration, comparators generated from a field name |
| `06_threads.c` | spinlock, rwlock, the thread-safe wrappers and the parallel algorithms |

## Where to start

`01_containers.c` is the gentlest introduction. `03_algorithms.c` is the one
worth reading if you are coming from C++ and want to know how the generic
algorithms cope without templates; it also shows the two things that catch
people out with struct elements, namely comparators for types with padding and
destructors for elements that own resources.

## Things the examples deliberately show

A few behaviours are surprising enough that they are worth seeing run:

- **`stl_vector_at_t(v, T, i)` returns NULL when `sizeof(T)` does not match the
  vector's element size** (05). The macro checks, so a wrong type fails loudly
  instead of reading past the element.

- **A priority queue is a max-heap by default** (04). To get the smallest
  element first, as the example's task queue does, the comparator has to
  reverse the natural order.

- **`stl_map_put` needs to know how big the value is** (02, 03). Pass
  `sizeof(V)` to the constructor when the value does not run to the end of the
  element; otherwise passing a pointer to a 4-byte value for a slot of 8 bytes
  would read past it.

- **`stl_list_erase_node` inside `stl_list_foreach_safe` is safe, but erasing
  from a vector while iterating is not** (05). The list macro caches the
  successor before the body runs; a vector would shift the tail underneath you.

- **`stl_string` is always NUL-terminated**, so `printf`, `strchr` and friends
  work on it directly (01). Length is tracked separately, so embedded NUL bytes
  survive the length-aware functions.

## A note on the macro layer

`05_macros.c` uses `STL_ENABLE_CLEANUP` for the scope-exit helpers, which rely
on a compiler extension with no portable equivalent. That build therefore uses
`-std=gnu11` rather than strict `c11`, and the file guards the corresponding
code with `#if STL_HAVE_CLEANUP` so it still compiles and runs without the
switch. Everything else in the examples uses only standard C11.
