# Image Classification

One image → ResNet-18 with `uint8` pinned input → top-5 classification.
The result image is saved as `*_result.jpg`.
The complete flow is in `inference.cc`.

The CPU preserves the aspect ratio and resizes the short edge to 256 pixels.
It then center-crops to 224×224 and converts BGR to RGB.
The MXQ contains ImageNet normalization. The application does not normalize twice.
`inferPinnedMemory()` runs the NPU. The CPU applies softmax and selects top-5.
The SDK has no public classification/top-k DSP API, so this demo does not use DSP.
DSP letterbox is not a substitute for this model's resize/center-crop transform.

## Build (HOST, optional)

A prebuilt `inference` executable is included. To rebuild, use toolchain `v4.1.0+`.
From this demo directory:

```bash
bash build.sh
```

`build.sh` clears `LD_LIBRARY_PATH` and activates the latest installed supported toolchain.
To select a toolchain, replace `<version>` with its installed version:

```bash
TOOLCHAIN_ENV="/opt/crosstools/mobilint/2.0.0/v<version>/environment-setup-cortexa53-mobilint-linux" bash build.sh
```

## Run (BOARD)

Copy the complete demo directory to the board. Keep `imagenet.txt` next to the MXQ.
From the copied directory:

```bash
./inference ./resnet18_torchvision.mxq ./test-image_cls.png
```

The program prints top-5 and CPU/NPU latency. It saves `test-image_cls_result.jpg`.
