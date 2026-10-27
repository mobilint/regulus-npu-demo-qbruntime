#!/bin/sh
set -eu

if ! command -v v4l2-ctl >/dev/null 2>&1; then
    echo "check_camera.sh: v4l2-ctl is not installed or is not in PATH" >&2
    exit 1
fi

set -- /dev/video*
if [ ! -e "$1" ]; then
    echo "check_camera.sh: no /dev/video* device was found" >&2
    exit 1
fi

found=0
for device do
    info=$(v4l2-ctl -d "$device" --all 2>/dev/null || true)
    printf '%s\n' "$info" | grep -qi "usb" || continue
    printf '%s\n' "$info" | grep -qi "Video Capture" || continue

    index=${device#/dev/video}
    formats=$(v4l2-ctl -d "$device" --list-formats-ext 2>/dev/null \
        | grep -oE "'[A-Z0-9]+'" | sort -u | tr '\n' ' ' || true)
    printf 'USB camera: %s\n' "$device"
    printf '  formats: %s\n' "${formats:-unknown}"
    printf '  run:     ./run.sh -d %s\n\n' "$index"
    found=1
done

if [ "$found" -eq 0 ]; then
    echo "check_camera.sh: no USB Video Capture device was found" >&2
    exit 1
fi
