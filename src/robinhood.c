//
// robinhood.c - part of robinhood, a hash table with Robin Hood insertion
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

// robinhood.c - part of robinhood, a hash table with Robin Hood insertion
// Table of Contents
// Headers, etc.
// RHEntry type definition
// RHTable type definition
// RHIterator type definition
// File-local prototypes
// dup_string(const char*)
// failure(int,const char*,int,const char*,const char*)
// next_power_of_two(size_t)
// rh_find_index(RHTable,const char*,uint64_t,size_t*)
// rh_insert_unique(RHTable,char*,uint64_t,void*)
// rh_maybe_grow(RHTable)
// rh_capacity(RHTable)
// rh_clear(RHTable,const char*)
// rh_count(RHTable)
// rh_create(size_t)
// rh_destroy(RHTable*)
// rh_empty(RHTable)
// rh_get(RHTable,const char*,void*)
// rh_has(RHTable,const char*)
// rh_probe_stats(RHTable,struct RHProbeStats*)
// rh_resize_threshold(RHTable)
// rh_default_warning_handler(const char*)
// rh_set(RHTable,const char*,void*)
// rh_set_resize_threshold(RHTable,unsigned int)
// rh_set_warning_handler(RHWarningHandler)
// rhi_advance_to_used(RHIterator)
// rhi_advance(RHIterator)
// rhi_create(RHTable)
// rhi_destroy(RHIterator)
// rhi_is_finished(RHIterator)
// rhi_key(RHIterator)
// rhi_reset(RHIterator)
// string_hash(const char*)
// warning(const char*,int,const char*,const char*)

//
// Headers, etc.
//

#include "robinhood.h"

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//
// RHEntry type definition
//

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

#define RHE_SET_KEY(ENTRY,KEY)           (((ENTRY).key)      = (KEY))
#define RHE_SET_HASH(ENTRY,HASH)         (((ENTRY).hash)     = (HASH))
#define RHE_SET_VALUE(ENTRY,VALUE)       (((ENTRY).value)    = (VALUE))
#define RHE_SET_DISTANCE(ENTRY,DISTANCE) (((ENTRY).distance) = (DISTANCE))
#define RHE_SET_STATE(ENTRY,STATE)       (((ENTRY).state)    = (STATE))

//
// RHTable type definition
//

// See rh_resize_threshold()'s doc comment in robinhood.h for details.
#define RH_DEFAULT_RESIZE_THRESHOLD_PERCENT (80)

struct RHTable_struct
{
    RHEntry*     entries;
    size_t       capacity;
    size_t       count;
    unsigned int resize_threshold_percent;
};

typedef struct RHTable_struct* RHTable;

#define NULL_RHTABLE ((RHTable)NULL)

#define RH_ENTRIES(TABLE)  ((TABLE)->entries)
#define RH_CAPACITY(TABLE) ((TABLE)->capacity)
#define RH_COUNT(TABLE)    ((TABLE)->count)
#define RH_RESIZE_THRESHOLD_PERCENT(TABLE) ((TABLE)->resize_threshold_percent)

#define RH_SET_ENTRIES(TABLE,ENTRIES)   (((TABLE)->entries)  = (ENTRIES))
#define RH_SET_CAPACITY(TABLE,CAPACITY) (((TABLE)->capacity) = (CAPACITY))
#define RH_SET_COUNT(TABLE,COUNT)       (((TABLE)->count)    = (COUNT))
#define RH_SET_RESIZE_THRESHOLD_PERCENT(TABLE,PERCENT) \
    (((TABLE)->resize_threshold_percent) = (PERCENT))

//
// RHIterator type definition
//

struct RHIterator_struct
{
    RHTable table;
    size_t  position;
};

typedef struct RHIterator_struct* RHIterator;

#define NULL_RHITERATOR ((RHIterator)NULL)

#define RHI_TABLE(ITERATOR)    ((ITERATOR)->table)
#define RHI_POSITION(ITERATOR) ((ITERATOR)->position)

#define RHI_SET_POSITION(ITERATOR,POSITION)     \
    (((ITERATOR)->position) = (POSITION))

//
// File-local prototypes
//

static
char*
dup_string (const char* text);

static
void
failure (int         exit_value,
         const char* file,
         int         line,
         const char* routine,
         const char* argument);

static
size_t
next_power_of_two (size_t n);

static
bool
rh_find_index (RHTable     table,
               const char* key,
               uint64_t    hash,
               size_t*     out_index);

static
void
rh_insert_unique (RHTable  table,
                  char*    key,
                  uint64_t hash,
                  void*    value);

static
void
rh_maybe_grow (RHTable table);

static
void
rhi_advance_to_used (RHIterator iterator);

static
uint64_t
string_hash (const char* text);

static
void
warning (const char* file,
         int         line,
         const char* routine,
         const char* argument);

//
// dup_string(const char*)
//

static
char*
dup_string (const char* text)
{
    size_t length = strlen (text) + 1;
    char*  copy   = (char*)(malloc (length));

    if (copy == ((void*)NULL))
    {
        warning (__FILE__, __LINE__, "malloc", text);
    }
    else
    {
        memcpy (copy, text, length);
    }

    return copy;
}

//
// failure(int,const char*,int,const char*,const char*)
//

#define FAILURE_BUFFER_SIZE (1024)

// Unlike warning(), this uses a static buffer, making it non-reentrant, which
// is not ideal, but given the context that the program has already
// encountered a fatal situation, perhaps that's not so terrible.
//
static
void
failure (int         exit_value,
         const char* file,
         int         line,
         const char* routine,
         const char* argument)
{
    static char buffer [FAILURE_BUFFER_SIZE];
    char*       call   = (char*)routine;      // Will not modify `routine`

    fprintf (stderr, "%s:%d: ", file, line);
    if (argument != ((const char*)NULL))
    {
        snprintf (buffer, FAILURE_BUFFER_SIZE, "%s(%s)", routine, argument);
        call = buffer;
    }
    err (exit_value, "%s", call);
}

//
// next_power_of_two(size_t)
//

static
size_t
next_power_of_two (size_t n)
{
    if (n <= 1)
    {
        return 1;
    }
    if (n > ((size_t)1 << (sizeof (size_t) * CHAR_BIT - 1)))
    {
        return 0;   // overflow
    }
    --n;
    for (size_t shift = 1; shift < sizeof (size_t) * CHAR_BIT; shift <<= 1)
    {
        n |= n >> shift;
    }

    return n + 1;
}

//
// rh_find_index(RHTable,const char*,uint64_t,size_t*)
//

// Relies on the Robin Hood invariant that distances never decrease
// along a probe run: once a resident's distance is less than the
// distance we've probed to, `key` cannot be further along, since it
// would have displaced that resident on insert.
static
bool
rh_find_index (RHTable     table,
               const char* key,
               uint64_t    hash,
               size_t*     out_index)
{
    size_t   mask     = RH_CAPACITY (table) - 1;
    size_t   pos      = hash & mask;
    size_t   distance = 0;
    RHEntry* entries  = RH_ENTRIES (table);

    while (true)
    {
        RHEntry* slot = &entries [pos];

        if (RHE_STATE (*slot) == EMPTY)
        {
            return false;
        }
        if (RHE_DISTANCE (*slot) < distance)
        {
            return false;
        }
        if ((RHE_HASH (*slot) == hash)
            && (strcmp (RHE_KEY (*slot), key) == 0))
        {
            *out_index = pos;
            return true;
        }

        pos = (pos + 1) & mask;
        ++distance;
    }
}

//
// rh_insert_unique(RHTable,char*,uint64_t,void*)
//

// Takes ownership of `key`. Assumes no entry for this key already
// exists in the table.
static
void
rh_insert_unique (RHTable  table,
                  char*    key,
                  uint64_t hash,
                  void*    value)
{
    size_t   mask    = RH_CAPACITY (table) - 1;
    size_t   pos     = hash & mask;
    RHEntry* entries = RH_ENTRIES (table);

    RHEntry carry;
    RHE_SET_KEY      (carry, key);
    RHE_SET_HASH     (carry, hash);
    RHE_SET_VALUE    (carry, value);
    RHE_SET_DISTANCE (carry, 0);
    RHE_SET_STATE    (carry, USED);

    while (true)
    {
        RHEntry* slot = &entries [pos];

        if (RHE_STATE (*slot) == EMPTY)
        {
            *slot = carry;
            RH_SET_COUNT (table, RH_COUNT (table) + 1);
            return;
        }

        if (RHE_DISTANCE (*slot) < RHE_DISTANCE (carry))
        {
            RHEntry displaced = *slot;
            *slot = carry;
            carry = displaced;
        }

        pos = (pos + 1) & mask;
        RHE_SET_DISTANCE (carry, RHE_DISTANCE (carry) + 1);
    }
}

//
// rh_maybe_grow(RHTable)
//

// Doubles capacity and rehashes whenever load factor would exceed the
// table's own resize_threshold_percent -- see rh_resize_threshold().
static
void
rh_maybe_grow (RHTable table)
{
    size_t old_capacity = RH_CAPACITY (table);

    if (((RH_COUNT (table) + 1) * 100)
        <= (old_capacity * RH_RESIZE_THRESHOLD_PERCENT (table)))
    {
        return;
    }

    RHEntry* old_entries = RH_ENTRIES (table);
    size_t   new_capacity = old_capacity * 2;
    RHEntry* new_entries
        = (RHEntry*)(calloc (new_capacity, sizeof (struct RHEntry_struct)));
    if (new_entries == NULL)
    {
        warning (__FILE__, __LINE__, "calloc", (char*)NULL);
        return;
    }

    RH_SET_ENTRIES  (table, new_entries);
    RH_SET_CAPACITY (table, new_capacity);
    RH_SET_COUNT    (table, 0);
    for (size_t index = 0; index < old_capacity; ++index)
    {
        if (RHE_STATE (old_entries [index]) == USED)
        {
            rh_insert_unique (table,
                              RHE_KEY (old_entries [index]),
                              RHE_HASH (old_entries [index]),
                              RHE_VALUE (old_entries [index]));
        }
    }

    free (old_entries);
}

//
// rh_capacity(RHTable)
//

extern
size_t
rh_capacity (RHTable table)
{
    return RH_CAPACITY (table);
}

//
// rh_clear(RHTable,const char*)
//

// Removes a single key via backward-shift deletion: the freed slot is
// backfilled by shifting the following probe run back by one, so no
// tombstones are ever needed.
extern
void
rh_clear (RHTable     table,
          const char* key)
{
    uint64_t hash = string_hash (key);
    size_t   index;

    if (!rh_find_index (table, key, hash, &index))
    {
        return;
    }

    size_t   mask    = RH_CAPACITY (table) - 1;
    RHEntry* entries = RH_ENTRIES (table);
    free (RHE_KEY (entries [index]));

    size_t gap  = index;
    size_t next = (gap + 1) & mask;
    while ((RHE_STATE (entries [next]) == USED)
           && (RHE_DISTANCE (entries [next]) > 0))
    {
        entries [gap] = entries [next];
        RHE_SET_DISTANCE (entries [gap], RHE_DISTANCE (entries [gap]) - 1);
        gap  = next;
        next = (next + 1) & mask;
    }

    RHE_SET_STATE (entries [gap], EMPTY);
    RHE_SET_KEY   (entries [gap], NULL);
    RHE_SET_VALUE (entries [gap], NULL);
    RH_SET_COUNT  (table, RH_COUNT (table) - 1);
}

//
// rh_count(RHTable)
//

extern
size_t
rh_count (RHTable table)
{
    return RH_COUNT (table);
}

//
// rh_create(size_t)
//

extern
RHTable
rh_create (size_t initial_capacity)
{
    size_t capacity = next_power_of_two (initial_capacity);

    if (capacity == 0)
    {
        return NULL_RHTABLE;
    }

    RHTable result = (RHTable)(malloc (sizeof (struct RHTable_struct)));
    if (result == NULL_RHTABLE)
    {
        warning (__FILE__, __LINE__, "malloc", (char*)NULL);
    }
    else
    {
        RHEntry* entries
            = ((RHEntry*)(calloc (capacity, sizeof (struct RHEntry_struct))));

        if (entries == ((RHEntry*)NULL))
        {
            warning (__FILE__, __LINE__, "calloc", (char*)NULL);
            free ((void*)result);
            result = NULL_RHTABLE;
        }
        else
        {
            RH_SET_CAPACITY (result, capacity);
            RH_SET_COUNT (result, 0);
            RH_SET_ENTRIES (result, entries);
            RH_SET_RESIZE_THRESHOLD_PERCENT
                (result, RH_DEFAULT_RESIZE_THRESHOLD_PERCENT);
        }
    }

    return result;
}

//
// rh_destroy(RHTable*)
//

extern
void
rh_destroy (RHTable* table)
{
    if ((table == NULL) || (*table == NULL_RHTABLE))
    {
        return;
    }
    rh_empty (*table);
    free (RH_ENTRIES (*table));
    free (*table);
    *table = NULL_RHTABLE;
}

//
// rh_empty(RHTable)
//

extern
void
rh_empty (RHTable table)
{
    size_t   capacity = RH_CAPACITY (table);
    RHEntry* entries  = RH_ENTRIES (table);

    for (size_t index = 0; index < capacity; ++index)
    {
        if (RHE_STATE (entries [index]) == USED)
        {
            free (RHE_KEY (entries [index]));
        }
    }
    memset (entries, 0, capacity * sizeof (struct RHEntry_struct));
    RH_SET_COUNT (table, 0);
}

//
// rh_get(RHTable,const char*,void*)
//

extern
void*
rh_get (RHTable     table,
        const char* key,
        void*       not_found_result)
{
    uint64_t hash = string_hash (key);
    size_t   index;

    if (rh_find_index (table, key, hash, &index))
    {
        return RHE_VALUE (RH_ENTRIES (table)[index]);
    }

    return not_found_result;
}

//
// rh_has(RHTable,const char*)
//

extern
bool
rh_has (RHTable     table,
        const char* key)
{
    uint64_t hash = string_hash (key);
    size_t   index;

    return rh_find_index (table, key, hash, &index);
}

//
// rh_probe_stats(RHTable,struct RHProbeStats*)
//

extern
void
rh_probe_stats (RHTable              table,
                struct RHProbeStats* out_stats)
{
    size_t   capacity = RH_CAPACITY (table);
    RHEntry* entries  = RH_ENTRIES (table);

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
        if (RHE_STATE (entries [index]) != USED)
        {
            continue;
        }

        size_t distance = RHE_DISTANCE (entries [index]);
        size_t bucket
            = (distance < RH_PROBE_HISTOGRAM_BUCKETS)
              ? distance : (RH_PROBE_HISTOGRAM_BUCKETS - 1);

        ++count;
        ++out_stats->histogram [bucket];
        sum         += (double)distance;
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
    out_stats->stddev_distance = sqrt ((variance < 0.0) ? 0.0 : variance);
}

//
// rh_resize_threshold(RHTable)
//

extern
unsigned int
rh_resize_threshold (RHTable table)
{
    return RH_RESIZE_THRESHOLD_PERCENT (table);
}

//
// rh_default_warning_handler(const char*)
//

// Unsynchronized global state, like the rest of this file -- see
// rh_set_warning_handler()'s doc comment for what that means in
// practice. Starts pointing at this file's own default so programs
// that never call rh_set_warning_handler() see no change in behavior.
static RHWarningHandler warning_handler = rh_default_warning_handler;

extern
void
rh_default_warning_handler (const char* message)
{
    warnx ("%s", message);
}

//
// rh_set(RHTable,const char*,void*)
//

extern
void
rh_set (RHTable     table,
        const char* key,
        void*       value)
{
    uint64_t hash = string_hash (key);
    size_t   index;

    if (rh_find_index (table, key, hash, &index))
    {
        RHE_SET_VALUE (RH_ENTRIES (table)[index], value);
        return;
    }
    rh_maybe_grow (table);

    char* key_copy = dup_string (key);
    if (key_copy == NULL)
    {
        return;
    }
    rh_insert_unique (table, key_copy, hash, value);
}

//
// rh_set_resize_threshold(RHTable,unsigned int)
//

extern
bool
rh_set_resize_threshold (RHTable      table,
                         unsigned int percent)
{
    if ((percent < 1) || (percent > 100))
    {
        return false;
    }
    RH_SET_RESIZE_THRESHOLD_PERCENT (table, percent);
    return true;
}

//
// rh_set_warning_handler(RHWarningHandler)
//

extern
void
rh_set_warning_handler (RHWarningHandler handler)
{
    warning_handler = handler;
}

//
// rhi_advance_to_used(RHIterator)
//

static
void
rhi_advance_to_used (RHIterator iterator)
{
    size_t   capacity = RH_CAPACITY (RHI_TABLE (iterator));
    RHEntry* entries  = RH_ENTRIES (RHI_TABLE (iterator));

    while ((RHI_POSITION (iterator) < capacity)
           && (RHE_STATE (entries [RHI_POSITION (iterator)]) != USED))
    {
        RHI_SET_POSITION (iterator, RHI_POSITION (iterator) + 1);
    }
}

//
// rhi_advance(RHIterator)
//

extern
void
rhi_advance (RHIterator iterator)
{
    if (rhi_is_finished (iterator))
    {
        return;
    }

    RHI_SET_POSITION (iterator, RHI_POSITION (iterator) + 1);
    rhi_advance_to_used (iterator);
}

//
// rhi_create(RHTable)
//

extern
RHIterator
rhi_create (RHTable table)
{
    RHIterator iterator
        = (RHIterator)(malloc (sizeof (struct RHIterator_struct)));

    if (iterator == NULL_RHITERATOR)
    {
        warning (__FILE__, __LINE__, "malloc", (char*)NULL);
    }
    else
    {
        RHI_TABLE (iterator) = table;
        RHI_SET_POSITION (iterator, 0);
        rhi_advance_to_used (iterator);
    }

    return iterator;
}

//
// rhi_destroy(RHIterator)
//

extern
void
rhi_destroy (RHIterator iterator)
{
    free (iterator);
}

//
// rhi_is_finished(RHIterator)
//

extern
bool
rhi_is_finished (RHIterator iterator)
{
    return RHI_POSITION (iterator) >= RH_CAPACITY (RHI_TABLE (iterator));
}

//
// rhi_key(RHIterator)
//

extern
const char*
rhi_key (RHIterator iterator)
{
    if (rhi_is_finished (iterator))
    {
        return (const char*)NULL;
    }

    return RHE_KEY (
        RH_ENTRIES (RHI_TABLE (iterator))[RHI_POSITION (iterator)]);
}

//
// rhi_reset(RHIterator)
//

extern
void
rhi_reset (RHIterator iterator)
{
    RHI_SET_POSITION (iterator, 0);
    rhi_advance_to_used (iterator);
}

//
// string_hash(const char*)
//

// ChatGPT: Simple FNV-1a
static
uint64_t
string_hash (const char* text)
{
    uint64_t hash = 1469598103934665603ULL;
    while ((*text) != '\0')
    {
        hash ^= ((unsigned char)(*(text++)));
        hash *= 1099511628211ULL;
    }
    return hash;
}

//
// warning(const char*,int,const char*,const char*)
//

#define WARNING_BUFFER_SIZE (1024)

static
void
warning (const char* file,
         int         line,
         const char* routine,
         const char* argument)
{
    // Read exactly once into a local: warning_handler is a global that
    // rh_set_warning_handler() can change from another thread at any
    // time, and re-reading it later (e.g. a second check-then-call)
    // could see it turn NULL between the check and the call below.
    RHWarningHandler handler = warning_handler;

    if (handler == NULL)
    {
        return;
    }

    int   saved_errno = errno;           // Read before any other libc calls
    char* call        = (char*)routine;  // Will not modify `routine`

    if (argument != ((const char*)NULL))
    {
        int buffer_size = strlen (routine) + strlen (argument) + 3;
        call = malloc (buffer_size);
        if (call == ((void*)NULL))
        {
            failure (1, __FILE__, __LINE__, "malloc", (char*)NULL);
        }
        else
        {
            snprintf (call, buffer_size, "%s(%s)", routine, argument);
        }
    }

    char message [WARNING_BUFFER_SIZE];
    snprintf (message, sizeof (message), "%s:%d: %s: %s",
              file, line, call, strerror (saved_errno));

    if (call != routine)
    {
        free (call);
    }

    handler (message);
}
