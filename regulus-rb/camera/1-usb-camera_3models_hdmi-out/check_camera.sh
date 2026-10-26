#!/bin/sh
set -u

DEMO_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
found_node=0
found_camera=0

for v in /dev/video*; do
    [ -e "$v" ] || continue
    found_node=1
    info=$(v4l2-ctl -d "$v" --all 2>/dev/null || true)
    echo "$info" | grep -qi "usb" || continue
    echo "$info" | grep -qi "Video Capture" || continue

    n=${v#/dev/video}
    fmt=$(v4l2-ctl -d "$v" --list-formats-ext 2>/dev/null | grep -oE "'[A-Z0-9]+'" | sort -u | tr '\n' ' ' || true)
    echo "usb camera : $v"
    echo "  formats  : ${fmt:-(unknown)}"
    echo "  run      : $DEMO_DIR/run.sh -d $n"
    echo
    found_camera=1
done

[ "$found_node" -ne 0 ] || {
    echo "no /dev/video* found" >&2
    exit 1
}

[ "$found_camera" -ne 0 ] || echo "no USB capture camera found (check 'v4l2-ctl --list-devices')"
exit 0
