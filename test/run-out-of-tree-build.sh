#!/bin/sh
# Regression oracle: a bootstrapped, genuinely out-of-tree build must succeed.
#
# Usage: test/run-out-of-tree-build.sh [SRCDIR]
#   SRCDIR defaults to the repository root containing this script.
#
# The supplied tree is copied to disposable storage before Autotools is
# bootstrapped. Ignored/generated configure inputs are never used, and neither
# bootstrap nor build products are written to the supplied tree.

set -u

if [ "$#" -gt 1 ]; then
    echo "Usage: $0 [SRCDIR]" >&2
    exit 2
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P) || exit 1
if [ "$#" -eq 1 ]; then
    INPUT_SRCDIR=$1
else
    INPUT_SRCDIR=$SCRIPT_DIR/..
fi

if ! SRCDIR=$(CDPATH= cd -- "$INPUT_SRCDIR" && pwd -P); then
    echo "ERROR: cannot access source directory '$INPUT_SRCDIR'" >&2
    exit 1
fi

for required in configure.ac Makefile.am src/Makefile.am lib/Makefile.am; do
    if [ ! -f "$SRCDIR/$required" ]; then
        echo "ERROR: '$SRCDIR' is not a complete gawm source tree (missing $required)" >&2
        exit 1
    fi
done

WORKDIR=$(mktemp -d /tmp/gawm-vpath.XXXXXX) || {
    echo "ERROR: could not create scratch directory under /tmp" >&2
    exit 1
}

cleanup()
{
    status=$?
    trap - 0 HUP INT TERM
    rm -rf -- "$WORKDIR"
    exit "$status"
}
trap cleanup 0
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

SOURCE_SNAPSHOT=$WORKDIR/source
BUILDDIR=$WORKDIR/build
SOURCE_ARCHIVE=$WORKDIR/source.tar
SOURCE_LIST=$WORKDIR/source.list
mkdir "$SOURCE_SNAPSHOT" "$BUILDDIR" || exit 1

# In a Git worktree, copy tracked and non-ignored untracked source files so
# current edits are tested without importing stale ignored build products.
# A clean exported source tree (used for baseline RED proof) takes the fallback.
GIT_TOP=
if command -v git >/dev/null 2>&1; then
    GIT_TOP=$(git -C "$SRCDIR" rev-parse --show-toplevel 2>/dev/null || :)
fi

if [ "$GIT_TOP" = "$SRCDIR" ]; then
    git -C "$SRCDIR" ls-files --cached --others --exclude-standard -z \
        >"$SOURCE_LIST" || exit 1
    tar -C "$SRCDIR" --null --no-recursion -T "$SOURCE_LIST" \
        -cf "$SOURCE_ARCHIVE" || exit 1
else
    tar -C "$SRCDIR" \
        --exclude='./.git' \
        --exclude='./autom4te.cache' \
        --exclude='./configure' \
        --exclude='./aclocal.m4' \
        --exclude='./config.h.in' \
        --exclude='./Makefile.in' \
        --exclude='./src/Makefile.in' \
        --exclude='./lib/Makefile.in' \
        --exclude='./po/Makefile.in' \
        --exclude='*.o' --exclude='*.a' --exclude='.deps' \
        -cf "$SOURCE_ARCHIVE" . || exit 1
fi

tar -xf "$SOURCE_ARCHIVE" -C "$SOURCE_SNAPSHOT" || exit 1
rm -f -- "$SOURCE_ARCHIVE" "$SOURCE_LIST"

# Force bootstrap exclusively from configure.ac and Makefile.am inputs even if
# a supplied non-Git tree contained generated Autotools files.
rm -rf -- "$SOURCE_SNAPSHOT/autom4te.cache"
rm -f -- \
    "$SOURCE_SNAPSHOT/configure" \
    "$SOURCE_SNAPSHOT/aclocal.m4" \
    "$SOURCE_SNAPSHOT/config.h.in" \
    "$SOURCE_SNAPSHOT/Makefile.in" \
    "$SOURCE_SNAPSHOT/src/Makefile.in" \
    "$SOURCE_SNAPSHOT/lib/Makefile.in" \
    "$SOURCE_SNAPSHOT/po/Makefile.in" \
    "$SOURCE_SNAPSHOT/config.guess" \
    "$SOURCE_SNAPSHOT/config.sub" \
    "$SOURCE_SNAPSHOT/config.rpath" \
    "$SOURCE_SNAPSHOT/depcomp" \
    "$SOURCE_SNAPSHOT/install-sh" \
    "$SOURCE_SNAPSHOT/missing" \
    "$SOURCE_SNAPSHOT/mkinstalldirs"

show_log()
{
    log=$1
    if [ -f "$log" ]; then
        tail -n 80 "$log" >&2
    fi
}

echo "Source snapshot: $SOURCE_SNAPSHOT"
echo "Build directory: $BUILDDIR"

if (CDPATH= cd -- "$SOURCE_SNAPSHOT" && autoreconf -fi) \
        >"$WORKDIR/autoreconf.log" 2>&1; then
    echo "[ok] autoreconf -fi"
else
    echo "[FAIL] autoreconf -fi" >&2
    show_log "$WORKDIR/autoreconf.log"
    exit 1
fi

if (CDPATH= cd -- "$BUILDDIR" && \
        "$SOURCE_SNAPSHOT/configure" --disable-nls --enable-gawsound=no) \
        >"$WORKDIR/configure.log" 2>&1; then
    echo "[ok] configure"
else
    echo "[FAIL] configure" >&2
    show_log "$WORKDIR/configure.log"
    exit 1
fi

if (CDPATH= cd -- "$BUILDDIR" && make -j2) \
        >"$WORKDIR/make.log" 2>&1; then
    echo "[ok] make -j2"
else
    echo "[FAIL] make -j2" >&2
    if [ -f "$BUILDDIR/lib/libload.a" ] && \
            [ ! -e "$SOURCE_SNAPSHOT/lib/libload.a" ]; then
        echo "[diagnostic] build-tree lib/libload.a exists; source-tree lib/libload.a is absent" >&2
    fi
    show_log "$WORKDIR/make.log"
    exit 1
fi
