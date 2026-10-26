# Face Detection

One image goes through YOLO11s face detection. The program saves the marked
image as `*_result.jpg`. This checkpoint has box outputs only. It does not have
face landmark outputs.

The processing stages are:

1. The CPU reads the image and copies its BGR bytes into SP500 shared memory.
2. `sp500_letterbox` applies a 640 × 640 letterbox with padding value 114. It
   converts BGR to RGB on the DSP. It writes directly into shared input memory
   wrapped as `PinnedMemory<uint8_t>` with 4096-byte alignment.
3. `inferPinnedMemory` runs the NPU. The NPU writes six float32 output tensors
   directly into 4096-byte-aligned shared memory.
4. The DSP decodes the anchorless one-class heads. It applies the confidence
   filter and NMS.
5. The CPU draws each box with the `face` label and saves the result image.

The output heads use 80 × 80, 40 × 40, and 20 × 20 HWC grids. Each grid has a
64-channel box tensor and a one-channel class tensor. The NPU input and outputs
use DSP shared memory without inter-device copies. The CPU copies the decoded
BGR image once. Source coordinates use the scale and padding from the DSP.

## Build (HOST, optional)

A prebuilt `inference` executable is included. Rebuild only when needed.
`build.sh` clears `LD_LIBRARY_PATH` and activates the selected toolchain.

The build requires Mobilint SDK 2.0.0 toolchain v4.1.0 or later. `build.sh`
selects the latest installed supported version.

```bash
bash build.sh
```

To select a different installed toolchain, use its environment script:

```bash
TOOLCHAIN_ENV="/opt/crosstools/mobilint/2.0.0/v<version>/environment-setup-cortexa53-mobilint-linux" bash build.sh
```

The build places the executable at `./inference`.

## Run (BOARD)

Copy the complete demo directory to the board and run from that directory.

```bash
dsp_firmware_load
./inference ./yolov11s-face.mxq ./test-img_face-det.png
```

The result is saved next to the input image as `*_result.jpg`.
