/*
 * libstl_tree.c -- generic red-black tree.
 *
 * One implementation of the classic CLRS red-black tree with a sentinel
 * header node, used for both the ordered set and the ordered map.  Elements
 * are stored by value; the key is a byte range inside each element
 * (key_offset/key_size), so a map element of the form { K key; V value; } is
 * addressed with key_offset == 0.
 */

#include "libstl_internal.h"

typedef enum stl_rb_color {
    STL_RB_RED = 0,
    STL_RB_BLACK = 1
} stl_rb_color;

struct stl_rbtree_node {
    stl_rbtree_node *parent;
    stl_rbtree_node *left;
    stl_rbtree_node *right;
    unsigned char    color;
    /* element payload follows */
};

struct stl_rbtree {
    unsigned int       magic;
    stl_rbtree_node   *root;
    stl_rbtree_node    header;        /* sentinel; header.parent = root      */
    size_t             size;
    size_t             elem_size;
    size_t             node_offset;   /* sizeof(stl_rbtree_node)             */
    size_t             key_offset;
    size_t             key_size;
    stl_rbtree_policy  policy;
    stl_compare_fn     key_cmp;
    stl_dtor_fn        elem_dtor;
    const stl_allocator *alloc;
};

#define STL_RB_MAGIC 0x52u /* 'R' */

/* ------------------------------------------------------------------ */
/* Node helpers                                                        */
/* ------------------------------------------------------------------ */

static void *stl__rb_data(const stl_rbtree *t, stl_rbtree_node *node)
{
    return (void *)((stl_byte *)node + t->node_offset);
}

static const void *stl__rb_key(const stl_rbtree *t, const stl_rbtree_node *node)
{
    return (const void *)((const stl_byte *)node + t->node_offset + t->key_offset);
}

void *stl_rbtree_node_data(stl_rbtree_node *node)
{
    /* Without the tree we cannot know the offset; the offset is a fixed
     * property of the allocator layout, so callers should prefer the typed
     * set/map accessors.  We expose the generic helper for symmetry and
     * document that the payload starts right after the header. */
    if (node == NULL) {
        return NULL;
    }
    return (void *)((stl_byte *)node + sizeof(stl_rbtree_node));
}

static stl_rbtree_node *stl__rb_new_node(stl_rbtree *t, const void *elem)
{
    stl_rbtree_node *node;
    size_t bytes = t->node_offset + t->elem_size;

    node = (stl_rbtree_node *)stl_mem_alloc(t->alloc, 1, bytes);
    if (node == NULL) {
        return NULL;
    }
    node->parent = NULL;
    node->left = NULL;
    node->right = NULL;
    node->color = STL_RB_RED;
    if (elem != NULL) {
        memcpy((stl_byte *)node + t->node_offset, elem, t->elem_size);
    } else {
        memset((stl_byte *)node + t->node_offset, 0, t->elem_size);
    }
    return node;
}

static void stl__rb_destroy_node(stl_rbtree *t, stl_rbtree_node *node)
{
    if (t->elem_dtor != NULL) {
        t->elem_dtor(stl__rb_data(t, node));
    }
    stl_mem_free(t->alloc, node);
}

#define STL_RB_IS_RED(t, n)  ((n) != NULL && (n)->color == STL_RB_RED)
#define STL_RB_IS_BLACK(t, n) ((n) == NULL || (n)->color == STL_RB_BLACK)

static stl_rbtree_node *stl__rb_min(stl_rbtree_node *n)
{
    while (n != NULL && n->left != NULL) {
        n = n->left;
    }
    return n;
}

static stl_rbtree_node *stl__rb_max(stl_rbtree_node *n)
{
    while (n != NULL && n->right != NULL) {
        n = n->right;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* Rotations and rebalancing                                           */
/* ------------------------------------------------------------------ */

static void stl__rb_rotate_left(stl_rbtree *t, stl_rbtree_node *x)
{
    stl_rbtree_node *y = x->right;

    x->right = y->left;
    if (y->left != NULL) {
        y->left->parent = x;
    }
    y->parent = x->parent;
    if (x->parent == NULL) {
        t->root = y;
    } else if (x == x->parent->left) {
        x->parent->left = y;
    } else {
        x->parent->right = y;
    }
    y->left = x;
    x->parent = y;
}

static void stl__rb_rotate_right(stl_rbtree *t, stl_rbtree_node *x)
{
    stl_rbtree_node *y = x->left;

    x->left = y->right;
    if (y->right != NULL) {
        y->right->parent = x;
    }
    y->parent = x->parent;
    if (x->parent == NULL) {
        t->root = y;
    } else if (x == x->parent->right) {
        x->parent->right = y;
    } else {
        x->parent->left = y;
    }
    y->right = x;
    x->parent = y;
}

static void stl__rb_insert_fixup(stl_rbtree *t, stl_rbtree_node *z)
{
    while (z->parent != NULL && z->parent->color == STL_RB_RED) {
        stl_rbtree_node *parent = z->parent;
        stl_rbtree_node *grand = parent->parent;

        if (grand == NULL) {
            break;
        }
        if (parent == grand->left) {
            stl_rbtree_node *uncle = grand->right;
            if (STL_RB_IS_RED(t, uncle)) {
                parent->color = STL_RB_BLACK;
                uncle->color = STL_RB_BLACK;
                grand->color = STL_RB_RED;
                z = grand;
            } else {
                if (z == parent->right) {
                    z = parent;
                    stl__rb_rotate_left(t, z);
                    parent = z->parent;
                    grand = parent->parent;
                }
                parent->color = STL_RB_BLACK;
                grand->color = STL_RB_RED;
                stl__rb_rotate_right(t, grand);
            }
        } else {
            stl_rbtree_node *uncle = grand->left;
            if (STL_RB_IS_RED(t, uncle)) {
                parent->color = STL_RB_BLACK;
                uncle->color = STL_RB_BLACK;
                grand->color = STL_RB_RED;
                z = grand;
            } else {
                if (z == parent->left) {
                    z = parent;
                    stl__rb_rotate_right(t, z);
                    parent = z->parent;
                    grand = parent->parent;
                }
                parent->color = STL_RB_BLACK;
                grand->color = STL_RB_RED;
                stl__rb_rotate_left(t, grand);
            }
        }
    }
    t->root->color = STL_RB_BLACK;
}

/* Replace subtree rooted at u with subtree rooted at v. */
static void stl__rb_transplant(stl_rbtree *t, stl_rbtree_node *u, stl_rbtree_node *v)
{
    if (u->parent == NULL) {
        t->root = v;
    } else if (u == u->parent->left) {
        u->parent->left = v;
    } else {
        u->parent->right = v;
    }
    if (v != NULL) {
        v->parent = u->parent;
    }
}

static void stl__rb_delete_fixup(stl_rbtree *t, stl_rbtree_node *x, stl_rbtree_node *parent)
{
    while (x != t->root && STL_RB_IS_BLACK(t, x)) {
        if (parent == NULL) {
            break;
        }
        if (x == parent->left) {
            stl_rbtree_node *sibling = parent->right;
            if (STL_RB_IS_RED(t, sibling)) {
                sibling->color = STL_RB_BLACK;
                parent->color = STL_RB_RED;
                stl__rb_rotate_left(t, parent);
                sibling = parent->right;
            }
            if (sibling == NULL) {
                x = parent;
                parent = x->parent;
                continue;
            }
            if (STL_RB_IS_BLACK(t, sibling->left) && STL_RB_IS_BLACK(t, sibling->right)) {
                sibling->color = STL_RB_RED;
                x = parent;
                parent = x->parent;
            } else {
                if (STL_RB_IS_BLACK(t, sibling->right)) {
                    if (sibling->left != NULL) {
                        sibling->left->color = STL_RB_BLACK;
                    }
                    sibling->color = STL_RB_RED;
                    stl__rb_rotate_right(t, sibling);
                    sibling = parent->right;
                }
                sibling->color = parent->color;
                parent->color = STL_RB_BLACK;
                if (sibling->right != NULL) {
                    sibling->right->color = STL_RB_BLACK;
                }
                stl__rb_rotate_left(t, parent);
                x = t->root;
                parent = NULL;
            }
        } else {
            stl_rbtree_node *sibling = parent->left;
            if (STL_RB_IS_RED(t, sibling)) {
                sibling->color = STL_RB_BLACK;
                parent->color = STL_RB_RED;
                stl__rb_rotate_right(t, parent);
                sibling = parent->left;
            }
            if (sibling == NULL) {
                x = parent;
                parent = x->parent;
                continue;
            }
            if (STL_RB_IS_BLACK(t, sibling->right) && STL_RB_IS_BLACK(t, sibling->left)) {
                sibling->color = STL_RB_RED;
                x = parent;
                parent = x->parent;
            } else {
                if (STL_RB_IS_BLACK(t, sibling->left)) {
                    if (sibling->right != NULL) {
                        sibling->right->color = STL_RB_BLACK;
                    }
                    sibling->color = STL_RB_RED;
                    stl__rb_rotate_left(t, sibling);
                    sibling = parent->left;
                }
                sibling->color = parent->color;
                parent->color = STL_RB_BLACK;
                if (sibling->left != NULL) {
                    sibling->left->color = STL_RB_BLACK;
                }
                stl__rb_rotate_right(t, parent);
                x = t->root;
                parent = NULL;
            }
        }
    }
    if (x != NULL) {
        x->color = STL_RB_BLACK;
    }
}

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */

stl_rbtree *stl_rbtree_new(size_t elem_size, size_t key_offset, size_t key_size,
                           stl_rbtree_policy policy, stl_compare_fn key_cmp,
                           stl_dtor_fn elem_dtor, const stl_allocator *a)
{
    stl_rbtree *t;

    if (elem_size == 0) {
        STL_REPORT_INVALID("rbtree element size must be non-zero");
        return NULL;
    }
    if (key_size == 0) {
        key_size = elem_size;
    }
    if (key_offset + key_size > elem_size) {
        stl__set_error_at(STL_ERR_INVALID, __FILE__, __LINE__, "key range [%lu, %lu) exceeds element size %lu",
                  (unsigned long)key_offset, (unsigned long)(key_offset + key_size),
                  (unsigned long)elem_size);
        return NULL;
    }
    t = (stl_rbtree *)stl_mem_alloc(a, 1, sizeof(*t));
    if (t == NULL) {
        return NULL;
    }
    memset(t, 0, sizeof(*t));
    t->magic = STL_RB_MAGIC;
    t->elem_size = elem_size;
    t->node_offset = sizeof(stl_rbtree_node);
    t->key_offset = key_offset;
    t->key_size = key_size;
    t->policy = policy;
    t->key_cmp = (key_cmp != NULL) ? key_cmp : stl_cmp_mem;
    t->elem_dtor = elem_dtor;
    t->alloc = stl__allocator_or_default(a);
    t->header.parent = NULL;
    t->header.left = &t->header;
    t->header.right = &t->header;
    t->header.color = STL_RB_RED;   /* convention: header is never black */
    t->root = NULL;
    return t;
}

void stl_rbtree_free(stl_rbtree *t)
{
    if (t == NULL) {
        return;
    }
    STL_CHECK(t->magic == STL_RB_MAGIC, STL_ERR_INVALID, "not an rbtree");
    stl_rbtree_clear_ex(t);
    t->magic = 0;
    stl_mem_free(t->alloc, t);
}

stl_rbtree *stl_rbtree_copy(const stl_rbtree *t, stl_copy_fn copy_elem)
{
    stl_rbtree *out;
    const stl_rbtree_node *node;

    if (t == NULL) {
        return NULL;
    }
    out = stl_rbtree_new(t->elem_size, t->key_offset, t->key_size, t->policy,
                         t->key_cmp, t->elem_dtor, t->alloc);
    if (out == NULL) {
        return NULL;
    }
    for (node = stl_rbtree_first((stl_rbtree *)t); node != NULL;
         node = stl_rbtree_next((stl_rbtree_node *)node)) {
        stl_rbtree_node *fresh = stl__rb_new_node(out, stl__rb_data(t, (stl_rbtree_node *)node));
        if (fresh == NULL) {
            stl_rbtree_free(out);
            return NULL;
        }
        /* Insert as a leaf in-order: keeps the tree valid with no rotations
         * needed since we insert in sorted order (worst-case shape, but copy
         * is a rare operation and correctness matters more). */
        {
            stl_rbtree_node *parent = NULL;
            stl_rbtree_node *cur = out->root;
            while (cur != NULL) {
                parent = cur;
                cur = (out->key_cmp(stl__rb_key(out, fresh), stl__rb_key(out, cur)) < 0)
                    ? cur->left : cur->right;
            }
            fresh->parent = parent;
            if (parent == NULL) {
                out->root = fresh;
            } else if (out->key_cmp(stl__rb_key(out, fresh), stl__rb_key(out, parent)) < 0) {
                parent->left = fresh;
            } else {
                parent->right = fresh;
            }
            fresh->color = STL_RB_RED;
            out->size += 1;
            stl__rb_insert_fixup(out, fresh);
        }
        if (copy_elem != NULL) {
            if (!copy_elem(stl__rb_data(out, fresh),
                           stl__rb_data(t, (stl_rbtree_node *)node), t->elem_size)) {
                stl_rbtree_free(out);
                return NULL;
            }
        }
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* Queries                                                             */
/* ------------------------------------------------------------------ */

size_t stl_rbtree_size(const stl_rbtree *t)      { return (t != NULL) ? t->size : 0; }
int    stl_rbtree_empty(const stl_rbtree *t)     { return (t == NULL) || (t->size == 0); }
size_t stl_rbtree_elem_size(const stl_rbtree *t) { return (t != NULL) ? t->elem_size : 0; }
size_t stl_rbtree_key_size(const stl_rbtree *t)  { return (t != NULL) ? t->key_size : 0; }

/* Accessors used by the set/map layers. */
STL_PRIVATE size_t stl__rbtree_node_offset(const stl_rbtree *t) { return (t != NULL) ? t->node_offset : 0; }
STL_PRIVATE stl_rbtree_node *stl__rbtree_node_from_data(const stl_rbtree *t, const void *data)
{
    return (stl_rbtree_node *)(void *)((stl_byte *)data - ((t != NULL) ? t->node_offset : 0));
}
STL_PRIVATE void *stl__rbtree_data_of(const stl_rbtree *t, stl_rbtree_node *node)
{
    return (node != NULL) ? stl__rb_data(t, node) : NULL;
}
STL_PRIVATE size_t stl__rbtree_key_offset(const stl_rbtree *t) { return (t != NULL) ? t->key_offset : 0; }
STL_PRIVATE stl_compare_fn stl__rbtree_key_cmp(const stl_rbtree *t) { return (t != NULL) ? t->key_cmp : NULL; }
STL_PRIVATE stl_rbtree_policy stl__rbtree_policy(const stl_rbtree *t) { return (t != NULL) ? t->policy : STL_RBTREE_UNIQUE; }
STL_PRIVATE const stl_allocator *stl__rbtree_allocator(const stl_rbtree *t) { return (t != NULL) ? t->alloc : NULL; }
STL_PRIVATE stl_dtor_fn stl__rbtree_dtor(const stl_rbtree *t) { return (t != NULL) ? t->elem_dtor : NULL; }

stl_rbtree_node *stl_rbtree_first(stl_rbtree *t)
{
    if (t == NULL || t->root == NULL) {
        return NULL;
    }
    return stl__rb_min(t->root);
}

stl_rbtree_node *stl_rbtree_last(stl_rbtree *t)
{
    if (t == NULL || t->root == NULL) {
        return NULL;
    }
    return stl__rb_max(t->root);
}

stl_rbtree_node *stl_rbtree_next(stl_rbtree_node *node)
{
    stl_rbtree_node *n = node;

    if (n == NULL || n->right == NULL) {
        if (n == NULL) {
            return NULL;
        }
        /* Walk up while we are the right child; the sentinel header has its
         * right link pointing at itself, terminating the walk. */
        if (n->right == n) {
            return n->left;   /* header: first element */
        }
    }
    if (n->right != NULL) {
        return stl__rb_min(n->right);
    }
    {
        stl_rbtree_node *parent = n->parent;
        while (parent != NULL && n == parent->right) {
            n = parent;
            parent = parent->parent;
        }
        return parent;
    }
}

stl_rbtree_node *stl_rbtree_prev(stl_rbtree_node *node)
{
    stl_rbtree_node *n = node;

    if (n == NULL) {
        return NULL;
    }
    if (n->left != NULL) {
        return stl__rb_max(n->left);
    }
    {
        stl_rbtree_node *parent = n->parent;
        while (parent != NULL && n == parent->left) {
            n = parent;
            parent = parent->parent;
        }
        return parent;
    }
}

/* ------------------------------------------------------------------ */
/* Lookup                                                              */
/* ------------------------------------------------------------------ */

static stl_rbtree_node *stl__rb_find_node(const stl_rbtree *t, const void *key)
{
    stl_rbtree_node *cur = t->root;

    if (t->key_offset == 0 && t->key_size == t->elem_size) {
        while (cur != NULL) {
            int c = t->key_cmp(key, stl__rb_key(t, cur));
            if (c == 0) {
                return cur;
            }
            cur = (c < 0) ? cur->left : cur->right;
        }
    } else {
        while (cur != NULL) {
            int c = t->key_cmp(key, stl__rb_key(t, cur));
            if (c == 0) {
                return cur;
            }
            cur = (c < 0) ? cur->left : cur->right;
        }
    }
    return NULL;
}

stl_rbtree_node *stl_rbtree_find(stl_rbtree *t, const void *key)
{
    if (t == NULL || key == NULL) {
        return NULL;
    }
    return stl__rb_find_node(t, key);
}

const stl_rbtree_node *stl_rbtree_find_c(const stl_rbtree *t, const void *key)
{
    if (t == NULL || key == NULL) {
        return NULL;
    }
    return stl__rb_find_node(t, key);
}

size_t stl_rbtree_count(const stl_rbtree *t, const void *key)
{
    stl_rbtree_node *first;
    size_t n = 0;

    if (t == NULL || key == NULL) {
        return 0;
    }
    first = stl__rb_find_node(t, key);
    if (first == NULL) {
        return 0;
    }
    if (t->policy == STL_RBTREE_UNIQUE) {
        return 1;
    }
    /* Walk to the leftmost equal key, then count rightwards. */
    while (first->left != NULL && t->key_cmp(key, stl__rb_key(t, first->left)) == 0) {
        first = first->left;
    }
    while (first != NULL && t->key_cmp(key, stl__rb_key(t, first)) == 0) {
        ++n;
        first = stl_rbtree_next(first);
    }
    return n;
}

int stl_rbtree_contains(const stl_rbtree *t, const void *key)
{
    return stl_rbtree_find_c(t, key) != NULL;
}

stl_rbtree_node *stl_rbtree_lower_bound(stl_rbtree *t, const void *key)
{
    stl_rbtree_node *cur;
    stl_rbtree_node *best = NULL;

    if (t == NULL || key == NULL) {
        return NULL;
    }
    cur = t->root;
    while (cur != NULL) {
        int c = t->key_cmp(stl__rb_key(t, cur), key);
        if (c < 0) {
            cur = cur->right;
        } else {
            best = cur;
            cur = cur->left;
        }
    }
    return best;
}

stl_rbtree_node *stl_rbtree_upper_bound(stl_rbtree *t, const void *key)
{
    stl_rbtree_node *cur;
    stl_rbtree_node *best = NULL;

    if (t == NULL || key == NULL) {
        return NULL;
    }
    cur = t->root;
    while (cur != NULL) {
        int c = t->key_cmp(key, stl__rb_key(t, cur));
        if (c < 0) {
            best = cur;
            cur = cur->left;
        } else {
            cur = cur->right;
        }
    }
    return best;
}

void stl_rbtree_equal_range(stl_rbtree *t, const void *key,
                            stl_rbtree_node **first, stl_rbtree_node **last)
{
    if (first != NULL) {
        *first = stl_rbtree_lower_bound(t, key);
    }
    if (last != NULL) {
        *last = stl_rbtree_upper_bound(t, key);
    }
}

int stl_rbtree_node_is_end(const stl_rbtree *t, const stl_rbtree_node *node)
{
    return (node == NULL) || (node == &t->header);
}

/* ------------------------------------------------------------------ */
/* Insertion                                                           */
/* ------------------------------------------------------------------ */

static stl_rbtree_node *stl__rb_insert_internal(stl_rbtree *t, const void *elem,
                                                int allow_overwrite)
{
    stl_rbtree_node *parent = NULL;
    stl_rbtree_node *cur = t->root;
    stl_rbtree_node *node;
    const void *key;
    int c = 0;

    if (t == NULL || elem == NULL) {
        STL_REPORT_INVALID("rbtree insert of NULL element");
        return NULL;
    }
    key = (const stl_byte *)elem + t->key_offset;

    while (cur != NULL) {
        parent = cur;
        c = t->key_cmp(key, stl__rb_key(t, cur));
        if (c == 0 && t->policy == STL_RBTREE_UNIQUE) {
            if (allow_overwrite) {
                memcpy(stl__rb_data(t, cur), elem, t->elem_size);
            }
            return cur;
        }
        cur = (c < 0) ? cur->left : cur->right;
    }

    node = stl__rb_new_node(t, elem);
    if (node == NULL) {
        return NULL;
    }
    node->parent = parent;
    if (parent == NULL) {
        t->root = node;
    } else {
        if (t->key_cmp(key, stl__rb_key(t, parent)) < 0) {
            parent->left = node;
        } else {
            parent->right = node;
        }
    }
    node->left = NULL;
    node->right = NULL;
    node->color = STL_RB_RED;
    t->size += 1;
    stl__rb_insert_fixup(t, node);
    return node;
}

stl_rbtree_node *stl_rbtree_insert(stl_rbtree *t, const void *elem)
{
    return stl__rb_insert_internal(t, elem, 0);
}

stl_rbtree_node *stl_rbtree_insert_hint(stl_rbtree *t, stl_rbtree_node *hint, const void *elem)
{
    /* The hint is accepted for API compatibility; a faster path would compare
     * against it first, but correctness never depends on it. */
    STL_UNUSED(hint);
    return stl__rb_insert_internal(t, elem, 0);
}

void *stl_rbtree_emplace(stl_rbtree *t)
{
    /* Emplacing needs a unique key we do not have yet, so the caller must fill
     * the returned slot and then call stl_rbtree_reinsert_emplace().  We keep
     * the simple contract: reject and report, since silently inserting a
     * zeroed duplicate key would corrupt ordering. */
    stl__set_error_at(STL_ERR_UNSUPPORTED, __FILE__, __LINE__, "rbtree emplace requires a key; use insert() or the set/map helpers");
    STL_UNUSED(t);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Deletion                                                            */
/* ------------------------------------------------------------------ */

int stl_rbtree_erase_node(stl_rbtree *t, stl_rbtree_node *z)
{
    stl_rbtree_node *y;
    stl_rbtree_node *x;
    stl_rbtree_node *x_parent;
    unsigned char y_original_color;

    if (t == NULL || z == NULL) {
        STL_REPORT_INVALID("rbtree erase of NULL node");
        return STL_ERR_INVALID;
    }

    y = z;
    y_original_color = y->color;

    if (z->left == NULL) {
        x = z->right;
        x_parent = z->parent;
        stl__rb_transplant(t, z, z->right);
    } else if (z->right == NULL) {
        x = z->left;
        x_parent = z->parent;
        stl__rb_transplant(t, z, z->left);
    } else {
        y = stl__rb_min(z->right);
        y_original_color = y->color;
        x = y->right;
        if (y->parent == z) {
            x_parent = y;
        } else {
            x_parent = y->parent;
            stl__rb_transplant(t, y, y->right);
            y->right = z->right;
            y->right->parent = y;
        }
        stl__rb_transplant(t, z, y);
        y->left = z->left;
        y->left->parent = y;
        y->color = z->color;
    }

    t->size -= 1;
    if (y_original_color == STL_RB_BLACK) {
        stl__rb_delete_fixup(t, x, x_parent);
    }
    stl__rb_destroy_node(t, z);
    return STL_OK;
}

int stl_rbtree_erase(stl_rbtree *t, const void *key)
{
    stl_rbtree_node *node;

    if (t == NULL || key == NULL) {
        STL_REPORT_INVALID("rbtree erase with NULL key");
        return STL_ERR_INVALID;
    }
    node = stl__rb_find_node(t, key);
    if (node == NULL) {
        stl__set_error_at(STL_ERR_NOT_FOUND, __FILE__, __LINE__, "rbtree key not found");
        return STL_ERR_NOT_FOUND;
    }
    if (t->policy == STL_RBTREE_MULTI) {
        /* Erase the leftmost match so erasing one element at a time walks the
         * run of equal keys in a predictable order. */
        while (node->left != NULL && t->key_cmp(key, stl__rb_key(t, node->left)) == 0) {
            node = node->left;
        }
    }
    return stl_rbtree_erase_node(t, node);
}

int stl_rbtree_erase_range(stl_rbtree *t, const void *lo, const void *hi)
{
    stl_rbtree_node *first;
    size_t removed = 0;

    if (t == NULL) {
        STL_REPORT_INVALID("NULL rbtree");
        return STL_ERR_INVALID;
    }
    if (lo == NULL && hi == NULL) {
        stl_rbtree_clear_ex(t);
        return STL_OK;
    }
    first = (lo != NULL) ? stl_rbtree_lower_bound(t, lo) : stl_rbtree_first(t);
    while (first != NULL) {
        stl_rbtree_node *next = stl_rbtree_next(first);
        if (hi != NULL && t->key_cmp(stl__rb_key(t, first), hi) >= 0) {
            break;
        }
        stl_rbtree_erase_node(t, first);
        ++removed;
        first = next;
    }
    return (int)removed;
}

static void stl__rb_clear_subtree(stl_rbtree *t, stl_rbtree_node *node, int call_dtor)
{
    if (node == NULL) {
        return;
    }
    stl__rb_clear_subtree(t, node->left, call_dtor);
    stl__rb_clear_subtree(t, node->right, call_dtor);
    if (call_dtor && t->elem_dtor != NULL) {
        t->elem_dtor(stl__rb_data(t, node));
    }
    stl_mem_free(t->alloc, node);
}

void stl_rbtree_clear(stl_rbtree *t)
{
    if (t == NULL) {
        return;
    }
    stl__rb_clear_subtree(t, t->root, 0);
    t->root = NULL;
    t->size = 0;
}

void stl_rbtree_clear_ex(stl_rbtree *t)
{
    if (t == NULL) {
        return;
    }
    stl__rb_clear_subtree(t, t->root, 1);
    t->root = NULL;
    t->size = 0;
}

/* ------------------------------------------------------------------ */
/* Validation                                                          */
/* ------------------------------------------------------------------ */

static int stl__rb_validate_subtree(const stl_rbtree *t, const stl_rbtree_node *node,
                                    int *black_height)
{
    int lh = 0, rh = 0;

    if (node == NULL) {
        *black_height = 1;
        return 1;
    }
    if (node->color == STL_RB_RED) {
        if ((node->left != NULL && node->left->color == STL_RB_RED) ||
            (node->right != NULL && node->right->color == STL_RB_RED)) {
            return 0;   /* red node with a red child */
        }
    }
    if (node->left != NULL && node->left->parent != node) return 0;
    if (node->right != NULL && node->right->parent != node) return 0;
    if (!stl__rb_validate_subtree(t, node->left, &lh)) return 0;
    if (!stl__rb_validate_subtree(t, node->right, &rh)) return 0;
    if (lh != rh) {
        return 0;       /* unequal black heights */
    }
    *black_height = lh + ((node->color == STL_RB_BLACK) ? 1 : 0);
    return 1;
}

int stl_rbtree_validate(const stl_rbtree *t)
{
    int bh = 0;
    size_t counted = 0;
    const stl_rbtree_node *node;

    if (t == NULL) {
        return 0;
    }
    if (t->root != NULL && t->root->color != STL_RB_BLACK) {
        return 0;
    }
    if (!stl__rb_validate_subtree(t, t->root, &bh)) {
        return 0;
    }
    /* In-order traversal must be sorted and visit exactly `size` nodes. */
    {
        const stl_rbtree_node *prev = NULL;
        for (node = stl_rbtree_first((stl_rbtree *)t); node != NULL;
             node = stl_rbtree_next((stl_rbtree_node *)node)) {
            if (prev != NULL && t->key_cmp(stl__rb_key(t, prev), stl__rb_key(t, node)) > 0) {
                return 0;
            }
            prev = node;
            ++counted;
        }
    }
    return (counted == t->size) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Iterators                                                           */
/* ------------------------------------------------------------------ */

stl_iterator stl_rbtree_begin(stl_rbtree *t)
{
    stl_iterator it;
    stl_rbtree_node *node = stl_rbtree_first(t);
    it.owner = t;
    it.elem = (node != NULL) ? stl__rb_data(t, node) : NULL;
    it.index = 0;
    return it;
}

stl_iterator stl_rbtree_end(stl_rbtree *t)
{
    stl_iterator it;
    it.owner = t;
    it.elem = NULL;
    it.index = (t != NULL) ? t->size : 0;
    return it;
}

stl_iterator stl_rbtree_rbegin(stl_rbtree *t)
{
    stl_iterator it;
    stl_rbtree_node *node = stl_rbtree_last(t);
    it.owner = t;
    it.elem = (node != NULL) ? stl__rb_data(t, node) : NULL;
    it.index = (t != NULL && t->size > 0) ? t->size - 1 : 0;
    return it;
}

stl_iterator stl_rbtree_rend(stl_rbtree *t)
{
    stl_iterator it = stl_rbtree_end(t);
    it.index = (size_t)-1;
    return it;
}

stl_iterator stl_rbtree_iter_next(stl_iterator it)
{
    stl_rbtree *t = (stl_rbtree *)it.owner;
    stl_rbtree_node *node;

    if (t == NULL || it.elem == NULL) {
        return stl_rbtree_end(t);
    }
    node = (stl_rbtree_node *)((stl_byte *)it.elem - t->node_offset);
    node = stl_rbtree_next(node);
    if (node == NULL) {
        return stl_rbtree_end(t);
    }
    it.elem = stl__rb_data(t, node);
    it.index += 1;
    return it;
}

stl_iterator stl_rbtree_iter_prev(stl_iterator it)
{
    stl_rbtree *t = (stl_rbtree *)it.owner;
    stl_rbtree_node *node;

    if (t == NULL) {
        return stl_iter_null();
    }
    if (it.elem == NULL) {
        return stl_rbtree_rbegin(t);
    }
    node = (stl_rbtree_node *)((stl_byte *)it.elem - t->node_offset);
    node = stl_rbtree_prev(node);
    if (node == NULL) {
        return stl_rbtree_rend(t);
    }
    it.elem = stl__rb_data(t, node);
    if (it.index > 0) {
        it.index -= 1;
    }
    return it;
}

ptrdiff_t stl_rbtree_iter_distance(stl_iterator first, stl_iterator last)
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
