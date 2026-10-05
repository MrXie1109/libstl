/*
 * 03_algorithms.c -- algorithms over arrays, and containers of structs.
 *
 * The algorithms take a raw array plus an element size, so they work on the
 * storage of any contiguous container.  This example also shows the two things
 * that trip people up when the element is a struct: supplying a comparator
 * that only looks at the fields that matter, and giving a destructor when the
 * element owns resources.
 *
 * Build:
 *     cc -std=c11 -I.. -o 03_algorithms 03_algorithms.c ../build/libstl.a -lpthread -lm
 */

#include "libstl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A struct with padding, which is exactly the case where comparing the raw
 * bytes would give the wrong answer. */
typedef struct record {
    int         id;
    int         score;
    char        tag[4];
} record;

/* Order by score descending, then by id ascending.  A comparator sees
 * `const void *`, so the first thing it does is name the type. */
static int STL_CALL record_cmp(const void *a, const void *b)
{
    const record *x = (const record *)a;
    const record *y = (const record *)b;

    if (x->score != y->score) {
        return (x->score > y->score) ? STL_LESS : STL_GREATER;
    }
    return (x->id < y->id) ? STL_LESS : ((x->id > y->id) ? STL_GREATER : STL_EQUAL);
}

/* An element that owns memory: the container copies the struct, so the
 * destructor is what keeps the strdup'd name from leaking. */
typedef struct person {
    char *name;
    int   age;
} person;

static void STL_CALL person_dtor(void *elem)
{
    person *p = (person *)elem;
    free(p->name);
    p->name = NULL;
}

static int STL_CALL person_by_age(const void *a, const void *b)
{
    return stl_cmp_int32(&((const person *)a)->age, &((const person *)b)->age);
}

static int STL_CALL age_over_30(const void *elem, void *user)
{
    const person *p = (const person *)elem;
    int threshold = (user != NULL) ? *(const int *)user : 30;
    return p->age > threshold;
}

static void print_records(const char *label, const record *r, size_t n)
{
    size_t i;

    printf("  %-24s", label);
    for (i = 0; i < n; ++i) {
        printf(" %d/%d", r[i].id, r[i].score);
    }
    printf("\n");
}

/* ------------------------------------------------------------------ */

static void sorting_demo(void)
{
    record data[8];
    int i;

    printf("sorting structs\n");

    for (i = 0; i < 8; ++i) {
        data[i].id = i;
        data[i].score = (i * 7) % 5;        /* 0 2 4 1 3 0 2 4 */
        memcpy(data[i].tag, "xx", 3);
    }
    print_records("input", data, 8);

    stl_sort(data, 8, sizeof(record), record_cmp);
    print_records("by score desc, id asc", data, 8);

    printf("  is_sorted %d\n", stl_is_sorted(data, 8, sizeof(record), record_cmp));

    /* The top three, without sorting the rest. */
    stl_partial_sort(data, 8, sizeof(record), 3, record_cmp);
    print_records("top 3 only", data, 3);
}

static void search_demo(void)
{
    int numbers[16];
    int i;

    printf("\nsearching\n");

    for (i = 0; i < 16; ++i) {
        numbers[i] = i * 3;                 /* 0 3 6 ... 45 */
    }

    {
        int key = 21;
        if (stl_binary_search(&key, numbers, 16, sizeof(int), stl_cmp_int32)) {
            int *hit = (int *)stl_lower_bound(&key, numbers, 16, sizeof(int), stl_cmp_int32);
            printf("  binary_search(21) found at index %ld\n", (long)(hit - numbers));
        } else {
            printf("  binary_search(21) absent\n");
        }
    }

    {
        int key = 21;
        printf("  lower_bound(21) index %ld\n",
               (long)((int *)stl_lower_bound(&key, numbers, 16, sizeof(int), stl_cmp_int32) - numbers));
        printf("  upper_bound(21) index %ld\n",
               (long)((int *)stl_upper_bound(&key, numbers, 16, sizeof(int), stl_cmp_int32) - numbers));
    }

    {
        int wanted = 20;    /* not in the array */
        printf("  binary_search(20) %s\n",
               stl_binary_search(&wanted, numbers, 16, sizeof(int), stl_cmp_int32) ? "found" : "absent");
    }

    /* min/max in one pass */
    {
        int *lo = NULL, *hi = NULL;
        stl_min_max_element(numbers, 16, sizeof(int), stl_cmp_int32, (void **)&lo, (void **)&hi);
        printf("  min %d, max %d\n", *lo, *hi);
    }

    /* predicates */
    {
        int threshold = 20;
        printf("  count(greater than 20) %lu\n",
               (unsigned long)stl_count_if(numbers, 16, sizeof(int),
                                           stl_pred_greater_than_int, &threshold));
        printf("  any even %d, all even %d\n",
               stl_any_of(numbers, 16, sizeof(int), stl_pred_is_even_int, NULL),
               stl_all_of(numbers, 16, sizeof(int), stl_pred_is_even_int, NULL));
    }
}

static void set_operations_demo(void)
{
    int a[5];
    int b[4];
    int out[9];
    int i;

    printf("\nset operations on sorted arrays\n");

    for (i = 0; i < 5; ++i) { a[i] = (i + 1) * 2; }      /* 2 4 6 8 10 */
    for (i = 0; i < 4; ++i) { b[i] = (i + 2) * 2; }      /* 4 6 8 10   */

    {
        int *end = (int *)stl_set_intersection_raw(a, 5, b, 4, out, sizeof(int), stl_cmp_int32);
        printf("  intersection:");
        for (i = 0; i < (int)(end - out); ++i) printf(" %d", out[i]);
        printf("\n");
    }
    {
        int *end = (int *)stl_set_difference_raw(b, 4, a, 5, out, sizeof(int), stl_cmp_int32);
        printf("  b minus a:    %ld element(s)\n", (long)(end - out));
    }
    {
        int extra = 7;   /* odd, so not present in a */
        printf("  includes([7]) %d\n", stl_includes(a, 5, &extra, 1, sizeof(int), stl_cmp_int32));
    }
}

static void accumulate_demo(void)
{
    int values[10];
    int i;
    int sum = 0;
    int product = 0;

    printf("\nfolding\n");

    for (i = 0; i < 10; ++i) {
        values[i] = i + 1;
    }

    stl_accumulate(values, 10, sizeof(int), &sum, stl_op_add_int, NULL);
    printf("  sum    %d (helper says %ld)\n", sum, stl_sum_int(values, 10));

    /* reduce starts from an explicit seed. */
    {
        int seed = 1;
        stl_reduce(values, 4, sizeof(int), &seed, &product, stl_op_max_int, NULL);
        printf("  max of first 4 %d\n", product);
    }

    {
        double d[4];
        d[0] = 1.5; d[1] = 2.5; d[2] = 3.0; d[3] = 4.0;
        printf("  double mean %.2f\n", stl_mean_double(d, 4));
    }
}

static void owning_elements_demo(void)
{
    stl_vector *people = stl_vector_new(sizeof(person), person_dtor);
    static const char *names[] = { "ada", "grace", "linus", "barbara", "alan" };
    static const int ages[] = { 36, 45, 29, 41, 33 };
    size_t i;

    printf("\nelements that own memory\n");

    for (i = 0; i < 5; ++i) {
        person p;
        p.name = stl_strdup(names[i]);      /* owned by the element */
        p.age = ages[i];
        stl_vector_push_back(people, &p);
    }

    /* Sort by age; the pointers move with the structs, so nothing leaks. */
    stl_vector_sort(people, person_by_age);
    for (i = 0; i < stl_vector_size(people); ++i) {
        const person *p = (const person *)stl_vector_at_c(people, i);
        printf("  %-8s %d\n", p->name, p->age);
    }

    /* A predicate that needs a parameter gets it through `user`. */
    {
        size_t removed;
        int threshold = 38;
        removed = stl_vector_remove_if(people, age_over_30, &threshold);
        printf("  removed %lu over %d, %lu left\n",
               (unsigned long)removed, threshold,
               (unsigned long)stl_vector_size(people));
    }

    /* One call releases every remaining name, then the storage. */
    stl_vector_free(people);
}

int main(void)
{
    printf("libstl %s -- algorithms and custom element types\n\n", stl_version_string());

    sorting_demo();
    search_demo();
    set_operations_demo();
    accumulate_demo();
    owning_elements_demo();

    return 0;
}
