# 1-usb-camera-3-models-hdmi-out

USB 카메라 1대 -> NPU 모델 3개 -> HDMI 디스플레이 (1920 * 1080)

1. 데모 빌드
   ```
   make -j$(nproc)
   ```
2. USB 카메라 장치 이름 확인
   ```
   ./check.usbcamera.sh
   ```
3. 데모 실행
   ```
   1번 단계에서 /dev/video4 를 얻었다고 가정합니다
   ./demo -d 4
   ```
