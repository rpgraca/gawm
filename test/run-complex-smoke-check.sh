#!/bin/sh
# End-to-end complex-variable smoke check.
#
# By default this builds a disposable snapshot of the source tree, launches
# gawm on a private GTK Broadway display, loads complex_smoke.raw over gawio,
# plots V(out):mag, expands the complex signal folder in headless Chromium,
# and verifies the screenshot with OCR.
#
# Frozen acceptance:
#   S1  the Spice3 complex fixture loads and becomes the current table;
#   S2  `copyvar V(out):mag p0` resolves through gawio and adds the trace;
#   S3  the signal tree renders Real, Magnitude, and Phase leaves;
#   S4  the plotted magnitude autoscales to 5.000 (the raw real max is 3);
#   S5  gawm remains alive and a non-empty screenshot artifact is retained.
# Spice3's file-wide `Flags: complex` applies to both declared variables, so
# each value row is index, t(real,imag), V(out)(real,imag).
#
# Environment:
#   GAWM_SMOKE_BIN          use an existing binary instead of building SRCDIR
#   GAWM_SMOKE_ARTIFACT_DIR directory for screenshot and OCR output
#   GAWM_SMOKE_CHROMIUM     Chromium executable (default /usr/bin/chromium)
#   GAWM_SMOKE_PLAYWRIGHT   Playwright npm version (default 1.62.1)
#   DEBUG_KEEP=1            retain the disposable build/log directory
# Without an artifact override, evidence is retained in a new directory under
# /tmp and its path is printed on exit.
#
# Usage: test/run-complex-smoke-check.sh [SRCDIR]
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

for required in configure.ac Makefile.am src/Makefile.am lib/Makefile.am \
                test/complex_smoke.raw test/test_complex_smoke_broadway.js; do
    if [ ! -f "$SRCDIR/$required" ]; then
        echo "ERROR: source tree is missing '$required'" >&2
        exit 1
    fi
done

for command in broadwayd chromium npm node python3 tesseract; do
    if ! command -v "$command" >/dev/null 2>&1; then
        echo "ERROR: required smoke-test command '$command' is unavailable" >&2
        exit 1
    fi
done
if [ -z "${GAWM_SMOKE_BIN:-}" ]; then
    for command in autoreconf make gcc pkg-config; do
        if ! command -v "$command" >/dev/null 2>&1; then
            echo "ERROR: required build command '$command' is unavailable" >&2
            exit 1
        fi
    done
fi

WORKDIR=$(mktemp -d /tmp/gawm-complex-smoke.XXXXXX) || exit 1
if [ -n "${GAWM_SMOKE_ARTIFACT_DIR:-}" ]; then
    ARTIFACT_DIR=$GAWM_SMOKE_ARTIFACT_DIR
    mkdir -p "$ARTIFACT_DIR" || exit 1
else
    ARTIFACT_DIR=$(mktemp -d /tmp/gawm-complex-smoke-artifacts.XXXXXX) || exit 1
fi

BROADWAY_PID=
GAWM_PID=

cleanup()
{
    status=$?
    trap - 0 HUP INT TERM
    if [ -n "$GAWM_PID" ]; then
        kill "$GAWM_PID" 2>/dev/null || true
    fi
    if [ -n "$BROADWAY_PID" ]; then
        kill "$BROADWAY_PID" 2>/dev/null || true
    fi
    wait 2>/dev/null || true

    if [ "$status" -ne 0 ]; then
        echo "[FAIL] complex smoke check" >&2
        for log in "$WORKDIR/gawm.log" "$WORKDIR/broadway.log"; do
            if [ -s "$log" ]; then
                echo "--- $log ---" >&2
                tail -n 80 "$log" >&2
            fi
        done
    fi

    if [ "${DEBUG_KEEP:-0}" = "1" ]; then
        echo "[debug] scratch kept at $WORKDIR" >&2
    else
        rm -rf -- "$WORKDIR"
    fi
    echo "Artifacts: $ARTIFACT_DIR"
    exit "$status"
}
trap cleanup 0
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

if [ -n "${GAWM_SMOKE_BIN:-}" ]; then
    GAWM_BIN=$GAWM_SMOKE_BIN
    if [ ! -x "$GAWM_BIN" ]; then
        echo "ERROR: GAWM_SMOKE_BIN is not executable: $GAWM_BIN" >&2
        exit 1
    fi
    echo "Using supplied gawm: $GAWM_BIN"
else
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
        tar -C "$SRCDIR" --exclude='./.git' --exclude='autom4te.cache' \
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
        echo "[FAIL] autoreconf -fi" >&2
        tail -n 80 "$WORKDIR/autoreconf.log" >&2
        exit 1
    }
    echo "[ok] autoreconf -fi"

    (CDPATH='' cd -- "$BUILDDIR" && \
        "$SOURCE_SNAPSHOT/configure" --disable-nls --enable-gawsound=no) \
        >"$WORKDIR/configure.log" 2>&1 || {
        echo "[FAIL] configure" >&2
        tail -n 80 "$WORKDIR/configure.log" >&2
        exit 1
    }
    echo "[ok] configure"

    (CDPATH='' cd -- "$BUILDDIR" && make -j2) >"$WORKDIR/make.log" 2>&1 || {
        echo "[FAIL] make -j2" >&2
        tail -n 80 "$WORKDIR/make.log" >&2
        exit 1
    }
    echo "[ok] make -j2"
    GAWM_BIN=$BUILDDIR/src/gawm
fi

PORTS=$(python3 - <<'PY'
import socket

ports = []
for _ in range(2):
    sock = socket.socket()
    sock.bind(("127.0.0.1", 0))
    ports.append(sock.getsockname()[1])
    sock.close()
print(*ports)
PY
) || exit 1
BROADWAY_PORT=${PORTS%% *}
GAWM_PORT=${PORTS#* }
BROADWAY_DISPLAY_NUM=$((100 + ($$ % 1000)))

mkdir "$WORKDIR/config" || exit 1
broadwayd --address 127.0.0.1 --port "$BROADWAY_PORT" \
    ":$BROADWAY_DISPLAY_NUM" >"$WORKDIR/broadway.log" 2>&1 &
BROADWAY_PID=$!
export GAWM_SMOKE_BROADWAY_PORT="$BROADWAY_PORT"
python3 - <<'PY' || exit 1
import os
import socket
import time

port = int(os.environ["GAWM_SMOKE_BROADWAY_PORT"])
for _ in range(100):
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=1):
            break
    except OSError:
        time.sleep(0.1)
else:
    raise SystemExit("Broadway display did not become ready")
PY

GDK_BACKEND=broadway BROADWAY_DISPLAY=":$BROADWAY_DISPLAY_NUM" \
    "$GAWM_BIN" -C "$WORKDIR/config" -p "$GAWM_PORT" \
    >"$WORKDIR/gawm.log" 2>&1 &
GAWM_PID=$!

FIXTURE=$SRCDIR/test/complex_smoke.raw
export GAWM_SMOKE_PORT="$GAWM_PORT"
export GAWM_SMOKE_FIXTURE="$FIXTURE"

python3 - <<'PY' || exit 1
import os
import socket
import time

port = int(os.environ["GAWM_SMOKE_PORT"])
fixture = os.environ["GAWM_SMOKE_FIXTURE"]

for _ in range(150):
    try:
        sock = socket.create_connection(("127.0.0.1", port), timeout=1)
        sock.settimeout(15)
        reader = sock.makefile("r", encoding="utf-8", newline="\n")
        break
    except OSError:
        time.sleep(0.1)
else:
    raise SystemExit("S1 FAIL: could not connect to gawio")

def send(line):
    sock.sendall((line + "\n").encode())

def expect_ack(check):
    reply = reader.readline()
    if reply != "\n":
        raise SystemExit(f"{check} FAIL: {reply.rstrip() or 'connection closed'}")

send(f"load {fixture}")
expect_ack("S1 load complex fixture")

send("table_list")
count_line = reader.readline()
try:
    count = int(count_line.strip())
except ValueError:
    raise SystemExit(f"S1 FAIL: invalid table count {count_line!r}")
tables = [reader.readline().strip() for _ in range(count)]
expect_ack("S1 table_list")
if "complex_smoke.raw" not in tables:
    raise SystemExit(f"S1 FAIL: fixture table absent: {tables!r}")
print("s[ok ] S1 complex fixture loaded")

send("copyvar V(out):mag p0")
expect_ack("S2 copyvar V(out):mag")
print("s[ok ] S2 gawio plotted V(out):mag")

reader.close()
sock.close()
PY

SCREENSHOT=$ARTIFACT_DIR/gawm-complex-smoke.png
OCR=$ARTIFACT_DIR/gawm-complex-smoke.txt
export GAWM_SMOKE_URL="http://127.0.0.1:$BROADWAY_PORT/"
export GAWM_SMOKE_SCREENSHOT="$SCREENSHOT"
export GAWM_SMOKE_CHROMIUM="${GAWM_SMOKE_CHROMIUM:-/usr/bin/chromium}"

PLAYWRIGHT_PACKAGE=playwright@${GAWM_SMOKE_PLAYWRIGHT:-1.62.1}
PLAYWRIGHT_BIN=$(npm exec --yes --package="$PLAYWRIGHT_PACKAGE" -- \
    sh -c 'command -v playwright') || exit 1
NODE_PATH=$(dirname "$(dirname "$PLAYWRIGHT_BIN")")
export NODE_PATH
node "$SRCDIR/test/test_complex_smoke_broadway.js" || exit 1

if [ ! -s "$SCREENSHOT" ]; then
    echo "S5 FAIL: screenshot is absent or empty" >&2
    exit 1
fi
tesseract "$SCREENSHOT" stdout 2>/dev/null >"$OCR" || exit 1

for expected in Real Magnitude Phase 5.000; do
    if ! grep -F "$expected" "$OCR" >/dev/null; then
        echo "S3/S4 FAIL: OCR did not find '$expected'" >&2
        echo "--- OCR output ---" >&2
        cat "$OCR" >&2
        exit 1
    fi
done
echo "s[ok ] S3 complex folder renders Real/Magnitude/Phase"
echo "s[ok ] S4 plotted magnitude autoscales to 5.000"

if ! kill -0 "$GAWM_PID" 2>/dev/null; then
    echo "S5 FAIL: gawm exited during smoke check" >&2
    exit 1
fi
echo "s[ok ] S5 gawm stayed alive; screenshot retained"
echo "PASS: complex GUI/gawio smoke S1..S5"
