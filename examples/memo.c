//
// memo.c - part of robinhood, a hash table with Robin Hood insertion
//
// SPDX-License-Identifier: Unlicense
//

#include "robinhood.h"

#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

struct fib_stats
{
    size_t hits;
    size_t misses;
};

// ===========================================================================
// File-local prototypes
// ===========================================================================

static uint64_t
fib_memo(RHTable table, int n, struct fib_stats* stats);

int
main(int argc, char** argv);

static void
print_usage(const char* program_name);

// Demonstrates this table works as well for a small, hot, in-memory
// cache as it does for bulk-loading a directory tree (see
// scan.c/tester.c).
static uint64_t
fib_memo(RHTable table, int n, struct fib_stats* stats)
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
        result =
            fib_memo(table, n - 1, stats) + fib_memo(table, n - 2, stats);
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
    bool         have_resize_threshold = false;
    unsigned int resize_threshold      = 0;
    const char*  n_arg                 = NULL;

    enum
    {
        OPT_RESIZE_THRESHOLD = 256
    };

    static const struct option long_options [] = {
        {"resize-threshold", required_argument, NULL, OPT_RESIZE_THRESHOLD},
        {NULL, 0, NULL, 0}};

    opterr = 0;

    int opt;
    while ((opt = getopt_long(argc, argv, "", long_options, NULL)) != -1)
    {
        switch (opt)
        {
        case OPT_RESIZE_THRESHOLD:
            resize_threshold      = (unsigned int)(strtoul(optarg, NULL, 10));
            have_resize_threshold = true;
            break;
        default:
            print_usage(argv [0]);
            return 2;
        }
    }

    if (optind < argc)
    {
        n_arg = argv [optind];
        ++optind;
    }

    if ((n_arg == NULL) || (optind != argc))
    {
        print_usage(argv [0]);
        return 2;
    }

    char*    end = NULL;
    long int n   = strtol(n_arg, &end, 10);

    if ((end == n_arg) || (*end != '\0') || (n < 0) || (n > 90))
    {
        print_usage(argv [0]);
        return 2;
    }

    RHTable table = rh_create(16);

    if (have_resize_threshold &&
        !rh_set_resize_threshold(table, resize_threshold))
    {
        fprintf(stderr,
                "%s: invalid --resize-threshold value '%u'"
                " (must be 1-100)\n",
                argv [0], resize_threshold);
        rh_destroy(&table);
        return 2;
    }

    struct fib_stats stats;
    stats.hits   = 0;
    stats.misses = 0;

    uint64_t result = fib_memo(table, (int)n, &stats);

    printf("fib(%ld) = %llu\n", n, (unsigned long long int)result);
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

        uint64_t cached_value = fib_memo(table, k, &stats);

        printf("\nfib(%d) looked up again: %llu"
               " (%zu hit%s, %zu miss%s)\n",
               k, (unsigned long long int)cached_value, stats.hits,
               (stats.hits == 1) ? "" : "s", stats.misses,
               (stats.misses == 1) ? "" : "es");

        printf("invalidating fib(%d) via rh_clear...\n", k);
        rh_clear(table, k_key);

        stats.hits   = 0;
        stats.misses = 0;

        uint64_t recomputed = fib_memo(table, k, &stats);

        printf("fib(%d) recomputed: %llu (%zu hit%s, %zu miss%s)\n", k,
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
            "usage: %s [--resize-threshold PERCENT] <n>\n"
            "\n"
            "Computes fib(n) via recursion memoized in an RHTable"
            " (key: n as a\n"
            "decimal string; value: a heap-allocated uint64_t)."
            " Demonstrates\n"
            "rh_has/rh_get/rh_set as a cache, and rh_clear as"
            " invalidation.\n"
            "n must be an integer in [0, 90] (fib(91) would overflow"
            " uint64_t).\n"
            "--resize-threshold PERCENT sets the table's resize"
            " threshold (1-100,\n"
            "default 80) -- see rh_set_resize_threshold().\n",
            program_name);
}
