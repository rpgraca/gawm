#!/bin/sh
# Regression: malformed `copyvar`/`delvar` commands must be rejected without crashing.
#
# Builds a disposable out-of-tree gawm, then compiles and links the C harness
# `test_copyvar_harness.c` against the freshly built objects and runs it.
# The harness feeds truncated `copyvar`/`delvar` lines (no arguments) through
# the real aio_process_line path and asserts the server stays alive and returns
# an error for each.  See test_copyvar_harness.c for the assertion contract.
#
# RED  (unmodified): the process dies by SIGSEGV (exit 139).
# GREEN (fixed)    : exits 0 and prints "PASS: ...".
set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P) || exit 1

# Optional SRCDIR argument allows RED-proofing against a baseline snapshot:
#   test/run-copyvar-check.sh /path/to/unmodified/src
if [ "$#" -gt 1 ]; then
    echo "Usage: $0 [SRCDIR]" >&2
    exit 2
fi
if [ "$#" -eq 1 ]; then
    SRCDIR=$1
else
    SRCDIR=$SCRIPT_DIR/..
fi

if ! SRCDIR=$(CDPATH= cd -- "$SRCDIR" && pwd -P); then
    echo "ERROR: cannot access source directory '$SRCDIR'" >&2
    exit 1
fi

for required in configure.ac Makefile.am src/Makefile.am lib/Makefile.am; do
    if [ ! -f "$SRCDIR/$required" ]; then
        echo "ERROR: '$SRCDIR' is not a complete gawm source tree (missing $required)" >&2
        exit 1
    fi
done

WORKDIR=$(mktemp -d /tmp/gawm-copyvar.XXXXXX) || {
    echo "ERROR: could not create scratch directory under /tmp" >&2
    exit 1
}

cleanup()
{
    status=$?
    trap - 0 HUP INT TERM
    if [ "${DEBUG_KEEP:-0}" = "1" ]; then
        echo "[debug] scratch kept at $WORKDIR" >&2
    else
        rm -rf -- "$WORKDIR"
    fi
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

# Bootstrap only from Autotools inputs.
rm -rf -- "$SOURCE_SNAPSHOT/autom4te.cache"
rm -f -- \
    "$SOURCE_SNAPSHOT/configure" "$SOURCE_SNAPSHOT/aclocal.m4" \
    "$SOURCE_SNAPSHOT/config.h.in" "$SOURCE_SNAPSHOT/Makefile.in" \
    "$SOURCE_SNAPSHOT/src/Makefile.in" "$SOURCE_SNAPSHOT/lib/Makefile.in" \
    "$SOURCE_SNAPSHOT/po/Makefile.in" "$SOURCE_SNAPSHOT/config.guess" \
    "$SOURCE_SNAPSHOT/config.sub" "$SOURCE_SNAPSHOT/config.rpath" \
    "$SOURCE_SNAPSHOT/depcomp" "$SOURCE_SNAPSHOT/install-sh" \
    "$SOURCE_SNAPSHOT/missing" "$SOURCE_SNAPSHOT/mkinstalldirs"

echo "Source snapshot: $SOURCE_SNAPSHOT"
echo "Build directory: $BUILDDIR"

(CDPATH= cd -- "$SOURCE_SNAPSHOT" && autoreconf -fi) \
    >"$WORKDIR/autoreconf.log" 2>&1 || {
    echo "[FAIL] autoreconf -fi" >&2
    tail -n 80 "$WORKDIR/autoreconf.log" >&2
    exit 1
}
echo "[ok] autoreconf -fi"

(CDPATH= cd -- "$BUILDDIR" && \
    "$SOURCE_SNAPSHOT/configure" --disable-nls --enable-gawsound=no) \
    >"$WORKDIR/configure.log" 2>&1 || {
    echo "[FAIL] configure" >&2
    tail -n 80 "$WORKDIR/configure.log" >&2
    exit 1
}
echo "[ok] configure"

(CDPATH= cd -- "$BUILDDIR" && make -j2) >"$WORKDIR/make.log" 2>&1 || {
    echo "[FAIL] make -j2" >&2
    tail -n 80 "$WORKDIR/make.log" >&2
    exit 1
}
echo "[ok] make -j2"

# Compile the harness against the freshly built sources and objects.
HARNESS=$SCRIPT_DIR/test_copyvar_harness.c
HARNESS_OBJ=$WORKDIR/test_copyvar_harness.o
HARNESS_BIN=$WORKDIR/test_copyvar

SRC_SRC=$SOURCE_SNAPSHOT/src
SRC_LIB=$SOURCE_SNAPSHOT/lib
BLD_SRC=$BUILDDIR/src
BLD_LIB=$BUILDDIR/lib

CFLAGS=$(pkg-config --cflags gtk+-3.0) || CFLAGS=""
INCS="-DHAVE_CONFIG_H -I$BUILDDIR -I$SRC_LIB -I$SRC_SRC $CFLAGS"

if ! gcc $INCS -c "$HARNESS" -o "$HARNESS_OBJ" >"$WORKDIR/compile.log" 2>&1; then
    echo "[FAIL] compile harness" >&2
    tail -n 80 "$WORKDIR/compile.log" >&2
    exit 1
fi
echo "[ok] harness compile"

# Link against all real gawm objects except gawmain.o's `main`.  We keep its
# global definitions (userData, aw_* helpers) by relocating `main` to an
# unused symbol in a copied object.
if ! objcopy --redefine-sym main=unused_gaw_main \
        "$BLD_SRC/gawmain.o" "$WORKDIR/gawmain_nomain.o" \
        >"$WORKDIR/objcopy.log" 2>&1; then
    echo "[FAIL] objcopy gawmain" >&2
    tail -n 80 "$WORKDIR/objcopy.log" >&2
    exit 1
fi

OBJS=$(ls "$BLD_SRC"/*.o | grep -v '/gawmain\.o$' | tr '\n' ' ')

LIBS=$(pkg-config --libs gtk+-3.0) || LIBS=""
if ! gcc "$HARNESS_OBJ" $OBJS "$WORKDIR/gawmain_nomain.o" \
        "$BLD_LIB/libload.a" -o "$HARNESS_BIN" $LIBS -lXext -lm \
        >"$WORKDIR/link.log" 2>&1; then
    echo "[FAIL] link harness" >&2
    tail -n 80 "$WORKDIR/link.log" >&2
    exit 1
fi
echo "[ok] harness link"

# Run the harness.  It must exit 0 and print PASS; a SIGSEGV (unfixed code)
# yields a non-zero exit from the signal.
OUT=$("$HARNESS_BIN" 2>&1)
STATUS=$?
echo "$OUT"
if [ "$STATUS" -ne 0 ]; then
    echo "[FAIL] a malformed copyvar/delvar did not survive ($STATUS)" >&2
    exit 1
fi
if ! printf '%s\n' "$OUT" | grep -q "PASS: malformed copyvar/delvar returned error without crashing"; then
    echo "[FAIL] missing PASS marker" >&2
    exit 1
fi
echo "[ok] harness run (no crash, error returned for all commands)"
exit 0
