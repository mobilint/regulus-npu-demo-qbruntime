# REGULUS-RB Demos

REGULUS-RB board demos are grouped by input type. They require the qbruntime and
SP500 headers and libraries from the SDK. These SDK components are not bundled.

- [`camera/`](camera/) — real-time camera → NPU → HDMI demos
- [`image/`](image/) — still-image inference demos

## camera/

The HDMI camera demo uses fixed YOLO11s face detection, pose estimation, and
object detection models. It processes 480×320 input and renders 1920×1080 output.

- [`1-usb-camera_3models_hdmi-out`](camera/1-usb-camera_3models_hdmi-out/README.md)

## image/

Each demo uses a single `inference.cc` and `uint8` pinned NPU inference.
YOLO demos use DSP letterbox and postprocessing. Classification uses CPU resize,
center crop, and top-5 because the SDK has no public classification DSP API.
See each demo's README for build and execution steps.

- [`image-classification`](image/image-classification/README.md)
- [`object-detection`](image/object-detection/README.md)
- [`face-detection`](image/face-detection/README.md)
- [`instance-segmentation`](image/instance-segmentation/README.md)
- [`pose-estimation`](image/pose-estimation/README.md)
