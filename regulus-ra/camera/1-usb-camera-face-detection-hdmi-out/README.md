# 1-usb-camera-face-detection-hdmi-out

1 USB camera → NPU YOLOv8n face detection → HDMI display (1920×1080).

A prebuilt `demo` binary is already included in this repository, so you can run it on the board directly. Rebuild only if you changed the source.

## Build (HOST, optional)

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

Copy the `demo` binary together with `face_yolov8n_640_512.mxq`, `config.yaml`,
and `check.usbcamera.sh` to the board, then run it there. `config.yaml` sets the
camera and HDMI display resolution.

1. Find the USB camera device number:
   ```bash
   ./check.usbcamera.sh
   ```
2. Run the demo with that device number (e.g. 6):
   ```bash
   ./demo -d 6
   ```

The result is rendered to the HDMI display (1920×1080), with the display frame
rate printed once per second.
