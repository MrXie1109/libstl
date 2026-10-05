/*
 * 02_associative.c -- set, map, and their unordered counterparts.
 *
 * Shows the element layout a map expects, the difference between the ordered
 * and hashed containers, and how to iterate them.
 *
 * Build:
 *     cc -std=c11 -I.. -o 02_associative 02_associative.c ../build/libstl.a -lpthread -lm
 */

#include "libstl.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* A map element is the caller's own struct, and must begin with the key.
 * The map is told where the value sits so it can hand back a pointer to it. */
typedef struct word_count {
    char *word;     /* key, at offset 0            */
    int   count;    /* value, at some offset after */
} word_count;

/* ------------------------------------------------------------------ */
/* set: an ordered collection of unique keys                           */
/* ------------------------------------------------------------------ */

static void set_demo(void)
{
    stl_set *s = stl_set_new(sizeof(int), stl_cmp_int32, NULL);
    int values[8];
    int i;

    printf("set\n");

    values[0] = 5; values[1] = 3; values[2] = 9; values[3] = 3;
    values[4] = 1; values[5] = 7; values[6] = 5; values[7] = 2;

    for (i = 0; i < 8; ++i) {
        const void *stored = stl_set_insert(s, &values[i]);
        /* insert returns NULL when the key was already present. */
        printf("  insert %d -> %s\n", values[i],
               (stored != NULL) ? "added" : "already there");
    }

    printf("  size %lu, elements in order:", (unsigned long)stl_set_size(s));
    {
        stl_iterator it = stl_set_begin(s);
        while (!stl_iter_equal(it, stl_set_end(s))) {
            printf(" %d", *(int *)stl_iter_data(it));
            it = stl_set_iter_next(it);
        }
        printf("\n");
    }

    {
        int key = 5;
        printf("  contains(5) %d, count(5) %lu\n",
               stl_set_contains(s, &key),
               (unsigned long)stl_set_count(s, &key));
        stl_set_erase(s, &key);
        printf("  after erase(5), size %lu\n", (unsigned long)stl_set_size(s));
    }

    stl_set_free(s);
}

/* ------------------------------------------------------------------ */
/* map: an ordered key -> value mapping                                */
/* ------------------------------------------------------------------ */

static void map_demo(void)
{
    static const char *words[] = { "pear", "apple", "fig", "apple", "pear", "apple" };
    stl_map *counts = stl_map_new(sizeof(word_count), offsetof(word_count, count),
                                  sizeof(int), sizeof(char *), stl_cmp_cstr, NULL);
    size_t i;

    printf("\nmap\n");

    for (i = 0; i < sizeof(words) / sizeof(words[0]); ++i) {
        char *key = (char *)words[i];

        /* put inserts or overwrites; get_or_insert is the cheaper form when
         * the common case is a new key.  Here we want to accumulate. */
        int *slot = (int *)stl_map_get(counts, &key);
        if (slot != NULL) {
            *slot += 1;
        } else {
            int one = 1;
            stl_map_put(counts, &key, &one);
        }
    }

    printf("  %lu distinct words, in key order:\n", (unsigned long)stl_map_size(counts));
    {
        stl_iterator it = stl_map_begin(counts);
        while (!stl_iter_equal(it, stl_map_end(counts))) {
            word_count *entry = (word_count *)stl_iter_data(it);
            printf("    %-8s %d\n", entry->word, entry->count);
            it = stl_map_iter_next(it);
        }
    }

    /* The value pointer aliases the map's storage, so it can be written
     * through directly. */
    {
        char *key = (char *)"fig";
        int *slot = (int *)stl_map_get(counts, &key);
        if (slot != NULL) {
            *slot = 100;
        }
        printf("  set fig to %d in place\n", *(int *)stl_map_get(counts, &key));
    }

    /* Ordered lookup: the first key that does not sort before "fig". */
    {
        char *key = (char *)"fig";
        word_count *entry = (word_count *)stl_map_lower_bound(counts, &key);
        printf("  lower_bound(\"fig\") -> \"%s\"\n", (entry != NULL) ? entry->word : "(none)");
    }

    stl_map_free(counts);
}

/* ------------------------------------------------------------------ */
/* hashmap: same shape, average O(1) instead of O(log n)               */
/* ------------------------------------------------------------------ */

/* Hash a char* key by the text it points at, not by the pointer value. */
static size_t hash_word(const void *elem)
{
    const char *word = *(const char *const *)elem;
    return stl_hash_bytes(word, strlen(word));
}

static int word_equal(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b) == 0;
}

/* Produce a distinct key per call; the keys must outlive the map, so they are
 * kept in a static pool rather than on the stack. */
static const char *words_ring(int index)
{
    static char pool[1000][16];
    static int initialised = 0;

    if (!initialised) {
        int i;
        for (i = 0; i < 1000; ++i) {
            snprintf(pool[i], sizeof(pool[i]), "item%d", i);
        }
        initialised = 1;
    }
    return pool[index % 1000];
}

static void hashmap_demo(void)
{
    stl_hashmap *m = stl_hashmap_new(sizeof(word_count), offsetof(word_count, count),
                                     sizeof(int), sizeof(char *), hash_word, word_equal, NULL);
    int i;

    printf("\nhashmap\n");

    for (i = 0; i < 1000; ++i) {
        char *key = (char *)words_ring(i);
        int value = i;
        stl_hashmap_put(m, &key, &value);
    }
    printf("  %lu entries\n", (unsigned long)stl_hashmap_size(m));

    {
        char *key = (char *)"item7";
        int *slot = (int *)stl_hashmap_get(m, &key);
        printf("  get(\"item7\") -> %s\n", (slot != NULL) ? "found" : "absent");
    }

    /* Iteration order is unspecified, unlike the ordered map. */
    {
        size_t seen = 0;
        stl_iterator it = stl_hashmap_begin(m);
        while (!stl_iter_equal(it, stl_hashmap_end(m))) {
            ++seen;
            it = stl_hashmap_iter_next(it);
        }
        printf("  iteration visits %lu entries\n", (unsigned long)seen);
    }

    stl_hashmap_free(m);
}

int main(void)
{
    printf("libstl %s -- associative containers\n\n", stl_version_string());

    set_demo();
    map_demo();
    hashmap_demo();

    return 0;
}
