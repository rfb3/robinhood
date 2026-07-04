//
// robinhood.h - part of robinhood, a hash table with Robin Hood insertion
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

// robinhood.h - part of robinhood, a hash table with Robin Hood insertion
// Table of Contents
// Headers, etc.
// Table operations
// Iterator operations
// Global configuration

//
// Headers, etc.
//

///
/// @file
/// @brief A hash table with Robin Hood open-addressing insertion and
///        backward-shift deletion (no tombstones).
///
/// Not thread-safe: concurrent operations on the *same* RHTable or
/// RHIterator (including one of each derived from the other) require
/// external synchronization by the caller. Different tables may be
/// used concurrently from different threads with no extra care. See
/// rh_set_warning_handler() for the one piece of process-wide global
/// state and its own, narrower guarantee.
///

#ifndef ROBIN_HOOD_HEADER_INCLUDED
#define ROBIN_HOOD_HEADER_INCLUDED

#include <stdbool.h>
#include <stddef.h>

//
// Table operations
//

///
/// Opaque handle to a hash table. Create with rh_create() and release
/// with rh_destroy(). All other rh_*() functions expect a valid,
/// non-NULL handle.
///
typedef struct RHTable_struct*    RHTable;

///
/// Returns the table's current storage capacity: how many slots are
/// allocated, not how many keys are stored (see rh_count() for that).
/// Always a power of two, and grows automatically as needed -- see
/// rh_set().
///
/// @param table  The table to query.
/// @return       The table's current capacity, in slots.
///
extern
size_t
rh_capacity (RHTable table);

///
/// Removes a single key, if present, via backward-shift deletion (no
/// tombstones). A no-op if `key` is not in the table. Frees the
/// table's internal copy of `key`; does not free the associated
/// value -- the caller remains responsible for that.
///
/// @param table  The table to remove the key from.
/// @param key    The key to remove.
///
extern
void
rh_clear (RHTable     table,
          const char* key);

///
/// Returns the number of keys currently stored in the table.
///
/// @param table  The table to query.
/// @return       The number of keys currently stored.
///
extern
size_t
rh_count (RHTable table);

///
/// Creates a new, empty hash table.
///
/// @param initial_capacity  The minimum number of slots to allocate
///                          up front; rounded up to the next power of
///                          two (0 and 1 both round up to 1). The
///                          table still grows automatically beyond
///                          this as needed -- see rh_set().
/// @return                  A new table, or NULL on allocation
///                          failure (including if `initial_capacity`
///                          is too large to round up without
///                          overflow).
///
extern
RHTable
rh_create (size_t initial_capacity);

///
/// Frees a table and all of its internal key copies, then sets
/// `*table` to NULL. Does not free caller-supplied values. Safe to
/// call with `table` NULL, or with `*table` already NULL -- both are
/// a no-op.
///
/// @param table  Address of the table handle to destroy.
///
extern
void
rh_destroy (RHTable* table);

///
/// Removes every key from the table, freeing each internal key copy.
/// Does not free caller-supplied values, and does not shrink the
/// table's allocated capacity.
///
/// @param table  The table to empty.
///
extern
void
rh_empty (RHTable table);

///
/// Looks up `key` in the table.
///
/// @param table              The table to search.
/// @param key                The key to look up.
/// @param not_found_result   The value to return if `key` is not
///                           found.
/// @return                   The value associated with `key`, or
///                           `not_found_result` if `key` is not in
///                           the table.
///
extern
void*
rh_get (RHTable     table,
        const char* key,
        void*       not_found_result);

///
/// Checks whether `key` is present in the table.
///
/// @param table  The table to search.
/// @param key    The key to look for.
/// @return       true if `key` is present, false otherwise.
///
extern
bool
rh_has (RHTable     table,
        const char* key);

/// Number of buckets in `RHProbeStats.histogram` -- bucket `i` counts
/// entries at exactly probe distance `i`, for `i` less than this minus
/// one; the last bucket catches everything at or beyond that.
#define RH_PROBE_HISTOGRAM_BUCKETS (8)

///
/// A snapshot of how far Robin Hood probing has currently displaced a
/// table's entries from their ideal slots. Robin Hood's whole premise
/// is keeping this short and tightly distributed rather than letting
/// any one key pay for a long probe chain -- a `max_distance` or
/// `stddev_distance` that's high relative to `mean_distance` means
/// that promise isn't holding up as well as expected. See
/// rh_probe_stats().
///
struct RHProbeStats
{
    size_t count;
    size_t max_distance;
    double mean_distance;
    double stddev_distance;
    size_t histogram [RH_PROBE_HISTOGRAM_BUCKETS];
};

///
/// Computes probe-depth statistics over `table`'s current entries.
/// This is a full O(capacity) scan, not a running total maintained
/// incrementally -- cheap next to the cost of populating a table in
/// the first place, but still real work, so call it only when you
/// actually want the numbers, not from a hot path.
///
/// @param table      The table to inspect.
/// @param out_stats  Filled in with the computed statistics.
///
extern
void
rh_probe_stats (RHTable              table,
                struct RHProbeStats* out_stats);

///
/// Returns the table's current resize threshold, as a percentage
/// (1-100) of capacity: the table grows just before an insertion
/// would push its load factor past this value. Defaults to 80. See
/// rh_set_resize_threshold() to change it.
///
/// @param table  The table to query.
/// @return       The table's current resize threshold, 1-100.
///
extern
unsigned int
rh_resize_threshold (RHTable table);

///
/// Inserts `key` with `value`, or updates the value if `key` is
/// already present. The table keeps its own copy of `key` (the
/// caller's string is not retained); `value` itself is stored as
/// given and is never freed or copied by the table. May grow the
/// table's capacity first -- see rh_capacity().
///
/// @param table  The table to insert or update in.
/// @param key    The key to insert or update.
/// @param value  The value to associate with `key`.
///
extern
void
rh_set (RHTable     table,
        const char* key,
        void*       value);

///
/// Changes the table's resize threshold -- see rh_resize_threshold().
/// A lower value keeps probe chains shorter at the cost of more
/// frequent resizing (and more wasted capacity); a higher value packs
/// the table tighter at the cost of longer probe chains as it fills
/// up. Takes effect lazily: it only affects the table's next
/// insertion, and does not itself trigger an immediate resize even if
/// the table's current load factor already exceeds the new value.
///
/// @param table    The table to configure.
/// @param percent  The new resize threshold, which must be between 1
///                 and 100 inclusive.
/// @return         true if `percent` was valid and applied, false
///                 (leaving the threshold unchanged) otherwise.
///
extern
bool
rh_set_resize_threshold (RHTable      table,
                         unsigned int percent);

//
// Iterator operations
//

///
/// Opaque handle to an in-progress iteration over an RHTable's
/// entries. Create with rhi_create() and release with rhi_destroy().
/// Iteration order is unspecified (table storage order, not insertion
/// order).
///
typedef struct RHIterator_struct* RHIterator;

///
/// Advances the iterator to the next key, if any. A no-op if the
/// iterator is already finished -- see rhi_is_finished().
///
/// @param iterator  The iterator to advance.
///
extern
void
rhi_advance (RHIterator iterator);

///
/// Creates a new iterator over `table`, positioned at the first key
/// (if any).
///
/// @param table  The table to iterate over. The table must outlive
///               the iterator, and should not be modified while the
///               iterator is in use.
/// @return       A new iterator, or NULL on allocation failure.
///
extern
RHIterator
rhi_create (RHTable table);

///
/// Frees an iterator. Does not affect the underlying table.
///
/// @param iterator  The iterator to destroy.
///
extern
void
rhi_destroy (RHIterator iterator);

///
/// Checks whether an iterator has passed the last key.
///
/// @param iterator  The iterator to check.
/// @return          true if there are no more keys to visit, false
///                  otherwise.
///
extern
bool
rhi_is_finished (RHIterator iterator);

///
/// Returns the key at the iterator's current position.
///
/// @param iterator  The iterator to read from.
/// @return          The current key, or NULL if the iterator is
///                  finished -- see rhi_is_finished().
///
extern
const char*
rhi_key (RHIterator iterator);

///
/// Rewinds an iterator back to the first key (if any), as if freshly
/// created with rhi_create().
///
/// @param iterator  The iterator to rewind.
///
extern
void
rhi_reset (RHIterator iterator);

//
// Global configuration
//

///
/// Signature for a custom warning handler -- see
/// rh_set_warning_handler(). `message` is a single, complete,
/// already-formatted string (no trailing newline) describing what
/// happened. It's only valid for the duration of the call; copy it if
/// you need to keep it.
///
typedef void (*RHWarningHandler) (const char* message);

///
/// The library's own default warning handler: prints `message` to
/// stderr via warn(3) (prefixed with the program's name, per warn(3)'s
/// own behavior). Installed automatically -- there's no need to pass
/// this to rh_set_warning_handler() yourself unless you've already
/// installed something else and want to restore default behavior.
///
/// @param message  The message to print, as described in
///                 RHWarningHandler's own documentation.
///
extern
void
rh_default_warning_handler (const char* message);

///
/// Installs a handler for the library's internal warnings (currently:
/// allocation failures inside rh_create()/rh_set()/rh_maybe_grow()/
/// rhi_create()). This is a global, process-wide setting, not
/// per-table -- some warnings (rh_create() failing to allocate the
/// table itself) happen before any RHTable exists to attach a
/// per-instance setting to. Like the rest of this library, it's
/// unsynchronized global state -- fine for single-threaded use or if
/// you set it once up front. warning() reads this exactly once per
/// call, so a concurrent change can't crash it (e.g. by seeing NULL at
/// one read and a stale handler at another) -- but which handler
/// receives any given in-flight warning is still unpredictable, so
/// this isn't a substitute for real synchronization if you need
/// deterministic ordering.
///
/// @param handler  Called for each warning from then on. Pass NULL to
///                 suppress warnings entirely. The default, before
///                 this is ever called, prints to stderr (see
///                 warn(3)) -- programs that never call this see no
///                 change in behavior.
///
extern
void
rh_set_warning_handler (RHWarningHandler handler);

#endif // ROBIN_HOOD_HEADER_INCLUDED
