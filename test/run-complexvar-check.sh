#!/bin/sh
# Acceptance node for the dirty complex-derived-WaveVar patch (checks C1..C9).
#
# Builds a disposable out-of-tree gawm, compiles AND LINKS only the real
# lib/libload.a (no GTK/display), then builds the harness
# test_complexvar_harness.c against it and runs:
#
#   1. NORMAL behaviour pass  - runs every C1..C9 check and reports each
#                              (no early abort).  The current patch is RED on
#                              C3 (huge magnitude -> inf), C8 (canonical
#                              names / dataset_get_var_for_name) and C9
#                              (get-or-create pointer identity).
#   2. Address/LeakSanitizer LIFECYCLE pass - runs `--lifecycle-only`, which
#                              creates repeated derived WaveVars and destroys
#                              the dataset.  The current patch leaks every
#                              derived WaveVar (never tracked/destroyed), so
#                              LeakSanitizer exits nonzero; a fixed patch must
#                              be leak-free and exit 0.
#
# The overall command exits 0 ONLY when the normal suite is a clean PASS and
# the lifecycle pass is leak-free; any fault yields a non-zero exit with an
# explicit "expected RED / GREEN" verdict.
#
# Usage: test/run-complexvar-check.sh [SRCDIR]   (SRCDIR defaults to repo root)
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

WORKDIR=$(mktemp -d /tmp/gawm-complexvar.XXXXXX) || {
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

# Build only the real library used by the harness: lib/libload.a.  The lib is
# an automake SUBDIR, so the target must be built from the lib sub-Makefile
# (there is no top-level `lib/libload.a` target).
(CDPATH= cd -- "$BUILDDIR/lib" && make -j2 libload.a) \
    >"$WORKDIR/make.log" 2>&1 || {
    echo "[FAIL] make lib/libload.a" >&2
    tail -n 80 "$WORKDIR/make.log" >&2
    exit 1
}
echo "[ok] make lib/libload.a"

SRC_LIB=$SOURCE_SNAPSHOT/lib
BLD_LIB=$BUILDDIR/lib
HARNESS=$SCRIPT_DIR/test_complexvar_harness.c

GLIB_CFLAGS=$(pkg-config --cflags glib-2.0) || GLIB_CFLAGS=""
GLIB_LIBS=$(pkg-config --libs glib-2.0) || GLIB_LIBS="-lglib-2.0"

INCS="-DHAVE_CONFIG_H -I$BUILDDIR -I$SRC_LIB $GLIB_CFLAGS"

BIN=$WORKDIR/test_complexvar
BIN_ASAN=$WORKDIR/test_complexvar_asan

if ! gcc $INCS -c "$HARNESS" -o "$WORKDIR/harness.o" >"$WORKDIR/compile.log" 2>&1; then
    echo "[FAIL] compile harness" >&2
    tail -n 80 "$WORKDIR/compile.log" >&2
    exit 1
fi
echo "[ok] harness compile"

if ! gcc "$WORKDIR/harness.o" "$BLD_LIB/libload.a" -o "$BIN" \
        $GLIB_LIBS -lm >"$WORKDIR/link.log" 2>&1; then
    echo "[FAIL] link plain harness" >&2
    tail -n 80 "$WORKDIR/link.log" >&2
    exit 1
fi
echo "[ok] harness link (plain)"

if ! gcc $INCS -fsanitize=address,undefined -g -O1 -fno-omit-frame-pointer \
        -c "$HARNESS" -o "$WORKDIR/harness_asan.o" \
        >"$WORKDIR/compile_asan.log" 2>&1; then
    echo "[FAIL] compile ASan harness" >&2
    tail -n 80 "$WORKDIR/compile_asan.log" >&2
    exit 1
fi
if ! gcc "$WORKDIR/harness_asan.o" "$BLD_LIB/libload.a" \
        -fsanitize=address,undefined -o "$BIN_ASAN" \
        $GLIB_LIBS -lm >"$WORKDIR/link_asan.log" 2>&1; then
    echo "[FAIL] link ASan harness" >&2
    tail -n 80 "$WORKDIR/link_asan.log" >&2
    exit 1
fi
echo "[ok] harness link (ASan/UBSan)"

echo
echo "=== NORMAL behaviour pass (C1..C9) ==="
OUT=$("$BIN" 2>&1)
NORMAL_STATUS=$?
printf '%s\n' "$OUT"

if printf '%s\n' "$OUT" | grep -q "^PASS: complexvar C1-C9 all satisfied"; then
    NORMAL_OK=1
else
    NORMAL_OK=0
fi

echo
echo "=== Address/LeakSanitizer LIFECYCLE pass (--lifecycle-only) ==="
ASAN_OPTIONS="detect_leaks=1:halt_on_error=1:exitcode=99" \
LSAN_OPTIONS="suppressions=/dev/null" \
G_SLICE="always-malloc" \
    "$BIN_ASAN" --lifecycle-only >"$WORKDIR/lifecycle.out" 2>&1
LIFE_STATUS=$?
cat "$WORKDIR/lifecycle.out"
if [ "$LIFE_STATUS" -eq 0 ]; then
    LEAK_OK=1
else
    LEAK_OK=0
fi

echo
if [ "$NORMAL_OK" -eq 1 ] && [ "$LEAK_OK" -eq 1 ]; then
    echo "VERDICT: GREEN - normal C1..C9 all pass AND lifecycle pass is leak-free"
    exit 0
fi

echo "VERDICT: RED on current dirty patch:"
[ "$NORMAL_OK" -eq 0 ] && echo "  - normal suite: C3/C8/C9 (or more) not satisfied (expected on current patch)"
if [ "$LEAK_OK" -eq 0 ]; then
    echo "  - lifecycle: LeakSanitizer reported leaks (exit $LIFE_STATUS; expected on current patch)"
    echo "      * derived WaveVars: repeated wavevar_new_derived() objects/names are never"
    echo "        owned/freed by dataset_destroy (the acceptance target of this node)"
    echo "      * NOTE: the pre-existing wds->vars container leak (dataset_destroy's"
    echo "        g_ptr_array_free(vars, FALSE) never freeing ->pdata) is ISOLATED and"
    echo "        out of scope: the harness ignores that exact allocation via"
    echo "        __lsan_ignore_object() in lifecycle mode, so it is not a barrier to"
    echo "        'fixed code zero' and must not force an implementation change."
fi
exit 1
