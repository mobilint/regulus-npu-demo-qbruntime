#!/bin/sh
set -eu

DEMO_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
BINARY="$DEMO_DIR/demo_usb_hdmi"
MODEL="$DEMO_DIR/mxq/yolo11s-seg.mxq"

if [ ! -x "$BINARY" ]; then
    echo "run.sh: executable is missing or is not executable: $BINARY" >&2
    exit 1
fi

unset LD_PRELOAD || true
# Let the application's parser distinguish help, valid arguments, and errors.
argument_status=0
(cd "$DEMO_DIR" && exec "$BINARY" --launcher-check "$@") || argument_status=$?
case "$argument_status" in
    64) ;;
    *) exit "$argument_status" ;;
esac

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
if [ ! -f "$MODEL" ]; then
    echo "run.sh: model is missing: $MODEL" >&2
    exit 1
fi
for tool in dsp_firmware_load flock fuser; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "run.sh: required command is not in PATH: $tool" >&2
        exit 1
    fi
done

# Keep this inode: unlinking it would let another launcher acquire a different lock.
# The application inherits descriptor 9 and keeps ownership if this shell is killed.
exec 9>>/run/regulus-sp500-demo.lock
if ! flock -n 9; then
    echo "run.sh: another SP500 launcher is active or the lock is unavailable" >&2
    exit 1
fi

for device in /dev/sp500_dsp /dev/sp500_link; do
    if [ ! -e "$device" ]; then
        echo "run.sh: required DSP device is missing: $device" >&2
        exit 1
    fi
done
# Also reject already-running applications that do not use the launcher lock.
if fuser -s /dev/sp500_dsp /dev/sp500_link; then
    echo "run.sh: SP500 is in use; stop the other DSP application first" >&2
    exit 1
else
    fuser_status=$?
    if [ "$fuser_status" -ne 1 ]; then
        echo "run.sh: could not check DSP device users" >&2
        exit 1
    fi
fi

demo_pid=
saved_governors=
starting_child=0
pending_signal=0
request_stop() {
    pending_signal=$1
    if [ "$starting_child" -eq 0 ]; then
        exit "$pending_signal"
    fi
}
cleanup() {
    run_status=$?
    trap - 0
    trap '' INT TERM HUP
    set +e
    if [ -n "$demo_pid" ]; then
        # TERM also works before the background child installs its INT handler.
        # The demo handles TERM gracefully, but has no HUP handler.
        kill -TERM "$demo_pid" 2>/dev/null || true
        # Preserve the wrapper's signal status, even if the main wait already reaped it.
        wait "$demo_pid" 2>/dev/null || true
    fi
    # Sysfs paths and governor names are kernel-defined single words.
    for saved in $saved_governors; do
        governor=${saved%=*}
        previous=${saved##*=}
        if [ "$(cat "$governor" 2>/dev/null)" = "$previous" ]; then
            continue
        fi
        if ! printf '%s\n' "$previous" > "$governor"; then
            echo "run.sh: failed to restore CPU governor: $governor" >&2
            [ "$run_status" -ne 0 ] || run_status=1
        fi
    done
    exit "$run_status"
}
trap cleanup 0
trap 'request_stop 130' INT
trap 'request_stop 143' TERM
trap 'request_stop 129' HUP

echo "[run.sh] load installed SP500 firmware"
dsp_firmware_load

# Snapshot every path before writing: several CPU paths can alias the same policy.
for governor in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    [ -e "$governor" ] || continue
    if ! previous=$(cat "$governor") || [ -z "$previous" ]; then
        echo "run.sh: could not read CPU governor: $governor" >&2
        exit 1
    fi
    saved_governors="$saved_governors $governor=$previous"
done
for saved in $saved_governors; do
    governor=${saved%=*}
    if ! printf '%s\n' performance > "$governor"; then
        echo "run.sh: could not set performance governor: $governor" >&2
    fi
done

cd "$DEMO_DIR"
starting_child=1
if [ "${RT:-1}" != 0 ] && command -v chrt >/dev/null 2>&1; then
    echo "[run.sh] start demo with FIFO priority 50"
    chrt -f 50 "$BINARY" "$MODEL" "$@" &
else
    echo "[run.sh] start demo with normal scheduling"
    "$BINARY" "$MODEL" "$@" &
fi
demo_pid=$!
starting_child=0
if [ "$pending_signal" -ne 0 ]; then
    exit "$pending_signal"
fi
child_status=0
wait "$demo_pid" || child_status=$?
demo_pid=
exit "$child_status"
