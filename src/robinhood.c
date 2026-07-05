//
// robinhood.c - part of robinhood, a hash table with Robin Hood insertion
//
// SPDX-License-Identifier: Unlicense
//

#include "robinhood.h"

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum entry_state_enum
{
    EMPTY = 0,
    USED  = 1
};

typedef enum entry_state_enum entry_state;

struct RHEntry_struct
{
    char*       key;
    uint64_t    hash;
    void*       value;
    size_t      distance;
    entry_state state;
};

typedef struct RHEntry_struct RHEntry;

#define RHE_KEY(ENTRY)      ((ENTRY).key)
#define RHE_HASH(ENTRY)     ((ENTRY).hash)
#define RHE_VALUE(ENTRY)    ((ENTRY).value)
#define RHE_DISTANCE(ENTRY) ((ENTRY).distance)
#define RHE_STATE(ENTRY)    ((ENTRY).state)

#define RHE_SET_KEY(ENTRY, KEY)           (((ENTRY).key) = (KEY))
#define RHE_SET_HASH(ENTRY, HASH)         (((ENTRY).hash) = (HASH))
#define RHE_SET_VALUE(ENTRY, VALUE)       (((ENTRY).value) = (VALUE))
#define RHE_SET_DISTANCE(ENTRY, DISTANCE) (((ENTRY).distance) = (DISTANCE))
#define RHE_SET_STATE(ENTRY, STATE)       (((ENTRY).state) = (STATE))

// The table grows whenever the next insertion would push its load
// factor past this percentage of capacity -- see rh_maybe_grow().
// Matches, e.g., Java's HashMap default of 0.75, adjusted slightly
// looser; see PERFORMANCE.md's "Configurable resize threshold" section
// for the empirical case that no other fixed value in the 70-90% range
// is worth the complexity of making this configurable.
#define RH_RESIZE_THRESHOLD_PERCENT (80)

struct RHTable_struct
{
    RHEntry* entries;
    size_t   capacity;
    size_t   count;
};

typedef struct RHTable_struct* RHTable;

#define NULL_RHTABLE ((RHTable)NULL)

#define RH_ENTRIES(TABLE)  ((TABLE)->entries)
#define RH_CAPACITY(TABLE) ((TABLE)->capacity)
#define RH_COUNT(TABLE)    ((TABLE)->count)

#define RH_SET_ENTRIES(TABLE, ENTRIES)   (((TABLE)->entries) = (ENTRIES))
#define RH_SET_CAPACITY(TABLE, CAPACITY) (((TABLE)->capacity) = (CAPACITY))
#define RH_SET_COUNT(TABLE, COUNT)       (((TABLE)->count) = (COUNT))

struct RHIterator_struct
{
    RHTable table;
    size_t  position;
};

typedef struct RHIterator_struct* RHIterator;

#define NULL_RHITERATOR ((RHIterator)NULL)

#define RHI_TABLE(ITERATOR)    ((ITERATOR)->table)
#define RHI_POSITION(ITERATOR) ((ITERATOR)->position)

#define RHI_SET_POSITION(ITERATOR, POSITION)                                 \
    (((ITERATOR)->position) = (POSITION))

// ===========================================================================
// File-local prototypes
// ===========================================================================

static char*
dup_string(const char* text);

static size_t
next_power_of_two(size_t n);

static bool
rh_find_index(RHTable     table,
              const char* key,
              uint64_t    hash,
              size_t*     out_index);

static void
rh_insert_unique(RHTable table, char* key, uint64_t hash, void* value);

static bool
rh_maybe_grow(RHTable table);

static void
rhi_advance_to_used(RHIterator iterator);

static uint64_t
string_hash(const char* text);

static char*
dup_string(const char* text)
{
    size_t length = strlen(text) + 1;
    char*  copy   = (char*)(malloc(length));

    if (copy != ((void*)NULL))
    {
        memcpy(copy, text, length);
    }

    return copy;
}

static size_t
next_power_of_two(size_t n)
{
    if (n <= 1)
    {
        return 1;
    }
    if (n > ((size_t)1 << (sizeof(size_t) * CHAR_BIT - 1)))
    {
        return 0; // overflow
    }
    --n;
    for (size_t shift = 1; shift < sizeof(size_t) * CHAR_BIT; shift <<= 1)
    {
        n |= n >> shift;
    }

    return n + 1;
}

// Relies on the Robin Hood invariant that distances never decrease
// along a probe run: once a resident's distance is less than the
// distance we've probed to, `key` cannot be further along, since it
// would have displaced that resident on insert.
static bool
rh_find_index(RHTable     table,
              const char* key,
              uint64_t    hash,
              size_t*     out_index)
{
    size_t   mask     = RH_CAPACITY(table) - 1;
    size_t   pos      = hash & mask;
    size_t   distance = 0;
    RHEntry* entries  = RH_ENTRIES(table);

    while (true)
    {
        RHEntry* slot = &entries [pos];

        if (RHE_STATE(*slot) == EMPTY)
        {
            return false;
        }
        if (RHE_DISTANCE(*slot) < distance)
        {
            return false;
        }
        if ((RHE_HASH(*slot) == hash) && (strcmp(RHE_KEY(*slot), key) == 0))
        {
            *out_index = pos;
            return true;
        }

        pos = (pos + 1) & mask;
        ++distance;
    }
}

// Takes ownership of `key`. Assumes no entry for this key already
// exists in the table.
static void
rh_insert_unique(RHTable table, char* key, uint64_t hash, void* value)
{
    size_t   mask    = RH_CAPACITY(table) - 1;
    size_t   pos     = hash & mask;
    RHEntry* entries = RH_ENTRIES(table);

    RHEntry carry;
    RHE_SET_KEY(carry, key);
    RHE_SET_HASH(carry, hash);
    RHE_SET_VALUE(carry, value);
    RHE_SET_DISTANCE(carry, 0);
    RHE_SET_STATE(carry, USED);

    while (true)
    {
        RHEntry* slot = &entries [pos];

        if (RHE_STATE(*slot) == EMPTY)
        {
            *slot = carry;
            RH_SET_COUNT(table, RH_COUNT(table) + 1);
            return;
        }

        if (RHE_DISTANCE(*slot) < RHE_DISTANCE(carry))
        {
            RHEntry displaced = *slot;
            *slot             = carry;
            carry             = displaced;
        }

        pos = (pos + 1) & mask;
        RHE_SET_DISTANCE(carry, RHE_DISTANCE(carry) + 1);
    }
}

// Doubles capacity and rehashes whenever load factor would exceed
// RH_RESIZE_THRESHOLD_PERCENT. Returns false (table left unchanged) if
// growth was needed but the allocation failed -- callers fail the
// whole operation rather than insert into a table that's silently
// over that threshold.
static bool
rh_maybe_grow(RHTable table)
{
    size_t old_capacity = RH_CAPACITY(table);

    if (((RH_COUNT(table) + 1) * 100) <=
        (old_capacity * RH_RESIZE_THRESHOLD_PERCENT))
    {
        return true;
    }

    RHEntry* old_entries  = RH_ENTRIES(table);
    size_t   new_capacity = old_capacity * 2;
    RHEntry* new_entries =
        (RHEntry*)(calloc(new_capacity, sizeof(struct RHEntry_struct)));
    if (new_entries == NULL)
    {
        // Genuine allocation failure -- not realistically forced without
        // a fault-injecting allocator, so excluded from coverage.
        return false; // LCOV_EXCL_LINE
    }

    RH_SET_ENTRIES(table, new_entries);
    RH_SET_CAPACITY(table, new_capacity);
    RH_SET_COUNT(table, 0);
    for (size_t index = 0; index < old_capacity; ++index)
    {
        if (RHE_STATE(old_entries [index]) == USED)
        {
            rh_insert_unique(table, RHE_KEY(old_entries [index]),
                             RHE_HASH(old_entries [index]),
                             RHE_VALUE(old_entries [index]));
        }
    }

    free(old_entries);
    return true;
}

extern size_t
rh_capacity(RHTable table)
{
    return RH_CAPACITY(table);
}

// Removes a single key via backward-shift deletion: the freed slot is
// backfilled by shifting the following probe run back by one, so no
// tombstones are ever needed.
extern void
rh_clear(RHTable table, const char* key)
{
    uint64_t hash = string_hash(key);
    size_t   index;

    if (!rh_find_index(table, key, hash, &index))
    {
        return;
    }

    size_t   mask    = RH_CAPACITY(table) - 1;
    RHEntry* entries = RH_ENTRIES(table);
    free(RHE_KEY(entries [index]));

    size_t gap  = index;
    size_t next = (gap + 1) & mask;
    while ((RHE_STATE(entries [next]) == USED) &&
           (RHE_DISTANCE(entries [next]) > 0))
    {
        entries [gap] = entries [next];
        RHE_SET_DISTANCE(entries [gap], RHE_DISTANCE(entries [gap]) - 1);
        gap  = next;
        next = (next + 1) & mask;
    }

    RHE_SET_STATE(entries [gap], EMPTY);
    RHE_SET_KEY(entries [gap], NULL);
    RHE_SET_VALUE(entries [gap], NULL);
    RH_SET_COUNT(table, RH_COUNT(table) - 1);
}

extern size_t
rh_count(RHTable table)
{
    return RH_COUNT(table);
}

extern RHTable
rh_create(size_t initial_capacity)
{
    size_t capacity = next_power_of_two(initial_capacity);

    if (capacity == 0)
    {
        return NULL_RHTABLE;
    }

    RHTable result = (RHTable)(malloc(sizeof(struct RHTable_struct)));
    if (result != NULL_RHTABLE)
    {
        RHEntry* entries =
            ((RHEntry*)(calloc(capacity, sizeof(struct RHEntry_struct))));

        if (entries == ((RHEntry*)NULL))
        {
            // Genuine allocation failure -- not realistically forced
            // without a fault-injecting allocator, so excluded from
            // coverage (see rh_maybe_grow()'s equivalent above).
            free((void*)result);  // LCOV_EXCL_START
            result = NULL_RHTABLE;
        } // LCOV_EXCL_STOP
        else
        {
            RH_SET_CAPACITY(result, capacity);
            RH_SET_COUNT(result, 0);
            RH_SET_ENTRIES(result, entries);
        }
    }

    return result;
}

extern void
rh_destroy(RHTable* table)
{
    if ((table == NULL) || (*table == NULL_RHTABLE))
    {
        return;
    }
    rh_empty(*table);
    free(RH_ENTRIES(*table));
    free(*table);
    *table = NULL_RHTABLE;
}

extern void
rh_empty(RHTable table)
{
    size_t   capacity = RH_CAPACITY(table);
    RHEntry* entries  = RH_ENTRIES(table);

    for (size_t index = 0; index < capacity; ++index)
    {
        if (RHE_STATE(entries [index]) == USED)
        {
            free(RHE_KEY(entries [index]));
        }
    }
    memset(entries, 0, capacity * sizeof(struct RHEntry_struct));
    RH_SET_COUNT(table, 0);
}

extern void*
rh_get(RHTable table, const char* key, void* not_found_result)
{
    uint64_t hash = string_hash(key);
    size_t   index;

    if (rh_find_index(table, key, hash, &index))
    {
        return RHE_VALUE(RH_ENTRIES(table) [index]);
    }

    return not_found_result;
}

extern bool
rh_has(RHTable table, const char* key)
{
    uint64_t hash = string_hash(key);
    size_t   index;

    return rh_find_index(table, key, hash, &index);
}

extern void
rh_probe_stats(RHTable table, struct RHProbeStats* out_stats)
{
    size_t   capacity = RH_CAPACITY(table);
    RHEntry* entries  = RH_ENTRIES(table);

    size_t count        = 0;
    size_t max_distance = 0;
    double sum          = 0.0;
    double sum_squares  = 0.0;

    for (size_t bucket = 0; bucket < RH_PROBE_HISTOGRAM_BUCKETS; ++bucket)
    {
        out_stats->histogram [bucket] = 0;
    }

    for (size_t index = 0; index < capacity; ++index)
    {
        if (RHE_STATE(entries [index]) != USED)
        {
            continue;
        }

        size_t distance = RHE_DISTANCE(entries [index]);
        size_t bucket   = (distance < RH_PROBE_HISTOGRAM_BUCKETS)
                              ? distance
                              : (RH_PROBE_HISTOGRAM_BUCKETS - 1);

        ++count;
        ++out_stats->histogram [bucket];
        sum += (double)distance;
        sum_squares += ((double)distance) * ((double)distance);

        if (distance > max_distance)
        {
            max_distance = distance;
        }
    }

    out_stats->count        = count;
    out_stats->max_distance = max_distance;

    if (count == 0)
    {
        out_stats->mean_distance   = 0.0;
        out_stats->stddev_distance = 0.0;
        return;
    }

    double mean     = sum / (double)count;
    double variance = (sum_squares / (double)count) - (mean * mean);

    out_stats->mean_distance   = mean;
    out_stats->stddev_distance = sqrt((variance < 0.0) ? 0.0 : variance);
}

extern bool
rh_set(RHTable table, const char* key, void* value)
{
    uint64_t hash = string_hash(key);
    size_t   index;

    if (rh_find_index(table, key, hash, &index))
    {
        RHE_SET_VALUE(RH_ENTRIES(table) [index], value);
        return true;
    }

    if (!rh_maybe_grow(table))
    {
        return false; // LCOV_EXCL_LINE -- rh_maybe_grow()'s own failure
                      // path is already excluded; nothing new to cover.
    }

    char* key_copy = dup_string(key);
    if (key_copy == NULL)
    {
        // Genuine allocation failure -- not realistically forced without
        // a fault-injecting allocator, so excluded from coverage.
        return false; // LCOV_EXCL_LINE
    }
    rh_insert_unique(table, key_copy, hash, value);
    return true;
}

static void
rhi_advance_to_used(RHIterator iterator)
{
    size_t   capacity = RH_CAPACITY(RHI_TABLE(iterator));
    RHEntry* entries  = RH_ENTRIES(RHI_TABLE(iterator));

    while ((RHI_POSITION(iterator) < capacity) &&
           (RHE_STATE(entries [RHI_POSITION(iterator)]) != USED))
    {
        RHI_SET_POSITION(iterator, RHI_POSITION(iterator) + 1);
    }
}

extern void
rhi_advance(RHIterator iterator)
{
    if (rhi_is_finished(iterator))
    {
        return;
    }

    RHI_SET_POSITION(iterator, RHI_POSITION(iterator) + 1);
    rhi_advance_to_used(iterator);
}

extern RHIterator
rhi_create(RHTable table)
{
    RHIterator iterator =
        (RHIterator)(malloc(sizeof(struct RHIterator_struct)));

    if (iterator != NULL_RHITERATOR)
    {
        RHI_TABLE(iterator) = table;
        RHI_SET_POSITION(iterator, 0);
        rhi_advance_to_used(iterator);
    }

    return iterator;
}

extern void
rhi_destroy(RHIterator iterator)
{
    free(iterator);
}

extern bool
rhi_is_finished(RHIterator iterator)
{
    return RHI_POSITION(iterator) >= RH_CAPACITY(RHI_TABLE(iterator));
}

extern const char*
rhi_key(RHIterator iterator)
{
    if (rhi_is_finished(iterator))
    {
        return (const char*)NULL;
    }

    return RHE_KEY(RH_ENTRIES(RHI_TABLE(iterator)) [RHI_POSITION(iterator)]);
}

extern void
rhi_reset(RHIterator iterator)
{
    RHI_SET_POSITION(iterator, 0);
    rhi_advance_to_used(iterator);
}

// ChatGPT: Simple FNV-1a
static uint64_t
string_hash(const char* text)
{
    uint64_t hash = 1469598103934665603ULL;
    while ((*text) != '\0')
    {
        hash ^= ((unsigned char)(*(text++)));
        hash *= 1099511628211ULL;
    }
    return hash;
}
