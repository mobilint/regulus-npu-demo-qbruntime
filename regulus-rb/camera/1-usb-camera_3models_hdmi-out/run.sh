#!/bin/sh
# Load the installed SP500 firmware and start the HDMI demo.
set -eu

DEMO_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
BINARY_NAME=demo_usb_hdmi
BINARY="$DEMO_DIR/$BINARY_NAME"

require_file() {
    if [ ! -f "$1" ]; then
        echo "run.sh: required file is missing: $1" >&2
        exit 1
    fi
}

require_executable() {
    require_file "$1"
    if [ ! -x "$1" ]; then
        echo "run.sh: file is not executable: $1" >&2
        exit 1
    fi
}

wait_for_demo_exit() {
    remaining=$1
    while [ "$remaining" -gt 0 ]; do
        if ! pgrep -x "$BINARY_NAME" >/dev/null 2>&1; then
            return 0
        fi
        sleep 1
        remaining=$((remaining - 1))
    done
    ! pgrep -x "$BINARY_NAME" >/dev/null 2>&1
}

stop_existing_demo() {
    if ! pgrep -x "$BINARY_NAME" >/dev/null 2>&1; then
        return
    fi

    echo "[run.sh] stop existing $BINARY_NAME process"
    pkill -TERM -x "$BINARY_NAME" 2>/dev/null || true
    if ! wait_for_demo_exit 5; then
        echo "[run.sh] $BINARY_NAME did not stop; send SIGKILL" >&2
        pkill -KILL -x "$BINARY_NAME" 2>/dev/null || true
        if ! wait_for_demo_exit 2; then
            echo "[run.sh] cannot stop $BINARY_NAME; firmware reload is blocked" >&2
            exit 1
        fi
    fi
}

require_executable "$BINARY"
command -v dsp_firmware_load >/dev/null 2>&1 || {
    echo "run.sh: dsp_firmware_load is not installed or is not in PATH" >&2
    exit 1
}
require_file "$DEMO_DIR/mxq/yolov11s-face.mxq"
require_file "$DEMO_DIR/mxq/yolo11s-pose.mxq"
require_file "$DEMO_DIR/mxq/yolo11s.mxq"
require_file "$DEMO_DIR/img/[Regulus] Smart Camera_Demo_260804.jpg"

unset LD_PRELOAD || true

if [ "$(id -u)" -ne 0 ]; then
    echo "run.sh: root access is required" >&2
    exit 1
fi

stop_existing_demo

echo "[run.sh] load installed SP500 firmware"
dsp_firmware_load
echo "[run.sh] SP500 firmware is ready"

# Pin all cores to performance (reset each boot); keeps frame time steady.
echo "[run.sh] CPU governor -> performance"
for g in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    echo performance > "$g" 2>/dev/null || true
done

# Real-time FIFO for every pipeline thread (they inherit it). On this HZ=100 kernel a CFS thread
# woken by a queue push can wait milliseconds for a core even with cores idle. No thread spins
# (the report loop sleeps), so FIFO cannot starve the system. RT=0 turns it off.
RT_PREFIX=""
if [ "${RT:-1}" != 0 ] && command -v chrt >/dev/null 2>&1; then
    RT_PREFIX="chrt -f 50"
fi

cd "$DEMO_DIR"
echo "[run.sh] start $RT_PREFIX $BINARY_NAME"
exec $RT_PREFIX "$BINARY" "$@"
