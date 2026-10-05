/*
 * test_macros.c -- exercises the convenience macro layer of libstl.h.
 *
 * Build:
 *     cc -std=c11 -I. -o test_macros tests/test_macros.c libstl.a -lpthread -lm
 *
 * The scope-exit helpers are exercised only when the build opts in with
 * -DSTL_ENABLE_CLEANUP, since that feature relies on a GCC/Clang extension.
 */

#include "libstl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Declare a named key/value pair plus its comparator in one line. */
stl_define_pair(int_pair, int, int);

typedef struct point {
    int x;
    int y;
} point;

/* Per-field comparator and equality functions generated from the field name. */
stl_define_cmp_fn(point_cmp_x, point, x);
stl_define_eq_fn(point_eq_x, point, x);

typedef struct blob {
    char *data;
    int   len;
} blob;


/* Destructor that frees the owned buffer of a blob element. */
stl_define_dtor_fn(blob_dtor, blob, stl_free_field(e->data));

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            printf("  FAIL %s:%d %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                    \
    } while (0)

#define CHECK_EQ_INT(a, b)                                                   \
    do {                                                                     \
        long x = (long)(a), y = (long)(b);                                   \
        ++g_checks;                                                          \
        if (x != y) {                                                        \
            ++g_failures;                                                    \
            printf("  FAIL %s:%d %s == %s (%ld != %ld)\n",                   \
                   __FILE__, __LINE__, #a, #b, x, y);                        \
        }                                                                    \
    } while (0)

static void test_allocation_macros(void)
{
    printf("\n== allocation macros ==\n");
    {
        int *one = stl_new(int);
        int *many;
        CHECK(one != NULL);
        *one = 5;
        CHECK_EQ_INT(*one, 5);
        stl_delete(one);

        many = stl_new_array(int, 10);
        CHECK(many != NULL);
        CHECK_EQ_INT(many[0], 0);      /* zeroed */
        CHECK_EQ_INT(many[9], 0);
        stl_delete(many);
    }

    {
        point *p = stl_new(point);
        CHECK(p != NULL);
        CHECK_EQ_INT(p->x, 0);
        stl_delete(p);
    }

    {
        /* blob carries an owned buffer: exercise the generated destructor. */
        blob b;
        b.data = (char *)stl_mem_alloc(NULL, 16, 1);
        b.len = 16;
        CHECK(b.data != NULL);
        strcpy(b.data, "payload");
        blob_dtor(&b);
        CHECK(b.data == NULL);
    }

    {
        /* stl_free_all frees several blocks and ignores NULL arguments. */
        int *a = stl_new(int);
        int *b = NULL;
        int *c = stl_new(int);
        CHECK(a != NULL && c != NULL);
        stl_free_all(a, b, c);
        a = NULL;
        c = NULL;
        stl_free_all(a, b, c);   /* now a no-op */
    }

    CHECK_EQ_INT(stl_offsetof(int_pair, value), sizeof(int));
}

static void test_scope_helpers(void)
{
    printf("\n== scope helpers ==\n");
#if STL_HAVE_CLEANUP
    {
        int *p stl_autofree = stl_new(int);
        *p = 7;
        CHECK_EQ_INT(*p, 7);
        /* Released automatically when this block exits. */
    }
    printf("  (stl_autofree is active: STL_ENABLE_CLEANUP was defined)\n");
#else
    printf("  (stl_autofree is inactive; build with -DSTL_ENABLE_CLEANUP to test it)\n");
#endif

    /* The string helpers declare a stl_string* either way; only the automatic
     * release depends on the cleanup extension. */
    {
        stl_string_scope(s, "hello");
        CHECK_EQ_INT(stl_string_size(s), 5);
        stl_str_append_fmt(s, "-%d", 42);
        CHECK(strcmp(stl_cstr(s), "hello-42") == 0);
#if !STL_HAVE_CLEANUP
        stl_string_free(s);
#endif
    }
    {
        stl_string_scope_fmt(s, "%s=%d", "answer", 42);
        CHECK(strcmp(stl_cstr(s), "answer=42") == 0);
#if !STL_HAVE_CLEANUP
        stl_string_free(s);
#endif
    }
}

static void test_vector_macros(void)
{
    stl_vector *v = stl_vector_new(sizeof(point), NULL);
    point *p;

    printf("\n== vector macros ==\n");

    stl_vector_push_literal(v, point, { .x = 1, .y = 2 });
    stl_vector_push_literal(v, point, { .x = 3, .y = 4 });
    {
        point tmp;
        tmp.x = 5;
        tmp.y = 6;
        stl_vector_push(v, point, tmp);
    }

    CHECK_EQ_INT(stl_vector_size(v), 3);
    CHECK_EQ_INT(stl_vector_at_t(v, point, 0)->x, 1);
    CHECK_EQ_INT(stl_vector_at_t(v, point, 1)->y, 4);
    CHECK_EQ_INT(stl_vector_back_t(v, point)->x, 5);
    CHECK_EQ_INT(stl_vector_front_t(v, point)->x, 1);

    /* A wrong element type yields NULL instead of a silent overflow. */
    CHECK(stl_vector_at_t(v, int, 0) == NULL);

    {
        int total = 0;
        stl_vector_foreach_t(v, point, p) {
            total += p->x + p->y;
        }
        CHECK_EQ_INT(total, 21);
    }

    {
        int total = 0;
        stl_vector_foreach_rev(v, point, p) {
            total += p->x;
        }
        CHECK_EQ_INT(total, 9);
    }

    {
        size_t seen = 0;
        stl_vector_foreach_idx(v, point, p, idx) {
            CHECK_EQ_INT(p->x, (int)idx * 2 + 1);
            ++seen;
        }
        CHECK_EQ_INT(seen, 3);
    }

    /* Mutation through the iterator pointer is visible in the container. */
    stl_vector_foreach_t(v, point, p) {
        p->y = 0;
    }
    CHECK_EQ_INT(stl_vector_at_t(v, point, 0)->y, 0);

    stl_vector_free(v);
}

static void test_set_macros(void)
{
    stl_set *s = stl_set_new(sizeof(point), point_cmp_x, NULL);
    point *p;
    int i;

    printf("\n== set macros ==\n");
    CHECK(s != NULL);
    CHECK_EQ_INT(stl_set_empty(s), 1);

    for (i = 10; i > 0; --i) {
        point q;
        q.x = i;
        q.y = i * 2;
        stl_set_insert(s, &q);
    }
    CHECK_EQ_INT(stl_set_size(s), 10);

    {
        int expected = 1;
        stl_set_foreach_t(s, point, p) {
            CHECK_EQ_INT(p->x, expected);
            ++expected;
        }
        CHECK_EQ_INT(expected, 11);
    }

    {
        int expected = 10;
        stl_set_foreach_rev(s, point, p) {
            CHECK_EQ_INT(p->x, expected);
            --expected;
        }
        CHECK_EQ_INT(expected, 0);
    }

    stl_set_free(s);
}

/* A field at a non-zero offset, and an unsigned field: both used to compare
 * the wrong bytes, either reading the wrong region of the element or ordering
 * an unsigned value as if it were signed. */
typedef struct {
    int            id;
    int            score;
    unsigned short code;
} graded;

stl_define_cmp_fn(graded_cmp_score, graded, score);
stl_define_cmp_fn(graded_cmp_code, graded, code);

static void test_field_offset_comparators(void)
{
    printf("\n== comparators over a non-zero field offset ==\n");
    {
        stl_set *s = stl_set_new(sizeof(graded), graded_cmp_score, NULL);
        graded *p;
        int seen[4];
        int n = 0;
        int i;

        CHECK(offsetof(graded, score) != 0);
        for (i = 0; i < 4; ++i) {
            graded g;
            g.id = i;
            /* score deliberately not in insertion order */
            g.score = (int[]){ 50, 10, 90, 30 }[i];
            g.code = 0;
            stl_set_insert(s, &g);
        }
        stl_set_foreach_t(s, graded, p) {
            if (n < 4) seen[n] = p->score;
            ++n;
        }
        CHECK_EQ_INT(n, 4);
        CHECK_EQ_INT(seen[0], 10);
        CHECK_EQ_INT(seen[1], 30);
        CHECK_EQ_INT(seen[2], 50);
        CHECK_EQ_INT(seen[3], 90);
        stl_set_free(s);
    }

    printf("== unsigned fields ==\n");
    {
        graded a, b;
        memset(&a, 0, sizeof(a));
        memset(&b, 0, sizeof(b));
        a.code = 60000;
        b.code = 1;
        /* 60000 > 1 as unsigned; as signed 16-bit it would be negative. */
        CHECK(graded_cmp_code(&a, &b) > 0);
        CHECK(graded_cmp_code(&b, &a) < 0);
        CHECK_EQ_INT(graded_cmp_code(&a, &a), 0);
    }
}

static void test_map_macros(void)
{
    stl_map *m = stl_pair_map_new(int_pair, int, int);
    int_pair *it;

    printf("\n== map macros ==\n");
    CHECK(m != NULL);

    stl_map_put_pair(m, int_pair, 1, 100);
    stl_map_put_pair(m, int_pair, 2, 200);
    stl_map_put_pair(m, int_pair, 3, 300);
    CHECK_EQ_INT(stl_map_size(m), 3);

    {
        int expected_key = 1;
        stl_map_foreach_t(m, int_pair, it) {
            CHECK_EQ_INT(it->key, expected_key);
            CHECK_EQ_INT(it->value, expected_key * 100);
            ++expected_key;
        }
        CHECK_EQ_INT(expected_key, 4);
    }

    {
        int expected_key = 3;
        stl_map_foreach_rev(m, int_pair, it) {
            CHECK_EQ_INT(it->key, expected_key);
            --expected_key;
        }
        CHECK_EQ_INT(expected_key, 0);
    }

    {
        int key_sum = 0;
        int value_sum = 0;
        stl_map_foreach_kv(m, int_pair, it) {
            key_sum += it->key;
            value_sum += it->value;
        }
        CHECK_EQ_INT(key_sum, 6);
        CHECK_EQ_INT(value_sum, 600);
    }

    {
        int lookup = 2;
        CHECK_EQ_INT(*(int *)stl_map_get(m, &lookup), 200);
    }

    stl_map_free(m);
}

static void test_hashmap_macros(void)
{
    stl_hashmap *m = stl_pair_hashmap_new(int_pair, int, int, stl_hash_int, stl_eq_int);
    int_pair *it;

    printf("\n== hashmap macros ==\n");
    CHECK(m != NULL);

    stl_hashmap_put_pair(m, int_pair, 10, 1000);
    stl_hashmap_put_pair(m, int_pair, 20, 2000);
    CHECK_EQ_INT(stl_hashmap_size(m), 2);

    {
        int seen = 0;
        stl_hashmap_foreach_t(m, int_pair, it) {
            CHECK(it->key == 10 || it->key == 20);
            CHECK_EQ_INT(it->value, it->key * 100);
            ++seen;
        }
        CHECK_EQ_INT(seen, 2);
    }

    stl_hashmap_free(m);
}

static void test_stringset_macros(void)
{
    stl_set *s = stl_strset_new();
    const char *names[4];
    char **it;
    int i;

    printf("\n== string set macros ==\n");
    names[0] = "charlie";
    names[1] = "alpha";
    names[2] = "bravo";
    names[3] = "alpha";     /* duplicate */
    for (i = 0; i < 4; ++i) {
        stl_set_insert(s, &names[i]);
    }
    CHECK_EQ_INT(stl_set_size(s), 3);

    {
        const char *expected[3];
        int idx = 0;
        expected[0] = "alpha";
        expected[1] = "bravo";
        expected[2] = "charlie";
        stl_set_foreach_t(s, char *, it) {
            CHECK(strcmp(*it, expected[idx]) == 0);
            ++idx;
        }
        CHECK_EQ_INT(idx, 3);
    }

    stl_set_free(s);
}

static void test_list_macros(void)
{
    stl_list *l = stl_list_new(sizeof(int), NULL);
    int *p;
    int i;

    printf("\n== list macros ==\n");
    for (i = 0; i < 6; ++i) {
        stl_list_push_back_t(l, int, i);
    }
    stl_list_push_front_t(l, int, -1);
    CHECK_EQ_INT(stl_list_size(l), 7);

    {
        int expected = -1;
        stl_list_foreach_t(l, int, p) {
            CHECK_EQ_INT(*p, expected);
            ++expected;
        }
        CHECK_EQ_INT(expected, 6);
    }
    CHECK_EQ_INT(*stl_list_front_t(l, int), -1);
    CHECK_EQ_INT(*stl_list_back_t(l, int), 5);

    /* Safe erase: removing even values while walking the list. */
    {
        stl_list_foreach_safe(l, int, p, node) {
            if (*p % 2 == 0) {
                stl_list_erase_node(l, node);
            }
        }
    }
    /* 0, 2 and 4 were removed; -1, 1, 3 and 5 remain. */
    CHECK_EQ_INT(stl_list_size(l), 4);
    {
        int expected[4];
        int idx = 0;
        expected[0] = -1;
        expected[1] = 1;
        expected[2] = 3;
        expected[3] = 5;
        stl_list_foreach_t(l, int, p) {
            CHECK_EQ_INT(*p, expected[idx]);
            ++idx;
        }
        CHECK_EQ_INT(idx, 4);
    }

    {
        stl_deque *d = stl_deque_new(sizeof(int), NULL);
        stl_deque_push_back_t(d, int, 1);
        stl_deque_push_front_t(d, int, 0);
        CHECK_EQ_INT(*stl_deque_front_t(d, int), 0);
        CHECK_EQ_INT(*stl_deque_back_t(d, int), 1);
        {
            int sum = 0;
            stl_deque_foreach_t(d, int, p) { sum += *p; }
            CHECK_EQ_INT(sum, 1);
        }
        {
            int sum = 0;
            stl_deque_foreach_rev(d, int, p) { sum += *p; }
            CHECK_EQ_INT(sum, 1);
        }
        stl_deque_free(d);
    }

    stl_list_free(l);
}

static void test_adaptor_macros(void)
{
    printf("\n== adaptor macros ==\n");
    {
        stl_stack *st = stl_stack_new(sizeof(int), NULL);
        int out = 0;
        stl_stack_push_t(st, int, 7);
        stl_stack_push_t(st, int, 8);
        CHECK_EQ_INT(*stl_stack_top_t(st, int), 8);
        stl_stack_pop_t(st, int, out);
        CHECK_EQ_INT(out, 8);
        CHECK_EQ_INT(*stl_stack_top_t(st, int), 7);
        stl_stack_free(st);
    }
    {
        stl_queue *q = stl_queue_new(sizeof(int), NULL);
        int out = 0;
        stl_queue_push_t(q, int, 1);
        stl_queue_push_t(q, int, 2);
        CHECK_EQ_INT(*stl_queue_front_t(q, int), 1);
        CHECK_EQ_INT(*stl_queue_back_t(q, int), 2);
        stl_queue_pop_t(q, int, out);
        CHECK_EQ_INT(out, 1);
        stl_queue_free(q);
    }
    {
        stl_priority_queue *pq = stl_priority_queue_new(sizeof(int), stl_cmp_int32, NULL);
        int out = 0;
        stl_pq_push_t(pq, int, 3);
        stl_pq_push_t(pq, int, 9);
        stl_pq_push_t(pq, int, 5);
        CHECK_EQ_INT(*stl_pq_top_t(pq, int), 9);
        stl_pq_pop_t(pq, int, out);
        CHECK_EQ_INT(out, 9);
        stl_pq_pop_t(pq, int, out);
        CHECK_EQ_INT(out, 5);
        stl_priority_queue_free(pq);
    }
}

static void test_utility_macros(void)
{
    printf("\n== utility macros ==\n");
    CHECK_EQ_INT(stl_min_of(3, 9), 3);
    CHECK_EQ_INT(stl_max_of(3, 9), 9);
    CHECK_EQ_INT(stl_clamp(15, 0, 10), 10);
    CHECK_EQ_INT(stl_clamp(-5, 0, 10), 0);
    CHECK_EQ_INT(stl_clamp(5, 0, 10), 5);
    CHECK_EQ_INT(stl_array_len("hello"), 6);   /* includes the NUL */

    {
        int a = 1;
        int b = 2;
        stl_swap_t(int, a, b);
        CHECK_EQ_INT(a, 2);
        CHECK_EQ_INT(b, 1);
        stl_swap_generic(a, b);
        CHECK_EQ_INT(a, 1);
        CHECK_EQ_INT(b, 2);
    }

    {
        int_pair p;
        stl_zero(p);
        CHECK_EQ_INT(p.key, 0);
        CHECK_EQ_INT(p.value, 0);
        p = stl_pair_make(int_pair, 3, 4);
        CHECK_EQ_INT(p.key, 3);
        CHECK_EQ_INT(p.value, 4);
    }

    {
        stl_vector *v = stl_vector_new(sizeof(int), NULL);
        int i;
        for (i = 0; i < 4; ++i) {
            stl_vector_push(v, int, i * i);
        }
        i = 0;
        stl_vector_foreach_t(v, int, item) {
            CHECK_EQ_INT(*item, i * i);
            ++i;
        }
        stl_vector_free(v);
    }
}

int main(void)
{
    printf("libstl %s -- convenience macro test\n", stl_version_string());

    test_allocation_macros();
    test_scope_helpers();
    test_vector_macros();
    test_set_macros();
    test_field_offset_comparators();
    test_map_macros();
    test_hashmap_macros();
    test_stringset_macros();
    test_list_macros();
    test_adaptor_macros();
    test_utility_macros();

    printf("\n----------------------------------------\n");
    printf("%d checks, %d failures\n", g_checks, g_failures);
    if (g_failures == 0) {
        printf("ALL MACRO TESTS PASSED\n");
        return 0;
    }
    printf("THERE WERE FAILURES\n");
    return 1;
}
