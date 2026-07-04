dnl configure.ac -- autoconf input for robinhood.
dnl
dnl Checks for the features robinhood actually needs (most of them
dnl POSIX, two BSD/GNU extensions) and fails outright if any are
dnl missing -- there's no optional/fallback behavior to gate behind
dnl HAVE_* defines, so this deliberately doesn't generate a config.h.

AC_INIT([robinhood], [1.0.0])
AC_CONFIG_SRCDIR([src/robinhood.c])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([foreign subdir-objects])

dnl A plain `git clone` doesn't preserve file mtimes, so configure.ac/
dnl Makefile.am can end up looking newer than the committed configure/
dnl Makefile.in/aclocal.m4 purely from checkout ordering -- without this,
dnl that spuriously triggers automake's maintainer-mode rules to
dnl regenerate them via aclocal/autoconf/automake, which a consumer
dnl building from a plain checkout doesn't have installed. Disabling
dnl maintainer mode means `make` always just uses the already-generated,
dnl committed files; `./configure --enable-maintainer-mode` opts back in
dnl for anyone actually editing configure.ac/Makefile.am.
AM_MAINTAINER_MODE([disable])

AC_PROG_CC
AM_PROG_AR
LT_INIT

dnl <err.h> err()/warn()/warnx() (used by src/robinhood.c) -- BSD
dnl extensions, not POSIX, but present on macOS, the BSDs, glibc, and
dnl musl.
AC_MSG_CHECKING([for err.h err()/warn()/warnx()])
AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
#include <err.h>
int main (void) { warn ("%s", "x"); warnx ("%s", "x"); return 0; }
]])], [AC_MSG_RESULT([yes])], [
    AC_MSG_RESULT([no])
    AC_MSG_ERROR([[could not compile a program using <err.h>
err()/warn()/warnx() -- these are required by src/robinhood.c. They
are BSD extensions, not POSIX, but are present on macOS, the BSDs,
glibc, and musl; if your libc genuinely lacks them this library
cannot build as currently written.]])
])

dnl openat/fstatat/fdopendir (POSIX.1-2008, used by examples/scan.c)
AC_MSG_CHECKING([for openat/fstatat/fdopendir])
AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
#define _XOPEN_SOURCE 700
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
int main (void)
{
    int fd = openat (AT_FDCWD, ".", O_RDONLY | O_DIRECTORY);
    struct stat st;
    fstatat (fd, ".", &st, AT_SYMLINK_NOFOLLOW);
    DIR* d = fdopendir (fd);
    return d == 0;
}
]])], [AC_MSG_RESULT([yes])], [
    AC_MSG_RESULT([no])
    AC_MSG_ERROR([[could not compile a program using
openat/fstatat/fdopendir -- these POSIX.1-2008 calls are required by
examples/scan.c]])
])

dnl nftw (POSIX.1-2001 XSI, used by tests/tester.c)
AC_MSG_CHECKING([for nftw])
AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
#define _XOPEN_SOURCE 700
#include <ftw.h>
#include <sys/stat.h>
static int visit (const char* p, const struct stat* s, int t, struct FTW* f)
{
    return 0;
}
int main (void) { return nftw (".", visit, 20, FTW_PHYS); }
]])], [AC_MSG_RESULT([yes])], [
    AC_MSG_RESULT([no])
    AC_MSG_ERROR([[could not compile a program using nftw() -- this
POSIX.1-2001 XSI call is required by tests/tester.c]])
])

dnl getifaddrs (BSD-originated, not POSIX, used by examples/netifs.c)
AC_MSG_CHECKING([for getifaddrs])
AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
#include <ifaddrs.h>
int main (void)
{
    struct ifaddrs* list;
    if (getifaddrs (&list) != 0) return 1;
    freeifaddrs (list);
    return 0;
}
]])], [AC_MSG_RESULT([yes])], [
    AC_MSG_RESULT([no])
    AC_MSG_ERROR([[could not compile a program using getifaddrs() --
this is required by examples/netifs.c. It originated in BSD and is
not part of POSIX, but is present on macOS, the BSDs, glibc, and
musl.]])
])

dnl getopt_long (GNU extension, not POSIX, used by the example programs)
AC_MSG_CHECKING([for getopt_long])
AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
#include <getopt.h>
static struct option opts[] =
{
    { "help", no_argument, 0, 1 },
    { 0, 0, 0, 0 }
};
int main (void)
{
    return getopt_long (0, (char**)0, "", opts, (int*)0);
}
]])], [AC_MSG_RESULT([yes])], [
    AC_MSG_RESULT([no])
    AC_MSG_ERROR([[could not compile a program using getopt_long() --
this is required by examples/scan.c, memo.c, wordfreq.c, netifs.c, and
environ.c for command-line argument parsing. It originated in GNU libc
and is not part of POSIX, but is present on macOS, the BSDs, glibc,
and musl.]])
])

AC_CONFIG_FILES([Makefile robinhood.pc])
AC_OUTPUT
