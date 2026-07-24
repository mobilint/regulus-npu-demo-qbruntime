# 1-mipi-camera-3-models-hdmi-out

1 MIPI camera → NPU 3 models (face detection / pose estimation / object
detection) → HDMI display (1920×1080).

## Build (HOST)

Activate the cross-toolchain on the host, then build.

```bash
# If LD_LIBRARY_PATH is set (e.g. inside the build container), unset it first.
unset LD_LIBRARY_PATH

# Activate the cross-toolchain.
source /opt/crosstools/mobilint/1.0.0/v3.4.0/environment-setup-cortexa53-mobilint-linux

# Build.
make -j$(nproc)
```

This produces the `demo` binary.

## Run (REGULUS)

Copy the `demo` binary together with the `mxq/` and `img/` directories to the
board, then run it there.

1. Find the MIPI camera device number:
   ```bash
   ./check.video.sh
   ```
2. Run the demo with that device number (e.g. 0):
   ```bash
   ./demo -d 0
   ```

The result is rendered to the HDMI display (1920×1080).
