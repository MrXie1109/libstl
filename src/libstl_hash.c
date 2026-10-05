/*
 * libstl_hash.c -- generic hash table backing hashset / hashmap.
 *
 * Separate chaining with power-of-two bucket counts, per-node payloads and
 * an intrusive doubly linked list threaded through the nodes so that
 * iteration is O(size) regardless of bucket count.
 */

#include "libstl_internal.h"

struct stl_hashtable_node {
    stl_hashtable_node *next;        /* next node in the same bucket      */
    stl_hashtable_node *bucket_prev; /* same-bucket predecessor (NULL at head) */
    stl_hashtable_node *order_prev;  /* global iteration order            */
    stl_hashtable_node *order_next;
    size_t              hash;
    /* element payload follows */
};

struct stl_hashtable {
    unsigned int          magic;
    stl_hashtable_node  **buckets;
    size_t                bucket_count;
    size_t                size;
    size_t                elem_size;
    size_t                node_offset;
    size_t                key_offset;
    size_t                key_size;
    stl_hashtable_policy  policy;
    stl_hash_fn           hash;
    stl_equal_fn          key_eq;
    stl_dtor_fn           elem_dtor;
    const stl_allocator  *alloc;
    double                max_load_factor;
    stl_hashtable_node   *order_head;
    stl_hashtable_node   *order_tail;
};

#define STL_HASH_MAGIC 0x48u /* 'H' */

static void *stl__ht_data(const stl_hashtable *h, stl_hashtable_node *node)
{
    return (void *)((stl_byte *)node + h->node_offset);
}

static const void *stl__ht_key(const stl_hashtable *h, const stl_hashtable_node *node)
{
    return (const void *)((const stl_byte *)node + h->node_offset + h->key_offset);
}

static size_t stl__ht_bucket_for(const stl_hashtable *h, size_t hash)
{
    return hash & (h->bucket_count - 1);
}

static size_t stl__ht_next_pow2(size_t n)
{
    size_t p = STL_HASH_MIN_BUCKETS;
    if (n <= p) {
        return p;
    }
    while (p < n) {
        size_t next = p << 1;
        if (next <= p) {
            return n;
        }
        p = next;
    }
    return p;
}

static void stl__ht_order_insert(stl_hashtable *h, stl_hashtable_node *node)
{
    node->order_next = NULL;
    node->order_prev = h->order_tail;
    if (h->order_tail != NULL) {
        h->order_tail->order_next = node;
    } else {
        h->order_head = node;
    }
    h->order_tail = node;
}

static void stl__ht_order_remove(stl_hashtable *h, stl_hashtable_node *node)
{
    if (node->order_prev != NULL) {
        node->order_prev->order_next = node->order_next;
    } else {
        h->order_head = node->order_next;
    }
    if (node->order_next != NULL) {
        node->order_next->order_prev = node->order_prev;
    } else {
        h->order_tail = node->order_prev;
    }
    node->order_prev = NULL;
    node->order_next = NULL;
}

static void stl__ht_bucket_link(stl_hashtable *h, size_t bucket, stl_hashtable_node *node)
{
    node->bucket_prev = NULL;
    node->next = h->buckets[bucket];
    if (node->next != NULL) {
        node->next->bucket_prev = node;
    }
    h->buckets[bucket] = node;
}

static void stl__ht_bucket_unlink(stl_hashtable *h, stl_hashtable_node *node)
{
    size_t bucket = stl__ht_bucket_for(h, node->hash);
    if (node->bucket_prev != NULL) {
        node->bucket_prev->next = node->next;
    } else {
        h->buckets[bucket] = node->next;
    }
    if (node->next != NULL) {
        node->next->bucket_prev = node->bucket_prev;
    }
    node->next = NULL;
    node->bucket_prev = NULL;
}

static stl_hashtable_node *stl__ht_new_node(stl_hashtable *h, const void *elem, size_t hash)
{
    stl_hashtable_node *node;
    size_t bytes = h->node_offset + h->elem_size;

    node = (stl_hashtable_node *)stl_mem_alloc(h->alloc, 1, bytes);
    if (node == NULL) {
        return NULL;
    }
    node->next = NULL;
    node->bucket_prev = NULL;
    node->order_prev = NULL;
    node->order_next = NULL;
    node->hash = hash;
    if (elem != NULL) {
        memcpy((stl_byte *)node + h->node_offset, elem, h->elem_size);
    } else {
        memset((stl_byte *)node + h->node_offset, 0, h->elem_size);
    }
    return node;
}

static void stl__ht_destroy_node(stl_hashtable *h, stl_hashtable_node *node)
{
    if (h->elem_dtor != NULL) {
        h->elem_dtor(stl__ht_data(h, node));
    }
    stl_mem_free(h->alloc, node);
}

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */

stl_hashtable *stl_hashtable_new(size_t elem_size, size_t key_offset, size_t key_size,
                                 stl_hashtable_policy policy, stl_hash_fn hash,
                                 stl_equal_fn key_eq, stl_dtor_fn elem_dtor,
                                 const stl_allocator *a)
{
    stl_hashtable *h;

    if (elem_size == 0) {
        STL_REPORT_INVALID("hashtable element size must be non-zero");
        return NULL;
    }
    if (key_size == 0) {
        key_size = elem_size;
    }
    if (key_offset + key_size > elem_size) {
        stl__set_error_at(STL_ERR_INVALID, __FILE__, __LINE__, "hash key range [%lu, %lu) exceeds element size %lu",
                  (unsigned long)key_offset, (unsigned long)(key_offset + key_size),
                  (unsigned long)elem_size);
        return NULL;
    }
    h = (stl_hashtable *)stl_mem_alloc(a, 1, sizeof(*h));
    if (h == NULL) {
        return NULL;
    }
    memset(h, 0, sizeof(*h));
    h->magic = STL_HASH_MAGIC;
    h->elem_size = elem_size;
    h->node_offset = sizeof(stl_hashtable_node);
    h->key_offset = key_offset;
    h->key_size = key_size;
    h->policy = policy;
    h->hash = (hash != NULL) ? hash : stl_hash_mem;
    h->key_eq = (key_eq != NULL) ? key_eq : stl_eq_mem;
    h->elem_dtor = elem_dtor;
    h->alloc = stl__allocator_or_default(a);
    h->max_load_factor = STL_HASH_MAX_LOAD;
    h->bucket_count = STL_HASH_MIN_BUCKETS;
    h->buckets = (stl_hashtable_node **)stl_mem_alloc(h->alloc, h->bucket_count,
                                                      sizeof(stl_hashtable_node *));
    if (h->buckets == NULL) {
        stl_mem_free(h->alloc, h);
        return NULL;
    }
    memset(h->buckets, 0, h->bucket_count * sizeof(stl_hashtable_node *));
    return h;
}

void stl_hashtable_free(stl_hashtable *h)
{
    if (h == NULL) {
        return;
    }
    STL_CHECK(h->magic == STL_HASH_MAGIC, STL_ERR_INVALID, "not a hashtable");
    stl_hashtable_clear_ex(h);
    stl_mem_free(h->alloc, h->buckets);
    h->magic = 0;
    stl_mem_free(h->alloc, h);
}

stl_hashtable *stl_hashtable_copy(const stl_hashtable *h, stl_copy_fn copy_elem)
{
    stl_hashtable *out;
    stl_hashtable_node *node;

    if (h == NULL) {
        return NULL;
    }
    out = stl_hashtable_new(h->elem_size, h->key_offset, h->key_size, h->policy,
                            h->hash, h->key_eq, h->elem_dtor, h->alloc);
    if (out == NULL) {
        return NULL;
    }
    for (node = h->order_head; node != NULL; node = node->order_next) {
        stl_hashtable_node *fresh = stl_hashtable_insert(out, stl__ht_data(h, node));
        if (fresh == NULL) {
            stl_hashtable_free(out);
            return NULL;
        }
        if (copy_elem != NULL &&
            !copy_elem(stl__ht_data(out, fresh), stl__ht_data(h, node), h->elem_size)) {
            stl_hashtable_free(out);
            return NULL;
        }
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* Resizing                                                            */
/* ------------------------------------------------------------------ */

int stl_hashtable_rehash(stl_hashtable *h, size_t buckets)
{
    stl_hashtable_node **fresh;
    size_t new_count;
    size_t i;
    stl_hashtable_node *node;

    if (h == NULL) {
        STL_REPORT_INVALID("NULL hashtable");
        return STL_ERR_INVALID;
    }
    new_count = stl__ht_next_pow2((buckets < STL_HASH_MIN_BUCKETS) ? STL_HASH_MIN_BUCKETS : buckets);
    if (new_count < STL_HASH_MIN_BUCKETS) {
        new_count = STL_HASH_MIN_BUCKETS;
    }
    fresh = (stl_hashtable_node **)stl_mem_alloc(h->alloc, new_count,
                                                 sizeof(stl_hashtable_node *));
    if (fresh == NULL) {
        return STL_ERR_NOMEM;
    }
    memset(fresh, 0, new_count * sizeof(stl_hashtable_node *));
    stl_mem_free(h->alloc, h->buckets);
    h->buckets = fresh;
    h->bucket_count = new_count;

    for (i = 0; i < new_count; ++i) {
        h->buckets[i] = NULL;
    }
    for (node = h->order_head; node != NULL; ) {
        stl_hashtable_node *next = node->order_next;
        node->next = NULL;
        node->bucket_prev = NULL;
        stl__ht_bucket_link(h, stl__ht_bucket_for(h, node->hash), node);
        node = next;
    }
    return STL_OK;
}

int stl_hashtable_reserve(stl_hashtable *h, size_t count)
{
    size_t needed;

    if (h == NULL) {
        STL_REPORT_INVALID("NULL hashtable");
        return STL_ERR_INVALID;
    }
    needed = (size_t)((double)count / h->max_load_factor) + 1;
    if (needed <= h->bucket_count) {
        return STL_OK;
    }
    return stl_hashtable_rehash(h, needed);
}

void stl_hashtable_set_max_load_factor(stl_hashtable *h, double lf)
{
    if (h == NULL) {
        return;
    }
    if (lf < 0.25) {
        lf = 0.25;
    }
    if (lf > 16.0) {
        lf = 16.0;
    }
    h->max_load_factor = lf;
    if ((double)h->size / (double)h->bucket_count > h->max_load_factor) {
        (void)stl_hashtable_rehash(h, (size_t)((double)h->size / h->max_load_factor) + 1);
    }
}

/* ------------------------------------------------------------------ */
/* Lookup                                                              */
/* ------------------------------------------------------------------ */

static stl_hashtable_node *stl__ht_find_node(const stl_hashtable *h, const void *key,
                                             size_t *bucket_out)
{
    size_t bucket;
    size_t hash;
    stl_hashtable_node *node;

    hash = h->hash(key);
    bucket = stl__ht_bucket_for(h, hash);
    if (bucket_out != NULL) {
        *bucket_out = bucket;
    }
    for (node = h->buckets[bucket]; node != NULL; node = node->next) {
        if (node->hash == hash && h->key_eq(stl__ht_key(h, node), key)) {
            return node;
        }
    }
    return NULL;
}

stl_hashtable_node *stl_hashtable_find(stl_hashtable *h, const void *key)
{
    if (h == NULL || key == NULL) {
        return NULL;
    }
    return stl__ht_find_node(h, key, NULL);
}

const stl_hashtable_node *stl_hashtable_find_c(const stl_hashtable *h, const void *key)
{
    if (h == NULL || key == NULL) {
        return NULL;
    }
    return stl__ht_find_node(h, key, NULL);
}

size_t stl_hashtable_count(const stl_hashtable *h, const void *key)
{
    size_t bucket;
    size_t hash;
    size_t n = 0;
    stl_hashtable_node *node;

    if (h == NULL || key == NULL) {
        return 0;
    }
    hash = h->hash(key);
    bucket = stl__ht_bucket_for(h, hash);
    for (node = h->buckets[bucket]; node != NULL; node = node->next) {
        if (node->hash == hash && h->key_eq(stl__ht_key(h, node), key)) {
            ++n;
        }
    }
    return n;
}

int stl_hashtable_contains(const stl_hashtable *h, const void *key)
{
    return stl_hashtable_find_c(h, key) != NULL;
}

/* ------------------------------------------------------------------ */
/* Insert / erase                                                      */
/* ------------------------------------------------------------------ */

stl_hashtable_node *stl_hashtable_insert(stl_hashtable *h, const void *elem)
{
    size_t hash;
    size_t bucket;
    stl_hashtable_node *node;

    if (h == NULL || elem == NULL) {
        STL_REPORT_INVALID("hashtable insert of NULL element");
        return NULL;
    }
    hash = h->hash((const stl_byte *)elem + h->key_offset);
    bucket = stl__ht_bucket_for(h, hash);

    if (h->policy == STL_HASHTABLE_UNIQUE) {
        for (node = h->buckets[bucket]; node != NULL; node = node->next) {
            if (node->hash == hash &&
                h->key_eq(stl__ht_key(h, node), (const stl_byte *)elem + h->key_offset)) {
                return node;
            }
        }
    }

    if ((double)(h->size + 1) / (double)h->bucket_count > h->max_load_factor) {
        if (stl_hashtable_rehash(h, h->bucket_count * 2) != STL_OK) {
            return NULL;
        }
        bucket = stl__ht_bucket_for(h, hash);
    }

    node = stl__ht_new_node(h, elem, hash);
    if (node == NULL) {
        return NULL;
    }
    stl__ht_bucket_link(h, bucket, node);
    stl__ht_order_insert(h, node);
    h->size += 1;
    return node;
}

void *stl_hashtable_emplace(stl_hashtable *h)
{
    /* Without a key we cannot compute a bucket, so emplace is unsupported for
     * the generic table; use insert() with a fully built element. */
    stl__set_error_at(STL_ERR_UNSUPPORTED, __FILE__, __LINE__, "hashtable emplace requires a key; use insert()");
    STL_UNUSED(h);
    return NULL;
}

int stl_hashtable_erase_node(stl_hashtable *h, stl_hashtable_node *node)
{
    if (h == NULL || node == NULL) {
        STL_REPORT_INVALID("hashtable erase of NULL node");
        return STL_ERR_INVALID;
    }
    stl__ht_bucket_unlink(h, node);
    stl__ht_order_remove(h, node);
    h->size -= 1;
    stl__ht_destroy_node(h, node);
    return STL_OK;
}

int stl_hashtable_erase(stl_hashtable *h, const void *key)
{
    stl_hashtable_node *node;

    if (h == NULL || key == NULL) {
        STL_REPORT_INVALID("hashtable erase with NULL key");
        return STL_ERR_INVALID;
    }
    node = stl__ht_find_node(h, key, NULL);
    if (node == NULL) {
        stl__set_error_at(STL_ERR_NOT_FOUND, __FILE__, __LINE__, "hashtable key not found");
        return STL_ERR_NOT_FOUND;
    }
    return stl_hashtable_erase_node(h, node);
}

void stl_hashtable_clear(stl_hashtable *h)
{
    stl_hashtable_node *node;

    if (h == NULL) {
        return;
    }
    for (node = h->order_head; node != NULL; ) {
        stl_hashtable_node *next = node->order_next;
        stl_mem_free(h->alloc, node);
        node = next;
    }
    h->order_head = NULL;
    h->order_tail = NULL;
    h->size = 0;
    if (h->buckets != NULL) {
        memset(h->buckets, 0, h->bucket_count * sizeof(stl_hashtable_node *));
    }
}

void stl_hashtable_clear_ex(stl_hashtable *h)
{
    stl_hashtable_node *node;

    if (h == NULL) {
        return;
    }
    for (node = h->order_head; node != NULL; ) {
        stl_hashtable_node *next = node->order_next;
        if (h->elem_dtor != NULL) {
            h->elem_dtor(stl__ht_data(h, node));
        }
        stl_mem_free(h->alloc, node);
        node = next;
    }
    h->order_head = NULL;
    h->order_tail = NULL;
    h->size = 0;
    if (h->buckets != NULL) {
        memset(h->buckets, 0, h->bucket_count * sizeof(stl_hashtable_node *));
    }
}

/* ------------------------------------------------------------------ */
/* Queries / iteration                                                 */
/* ------------------------------------------------------------------ */

size_t stl_hashtable_size(const stl_hashtable *h) { return (h != NULL) ? h->size : 0; }
size_t stl_hashtable_bucket_count(const stl_hashtable *h) { return (h != NULL) ? h->bucket_count : 0; }
int    stl_hashtable_empty(const stl_hashtable *h) { return (h == NULL) || (h->size == 0); }
double stl_hashtable_load_factor(const stl_hashtable *h)
{
    if (h == NULL || h->bucket_count == 0) {
        return 0.0;
    }
    return (double)h->size / (double)h->bucket_count;
}

size_t stl_hashtable_bucket_size(const stl_hashtable *h, size_t bucket)
{
    size_t n = 0;
    stl_hashtable_node *node;

    if (h == NULL || bucket >= h->bucket_count) {
        return 0;
    }
    for (node = h->buckets[bucket]; node != NULL; node = node->next) {
        ++n;
    }
    return n;
}

void *stl_hashtable_node_data(stl_hashtable_node *node)
{
    if (node == NULL) {
        return NULL;
    }
    return (void *)((stl_byte *)node + sizeof(stl_hashtable_node));
}

stl_hashtable_node *stl_hashtable_first(stl_hashtable *h)
{
    return (h != NULL) ? h->order_head : NULL;
}

stl_hashtable_node *stl_hashtable_next(stl_hashtable *h, stl_hashtable_node *node)
{
    STL_UNUSED(h);
    return (node != NULL) ? node->order_next : NULL;
}

stl_hashtable_node *stl_hashtable_bucket_head(stl_hashtable *h, size_t bucket)
{
    if (h == NULL || bucket >= h->bucket_count) {
        return NULL;
    }
    return h->buckets[bucket];
}

stl_hashtable_node *stl_hashtable_bucket_next(stl_hashtable_node *node)
{
    return (node != NULL) ? node->next : NULL;
}

void *stl_hashtable_find_if(const stl_hashtable *h,
                            int (STL_CALL *pred)(const void *, void *), void *user)
{
    stl_hashtable_node *node;

    if (h == NULL || pred == NULL) {
        return NULL;
    }
    for (node = h->order_head; node != NULL; node = node->order_next) {
        if (pred(stl__ht_data(h, node), user)) {
            return stl__ht_data(h, node);
        }
    }
    return NULL;
}

void stl_hashtable_foreach(stl_hashtable *h, stl_visit_fn fn, void *user)
{
    stl_hashtable_node *node;

    if (h == NULL || fn == NULL) {
        return;
    }
    for (node = h->order_head; node != NULL; ) {
        stl_hashtable_node *next = node->order_next;
        if (fn(stl__ht_data(h, node), user)) {
            break;
        }
        node = next;
    }
}

void stl_hashtable_foreach_c(const stl_hashtable *h, stl_visit_fn fn, void *user)
{
    stl_hashtable_foreach((stl_hashtable *)h, fn, user);
}

/* Accessors for the hashset/hashmap layer. */
STL_PRIVATE size_t stl__hashtable_node_offset(const stl_hashtable *h) { return (h != NULL) ? h->node_offset : 0; }
STL_PRIVATE stl_hashtable_node *stl__hashtable_node_from_data(const stl_hashtable *h, const void *data)
{
    return (stl_hashtable_node *)(void *)((stl_byte *)data - ((h != NULL) ? h->node_offset : 0));
}
STL_PRIVATE size_t stl__hashtable_key_offset(const stl_hashtable *h) { return (h != NULL) ? h->key_offset : 0; }
STL_PRIVATE size_t stl__hashtable_key_size(const stl_hashtable *h)   { return (h != NULL) ? h->key_size : 0; }
STL_PRIVATE size_t stl__hashtable_elem_size(const stl_hashtable *h)  { return (h != NULL) ? h->elem_size : 0; }
STL_PRIVATE stl_hash_fn stl__hashtable_hash(const stl_hashtable *h)  { return (h != NULL) ? h->hash : NULL; }
STL_PRIVATE stl_equal_fn stl__hashtable_eq(const stl_hashtable *h)   { return (h != NULL) ? h->key_eq : NULL; }
STL_PRIVATE stl_hashtable_policy stl__hashtable_policy(const stl_hashtable *h) { return (h != NULL) ? h->policy : STL_HASHTABLE_UNIQUE; }
STL_PRIVATE const stl_allocator *stl__hashtable_allocator(const stl_hashtable *h) { return (h != NULL) ? h->alloc : NULL; }
STL_PRIVATE stl_dtor_fn stl__hashtable_dtor(const stl_hashtable *h)  { return (h != NULL) ? h->elem_dtor : NULL; }

/* ------------------------------------------------------------------ */
/* Iterators                                                           */
/* ------------------------------------------------------------------ */

static size_t stl__ht_order_index(const stl_hashtable *h, const stl_hashtable_node *node)
{
    const stl_hashtable_node *cur;
    size_t i = 0;

    for (cur = h->order_head; cur != NULL && cur != node; cur = cur->order_next) {
        ++i;
    }
    return i;
}

stl_iterator stl_hashtable_begin(stl_hashtable *h)
{
    stl_iterator it;
    it.owner = h;
    it.elem = (h != NULL && h->order_head != NULL) ? stl__ht_data(h, h->order_head) : NULL;
    it.index = 0;
    return it;
}

stl_iterator stl_hashtable_end(stl_hashtable *h)
{
    stl_iterator it;
    it.owner = h;
    it.elem = NULL;
    it.index = (h != NULL) ? h->size : 0;
    return it;
}

stl_iterator stl_hashtable_iter_next(stl_iterator it)
{
    stl_hashtable *h = (stl_hashtable *)it.owner;
    stl_hashtable_node *node;

    if (h == NULL || it.elem == NULL) {
        return stl_hashtable_end(h);
    }
    node = (stl_hashtable_node *)(void *)((stl_byte *)it.elem - h->node_offset);
    node = node->order_next;
    if (node == NULL) {
        return stl_hashtable_end(h);
    }
    it.elem = stl__ht_data(h, node);
    it.index = stl__ht_order_index(h, node);
    return it;
}

stl_iterator stl_hashtable_iter_prev(stl_iterator it)
{
    stl_hashtable *h = (stl_hashtable *)it.owner;
    stl_hashtable_node *node;

    if (h == NULL) {
        return stl_iter_null();
    }
    if (it.elem == NULL) {
        if (h->order_tail == NULL) {
            return stl_hashtable_end(h);
        }
        it.elem = stl__ht_data(h, h->order_tail);
        it.index = stl__ht_order_index(h, h->order_tail);
        return it;
    }
    node = (stl_hashtable_node *)(void *)((stl_byte *)it.elem - h->node_offset);
    node = node->order_prev;
    if (node == NULL) {
        it.elem = NULL;
        it.index = (size_t)-1;
        return it;
    }
    it.elem = stl__ht_data(h, node);
    it.index = stl__ht_order_index(h, node);
    return it;
}
