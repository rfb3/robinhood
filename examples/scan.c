//
// scan.c - part of robinhood, a hash table with Robin Hood insertion
//
// SPDX-License-Identifier: Unlicense
//

// Precedes all #includes so glibc won't mask openat/fstatat/fdopendir.
#define _XOPEN_SOURCE 700

#include "robinhood.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/stat.h>

// Detects symlink cycles when following symlinks: if a candidate
// directory's (device, inode) matches an ancestor already on this
// chain, descending into it would loop forever.
struct ancestor
{
    dev_t                  device;
    ino_t                  inode;
    const struct ancestor* parent;
};

// `path_buffer`/`path_length` are the single shared, growable-in-place
// path buffer maintained by walk()/walk_body() -- see their comments.
struct scan_context
{
    bool         cross_mounts;
    bool         follow_symlinks;
    dev_t        root_device;
    const char** excludes;
    size_t       excludes_count;
    RHTable      table;

    char   path_buffer [PATH_MAX];
    size_t path_length;

    size_t unreadable_count;
    size_t excluded_root_count;
    size_t store_failed_count;
};

// ===========================================================================
// File-local prototypes
// ===========================================================================

int
main(int argc, char** argv);

static bool
path_is_excluded(const char*  path,
                 const char** excludes,
                 size_t       excludes_count);

static void
print_probe_stats(const struct RHProbeStats* stats);

static void
print_usage(const char* program_name);

static void
store_entry(struct scan_context* context,
            const char*          path,
            const struct stat*   entry_stat);

static void
walk(int                    dir_fd,
     const char*            name,
     struct scan_context*   context,
     const struct ancestor* parent);

static void
walk_body(int                    dir_fd,
          const char*            name,
          struct scan_context*   context,
          const struct ancestor* parent);

int
main(int argc, char** argv)
{
    bool         cross_mounts          = false;
    bool         follow_symlinks       = false;
    bool         probe_stats           = false;
    bool         have_resize_threshold = false;
    unsigned int resize_threshold      = 0;
    const char*  root                  = NULL;
    const char** excludes              = NULL;
    size_t       excludes_count        = 0;

    enum
    {
        OPT_CROSS_MOUNTS = 256,
        OPT_FOLLOW_SYMLINKS,
        OPT_EXCLUDE,
        OPT_PROBE_STATS,
        OPT_RESIZE_THRESHOLD
    };

    static const struct option long_options [] = {
        {"cross-mounts", no_argument, NULL, OPT_CROSS_MOUNTS},
        {"follow-symlinks", no_argument, NULL, OPT_FOLLOW_SYMLINKS},
        {"exclude", required_argument, NULL, OPT_EXCLUDE},
        {"probe-stats", no_argument, NULL, OPT_PROBE_STATS},
        {"resize-threshold", required_argument, NULL, OPT_RESIZE_THRESHOLD},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}};

    opterr = 0;

    int opt;
    while ((opt = getopt_long(argc, argv, "h", long_options, NULL)) != -1)
    {
        switch (opt)
        {
        case OPT_CROSS_MOUNTS:
            cross_mounts = true;
            break;
        case OPT_FOLLOW_SYMLINKS:
            follow_symlinks = true;
            break;
        case OPT_PROBE_STATS:
            probe_stats = true;
            break;
        case OPT_RESIZE_THRESHOLD:
            resize_threshold      = (unsigned int)(strtoul(optarg, NULL, 10));
            have_resize_threshold = true;
            break;
        case OPT_EXCLUDE:
        {
            const char** grown = (const char**)(realloc(
                excludes, (excludes_count + 1) * sizeof(const char*)));

            if (grown == NULL)
            {
                fprintf(stderr, "%s: out of memory\n", argv [0]);
                free(excludes);
                return 1;
            }

            excludes                  = grown;
            excludes [excludes_count] = optarg;
            ++excludes_count;
            break;
        }
        case 'h':
            print_usage(argv [0]);
            free(excludes);
            return 0;
        default:
            print_usage(argv [0]);
            free(excludes);
            return 2;
        }
    }

    if (optind < argc)
    {
        root = argv [optind];
        ++optind;
    }

    if ((root == NULL) || (optind != argc))
    {
        print_usage(argv [0]);
        free(excludes);
        return 2;
    }

    struct stat root_stat;
    int         root_stat_flags = follow_symlinks ? 0 : AT_SYMLINK_NOFOLLOW;
    int         root_stat_result =
        fstatat(AT_FDCWD, root, &root_stat, root_stat_flags);

    if (root_stat_result != 0)
    {
        fprintf(stderr, "%s: cannot stat '%s': %s\n", argv [0], root,
                strerror(errno));
        free(excludes);
        return 1;
    }

    RHTable table = rh_create(1024);

    if (have_resize_threshold &&
        !rh_set_resize_threshold(table, resize_threshold))
    {
        fprintf(stderr,
                "%s: invalid --resize-threshold value '%u'"
                " (must be 1-100)\n",
                argv [0], resize_threshold);
        rh_destroy(&table);
        free(excludes);
        return 2;
    }

    struct scan_context context;
    context.cross_mounts        = cross_mounts;
    context.follow_symlinks     = follow_symlinks;
    context.root_device         = root_stat.st_dev;
    context.excludes            = excludes;
    context.excludes_count      = excludes_count;
    context.table               = table;
    context.path_buffer [0]     = '\0';
    context.path_length         = 0;
    context.unreadable_count    = 0;
    context.excluded_root_count = 0;
    context.store_failed_count  = 0;

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    walk(AT_FDCWD, root, &context, NULL);

    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed_seconds = ((double)(end.tv_sec - start.tv_sec)) +
                             ((double)(end.tv_nsec - start.tv_nsec)) / 1e9;

    printf("%s: %zu entries, capacity %zu, %.3f seconds"
           " (cross_mounts=%s, follow_symlinks=%s, excludes=%zu,"
           " unreadable=%zu, excluded_roots=%zu, store_failed=%zu,"
           " resize_threshold=%u)\n",
           root, rh_count(table), rh_capacity(table), elapsed_seconds,
           cross_mounts ? "yes" : "no", follow_symlinks ? "yes" : "no",
           excludes_count, context.unreadable_count,
           context.excluded_root_count, context.store_failed_count,
           rh_resize_threshold(table));

    if (probe_stats)
    {
        struct timespec stats_start;
        clock_gettime(CLOCK_MONOTONIC, &stats_start);

        struct RHProbeStats stats;
        rh_probe_stats(table, &stats);

        struct timespec stats_end;
        clock_gettime(CLOCK_MONOTONIC, &stats_end);

        double stats_seconds =
            ((double)(stats_end.tv_sec - stats_start.tv_sec)) +
            ((double)(stats_end.tv_nsec - stats_start.tv_nsec)) / 1e9;

        print_probe_stats(&stats);
        printf("probe stats computed in %.6f seconds\n", stats_seconds);
    }

    RHIterator it;

    for (it = rhi_create(table); !rhi_is_finished(it); rhi_advance(it))
    {
        free(rh_get(table, rhi_key(it), NULL));
    }

    rhi_destroy(it);
    rh_destroy(&table);
    free(excludes);

    return 0;
}

static bool
path_is_excluded(const char*  path,
                 const char** excludes,
                 size_t       excludes_count)
{
    for (size_t index = 0; index < excludes_count; ++index)
    {
        size_t prefix_length = strlen(excludes [index]);

        if (strncmp(path, excludes [index], prefix_length) != 0)
        {
            continue;
        }

        if ((path [prefix_length] == '\0') || (path [prefix_length] == '/'))
        {
            return true;
        }
    }

    return false;
}

static void
print_probe_stats(const struct RHProbeStats* stats)
{
    printf("probe depth: mean=%.2f stddev=%.2f max=%zu (n=%zu)\n",
           stats->mean_distance, stats->stddev_distance, stats->max_distance,
           stats->count);

    for (size_t index = 0; index < RH_PROBE_HISTOGRAM_BUCKETS; ++index)
    {
        double percent = (stats->count == 0)
                             ? 0.0
                             : (100.0 * (double)(stats->histogram [index]) /
                                (double)(stats->count));

        if (index == (RH_PROBE_HISTOGRAM_BUCKETS - 1))
        {
            printf("  distance %zu+: %zu (%.1f%%)\n", index,
                   stats->histogram [index], percent);
        }
        else
        {
            printf("  distance %zu: %zu (%.1f%%)\n", index,
                   stats->histogram [index], percent);
        }
    }
}

static void
print_usage(const char* program_name)
{
    fprintf(stderr,
            "usage: %s [--cross-mounts] [--follow-symlinks]"
            " [--exclude PATH]...\n"
            "       [--probe-stats] [--resize-threshold PERCENT]"
            " <directory>\n"
            "\n"
            "  --cross-mounts     do not stop at filesystem/mount"
            " boundaries.\n"
            "                     Default: stay on the starting"
            " filesystem.\n"
            "  --follow-symlinks  follow symlinked directories instead"
            " of\n"
            "                     treating them as leaves. Default: do"
            " not.\n"
            "  --exclude PATH     skip this path and everything under"
            " it.\n"
            "                     Repeatable.\n"
            "  --probe-stats      print Robin Hood probe-depth"
            " statistics (mean,\n"
            "                     max, stddev, a histogram) after the"
            " scan.\n"
            "                     Default: don't -- see PERFORMANCE.md"
            " for why\n"
            "                     that's the default despite negligible"
            " overhead.\n"
            "  --resize-threshold PERCENT\n"
            "                     load factor (1-100) at which the"
            " table grows.\n"
            "                     Default: 80 -- see"
            " rh_set_resize_threshold().\n",
            program_name);
}

static void
store_entry(struct scan_context* context,
            const char*          path,
            const struct stat*   entry_stat)
{
    struct stat* stat_copy = (struct stat*)(malloc(sizeof(struct stat)));

    if (stat_copy == NULL)
    {
        ++context->store_failed_count;
        return;
    }

    memcpy(stat_copy, entry_stat, sizeof(struct stat));
    if (!rh_set(context->table, path, stat_copy))
    {
        free(stat_copy);
        ++context->store_failed_count;
    }
}

// Manages a single shared path buffer (`context->path_buffer`) instead of
// building a fresh string per call: appends `name` before recursing into
// walk_body(), then truncates back afterward. This bounds stack usage to
// one PATH_MAX buffer for the whole scan regardless of tree depth, unlike
// a per-call stack buffer whose footprint would grow with recursion depth.
// (This is a robustness property, not a throughput one -- the analogous
// fstatat/openat change showed no measurable speed difference on this
// workload; see PERFORMANCE.md.) Safe to mutate the buffer after handing
// a path to store_entry(): rh_set() copies the key string itself, so the
// table never holds a pointer into this shared, mutable buffer.
static void
walk(int                    dir_fd,
     const char*            name,
     struct scan_context*   context,
     const struct ancestor* parent)
{
    size_t saved_length  = context->path_length;
    size_t name_length   = strlen(name);
    bool   need_slash    = (saved_length > 0) &&
                           (context->path_buffer [saved_length - 1] != '/');
    size_t needed_length = saved_length + (need_slash ? 1 : 0) + name_length;

    if (needed_length >= PATH_MAX)
    {
        ++context->unreadable_count;
        return;
    }

    char* dest = context->path_buffer + saved_length;

    if (need_slash)
    {
        *(dest++) = '/';
    }

    memcpy(dest, name, name_length + 1);
    context->path_length = needed_length;

    walk_body(dir_fd, name, context, parent);

    context->path_length                = saved_length;
    context->path_buffer [saved_length] = '\0';
}

// Does the actual per-entry work for the path walk() just appended to
// `context->path_buffer`. `name` is resolved via `fstatat`/`openat`
// relative to the already-open `dir_fd` (or AT_FDCWD for the initial
// call); `context->path_buffer` holds the accumulated full path, used
// only for bookkeeping (table key, exclusion matching) and never touched
// by a syscall itself. Failures (stat, opendir) are local to the
// entry/subtree they occur on -- unlike nftw on this platform, one bad
// entry can't abort the whole scan.
static void
walk_body(int                    dir_fd,
          const char*            name,
          struct scan_context*   context,
          const struct ancestor* parent)
{
    const char* full_path = context->path_buffer;

    if (path_is_excluded(full_path, context->excludes,
                         context->excludes_count))
    {
        ++context->excluded_root_count;
        return;
    }

    struct stat entry_stat;
    int stat_flags  = context->follow_symlinks ? 0 : AT_SYMLINK_NOFOLLOW;
    int stat_result = fstatat(dir_fd, name, &entry_stat, stat_flags);

    if (stat_result != 0)
    {
        ++context->unreadable_count;
        return;
    }

    bool crosses_mount = (!context->cross_mounts) &&
                         (entry_stat.st_dev != context->root_device);

    store_entry(context, full_path, &entry_stat);

    if (crosses_mount || !S_ISDIR(entry_stat.st_mode))
    {
        return;
    }

    for (const struct ancestor* ancestor = parent; ancestor != NULL;
         ancestor                        = ancestor->parent)
    {
        if ((ancestor->device == entry_stat.st_dev) &&
            (ancestor->inode == entry_stat.st_ino))
        {
            return;
        }
    }

    int child_dir_fd = openat(dir_fd, name, O_RDONLY | O_DIRECTORY);

    if (child_dir_fd < 0)
    {
        ++context->unreadable_count;
        return;
    }

    DIR* directory = fdopendir(child_dir_fd);

    if (directory == NULL)
    {
        close(child_dir_fd);
        ++context->unreadable_count;
        return;
    }

    struct ancestor self;
    self.device = entry_stat.st_dev;
    self.inode  = entry_stat.st_ino;
    self.parent = parent;

    struct dirent* entry;

    while ((entry = readdir(directory)) != NULL)
    {
        if ((strcmp(entry->d_name, ".") == 0) ||
            (strcmp(entry->d_name, "..") == 0))
        {
            continue;
        }

        walk(child_dir_fd, entry->d_name, context, &self);
    }

    closedir(directory);
}
