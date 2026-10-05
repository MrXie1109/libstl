/*
 * libstl_bitset.c -- dynamic bitset (std::bitset / std::vector<bool> hybrid).
 */

#include "libstl_internal.h"

typedef size_t stl_bitset_word;
#define STL_BITS_PER_WORD (sizeof(stl_bitset_word) * CHAR_BIT)

struct stl_bitset {
    unsigned int      magic;
    stl_bitset_word  *words;
    size_t            nbits;
    size_t            nwords;
    const stl_allocator *alloc;
};

#define STL_BITSET_MAGIC 0x42u /* 'B' */

static size_t stl__bitset_words_for(size_t nbits)
{
    return (nbits + STL_BITS_PER_WORD - 1) / STL_BITS_PER_WORD;
}

static stl_bitset_word stl__bitset_mask(size_t i)
{
    return (stl_bitset_word)1 << (i % STL_BITS_PER_WORD);
}

stl_bitset *stl_bitset_new_a(size_t nbits, const stl_allocator *a)
{
    stl_bitset *b;

    b = (stl_bitset *)stl_mem_alloc(a, 1, sizeof(*b));
    if (b == NULL) {
        return NULL;
    }
    memset(b, 0, sizeof(*b));
    b->magic = STL_BITSET_MAGIC;
    b->alloc = stl__allocator_or_default(a);
    b->nbits = nbits;
    b->nwords = stl__bitset_words_for(nbits);
    if (b->nwords == 0) {
        b->nwords = 1;
    }
    b->words = (stl_bitset_word *)stl_mem_alloc(b->alloc, b->nwords, sizeof(stl_bitset_word));
    if (b->words == NULL) {
        stl_mem_free(b->alloc, b);
        return NULL;
    }
    memset(b->words, 0, b->nwords * sizeof(stl_bitset_word));
    return b;
}

stl_bitset *stl_bitset_new(size_t nbits)
{
    return stl_bitset_new_a(nbits, NULL);
}

void stl_bitset_free(stl_bitset *b)
{
    if (b == NULL) {
        return;
    }
    STL_CHECK(b->magic == STL_BITSET_MAGIC, STL_ERR_INVALID, "not a bitset");
    stl_mem_free(b->alloc, b->words);
    b->magic = 0;
    stl_mem_free(b->alloc, b);
}

stl_bitset *stl_bitset_copy(const stl_bitset *b)
{
    stl_bitset *out;

    if (b == NULL) {
        return NULL;
    }
    out = stl_bitset_new_a(b->nbits, b->alloc);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out->words, b->words, b->nwords * sizeof(stl_bitset_word));
    return out;
}

size_t stl_bitset_size(const stl_bitset *b) { return (b != NULL) ? b->nbits : 0; }

size_t stl_bitset_count(const stl_bitset *b)
{
    size_t i;
    size_t n = 0;

    if (b == NULL) {
        return 0;
    }
    for (i = 0; i < b->nwords; ++i) {
        stl_bitset_word w = b->words[i];
        while (w != 0) {
            w &= (stl_bitset_word)(w - 1);
            ++n;
        }
    }
    return n;
}

void stl_bitset_set(stl_bitset *b, size_t i)
{
    if (b == NULL || i >= b->nbits) {
        STL_REPORT_RANGE(i, (b != NULL) ? b->nbits : 0);
        return;
    }
    b->words[i / STL_BITS_PER_WORD] |= stl__bitset_mask(i);
}

void stl_bitset_reset(stl_bitset *b, size_t i)
{
    if (b == NULL || i >= b->nbits) {
        STL_REPORT_RANGE(i, (b != NULL) ? b->nbits : 0);
        return;
    }
    b->words[i / STL_BITS_PER_WORD] &= (stl_bitset_word)~stl__bitset_mask(i);
}

void stl_bitset_flip(stl_bitset *b, size_t i)
{
    if (b == NULL || i >= b->nbits) {
        STL_REPORT_RANGE(i, (b != NULL) ? b->nbits : 0);
        return;
    }
    b->words[i / STL_BITS_PER_WORD] ^= stl__bitset_mask(i);
}

void stl_bitset_set_all(stl_bitset *b)
{
    size_t i;

    if (b == NULL) {
        return;
    }
    for (i = 0; i < b->nwords; ++i) {
        b->words[i] = (stl_bitset_word)~(stl_bitset_word)0;
    }
    /* Clear unused trailing bits so count()/all() stay correct. */
    if (b->nbits % STL_BITS_PER_WORD != 0) {
        size_t used = b->nbits % STL_BITS_PER_WORD;
        b->words[b->nwords - 1] = ((stl_bitset_word)1 << used) - 1;
    }
}

void stl_bitset_reset_all(stl_bitset *b)
{
    if (b != NULL) {
        memset(b->words, 0, b->nwords * sizeof(stl_bitset_word));
    }
}

void stl_bitset_flip_all(stl_bitset *b)
{
    size_t i;

    if (b == NULL) {
        return;
    }
    for (i = 0; i < b->nwords; ++i) {
        b->words[i] = ~b->words[i];
    }
    if (b->nbits % STL_BITS_PER_WORD != 0) {
        size_t used = b->nbits % STL_BITS_PER_WORD;
        b->words[b->nwords - 1] &= ((stl_bitset_word)1 << used) - 1;
    }
}

int stl_bitset_test(const stl_bitset *b, size_t i)
{
    if (b == NULL || i >= b->nbits) {
        STL_REPORT_RANGE(i, (b != NULL) ? b->nbits : 0);
        return 0;
    }
    return (b->words[i / STL_BITS_PER_WORD] & stl__bitset_mask(i)) != 0;
}

int stl_bitset_none(const stl_bitset *b) { return stl_bitset_count(b) == 0; }
int stl_bitset_any(const stl_bitset *b)  { return stl_bitset_count(b) != 0; }
int stl_bitset_all(const stl_bitset *b)
{
    return (b != NULL) && (stl_bitset_count(b) == b->nbits);
}

int stl_bitset_equals(const stl_bitset *a, const stl_bitset *b)
{
    if (a == b) {
        return 1;
    }
    if (a == NULL || b == NULL || a->nbits != b->nbits) {
        return 0;
    }
    return memcmp(a->words, b->words, a->nwords * sizeof(stl_bitset_word)) == 0;
}

static int stl__bitset_op(stl_bitset *r, const stl_bitset *a, const stl_bitset *b, int op)
{
    size_t i;

    if (r == NULL || a == NULL || b == NULL || a->nbits != b->nbits || r->nbits != a->nbits) {
        stl__set_error_at(STL_ERR_INVALID, __FILE__, __LINE__, "bitset operation requires equal sizes");
        return STL_ERR_INVALID;
    }
    for (i = 0; i < r->nwords; ++i) {
        switch (op) {
        case 0: r->words[i] = a->words[i] & b->words[i]; break;
        case 1: r->words[i] = a->words[i] | b->words[i]; break;
        default: r->words[i] = a->words[i] ^ b->words[i]; break;
        }
    }
    if (r->nbits % STL_BITS_PER_WORD != 0) {
        size_t used = r->nbits % STL_BITS_PER_WORD;
        r->words[r->nwords - 1] &= ((stl_bitset_word)1 << used) - 1;
    }
    return STL_OK;
}

int stl_bitset_and(stl_bitset *r, const stl_bitset *a, const stl_bitset *b) { return stl__bitset_op(r, a, b, 0); }
int stl_bitset_or(stl_bitset *r, const stl_bitset *a, const stl_bitset *b)  { return stl__bitset_op(r, a, b, 1); }
int stl_bitset_xor(stl_bitset *r, const stl_bitset *a, const stl_bitset *b) { return stl__bitset_op(r, a, b, 2); }

int stl_bitset_not(stl_bitset *r, const stl_bitset *a)
{
    size_t i;

    if (r == NULL || a == NULL || r->nbits != a->nbits) {
        stl__set_error_at(STL_ERR_INVALID, __FILE__, __LINE__, "bitset::not requires equal sizes");
        return STL_ERR_INVALID;
    }
    for (i = 0; i < r->nwords; ++i) {
        r->words[i] = ~a->words[i];
    }
    if (r->nbits % STL_BITS_PER_WORD != 0) {
        size_t used = r->nbits % STL_BITS_PER_WORD;
        r->words[r->nwords - 1] &= ((stl_bitset_word)1 << used) - 1;
    }
    return STL_OK;
}

size_t stl_bitset_find_first(const stl_bitset *b)
{
    size_t i;

    if (b == NULL) {
        return STL_NPOS;
    }
    for (i = 0; i < b->nwords; ++i) {
        if (b->words[i] != 0) {
            size_t bit = 0;
            stl_bitset_word w = b->words[i];
            while ((w & 1u) == 0) {
                w >>= 1;
                ++bit;
            }
            {
                size_t index = i * STL_BITS_PER_WORD + bit;
                return (index < b->nbits) ? index : STL_NPOS;
            }
        }
    }
    return STL_NPOS;
}

size_t stl_bitset_find_next(const stl_bitset *b, size_t from)
{
    size_t i;

    if (b == NULL || from >= b->nbits) {
        return STL_NPOS;
    }
    for (i = from + 1; i < b->nbits; ++i) {
        if (stl_bitset_test(b, i)) {
            return i;
        }
    }
    return STL_NPOS;
}

void stl_bitset_foreach(const stl_bitset *b, int (STL_CALL *fn)(size_t index, void *user), void *user)
{
    size_t i;

    if (b == NULL || fn == NULL) {
        return;
    }
    for (i = 0; i < b->nbits; ++i) {
        if (stl_bitset_test(b, i) && fn(i, user)) {
            break;
        }
    }
}

stl_string *stl_bitset_to_string(const stl_bitset *b)
{
    stl_string *s;
    size_t i;

    if (b == NULL) {
        return NULL;
    }
    s = stl_string_new_cap(b->nbits);
    if (s == NULL) {
        return NULL;
    }
    for (i = 0; i < b->nbits; ++i) {
        if (stl_string_append_char(s, stl_bitset_test(b, i) ? '1' : '0') != STL_OK) {
            stl_string_free(s);
            return NULL;
        }
    }
    return s;
}
