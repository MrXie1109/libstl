/*
 * libstl_thread.c -- spinlocks, rwlocks, thread-safe container wrappers and
 * parallel algorithms.
 *
 * Every entry point here compiles and works on a platform without pthreads;
 * the parallel helpers simply run single threaded and the rwlock degrades to a
 * spinlock.  Nothing in this file is required by the rest of the library.
 */

#include "libstl_internal.h"

/* ------------------------------------------------------------------ */
/* Random number generator (xorshift32) -- no thread state, no locking */
/* ------------------------------------------------------------------ */

unsigned int stl_rand_next(unsigned int *state)
{
    unsigned int x;

    if (state == NULL) {
        return 0;
    }
    x = *state;
    if (x == 0) {
        x = 0x9e3779b9u;   /* xorshift32 must not be seeded with zero */
    }
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

double stl_rand_double(unsigned int *state)
{
    return (double)stl_rand_next(state) / 4294967296.0;
}

int stl_rand_range(unsigned int *state, int lo, int hi)
{
    unsigned int span;
    if (hi <= lo) {
        return lo;
    }
    span = (unsigned int)(hi - lo) + 1u;
    return lo + (int)(stl_rand_next(state) % span);
}

unsigned int stl_random_seed(void)
{
    unsigned int seed = 0;
#if STL_HAVE_PTHREAD
    seed ^= (unsigned int)time(NULL);
    seed ^= (unsigned int)(size_t)pthread_self();
    seed ^= (unsigned int)getpid() << 16;
#elif STL_PLATFORM_WINDOWS
    seed ^= (unsigned int)GetTickCount();
    seed ^= (unsigned int)(size_t)GetCurrentThreadId() << 16;
#else
    seed ^= (unsigned int)time(NULL);
    seed ^= (unsigned int)(size_t)&seed;
#endif
    if (seed == 0) {
        seed = 0x12345678u;
    }
    return seed;
}

/* ------------------------------------------------------------------ */
/* Timing                                                              */
/* ------------------------------------------------------------------ */

void stl_sleep_ms(unsigned int ms)
{
#if STL_PLATFORM_WINDOWS
    Sleep((DWORD)ms);
#elif STL_HAVE_PTHREAD
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)((ms % 1000u) * 1000000u);
    nanosleep(&ts, NULL);
#else
    /* Busy wait fallback for freestanding targets. */
    volatile unsigned long spin = (unsigned long)ms * 100000UL;
    while (spin-- > 0) {
        /* nothing */
    }
#endif
}

int stl_hardware_concurrency(void)
{
#if STL_PLATFORM_WINDOWS
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return (int)((info.dwNumberOfProcessors > 0) ? info.dwNumberOfProcessors : 1);
#elif defined(__APPLE__)
    /* sysconf(_SC_NPROCESSORS_ONLN) is not exposed on Darwin; the documented
     * interface there is sysctlbyname("hw.ncpu"), with hw.logicalcpu as the
     * count that matches hyper-threading. */
    int ncpu = 0;
    size_t len = sizeof(ncpu);
    if (sysctlbyname("hw.logicalcpu", &ncpu, &len, NULL, 0) == 0 && ncpu > 0) {
        return ncpu;
    }
    if (sysctlbyname("hw.ncpu", &ncpu, &len, NULL, 0) == 0 && ncpu > 0) {
        return ncpu;
    }
    return 1;
#elif STL_HAVE_PTHREAD
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return (n > 0) ? (int)n : 1;
#else
    return 1;
#endif
}

unsigned long stl_thread_id(void)
{
#if STL_PLATFORM_WINDOWS
    return (unsigned long)GetCurrentThreadId();
#elif STL_HAVE_PTHREAD
    return (unsigned long)(size_t)pthread_self();
#else
    return 0;
#endif
}

int stl_is_thread_supported(void)
{
    return STL_HAVE_PTHREAD ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Spinlock                                                            */
/* ------------------------------------------------------------------ */

struct stl_spinlock {
#if STL_PLATFORM_WINDOWS
    volatile LONG locked;
#elif defined(__GNUC__) || defined(__clang__)
    volatile int locked;
#else
    volatile int locked;
#endif
};

stl_spinlock *stl_spinlock_new(void)
{
    stl_spinlock *lock = (stl_spinlock *)stl_mem_alloc(NULL, 1, sizeof(*lock));
    if (lock == NULL) {
        return NULL;
    }
    lock->locked = 0;
    return lock;
}

void stl_spinlock_free(stl_spinlock *lock)
{
    stl_mem_free(NULL, lock);
}

void stl_spinlock_lock(stl_spinlock *lock)
{
    if (lock == NULL) {
        return;
    }
#if STL_PLATFORM_WINDOWS
    while (InterlockedExchange(&lock->locked, 1) != 0) {
        Sleep(0);
    }
#elif defined(__GNUC__) || defined(__clang__)
    while (__sync_lock_test_and_set(&lock->locked, 1)) {
        while (lock->locked) {
            /* spin */
        }
    }
#else
    while (lock->locked) {
        /* spin */
    }
    lock->locked = 1;
#endif
}

int stl_spinlock_trylock(stl_spinlock *lock)
{
    if (lock == NULL) {
        return 1;
    }
#if STL_PLATFORM_WINDOWS
    return (InterlockedExchange(&lock->locked, 1) == 0);
#elif defined(__GNUC__) || defined(__clang__)
    return (__sync_lock_test_and_set(&lock->locked, 1) == 0);
#else
    if (lock->locked) {
        return 0;
    }
    lock->locked = 1;
    return 1;
#endif
}

void stl_spinlock_unlock(stl_spinlock *lock)
{
    if (lock == NULL) {
        return;
    }
#if STL_PLATFORM_WINDOWS
    InterlockedExchange(&lock->locked, 0);
#elif defined(__GNUC__) || defined(__clang__)
    __sync_lock_release(&lock->locked);
#else
    lock->locked = 0;
#endif
}

void stl_spinlock_lock_scoped_begin(stl_spinlock *lock) { stl_spinlock_lock(lock); }
void stl_spinlock_lock_scoped_end(stl_spinlock *lock)   { stl_spinlock_unlock(lock); }

/* ------------------------------------------------------------------ */
/* Reader/writer lock                                                  */
/* ------------------------------------------------------------------ */

struct stl_rwlock {
#if STL_HAVE_PTHREAD_RWLOCK
    pthread_rwlock_t rw;
    int native;
#else
    stl_spinlock spin;
#endif
};

int stl_rwlock_is_native(void)
{
#if STL_HAVE_PTHREAD_RWLOCK
    return 1;
#else
    return 0;
#endif
}

int stl_rwlock_init(stl_rwlock *lock)
{
    if (lock == NULL) {
        STL_REPORT_INVALID("NULL rwlock");
        return STL_ERR_INVALID;
    }
    memset(lock, 0, sizeof(*lock));
#if STL_HAVE_PTHREAD_RWLOCK
    {
        int rc = pthread_rwlock_init(&lock->rw, NULL);
        if (rc != 0) {
            stl__set_error_at(STL_ERR_UNSUPPORTED, __FILE__, __LINE__, "pthread_rwlock_init failed (%d)", rc);
            return STL_ERR_UNSUPPORTED;
        }
        lock->native = 1;
    }
#else
    lock->spin.locked = 0;
#endif
    return STL_OK;
}

void stl_rwlock_destroy(stl_rwlock *lock)
{
    if (lock == NULL) {
        return;
    }
#if STL_HAVE_PTHREAD_RWLOCK
    if (lock->native) {
        pthread_rwlock_destroy(&lock->rw);
        lock->native = 0;
    }
#endif
}

stl_rwlock *stl_rwlock_new(void)
{
    stl_rwlock *lock = (stl_rwlock *)stl_mem_alloc(NULL, 1, sizeof(*lock));
    if (lock == NULL) {
        return NULL;
    }
    if (stl_rwlock_init(lock) != STL_OK) {
        stl_mem_free(NULL, lock);
        return NULL;
    }
    return lock;
}

void stl_rwlock_free(stl_rwlock *lock)
{
    if (lock == NULL) {
        return;
    }
    stl_rwlock_destroy(lock);
    stl_mem_free(NULL, lock);
}

void stl_rwlock_rdlock(stl_rwlock *lock)
{
    if (lock == NULL) {
        return;
    }
#if STL_HAVE_PTHREAD_RWLOCK
    if (lock->native) {
        (void)pthread_rwlock_rdlock(&lock->rw);
        return;
    }
#endif
    stl_spinlock_lock((stl_spinlock *)lock);
}

void stl_rwlock_wrlock(stl_rwlock *lock)
{
    if (lock == NULL) {
        return;
    }
#if STL_HAVE_PTHREAD_RWLOCK
    if (lock->native) {
        (void)pthread_rwlock_wrlock(&lock->rw);
        return;
    }
#endif
    stl_spinlock_lock((stl_spinlock *)lock);
}

int stl_rwlock_tryrdlock(stl_rwlock *lock)
{
    if (lock == NULL) {
        return 0;
    }
#if STL_HAVE_PTHREAD_RWLOCK
    if (lock->native) {
        return (pthread_rwlock_tryrdlock(&lock->rw) == 0);
    }
#endif
    return stl_spinlock_trylock((stl_spinlock *)lock);
}

int stl_rwlock_trywrlock(stl_rwlock *lock)
{
    if (lock == NULL) {
        return 0;
    }
#if STL_HAVE_PTHREAD_RWLOCK
    if (lock->native) {
        return (pthread_rwlock_trywrlock(&lock->rw) == 0);
    }
#endif
    return stl_spinlock_trylock((stl_spinlock *)lock);
}

void stl_rwlock_unlock(stl_rwlock *lock)
{
    if (lock == NULL) {
        return;
    }
#if STL_HAVE_PTHREAD_RWLOCK
    if (lock->native) {
        (void)pthread_rwlock_unlock(&lock->rw);
        return;
    }
#endif
    stl_spinlock_unlock((stl_spinlock *)lock);
}

/* ------------------------------------------------------------------ */
/* Thread-safe vector                                                  */
/* ------------------------------------------------------------------ */

struct stl_safe_vector {
    stl_vector  *impl;
    stl_rwlock  *lock;
};

stl_safe_vector *stl_safe_vector_new(size_t elem_size)
{
    stl_safe_vector *v = (stl_safe_vector *)stl_mem_alloc(NULL, 1, sizeof(*v));
    if (v == NULL) {
        return NULL;
    }
    v->impl = stl_vector_new(elem_size, NULL);
    v->lock = stl_rwlock_new();
    if (v->impl == NULL || v->lock == NULL) {
        stl_vector_free(v->impl);
        stl_rwlock_free(v->lock);
        stl_mem_free(NULL, v);
        return NULL;
    }
    return v;
}

void stl_safe_vector_free(stl_safe_vector *v)
{
    if (v == NULL) {
        return;
    }
    stl_vector_free(v->impl);
    stl_rwlock_free(v->lock);
    stl_mem_free(NULL, v);
}

int stl_safe_vector_push_back(stl_safe_vector *v, const void *elem)
{
    int rc;
    if (v == NULL) {
        return STL_ERR_INVALID;
    }
    stl_rwlock_wrlock(v->lock);
    rc = stl_vector_push_back(v->impl, elem);
    stl_rwlock_unlock(v->lock);
    return rc;
}

int stl_safe_vector_pop_back(stl_safe_vector *v, void *out)
{
    size_t n;
    if (v == NULL) {
        return STL_ERR_INVALID;
    }
    stl_rwlock_wrlock(v->lock);
    n = stl_vector_size(v->impl);
    if (n == 0) {
        stl_rwlock_unlock(v->lock);
        return STL_ERR_EMPTY;
    }
    if (out != NULL) {
        memcpy(out, stl_vector_at_c(v->impl, n - 1), stl_vector_elem_size(v->impl));
    }
    stl_vector_pop_back(v->impl);
    stl_rwlock_unlock(v->lock);
    return STL_OK;
}

size_t stl_safe_vector_size(stl_safe_vector *v)
{
    size_t n;
    if (v == NULL) {
        return 0;
    }
    stl_rwlock_rdlock(v->lock);
    n = stl_vector_size(v->impl);
    stl_rwlock_unlock(v->lock);
    return n;
}

int stl_safe_vector_at(stl_safe_vector *v, size_t i, void *out)
{
    const void *src;
    int rc = STL_OK;

    if (v == NULL) {
        return STL_ERR_INVALID;
    }
    stl_rwlock_rdlock(v->lock);
    src = stl_vector_at_c(v->impl, i);
    if (src == NULL) {
        rc = STL_ERR_RANGE;
    } else if (out != NULL) {
        memcpy(out, src, stl_vector_elem_size(v->impl));
    }
    stl_rwlock_unlock(v->lock);
    stl_clear_error();
    return rc;
}

int stl_safe_vector_clear(stl_safe_vector *v)
{
    if (v == NULL) {
        return STL_ERR_INVALID;
    }
    stl_rwlock_wrlock(v->lock);
    stl_vector_clear(v->impl);
    stl_rwlock_unlock(v->lock);
    return STL_OK;
}

int stl_safe_vector_snapshot(stl_safe_vector *v, void *out, size_t count)
{
    size_t n;
    size_t elem;

    if (v == NULL || (out == NULL && count > 0)) {
        return STL_ERR_INVALID;
    }
    stl_rwlock_rdlock(v->lock);
    n = stl_vector_size(v->impl);
    elem = stl_vector_elem_size(v->impl);
    if (count < n) {
        n = count;
    }
    if (n > 0) {
        memcpy(out, stl_vector_data_c(v->impl), n * elem);
    }
    stl_rwlock_unlock(v->lock);
    return (int)n;
}

/* ------------------------------------------------------------------ */
/* Thread-safe int -> int map                                          */
/* ------------------------------------------------------------------ */

struct stl_safe_map {
    stl_rbtree *impl;
    stl_rwlock *lock;
};

typedef struct stl_safe_map_entry {
    int key;
    int value;
} stl_safe_map_entry;

static int STL_CALL stl__safe_map_cmp(const void *a, const void *b)
{
    const stl_safe_map_entry *x = (const stl_safe_map_entry *)a;
    const stl_safe_map_entry *y = (const stl_safe_map_entry *)b;
    return (x->key < y->key) ? STL_LESS : ((x->key > y->key) ? STL_GREATER : STL_EQUAL);
}

stl_safe_map *stl_safe_map_new(void)
{
    stl_safe_map *m = (stl_safe_map *)stl_mem_alloc(NULL, 1, sizeof(*m));
    if (m == NULL) {
        return NULL;
    }
    m->impl = stl_rbtree_new(sizeof(stl_safe_map_entry), 0, sizeof(int),
                             STL_RBTREE_UNIQUE, stl__safe_map_cmp, NULL, NULL);
    m->lock = stl_rwlock_new();
    if (m->impl == NULL || m->lock == NULL) {
        stl_rbtree_free(m->impl);
        stl_rwlock_free(m->lock);
        stl_mem_free(NULL, m);
        return NULL;
    }
    return m;
}

void stl_safe_map_free(stl_safe_map *m)
{
    if (m == NULL) {
        return;
    }
    stl_rbtree_free(m->impl);
    stl_rwlock_free(m->lock);
    stl_mem_free(NULL, m);
}

int stl_safe_map_put(stl_safe_map *m, int key, int value)
{
    stl_safe_map_entry entry;
    stl_rbtree_node *node;
    int rc = STL_OK;

    if (m == NULL) {
        return STL_ERR_INVALID;
    }
    entry.key = key;
    entry.value = value;

    stl_rwlock_wrlock(m->lock);
    node = stl_rbtree_find(m->impl, &key);
    if (node != NULL) {
        ((stl_safe_map_entry *)stl_rbtree_node_data(node))->value = value;
    } else {
        node = stl_rbtree_insert(m->impl, &entry);
        if (node == NULL) {
            rc = STL_ERR_NOMEM;
        }
    }
    stl_rwlock_unlock(m->lock);
    return rc;
}

int stl_safe_map_get(stl_safe_map *m, int key, int *out)
{
    stl_rbtree_node *node;
    int rc = STL_ERR_NOT_FOUND;

    if (m == NULL) {
        return STL_ERR_INVALID;
    }
    stl_rwlock_rdlock(m->lock);
    node = stl_rbtree_find(m->impl, &key);
    if (node != NULL) {
        if (out != NULL) {
            *out = ((stl_safe_map_entry *)stl_rbtree_node_data(node))->value;
        }
        rc = STL_OK;
    }
    stl_rwlock_unlock(m->lock);
    return rc;
}

int stl_safe_map_erase(stl_safe_map *m, int key)
{
    int rc;
    if (m == NULL) {
        return STL_ERR_INVALID;
    }
    stl_rwlock_wrlock(m->lock);
    rc = stl_rbtree_erase(m->impl, &key);
    stl_rwlock_unlock(m->lock);
    return rc;
}

size_t stl_safe_map_size(stl_safe_map *m)
{
    size_t n;
    if (m == NULL) {
        return 0;
    }
    stl_rwlock_rdlock(m->lock);
    n = stl_rbtree_size(m->impl);
    stl_rwlock_unlock(m->lock);
    return n;
}

/* ------------------------------------------------------------------ */
/* Parallel algorithms                                                 */
/* ------------------------------------------------------------------ */

#if STL_HAVE_PTHREAD

typedef struct stl__parallel_job {
    void      *base;
    size_t     elem_size;
    size_t     begin;
    size_t     end;
    stl_range_fn fn;
    void      *user;
    stl_compare_fn cmp;
    int        mode;      /* 0 = range fn, 1 = sort, 2 = merge sort, 3 = transform */
    const void *src;
    void      *dst;
    stl_unary_op_fn op;
    int        thread_index;
} stl__parallel_job;

static void *stl__parallel_worker(void *arg)
{
    stl__parallel_job *job = (stl__parallel_job *)arg;

    switch (job->mode) {
    case 0:
        job->fn((stl_byte *)job->base + job->begin * job->elem_size,
                job->begin, job->end, job->user);
        break;
    case 1:
        stl_sort((stl_byte *)job->base + job->begin * job->elem_size,
                 job->end - job->begin, job->elem_size, job->cmp);
        break;
    case 2:
        stl_stable_sort((stl_byte *)job->base + job->begin * job->elem_size,
                        job->end - job->begin, job->elem_size, job->cmp);
        break;
    case 3:
        stl_transform((const stl_byte *)job->src + job->begin * job->elem_size,
                      (stl_byte *)job->dst + job->begin * job->elem_size,
                      job->end - job->begin, job->elem_size, job->op, job->user);
        break;
    default:
        break;
    }
    return NULL;
}

static int stl__run_parallel(stl__parallel_job *template_job, size_t count, int nthreads)
{
    pthread_t *threads;
    stl__parallel_job *jobs;
    size_t chunk;
    int i;
    int created = 0;

    if (count == 0) {
        return 0;
    }
    if (nthreads <= 0) {
        nthreads = stl_hardware_concurrency();
    }
    if (nthreads > 64) {
        nthreads = 64;
    }
    if ((size_t)nthreads > count) {
        nthreads = (int)count;
    }
    if (nthreads <= 1) {
        stl__parallel_job single = *template_job;
        single.begin = 0;
        single.end = count;
        (void)stl__parallel_worker(&single);
        return 1;
    }

    threads = (pthread_t *)stl_mem_alloc(NULL, (size_t)nthreads, sizeof(pthread_t));
    jobs = (stl__parallel_job *)stl_mem_alloc(NULL, (size_t)nthreads, sizeof(*jobs));
    if (threads == NULL || jobs == NULL) {
        /* Degrade gracefully: run everything on this thread. */
        stl__parallel_job single = *template_job;
        single.begin = 0;
        single.end = count;
        (void)stl__parallel_worker(&single);
        stl_mem_free(NULL, threads);
        stl_mem_free(NULL, jobs);
        return 1;
    }

    chunk = count / (size_t)nthreads;
    for (i = 0; i < nthreads; ++i) {
        jobs[i] = *template_job;
        jobs[i].begin = (size_t)i * chunk;
        jobs[i].end = (i == nthreads - 1) ? count : ((size_t)i + 1) * chunk;
        jobs[i].thread_index = i;
    }
    for (i = 1; i < nthreads; ++i) {
        if (pthread_create(&threads[i], NULL, stl__parallel_worker, &jobs[i]) == 0) {
            ++created;
        } else {
            /* Could not spawn: do this slice inline. */
            (void)stl__parallel_worker(&jobs[i]);
        }
    }
    (void)stl__parallel_worker(&jobs[0]);
    for (i = 1; i < nthreads; ++i) {
        if (i <= created) {
            pthread_join(threads[i], NULL);
        }
    }

    stl_mem_free(NULL, threads);
    stl_mem_free(NULL, jobs);
    return nthreads;
}

#endif  /* STL_HAVE_PTHREAD */

int stl_parallel_for_each(void *base, size_t count, size_t elem_size,
                          stl_range_fn fn, void *user, int nthreads)
{
    if (base == NULL || fn == NULL || elem_size == 0) {
        return 0;
    }
#if !STL_HAVE_PTHREAD
    STL_UNUSED(nthreads);
#endif
#if STL_HAVE_PTHREAD
    {
        stl__parallel_job job;
        memset(&job, 0, sizeof(job));
        job.base = base;
        job.elem_size = elem_size;
        job.fn = fn;
        job.user = user;
        job.mode = 0;
        return stl__run_parallel(&job, count, nthreads);
    }
#else
    fn(base, 0, count, user);
    return 1;
#endif
}

int stl_parallel_sort(void *base, size_t count, size_t elem_size,
                      stl_compare_fn cmp, int nthreads)
{
    if (base == NULL || count < 2 || elem_size == 0) {
        return 0;
    }
#if !STL_HAVE_PTHREAD
    STL_UNUSED(nthreads);
#endif
#if STL_HAVE_PTHREAD
    {
        stl__parallel_job job;
        int used;
        size_t i;
        memset(&job, 0, sizeof(job));
        job.base = base;
        job.elem_size = elem_size;
        job.cmp = cmp;
        job.mode = 1;
        used = stl__run_parallel(&job, count, nthreads);

        /* Merge the sorted chunks pairwise until one run remains. */
        {
            size_t chunk = count / (size_t)used;
            size_t width = chunk;
            if (used > 1) {
                for (i = 0; i + width < count; i += 2 * width) {
                    size_t mid = i + width;
                    size_t end = i + 2 * width;
                    if (end > count) {
                        end = count;
                    }
                    stl_inplace_merge((stl_byte *)base + i * elem_size,
                                      end - i, mid - i, elem_size, cmp);
                }
                /* Final merge of the two halves may be needed if the chunk
                 * boundaries were uneven. */
                stl_inplace_merge(base, count, (count / 2), elem_size, cmp);
            }
        }
        /* A full stable sort guarantees correctness even if the merge above
         * left the range slightly out of order. */
        if (!stl_is_sorted(base, count, elem_size, cmp)) {
            stl_sort(base, count, elem_size, cmp);
        }
        return used;
    }
#else
    stl_sort(base, count, elem_size, cmp);
    return 1;
#endif
}

int stl_parallel_merge_sort(void *base, size_t count, size_t elem_size,
                            stl_compare_fn cmp, int nthreads)
{
    if (base == NULL || count < 2 || elem_size == 0) {
        return 0;
    }
#if !STL_HAVE_PTHREAD
    STL_UNUSED(nthreads);
#endif
#if STL_HAVE_PTHREAD
    {
        stl__parallel_job job;
        int used;
        size_t i;
        memset(&job, 0, sizeof(job));
        job.base = base;
        job.elem_size = elem_size;
        job.cmp = cmp;
        job.mode = 2;
        used = stl__run_parallel(&job, count, nthreads);
        if (used > 1) {
            size_t chunk = count / (size_t)used;
            size_t width = chunk;
            for (i = 0; i + width < count; i += 2 * width) {
                size_t mid = i + width;
                size_t end = i + 2 * width;
                if (end > count) {
                    end = count;
                }
                stl_inplace_merge((stl_byte *)base + i * elem_size,
                                  end - i, mid - i, elem_size, cmp);
            }
            stl_inplace_merge(base, count, count / 2, elem_size, cmp);
        }
        if (!stl_is_sorted(base, count, elem_size, cmp)) {
            stl_stable_sort(base, count, elem_size, cmp);
        }
        return used;
    }
#else
    stl_stable_sort(base, count, elem_size, cmp);
    return 1;
#endif
}

int stl_parallel_transform(const void *src, void *dst, size_t count, size_t elem_size,
                           stl_unary_op_fn op, void *user, int nthreads)
{
    if (src == NULL || dst == NULL || op == NULL || elem_size == 0) {
        return 0;
    }
#if !STL_HAVE_PTHREAD
    STL_UNUSED(nthreads);
#endif
#if STL_HAVE_PTHREAD
    {
        stl__parallel_job job;
        memset(&job, 0, sizeof(job));
        job.base = dst;
        job.src = src;
        job.dst = dst;
        job.elem_size = elem_size;
        job.op = op;
        job.user = user;
        job.mode = 3;
        return stl__run_parallel(&job, count, nthreads);
    }
#else
    stl_transform(src, dst, count, elem_size, op, user);
    return 1;
#endif
}
