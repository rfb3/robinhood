# robinhood

A small C library implementing a hash table with Robin Hood
open-addressing insertion and backward-shift deletion.

- String keys, `void*` values.
- Backward-shift deletion, so there are no tombstones and no lookup
  slowdown from accumulated deletions.
- Grows (doubling) automatically once load factor would exceed 80%.
- Written to POSIX.1-2001 base where possible; the example tools use
  newer POSIX.1-2008 calls (`fstatat`/`openat`) where that materially
  helps, documented in `PERFORMANCE.md`. The library itself also relies
  on a BSD extension (`<err.h>`), and one example (`netifs`) on another
  (`getifaddrs`) -- both checked by `./configure`.

## Building

```sh
./configure     # detect a C compiler, check for required features, and
                # set an install prefix (default: /usr/local)
make            # build librobinhood (static + shared) and the examples
make test       # build and run the unit test suite
make sanitize   # rebuild and run under AddressSanitizer + UBSan
make coverage   # rebuild and run under gcov, report line/branch coverage
make install    # install the library, header, and pkg-config file to
                # $PREFIX (or $DESTDIR$PREFIX, for staged installs)
make dist       # produce a robinhood-<version>.tar.gz source tarball
make clean      # remove build artifacts
```

Once installed, downstream projects can use `pkg-config --cflags --libs
robinhood` instead of hardcoding `-lrobinhood`. Static linking still
needs `-lm` listed explicitly alongside it (see `robinhood.pc`'s
`Libs:` line) -- dynamic linking picks up the library's own transitive
`libm` dependency automatically, static linking doesn't.

Without installing, the built programs (`scan`, `memo`, etc.) and the
library (`librobinhood.a`/`.so`/`.dylib`) land directly in the repo
root -- run them as `./scan`, etc. Build from the repo root only;
there's no per-directory subdirectory build (`cd tests && make`
doesn't work).

The build is autoconf/automake/libtool: `configure`/`Makefile.in` are
generated from `configure.ac`/`Makefile.am` and checked in, so a plain
checkout builds with nothing beyond a C compiler and `make` -- no
autotools required unless you're editing `configure.ac`/`Makefile.am`
themselves. If you do, regenerate with `autoreconf --install`; on
macOS with Homebrew, prefix
`PATH="/opt/homebrew/opt/libtool/libexec/gnubin:$PATH"` onto that
command specifically -- Homebrew's GNU `libtool`/`libtoolize` aren't
on `PATH` by default, to avoid shadowing macOS's own incompatible BSD
`/usr/bin/libtool`. If `librobinhood`'s public ABI ever changes, bump
`-version-info` in `Makefile.am`'s `librobinhood_la_LDFLAGS` per
libtool's own versioning rules -- that's separate from, and doesn't
move in lockstep with, `configure.ac`'s package version. Run
`./configure --help` for options.

`make test`'s full per-category output (`basic_operations: PASS`,
etc.) goes to `tester.log`, not stdout -- `cat tester.log` for the
detail (`VERBOSE=1` only dumps it automatically on a *failing* run).
There's no single-test-selection target -- comment out calls inside
individual `test_*` functions in `tests/tester.c` to narrow things
down while iterating, or add a small new `.c` driver to
`noinst_PROGRAMS` in `Makefile.am` and link it against
`librobinhood.la` the same way the examples do. `make sanitize`
catches heap overflows, use-after-free, and UB that `make test`'s
assertion-style checks alone can miss -- run it after touching
anything with manual memory management. **On macOS this doesn't catch
leaks**: Apple's ASan doesn't support LeakSanitizer, unlike Linux,
where it's automatic -- run `make sanitize` on a Linux machine or
container periodically too, if you can, for real leak coverage.

`make distclean` returns the tree to freshly-unpacked-tarball state --
it removes `Makefile`/`config.log`/`config.status`/`.deps/`/`.libs/`,
but leaves `configure`/`Makefile.in`/`build-aux/`/`m4/*.m4` in place,
since those are committed source here, not build output (see
"Building" above).

## Project layout

```
include/robinhood.h    public header
src/robinhood.c         library implementation
examples/scan.c         example: directory tree walker (see below)
examples/memo.c         example: memoized Fibonacci / cache demo (see below)
examples/wordfreq.c     example: word-frequency counter (see below)
examples/netifs.c       example: network interface snapshot (see below)
examples/environ.c      example: environment-variable walk (see below)
tests/tester.c          unit test suite
configure.ac             autoconf input
Makefile.am              automake input (single, non-recursive --
                         covers the library, examples, and tests)
robinhood.pc.in          template for the installed pkg-config file
LICENSE                  Unlicense (public domain)
```

`configure`, `Makefile.in`, `build-aux/`, and `m4/` are generated from
`configure.ac`/`Makefile.am` by `autoreconf` but checked in alongside
them (see "Building" above for why). Build outputs (the library and
the example/test binaries) land directly at the repo root, next to
`Makefile.am` -- there's no separate `bin/`/`lib/` output directory.

## Using the library

```c
#include "robinhood.h"

RHTable table = rh_create (16);   // initial capacity, rounded up to a power of two

rh_set (table, "hello", (void*)"world");

if (rh_has (table, "hello"))
{
    const char* value = (const char*)(rh_get (table, "hello", NULL));
    printf ("%s\n", value);   // "world"
}

rh_clear (table, "hello");        // remove one key
rh_destroy (&table);              // frees the table and its key copies
                                   // (caller-supplied values are not freed)
```

Iteration:

```c
for (RHIterator it = rhi_create (table); !rhi_is_finished (it); rhi_advance (it))
{
    const char* key = rhi_key (it);
    // ...
}
```

The full API is documented in `include/robinhood.h`, in
[Doxygen](https://www.doxygen.nl/)-style comments -- see "Documentation"
below to generate browsable HTML from them.

### Warnings

By default, the library prints a message to stderr if an internal
allocation fails (inside `rh_create`/`rh_set`/`rh_maybe_grow`/
`rhi_create`) and returns a failure indication (`NULL`/no-op, per the
function) rather than aborting. Install your own handler to log
these differently, or pass `NULL` to suppress them entirely:

```c
void my_handler (const char* message)
{
    fprintf (log_file, "robinhood: %s\n", message);
}

rh_set_warning_handler (my_handler);   // or rh_set_warning_handler (NULL) to suppress
...
rh_set_warning_handler (rh_default_warning_handler);   // restore default behavior
```

This is a single, process-wide setting rather than a per-table one --
`rh_create()` can itself fail to allocate the table, before there's
any `RHTable` to attach a per-instance setting to.

### Resize threshold

The table doubles capacity whenever the next insertion would push its
load factor past a threshold, 80% by default. Change it per-table with
`rh_set_resize_threshold()`; `rh_resize_threshold()` reads it back:

```c
rh_set_resize_threshold (table, 70);   // grow sooner, pack tighter probing
rh_set_resize_threshold (table, 90);   // grow later, denser table

unsigned current = rh_resize_threshold (table);   // 1-100
```

Valid values are 1-100; anything else is rejected (the function
returns `false` and leaves the threshold unchanged). Takes effect
lazily -- it only affects the table's next insertion, not immediately.
See `PERFORMANCE.md`'s "Configurable resize threshold" section before
reaching for this: within the 70-90% range, lower is consistently a
little faster and higher is consistently a little denser, but there's
no hidden value in between that beats the 80% default on both counts.

### Thread safety

Not thread-safe: concurrent operations on the *same* table or iterator
need a lock (or other synchronization) you provide yourself. Different
tables can be used concurrently from different threads with no extra
care, since they share no state.

The one exception is `rh_set_warning_handler()`, which is process-wide
global state rather than per-table -- see its own doc comment in
`include/robinhood.h` for the specific (narrower) guarantee it makes
under concurrent use.

## Documentation

`include/robinhood.h` carries `@brief`/`@param`/`@return` comments on
every function, in [Doxygen](https://www.doxygen.nl/)'s Javadoc-style
format. `make docs` (requires `doxygen` -- `brew install doxygen` /
`apt install doxygen`) generates browsable HTML at
`docs/html/index.html`, with this README as the front page. Optional:
nothing else here depends on it, and it's not part of `make`/`make
test`.



The same generated docs are published at
[rfb3.github.io/robinhood][gh-pages] -- rebuilt and redeployed
automatically on every push to `main` (see
`.github/workflows/docs.yml`), so there's no generated HTML checked
into this repo itself.

[gh-pages]: https://rfb3.github.io/robinhood/robinhood_8h.html#func-members

## Code coverage

`make coverage` rebuilds `src/robinhood.c` and `tests/tester.c` with
`gcov` instrumentation (bypassing the built library, same reasoning as
`make sanitize`), runs `tester`, and reports per-function and overall
line/branch coverage, leaving a `robinhood.c.gcov` line-by-line
annotation behind. `make coverage-html` (requires `lcov`/`genhtml` --
`brew install lcov` / `apt install lcov`) additionally generates a
browsable HTML report at `coverage-html/index.html`. Both are optional
-- not part of `make`/`make test` -- and `make clean` removes their
output.

## Example programs

Six small programs exercise the library in different ways -- `scan`,
`tester`, and `memo` are deliberately built on different underpinnings
(hand-rolled `fstatat`/`openat` recursion vs. `nftw` vs. no filesystem
traversal at all) rather than sharing one code path, so each keeps
demonstrating the library under genuinely different real-world usage
patterns instead of one superseding the others:

- **`tester`** (`./tester`) -- the unit test suite (`make test`).
- **`scan <directory>`** (`./scan`) -- walks a directory tree (hand-rolled
  `fstatat`/`openat`-based recursion, not `nftw`/`fts`) and maps each
  pathname to a copy of its `stat(2)` info. Options: `--cross-mounts`,
  `--follow-symlinks`, `--exclude PATH` (repeatable), `--probe-stats`
  (prints Robin Hood probe-depth statistics -- mean/max/stddev and a
  histogram -- after the walk; off by default, though measured
  overhead is negligible, see `PERFORMANCE.md`), `--resize-threshold
  PERCENT` (sets the table's resize threshold via
  `rh_set_resize_threshold()` -- 1-100, default 80; see
  `PERFORMANCE.md` for whether changing it is actually worth it). See
  `PERFORMANCE.md` for real-world timings, including scans of an
  entire home directory and root filesystem.
- **`memo <n>`** (`./memo`) -- computes `fib(n)` via recursion memoized in the
  table, demonstrating it as a cache: `rh_has`/`rh_get` for lookups,
  `rh_set` to populate a miss, and `rh_clear` to invalidate one entry
  and show the resulting partial recompute.
- **`wordfreq [top_n]`** (`./wordfreq`) -- reads words from stdin, case-
  folds and trims punctuation, and counts occurrences via a genuine
  read-modify-write (`rh_get` the current count, increment, `rh_set` it
  back) -- the one usage pattern none of the other examples exercise.
  Prints the `top_n` most frequent words (default: 10).
- **`netifs`** (`./netifs`) -- enumerates this machine's network
  interfaces via `getifaddrs`/`freeifaddrs`, mapping each (interface,
  address family) pair to its address and flags. IPv4/IPv6 only --
  link-layer/MAC-address entries are skipped, since their `sockaddr`
  representation isn't portable across platforms. A single interface
  can carry more than one address of the same family (e.g. multiple
  IPv6 addresses), so same-key collisions are disambiguated rather
  than silently overwritten.
- **`environ`** (`./environ`) -- walks the process's environment into
  the table and prints it sorted by name. The simplest example: no
  syscalls beyond what the process already holds in memory, and no
  heap allocation for values at all -- each value is a borrowed
  pointer into the existing environment strings, not a copy.

`memo`, `wordfreq`, `netifs`, and `environ` also accept the same
`--resize-threshold PERCENT` option as `scan`.

## Project status

The directory layout, the autoconf/automake/libtool build, `make
install`/`make uninstall`, and the license are all settled now. One
thing worth
knowing: `src/robinhood.c` uses `<err.h>`'s `err()`/`warn()`/`warnx()`
(a BSD extension present on macOS, the BSDs, glibc, and musl, but not
part of POSIX), and the example programs parse their command-line
options with `getopt_long()` (a GNU extension, also not part of
POSIX, but likewise present on macOS, the BSDs, glibc, and musl) --
`./configure` checks for both, along with the POSIX features the
example programs need, and fails with a clear message if your
toolchain is missing any of them.

## Further reading

- `STYLE.md` -- code style conventions for contributors.
- `PERFORMANCE.md` -- empirical growth/timing findings from the `scan` tool.

## License

[The Unlicense](https://unlicense.org) -- public domain. See
[`LICENSE`](LICENSE) for the full text. Every `.c`/`.h` file also
carries an `SPDX-License-Identifier: Unlicense` line.
