# Image Classification

1 input image → NPU ResNet-18 image classification (top-5) → annotated result image (`*_result.jpg`).

A prebuilt `inference` binary is already included in this repository, so you can
run it on the board directly. Rebuild only if you changed the source.

## Build (HOST, optional)

Activate the cross-toolchain on the host, then build.

```bash
# If LD_LIBRARY_PATH is set (e.g. inside the build container), unset it first.
unset LD_LIBRARY_PATH

# build.sh activates the cross-toolchain
# (/opt/crosstools/mobilint/1.0.0/v3.4.0/environment-setup-cortexa53-mobilint-linux)
# and runs cmake + make.
bash build.sh
```

This produces the `inference` binary.

## Run (REGULUS)

Copy the `inference` binary together with `resnet18_torchvision.mxq`, `imagenet.txt`, and a test image to the board, then run
it there.

```bash
./inference ./resnet18_torchvision.mxq ./test-image_cls.png
```

Preprocessing (Resize → CenterCrop → Normalize) and the top-5 output are
built into `inference.cc`; class labels are read from `imagenet.txt`. The
result is saved next to the input image as `*_result.jpg`.
