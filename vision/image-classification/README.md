# Image Classification

## Build (HOST)

Source the cross-toolchain on the host machine before building.

```bash
source /opt/crosstools/mobilint/1.0.0/v3.4.0/environment-setup-cortexa53-mobilint-linux
bash compile.sh
```

## Run (REGULUS)

Copy `inference_yaml`, `resnet18_torchvision.mxq`, `imagenet.txt`, and a config yaml to the REGULUS device, then run:

```bash
./inference_yaml ./resnet18_torchvision.mxq ./config.yaml ./test-image_cls.png
```
