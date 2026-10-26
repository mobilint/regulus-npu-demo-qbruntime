# 1-usb-camera_3models_hdmi-out

1 USB camera → NPU 3 YOLO11s models (face detection / pose estimation / object
detection) → HDMI display (1920×1080).

## Build (HOST)

Activate the cross-toolchain, then build from this demo directory.
The SDK provides the qbruntime and SP500 headers and libraries.

```bash
unset LD_LIBRARY_PATH
source /opt/crosstools/mobilint/2.0.0/v4.1.0/environment-setup-cortexa53-mobilint-linux
make -j"$(nproc)"
```

This produces the `demo_usb_hdmi` binary.

## Run (REGULUS)

Copy the entire `1-usb-camera_3models_hdmi-out/` directory to the board.

Use these three fixed model files in `mxq/`:

- `yolov11s-face.mxq`
- `yolo11s-pose.mxq`
- `yolo11s.mxq`

Run these commands from the deployed demo directory:

1. Find the USB camera device number:
   ```bash
   ./check_camera.sh
   ```
2. Run the demo with that device number:
   ```bash
   ./run.sh -d <camera-index>
   ```

`run.sh` loads the DSP firmware before starting the demo.
The result is rendered to the HDMI display (1920×1080).
Press `Ctrl+C` to stop.
