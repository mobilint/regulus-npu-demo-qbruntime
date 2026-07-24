# REGULUS-RA 데모

REGULUS-RA 보드용 데모이며, 실행 방식에 따라 두 그룹으로 나뉩니다. 각 데모는 빌드·실행
방법을 담은 자체 README 가 있으니 자세한 내용은 해당 README 를 참고하세요.

- `camera/` — 실시간 카메라 → NPU → HDMI 데모
- `image/`  — 단일 이미지 추론 데모

## camera/

실시간 카메라 → NPU → HDMI 출력 데모입니다. `make` 로 빌드하고
`./demo -d <video_device_number>` 로 실행합니다.

3모델 데모 — 카메라 1대에서 NPU 모델 3개(얼굴 검출 / 자세 추정 / 객체 검출)를 동시에
실행, HDMI 출력(1920×1080):

- `1-usb-camera-3-models-hdmi-out` — USB 카메라 입력
- `1-mipi-camera-3-models-hdmi-out` — MIPI 카메라 입력

단일 모델 데모 — 카메라 1대, YOLOv8n 얼굴 검출, HDMI 출력:

- `1-usb-camera-face-detection-hdmi-out` — USB 카메라 입력

자세한 내용은 각 데모의 README 를 참고하세요.

## image/

단일 이미지 추론 데모입니다 — 입력 이미지 1장을 추론하고 결과 이미지(`*_result.jpg`)를
저장합니다. `compile.sh` 로 빌드하고 `./inference <mxq> <image>` 로 실행합니다.

- `object-detection`
- `instance-segmentation`
- `pose-estimation`
- `face-detection`
- `image-classification`

자세한 내용은 각 데모의 README 를 참고하세요.
