/*
 * 01_containers.c -- the sequence containers, side by side.
 *
 * Demonstrates vector, deque and list on the same task, and shows why the
 * choice between them matters.  Everything here uses only the plain API; the
 * macro layer is covered in 05_macros.c.
 *
 * Build:
 *     cc -std=c11 -I.. -o 01_containers 01_containers.c ../build/libstl.a -lpthread -lm
 */

#include "libstl.h"

#include <stdio.h>
#include <string.h>

static void print_ints(const char *label, const int *data, size_t count)
{
    size_t i;

    printf("  %-22s [", label);
    for (i = 0; i < count; ++i) {
        printf("%s%d", (i == 0) ? "" : " ", data[i]);
    }
    printf("]\n");
}

/* ------------------------------------------------------------------ */
/* vector: contiguous, so it also works as a plain array               */
/* ------------------------------------------------------------------ */

static void vector_demo(void)
{
    stl_vector *v = stl_vector_new(sizeof(int), NULL);
    int i;

    printf("vector\n");

    for (i = 1; i <= 10; ++i) {
        stl_vector_push_back(v, &i);
    }
    print_ints("after 10 pushes", (const int *)stl_vector_data_c(v), stl_vector_size(v));

    /* The storage is contiguous, so it can be handed to any C API. */
    stl_sort(stl_vector_data(v), stl_vector_size(v), sizeof(int), stl_cmp_int32);
    print_ints("sorted", (const int *)stl_vector_data_c(v), stl_vector_size(v));

    {
        int key = 7;
        size_t at = stl_vector_lower_bound(v, &key, stl_cmp_int32);
        printf("  lower_bound(7)         index %lu\n", (unsigned long)at);
    }

    /* Insertion in the middle moves the tail: O(n). */
    {
        int zero = 0;
        stl_vector_insert(v, 0, &zero);
        print_ints("inserted 0 at front", (const int *)stl_vector_data_c(v), stl_vector_size(v));
    }

    printf("  size %lu, capacity %lu\n",
           (unsigned long)stl_vector_size(v),
           (unsigned long)stl_vector_capacity(v));

    stl_vector_free(v);
}

/* ------------------------------------------------------------------ */
/* deque: cheap at both ends                                           */
/* ------------------------------------------------------------------ */

static void deque_demo(void)
{
    stl_deque *d = stl_deque_new(sizeof(int), NULL);
    int i;
    int out[6];

    printf("\ndeque\n");

    for (i = 3; i <= 5; ++i) {
        stl_deque_push_back(d, &i);      /* 3 4 5 */
    }
    for (i = 2; i >= 1; --i) {
        stl_deque_push_front(d, &i);     /* 1 2 3 4 5 */
    }

    for (i = 0; i < 5; ++i) {
        out[i] = *(int *)stl_deque_at(d, (size_t)i);
    }
    print_ints("pushed at both ends", out, 5);

    /* A ring of blocks means the ends can be removed without moving anything. */
    stl_deque_pop_front(d);
    stl_deque_pop_back(d);
    printf("  front %d, back %d, size %lu\n",
           *(int *)stl_deque_front(d), *(int *)stl_deque_back(d),
           (unsigned long)stl_deque_size(d));

    stl_deque_free(d);
}

/* ------------------------------------------------------------------ */
/* list: O(1) insertion anywhere, stable element addresses             */
/* ------------------------------------------------------------------ */

static void list_demo(void)
{
    stl_list *l = stl_list_new(sizeof(int), NULL);
    stl_list_node *second = NULL;
    int i;

    printf("\nlist\n");

    for (i = 1; i <= 5; ++i) {
        stl_list_node *node = stl_list_push_back(l, &i);
        if (i == 2) {
            second = node;
        }
    }

    /* Element addresses stay valid until the node itself is erased, so a
     * handle taken at insert time can be used much later. */
    {
        int splice_in = 99;
        stl_list_insert_after(l, second, &splice_in);
    }
    printf("  inserted 99 after the 2nd node\n");
    printf("  the handle still points at %d\n", *(int *)stl_list_node_data(second));

    /* Erasing is O(1) given the handle, and does not disturb other nodes. */
    stl_list_erase_node(l, second);
    printf("  erased that node; size is now %lu\n", (unsigned long)stl_list_size(l));

    {
        int sum = 0;
        stl_list_node *n;
        for (n = stl_list_begin_node(l); n != NULL; n = stl_list_node_next(n)) {
            sum += *(int *)stl_list_node_data(n);
        }
        printf("  sum of the rest %d\n", sum);
    }

    /* sort and unique work on the elements, keeping node identity intact. */
    {
        int dup = 5;
        stl_list_push_back(l, &dup);
    }
    stl_list_sort(l, stl_cmp_int32);
    stl_list_unique(l, stl_eq_int);
    printf("  sorted and de-duplicated, size %lu\n", (unsigned long)stl_list_size(l));

    stl_list_free(l);
}

/* ------------------------------------------------------------------ */
/* string: owned, growable, and still usable as a C string             */
/* ------------------------------------------------------------------ */

static void string_demo(void)
{
    stl_string *s = stl_string_new_from("the quick brown fox");

    printf("\nstring\n");

    stl_string_append(s, " jumps over the lazy dog");
    printf("  \"%s\"\n", stl_string_cstr(s));
    printf("  length %lu, capacity %lu\n",
           (unsigned long)stl_string_size(s),
           (unsigned long)stl_string_capacity(s));

    /* Every string is NUL-terminated, so C functions accept it directly. */
    printf("  strchr('q') at offset %ld\n",
           (long)(strchr(stl_string_cstr(s), 'q') - stl_string_cstr(s)));

    {
        size_t at = stl_string_find(s, "fox", 0);
        stl_string *word = stl_string_substr(s, at, 3);
        printf("  find(\"fox\") at %lu, substr \"%s\"\n",
               (unsigned long)at, stl_string_cstr(word));
        stl_string_free(word);
    }

    stl_string_toupper(s);
    printf("  upper \"%s\"\n", stl_string_cstr(s));

    stl_string_free(s);
}

int main(void)
{
    printf("libstl %s -- sequence containers\n\n", stl_version_string());

    vector_demo();
    deque_demo();
    list_demo();
    string_demo();

    return 0;
}
