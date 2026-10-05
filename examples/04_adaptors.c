/*
 * 04_adaptors.c -- stack, queue, priority queue and bitset.
 *
 * The adaptors restrict a sequence container to one access pattern.  The
 * priority queue is the interesting one, because its ordering convention is
 * easy to get backwards.
 *
 * Build:
 *     cc -std=c11 -I.. -o 04_adaptors 04_adaptors.c ../build/libstl.a -lpthread -lm
 */

#include "libstl.h"

#include <stdio.h>
#include <string.h>

/* A task with a priority; the queue is ordered by the priority field. */
typedef struct task {
    int         priority;
    const char *name;
} task;

/* A min-heap order: lower numbers are more urgent, so `a` outranks `b` when
 * its priority is smaller.  A priority queue always yields the element its
 * comparator calls *greatest*. */
static int STL_CALL task_cmp(const void *a, const void *b)
{
    const task *x = (const task *)a;
    const task *y = (const task *)b;

    /* Reverse the natural order to get the smallest priority at the top. */
    if (x->priority != y->priority) {
        return (x->priority < y->priority) ? STL_GREATER : STL_LESS;
    }
    return STL_EQUAL;
}

static void stack_demo(void)
{
    stl_stack *s = stl_stack_new(sizeof(int), NULL);
    int i;

    printf("stack\n");

    for (i = 1; i <= 5; ++i) {
        stl_stack_push(s, &i);
    }
    printf("  pushed 1..5, top is %d\n", *(int *)stl_stack_top(s));

    printf("  draining:");
    while (!stl_stack_empty(s)) {
        printf(" %d", *(int *)stl_stack_top(s));
        stl_stack_pop(s);
    }
    printf("   (last in, first out)\n");

    /* Popping an empty stack reports an error rather than misbehaving. */
    stl_stack_pop(s);
    printf("  popping an empty stack reported: %s\n", stl_error_string(stl_get_error()));

    stl_stack_free(s);
}

static void queue_demo(void)
{
    stl_queue *q = stl_queue_new(sizeof(int), NULL);
    int i;

    printf("\nqueue\n");

    for (i = 1; i <= 5; ++i) {
        stl_queue_push(q, &i);
    }
    printf("  pushed 1..5, front %d, back %d\n",
           *(int *)stl_queue_front(q), *(int *)stl_queue_back(q));

    printf("  draining:");
    while (!stl_queue_empty(q)) {
        printf(" %d", *(int *)stl_queue_front(q));
        stl_queue_pop(q);
    }
    printf("   (first in, first out)\n");

    stl_queue_free(q);
}

static void priority_queue_demo(void)
{
    stl_priority_queue *pq = stl_priority_queue_new(sizeof(task), task_cmp, NULL);
    static const task tasks[] = {
        { 5, "write docs" },
        { 1, "fix crash" },
        { 3, "review PR" },
        { 2, "answer mail" },
        { 4, "refactor" },
    };
    size_t i;

    printf("\npriority queue\n");

    for (i = 0; i < sizeof(tasks) / sizeof(tasks[0]); ++i) {
        stl_priority_queue_push(pq, &tasks[i]);
    }

    printf("  draining by urgency:\n");
    while (!stl_priority_queue_empty(pq)) {
        const task *t = (const task *)stl_priority_queue_top(pq);
        printf("    priority %d  %s\n", t->priority, t->name);
        stl_priority_queue_pop(pq);
    }

    /* For plain integers the built-in comparator is enough, and it gives a
     * max-heap: the largest value comes out first. */
    {
        stl_priority_queue *nums = stl_priority_queue_new(sizeof(int), stl_cmp_int32, NULL);
        int values[6];
        int k;

        values[0] = 12; values[1] = 5; values[2] = 30;
        values[3] = 8;  values[4] = 21; values[5] = 3;
        for (k = 0; k < 6; ++k) {
            stl_priority_queue_push(nums, &values[k]);
        }
        printf("  default max-heap:");
        while (!stl_priority_queue_empty(nums)) {
            printf(" %d", *(int *)stl_priority_queue_top(nums));
            stl_priority_queue_pop(nums);
        }
        printf("\n");
        stl_priority_queue_free(nums);
    }

    /* The heap primitives work on any array, not just through the adaptor. */
    {
        int data[7];
        int k;

        data[0] = 9; data[1] = 4; data[2] = 7; data[3] = 1;
        data[4] = 8; data[5] = 3; data[6] = 6;

        stl_heap_make(data, 7, sizeof(int), stl_cmp_int32);
        printf("  heap root %d, is_heap %d\n", data[0],
               stl_heap_is_heap(data, 7, sizeof(int), stl_cmp_int32));

        stl_heap_sort(data, 7, sizeof(int), stl_cmp_int32);
        printf("  heap_sort:");
        for (k = 0; k < 7; ++k) printf(" %d", data[k]);
        printf("\n");
    }

    stl_priority_queue_free(pq);
}

static void bitset_demo(void)
{
    stl_bitset *seen = stl_bitset_new(64);
    stl_bitset *other = stl_bitset_new(64);
    stl_string *text;
    int i;

    printf("\nbitset\n");

    /* Mark the multiples of three below 64. */
    for (i = 0; i < 64; i += 3) {
        stl_bitset_set(seen, (size_t)i);
    }
    printf("  multiples of 3: %lu bits set\n", (unsigned long)stl_bitset_count(seen));
    printf("  first set bit %lu, next after 0 is %lu\n",
           (unsigned long)stl_bitset_find_first(seen),
           (unsigned long)stl_bitset_find_next(seen, 0));

    for (i = 0; i < 64; i += 2) {
        stl_bitset_set(other, (size_t)i);
    }
    {
        stl_bitset *both = stl_bitset_new(64);
        stl_bitset_and(both, seen, other);
        printf("  multiples of 6 (AND): %lu\n", (unsigned long)stl_bitset_count(both));
        stl_bitset_free(both);
    }

    stl_bitset_set(seen, 0);
    text = stl_bitset_to_string(seen);
    printf("  as a string, first 16 bits: %.16s\n", stl_string_cstr(text));
    stl_string_free(text);

    stl_bitset_free(seen);
    stl_bitset_free(other);
}

int main(void)
{
    printf("libstl %s -- adaptors and bitset\n\n", stl_version_string());

    stack_demo();
    queue_demo();
    priority_queue_demo();
    bitset_demo();

    return 0;
}
