# REGULUS-RB 데모

REGULUS-RB 보드용 데모를 input 유형에 따라 구분합니다. SDK가 제공하는 qbruntime과
SP500 header 및 library가 필요합니다. 이 SDK 구성 요소는 포함되지 않습니다.

- `camera/` — 실시간 camera → NPU → HDMI 데모
- `image/` — 단일 image 추론 데모

## camera/

HDMI camera 데모는 고정된 YOLO11s 얼굴 검출, 자세 추정, 객체 검출 model을
사용합니다. 480×320 input을 처리하고 1920×1080 output을 HDMI로 출력합니다.

- [`1-usb-camera_3models_hdmi-out`](camera/1-usb-camera_3models_hdmi-out/README.KR.md)

## image/

각 데모는 단일 `inference.cc`와 `uint8` pinned NPU 추론을 사용합니다.
YOLO 데모는 DSP letterbox와 postprocessing을 사용합니다. 분류는 공개 DSP API가
없으므로 CPU resize, center crop, top-5를 사용합니다.
빌드·실행 방법은 각 데모의 README를 참고하세요.

- [이미지 분류](image/image-classification/README.KR.md)
- [객체 검출](image/object-detection/README.KR.md)
- [얼굴 검출](image/face-detection/README.KR.md)
- [인스턴스 분할](image/instance-segmentation/README.KR.md)
- [자세 추정](image/pose-estimation/README.KR.md)
