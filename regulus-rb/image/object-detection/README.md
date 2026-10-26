# Object Detection

One image goes through YOLO11s object detection. The program saves the marked
image as `*_result.jpg`.

The processing stages are:

1. The CPU reads the image and copies its BGR bytes into SP500 shared memory.
2. `sp500_letterbox` applies a 640 × 640 letterbox with padding value 114 and
   converts BGR to RGB on the DSP. It writes directly into shared input memory
   wrapped as `PinnedMemory<uint8_t>` with 4096-byte alignment.
3. `inferPinnedMemory` runs the NPU. The NPU reads the DSP-prepared input and writes directly to shared
   outputs, and the DSP reads them without an output copy.
4. The DSP decodes results, applies the confidence filter, and applies NMS.
5. The CPU draws the detections and saves the result image.

The NPU input and outputs use DSP shared memory without inter-device copies.
The full pipeline is not zero-copy: the CPU copies the decoded BGR image once.
Source coordinates are restored with the scale and padding returned by the DSP.

## Build (HOST, optional)

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

```bash
dsp_firmware_load
./inference ./yolo11s.mxq ./test-img_obj-det.png
```

The result is saved next to the input image as `*_result.jpg`.
