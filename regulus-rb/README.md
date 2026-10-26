# REGULUS-RB Demos

REGULUS-RB board demos are grouped by input type. They require the qbruntime and
SP500 headers and libraries from the SDK. These SDK components are not bundled.

- `camera/` — real-time camera → NPU → HDMI demos
- `image/` — no still-image demo is available

## camera/

The HDMI camera demo uses fixed YOLO11s face detection, pose estimation, and
object detection models. It processes 480×320 input and renders 1920×1080 output.

- [`1-usb-camera_3models_hdmi-out`](camera/1-usb-camera_3models_hdmi-out/README.md)

## image/

No still-image demo is available yet.
