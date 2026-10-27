#!/bin/sh
set -eu

DEMO_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
BINARY="$DEMO_DIR/demo_usb_hdmi"
MODEL="$DEMO_DIR/mxq/yolo11s-seg.mxq"

if [ "$#" -gt 0 ]; then
    case "$1" in
        -*) ;;
        *) MODEL=$1; shift ;;
    esac
fi

case "$MODEL" in
    /*) ;;
    *) MODEL="$DEMO_DIR/$MODEL" ;;
esac

if [ "$(id -u)" -ne 0 ]; then
    echo "run.sh: root access is required" >&2
    exit 1
fi
if [ ! -x "$BINARY" ]; then
    echo "run.sh: executable is missing or is not executable: $BINARY" >&2
    exit 1
fi
if [ ! -f "$MODEL" ]; then
    echo "run.sh: model is missing: $MODEL" >&2
    exit 1
fi
if ! command -v dsp_firmware_load >/dev/null 2>&1; then
    echo "run.sh: dsp_firmware_load is not installed or is not in PATH" >&2
    exit 1
fi

unset LD_PRELOAD || true
echo "[run.sh] load installed SP500 firmware"
dsp_firmware_load

for governor in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    [ -e "$governor" ] || continue
    (echo performance > "$governor") 2>/dev/null || true
done

cd "$DEMO_DIR"
if [ "${RT:-1}" != 0 ] && command -v chrt >/dev/null 2>&1; then
    echo "[run.sh] start demo with FIFO priority 50"
    exec chrt -f 50 "$BINARY" "$MODEL" "$@"
fi

echo "[run.sh] start demo with normal scheduling"
exec "$BINARY" "$MODEL" "$@"
