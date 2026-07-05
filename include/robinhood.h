//
// robinhood.h - part of robinhood, a hash table with Robin Hood insertion
//
// SPDX-License-Identifier: Unlicense
//

///
/// @file
/// @brief A hash table with Robin Hood open-addressing insertion and
///        backward-shift deletion (no tombstones).
///
/// Not thread-safe internally, by design: concurrent operations on
/// the *same* RHTable or RHIterator (including one of each derived
/// from the other) are entirely possible, but not built in -- that's
/// on the caller to synchronize (a lock or equivalent around those
/// calls). Different tables may be used concurrently from different
/// threads with no extra care at all; see README.md's "Thread safety"
/// section for why a per-table lock isn't provided.
///

#ifndef ROBIN_HOOD_HEADER_INCLUDED
#define ROBIN_HOOD_HEADER_INCLUDED

#include <stdbool.h>
#include <stddef.h>

// ===========================================================================
// Table operations
// ===========================================================================

///
/// Opaque handle to a hash table. Create with rh_create() and release
/// with rh_destroy(). All other rh_*() functions expect a valid,
/// non-NULL handle.
///
typedef struct RHTable_struct* RHTable;

///
/// Returns the table's current storage capacity: how many slots are
/// allocated, not how many keys are stored (see rh_count() for that).
/// Always a power of two, and grows automatically as needed -- see
/// rh_set().
///
/// @param table  The table to query.
/// @return       The table's current capacity, in slots.
///
extern size_t
rh_capacity(RHTable table);

///
/// Removes a single key, if present, via backward-shift deletion (no
/// tombstones). A no-op if `key` is not in the table. Frees the
/// table's internal copy of `key`; does not free the associated
/// value -- the caller remains responsible for that.
///
/// @param table  The table to remove the key from.
/// @param key    The key to remove.
///
extern void
rh_clear(RHTable table, const char* key);

///
/// Returns the number of keys currently stored in the table.
///
/// @param table  The table to query.
/// @return       The number of keys currently stored.
///
extern size_t
rh_count(RHTable table);

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
extern RHTable
rh_create(size_t initial_capacity);

///
/// Frees a table and all of its internal key copies, then sets
/// `*table` to NULL. Does not free caller-supplied values. Safe to
/// call with `table` NULL, or with `*table` already NULL -- both are
/// a no-op.
///
/// @param table  Address of the table handle to destroy.
///
extern void
rh_destroy(RHTable* table);

///
/// Removes every key from the table, freeing each internal key copy.
/// Does not free caller-supplied values, and does not shrink the
/// table's allocated capacity.
///
/// @param table  The table to empty.
///
extern void
rh_empty(RHTable table);

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
extern void*
rh_get(RHTable table, const char* key, void* not_found_result);

///
/// Checks whether `key` is present in the table.
///
/// @param table  The table to search.
/// @param key    The key to look for.
/// @return       true if `key` is present, false otherwise.
///
extern bool
rh_has(RHTable table, const char* key);

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
    size_t count;           ///< Entries scanned (the table's key count).
    size_t max_distance;    ///< The largest probe distance seen.
    double mean_distance;   ///< The average probe distance.
    double stddev_distance; ///< The probe distance's standard deviation.

    /// Entry count per bucket -- see RH_PROBE_HISTOGRAM_BUCKETS for how
    /// a distance maps to a bucket index.
    size_t histogram[RH_PROBE_HISTOGRAM_BUCKETS];
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
extern void
rh_probe_stats(RHTable table, struct RHProbeStats* out_stats);

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
/// @return       true if the key/value pair is now in the table; false
///               on allocation failure -- including a failed resize,
///               which fails the whole operation rather than
///               silently exceeding the table's 80% load-factor
///               growth trigger.
///
extern bool
rh_set(RHTable table, const char* key, void* value);

// ===========================================================================
// Iterator operations
// ===========================================================================

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
extern void
rhi_advance(RHIterator iterator);

///
/// Creates a new iterator over `table`, positioned at the first key
/// (if any).
///
/// @param table  The table to iterate over. The table must outlive
///               the iterator, and should not be modified while the
///               iterator is in use.
/// @return       A new iterator, or NULL on allocation failure.
///
extern RHIterator
rhi_create(RHTable table);

///
/// Frees an iterator. Does not affect the underlying table.
///
/// @param iterator  The iterator to destroy.
///
extern void
rhi_destroy(RHIterator iterator);

///
/// Checks whether an iterator has passed the last key.
///
/// @param iterator  The iterator to check.
/// @return          true if there are no more keys to visit, false
///                  otherwise.
///
extern bool
rhi_is_finished(RHIterator iterator);

///
/// Returns the key at the iterator's current position.
///
/// @param iterator  The iterator to read from.
/// @return          The current key, or NULL if the iterator is
///                  finished -- see rhi_is_finished().
///
extern const char*
rhi_key(RHIterator iterator);

///
/// Rewinds an iterator back to the first key (if any), as if freshly
/// created with rhi_create().
///
/// @param iterator  The iterator to rewind.
///
extern void
rhi_reset(RHIterator iterator);

#endif // ROBIN_HOOD_HEADER_INCLUDED
