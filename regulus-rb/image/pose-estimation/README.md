# Pose Estimation

One image goes through YOLO11s pose estimation. The program saves the marked
image as `*_result.jpg`.

The DSP applies a 640 × 640 letterbox with padding value 114 and converts BGR
to RGB. The NPU uses shared pinned input and output memory. The DSP decodes the
pose records and applies NMS. The CPU draws the boxes, COCO skeletons, and
keypoints with confidence of 0.5 or more.

## Build (HOST, optional)

A prebuilt `inference` executable is included. Rebuild only when needed.
`build.sh` clears `LD_LIBRARY_PATH` and activates the selected toolchain.

The build requires Mobilint SDK 2.0.0 toolchain v4.1.0 or later. `build.sh`
selects the latest installed supported version.

```bash
bash build.sh
```

The build places the executable at `./inference`.

## Run (BOARD)

Copy the complete demo directory to the board and run from that directory.

```bash
dsp_firmware_load
./inference ./yolo11s-pose.mxq ./test-img_pose-est.png
```

The result is saved next to the input image as `*_result.jpg`.
