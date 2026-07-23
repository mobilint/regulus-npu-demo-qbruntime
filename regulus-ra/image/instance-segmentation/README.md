# Instance Segmentation

## Build (HOST)

Source the cross-toolchain on the host machine before building.

```bash
source /opt/crosstools/mobilint/1.0.0/v3.4.0/environment-setup-cortexa53-mobilint-linux
bash compile.sh
```

## Run (REGULUS)

Copy `inference` and `yolov8n-seg.mxq` to the REGULUS device, then run:

```bash
./inference ./yolov8n-seg.mxq ./test-img_seg.png
```
