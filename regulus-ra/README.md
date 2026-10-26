# REGULUS-RA Demos

Demos for the REGULUS-RA board, grouped by how they run. Each demo has its own
README with build and run instructions — see it for details.

- [`camera/`](camera/) — real-time camera → NPU → HDMI demos
- [`image/`](image/) — still-image inference demos

## camera/

Real-time camera → NPU → HDMI display demos. Build with `make`, then run
`./demo -d <video_device_number>`.

Three-model demos — one camera, 3 NPU models (face detection / pose estimation /
object detection) at once, HDMI output (1920×1080):

- [`1-usb-camera-3-models-hdmi-out`](camera/1-usb-camera-3-models-hdmi-out/README.md) — USB camera input
- [`1-mipi-camera-3-models-hdmi-out`](camera/1-mipi-camera-3-models-hdmi-out/README.md) — MIPI camera input

Single-model demo — one camera, YOLOv8n face detection, HDMI output:

- [`1-usb-camera-face-detection-hdmi-out`](camera/1-usb-camera-face-detection-hdmi-out/README.md) — USB camera input

See each demo's own README for details.

## image/

Still-image inference demos — take one input image, run inference, and save the
annotated result (`*_result.jpg`). Build with `compile.sh`, then run
`./inference <mxq> <image>`.

- [`image-classification`](image/image-classification/README.md)
- [`object-detection`](image/object-detection/README.md)
- [`face-detection`](image/face-detection/README.md)
- [`instance-segmentation`](image/instance-segmentation/README.md)
- [`pose-estimation`](image/pose-estimation/README.md)

See each demo's own README for details.
