//
// wordfreq.c - part of robinhood, a hash table with Robin Hood insertion
//
// SPDX-License-Identifier: Unlicense
//

#include "robinhood.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// `word` points directly at the table's own internal key copy --
// valid only until the table it came from is modified or destroyed.
struct word_count
{
    const char* word;
    long int    count;
};

// ===========================================================================
// File-local prototypes
// ===========================================================================

static int
compare_counts_descending(const void* left, const void* right);

int
main(int argc, char** argv);

static size_t
normalize_word(char* word);

static void
print_usage(const char* program_name);

// qsort() comparator; ties broken alphabetically for determinism.
static int
compare_counts_descending(const void* left, const void* right)
{
    const struct word_count* left_entry  = (const struct word_count*)left;
    const struct word_count* right_entry = (const struct word_count*)right;

    if (left_entry->count != right_entry->count)
    {
        return (left_entry->count < right_entry->count) ? 1 : -1;
    }

    return strcmp(left_entry->word, right_entry->word);
}

// Classic hash-table read-modify-write, the one usage pattern neither
// scan.c's bulk insert-and-check nor memo.c's cache-lookup-then-populate
// exercises: rh_get the current count (0 if this is the first time the
// word's been seen), increment, rh_set it back. Counts are stored as a
// pointer-cast integer rather than a heap allocation, so -- unlike
// scan.c/memo.c -- the final RHIterator loop below exists to report
// results, not to free anything.
int
main(int argc, char** argv)
{
    long int top_n = 10;

    if (argc > 2)
    {
        print_usage(argv [0]);
        return 2;
    }

    if (argc == 2)
    {
        char* end = NULL;
        top_n     = strtol(argv [1], &end, 10);

        if ((end == argv [1]) || (*end != '\0') || (top_n < 1))
        {
            print_usage(argv [0]);
            return 2;
        }
    }

    RHTable table = rh_create(1024);

    size_t total_words  = 0;
    size_t store_failed = 0;
    char   word [256];

    while (scanf("%255s", word) == 1)
    {
        if (normalize_word(word) == 0)
        {
            continue;
        }

        long int count = (long int)(rh_get(table, word, NULL));
        if (!rh_set(table, word, (void*)(count + 1)))
        {
            ++store_failed;
        }
        ++total_words;
    }

    size_t distinct_words = rh_count(table);

    printf("%zu distinct word%s, %zu total", distinct_words,
           (distinct_words == 1) ? "" : "s", total_words);
    if (store_failed > 0)
    {
        printf(" (%zu word%s not counted -- allocation failed)", store_failed,
               (store_failed == 1) ? "" : "s");
    }
    printf("\n");

    // malloc(0) is implementation-defined and may legitimately return
    // NULL, so an empty table (distinct_words == 0) must skip
    // allocation entirely rather than treat that NULL as failure.
    struct word_count* counts = NULL;

    if (distinct_words > 0)
    {
        counts = (struct word_count*)(malloc(distinct_words *
                                             sizeof(struct word_count)));

        if (counts == NULL)
        {
            rh_destroy(&table);
            return 1;
        }
    }

    size_t     index = 0;
    RHIterator it;

    for (it = rhi_create(table); !rhi_is_finished(it); rhi_advance(it))
    {
        const char* key = rhi_key(it);

        counts [index].word  = key;
        counts [index].count = (long int)(rh_get(table, key, NULL));
        ++index;
    }

    rhi_destroy(it);

    if (distinct_words > 0)
    {
        qsort(counts, distinct_words, sizeof(struct word_count),
              compare_counts_descending);
    }

    size_t shown =
        (((size_t)top_n) < distinct_words) ? ((size_t)top_n) : distinct_words;

    printf("\ntop %zu:\n", shown);

    for (size_t rank = 0; rank < shown; ++rank)
    {
        printf("%6ld  %s\n", counts [rank].count, counts [rank].word);
    }

    free(counts);
    rh_destroy(&table);

    return 0;
}

// So "Hello," and "hello" count as the same word; returns 0 if
// nothing alphanumeric remains.
static size_t
normalize_word(char* word)
{
    size_t length = strlen(word);
    size_t start  = 0;

    while ((start < length) && !isalnum((unsigned char)(word [start])))
    {
        ++start;
    }

    size_t end = length;

    while ((end > start) && !isalnum((unsigned char)(word [end - 1])))
    {
        --end;
    }

    size_t trimmed_length = end - start;

    for (size_t index = 0; index < trimmed_length; ++index)
    {
        word [index] = (char)(tolower((unsigned char)(word [start + index])));
    }
    word [trimmed_length] = '\0';

    return trimmed_length;
}

static void
print_usage(const char* program_name)
{
    fprintf(stderr,
            "usage: %s [top_n]\n"
            "\n"
            "Reads whitespace-separated words from stdin, counts"
            " occurrences\n"
            "(case-insensitive, punctuation-trimmed) in an RHTable,"
            " and prints\n"
            "the top_n most frequent (default: 10).\n",
            program_name);
}
