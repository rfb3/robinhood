# Style guide

Coding conventions for this repository. These apply to all `.c`/`.h`
files (and, where noted, `Makefile.am`) — follow them for new code and
when touching existing code nearby.

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
- Every `if`/`for`/`while`/etc. gets curly braces, even for a
  single-statement body. No brace-less singleton blocks.
- Opening curly braces always go on their own line, never a "hanging"
  brace at the end of the previous line — for blocks and function bodies
  alike.
- Space before `(` and `[` — for function calls/declarations and array
  indexing alike — unless the character immediately before it is itself
  `)`, `(`, `]`, or `[`. For example:
  ```c
  rh_get (table, key, NULL)
  entries [pos]
  bar ()
  (char*)(malloc (length))   // no space: '(' is preceded by ')'
  RH_ENTRIES (table)[index]  // no space before '[': preceded by ')'
  ```
  Exception: `#define NAME(args)` function-like macro definitions never
  get a space before `(` — that's mandatory C preprocessor syntax (a
  space there makes it an object-like macro instead), not a style choice.
  This applies only to the `#define` line itself, not to invocations of
  the macro elsewhere (`RHE_KEY (entry)` still gets the space).
- When ordering lists of things, if there is no need for any particular
  ordering, put things in alphabetical/lexicographic order. Everything from
  dependencies in a makefile to ordering of function definitions in source
  files. Two recognized needs for a different order: grouping a `static`
  helper immediately before the function(s) that call it, when it's only
  ever used by one nearby group (`src/robinhood.c`: `rhi_advance_to_used`
  right before `rhi_advance`/`rhi_create`/`rhi_reset`, its only callers);
  and placing a definition next to the global variable that references it
  as an initializer (`src/robinhood.c`: `rh_default_warning_handler`,
  placed next to `static RHWarningHandler warning_handler =
  rh_default_warning_handler;` rather than in strict alphabetical order
  among the other `rh_*` functions).
- `*` in a pointer type binds to the type, not the variable name: `char*
  key`, never `char *key`. Applies everywhere a pointer type appears —
  declarations, parameters, casts, struct fields alike.
- Declare one variable per line. Never comma-separated multiple
  declarators (`int a, b;`), even when they share a type — this sidesteps
  the classic C gotcha where a `*`/`[]` on one declarator in such a list
  doesn't apply to the others (`char* a, b;` makes `b` a plain `char`,
  not a pointer).
- `//` exclusively, for every comment in this codebase — on its own line
  and trailing actual code on the same line alike, e.g. `return 0;   //
  overflow` (`src/robinhood.c`: `next_power_of_two`). No `/* ... */`
  comments anywhere, including short same-line annotations — a trailing
  `//` is always the last thing on its line by construction, so it works
  equally well there. `///`, used only in `include/robinhood.h` for
  Doxygen-style doc comments (see "Documentation comments" below), counts
  as part of this same `//` family, not an exception to it.

## Function declarations and definitions

Return type goes on its own line above the function name, and — for
`static` functions — so does the linkage keyword, above that:

```c
extern
void*
rh_get (RHTable     table,
        const char* key,
        void*       not_found_result);

static
size_t
next_power_of_two (size_t n);
```

This applies to function declarations/definitions only, not variables —
a `static`/`extern` variable keeps its linkage keyword on the same line
as its type: `static int errors = 0;`, `extern char** environ;`.

When there's more than one parameter, put one per line — in both
prototypes and definitions, not just prototypes — with types padded so
every parameter name lines up in the same column (pad each type to the
width of the widest type in the list, plus one space). A single-parameter
function stays on one line, e.g. `rh_capacity (RHTable table);` — the
one-per-line rule only kicks in once there's something to align.

This applies to declarations/definitions only, not call sites —
`rh_set (table, "me", value);` stays on one line regardless of argument
count.

The same column-alignment applies beyond function parameters, to any
block of consecutive, related declarations — struct fields and grouped
local variables alike:

```c
struct RHTable_struct
{
    RHEntry*     entries;
    size_t       capacity;
    size_t       count;
    unsigned int resize_threshold_percent;
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
  `WARNING_BUFFER_SIZE`) and `enum` constants (`EMPTY`/`USED`,
  `OPT_RESIZE_THRESHOLD`) — everything else (functions, variables,
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
*(dest++) = '/';                      // examples/scan.c: walk()
hash ^= (unsigned char)(*(text++));   // src/robinhood.c: string_hash()
```

In both cases, `++dest`/`++text` would advance the pointer *before* the
write, changing which byte gets touched — so postfix is required there,
not just preferred.

## Includes

Group `#include` directives into up to three blank-line-separated
blocks, in this order, each sorted alphabetically within itself:

1. This project's own headers, in quotes (`"robinhood.h"`) — kept
   separate and first so a missing or wrong local include shows up
   immediately, rather than being hidden behind whatever transitively
   pulls it in from a system header.
2. "Regular" system/library headers in angle brackets.
3. `<sys/...>` headers, split out into their own trailing block.

```c
#include "robinhood.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/stat.h>
```

(`examples/scan.c`.) A file with no `<sys/...>` headers just has the
first two blocks; a file with only one local header and no others
still gets its own block for it. Only include a header if the file
actually uses something it declares — don't include one "just in
case," and don't rely on one header transitively pulling in another
you also reference directly (e.g. `<sys/stat.h>` already guarantees
`dev_t`/`ino_t` per POSIX, so a file that only needs those two types
and already includes `<sys/stat.h>` doesn't also need `<sys/types.h>`).

## File structure

Every `.c`/`.h` file starts with a one-line file tagline comment,
immediately followed by an `SPDX-License-Identifier: Unlicense` line
(same comment block, before the form feed — this doesn't add a page of
its own, so it doesn't affect the ToC below), then a Table of Contents
comment block listing every section in file order, then the sections
themselves — each introduced by a `//` banner:

```c
//
// Section Name
//
```

Section boundaries are marked by a form-feed character (`^L`, `\f`) on
its own line, in place of the usual blank line separator. This is used
by an external tool to regenerate the Table of Contents from the first
non-blank line of each page, so keep section banners and the ToC listing
in sync when adding, renaming, or reordering sections.

In `.c` files, a function's banner is its name immediately followed by
its parameter types in parentheses, comma-separated, no spaces, no
return type, no parameter names -- e.g. `rh_clear(RHTable,const
char*)`, not `rh_clear` or `rh_clear (RHTable table, const char* key)`.
This is deliberately more compressed than the function's own
declaration; it exists to make the ToC/banner scan quickly, not to
restate the signature exactly as written. If a banner would exceed the
78-column limit, truncate it at 78 columns outright (even mid-word) —
don't wrap it or shorten individual type names to make it fit.

Every `.c` file with more than one function gets a "File-local
prototypes" page, right after any type-definition pages and before the
first function-definition page, forward-declaring every function
defined later in the file — written out in full (return type, `static`
where applicable, real parameter names, one per line once there's more
than one parameter — not the compressed banner form), alphabetically
by name, including `main` even though it's never `static` and normally
wouldn't need forward declaring. Macros (e.g. `CHECK`) and struct-only
sections don't get prototype entries -- only actual functions do.

A type-definition page holding only a plain data type (no reference to
any function) always goes before "File-local prototypes", per the above.
But `tests/tester.c`'s `struct test` (the `{ name, function }` pair type)
and its `tests[]` table are deliberately split across two different
pages precisely because of this ordering rule: `struct test` itself has
no function reference, so it lives in the early "Type definitions" page
like any other type. The `tests[]` table, though, is initialized with
pointers to every `test_*` function by name — it can't move any earlier
than "File-local prototypes" without losing the forward declarations
that make those names valid there, so it stays as a later page (see its
own `// Test table //` banner) positioned wherever in the function
sequence made sense, not up front with the type itself.

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
extern
int
example_function (int name);
```

Wrap comment text to the same 78-column limit as code. `@param`
descriptions align like the function's own parameter list would (see
"Function declarations and definitions" above) when there's more than
one. Never place a doc comment as the first non-blank line of a page —
that line feeds the ToC (see "File structure" above), and a `///`
line there hasn't been confirmed to interact well with the external
ToC tool. Implementation (`.c`) files don't repeat these comments —
only the public header does.
