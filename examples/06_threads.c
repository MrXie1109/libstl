/*
 * 06_threads.c -- locks, thread-safe wrappers and parallel algorithms.
 *
 * Everything here compiles and behaves correctly in a build without pthread
 * support; the parallel helpers then run their work on the calling thread
 * alone.  stl_is_thread_supported() reports which build this is.
 *
 * Build:
 *     cc -std=c11 -I.. -o 06_threads 06_threads.c ../build/libstl.a -lpthread -lm
 */

#include "libstl.h"

#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Locks                                                               */
/* ------------------------------------------------------------------ */

static void lock_demo(void)
{
    stl_spinlock *spin;
    stl_rwlock *rw;
    int counter = 0;
    int i;

    printf("locks\n");

    spin = stl_spinlock_new();
    if (spin != NULL) {
        /* A spinlock burns CPU while waiting, so it suits very short critical
         * sections.  trylock lets a caller back off instead of spinning. */
        stl_spinlock_lock(spin);
        counter += 1;
        printf("  spinlock held, trylock from the same thread says %d\n",
               stl_spinlock_trylock(spin));
        stl_spinlock_unlock(spin);

        stl_spinlock_lock(spin);
        counter += 1;
        stl_spinlock_unlock(spin);
        printf("  counter after two guarded increments: %d\n", counter);
        stl_spinlock_free(spin);
    }

    rw = stl_rwlock_new();
    if (rw != NULL) {
        printf("  rwlock is %s\n",
               stl_rwlock_is_native() ? "the platform's own"
                                      : "a spinlock fallback (no pthreads)");

        /* Many readers may hold it at once; a writer excludes everyone. */
        for (i = 0; i < 3; ++i) {
            stl_rwlock_rdlock(rw);
            stl_rwlock_unlock(rw);
        }
        stl_rwlock_wrlock(rw);
        stl_rwlock_unlock(rw);
        printf("  read and write locks both acquired and released\n");

        stl_rwlock_free(rw);
    }
}

/* ------------------------------------------------------------------ */
/* Thread-safe containers                                              */
/* ------------------------------------------------------------------ */

static void safe_container_demo(void)
{
    stl_safe_vector *v;
    stl_safe_map *m;
    int i;

    printf("\nthread-safe wrappers\n");

    v = stl_safe_vector_new(sizeof(int));
    if (v != NULL) {
        for (i = 0; i < 100; ++i) {
            stl_safe_vector_push_back(v, &i);
        }
        printf("  safe_vector holds %lu elements\n", (unsigned long)stl_safe_vector_size(v));

        /* Reading one element at a time would interleave with a concurrent
         * writer, so a snapshot copies several under a single lock. */
        {
            int snapshot[10];
            int copied = stl_safe_vector_snapshot(v, snapshot, 10);
            int sum = 0;
            for (i = 0; i < copied; ++i) {
                sum += snapshot[i];
            }
            printf("  snapshot of %d elements sums to %d\n", copied, sum);
        }

        {
            int last = 0;
            stl_safe_vector_pop_back(v, &last);
            printf("  popped %d, now %lu elements\n",
                   last, (unsigned long)stl_safe_vector_size(v));
        }
        stl_safe_vector_free(v);
    }

    m = stl_safe_map_new();
    if (m != NULL) {
        for (i = 0; i < 50; ++i) {
            stl_safe_map_put(m, i, i * i);
        }
        {
            int value = 0;
            if (stl_safe_map_get(m, 7, &value) == STL_OK) {
                printf("  safe_map[7] = %d, %lu entries\n",
                       value, (unsigned long)stl_safe_map_size(m));
            }
        }
        stl_safe_map_free(m);
    }
}

/* ------------------------------------------------------------------ */
/* Parallel algorithms                                                 */
/* ------------------------------------------------------------------ */

/* Invoked on one contiguous slice; `begin` and `end` are indices into the
 * whole array, so a slice callback can tell where it is. */
static void STL_CALL double_slice(void *base, size_t begin, size_t end, void *user)
{
    int *values = (int *)base;
    size_t i;

    (void)user;
    for (i = 0; i < end - begin; ++i) {
        values[i] *= 2;
    }
}

static void parallel_demo(void)
{
    int data[1000];
    int i;
    int threads;

    printf("\nparallel algorithms\n");
    printf("  hardware concurrency: %d, pthread support: %d\n",
           stl_hardware_concurrency(), stl_is_thread_supported());

    for (i = 0; i < 1000; ++i) {
        data[i] = 999 - i;                      /* reverse sorted */
    }

    threads = stl_parallel_sort(data, 1000, sizeof(int), stl_cmp_int32, 0);
    printf("  parallel_sort used %d thread(s), sorted: %d\n",
           threads, stl_is_sorted(data, 1000, sizeof(int), stl_cmp_int32));

    for (i = 0; i < 1000; ++i) {
        data[i] = i + 1;
    }
    threads = stl_parallel_for_each(data, 1000, sizeof(int), double_slice, NULL, 4);
    printf("  parallel_for_each used %d thread(s), data[0] = %d, data[999] = %d\n",
           threads, data[0], data[999]);

    /* Passing 0 lets the library size the pool itself. */
    {
        int dst[1000];
        threads = stl_parallel_transform(data, dst, 1000, sizeof(int),
                                         stl_op_negate_int, NULL, 0);
        printf("  parallel_transform used %d thread(s), dst[0] = %d\n", threads, dst[0]);
    }
}

/* ------------------------------------------------------------------ */
/* Random numbers                                                      */
/* ------------------------------------------------------------------ */

static void random_demo(void)
{
    unsigned int state = 42;
    int i;
    int in_range = 1;

    printf("\nrandom numbers\n");

    for (i = 0; i < 1000; ++i) {
        int value = stl_rand_range(&state, 1, 6);   /* a die roll */
        if (value < 1 || value > 6) {
            in_range = 0;
        }
    }
    printf("  1000 die rolls all in [1, 6]: %d\n", in_range);
    printf("  stl_rand_double(&state) = %.6f\n", stl_rand_double(&state));

    /* Shuffling uses the same generator and is reproducible for a given seed. */
    {
        int a[10];
        int b[10];
        for (i = 0; i < 10; ++i) {
            a[i] = i;
            b[i] = i;
        }
        stl_shuffle(a, 10, sizeof(int), 12345);
        stl_shuffle(b, 10, sizeof(int), 12345);
        printf("  shuffle with the same seed is reproducible: %d\n",
               stl_equal(a, b, 10, sizeof(int), stl_eq_int));

        stl_shuffle(a, 10, sizeof(int), 999);
        printf("  a different seed gives a different order: %d\n",
               !stl_equal(a, b, 10, sizeof(int), stl_eq_int));
    }
}

int main(void)
{
    printf("libstl %s -- threads\n\n", stl_version_string());

    lock_demo();
    safe_container_demo();
    parallel_demo();
    random_demo();

    return 0;
}
