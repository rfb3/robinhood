# Performance notes

Empirical findings from exercising the `robinhood` hash table, gathered
mainly via the `scan` tool (`make scan`, then `./scan <directory>`), which
walks a directory tree and inserts one entry per path, keyed by pathname,
with a heap-allocated copy of that path's `struct stat` as the value. The
"Other examples" section near the end covers what running `memo`,
`wordfreq`, `netifs`, and `environ` at scale turned up.

## Table of contents

- [Traversal engine](#traversal-engine)
- [The `/dev/fd` incident](#the-devfd-incident)
- [Growth behavior](#growth-behavior)
- [Large-scale scans](#large-scale-scans)
- [Caveats](#caveats)
- [Other examples](#other-examples)
  - [`memo`: the memoization payoff, made concrete](#memo-the-memoization-payoff-made-concrete)
  - [`wordfreq`: real-corpus throughput, and a case-folding sanity check](#wordfreq-real-corpus-throughput-and-a-case-folding-sanity-check)
  - [`environ`: synthetic scaling and the ARG_MAX ceiling](#environ-synthetic-scaling-and-the-arg_max-ceiling)
  - [`netifs`: a null result](#netifs-a-null-result)
- [Is Robin Hood living up to expectations?](#is-robin-hood-living-up-to-expectations)
  - [Overhead](#overhead)
- [Configurable resize threshold: does anything beat the 80% default?](#configurable-resize-threshold-does-anything-beat-the-80-default)

## Traversal engine

`scan` originally used `nftw` (POSIX.1-2001 XSI). It was replaced with a
hand-rolled recursive walker built on plain `opendir`/`readdir`/`lstat`/
`closedir` (POSIX.1 base, not even the XSI option group), for three
reasons found while using it:

- **Maximum portability.** `nftw` is POSIX via XSI; `fts` (the other usual
  candidate, needed for real subtree pruning) was never standardized at
  all and has had real historical portability warts (e.g. a long-standing
  32-bit `ino_t`/`off_t` truncation bug in glibc). `opendir`/`readdir`/
  `lstat` are guaranteed on every POSIX system with zero signature
  ambiguity.
- **Genuine subtree pruning.** The `--exclude PATH` option (see below)
  needs to stop descent entirely, not just filter what gets stored.
  `nftw`'s subtree-skip mechanism (`FTW_ACTIONRETVAL`/`FTW_SKIP_SUBTREE`)
  is a glibc-only extension — confirmed absent from macOS's `<ftw.h>`
  (OpenBSD-derived: only `FTW_PHYS`/`FTW_MOUNT`/`FTW_DEPTH`/`FTW_CHDIR`
  exist there) — so it wasn't available for this project's actual target.
- **Per-entry fault isolation.** One bad entry can no longer abort the
  entire scan (see the `/dev/fd` incident below) — a stat/opendir failure
  is confined to the entry or subtree it happened on.

The initial version paid a real cost for the portability choice: it did
absolute-path `lstat`/`opendir` per entry, re-resolving the full path from
the root each time, whereas `nftw`/`fts` implementations typically use
relative `openat`/`fstatat` lookups against an open directory fd to avoid
that. Measured impact at the time: roughly 15% lower throughput than the
original `nftw` version.

That was later revisited: `walk` now takes `fstatat`/`openat`/`fdopendir`
(POSIX.1-2008/SUSv4 — a newer standard revision, but still fully POSIX, not
a `fts`-style non-standard dependency), resolving one path component per
call against the parent directory's already-open fd instead of the whole
path from the root. Each recursive call receives the parent's directory fd
and just the bare entry name; `full_path` is still threaded through
separately, but purely for bookkeeping (table key, `--exclude` matching)
— it no longer touches any syscall.

**The measured result was a null result**: 53.4s vs. 54.3s on the same
`/` benchmark (see below) — within the run-to-run noise already observed
across these scans (e.g. 45.0s → 54.3s between two supposedly-equivalent
earlier runs), not a real win. Best guess: the O(depth) path-resolution
cost this was meant to eliminate is largely absorbed by the OS's
directory-entry/inode cache on a local, warm-cached APFS volume — doubly so
since the same tree had already been scanned repeatedly earlier in the
session. The dominant per-entry costs (the `malloc`+`memcpy` of a
`struct stat`, the hash table insert, actual disk I/O for the tree's cold
parts) are identical between the two approaches either way. The change was
kept anyway, since it's still strictly POSIX-standard and might pay off on
a colder cache, a deeper tree, or a slower/network filesystem — just not
demonstrated here.

A second, related change followed the same pattern: `walk`/`walk_body`
were split so a single shared `context->path_buffer` gets one component
appended per recursion level and truncated back on return, instead of a
fresh `snprintf`-built string at every call (which re-copies the entire
parent path on every single entry — an O(depth) cost per entry that the
`fstatat`/`openat` change above never touched, since it only addressed
the *syscall* side of path resolution). Verified correct by instrumenting
a build to print every visited path across a deliberately adversarial
tree (a long-named subdirectory followed by a short-named sibling) and
confirming no stale characters survived a truncation. Measured result:
53.2s — again no real difference from the two prior versions. This change
was made and kept for a different reason than speed: it bounds stack
usage to one `PATH_MAX` buffer for the entire scan, rather than one
per recursion level (the previous per-call `char child_path[PATH_MAX]`
stack local meant stack usage grew linearly with tree depth). Three
traversal-engine iterations in a row have now shown that, on this
workload, wall-clock time is essentially insensitive to path-resolution
and path-bookkeeping strategy — the bottleneck lies elsewhere (disk I/O,
per-entry allocation, hash insert).

`scan` supports:
- `--cross-mounts` — cross filesystem/mount boundaries (default: stay on
  the starting filesystem, compared by `st_dev`).
- `--follow-symlinks` — follow symlinked directories instead of treating
  them as leaves (default: don't). Cycle-protected via an ancestor
  `(device, inode)` chain — the same mechanism `nftw` uses internally.
- `--exclude PATH` (repeatable) — skip a path and everything under it;
  descent is genuinely never attempted, not just filtered after the fact.

## The `/dev/fd` incident

An early `--cross-mounts` run against `/` (using `nftw` at the time)
aborted partway through with `nftw` returning `-1` — not a hang, a fast
internal failure. Instrumented logging showed the last paths visited were
`/dev/fd/0`, `/dev/fd/1`, `/dev/fd/2` before the abort. `/dev/fd` mirrors
the *scanning process's own* open file descriptors; walking it is
inherently unstable since the descriptor set changes as the walker opens
and closes files during its own traversal (the same class of problem as
walking `/proc` on Linux). This wasn't a hang risk from crossing mount
points as originally suspected (a direct probe of the `autofs` automount
at `/System/Volumes/Data/home` returned instantly, empty) — it was this
specific pseudo-filesystem. Excluding it resolved it cleanly; see the
hand-rolled walker rationale above for why fixing this properly required
leaving `nftw` behind rather than just special-casing it.

The exclusion was first done as `--exclude /dev` (the whole directory),
then narrowed to `--exclude /dev/fd --exclude /proc` — surgical rather
than blanket, so the rest of `/dev` (device special files, harmless to
`stat`) still gets scanned, and the same command is portable across
platforms: `/proc` doesn't exist on macOS, so excluding it there is a
no-op (`path_is_excluded` just never matches it — confirmed by
`excluded_roots=1`, not `2`, on the macOS run below), but it's the
Linux analogue of the exact same self-referential-fd problem. This is
the recommended default pair of exclusions for any `--cross-mounts` run
on either platform.

## Growth behavior

The table doubles capacity (always a power of two) whenever inserting the
next entry would push the load factor past a fixed 80% threshold
(`rh_maybe_grow` in `src/robinhood.c`; see "Configurable resize
threshold" below for the experiment confirming no other value in the
70-90% range is worth choosing instead, which is why this isn't
configurable). Growing 64 keys into a table started at capacity 4
produces this doubling sequence at that threshold, confirmed against
the actual implementation:

| Capacity | Reached at insertion # |
|---:|---:|
| 4 | (initial) |
| 8 | 4 |
| 16 | 7 |
| 32 | 13 |
| 64 | 26 |
| 128 | 52 |

The table settles at 64 entries in a 128-slot table (50% load factor) —
i.e. it always lands just past the most recent doubling, never closer to
80% than one insertion's worth of margin.

## Large-scale scans

| Target | Options | Entries | Final capacity | Elapsed | Throughput |
|---|---|---:|---:|---:|---:|
| `~` (home directory) | default (`nftw`) | 478,659 | 1,048,576 (2²⁰) | 11.929s | ~40,100 entries/s |
| `/` (sealed system volume) | default (`nftw`) | 2,044,423 | 4,194,304 (2²²) | 45.693s | ~44,700 entries/s |
| `/` (crossing mounts) | `--cross-mounts` (`nftw`) | 2,065,313 | 4,194,304 (2²²) | 45.019s | aborted (`/dev/fd`, see above) |
| `/` (crossing mounts, `/dev` excluded) | `--cross-mounts --exclude /dev` (hand-rolled, `opendir`/`lstat`) | 2,066,530 | 4,194,304 (2²²) | 54.319s | ~38,050 entries/s |
| `/` (crossing mounts, `/dev` excluded) | `--cross-mounts --exclude /dev` (hand-rolled, `openat`/`fstatat`) | 2,066,637 | 4,194,304 (2²²) | 53.434s | ~38,680 entries/s |
| `/` (crossing mounts, `/dev` excluded) | `--cross-mounts --exclude /dev` (hand-rolled, shared path buffer) | 2,066,990 | 4,194,304 (2²²) | 53.232s | ~38,830 entries/s |
| `/` (crossing mounts, narrower exclusion) | `--cross-mounts --exclude /dev/fd --exclude /proc` (shared path buffer) | 2,070,068 | 4,194,304 (2²²) | 53.617s | ~38,610 entries/s |

Rows 3-5 are the same benchmark across three traversal-engine iterations
(absolute-path `opendir`/`lstat` → `openat`/`fstatat` → shared path
buffer) — see "Traversal engine" above for why none of these differences
count as a real win; they're all within run-to-run noise. The last row
switches from excluding all of `/dev` to just `/dev/fd` (plus the inert,
Linux-only `/proc`), which is why its entry count is slightly higher —
it now also covers the rest of `/dev` (device special files).

Notes on the mount-crossing rows: crossing mount boundaries barely changed the
entry count on this machine (2,044,423 → 2,066,530, +~1%). That suggests
APFS firmlinks make `/System/Volumes/Data` (where `/Users`, `/Applications`,
etc. actually live) report the same `st_dev` as `/`, so the default
single-filesystem scan was already covering it; the small delta comes from
genuinely separate small system volumes (`/System/Volumes/VM`, `Preboot`,
`Update`, `xarts`, `iSCPreboot`, `Hardware`) plus `/Volumes/Docker`. The
final run also reported 497 unreadable paths (permission-denied etc.),
handled gracefully rather than aborting.

All runs land at ~49-50% final load factor, consistent with the small-scale
growth pattern above.

## Caveats

- Timing includes the full cost of the walk: directory traversal and
  `stat`/`lstat` syscalls, a `malloc` + `memcpy` of a `struct stat` (~144
  bytes) per entry, and the table insert. It is not an isolated
  microbenchmark of `rh_set` alone — disk/filesystem-cache behavior likely
  dominates over hash table overhead at this scale.
- Crossing mount points (or even just `stat`ing a mount trigger point) can
  still hang indefinitely if it touches an unreachable network automount —
  neither `FTW_MOUNT` nor the hand-rolled walker's `st_dev` check can
  prevent this, since the hang (if any) happens inside the syscall that
  would tell you which device you're on, before any userspace logic runs.
  The large-scale runs above were wrapped in a shell-level watchdog
  (background + timed `kill -9`) as a precaution; none of them actually
  hung.
- Single run each, single-threaded, on one machine — treat these numbers
  as orientation, not a controlled benchmark.

## Other examples

`scan` is the only example built for stress-testing from the start; the
other four (`memo`, `wordfreq`, `netifs`, `environ`) were built to
demonstrate different parts of the API, not to be pushed to scale. Running
them at scale anyway turned up a few things worth recording — and one
genuine null result.

### `memo`: the memoization payoff, made concrete

`memo`'s own table never holds more than ~91 entries (keys are `0`..`90`
as decimal strings), so there's no hash-table growth story here. The
interesting number is what memoization avoids. Timing `./memo 90` ten
times (`time.perf_counter()` around the whole process, median of 10):
**2.5ms**, almost entirely process startup — the actual computation is
immeasurably fast at this scale.

For contrast, a naive (non-memoized) recursive `fib()` was timed
separately (`user` time, to exclude process-startup noise; `real` time
was inconsistent run-to-run at these small scales):

| n | naive `fib(n)` user time |
|---:|---:|
| 35 | 0.02s |
| 40 | 0.30s |
| 42 | 0.80s |

The growth ratio between these points (1.63x–1.72x per step) matches the
theoretical expectation for unmemoized Fibonacci recursion (the golden
ratio, φ≈1.618, per step) closely enough to extrapolate with it:
naive `fib(90)` would take on the order of **270 years** of CPU time
(0.80s × φ⁴⁸). This is an extrapolation, not a measurement — nobody ran
naive `fib(90)` to completion — but the fit between the measured growth
rate and the theoretical one is close enough (within ~6%) to trust the
order of magnitude. `memo`'s entire reason to exist is collapsing that
into 2.5ms.

### `wordfreq`: real-corpus throughput, and a case-folding sanity check

Fed `wordfreq` two real corpora, not synthetic ones: this repository's
own `CONVERSATION.md` (a real conversation transcript, 74,030 words) and
macOS's `/usr/share/dict/words` (235,976 words, one per line).

| Input | Words | Distinct | Median time (warm cache) | Throughput |
|---|---:|---:|---:|---:|
| `CONVERSATION.md` | 74,030 | 6,512 | 11.9ms | ~6.2M words/s |
| `CONVERSATION.md` × 30 (concatenated) | 2,220,900 | 6,512 | 229.1ms | ~9.7M words/s |
| `/usr/share/dict/words` | 235,976 | 234,456 | 101.9ms | ~2.3M words/s |

The dictionary case is markedly slower per word than either
`CONVERSATION.md` case, despite being a similar total size to the small
corpus — because it's nearly the opposite workload. `CONVERSATION.md`
mostly exercises cheap repeated `rh_get`-then-`rh_set` updates against a
small, fast-resident vocabulary of 6,512 words; the dictionary is nearly
all first-time `rh_set` inserts against a table that ends up growing to
524,288 slots (44.7% final load factor, doubling from the initial 1,024
per the usual `rh_maybe_grow` sequence) — i.e. it pays for ~9 doubling
rehashes that the natural-language runs never trigger.

A genuine (unplanned) correctness validation, not just a performance
number: `/usr/share/dict/words` turns out to contain both capitalized and
lowercase forms of some entries as separate lines (`A`/`a`,
`Abelite`/`abelite`). `wordfreq`'s case-folding correctly merged these
into single entries with count 2 — confirming the "`Hello,` and `hello`
count as the same word" design intent (see `examples/wordfreq.c`) against
real data it wasn't specifically constructed to test, not just the
hand-written example in the code comment.

Note on the first row: the very first read of `CONVERSATION.md` in a cold
cache measured 53ms wall time versus 11.9ms warm — disk I/O, not hash
table cost, as with `scan`'s caveats above. The table above uses the warm
number for a fair per-word comparison.

### `environ`: synthetic scaling and the ARG_MAX ceiling

`environ`'s natural size on this machine is 47 variables — too small to
say anything about the table. Synthetic variables were injected via
`subprocess.run(..., env=...)` to push past that:

| Synthetic vars added | Total vars | Wall time |
|---:|---:|---:|
| 0 | 47 | 3.8ms |
| 1,000 | 1,047 | 3.8ms |
| 5,000 | 5,047 | 8.2ms |
| 10,000 | 10,047 | 15.8ms |
| 20,000 | 20,047 | 27.2ms |

Roughly linear past the first couple thousand (process-startup overhead
dominates below that), at very roughly ~850,000 variables/s once it does.
Pushing further (50,000 synthetic variables) failed outright with
`OSError: [Errno 7] Argument list too long` — `execve`'s environment is
capped by the OS (`getconf ARG_MAX` reports 1,048,576 bytes on this
machine), and that ceiling arrives long before `environ`'s own table
(borrowed-pointer values, no allocation at all — see `examples/environ.c`)
would show any strain of its own. The practical scaling limit for this
example is entirely outside the hash table.

### `netifs`: a null result

`netifs`'s table holds one entry per (interface, address family) pair —
15 on this machine. Timed runs after the first (which included one-time
process/dynamic-linker warmup noise, consistent with the `memo`/`scan`
pattern noted elsewhere in this document) landed at a consistent 0-1ms.
That entire cost is the `getifaddrs()` syscall itself; a 15-entry table
gives the hash table nothing to do that would register on a wall-clock
timer. Unlike `environ`, there's no practical way to synthetically
inflate a machine's real network interface count to test further, so
this is recorded as a genuine null result rather than left untested:
`netifs` is not, and structurally cannot be, a hash-table stress test.

## Is Robin Hood living up to expectations?

`rh_probe_stats()` (see `include/robinhood.h`) scans a table's current
entries and reports how far Robin Hood probing has actually displaced
them from their ideal slots: count, mean, max, standard deviation, and
a histogram bucketed by distance. `scan --probe-stats` wires this in
after the walk completes -- disabled by default (see "Overhead" below
for why that's a UX choice, not a performance one).

Robin Hood's whole premise is keeping probe chains short and tightly
clustered near zero, unlike naive linear probing where one unlucky key
can end up arbitrarily far from home. Run against the two large-scale
scans already documented above:

| Target | Entries | Capacity | Load factor | Mean | Stddev | Max |
|---|---:|---:|---:|---:|---:|---:|
| `~` (home directory) | 486,019 | 1,048,576 | 46.3% | 0.43 | 0.74 | 8 |
| `/` (crossing mounts, `/dev/fd`+`/proc` excluded) | 2,105,273 | 4,194,304 | 50.2% | 0.51 | 0.82 | 11 |

And the full histogram for the larger (`/`) run:

| Distance | Count | % |
|---:|---:|---:|
| 0 | 1,361,880 | 64.7% |
| 1 | 516,518 | 24.5% |
| 2 | 160,492 | 7.6% |
| 3 | 47,106 | 2.2% |
| 4 | 13,703 | 0.7% |
| 5 | 4,021 | 0.2% |
| 6 | 1,117 | 0.1% |
| 7+ | 436 | 0.0% |

The shape is textbook: a sharp exponential-looking decay, with nearly
two-thirds of all keys sitting in their ideal slot (distance 0) and the
tail thinning out fast. The headline number is `max`: across 2.1
million entries at just past 50% load factor, not a single key ever
needed more than 11 probes to find — the specific guarantee Robin Hood
hashing exists to make (bounded worst case, not just a good average),
holding up in practice, not just in theory, at real scale on real
filesystem-path keys.

### Overhead

Timed the `/` scan above with and without `--probe-stats`: 53.341s vs.
53.617s for the walk itself — within the same run-to-run noise already
established for this benchmark (see "Traversal engine" above), not a
real difference. The stats computation itself (a separately-timed
O(capacity) pass, reported by `scan --probe-stats` itself) took
0.012130s on the 4,194,304-slot table -- about 0.02% of the walk's
total time. The smaller `~` run showed the same pattern: 0.002973s of
stats computation against a 14.864s walk.

This confirms the instrumentation can't meaningfully slow anything
down -- it reuses the `distance` field Robin Hood already tracks on
every entry for its own correctness, so `rh_probe_stats()` adds no
per-operation cost to `rh_get`/`rh_set`/etc. at all; the only cost is
the one-time O(capacity) scan when explicitly requested. `--probe-stats`
still defaults to off in `scan`, not because it's slow, but because
it's optional diagnostic output most runs don't want cluttering the
one-line summary -- an opt-in flag for output cleanliness, not a
performance workaround.

## Configurable resize threshold: does anything beat the 80% default?

The library briefly made the 80% growth trigger above configurable
per-table (`rh_resize_threshold()`/`rh_set_resize_threshold()`, with a
`--resize-threshold PERCENT` flag on every example) before this
experiment showed the answer below, at which point it was removed
again in favor of a fixed 80%: does any value between 70% and 90%, in
5% steps, meaningfully outperform the 80% default?

**First attempt, and why it didn't work:** the obvious experiment is
`scan --resize-threshold N --probe-stats ~` for each `N`. Run against
this machine's home directory (~487K entries), all five values from
70% to 90% land on the exact same final capacity (1,048,576) and the
exact same probe-depth histogram -- the resize threshold made zero
observable difference. This isn't a bug; it falls straight out of the
doubling growth policy above. Capacity only ever takes discrete
power-of-two values, so for any fixed key count, "which threshold do I
use" only changes anything for the specific range of percentages that
straddle the exact count where one more doubling becomes necessary.
Here, 487K keys past a 524,288-slot table already exceeds even a 90%
threshold (524,288 x 0.90 = 471,859 < 487K), so every configuration in
70-90% ends up growing to 1,048,576 regardless -- the threshold choice
was never in play for this particular table size. The same collapse
showed up in an earlier synthetic sweep at 3,000,000 keys: 75%, 80%,
85%, and 90% were bucket-for-bucket identical (capacity 4,194,304),
and only 70% (which crossed the one relevant doubling boundary)
differed. **Takeaway for anyone trying this themselves: a real-workload
sweep like this can easily "prove" the threshold doesn't matter simply
because your table size didn't happen to straddle a boundary in the
range you tested** -- it's a coincidence of table size, not evidence
about the underlying trade-off.

**Second attempt: hold capacity fixed, vary only the real load
factor.** To see the actual trade-off, a table needs to reach each
target load factor without an intervening doubling changing the
denominator out from under the comparison. This used a scratch build
of the then-configurable threshold, set to 100% to disable further
growth, with a table fixed at capacity 2^20 (1,048,576) and filled to
exactly 70/75/80/85/90% of that capacity from empty, timing insertion
and both hit and miss lookups over the resulting table (5 runs per
level, averaged; synthetic `"key-%08ld-%04ld"` keys, not filesystem
paths):

| Load factor | Insert (ns/op) | Get hit (ns/op) | Get miss (ns/op) | Probe mean | Probe max | Probe stddev |
|---:|---:|---:|---:|---:|---:|---:|
| 70% | 100.8 | 70.6 | 220.4 | 1.17 | 20 | 1.52 |
| 75% | 102.2 | 76.5 | 222.1 | 1.51 | 22 | 1.86 |
| 80% (default) | 106.6 | 81.5 | 212.1 | 2.01 | 28 | 2.36 |
| 85% | 119.7 | 90.6 | 208.7 | 2.85 | 37 | 3.21 |
| 90% | 117.9 | 98.5 | 221.3 | 4.53 | 47 | 4.86 |

This is the real signal the `scan`-based sweep above couldn't see: probe
depth roughly doubles with each 10-point rise in load factor (as Robin
Hood theory predicts), and `get_hit` cost scales with it -- about 40%
slower at 90% than at 70%. `insert` shows the same trend, more mildly.
`get_miss` cost stays essentially flat across all five levels: a
negative lookup exits as soon as it meets a resident whose own distance
is shorter than the probe distance so far, and that early-exit tends to
fire quickly regardless of how long the *positive* chains get, so
miss cost is dominated by hashing/string-comparison overhead rather
than probe count here.

**Conclusion: no, nothing in 70-90% beats 80% -- the curve is
monotonic, not U-shaped.** Every step down in load factor is faster,
every step up is slower; there's no hidden sweet spot partway through
this range that's simultaneously faster *and* denser than the default.
80% is the standard compromise for exactly this reason (matching, e.g.,
Java's `HashMap` default of 0.75): it buys back a meaningful amount of
memory efficiency over 70% (more entries per allocated slot) in
exchange for probe chains that are still short in absolute terms (mean
2.0, max 28 at over 800K entries) and a lookup cost still measured in
tens of nanoseconds. Given the real-world `scan` numbers above, where
the entire walk is dominated by filesystem I/O (user CPU time ~0.46s
against a ~15s wall-clock walk), this whole trade-off is very unlikely
to be visible in practice for this library's own example workloads.
This is exactly why the configurable threshold was removed again after
this experiment rather than kept: no fixed value anyone would actually
pick beats 80%, so the extra API (a setter, a getter, a CLI flag on
every example) was pure surface area for a knob nothing in this
library's own testing ever wanted to turn.
