/*
 * 05_macros.c -- the convenience macro layer.
 *
 * The macros are function-style and lower-case, so a call site reads much like
 * C++ without any of them being a real symbol.  This example covers the ones
 * that change the shape of the code most: typed access, iteration, generated
 * comparators, and one-line container construction.
 *
 * Build:
 *     cc -std=c11 -I.. -o 05_macros 05_macros.c ../build/libstl.a -lpthread -lm
 *
 * The scope-exit helpers (stl_autofree, stl_string_scope) need a compiler
 * extension and are opt-in, so they are exercised separately:
 *     cc -std=gnu11 -DSTL_ENABLE_CLEANUP -I.. -o 05_macros 05_macros.c ...
 */

#include "libstl.h"

#include <stdio.h>

/* Declare a key/value pair type together with its key comparator. */
stl_define_pair(int_pair, int, int);

typedef struct point {
    int x;
    int y;
} point;

/* Generate a comparator and an equality function from a field name.  The
 * generated functions are static and unused-safe. */
stl_define_cmp_fn(point_cmp_x, point, x);
stl_define_eq_fn(point_eq_x, point, x);

static void typed_access_demo(void)
{
    stl_vector *v = stl_vector_new(sizeof(point), NULL);
    point *p;

    printf("typed access\n");

    /* Push without declaring a temporary: the macro builds one for you. */
    stl_vector_push_literal(v, point, { .x = 1, .y = 10 });
    stl_vector_push_literal(v, point, { .x = 2, .y = 20 });
    stl_vector_push_literal(v, point, { .x = 3, .y = 30 });

    /* The accessor checks that the element size matches T, so a wrong type
     * yields NULL instead of reading past the element. */
    p = stl_vector_at_t(v, point, 1);
    printf("  at(1) = (%d, %d)\n", p->x, p->y);
    printf("  front = %d, back = %d\n",
           stl_vector_front_t(v, point)->x,
           stl_vector_back_t(v, point)->x);

    /* Asking for the wrong type fails loudly rather than silently. */
    printf("  at_t(v, int, 0) is %s\n",
           (stl_vector_at_t(v, int, 0) == NULL) ? "NULL, as it should be" : "wrong");

    stl_vector_free(v);
}

static void iteration_demo(void)
{
    stl_list *l = stl_list_new(sizeof(int), NULL);
    stl_vector *v = stl_vector_new(sizeof(int), NULL);
    int i;

    printf("\niteration\n");

    for (i = 1; i <= 6; ++i) {
        stl_list_push_back_t(l, int, i * i);
        stl_vector_push(v, int, i);
    }

    /* The macro declares the loop variable; it is a T*. */
    {
        int *n;
        printf("  list:");
        stl_list_foreach_t(l, int, n) {
            printf(" %d", *n);
        }
        printf("\n");
    }

    /* Erasing the current node is safe: the successor is fetched first. */
    {
        int *n;
        stl_list_foreach_safe(l, int, n, node) {
            if (*n % 2 == 0) {
                stl_list_erase_node(l, node);
            }
        }
        printf("  after removing even values:");
        {
            int *m;
            stl_list_foreach_t(l, int, m) {
                printf(" %d", *m);
            }
        }
        printf("\n");
    }

    /* Indexed and reverse walks. */
    {
        int *n;
        printf("  vector with index:");
        stl_vector_foreach_idx(v, int, n, idx) {
            printf(" %lu->%d", (unsigned long)idx, *n);
        }
        printf("\n");
    }
    {
        int *n;
        printf("  vector reversed:");
        stl_vector_foreach_rev(v, int, n) {
            printf(" %d", *n);
        }
        printf("\n");
    }

    stl_list_free(l);
    stl_vector_free(v);
}

static void generated_function_demo(void)
{
    stl_set *by_x = stl_set_new_t(point, x);
    point *p;

    printf("\ncomparators from a field name\n");

    {
        point items[4];
        int i;
        items[0].x = 3; items[0].y = 0;
        items[1].x = 1; items[1].y = 0;
        items[2].x = 4; items[2].y = 0;
        items[3].x = 2; items[3].y = 0;

        for (i = 0; i < 4; ++i) {
            stl_set_insert(by_x, &items[i]);
        }
    }

    printf("  set ordered by .x:");
    stl_set_foreach_t(by_x, point, p) {
        printf(" %d", p->x);
    }
    printf("\n");

    /* The generated functions are ordinary callables, usable anywhere a
     * comparator is expected. */
    {
        point a, b;
        a.x = 1; a.y = 0;
        b.x = 2; b.y = 0;
        printf("  point_cmp_x(a, b) = %d, point_eq_x(a, a) = %d\n",
               point_cmp_x(&a, &b), point_eq_x(&a, &a));
    }

    stl_set_free(by_x);
}

static void map_macro_demo(void)
{
    stl_map *m = stl_pair_map_new(int_pair, int, int);
    int_pair *entry;

    printf("\npairs and maps\n");

    stl_map_put_pair(m, int_pair, 1, 100);
    stl_map_put_pair(m, int_pair, 2, 200);
    stl_map_put_pair(m, int_pair, 3, 300);

    stl_map_foreach_t(m, int_pair, entry) {
        printf("  %d -> %d\n", entry->key, entry->value);
    }

    stl_map_free(m);
}

static void allocation_demo(void)
{
    printf("\nallocation\n");

    {
        /* stl_new zeroes the object, like `new T` does for plain types. */
        point *p = stl_new(point);
        printf("  stl_new(point) gives (%d, %d)\n", p->x, p->y);
        stl_delete(p);
    }

    {
        int *block = stl_new_array(int, 8);
        int i;
        printf("  stl_new_array(int, 8) is zeroed:");
        for (i = 0; i < 8; ++i) printf(" %d", block[i]);
        printf("\n");
        stl_delete(block);
    }

    {
        /* stl_free_all frees several blocks and ignores NULL. */
        int *a = stl_new(int);
        int *b = NULL;
        stl_free_all(a, b);
    }

    printf("  small utilities: min %d, max %d, clamp %d, array_len %lu\n",
           stl_min_of(3, 9), stl_max_of(3, 9), stl_clamp(15, 0, 10),
           (unsigned long)stl_array_len("hello"));
}

#if STL_HAVE_CLEANUP
static void cleanup_demo(void)
{
    printf("\nscope-exit release (STL_ENABLE_CLEANUP)\n");

    {
        /* Released automatically when this block exits. */
        int *block stl_autofree = stl_new_array(int, 16);
        block[0] = 42;
        printf("  stl_autofree buffer in use: block[0] = %d\n", block[0]);
    }

    {
        stl_string_scope(s, "built with STL_ENABLE_CLEANUP");
        stl_str_append_fmt(s, " (%d)", 1);
        printf("  stl_string_scope: \"%s\"\n", stl_cstr(s));
    }
}
#endif

int main(void)
{
    printf("libstl %s -- the macro layer\n\n", stl_version_string());

    typed_access_demo();
    iteration_demo();
    generated_function_demo();
    map_macro_demo();
    allocation_demo();

#if STL_HAVE_CLEANUP
    cleanup_demo();
#else
    printf("\nscope-exit release: not enabled; rebuild with -DSTL_ENABLE_CLEANUP\n");
#endif

    return 0;
}
