//
// memo.c - part of robinhood, a hash table with Robin Hood insertion
//
// SPDX-License-Identifier: Unlicense
//

#include "robinhood.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

struct fibonacci_stats
{
    size_t hits;
    size_t misses;
};

// ===========================================================================
// File-local prototypes
// ===========================================================================

static uint64_t
fibonacci_memo(RHTable table, int n, struct fibonacci_stats* stats);

int
main(int argc, char** argv);

static void
print_usage(const char* program_name);

// Demonstrates this table works as well for a small, hot, in-memory
// cache as it does for bulk-loading a directory tree (see
// scan.c/tester.c).
static uint64_t
fibonacci_memo(RHTable table, int n, struct fibonacci_stats* stats)
{
    char key [16];
    snprintf(key, sizeof(key), "%d", n);

    if (rh_has(table, key))
    {
        ++stats->hits;
        return *(uint64_t*)(rh_get(table, key, NULL));
    }

    ++stats->misses;

    uint64_t result;

    if (n < 2)
    {
        result = (uint64_t)n;
    }
    else
    {
        result = fibonacci_memo(table, n - 1, stats) +
                 fibonacci_memo(table, n - 2, stats);
    }

    uint64_t* stored = (uint64_t*)(malloc(sizeof(uint64_t)));

    if (stored != NULL)
    {
        *stored = result;
        if (!rh_set(table, key, stored))
        {
            free(stored);
        }
    }

    return result;
}

int
main(int argc, char** argv)
{
    if (argc != 2)
    {
        print_usage(argv [0]);
        return 2;
    }

    char*    end = NULL;
    long int n   = strtol(argv [1], &end, 10);
    bool valid = (end != argv [1]) && (*end == '\0') && (n >= 0) && (n <= 90);

    if (!valid)
    {
        print_usage(argv [0]);
        return 2;
    }

    RHTable table = rh_create(16);

    struct fibonacci_stats stats;
    stats.hits   = 0;
    stats.misses = 0;

    uint64_t result = fibonacci_memo(table, (int)n, &stats);

    printf("fibonacci(%ld) = %llu\n", n, (unsigned long long int)result);
    printf("cache after first computation: %zu entries, %zu hits,"
           " %zu misses\n",
           rh_count(table), stats.hits, stats.misses);

    if (n >= 2)
    {
        int  k = (int)(n / 2);
        char k_key [16];
        snprintf(k_key, sizeof(k_key), "%d", k);

        stats.hits   = 0;
        stats.misses = 0;

        uint64_t cached_value = fibonacci_memo(table, k, &stats);

        printf("\nfibonacci(%d) looked up again: %llu"
               " (%zu hit%s, %zu miss%s)\n",
               k, (unsigned long long int)cached_value, stats.hits,
               (stats.hits == 1) ? "" : "s", stats.misses,
               (stats.misses == 1) ? "" : "es");

        printf("invalidating fibonacci(%d) via rh_clear...\n", k);
        rh_clear(table, k_key);

        stats.hits   = 0;
        stats.misses = 0;

        uint64_t recomputed = fibonacci_memo(table, k, &stats);

        printf("fibonacci(%d) recomputed: %llu (%zu hit%s, %zu miss%s)\n", k,
               (unsigned long long int)recomputed, stats.hits,
               (stats.hits == 1) ? "" : "s", stats.misses,
               (stats.misses == 1) ? "" : "es");

        if (recomputed != cached_value)
        {
            fprintf(stderr, "warning: recomputed value differs!\n");
        }
    }

    printf("\nfinal cache: %zu entries, capacity %zu\n", rh_count(table),
           rh_capacity(table));

    RHIterator it;

    for (it = rhi_create(table); !rhi_is_finished(it); rhi_advance(it))
    {
        free(rh_get(table, rhi_key(it), NULL));
    }

    rhi_destroy(it);
    rh_destroy(&table);

    return 0;
}

static void
print_usage(const char* program_name)
{
    fprintf(stderr,
            "usage: %s <n>\n"
            "\n"
            "Computes fibonacci(n) via recursion memoized in an RHTable"
            " (key: n as a\n"
            "decimal string; value: a heap-allocated uint64_t)."
            " Demonstrates\n"
            "rh_has/rh_get/rh_set as a cache, and rh_clear as"
            " invalidation.\n"
            "n must be an integer in [0, 90] (fibonacci(91) would"
            " overflow uint64_t).\n",
            program_name);
}
