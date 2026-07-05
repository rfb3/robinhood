# Style guide

Coding conventions for this repository. These apply to all `.c`/`.h`
files (and, where noted, `Makefile.am`) — follow them for new code and
when touching existing code nearby. The overall aim is idiomatic
Allman-style C: brace-on-its-own-line is the one deliberately named
style choice, and the rest of this guide tries to stay close to what
most Allman-style C codebases already do (and what `clang-format` can
already enforce), rather than introducing bespoke conventions that
fight standard tooling.

## Table of contents

- [Formatting](#formatting)
- [Function declarations and definitions](#function-declarations-and-definitions)
- [Naming](#naming)
- [Increment and decrement](#increment-and-decrement)
- [Includes](#includes)
- [File structure](#file-structure)
- [Documentation comments](#documentation-comments)

## Formatting

- Keep lines, including comments, to 78 characters or fewer. This applies
  repo-wide, including Makefiles (wrap long recipe lines with a
  trailing `\`).
- Always spaces, never tabs — except Makefile recipe lines, which must
  start with a literal tab (required Make syntax, not a style choice).
- Braces: Allman style throughout — an opening `{` always goes on its
  own line, never "hanging" at the end of the previous line, for blocks
  and function bodies alike; and every `if`/`for`/`while`/etc. gets
  braces, even for a single-statement body — no brace-less singleton
  blocks.
- Prefer `while (true)` over `for (;;)` for an intentionally infinite
  loop — `true` says what's happening, where `for (;;)`'s three empty
  clauses only say what *isn't*.
- No space before `(` for function/macro calls, declarations, or
  definitions — `rh_get(table, key, NULL)`, `bar()`, `RHE_KEY(entry)`,
  `#define RHE_KEY(ENTRY) ...`. This is the one place the standard
  Allman convention and the mandatory C-preprocessor rule for
  function-like macros (`#define NAME(args)` can never have a space,
  or it becomes an object-like macro) already agree, so there's no
  exception to carve out here anymore.
- Space before `(` for the control-flow keywords `if`, `for`, `while`,
  `switch`, and `return` (when followed by a parenthesized
  expression) — `if (x)`, `while (true)`, `return (a) ? b : c;`.
- Space before `[` for array indexing — `entries [pos]`, not
  `entries[pos]` — this is `clang-format`'s `SpaceBeforeSquareBrackets`
  option, kept because it reads well and doesn't fight the tool.
- When ordering lists of things, if there is no need for any particular
  ordering, put things in alphabetical/lexicographic order. Everything from
  dependencies in a makefile to ordering of function definitions in source
  files. One recognized need for a different order: grouping a `static`
  helper immediately before the function(s) that call it, when it's only
  ever used by one nearby group (`src/robinhood.c`: `rhi_advance_to_used`
  right before `rhi_advance`/`rhi_create`/`rhi_reset`, its only callers).
- `*` in a pointer type binds to the type, not the variable name: `char*
  key`, never `char *key`. Applies everywhere a pointer type appears —
  declarations, parameters, casts, struct fields alike.
- Declare one variable per line. Never comma-separated multiple
  declarators (`int a, b;`), even when they share a type — this sidesteps
  the classic C gotcha where a `*`/`[]` on one declarator in such a list
  doesn't apply to the others (`char* a, b;` makes `b` a plain `char`,
  not a pointer).
- `//` exclusively, for every comment in this codebase — on its own line
  and trailing actual code on the same line alike, e.g. `return 0; //
  overflow` (`src/robinhood.c`: `next_power_of_two`). No `/* ... */`
  comments anywhere, including short same-line annotations — a trailing
  `//` is always the last thing on its line by construction, so it works
  equally well there. `///`, used only in `include/robinhood.h` for
  Doxygen-style doc comments (see "Documentation comments" below), counts
  as part of this same `//` family, not an exception to it.

## Function declarations and definitions

Linkage (`static`/`extern`, when present) and the return type share one
line; the function name always starts the next line, along with its
parameter list — this is `clang-format`'s `BreakAfterReturnType: All`,
not a hand-maintained convention, so `make format` already enforces it:

```c
extern void*
rh_get(RHTable table, const char* key, void* not_found_result);

static size_t
next_power_of_two(size_t n);
```

This applies to function declarations/definitions only, not variables —
a `static`/`extern` variable keeps its linkage keyword on the same line
as its type: `static int errors = 0;`, `extern char** environ;`.

The function name and its first parameter always share one line. When
the whole signature fits within the 78-column limit, every remaining
parameter joins them there too — this covers most functions here,
including two-and-three-parameter ones like `rh_clear(RHTable table,
const char* key)`. Once a signature doesn't fit, the first parameter
still stays put, but every parameter after it moves to its own line —
in both prototypes and definitions, not just prototypes — with types
padded so every parameter name lines up in the same column (pad each
type to the width of the widest type in the list, plus one space):

```c
static bool
rh_find_index(RHTable     table,
              const char* key,
              uint64_t    hash,
              size_t*     out_index);
```

This applies to declarations/definitions only, not call sites —
`rh_set(table, "me", value);` stays on one line regardless of argument
count.

The same column-alignment applies beyond function parameters, to any
block of consecutive, related declarations — struct fields and grouped
local variables alike:

```c
struct RHTable_struct
{
    RHEntry* entries;
    size_t   capacity;
    size_t   count;
};
```

## Naming

- Prefer `index` over `i` as a for-loop variable name, unless a more
  specific name fits better — e.g. `shift`, `distance`, or a pointer/node
  cursor like `parent`/`entry` that isn't really an index at all.
- Spell out `int` explicitly with every modifier keyword (`unsigned`,
  `long`, `short`, `signed`), even though C treats it as implied —
  `unsigned int`, `long int`, `unsigned long long int`, never bare
  `unsigned`/`long` on their own. Doesn't apply to `unsigned char`/
  `signed char`/`long double`: those aren't a modifier-plus-implied-`int`
  at all, `char`/`double` are already complete, distinct base types.
- `UPPER_SNAKE_CASE` for macros (`RH_CAPACITY`, `CHECK`,
  `TEST_COUNT`) and `enum` constants (`EMPTY`/`USED`,
  `OPT_PROBE_STATS`) — everything else (functions, variables,
  struct/typedef names) is `snake_case` or, for the library's own public
  `RH`-prefixed opaque types, a run-together `RHWord` form (`RHTable`,
  `RHProbeStats`). The same `RHWord` form also covers internal types that
  are purely part of the same family even though never exposed in
  `robinhood.h` (`src/robinhood.c`'s `RHEntry`, a plain struct describing
  one table slot) — the test is "part of the `RH` type family", not
  "public", so a genuinely unrelated internal type still gets
  `snake_case` (`src/robinhood.c`'s `entry_state`/`entry_state_enum`).

## Increment and decrement

Default to prefix (`++index`); only use postfix (`index++`) when the
postfix semantics — using the old value before it changes — are actually
needed. For example:

```c
*(dest++) = '/';                        // examples/scan.c: walk()
hash ^= ((unsigned char)(*(text++)));   // src/robinhood.c: string_hash()
```

In both cases, `++dest`/`++text` would advance the pointer *before* the
write, changing which byte gets touched — so postfix is required there,
not just preferred.

## Includes

Group `#include` directives into blank-line-separated blocks, in this
order, each sorted alphabetically within itself:

1. This project's own headers, in quotes (`"robinhood.h"`) — kept
   separate and first so a missing or wrong local include shows up
   immediately, rather than being hidden behind whatever transitively
   pulls it in from a system header.
2. "Regular" system/library headers with no directory prefix, in angle
   brackets (`<stdio.h>`, `<stdlib.h>`, etc.).
3. Everything else in angle brackets — headers with a directory prefix
   like `<sys/...>` or `<netinet/...>` — grouped one prefix per block,
   not lumped into a single trailing block together: each distinct
   prefix gets its own block, separated from the others (and from the
   plain block above) by a blank line. The blocks themselves are
   sorted too, not just the headers within each one — alphabetically
   by prefix, `<net/...>` before `<sys/...>` — the same
   alphabetical-ordering default this guide applies to everything else
   (see "Formatting" above).

```c
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
```

(`examples/scan.c`, which only ever needs one prefix.) A file with no
prefixed headers at all just has the first two blocks; a file with
only one local header and no others still gets its own block for it.
`examples/netifs.c` needs four prefixed headers across four different
prefixes, so it gets four one-line blocks after the plain block, one
per prefix, the blocks themselves alphabetical too (`arpa` < `net` <
`netinet` < `sys`):

```c
#include <arpa/inet.h>

#include <net/if.h>

#include <netinet/in.h>

#include <sys/socket.h>
```

Only include a header if the file actually uses something it
declares — don't include one "just in case," and drop one the moment
nothing in the file needs it anymore (e.g. a leftover `<assert.h>`
after the last `assert()` call is removed).

Relying on one *standard* header to transitively guarantee another
*standard* header's symbols is fine, since that relationship is a
documented, permanent part of the C/POSIX standard, not an
implementation detail: `<sys/stat.h>` already guarantees `dev_t`/
`ino_t` per POSIX, so a file that only needs those two types and
already includes `<sys/stat.h>` doesn't also need `<sys/types.h>`.
But never rely on one of *this project's own* headers (`"robinhood.h"`)
to transitively supply a standard-library symbol — that's just
`robinhood.h`'s own implementation detail today, not a contract, and
could change out from under every file quietly depending on it. If a
`.c` file directly uses `bool`/`true`/`false`, it includes
`<stdbool.h>` itself, even though `"robinhood.h"` already does —
don't lean on the local header's own includes for anything beyond
this project's own declared types (`RHTable`, `RHIterator`, etc.).

## File structure

Every `.c`/`.h` file starts with a one-line file tagline comment,
immediately followed by an `SPDX-License-Identifier: Unlicense` line
(same comment block), then the rest of the file.

(This project used to delineate every section — headers, type
definitions, and every single function — with a `//`-bordered banner,
plus a form-feed character between them and a hand-maintained Table of
Contents comment listing them all. All of that is dropped now: none
of it is something a normal toolchain understands or preserves, it
didn't survive contact with `clang-format`, and without the page
mechanism it existed to serve, a banner that just echoes the function
name or says "Type definitions" right above a `struct` is a pure
"what" comment doing no real work -- the code already says that.)

A banner is still warranted for a genuinely multi-item grouping whose
existence isn't obvious from the code alone -- a handful of related
functions under one topic (`include/robinhood.h`'s "Table operations",
"Iterator operations", "Global configuration"), or a whole block of
forward declarations ("File-local prototypes", in any `.c` file with
one). There is exactly one banner style, used every time one of these
is warranted, never the plain three-line `//`/text/`//` form:

```c
// ===========================================================================
// Section name
// ===========================================================================
```

Reserve it for real, multi-item structure like the examples above --
not for a single function, a single type, or anything else the code
immediately beneath it already says on its own (that's what got
removed: `Headers, etc.`, `Type definitions`, and a compressed-name
banner on every function). The file tagline/SPDX comment at the very
top of the file is the one exception, and stays in the plain form --
it's file identification, not a code section.

Every `.c` file with more than one function gets a "File-local
prototypes" section, right after any type definitions and before the
first function definition, forward-declaring every function defined
later in the file — written out in full (return type, `static` where
applicable, real parameter names, wrapped one per line per "Function
declarations and definitions" above once the signature doesn't fit on
one line), alphabetically by name, including `main` even though
it's never `static` and normally wouldn't need forward declaring.
Macros (e.g. `CHECK`) and struct-only definitions don't get prototype
entries -- only actual functions do.

A type definition holding only a plain data type (no reference to any
function) always goes before "File-local prototypes", per the above.
But `tests/tester.c`'s `struct test` (the `{ name, function }` pair
type) and its `tests[]` table are deliberately split across two
different spots in the file precisely because of this ordering rule:
`struct test` itself has no function reference, so it sits early,
alongside the file's other type definitions. The `tests[]` table,
though, is initialized with pointers to every `test_*` function by
name -- it can't move any earlier than "File-local prototypes" without
losing the forward declarations that make those names valid there, so
it stays lower in the file, wherever in the function sequence made
sense, not up front with the type itself.

## Documentation comments

Every function and typedef declared in `include/robinhood.h` gets a
Doxygen-style comment immediately above it, using `///` (not `/** */`,
to stay consistent with the rest of the file using `//` exclusively)
and `@brief`/`@param`/`@return` tags:

```c
///
/// One-line (or short-paragraph) description of what the function
/// does, including any behavior worth calling out (ownership,
/// no-op conditions, NULL handling, etc.).
///
/// @param name  What this parameter means.
/// @return      What gets returned, and under what conditions.
///
extern int
example_function(int name);
```

Wrap comment text to the same 78-column limit as code. `@param`
descriptions align like the function's own parameter list would (see
"Function declarations and definitions" above) when there's more than
one. Implementation (`.c`) files don't repeat these comments —
only the public header does.
