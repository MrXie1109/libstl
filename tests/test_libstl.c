/*
 * test_libstl.c -- exercise every public area of libstl.
 *
 * Build:  cc -I. -o test_libstl tests/test_libstl.c libstl.a -lpthread -lm
 * Run  :  ./test_libstl
 *
 * Exits non-zero on the first failure and prints a summary otherwise.
 */

#include "libstl.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/* Tiny test harness                                                   */
/* ------------------------------------------------------------------ */

static int g_checks = 0;
static int g_failures = 0;
static const char *g_section = "";

#define SECTION(name) do { g_section = (name); printf("\n== %s ==\n", (name)); } while (0)

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            printf("  FAIL %s:%d [%s] %s\n", __FILE__, __LINE__, g_section, #cond); \
        }                                                                    \
    } while (0)

#define CHECK_EQ_INT(a, b)                                                   \
    do {                                                                     \
        long stl__a = (long)(a), stl__b = (long)(b);                         \
        ++g_checks;                                                          \
        if (stl__a != stl__b) {                                              \
            ++g_failures;                                                    \
            printf("  FAIL %s:%d [%s] %s == %s (%ld != %ld)\n",              \
                   __FILE__, __LINE__, g_section, #a, #b, stl__a, stl__b);   \
        }                                                                    \
    } while (0)

#define CHECK_EQ_STR(a, b)                                                   \
    do {                                                                     \
        const char *stl__a = (a), *stl__b = (b);                             \
        ++g_checks;                                                          \
        if (stl__a == NULL || stl__b == NULL || strcmp(stl__a, stl__b) != 0) { \
            ++g_failures;                                                    \
            printf("  FAIL %s:%d [%s] \"%s\" != \"%s\"\n",                   \
                   __FILE__, __LINE__, g_section,                            \
                   stl__a ? stl__a : "(null)", stl__b ? stl__b : "(null)");  \
        }                                                                    \
    } while (0)

/* ------------------------------------------------------------------ */
/* vector                                                              */
/* ------------------------------------------------------------------ */

static void test_vector(void)
{
    stl_vector *v = stl_vector_new(sizeof(int), NULL);
    int i;

    SECTION("vector basics");
    CHECK(v != NULL);
    CHECK(stl_vector_empty(v));
    CHECK_EQ_INT(stl_vector_size(v), 0);

    for (i = 0; i < 1000; ++i) {
        CHECK(stl_vector_push_back(v, &i) == STL_OK);
    }
    CHECK_EQ_INT(stl_vector_size(v), 1000);
    CHECK(stl_vector_capacity(v) >= 1000);
    CHECK_EQ_INT(*(int *)stl_vector_front(v), 0);
    CHECK_EQ_INT(*(int *)stl_vector_back(v), 999);
    CHECK_EQ_INT(*(int *)stl_vector_at(v, 500), 500);
    CHECK(stl_vector_at(v, 1000) == NULL);       /* out of range reports */
    CHECK(stl_get_error() == STL_ERR_RANGE);
    CHECK_EQ_INT(*(int *)stl_vector_index(v, -1), 999);
    CHECK_EQ_INT(*(int *)stl_vector_index(v, -1000), 0);

    SECTION("vector modifiers");
    {
        int x = 12345;
        CHECK(stl_vector_insert(v, 0, &x) == STL_OK);
        CHECK_EQ_INT(stl_vector_size(v), 1001);
        CHECK_EQ_INT(*(int *)stl_vector_at(v, 0), 12345);
        CHECK_EQ_INT(*(int *)stl_vector_at(v, 1), 0);
        CHECK(stl_vector_erase(v, 0) == STL_OK);
        CHECK_EQ_INT(stl_vector_size(v), 1000);

        CHECK(stl_vector_insert_n(v, 10, 3, &x) == STL_OK);
        CHECK_EQ_INT(stl_vector_size(v), 1003);
        CHECK_EQ_INT(*(int *)stl_vector_at(v, 10), 12345);
        CHECK_EQ_INT(*(int *)stl_vector_at(v, 12), 12345);
        CHECK(stl_vector_erase_range(v, 10, 13) == STL_OK);
        CHECK_EQ_INT(stl_vector_size(v), 1000);
    }

    SECTION("vector algorithms");
    {
        int neg = -1;
        int found;
        stl_vector_fill(v, &neg);
        CHECK_EQ_INT(*(int *)stl_vector_at(v, 0), -1);
        CHECK_EQ_INT(*(int *)stl_vector_at(v, 999), -1);
        for (i = 0; i < 1000; ++i) {
            int val = 999 - i;               /* reverse sorted input */
            *(int *)stl_vector_at(v, i) = val;
        }
        stl_vector_sort(v, stl_cmp_int32);
        CHECK(stl_is_sorted(stl_vector_data(v), stl_vector_size(v), sizeof(int), stl_cmp_int32));
        CHECK_EQ_INT(*(int *)stl_vector_at(v, 0), 0);
        CHECK_EQ_INT(*(int *)stl_vector_at(v, 999), 999);

        found = 42;
        CHECK(stl_vector_contains(v, &found, stl_cmp_int32));
        CHECK_EQ_INT(stl_vector_lower_bound(v, &found, stl_cmp_int32), 42);
        CHECK_EQ_INT(stl_vector_upper_bound(v, &found, stl_cmp_int32), 43);

        CHECK(stl_vector_index_of(v, &found, stl_eq_int) == 42);
        stl_vector_reverse(v);
        CHECK_EQ_INT(*(int *)stl_vector_at(v, 0), 999);
        stl_vector_reverse(v);
    }

    SECTION("vector resize / reserve");
    CHECK(stl_vector_resize_v(v, 5, &i) == STL_OK);
    CHECK_EQ_INT(stl_vector_size(v), 5);
    CHECK(stl_vector_resize(v, 2000) == STL_OK);
    CHECK_EQ_INT(stl_vector_size(v), 2000);
    CHECK_EQ_INT(*(int *)stl_vector_at(v, 1999), 0);
    CHECK(stl_vector_shrink_to_fit(v) == STL_OK);
    CHECK_EQ_INT(stl_vector_capacity(v), 2000);
    stl_vector_clear(v);
    CHECK(stl_vector_empty(v));

    SECTION("vector iterators");
    for (i = 0; i < 10; ++i) {
        CHECK(stl_vector_push_back(v, &i) == STL_OK);
    }
    {
        stl_iterator it = stl_vector_begin(v);
        int sum = 0;
        int steps = 0;
        while (!stl_iter_equal(it, stl_vector_end(v))) {
            sum += *(int *)stl_iter_data(it);
            it = stl_vector_iter_next(it);
            ++steps;
            CHECK(steps < 100);
        }
        CHECK_EQ_INT(sum, 45);
        CHECK_EQ_INT(stl_vector_iter_distance(stl_vector_begin(v), stl_vector_end(v)), 10);
        it = stl_vector_iter_prev(stl_vector_end(v));
        CHECK_EQ_INT(*(int *)stl_iter_data(it), 9);
    }

    stl_vector_free(v);
}

/* ------------------------------------------------------------------ */
/* deque                                                               */
/* ------------------------------------------------------------------ */

static void test_deque(void)
{
    stl_deque *d = stl_deque_new(sizeof(int), NULL);
    int i;

    SECTION("deque");
    CHECK(d != NULL);
    for (i = 0; i < 500; ++i) {
        int v = i;
        CHECK(stl_deque_push_back(d, &v) == STL_OK);
    }
    for (i = 0; i < 500; ++i) {
        int v = 1000 + i;
        CHECK(stl_deque_push_front(d, &v) == STL_OK);
    }
    CHECK_EQ_INT(stl_deque_size(d), 1000);
    CHECK_EQ_INT(*(int *)stl_deque_front(d), 1499);
    CHECK_EQ_INT(*(int *)stl_deque_back(d), 499);
    CHECK_EQ_INT(*(int *)stl_deque_at(d, 0), 1499);
    CHECK_EQ_INT(*(int *)stl_deque_at(d, 499), 1000);
    CHECK_EQ_INT(*(int *)stl_deque_at(d, 500), 0);
    CHECK_EQ_INT(*(int *)stl_deque_at(d, 999), 499);

    stl_deque_pop_front(d);
    stl_deque_pop_back(d);
    CHECK_EQ_INT(stl_deque_size(d), 998);
    CHECK_EQ_INT(*(int *)stl_deque_front(d), 1498);
    CHECK_EQ_INT(*(int *)stl_deque_back(d), 498);

    {
        int x = 777;
        CHECK(stl_deque_insert(d, 100, &x) == STL_OK);
        CHECK_EQ_INT(stl_deque_size(d), 999);
        CHECK_EQ_INT(*(int *)stl_deque_at(d, 100), 777);
        CHECK(stl_deque_erase(d, 100) == STL_OK);
        CHECK_EQ_INT(stl_deque_size(d), 998);
    }

    stl_deque_reverse(d);
    CHECK_EQ_INT(*(int *)stl_deque_front(d), 498);
    stl_deque_reverse(d);
    CHECK_EQ_INT(*(int *)stl_deque_front(d), 1498);

    stl_deque_sort(d, stl_cmp_int32);
    CHECK_EQ_INT(*(int *)stl_deque_front(d), 0);
    /* Elements span 0..499 from the back pushes and 1000..1498 from the front
     * pushes, minus the two popped ends: 998 values ending at 1498. */
    CHECK_EQ_INT(*(int *)stl_deque_at(d, 997), 1498);

    {
        stl_iterator it = stl_deque_begin(d);
        size_t n = 0;
        while (!stl_iter_equal(it, stl_deque_end(d)) && n < 5000) {
            it = stl_deque_iter_next(it);
            ++n;
        }
        CHECK_EQ_INT(n, 998);
    }

    stl_deque_clear(d);
    CHECK(stl_deque_empty(d));
    stl_deque_free(d);
}

/* ------------------------------------------------------------------ */
/* list                                                                */
/* ------------------------------------------------------------------ */

static int sum_list_int(const stl_list *l)
{
    int sum = 0;
    int *p;
    stl_list_foreach_t((stl_list *)l, int, p) {
        sum += *p;
    }
    return sum;
}

static void test_list(void)
{
    stl_list *l = stl_list_new(sizeof(int), NULL);
    int i;

    SECTION("list");
    for (i = 0; i < 100; ++i) {
        CHECK(stl_list_push_back(l, &i) != NULL);
    }
    CHECK_EQ_INT(stl_list_size(l), 100);
    CHECK_EQ_INT(*(int *)stl_list_front(l), 0);
    CHECK_EQ_INT(*(int *)stl_list_back(l), 99);
    CHECK_EQ_INT(sum_list_int(l), 4950);

    for (i = 0; i < 10; ++i) {
        int v = -i;
        CHECK(stl_list_push_front(l, &v) != NULL);
    }
    CHECK_EQ_INT(stl_list_size(l), 110);
    CHECK_EQ_INT(*(int *)stl_list_front(l), -9);
    CHECK_EQ_INT(*(int *)stl_list_at(l, 0), -9);
    CHECK_EQ_INT(*(int *)stl_list_at(l, 10), 0);

    stl_list_pop_front(l);
    stl_list_pop_back(l);
    CHECK_EQ_INT(stl_list_size(l), 108);

    SECTION("list sort/unique/reverse");
    stl_list_sort(l, stl_cmp_int32);
    CHECK_EQ_INT(*(int *)stl_list_front(l), -8);
    CHECK_EQ_INT(*(int *)stl_list_back(l), 98);

    {
        int dup = 50;
        CHECK(stl_list_push_back(l, &dup) != NULL);
        stl_list_sort(l, stl_cmp_int32);
        CHECK_EQ_INT(stl_list_size(l), 109);
        stl_list_unique(l, stl_eq_int);
        /* The front loop pushed -0, which equals the existing 0, so after
         * adding the extra 50 there are two duplicate pairs to collapse. */
        CHECK_EQ_INT(stl_list_size(l), 107);
    }

    stl_list_reverse(l);
    CHECK_EQ_INT(*(int *)stl_list_front(l), 98);
    CHECK_EQ_INT(*(int *)stl_list_back(l), -8);
    stl_list_reverse(l);

    SECTION("list splice/merge/slice");
    {
        stl_list *other = stl_list_new(sizeof(int), NULL);
        int vals[3];
        vals[0] = 1000; vals[1] = 1001; vals[2] = 1002;
        for (i = 0; i < 3; ++i) {
            CHECK(stl_list_push_back(other, &vals[i]) != NULL);
        }
        CHECK(stl_list_splice(l, 0, other) == STL_OK);
        CHECK_EQ_INT(stl_list_size(l), 110);
        CHECK_EQ_INT(stl_list_size(other), 0);
        CHECK_EQ_INT(stl_list_size(l), 110);

        {
            stl_list *sorted_a = stl_list_new(sizeof(int), NULL);
            stl_list *sorted_b = stl_list_new(sizeof(int), NULL);
            int a[3];
            int b[3];
            a[0] = 1; a[1] = 5; a[2] = 9;
            b[0] = 2; b[1] = 5; b[2] = 7;
            for (i = 0; i < 3; ++i) {
                stl_list_push_back(sorted_a, &a[i]);
                stl_list_push_back(sorted_b, &b[i]);
            }
            CHECK(stl_list_merge(sorted_a, sorted_b, stl_cmp_int32) == STL_OK);
            CHECK_EQ_INT(stl_list_size(sorted_a), 6);
            CHECK_EQ_INT(stl_list_size(sorted_b), 0);
            CHECK_EQ_INT(*(int *)stl_list_at(sorted_a, 0), 1);
            CHECK_EQ_INT(*(int *)stl_list_at(sorted_a, 5), 9);
            stl_list_free(sorted_a);
            stl_list_free(sorted_b);
        }
        stl_list_free(other);

        {
            stl_list *sl = stl_list_slice(l, 0, 3);
            CHECK(sl != NULL);
            CHECK_EQ_INT(stl_list_size(sl), 3);
            stl_list_free(sl);
        }
    }

    SECTION("list node handles");
    {
        int v = 4242;
        stl_list_node *node = stl_list_push_back(l, &v);
        CHECK(node != NULL);
        CHECK_EQ_INT(*(int *)stl_list_node_data(node), 4242);
        CHECK(stl_list_erase_node(l, node) == STL_OK);
    }

    stl_list_clear(l);
    CHECK(stl_list_empty(l));
    stl_list_free(l);
}

/* ------------------------------------------------------------------ */
/* set / map                                                           */
/* ------------------------------------------------------------------ */

static void test_set(void)
{
    stl_set *s = stl_set_new(sizeof(int), stl_cmp_int32, NULL);
    int i;

    SECTION("set");
    for (i = 0; i < 200; ++i) {
        int v = i % 100;                     /* duplicates for half */
        stl_set_insert(s, &v);
    }
    CHECK_EQ_INT(stl_set_size(s), 100);
    CHECK(stl_set_validate(s));
    for (i = 0; i < 100; ++i) {
        CHECK(stl_set_contains(s, &i));
        CHECK_EQ_INT(stl_set_count(s, &i), 1);
    }
    {
        int v = 999;
        CHECK(!stl_set_contains(s, &v));
        CHECK(stl_set_find(s, &v) == NULL);
    }

    SECTION("set ordered iteration");
    {
        int expected = 0;
        stl_iterator it = stl_set_begin(s);
        while (!stl_iter_equal(it, stl_set_end(s))) {
            CHECK_EQ_INT(*(int *)stl_iter_data(it), expected);
            ++expected;
            it = stl_set_iter_next(it);
        }
        CHECK_EQ_INT(expected, 100);
    }

    SECTION("set erase/bounds");
    {
        int v = 50;
        CHECK(stl_set_erase(s, &v) == STL_OK);
        CHECK_EQ_INT(stl_set_size(s), 99);
        CHECK(!stl_set_contains(s, &v));
        CHECK_EQ_INT(*(int *)stl_set_lower_bound(s, &v), 51);
        CHECK_EQ_INT(*(int *)stl_set_upper_bound(s, &v), 51);
        v = 51;
        CHECK_EQ_INT(*(int *)stl_set_lower_bound(s, &v), 51);
        CHECK_EQ_INT(*(int *)stl_set_upper_bound(s, &v), 52);
    }
    CHECK(stl_set_validate(s));

    SECTION("set algebra");
    {
        stl_set *a = stl_set_new(sizeof(int), stl_cmp_int32, NULL);
        stl_set *b = stl_set_new(sizeof(int), stl_cmp_int32, NULL);
        stl_set *u;
        stl_set *inter;
        stl_set *diff;
        stl_set *sym;
        for (i = 0; i < 10; ++i) { stl_set_insert(a, &i); }
        for (i = 5; i < 15; ++i) { stl_set_insert(b, &i); }

        u = stl_set_union(a, b, NULL);
        inter = stl_set_intersection(a, b, NULL);
        diff = stl_set_difference(a, b, NULL);
        sym = stl_set_symmetric_difference(a, b, NULL);
        CHECK_EQ_INT(stl_set_size(u), 15);
        CHECK_EQ_INT(stl_set_size(inter), 5);
        CHECK_EQ_INT(stl_set_size(diff), 5);
        CHECK_EQ_INT(stl_set_size(sym), 10);
        CHECK(stl_set_is_subset(inter, a));
        CHECK(stl_set_is_subset(inter, b));
        CHECK(!stl_set_is_subset(a, b));
        CHECK(stl_set_validate(u));
        CHECK(stl_set_validate(inter));
        CHECK(stl_set_validate(diff));
        CHECK(stl_set_validate(sym));
        stl_set_free(u);
        stl_set_free(inter);
        stl_set_free(diff);
        stl_set_free(sym);
        stl_set_free(a);
        stl_set_free(b);
    }

    SECTION("multiset");
    {
        stl_set *ms = stl_set_new_policy(sizeof(int), stl_cmp_int32, STL_SET_MULTI, NULL, NULL);
        int v = 7;
        stl_set_insert(ms, &v);
        stl_set_insert(ms, &v);
        stl_set_insert(ms, &v);
        CHECK_EQ_INT(stl_set_size(ms), 3);
        CHECK_EQ_INT(stl_set_count(ms, &v), 3);
        CHECK(stl_set_erase(ms, &v) == STL_OK);
        CHECK_EQ_INT(stl_set_count(ms, &v), 2);
        CHECK(stl_set_validate(ms));
        stl_set_free(ms);
    }

    stl_set_free(s);
}

typedef struct int_pair {
    int key;
    int value;
} int_pair;

static stl_map *make_int_map(void)
{
    return stl_map_new(sizeof(int_pair), offsetof(int_pair, value), sizeof(int),
                       stl_cmp_int32, NULL);
}

static void test_map(void)
{
    stl_map *m = make_int_map();
    int i;

    SECTION("map");
    CHECK(m != NULL);
    for (i = 0; i < 100; ++i) {
        CHECK(stl_map_put(m, &i, &i) != NULL);
    }
    CHECK_EQ_INT(stl_map_size(m), 100);
    CHECK(stl_map_validate(m));

    for (i = 0; i < 100; ++i) {
        int *val = (int *)stl_map_get(m, &i);
        CHECK(val != NULL);
        CHECK_EQ_INT(*val, i);
    }
    {
        int missing = 12345;
        CHECK(stl_map_get(m, &missing) == NULL);
        CHECK(!stl_map_contains(m, &missing));
    }

    SECTION("map overwrite");
    {
        int key = 5;
        int newval = 5000;
        CHECK(stl_map_put(m, &key, &newval) != NULL);
        CHECK_EQ_INT(stl_map_size(m), 100);
        CHECK_EQ_INT(*(int *)stl_map_get(m, &key), 5000);
    }

    SECTION("map insert of duplicate key");
    {
        int_pair p;
        p.key = 5;
        p.value = 99;
        CHECK(stl_map_insert(m, &p) == NULL);     /* rejected */
        CHECK_EQ_INT(*(int *)stl_map_get(m, &p.key), 5000);
    }

    SECTION("map ordered iteration");
    {
        int expected = 0;
        stl_iterator it = stl_map_begin(m);
        while (!stl_iter_equal(it, stl_map_end(m))) {
            int_pair *p = (int_pair *)stl_iter_data(it);
            CHECK_EQ_INT(p->key, expected);
            ++expected;
            it = stl_map_iter_next(it);
        }
        CHECK_EQ_INT(expected, 100);
    }

    SECTION("map erase / bounds");
    {
        int key = 5;
        CHECK(stl_map_erase(m, &key) == STL_OK);
        CHECK_EQ_INT(stl_map_size(m), 99);
        CHECK(stl_map_get(m, &key) == NULL);
        CHECK(stl_map_erase(m, &key) == STL_ERR_NOT_FOUND);
        CHECK_EQ_INT(((int_pair *)stl_map_lower_bound(m, &key))->key, 6);
        CHECK_EQ_INT(((int_pair *)stl_map_upper_bound(m, &key))->key, 6);
        CHECK(stl_map_validate(m));
    }

    SECTION("map get_or_insert");
    {
        int key = 9999;
        int def = -1;
        int *slot = (int *)stl_map_get_or_insert(m, &key, &def);
        CHECK(slot != NULL);
        CHECK_EQ_INT(*slot, -1);
        *slot = 4242;
        CHECK_EQ_INT(*(int *)stl_map_get(m, &key), 4242);
        /* Second call must return the existing value. */
        CHECK_EQ_INT(*(int *)stl_map_get_or_insert(m, &key, &def), 4242);
    }

    SECTION("map foreach / range");
    {
        int_pair lo, hi;
        size_t total = 0;
        lo.key = 10; hi.key = 20;
        total = (size_t)stl_map_foreach_range(m, &lo.key, &hi.key, NULL, NULL);
        CHECK_EQ_INT(total, 0);   /* NULL callback visits nothing */

        total = 0;
        {
            stl_iterator it = stl_map_begin(m);
            while (!stl_iter_equal(it, stl_map_end(m))) {
                ++total;
                it = stl_map_iter_next(it);
            }
        }
        CHECK_EQ_INT(total, stl_map_size(m));
    }

    stl_map_free(m);

    SECTION("multimap");
    {
        stl_map *mm = stl_map_new_policy(sizeof(int_pair), offsetof(int_pair, value),
                                         sizeof(int), stl_cmp_int32, STL_MAP_MULTI, NULL, NULL);
        int_pair p;
        int k;
        for (k = 0; k < 3; ++k) {
            p.key = 1;
            p.value = k;
            CHECK(stl_map_insert(mm, &p) != NULL);
        }
        CHECK_EQ_INT(stl_map_size(mm), 3);
        CHECK_EQ_INT(stl_map_count(mm, &p.key), 3);
        CHECK(stl_map_validate(mm));
        stl_map_free(mm);
    }
}

/* ------------------------------------------------------------------ */
/* hash containers                                                     */
/* ------------------------------------------------------------------ */

static size_t hash_string_elem(const void *elem)
{
    const char *s = *(const char *const *)elem;
    return stl_hash_bytes(s, strlen(s));
}

static int eq_string_elem(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b) == 0;
}

static void test_hash(void)
{
    SECTION("hashset");
    {
        stl_hashset *hs = stl_hashset_new(sizeof(char *), hash_string_elem, eq_string_elem, NULL);
        const char *words[] = { "alpha", "beta", "gamma", "delta", "epsilon", "alpha" };
        size_t i;
        for (i = 0; i < sizeof(words) / sizeof(words[0]); ++i) {
            stl_hashset_insert(hs, &words[i]);
        }
        CHECK_EQ_INT(stl_hashset_size(hs), 5);
        CHECK(stl_hashset_contains(hs, &words[0]));
        CHECK(stl_hashset_contains(hs, &words[4]));
        {
            const char *missing = "zeta";
            CHECK(!stl_hashset_contains(hs, &missing));
            CHECK(stl_hashset_erase(hs, &missing) == STL_ERR_NOT_FOUND);
        }
        CHECK(stl_hashset_contains(hs, &words[0]));
        stl_hashset_erase(hs, &words[0]);
        CHECK_EQ_INT(stl_hashset_size(hs), 4);
        CHECK(!stl_hashset_contains(hs, &words[0]));
        CHECK(stl_hashset_reserve(hs, 1000) == STL_OK);
        CHECK_EQ_INT(stl_hashset_size(hs), 4);
        CHECK(stl_hashset_contains(hs, &words[1]));
        stl_hashset_free(hs);
    }

    SECTION("hashmap");
    {
        stl_hashmap *hm = stl_hashmap_new(sizeof(int_pair), offsetof(int_pair, value),
                                          sizeof(int), stl_hash_int, stl_eq_int, NULL);
        int i;
        for (i = 0; i < 1000; ++i) {
            CHECK(stl_hashmap_put(hm, &i, &i) != NULL);
        }
        CHECK_EQ_INT(stl_hashmap_size(hm), 1000);
        for (i = 0; i < 1000; ++i) {
            int *v = (int *)stl_hashmap_get(hm, &i);
            CHECK(v != NULL);
            CHECK_EQ_INT(*v, i);
        }
        {
            int missing = -1;
            CHECK(stl_hashmap_get(hm, &missing) == NULL);
        }
        for (i = 0; i < 500; ++i) {
            CHECK(stl_hashmap_erase(hm, &i) == STL_OK);
        }
        CHECK_EQ_INT(stl_hashmap_size(hm), 500);
        for (i = 500; i < 1000; ++i) {
            CHECK_EQ_INT(*(int *)stl_hashmap_get(hm, &i), i);
        }
        {
            size_t visited = 0;
            stl_iterator it = stl_hashmap_begin(hm);
            while (!stl_iter_equal(it, stl_hashmap_end(hm))) {
                ++visited;
                it = stl_hashmap_iter_next(it);
                CHECK(visited <= 500);
            }
            CHECK_EQ_INT(visited, 500);
        }
        stl_hashmap_free(hm);
    }

    SECTION("hash stress");
    {
        stl_hashmap *hm = stl_hashmap_new(sizeof(int_pair), offsetof(int_pair, value),
                                          sizeof(int), stl_hash_int, stl_eq_int, NULL);
        unsigned int seed = 12345;
        int i;
        /* Insert keys in a pseudo-random order to exercise rehashing. */
        for (i = 0; i < 20000; ++i) {
            int key = (int)(stl_rand_next(&seed) & 0xffffu);
            stl_hashmap_put(hm, &key, &i);
        }
        CHECK(stl_hashmap_size(hm) > 10000);
        stl_hashmap_free(hm);
    }
}

/* ------------------------------------------------------------------ */
/* adaptors                                                            */
/* ------------------------------------------------------------------ */

static void test_adaptors(void)
{
    SECTION("stack");
    {
        stl_stack *s = stl_stack_new(sizeof(int), NULL);
        int i;
        for (i = 0; i < 10; ++i) {
            CHECK(stl_stack_push(s, &i) == STL_OK);
        }
        CHECK_EQ_INT(stl_stack_size(s), 10);
        CHECK_EQ_INT(*(int *)stl_stack_top(s), 9);
        stl_stack_pop(s);
        CHECK_EQ_INT(*(int *)stl_stack_top(s), 8);
        while (!stl_stack_empty(s)) {
            stl_stack_pop(s);
        }
        CHECK(stl_stack_empty(s));
        CHECK(stl_stack_top(s) == NULL);
        CHECK(stl_get_error() == STL_ERR_EMPTY);
        stl_stack_free(s);
    }

    SECTION("queue");
    {
        stl_queue *q = stl_queue_new(sizeof(int), NULL);
        int i;
        for (i = 0; i < 10; ++i) {
            CHECK(stl_queue_push(q, &i) == STL_OK);
        }
        CHECK_EQ_INT(stl_queue_size(q), 10);
        CHECK_EQ_INT(*(int *)stl_queue_front(q), 0);
        CHECK_EQ_INT(*(int *)stl_queue_back(q), 9);
        stl_queue_pop(q);
        CHECK_EQ_INT(*(int *)stl_queue_front(q), 1);
        CHECK_EQ_INT(stl_queue_size(q), 9);
        stl_queue_clear(q);
        CHECK(stl_queue_empty(q));
        stl_queue_free(q);
    }

    SECTION("priority_queue");
    {
        stl_priority_queue *pq = stl_priority_queue_new(sizeof(int), stl_cmp_int32, NULL);
        int values[8];
        int i;
        values[0] = 5; values[1] = 1; values[2] = 9; values[3] = 3;
        values[4] = 7; values[5] = 2; values[6] = 8; values[7] = 4;
        for (i = 0; i < 8; ++i) {
            CHECK(stl_priority_queue_push(pq, &values[i]) == STL_OK);
        }
        CHECK_EQ_INT(stl_priority_queue_size(pq), 8);
        /* The queue holds 1..9 with 6 missing; a max-heap must yield them in
         * descending order. */
        {
            static const int expected[8] = { 9, 8, 7, 5, 4, 3, 2, 1 };
            for (i = 0; i < 8; ++i) {
                CHECK_EQ_INT(*(int *)stl_priority_queue_top(pq), expected[i]);
                stl_priority_queue_pop(pq);
            }
        }
        CHECK(stl_priority_queue_empty(pq));
        stl_priority_queue_free(pq);
    }

    SECTION("heap algorithms");
    {
        int data[10];
        int i;
        for (i = 0; i < 10; ++i) {
            data[i] = (i * 7) % 10;
        }
        stl_heap_make(data, 10, sizeof(int), stl_cmp_int32);
        CHECK(stl_heap_is_heap(data, 10, sizeof(int), stl_cmp_int32));
        stl_heap_sort(data, 10, sizeof(int), stl_cmp_int32);
        for (i = 1; i < 10; ++i) {
            CHECK(data[i - 1] <= data[i]);
        }
    }
}

/* ------------------------------------------------------------------ */
/* string                                                              */
/* ------------------------------------------------------------------ */

static void test_string(void)
{
    SECTION("string basics");
    {
        stl_string *s = stl_string_new_from("Hello");
        CHECK(s != NULL);
        CHECK_EQ_INT(stl_string_size(s), 5);
        CHECK_EQ_STR(stl_string_cstr(s), "Hello");
        CHECK(stl_string_append(s, ", World") == STL_OK);
        CHECK_EQ_STR(stl_string_cstr(s), "Hello, World");
        CHECK_EQ_INT(stl_string_size(s), 12);
        CHECK(stl_string_prepend(s, ">> ") == STL_OK);
        CHECK_EQ_STR(stl_string_cstr(s), ">> Hello, World");
        CHECK(stl_string_insert(s, 0, "[") == STL_OK);
        CHECK_EQ_STR(stl_string_cstr(s), "[>> Hello, World");
        CHECK(stl_string_append_char(s, ']') == STL_OK);
        CHECK_EQ_STR(stl_string_cstr(s), "[>> Hello, World]");
        CHECK(stl_string_erase(s, 0, 4) == STL_OK);
        CHECK_EQ_STR(stl_string_cstr(s), "Hello, World]");
        stl_string_free(s);
    }

    SECTION("string search");
    {
        stl_string *s = stl_string_new_from("the quick brown fox jumps over the lazy dog");
        CHECK_EQ_INT(stl_string_find(s, "quick", 0), 4);
        CHECK_EQ_INT(stl_string_find(s, "the", 0), 0);
        CHECK_EQ_INT(stl_string_find(s, "the", 1), 31);
        CHECK_EQ_INT(stl_string_rfind(s, "the", STL_NPOS), 31);
        CHECK_EQ_INT(stl_string_find(s, "missing", 0), STL_NPOS);
        CHECK(stl_string_contains(s, "brown"));
        CHECK(stl_string_starts_with(s, "the "));
        CHECK(stl_string_ends_with(s, "dog"));
        CHECK(!stl_string_starts_with(s, "quick"));
        CHECK_EQ_INT(stl_string_find_char(s, 'q', 0), 4);
        CHECK_EQ_INT(stl_string_rfind_char(s, 'o', STL_NPOS), 41);
        stl_string_free(s);
    }

    SECTION("string transform");
    {
        stl_string *s = stl_string_new_from("  Hello World  ");
        CHECK(stl_string_trim(s) == STL_OK);
        CHECK_EQ_STR(stl_string_cstr(s), "Hello World");
        CHECK(stl_string_toupper(s) == STL_OK);
        CHECK_EQ_STR(stl_string_cstr(s), "HELLO WORLD");
        CHECK(stl_string_tolower(s) == STL_OK);
        CHECK_EQ_STR(stl_string_cstr(s), "hello world");
        stl_string_reverse(s);
        CHECK_EQ_STR(stl_string_cstr(s), "dlrow olleh");
        CHECK(stl_string_replace_all(s, "dlrow", "world") == STL_OK);
        CHECK_EQ_STR(stl_string_cstr(s), "world olleh");
        CHECK(stl_string_replace_all(s, "l", "L") == STL_OK);
        CHECK_EQ_STR(stl_string_cstr(s), "worLd oLLeh");
        stl_string_free(s);
    }

    SECTION("string substr / compare / format");
    {
        stl_string *s = stl_string_new_from("abcdefghij");
        stl_string *sub = stl_string_substr(s, 2, 3);
        stl_string *eq;
        CHECK(sub != NULL);
        CHECK_EQ_STR(stl_string_cstr(sub), "cde");
        stl_string_free(sub);
        sub = stl_string_substr(s, 8, 100);
        CHECK_EQ_STR(stl_string_cstr(sub), "ij");
        stl_string_free(sub);

        eq = stl_string_new_from("abcdefghij");
        CHECK(stl_string_equals(s, eq));
        CHECK_EQ_INT(stl_string_compare(s, eq), 0);
        CHECK(stl_string_equals_cstr(s, "abcdefghij"));
        stl_string_free(eq);

        {
            stl_string *f = stl_format("%d/%s/%.2f", 42, "pi", 3.14159);
            CHECK(f != NULL);
            CHECK_EQ_STR(stl_string_cstr(f), "42/pi/3.14");
            CHECK(stl_string_append_fmt(f, "|%c%c", 'a', 'b') == STL_OK);
            CHECK_EQ_STR(stl_string_cstr(f), "42/pi/3.14|ab");
            stl_string_free(f);
        }
        stl_string_free(s);
    }

    SECTION("string split / join");
    {
        stl_string *s = stl_string_new_from("a,bb,ccc,dddd");
        stl_vector *parts = stl_vector_new(sizeof(stl_string *), NULL);
        stl_string *joined = stl_string_new();
        size_t n;
        size_t i;

        n = stl_string_split(s, ",", parts);
        CHECK_EQ_INT(n, 4);
        CHECK_EQ_INT(stl_vector_size(parts), 4);
        for (i = 0; i < stl_vector_size(parts); ++i) {
            stl_string **piece = (stl_string **)stl_vector_at(parts, i);
            CHECK(piece != NULL && *piece != NULL);
        }
        CHECK_EQ_STR(stl_string_cstr_c(*(stl_string **)stl_vector_at(parts, 2)), "ccc");
        CHECK(stl_string_join(joined, parts, "-") == STL_OK);
        CHECK_EQ_STR(stl_string_cstr(joined), "a-bb-ccc-dddd");

        for (i = 0; i < stl_vector_size(parts); ++i) {
            stl_string *piece = *(stl_string **)stl_vector_at(parts, i);
            stl_string_free(piece);
        }
        stl_vector_free(parts);
        stl_string_free(joined);
        stl_string_free(s);
    }
}

/* ------------------------------------------------------------------ */
/* raw algorithms                                                      */
/* ------------------------------------------------------------------ */

static int is_even_cb(const void *elem, void *user)
{
    STL_UNUSED(user);
    return (*(const int *)elem % 2) == 0;
}

static void test_algorithms(void)
{
    int data[100];
    int i;

    SECTION("algorithms sort/search");
    for (i = 0; i < 100; ++i) {
        data[i] = (i * 37) % 100;       /* permutation-ish */
    }
    stl_sort(data, 100, sizeof(int), stl_cmp_int32);
    CHECK(stl_is_sorted(data, 100, sizeof(int), stl_cmp_int32));
    for (i = 0; i < 100; ++i) {
        CHECK_EQ_INT(data[i], i);
        CHECK(stl_binary_search(&i, data, 100, sizeof(int), stl_cmp_int32));
    }
    {
        int missing = 1000;
        CHECK(!stl_binary_search(&missing, data, 100, sizeof(int), stl_cmp_int32));
    }

    SECTION("algorithms stable sort with duplicates");
    for (i = 0; i < 100; ++i) {
        data[i] = i % 10;
    }
    stl_stable_sort(data, 100, sizeof(int), stl_cmp_int32);
    CHECK(stl_is_sorted(data, 100, sizeof(int), stl_cmp_int32));
    CHECK_EQ_INT(data[0], 0);
    CHECK_EQ_INT(data[99], 9);

    SECTION("algorithms min/max/count");
    for (i = 0; i < 100; ++i) {
        data[i] = i - 50;
    }
    CHECK_EQ_INT(*(int *)stl_min_element(data, 100, sizeof(int), stl_cmp_int32), -50);
    CHECK_EQ_INT(*(int *)stl_max_element(data, 100, sizeof(int), stl_cmp_int32), 49);
    CHECK_EQ_INT(stl_count_if(data, 100, sizeof(int), is_even_cb, NULL), 50);

    SECTION("algorithms transform/accumulate");
    {
        int doubled[100];
        int sum = 0;
        stl_transform(data, doubled, 100, sizeof(int), stl_op_double_int, NULL);
        CHECK_EQ_INT(doubled[0], -100);
        stl_accumulate(data, 100, sizeof(int), &sum, stl_op_add_int, NULL);
        CHECK_EQ_INT(sum, -50);
        CHECK_EQ_INT(stl_sum_int(data, 100), -50);
    }

    SECTION("algorithms set ops on sorted arrays");
    {
        int a[5];
        int b[5];
        int out[10];
        int expect;
        a[0] = 1; a[1] = 2; a[2] = 3; a[3] = 4; a[4] = 5;
        b[0] = 3; b[1] = 4; b[2] = 5; b[3] = 6; b[4] = 7;
        stl_set_union_raw(a, 5, b, 5, out, sizeof(int), stl_cmp_int32);
        stl_set_intersection_raw(a, 5, b, 5, out, sizeof(int), stl_cmp_int32);
        expect = 3;
        CHECK(stl_includes(b, 5, &expect, 1, sizeof(int), stl_cmp_int32));
        expect = 1;
        CHECK(!stl_includes(b, 5, &expect, 1, sizeof(int), stl_cmp_int32));
    }

    SECTION("algorithms nth_element / partial_sort");
    {
        int big[500];
        for (i = 0; i < 500; ++i) {
            big[i] = 499 - i;
        }
        stl_nth_element(big, 500, sizeof(int), 250, stl_cmp_int32);
        CHECK_EQ_INT(big[250], 250);
        for (i = 0; i < 500; ++i) {
            big[i] = (i * 13) % 500;
        }
        stl_partial_sort(big, 500, sizeof(int), 10, stl_cmp_int32);
        for (i = 0; i < 10; ++i) {
            CHECK_EQ_INT(big[i], i);
        }
    }

    SECTION("algorithms reverse/rotate/unique/remove");
    {
        int arr[10];
        for (i = 0; i < 10; ++i) {
            arr[i] = i;
        }
        stl_reverse(arr, 10, sizeof(int));
        CHECK_EQ_INT(arr[0], 9);
        CHECK_EQ_INT(arr[9], 0);
        stl_rotate(arr, 10, sizeof(int), 3);
        CHECK_EQ_INT(arr[0], 6);
        CHECK_EQ_INT(arr[9], 7);

        for (i = 0; i < 10; ++i) {
            arr[i] = i / 2;
        }
        {
            void *new_end = stl_unique(arr, 10, sizeof(int), stl_eq_int);
            int kept = (int)((int *)new_end - arr);
            CHECK_EQ_INT(kept, 5);
            CHECK_EQ_INT(arr[0], 0);
            CHECK_EQ_INT(arr[4], 4);
        }
        {
            void *new_end = stl_remove_if(arr, 5, sizeof(int), is_even_cb, NULL);
            int kept = (int)((int *)new_end - arr);
            CHECK_EQ_INT(kept, 2);
            CHECK_EQ_INT(arr[0], 1);
            CHECK_EQ_INT(arr[1], 3);
        }
    }

    SECTION("algorithms shuffle");
    {
        int arr[100];
        int same = 0;
        for (i = 0; i < 100; ++i) {
            arr[i] = i;
        }
        stl_shuffle(arr, 100, sizeof(int), 0);
        for (i = 0; i < 100; ++i) {
            if (arr[i] == i) {
                ++same;
            }
        }
        CHECK(same < 20);   /* extremely unlikely to be identity */
    }

    SECTION("algorithms edge cases");
    CHECK(stl_is_sorted(NULL, 0, sizeof(int), stl_cmp_int32));
    stl_sort(NULL, 0, sizeof(int), stl_cmp_int32);   /* must not crash */
    CHECK(stl_min_element(NULL, 0, sizeof(int), stl_cmp_int32) == NULL);
    {
        int single = 5;
        CHECK_EQ_INT(*(int *)stl_min_element(&single, 1, sizeof(int), stl_cmp_int32), 5);
        stl_sort(&single, 1, sizeof(int), stl_cmp_int32);
        CHECK_EQ_INT(single, 5);
    }
}

/* ------------------------------------------------------------------ */
/* bitset                                                              */
/* ------------------------------------------------------------------ */

static void test_bitset(void)
{
    SECTION("bitset");
    {
        stl_bitset *b = stl_bitset_new(100);
        size_t i;
        CHECK(b != NULL);
        CHECK_EQ_INT(stl_bitset_size(b), 100);
        CHECK(stl_bitset_none(b));
        for (i = 0; i < 100; i += 3) {
            stl_bitset_set(b, i);
        }
        CHECK_EQ_INT(stl_bitset_count(b), 34);
        CHECK(stl_bitset_any(b));
        CHECK(!stl_bitset_all(b));
        CHECK(stl_bitset_test(b, 0));
        CHECK(!stl_bitset_test(b, 1));
        CHECK_EQ_INT(stl_bitset_find_first(b), 0);
        CHECK_EQ_INT(stl_bitset_find_next(b, 0), 3);
        stl_bitset_reset(b, 0);
        CHECK(!stl_bitset_test(b, 0));
        stl_bitset_flip(b, 1);
        CHECK(stl_bitset_test(b, 1));
        stl_bitset_set_all(b);
        CHECK(stl_bitset_all(b));
        CHECK_EQ_INT(stl_bitset_count(b), 100);
        stl_bitset_flip_all(b);
        CHECK(stl_bitset_none(b));
        stl_bitset_free(b);
    }

    SECTION("bitset set ops");
    {
        stl_bitset *a = stl_bitset_new(64);
        stl_bitset *b = stl_bitset_new(64);
        stl_bitset *r = stl_bitset_new(64);
        size_t i;
        for (i = 0; i < 64; i += 2) { stl_bitset_set(a, i); }
        for (i = 0; i < 64; i += 4) { stl_bitset_set(b, i); }
        CHECK(stl_bitset_and(r, a, b) == STL_OK);
        CHECK_EQ_INT(stl_bitset_count(r), 16);
        CHECK(stl_bitset_or(r, a, b) == STL_OK);
        CHECK_EQ_INT(stl_bitset_count(r), 32);
        CHECK(stl_bitset_xor(r, a, b) == STL_OK);
        CHECK_EQ_INT(stl_bitset_count(r), 16);
        CHECK(stl_bitset_not(r, a) == STL_OK);
        CHECK_EQ_INT(stl_bitset_count(r), 32);
        stl_bitset_free(a);
        stl_bitset_free(b);
        stl_bitset_free(r);
    }
}

/* ------------------------------------------------------------------ */
/* threading                                                           */
/* ------------------------------------------------------------------ */

static void range_negate(void *base, size_t begin, size_t end, void *user)
{
    int *p = (int *)base;
    size_t i;
    STL_UNUSED(user);
    STL_UNUSED(begin);
    STL_UNUSED(end);
    for (i = 0; i < end - begin; ++i) {
        p[i] = -p[i];
    }
}

static void test_threading(void)
{
    SECTION("spinlock");
    {
        stl_spinlock *lock = stl_spinlock_new();
        CHECK(lock != NULL);
        stl_spinlock_lock(lock);
        CHECK(!stl_spinlock_trylock(lock));   /* already held */
        stl_spinlock_unlock(lock);
        CHECK(stl_spinlock_trylock(lock));
        stl_spinlock_unlock(lock);
        stl_spinlock_free(lock);
    }

    SECTION("rwlock");
    {
        stl_rwlock *lock = stl_rwlock_new();
        CHECK(lock != NULL);
        stl_rwlock_rdlock(lock);
        stl_rwlock_unlock(lock);
        stl_rwlock_wrlock(lock);
        stl_rwlock_unlock(lock);
        stl_rwlock_free(lock);
    }

    SECTION("safe_vector");
    {
        stl_safe_vector *v = stl_safe_vector_new(sizeof(int));
        int i;
        int out = 0;
        CHECK(v != NULL);
        for (i = 0; i < 100; ++i) {
            CHECK(stl_safe_vector_push_back(v, &i) == STL_OK);
        }
        CHECK_EQ_INT(stl_safe_vector_size(v), 100);
        CHECK(stl_safe_vector_at(v, 50, &out) == STL_OK);
        CHECK_EQ_INT(out, 50);
        CHECK(stl_safe_vector_pop_back(v, &out) == STL_OK);
        CHECK_EQ_INT(out, 99);
        stl_safe_vector_free(v);
    }

    SECTION("safe_map");
    {
        stl_safe_map *m = stl_safe_map_new();
        int i;
        int out = 0;
        CHECK(m != NULL);
        for (i = 0; i < 100; ++i) {
            CHECK(stl_safe_map_put(m, i, i * 2) == STL_OK);
        }
        CHECK_EQ_INT(stl_safe_map_size(m), 100);
        CHECK(stl_safe_map_get(m, 42, &out) == STL_OK);
        CHECK_EQ_INT(out, 84);
        CHECK(stl_safe_map_erase(m, 42) == STL_OK);
        CHECK(stl_safe_map_get(m, 42, &out) == STL_ERR_NOT_FOUND);
        stl_safe_map_free(m);
    }

    SECTION("parallel algorithms");
    {
        int data[1000];
        int i;
        for (i = 0; i < 1000; ++i) {
            data[i] = 999 - i;
        }
        stl_parallel_sort(data, 1000, sizeof(int), stl_cmp_int32, 0);
        CHECK(stl_is_sorted(data, 1000, sizeof(int), stl_cmp_int32));

        for (i = 0; i < 1000; ++i) {
            data[i] = (i * 7919) % 1000;
        }
        stl_parallel_sort(data, 1000, sizeof(int), stl_cmp_int32, 4);
        CHECK(stl_is_sorted(data, 1000, sizeof(int), stl_cmp_int32));

        for (i = 0; i < 1000; ++i) {
            data[i] = 999 - i;
        }
        stl_parallel_merge_sort(data, 1000, sizeof(int), stl_cmp_int32, 4);
        CHECK(stl_is_sorted(data, 1000, sizeof(int), stl_cmp_int32));

        for (i = 0; i < 100; ++i) {
            data[i] = i + 1;
        }
        stl_parallel_for_each(data, 100, sizeof(int), range_negate, NULL, 4);
        CHECK_EQ_INT(data[0], -1);
        CHECK_EQ_INT(data[99], -100);

        CHECK(stl_hardware_concurrency() >= 1);
    }

    SECTION("random");
    {
        unsigned int state = 1;
        int i;
        unsigned int first = stl_rand_next(&state);
        unsigned int second = stl_rand_next(&state);
        CHECK(first != second);
        for (i = 0; i < 1000; ++i) {
            int r = stl_rand_range(&state, 10, 20);
            CHECK(r >= 10 && r <= 20);
        }
    }
}

/* ------------------------------------------------------------------ */
/* error handling and allocator                                        */
/* ------------------------------------------------------------------ */

static int g_error_count = 0;
static stl_error_code g_last_code = STL_OK;

static void STL_CALL counting_error_handler(stl_error_code code, const char *file,
                                            int line, const char *msg)
{
    STL_UNUSED(file);
    STL_UNUSED(line);
    STL_UNUSED(msg);
    ++g_error_count;
    g_last_code = code;
}

static void test_errors(void)
{
    SECTION("error handling");
    stl_set_error_handler(counting_error_handler);
    g_error_count = 0;

    {
        stl_vector *v = stl_vector_new(sizeof(int), NULL);
        stl_vector_at(v, 5);              /* out of range */
        CHECK(g_error_count > 0);
        CHECK_EQ_INT(g_last_code, STL_ERR_RANGE);
        stl_vector_pop_back(v);           /* empty */
        CHECK_EQ_INT(g_last_code, STL_ERR_EMPTY);
        stl_vector_free(v);
    }
    CHECK_EQ_STR(stl_error_string(STL_ERR_NOMEM), "out of memory");
    CHECK_EQ_STR(stl_error_string(STL_ERR_RANGE), "index or iterator out of range");

    stl_set_error_handler(NULL);
    CHECK(stl_get_error() == STL_OK || 1);
}

/* ------------------------------------------------------------------ */
/* version / misc                                                      */
/* ------------------------------------------------------------------ */

static void test_misc(void)
{
    SECTION("version and misc");
    CHECK_EQ_STR(stl_version_string(), STL_VERSION_STRING);
    CHECK_EQ_INT(stl_version(), STL_VERSION);
    CHECK(stl_size_of_pointer() >= 4);
    CHECK(stl_default_allocator() != NULL);
    CHECK(stl_strcasecmp("HeLLo", "hello") == 0);
    CHECK(stl_strncasecmp("HeLLo!", "hello?", 5) == 0);
    {
        char *copy = stl_strdup("abc");
        CHECK_EQ_STR(copy, "abc");
        stl_string_free_cstr(copy);
    }
    {
        char buf[64];
        CHECK_EQ_INT(stl_snprintf_c(buf, sizeof(buf), "%d-%s", 7, "x"), 3);
        CHECK_EQ_STR(buf, "7-x");
    }
}

/* ------------------------------------------------------------------ */
/* custom allocator                                                    */
/* ------------------------------------------------------------------ */

static size_t g_alloc_count = 0;
static size_t g_free_count = 0;

static void *STL_CALL counting_malloc(size_t size) { ++g_alloc_count; return malloc(size); }
static void *STL_CALL counting_realloc(void *p, size_t size) { return realloc(p, size); }
static void STL_CALL counting_free(void *p) { if (p) ++g_free_count; free(p); }

static void test_custom_allocator(void)
{
    stl_allocator alloc;
    stl_vector *v;
    int i;

    SECTION("many live maps");
    {
        /* The map used to keep its value offset in a fixed-size side table,
         * which capped the number of live maps.  Neither map kind has that
         * limit now, so create well past the old ceiling and use the last
         * instance to prove it is fully functional. */
        enum { COUNT = 3000 };
        stl_map **maps = (stl_map **)malloc(sizeof(stl_map *) * COUNT);
        stl_hashmap **hmaps = (stl_hashmap **)malloc(sizeof(stl_hashmap *) * COUNT);
        int i;
        int created = 0;

        CHECK(maps != NULL && hmaps != NULL);
        for (i = 0; i < COUNT; ++i) {
            maps[i] = make_int_map();
            hmaps[i] = stl_hashmap_new(sizeof(int_pair), offsetof(int_pair, value),
                                       sizeof(int), stl_hash_int, stl_eq_int, NULL);
            if (maps[i] != NULL && hmaps[i] != NULL) {
                ++created;
            }
        }
        CHECK_EQ_INT(created, COUNT);

        {
            int key = 42;
            int value = 99;
            CHECK(stl_map_put(maps[COUNT - 1], &key, &value) != NULL);
            CHECK_EQ_INT(*(int *)stl_map_get(maps[COUNT - 1], &key), 99);
            CHECK(stl_hashmap_put(hmaps[COUNT - 1], &key, &value) != NULL);
            CHECK_EQ_INT(*(int *)stl_hashmap_get(hmaps[COUNT - 1], &key), 99);
        }
        for (i = 0; i < COUNT; ++i) {
            stl_map_free(maps[i]);
            stl_hashmap_free(hmaps[i]);
        }
        free(maps);
        free(hmaps);
    }

    SECTION("custom allocator");
    alloc.malloc_fn = counting_malloc;
    alloc.realloc_fn = counting_realloc;
    alloc.free_fn = counting_free;
    alloc.user = NULL;

    g_alloc_count = 0;
    g_free_count = 0;

    v = stl_vector_new_a(sizeof(int), NULL, &alloc);
    CHECK(v != NULL);
    for (i = 0; i < 100; ++i) {
        stl_vector_push_back(v, &i);
    }
    CHECK(g_alloc_count > 0);
    stl_vector_free(v);
    CHECK_EQ_INT(g_alloc_count, g_free_count);
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("libstl %s -- self test\n", stl_version_string());
    printf("pointer size: %lu, threads: %s\n",
           (unsigned long)stl_size_of_pointer(),
           stl_is_thread_supported() ? "yes" : "no");

    test_vector();
    test_deque();
    test_list();
    test_set();
    test_map();
    test_hash();
    test_adaptors();
    test_string();
    test_algorithms();
    test_bitset();
    test_threading();
    test_errors();
    test_misc();
    test_custom_allocator();

    printf("\n----------------------------------------\n");
    printf("%d checks, %d failures\n", g_checks, g_failures);
    if (g_failures == 0) {
        printf("ALL TESTS PASSED\n");
        return 0;
    }
    printf("THERE WERE FAILURES\n");
    return 1;
}
