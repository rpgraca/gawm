#!/bin/sh
# Display-free regression for raw dataset min/max after row replacement.
set -u

SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P) || exit 1

if [ "$#" -gt 1 ]; then
    echo "Usage: $0 [SRCDIR]" >&2
    exit 2
fi
if [ "$#" -eq 1 ]; then
    SRCDIR=$1
else
    SRCDIR=$SCRIPT_DIR/..
fi
if ! SRCDIR=$(CDPATH='' cd -- "$SRCDIR" && pwd -P); then
    echo "ERROR: cannot access source directory '$SRCDIR'" >&2
    exit 1
fi

WORKDIR=$(mktemp -d /tmp/gawm-dataset-minmax.XXXXXX) || exit 1
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

GIT_TOP=$(git -C "$SRCDIR" rev-parse --show-toplevel 2>/dev/null || :)
if [ "$GIT_TOP" = "$SRCDIR" ]; then
    git -C "$SRCDIR" ls-files --cached --others --exclude-standard -z \
        >"$SOURCE_LIST" || exit 1
    tar -C "$SRCDIR" --null --no-recursion -T "$SOURCE_LIST" \
        -cf "$SOURCE_ARCHIVE" || exit 1
else
    tar -C "$SRCDIR" --exclude='./.git' --exclude='autom4te.cache' \
        --exclude='configure' --exclude='aclocal.m4' --exclude='config.h.in' \
        --exclude='Makefile.in' --exclude='src/Makefile.in' \
        --exclude='lib/Makefile.in' --exclude='po/Makefile.in' \
        --exclude='*.o' --exclude='*.a' --exclude='.deps' \
        -cf "$SOURCE_ARCHIVE" . || exit 1
fi
tar -xf "$SOURCE_ARCHIVE" -C "$SOURCE_SNAPSHOT" || exit 1

rm -rf -- "$SOURCE_SNAPSHOT/autom4te.cache"
rm -f -- "$SOURCE_SNAPSHOT/configure" "$SOURCE_SNAPSHOT/aclocal.m4" \
    "$SOURCE_SNAPSHOT/config.h.in" "$SOURCE_SNAPSHOT/Makefile.in" \
    "$SOURCE_SNAPSHOT/src/Makefile.in" "$SOURCE_SNAPSHOT/lib/Makefile.in" \
    "$SOURCE_SNAPSHOT/po/Makefile.in" "$SOURCE_SNAPSHOT/config.guess" \
    "$SOURCE_SNAPSHOT/config.sub" "$SOURCE_SNAPSHOT/config.rpath" \
    "$SOURCE_SNAPSHOT/depcomp" "$SOURCE_SNAPSHOT/install-sh" \
    "$SOURCE_SNAPSHOT/missing" "$SOURCE_SNAPSHOT/mkinstalldirs"

(CDPATH='' cd -- "$SOURCE_SNAPSHOT" && autoreconf -fi) \
    >"$WORKDIR/autoreconf.log" 2>&1 || {
    tail -n 80 "$WORKDIR/autoreconf.log" >&2
    exit 1
}
(CDPATH='' cd -- "$BUILDDIR" && \
    "$SOURCE_SNAPSHOT/configure" --disable-nls --enable-gawsound=no) \
    >"$WORKDIR/configure.log" 2>&1 || {
    tail -n 80 "$WORKDIR/configure.log" >&2
    exit 1
}
(CDPATH='' cd -- "$BUILDDIR/lib" && make -j2 libload.a) \
    >"$WORKDIR/make.log" 2>&1 || {
    tail -n 80 "$WORKDIR/make.log" >&2
    exit 1
}

GLIB_CFLAGS=$(pkg-config --cflags glib-2.0) || GLIB_CFLAGS=""
GLIB_LIBS=$(pkg-config --libs glib-2.0) || GLIB_LIBS="-lglib-2.0"
INCS="-DHAVE_CONFIG_H -I$BUILDDIR -I$SOURCE_SNAPSHOT/lib $GLIB_CFLAGS"
HARNESS=$SCRIPT_DIR/test_dataset_minmax_harness.c
BIN=$WORKDIR/test_dataset_minmax

gcc $INCS -c "$HARNESS" -o "$WORKDIR/harness.o" \
    >"$WORKDIR/compile.log" 2>&1 || {
    tail -n 80 "$WORKDIR/compile.log" >&2
    exit 1
}
gcc "$WORKDIR/harness.o" "$BUILDDIR/lib/libload.a" -o "$BIN" \
    $GLIB_LIBS -lm >"$WORKDIR/link.log" 2>&1 || {
    tail -n 80 "$WORKDIR/link.log" >&2
    exit 1
}

OUT=$($BIN 2>&1)
STATUS=$?
printf '%s\n' "$OUT"
if [ "$STATUS" -eq 0 ] && printf '%s\n' "$OUT" | \
        grep -q '^PASS: dataset-minmax M1-M3 all satisfied'; then
    exit 0
fi
exit 1
