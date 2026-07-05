//
// environ.c - part of robinhood, a hash table with Robin Hood insertion
//
// SPDX-License-Identifier: Unlicense
//

#include "robinhood.h"

#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// POSIX doesn't require environ to be declared in any header (unlike
// most of libc), so portable programs declare it themselves -- this
// works as-is on macOS, the BSDs, glibc, and musl.
extern char** environ;

// ===========================================================================
// File-local prototypes
// ===========================================================================

static int
compare_keys(const void* left, const void* right);

int
main(int argc, char** argv);

static void
print_usage(const char* program_name);

// `left`/`right` each point at one array element (i.e. a `const
// char*`), not at the string itself -- hence the double indirection.
static int
compare_keys(const void* left, const void* right)
{
    const char* const* left_key  = (const char* const*)left;
    const char* const* right_key = (const char* const*)right;

    return strcmp(*left_key, *right_key);
}

int
main(int argc, char** argv)
{
    bool         have_resize_threshold = false;
    unsigned int resize_threshold      = 0;

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

    if (optind != argc)
    {
        print_usage(argv [0]);
        return 2;
    }

    RHTable table = rh_create(64);

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

    size_t store_failed = 0;

    for (char** entry = environ; *entry != NULL; ++entry)
    {
        const char* equals = strchr(*entry, '=');

        if (equals == NULL)
        {
            // POSIX doesn't guarantee every entry contains '=' --
            // skip anything malformed rather than mishandling the key.
            continue;
        }

        size_t name_length = (size_t)(equals - *entry);
        char   name [256];

        if (name_length >= sizeof(name))
        {
            name_length = sizeof(name) - 1;
        }

        memcpy(name, *entry, name_length);
        name [name_length] = '\0';

        // Last occurrence of a duplicate name wins, since that falls
        // out naturally from rh_set() semantics -- contrast getenv(3),
        // which is documented to return the *first* match instead.
        if (!rh_set(table, name, (void*)(equals + 1)))
        {
            ++store_failed;
        }
    }

    size_t entry_count = rh_count(table);

    printf("%zu environment variable%s", entry_count,
           (entry_count == 1) ? "" : "s");
    if (store_failed > 0)
    {
        printf(" (%zu not stored -- allocation failed)", store_failed);
    }
    printf(":\n\n");

    const char** keys =
        (const char**)(malloc(entry_count * sizeof(const char*)));

    if ((keys == NULL) && (entry_count > 0))
    {
        rh_destroy(&table);
        return 1;
    }

    size_t     index = 0;
    RHIterator it;

    for (it = rhi_create(table); !rhi_is_finished(it); rhi_advance(it))
    {
        keys [index] = rhi_key(it);
        ++index;
    }

    rhi_destroy(it);

    qsort(keys, entry_count, sizeof(const char*), compare_keys);

    for (size_t rank = 0; rank < entry_count; ++rank)
    {
        const char* value = (const char*)(rh_get(table, keys [rank], NULL));

        printf("%-32s %s\n", keys [rank], value);
    }

    free(keys);
    rh_destroy(&table);

    return 0;
}

static void
print_usage(const char* program_name)
{
    fprintf(stderr,
            "usage: %s [--resize-threshold PERCENT]\n"
            "\n"
            "Walks this process's environment into an RHTable"
            " (name -> value)\n"
            "and prints it sorted by name. Values are borrowed"
            " pointers into\n"
            "the existing environment strings, not copies -- the"
            " only example\n"
            "that needs no heap allocation at all beyond what"
            " rh_set() itself\n"
            "does for its key copies.\n"
            "--resize-threshold PERCENT sets the table's resize"
            " threshold (1-100,\n"
            "default 80) -- see rh_set_resize_threshold().\n",
            program_name);
}
