//
// tester.c - part of robinhood, a hash table with Robin Hood insertion
//
// SPDX-License-Identifier: Unlicense
//

//
// Table of Contents
//
// This file uses the ASCII "form feed" character (Control-L) to delineate
// logical pages. This table of contents is collected from the first non-blank
// lines on each logical page.
//

// tester.c - part of robinhood, a hash table with Robin Hood insertion
// Table of Contents
// Headers, etc.
// Type definitions
// CHECK
// File-local prototypes
// make_key(int,char*,size_t)
// make_populated_table(size_t,int)
// test_basic_operations(void)
// test_deletion(void)
// directory_scan_visitor(const char*,const struct stat*,int,struct FTW*)
// test_directory_scan(void)
// test_growth(void)
// test_iteration(void)
// test_lifecycle(void)
// test_probe_stats(void)
// record_warning(const char*)
// test_warning_handler(void)
// test_resize_threshold(void)
// Test table
// run_all_tests(void)
// main(void)

//
// Headers, etc.
//

// Precedes all #includes so glibc won't mask the nftw() declaration.
#define _XOPEN_SOURCE 700

#include "robinhood.h"

#include <ftw.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>

//
// Type definitions
//

struct test
{
    const char* name;
    int         (*function) (void);
};

//
// CHECK
//

// Records a failure without aborting the run, so a single bad check
// doesn't hide the rest of a test's results.
#define CHECK(CONDITION)                                \
    do                                                  \
    {                                                   \
        if (!(CONDITION))                               \
        {                                               \
            fprintf (stderr, "%s:%d: FAILED: %s\n",     \
                     __FILE__, __LINE__, #CONDITION);   \
            ++errors;                                   \
        }                                               \
    }                                                   \
    while (0)

//
// File-local prototypes
//

static
int
directory_scan_visitor (const char*        fpath,
                         const struct stat* file_stat,
                         int                typeflag,
                         struct FTW*        ftw_info);

int
main (void);

static
void
make_key (int    index,
          char*  buffer,
          size_t buffer_size);

static
RHTable
make_populated_table (size_t initial_capacity,
                      int    count);

static
void
record_warning (const char* message);

static
int
run_all_tests (void);

static
int
test_basic_operations (void);

static
int
test_deletion (void);

static
int
test_directory_scan (void);

static
int
test_growth (void);

static
int
test_iteration (void);

static
int
test_lifecycle (void);

static
int
test_probe_stats (void);

static
int
test_resize_threshold (void);

static
int
test_warning_handler (void);

//
// make_key(int,char*,size_t)
//

static
void
make_key (int    index,
          char*  buffer,
          size_t buffer_size)
{
    snprintf (buffer, buffer_size, "key%d", index);
}

//
// make_populated_table(size_t,int)
//

static
RHTable
make_populated_table (size_t initial_capacity,
                      int    count)
{
    RHTable table = rh_create (initial_capacity);
    char    key [16];

    for (int index = 0; index < count; ++index)
    {
        make_key (index, key, sizeof (key));
        rh_set (table, key, (void*)(long int)index);
    }

    return table;
}

//
// test_basic_operations(void)
//

static
int
test_basic_operations (void)
{
    int errors = 0;

    // Capacity WILL be rounded up to power of two
    RHTable table = rh_create (32);

    CHECK (table != ((RHTable)NULL));
    CHECK (32 == rh_capacity (table));
    CHECK (0 == rh_count (table));

    rh_set (table, "me",     (void*)"foo");
    rh_set (table, "myself", (void*)"bar");
    rh_set (table, "I",      (void*)"baz");

    CHECK (rh_has (table, "me"));
    CHECK (rh_has (table, "myself"));
    CHECK (rh_has (table, "I"));
    CHECK (!rh_has (table, "nope"));
    CHECK (3 == rh_count (table));

    CHECK (0 == strcmp ("foo", (const char*)(rh_get (table, "me", NULL))));
    CHECK (0
           == strcmp ("bar", (const char*)(rh_get (table, "myself", NULL))));
    CHECK (0 == strcmp ("baz", (const char*)(rh_get (table, "I", NULL))));
    CHECK (NULL == rh_get (table, "nope", NULL));

    rh_set (table, "me", (void*)"updated");
    CHECK (0
           == strcmp ("updated", (const char*)(rh_get (table, "me", NULL))));
    CHECK (3 == rh_count (table));

    rh_destroy (&table);

    return errors;
}

//
// test_deletion(void)
//

static
int
test_deletion (void)
{
    int errors = 0;

    RHTable table = rh_create (32);
    rh_set (table, "me",     (void*)"foo");
    rh_set (table, "myself", (void*)"bar");
    rh_set (table, "I",      (void*)"baz");

    rh_clear (table, "myself");
    CHECK (!rh_has (table, "myself"));
    CHECK (rh_has (table, "me"));
    CHECK (rh_has (table, "I"));
    CHECK (2 == rh_count (table));

    rh_set (table, "myself", (void*)"bar-again");
    CHECK (rh_has (table, "myself"));
    CHECK (3 == rh_count (table));

    // Clearing a key that was never present is a no-op, not an error
    rh_clear (table, "nonexistent");
    CHECK (3 == rh_count (table));

    rh_destroy (&table);

    // Delete every other key out of a larger, collision-heavy table and
    // confirm the rest survive the backward shift
    RHTable small = make_populated_table (4, 64);
    char    key [16];

    for (int index = 0; index < 64; index += 2)
    {
        make_key (index, key, sizeof (key));
        rh_clear (small, key);
    }

    CHECK (32 == rh_count (small));

    for (int index = 0; index < 64; ++index)
    {
        make_key (index, key, sizeof (key));
        if (index % 2 == 0)
        {
            CHECK (!rh_has (small, key));
        }
        else
        {
            CHECK (rh_has (small, key));
        }
    }

    rh_destroy (&small);

    return errors;
}

//
// directory_scan_visitor(const char*,const struct stat*,int,struct FTW*)
//

// nftw's callback signature has no user-data parameter, so the table it
// populates is threaded through this file-scope variable instead.
static RHTable directory_scan_table = ((RHTable)NULL);

static
int
directory_scan_visitor (const char*        fpath,
                         const struct stat* file_stat,
                         int                typeflag,
                         struct FTW*        ftw_info)
{
    (void)ftw_info;

    // FTW_NS means stat()/lstat() failed for this path; the stat buffer's
    // contents are then undefined, so skip it rather than copying garbage.
    if (typeflag == FTW_NS)
    {
        return 0;
    }

    struct stat* stat_copy = (struct stat*)(malloc (sizeof (struct stat)));

    if (stat_copy == NULL)
    {
        return -1;
    }

    memcpy (stat_copy, file_stat, sizeof (struct stat));
    rh_set (directory_scan_table, fpath, stat_copy);

    return 0;
}

//
// test_directory_scan(void)
//

static
int
test_directory_scan (void)
{
    int errors = 0;

    // Point SCAN_ROOT at a bigger tree to stress-test the table with
    // far more entries.
    const char* SCAN_ROOT = ".";

    RHTable table = rh_create (16);
    directory_scan_table = table;

    int walk_result = nftw (SCAN_ROOT, directory_scan_visitor, 20, FTW_PHYS);
    CHECK (0 == walk_result);
    CHECK (rh_count (table) > 0);

    char known_path [512];
    snprintf (known_path, sizeof (known_path),
              "%s/src/robinhood.c", SCAN_ROOT);

    struct stat direct_stat;
    CHECK (0 == stat (known_path, &direct_stat));

    struct stat* found = (struct stat*)(rh_get (table, known_path, NULL));
    CHECK (found != NULL);
    if (found != NULL)
    {
        CHECK (found->st_ino == direct_stat.st_ino);
    }

    // The table only owns the key copies it makes internally -- free the
    // stat buffers we handed it before tearing the table down.
    RHIterator it;

    for (it = rhi_create (table); !rhi_is_finished (it); rhi_advance (it))
    {
        free (rh_get (table, rhi_key (it), NULL));
    }

    rhi_destroy (it);

    rh_destroy (&table);
    directory_scan_table = ((RHTable)NULL);

    return errors;
}

//
// test_growth(void)
//

static
int
test_growth (void)
{
    int errors = 0;

    // Force enough collisions/growth to exercise Robin Hood probing and
    // resize
    RHTable small = make_populated_table (4, 64);
    char    key [16];

    CHECK (64 == rh_count (small));
    CHECK (rh_capacity (small) > 4);

    for (int index = 0; index < 64; ++index)
    {
        make_key (index, key, sizeof (key));
        CHECK (rh_has (small, key));
        CHECK (((long int)index) == ((long int)(rh_get (small, key, NULL))));
    }

    rh_destroy (&small);

    return errors;
}

//
// test_iteration(void)
//

static
int
test_iteration (void)
{
    int errors = 0;

    // Iterate a table with gaps and confirm we visit exactly the
    // surviving keys, each exactly once
    RHTable small = make_populated_table (4, 64);
    char    key [16];

    for (int index = 0; index < 64; index += 2)
    {
        make_key (index, key, sizeof (key));
        rh_clear (small, key);
    }

    bool   seen [64] = { false };
    size_t visited  = 0;

    RHIterator it;

    for (it = rhi_create (small); !rhi_is_finished (it); rhi_advance (it))
    {
        const char* visited_key = rhi_key (it);
        int         index;

        CHECK (1 == sscanf (visited_key, "key%d", &index));
        CHECK (!seen [index]);
        seen [index] = true;
        ++visited;
    }

    // Past the end: rhi_key() returns NULL and rhi_advance() is a no-op
    CHECK (rhi_is_finished (it));
    CHECK (rhi_key (it) == ((const char*)NULL));
    rhi_advance (it);
    CHECK (rhi_is_finished (it));

    // rhi_reset() brings a finished iterator back to the first live entry
    rhi_reset (it);
    CHECK (!rhi_is_finished (it));
    CHECK (rhi_key (it) != ((const char*)NULL));

    rhi_destroy (it);

    CHECK (32 == visited);

    for (int index = 0; index < 64; ++index)
    {
        CHECK (seen [index] == (index % 2 == 1));
    }

    rh_destroy (&small);

    return errors;
}

//
// test_lifecycle(void)
//

static
int
test_lifecycle (void)
{
    int errors = 0;

    RHTable small = make_populated_table (4, 64);
    char    key [16];

    rh_empty (small);
    CHECK (0 == rh_count (small));

    for (int index = 0; index < 64; ++index)
    {
        make_key (index, key, sizeof (key));
        CHECK (!rh_has (small, key));
    }

    rh_destroy (&small);
    CHECK (small == ((RHTable)NULL));

    RHTable table = rh_create (8);
    rh_destroy (&table);
    CHECK (table == ((RHTable)NULL));

    // rh_destroy() tolerates a NULL pointer and an already-destroyed table
    rh_destroy (((RHTable*)NULL));

    RHTable already_destroyed = ((RHTable)NULL);
    rh_destroy (&already_destroyed);
    CHECK (already_destroyed == ((RHTable)NULL));

    // 0 and 1 both round up to a capacity of 1 (next_power_of_two's
    // n <= 1 case), rather than the general power-of-two rounding below
    RHTable minimal = rh_create (0);
    CHECK (1 == rh_capacity (minimal));
    rh_destroy (&minimal);

    minimal = rh_create (1);
    CHECK (1 == rh_capacity (minimal));
    rh_destroy (&minimal);

    // A capacity request too large to round up to the next power of two
    // overflows next_power_of_two(), which rh_create() reports as failure
    // rather than wrapping around to some small, wrong capacity
    RHTable overflowed = rh_create (SIZE_MAX);
    CHECK (overflowed == ((RHTable)NULL));

    return errors;
}

//
// test_probe_stats(void)
//

static
int
test_probe_stats (void)
{
    int errors = 0;

    // An empty table has nothing to report, but shouldn't crash or
    // report garbage
    RHTable empty = rh_create (16);
    struct RHProbeStats stats;

    rh_probe_stats (empty, &stats);
    CHECK (0 == stats.count);
    CHECK (0 == stats.max_distance);
    CHECK (0.0 == stats.mean_distance);
    CHECK (0.0 == stats.stddev_distance);

    rh_destroy (&empty);

    // Force enough collisions/growth to exercise real (non-zero)
    // probe distances, then sanity-check the reported statistics
    // against what rh_count()/rh_capacity() already say independently
    RHTable small = make_populated_table (4, 64);

    rh_probe_stats (small, &stats);
    CHECK (rh_count (small) == stats.count);
    CHECK (stats.max_distance < rh_capacity (small));
    CHECK (stats.mean_distance >= 0.0);
    CHECK (stats.stddev_distance >= 0.0);

    size_t histogram_total = 0;

    for (size_t index = 0; index < RH_PROBE_HISTOGRAM_BUCKETS; ++index)
    {
        histogram_total += stats.histogram [index];
    }
    CHECK (histogram_total == stats.count);

    rh_destroy (&small);

    return errors;
}

//
// record_warning(const char*)
//

// Captures what a custom warning handler received, so
// test_warning_handler() can check it -- mirrors directory_scan_table
// above: the callback signature has no user-data parameter.
static int  warning_handler_calls = 0;
static char warning_handler_message [256];

static
void
record_warning (const char* message)
{
    ++warning_handler_calls;
    snprintf (warning_handler_message, sizeof (warning_handler_message),
              "%s", message);
}

//
// test_warning_handler(void)
//

static
int
test_warning_handler (void)
{
    int errors = 0;

    // An absurdly large capacity reliably fails calloc() without
    // needing to actually exhaust real memory.
    size_t huge = ((size_t)1) << 60;

    warning_handler_calls       = 0;
    warning_handler_message [0] = '\0';
    rh_set_warning_handler (record_warning);

    RHTable failed = rh_create (huge);
    CHECK (failed == ((RHTable)NULL));
    CHECK (1 == warning_handler_calls);
    CHECK (strstr (warning_handler_message, "calloc") != NULL);

    // NULL suppresses entirely -- confirm no further calls happen
    warning_handler_calls = 0;
    rh_set_warning_handler (NULL);
    failed = rh_create (huge);

    CHECK (failed == ((RHTable)NULL));
    CHECK (0 == warning_handler_calls);

    // Restore default behavior for whatever runs after this test
    rh_set_warning_handler (rh_default_warning_handler);

    // Confirm the restored handler is actually reachable, not just assigned
    failed = rh_create (huge);
    CHECK (failed == ((RHTable)NULL));

    return errors;
}

//
// test_resize_threshold(void)
//

static
int
test_resize_threshold (void)
{
    int errors = 0;

    RHTable table = rh_create (16);
    CHECK (80 == rh_resize_threshold (table));

    CHECK (rh_set_resize_threshold (table, 70));
    CHECK (70 == rh_resize_threshold (table));

    CHECK (!rh_set_resize_threshold (table, 0));
    CHECK (70 == rh_resize_threshold (table));
    CHECK (!rh_set_resize_threshold (table, 101));
    CHECK (70 == rh_resize_threshold (table));

    CHECK (rh_set_resize_threshold (table, 1));
    CHECK (1 == rh_resize_threshold (table));
    CHECK (rh_set_resize_threshold (table, 100));
    CHECK (100 == rh_resize_threshold (table));

    rh_destroy (&table);

    // A lower threshold should trigger growth sooner, for the same
    // sequence of insertions into the same initial capacity
    RHTable strict  = rh_create (8);
    RHTable relaxed = rh_create (8);
    char    key [16];

    CHECK (rh_set_resize_threshold (strict, 50));
    CHECK (80 == rh_resize_threshold (relaxed));   // left at the default

    for (int index = 0; index < 5; ++index)
    {
        make_key (index, key, sizeof (key));
        rh_set (strict, key, (void*)(long int)index);
        rh_set (relaxed, key, (void*)(long int)index);
    }

    CHECK (16 == rh_capacity (strict));
    CHECK (8  == rh_capacity (relaxed));

    rh_destroy (&strict);
    rh_destroy (&relaxed);

    return errors;
}

//
// Test table
//

static const struct test tests [] =
{
    { "basic_operations", test_basic_operations },
    { "deletion",         test_deletion },
    { "directory_scan",   test_directory_scan },
    { "growth",           test_growth },
    { "iteration",        test_iteration },
    { "lifecycle",        test_lifecycle },
    { "probe_stats",      test_probe_stats },
    { "warning_handler",  test_warning_handler },
    { "resize_threshold", test_resize_threshold },
};

#define TEST_COUNT (sizeof (tests) / sizeof (tests [0]))

//
// run_all_tests(void)
//

static
int
run_all_tests (void)
{
    int total_errors = 0;

    for (size_t index = 0; index < TEST_COUNT; ++index)
    {
        int errors = tests [index].function ();

        printf ("%s: %s",
                tests [index].name, (errors == 0) ? "PASS" : "FAIL");
        if (errors != 0)
        {
            printf (" (%d error%s)", errors, (errors == 1) ? "" : "s");
        }
        printf ("\n");

        total_errors += errors;
    }

    return total_errors;
}

//
// main(void)
//

int
main (void)
{
    int total_errors = run_all_tests ();

    printf ("\n%d total error%s\n",
            total_errors, (total_errors == 1) ? "" : "s");

    return (total_errors == 0) ? 0 : 1;
}
