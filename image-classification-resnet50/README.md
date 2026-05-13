# image-classification-resnet50

ResNet-50 image classification demo on Mobilint REGULUS NPU.

## Prerequisites

- Cross-toolchain sourced: `source /opt/crosstools/mobilint/1.0.0/v3.4.0/environment-setup-cortexa53-mobilint-linux`
- `resnet50.mxq` present in this directory
- Test image `ILSVRC2012_val_00000001.JPEG` present in this directory

## C++ demo

### Build

```
make
```

### Run

```
./resnet50
```

Expected output:

```
label   : 65
predict : 65
```

## Python demo

```
python3 resnet50.py [path_to_mxq]
```

`path_to_mxq` is optional. Defaults to `resnet50.mxq` in the current directory.
