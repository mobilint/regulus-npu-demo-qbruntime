# 1-usb-camera_3models_hdmi-out

USB camera 1대 → NPU YOLO11s model 3개(얼굴 검출 / 자세 추정 / 객체 검출)
→ HDMI display(1920×1080).

## 빌드 (HOST)

> **필수 툴체인: `v4.1.0+` (v4.1.0 및 이후 버전)**

Cross-toolchain을 활성화한 뒤 이 demo directory에서 build합니다.
SDK에서 qbruntime과 SP500 header·library를 제공합니다.

`<toolchain-version>`을 설치된 툴체인 버전으로 바꾸세요.

```bash
unset LD_LIBRARY_PATH
source /opt/crosstools/mobilint/2.0.0/<toolchain-version>/environment-setup-cortexa53-mobilint-linux
make -j"$(nproc)"
```

`demo_usb_hdmi` binary가 생성됩니다.

## 실행 (REGULUS)

`1-usb-camera_3models_hdmi-out/` directory 전체를 board로 복사합니다.

`mxq/`에는 다음 고정 model 3개를 사용합니다.

- `yolov11s-face.mxq`
- `yolo11s-pose.mxq`
- `yolo11s.mxq`

배포한 demo directory에서 다음 명령을 실행합니다.

1. USB camera 장치 번호를 확인합니다.
   ```bash
   ./check_camera.sh
   ```
2. `<camera-index>`를 확인한 장치 번호로 바꾸어 실행합니다.
   ```bash
   ./run.sh -d <camera-index>
   ```

`run.sh`는 demo 실행 전에 DSP firmware를 load합니다.
결과는 HDMI display(1920×1080)로 출력됩니다.
종료하려면 `Ctrl+C`를 누릅니다.
