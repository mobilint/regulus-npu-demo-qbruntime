# 1-mipi-camera-3-models-hdmi-out

MIPI 카메라 1대 -> NPU 모델 3개 -> HDMI 디스플레이 (1920 * 1080)

1. 데모 빌드
   ```
   make -j$(nproc)
   ```
2. USB 카메라 장치 이름 확인
   ```
   ./check.video.sh
   ```
3. 데모 실행
   ```
   2번에서 확인한 MIPI 카메라 장치 번호를 사용하세요.
   ./demo -d 4
   ```
