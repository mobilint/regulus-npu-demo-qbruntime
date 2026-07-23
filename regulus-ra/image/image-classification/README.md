# Image Classification

## Build (HOST)

Source the cross-toolchain on the host machine before building.

```bash
source /opt/crosstools/mobilint/1.0.0/v3.4.0/environment-setup-cortexa53-mobilint-linux
bash compile.sh
```

## Run (REGULUS)

Copy `inference`, `resnet18_torchvision.mxq`, `imagenet.txt`, and the test image to the REGULUS device, then run:

```bash
./inference ./resnet18_torchvision.mxq ./test-image_cls.png
```

Preprocessing (Resize 256 → CenterCrop 224 → Normalize torch) and the top-5
classification postprocessing are built into `inference.cc`. The label list is
read from `imagenet.txt` in the working directory.
