#!/bin/sh
# Acceptance node for the derived-value min/max/range cache (checks G1..G4).
#
# Builds a disposable out-of-tree gawm, compiles AND LINKS only the real
# lib/libload.a (no GTK/display), then builds test_derived_cache_harness.c
# against it and runs it.
#
# The cache (G) is a behaviour-PRESERVING optimization with no black-box
# behavioural change, so the harness proves the cache exists and invalidates
# through a dedicated read-only seam `wavevar_derived_cache_valid`.  On a
# baseline without the cache (no seam, no cache fields) the harness cannot
# compile/link, which IS the intended RED.  On the fixed code it compiles,
# links, runs, and exits 0 only when G1..G4 all pass.
#
# Usage: test/run-derived-cache-check.sh [SRCDIR]   (SRCDIR defaults to repo root)
set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P) || exit 1

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

WORKDIR=$(mktemp -d /tmp/gawm-derived-cache.XXXXXX) || {
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

(CDPATH= cd -- "$BUILDDIR/lib" && make -j2 libload.a) \
    >"$WORKDIR/make.log" 2>&1 || {
    echo "[FAIL] make lib/libload.a" >&2
    tail -n 80 "$WORKDIR/make.log" >&2
    exit 1
}
echo "[ok] make lib/libload.a"

SRC_LIB=$SOURCE_SNAPSHOT/lib
BLD_LIB=$BUILDDIR/lib
HARNESS=$SCRIPT_DIR/test_derived_cache_harness.c

GLIB_CFLAGS=$(pkg-config --cflags glib-2.0) || GLIB_CFLAGS=""
GLIB_LIBS=$(pkg-config --libs glib-2.0) || GLIB_LIBS="-lglib-2.0"

INCS="-DHAVE_CONFIG_H -I$BUILDDIR -I$SRC_LIB $GLIB_CFLAGS"

BIN=$WORKDIR/test_derived_cache

if ! gcc $INCS -c "$HARNESS" -o "$WORKDIR/harness.o" >"$WORKDIR/compile.log" 2>&1; then
    echo "[FAIL] compile harness" >&2
    tail -n 80 "$WORKDIR/compile.log" >&2
    echo "VERDICT: RED (baseline) - harness did not compile; the cache seam" >&2
    echo "  wavevar_derived_cache_valid / cache fields are absent" >&2
    exit 1
fi
echo "[ok] harness compile"

if ! gcc "$WORKDIR/harness.o" "$BLD_LIB/libload.a" -o "$BIN" \
        $GLIB_LIBS -lm >"$WORKDIR/link.log" 2>&1; then
    echo "[FAIL] link harness (plain)" >&2
    tail -n 80 "$WORKDIR/link.log" >&2
    echo "VERDICT: RED (baseline) - harness did not link; the cache seam" >&2
    echo "  wavevar_derived_cache_valid / cache fields are absent" >&2
    exit 1
fi
echo "[ok] harness link (plain)"

echo
echo "=== derived-cache behaviour pass (G1..G4) ==="
OUT=$("$BIN" 2>&1)
STATUS=$?
printf '%s\n' "$OUT"

if [ "$STATUS" -eq 0 ] && printf '%s\n' "$OUT" | grep -q \
        "^PASS: derived-cache G1-G4 all satisfied"; then
    echo
    echo "VERDICT: GREEN - G1..G4 all pass"
    exit 0
fi

echo
echo "VERDICT: RED on current code:" 
printf '%s\n' "$OUT" | grep '^c\[FAIL\]' | sed 's/^/  /'
exit 1
