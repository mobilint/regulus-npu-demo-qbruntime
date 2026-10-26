# Instance Segmentation

One image goes through YOLO11s instance segmentation. The program saves the
marked image as `*_result.jpg`.

The processing stages are:

1. The CPU reads the image and copies its BGR bytes into SP500 shared memory.
2. `sp500_letterbox` applies a 640 × 640 letterbox with padding value 114 and
   converts BGR to RGB on the DSP. It writes directly into shared input memory
   wrapped as `PinnedMemory<uint8_t>` with 4096-byte alignment.
3. `inferPinnedMemory` runs the NPU. The NPU writes the ten raw outputs directly
   to 4096-byte-aligned shared memory.
4. The DSP decodes boxes and masks. It applies the confidence filter and NMS.
5. The CPU maps the DSP binary masks to the source image, draws the results, and
   saves the result image.

The NPU input and outputs use DSP shared memory without inter-device copies.
The full pipeline is not zero-copy because the CPU copies the decoded BGR image
once. The program uses the letterbox geometry returned by the DSP.

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
The demo returns up to 32 instances and reserves 12.5 MiB for packed masks.

```bash
dsp_firmware_load
./inference ./yolo11s-seg.mxq ./test-img_seg.png
```

The result is saved next to the input image as `*_result.jpg`.
