# 1-usb-camera-face-detection-hdmi-out

USB 카메라 1대 → NPU YOLOv8n 얼굴 검출 → HDMI 디스플레이(1920×1080).

이 저장소에는 이미 컴파일된 `demo` 바이너리가 포함되어 있어 보드에서 바로 실행할 수 있습니다. 소스를 수정한 경우에만 아래 절차로 다시 빌드하세요.

## 빌드 (HOST, 선택)

호스트에서 크로스 툴체인을 활성화한 뒤 빌드합니다.

```bash
# LD_LIBRARY_PATH 가 설정돼 있으면(예: 빌드 컨테이너 내부) 먼저 해제합니다.
unset LD_LIBRARY_PATH

# 크로스 툴체인 활성화.
source /opt/crosstools/mobilint/1.0.0/v3.4.0/environment-setup-cortexa53-mobilint-linux

# 빌드.
make -j$(nproc)
```

`demo` 바이너리가 생성됩니다.

## 실행 (REGULUS)

`demo` 바이너리를 `face_yolov8n_640_512.mxq`, `config.yaml`, `check.usbcamera.sh`
와 함께 보드로 복사한 뒤 보드에서 실행합니다. `config.yaml` 이 카메라와 HDMI
디스플레이 해상도를 설정합니다.

1. USB 카메라 장치 번호 확인:
   ```bash
   ./check.usbcamera.sh
   ```
2. 확인한 장치 번호로 실행 (예: 6):
   ```bash
   ./demo -d 6
   ```

결과는 HDMI 디스플레이(1920×1080)로 출력되며, 디스플레이 프레임 레이트가 초당
한 번 출력됩니다.
