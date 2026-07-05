ACLOCAL_AMFLAGS = -I m4

AM_CPPFLAGS = -I$(srcdir)/include

lib_LTLIBRARIES = librobinhood.la
librobinhood_la_SOURCES = src/robinhood.c
librobinhood_la_LDFLAGS = -version-info 0:0:0
librobinhood_la_LIBADD = -lm

include_HEADERS = include/robinhood.h

noinst_PROGRAMS = scan memo netifs environ wordfreq tester

scan_SOURCES = examples/scan.c
scan_LDADD = librobinhood.la -lm

memo_SOURCES = examples/memo.c
memo_LDADD = librobinhood.la -lm

netifs_SOURCES = examples/netifs.c
netifs_LDADD = librobinhood.la -lm

environ_SOURCES = examples/environ.c
environ_LDADD = librobinhood.la -lm

wordfreq_SOURCES = examples/wordfreq.c
wordfreq_LDADD = librobinhood.la -lm

tester_SOURCES = tests/tester.c
tester_LDADD = librobinhood.la -lm

TESTS = tester

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = robinhood.pc

EXTRA_DIST = README.md STYLE.md PERFORMANCE.md LICENSE Doxyfile .clang-format

# Bypasses the built library, so this doesn't fit the _PROGRAMS/
# _LIBRARIES model above: gcov/ASan+UBSan both need their instrumentation
# flags baked into the compile and link steps, which the already-built
# librobinhood.la can't be retrofitted with after the fact.
#
# allocator_may_return_null=1: test_warning_handler deliberately forces
# a huge rh_create() capacity to trigger a real calloc() failure. Plain
# libc calloc() detects the overflow and returns NULL per spec, but
# ASan's calloc interceptor aborts on it by default (usually a sign of
# a real bug) -- this opts out of that abort for a test that triggers
# it on purpose.
SANITIZE_FLAGS = -fsanitize=address,undefined -O0 -g

sanitize:
	$(CC) -I$(srcdir)/include $(SANITIZE_FLAGS) -o tester-sanitize \
	      $(srcdir)/src/robinhood.c $(srcdir)/tests/tester.c -lm
	ASAN_OPTIONS=allocator_may_return_null=1 ./tester-sanitize
	$(RM) tester-sanitize
	$(RM) -r tester-sanitize.dSYM

# Optional -- not part of `all`/`check`. README.md is the generated
# docs' front page via USE_MDFILE_AS_MAINPAGE.
docs:
	@command -v doxygen >/dev/null 2>&1 || { \
	    echo "docs: doxygen not found -- install it" \
	         "(e.g. 'brew install doxygen' or 'apt install doxygen')" >&2; \
	    exit 1; \
	}
	doxygen $(srcdir)/Doxyfile
	@echo "Generated docs/html/index.html"

# Optional -- not part of `all`/`check`. Reformats in place per
# .clang-format at the repo root (clang-format finds it automatically,
# searching upward from each file's own directory). See STYLE.md and
# .clang-format's own header comment for what this does and doesn't
# cover -- it doesn't replicate every hand-tuned convention (e.g. a
# short signature that already fits on one line loses its forced
# multi-line, column-aligned form), so review the diff rather than
# trusting it blindly on existing, hand-formatted code.
format:
	@command -v clang-format >/dev/null 2>&1 || { \
	    echo "format: clang-format not found -- install it" \
	         "(e.g. 'brew install clang-format' or 'apt install clang-format')" >&2; \
	    exit 1; \
	}
	clang-format -i $(srcdir)/include/*.h $(srcdir)/src/*.c \
	                 $(srcdir)/tests/*.c $(srcdir)/examples/*.c

# Optional -- not part of `all`/`check`. Regenerates configure/
# Makefile.in/aclocal.m4/build-aux/* from configure.ac/Makefile.am via
# autoreconf. configure.ac's AM_MAINTAINER_MODE([disable]) means plain
# `make` won't do this on its own (deliberate -- see README.md's
# Building section for why), so this is the one place that incantation
# needs to be remembered. The PATH prefix is a no-op everywhere except
# Homebrew macOS, where GNU libtool/libtoolize aren't on PATH by
# default (to avoid shadowing the incompatible BSD /usr/bin/libtool)
# -- without it, autoreconf either fails outright or silently uses the
# wrong libtool.
autogen:
	PATH="/opt/homebrew/opt/libtool/libexec/gnubin:$$PATH" autoreconf --install
	@echo "Regenerated configure/Makefile.in/aclocal.m4/build-aux --" \
	      "review the diff before committing."

# Optional -- not part of `all`/`check`. Bumps configure.ac's AC_INIT
# version (the single source of truth every other version string --
# PACKAGE_VERSION, robinhood.pc, the dist tarball name -- derives
# from) and regenerates the build system to match. Pass the new
# version explicitly, e.g.:
#   make version-bump VERSION=1.0.1
version-bump:
	@test -n "$(VERSION)" || { \
	    echo "version-bump: pass the new version, e.g." \
	         "'make version-bump VERSION=1.0.1'" >&2; \
	    exit 1; \
	}
	sed -i.bak -E \
	    's/^AC_INIT\(\[robinhood\], \[[^]]*\]\)/AC_INIT([robinhood], [$(VERSION)])/' \
	    $(srcdir)/configure.ac
	$(RM) $(srcdir)/configure.ac.bak
	$(MAKE) autogen
	@echo "Bumped to $(VERSION) -- review the diff, rebuild to confirm," \
	      "then tag."

clean-local:
	$(RM) -r docs
	$(RM) *.gcda *.gcno *.gcov coverage.info
	$(RM) -r coverage-html

# Alias: this project's docs/scripts all say "make test".
test: check

# Bypasses the built library, same reasoning as `sanitize` above: gcov
# needs --coverage baked into both the compile and link steps, which
# the already-built librobinhood.la can't be retrofitted with after
# the fact. Sources are compiled to objects explicitly (rather than
# compiling and linking in one step, as `sanitize` does) so the
# .gcno/.gcda notes/data files land at a predictable name matching the
# object (robinhood.gcno) -- combined compile-and-link instead names
# them after the *executable* on this platform's clang (e.g.
# tester-coverage-robinhood.gcno), which gcov won't find when later
# asked to report on plain "robinhood.c".
COVERAGE_FLAGS = --coverage -O0 -g

coverage:
	$(RM) *.gcda *.gcno *.gcov
	$(CC) -I$(srcdir)/include $(COVERAGE_FLAGS) -c $(srcdir)/src/robinhood.c -o robinhood.o
	$(CC) -I$(srcdir)/include $(COVERAGE_FLAGS) -c $(srcdir)/tests/tester.c -o tester.o
	$(CC) $(COVERAGE_FLAGS) -o tester-coverage robinhood.o tester.o -lm
	./tester-coverage
	gcov -b -f -o . $(srcdir)/src/robinhood.c
	@echo "Coverage report: robinhood.c.gcov"
	$(RM) tester-coverage robinhood.o tester.o
	$(RM) -r tester-coverage.dSYM

# Optional -- not part of `all`/`check`, and needs lcov/genhtml, which
# unlike gcov itself isn't bundled with either Xcode CLT or a stock
# GCC/glibc install (`brew install lcov` / `apt install lcov`). Same
# tool-check-with-install-hint pattern as `docs` above. Depends on
# `coverage` to regenerate fresh .gcda/.gcno data first.
coverage-html: coverage
	@command -v genhtml >/dev/null 2>&1 || { \
	    echo "coverage-html: lcov/genhtml not found -- install it" \
	         "(e.g. 'brew install lcov' or 'apt install lcov')" >&2; \
	    exit 1; \
	}
	lcov --capture --directory . --base-directory $(srcdir) \
	     --include '*/src/robinhood.c' --output-file coverage.info
	genhtml coverage.info --output-directory coverage-html
	@echo "Generated coverage-html/index.html"

.PHONY: test sanitize docs format autogen version-bump coverage coverage-html

tarball:
	( cd .. ; tar cf - robinhood | gzip > robinhood-`date +'%Y%m%d-%H%M-%Z'`.tar.gz )
