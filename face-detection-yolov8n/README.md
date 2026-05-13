# face-detection-yolov8n

YOLOv8n face detection demo on Mobilint REGULUS NPU.

## Build (HOST)

Source the cross-toolchain on the host machine before building.

```bash
source /opt/crosstools/mobilint/1.0.0/v3.4.0/environment-setup-cortexa53-mobilint-linux
make
```

## Run (REGULUS)

Copy the `demo` binary and `mxq/` directory to the REGULUS device, then run:

```bash
./demo -d <video_device_number>
```

Example (USB camera at /dev/video4):

```bash
./demo -d 4
```

Check the available USB camera device:

```bash
./check.usbcamera.sh
```
