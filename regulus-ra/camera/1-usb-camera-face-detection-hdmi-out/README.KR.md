# 1-usb-camera-face-detection-hdmi-out

Mobilint REGULUS NPU에서 동작하는 YOLOv8n 얼굴 검출 데모입니다.

## 빌드 (HOST)

빌드 전에 호스트 머신에서 크로스 툴체인을 소싱하세요.

```bash
source /opt/crosstools/mobilint/1.0.0/v3.4.0/environment-setup-cortexa53-mobilint-linux
make
```

## 실행 (REGULUS)

`demo` 바이너리와 `mxq/` 디렉토리를 REGULUS 장치로 복사한 뒤 실행하세요:

```bash
./demo -d <video_device_number>
```

예시 (USB 카메라가 /dev/video4 인 경우):

```bash
./demo -d 4
```

사용 가능한 USB 카메라 장치 확인:

```bash
./check.usbcamera.sh
```
