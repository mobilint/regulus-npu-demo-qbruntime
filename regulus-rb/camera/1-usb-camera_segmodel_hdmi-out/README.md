# 1-usb-camera_segmodel_hdmi-out

## Build (HOST, optional)

The prebuilt `demo_usb_hdmi` binary is included. Rebuild only if needed.
Activate Mobilint SDK 2.0.0 cross-toolchain v4.1.0 or later, then run `make` from this demo directory.
The Makefile uses the qbruntime and SP500 headers and libraries from the SDK.

Replace `<toolchain-version>` with the installed toolchain version.

```bash
unset LD_LIBRARY_PATH
source /opt/crosstools/mobilint/2.0.0/<toolchain-version>/environment-setup-cortexa53-mobilint-linux
make -j"$(nproc)"
```

This produces the `demo_usb_hdmi` binary.

## Run (REGULUS)

Copy the entire `1-usb-camera_segmodel_hdmi-out/` directory to the board.
The demo uses the fixed `mxq/yolo11s-seg.mxq` model.
The board must provide `dsp_firmware_load`, `flock`, and `fuser`.

Open a root shell and run these commands from the deployed demo directory:

1. Find the USB camera device number:
   ```bash
   ./check_camera.sh
   ```
2. Run the demo with that device number:
   ```bash
   ./run.sh -d <camera-index>
   ```

The result is rendered to the HDMI display (1280×720).
Press `Ctrl+C` to stop.

Options:

- `-d N`: camera index from 0 to 63; default 5
- `--conf C`: confidence threshold, `0 < C < 1`; default 0.25
- `--iou I`: NMS IoU threshold, `0 < I < 1`; default 0.45
- `--mask M`: mask threshold, `0 < M < 1`; default 0.5
- `--warmup SEC`: time excluded after the first HDMI output; default 3 seconds
- `--duration SEC`: measurement time after warmup; 0 runs until a signal; default 0
- `model.mxq` or `-m model.mxq`: path to a compatible 640×480 raw-head segmentation model
- `-h`, `--help`: show help
