#!/bin/sh
# Acceptance node for the 16-byte container leak in dataset_destroy().
#
# Builds a disposable out-of-tree gawm, compiles AND LINKS only the real
# lib/libload.a (no GTK/display), then builds the harness
# test_dataset_leak_harness.c against it and runs it under
# Address/LeakSanitizer:
#
#   - The harness constructs a small WDataSet (3 variables / columns, 4
#     rows), calls dataset_destroy() and exits 0.  On the unfixed baseline,
#     dataset_destroy leaks the wds->vars GPtrArray backing segment (it calls
#     g_ptr_array_free(vars, FALSE) after dataset_remove_all_vars() already
#     freed every element, so the ->pdata element array is returned to no one
#     and never freed -- 16 bytes for a small dataset).  LeakSanitizer must
#     report that exact allocation and exit nonzero (RED).
#   - A fixed dataset_destroy (g_ptr_array_free(vars, TRUE)) must be leak-free
#     and exit 0 (GREEN).
#
# Usage: test/run-dataset-leak-check.sh [SRCDIR]   (SRCDIR defaults to repo root)
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

WORKDIR=$(mktemp -d /tmp/gawm-datasetleak.XXXXXX) || {
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

# Build only the real library used by the harness: lib/libload.a.
(CDPATH= cd -- "$BUILDDIR/lib" && make -j2 libload.a) \
    >"$WORKDIR/make.log" 2>&1 || {
    echo "[FAIL] make lib/libload.a" >&2
    tail -n 80 "$WORKDIR/make.log" >&2
    exit 1
}
echo "[ok] make lib/libload.a"

SRC_LIB=$SOURCE_SNAPSHOT/lib
BLD_LIB=$BUILDDIR/lib
HARNESS=$SCRIPT_DIR/test_dataset_leak_harness.c

GLIB_CFLAGS=$(pkg-config --cflags glib-2.0) || GLIB_CFLAGS=""
GLIB_LIBS=$(pkg-config --libs glib-2.0) || GLIB_LIBS="-lglib-2.0"

INCS="-DHAVE_CONFIG_H -I$BUILDDIR -I$SRC_LIB $GLIB_CFLAGS"

BIN_ASAN=$WORKDIR/test_dataset_leak_asan

if ! gcc $INCS -fsanitize=address -g -O1 -fno-omit-frame-pointer \
        -c "$HARNESS" -o "$WORKDIR/harness_asan.o" \
        >"$WORKDIR/compile_asan.log" 2>&1; then
    echo "[FAIL] compile ASan harness" >&2
    tail -n 80 "$WORKDIR/compile_asan.log" >&2
    exit 1
fi
echo "[ok] harness compile (ASan)"

# Link with ONLY -fsanitize=address for the leak pass.
if ! gcc "$WORKDIR/harness_asan.o" "$BLD_LIB/libload.a" \
        -fsanitize=address -o "$BIN_ASAN" \
        $GLIB_LIBS -lm >"$WORKDIR/link_asan.log" 2>&1; then
    echo "[FAIL] link ASan harness" >&2
    tail -n 80 "$WORKDIR/link_asan.log" >&2
    exit 1
fi
echo "[ok] harness link (ASan)"

echo
echo "=== Address/LeakSanitizer leak pass ==="
ASAN_OPTIONS="detect_leaks=1:halt_on_error=1:exitcode=99" \
LSAN_OPTIONS="suppressions=/dev/null" \
G_SLICE="always-malloc" \
    "$BIN_ASAN" >"$WORKDIR/leak.out" 2>&1
LIFE_STATUS=$?
cat "$WORKDIR/leak.out"
echo
if [ "$LIFE_STATUS" -eq 0 ]; then
    echo "VERDICT: GREEN - dataset_destroy is leak-free (zero definitely-lost bytes)"
    exit 0
fi

echo "VERDICT: RED - LeakSanitizer reported leaks (exit $LIFE_STATUS)"
if printf '%s\n' "$(cat "$WORKDIR/leak.out")" | grep -q "definitely lost"; then
    echo "  - definitely-lost bytes present (the wds->vars backing segment leak)"
fi
exit 1
