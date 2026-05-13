# image-classification-resnet50

ResNet-50 image classification demo on Mobilint REGULUS NPU.

## Build (HOST)

Source the cross-toolchain on the host machine before building.

```bash
source /opt/crosstools/mobilint/1.0.0/v3.4.0/environment-setup-cortexa53-mobilint-linux
make
```

## Run (REGULUS)

Copy `resnet50`, `resnet50.mxq`, and `ILSVRC2012_val_00000001.JPEG` to the REGULUS device, then run:

### C++

```bash
./resnet50
```

Expected output:

```
label   : 65
predict : 65
```

### Python

```bash
python3 resnet50.py [path_to_mxq]
```

`path_to_mxq` is optional. Defaults to `resnet50.mxq` in the current directory.
