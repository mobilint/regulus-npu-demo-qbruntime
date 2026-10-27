# 1-usb-camera_segmodel_hdmi-out

## 빌드 (HOST)

> **필수 툴체인: `v4.1.0+` (v4.1.0 및 이후 버전)**

미리 빌드한 `demo_usb_hdmi` binary가 포함되어 있습니다. 필요한 경우에만 다시 빌드합니다.
Mobilint SDK 2.0.0 cross-toolchain을 활성화합니다.
이 demo directory에서 `make`를 실행합니다.
Makefile은 SDK가 제공하는 qbruntime과 SP500 header 및 library를 사용합니다.

`<toolchain-version>`을 설치한 toolchain 버전으로 바꾸세요.

```bash
unset LD_LIBRARY_PATH
source /opt/crosstools/mobilint/2.0.0/<toolchain-version>/environment-setup-cortexa53-mobilint-linux
make -j"$(nproc)"
```

`demo_usb_hdmi` binary가 생성됩니다.

## 실행 (REGULUS)

`1-usb-camera_segmodel_hdmi-out/` directory 전체를 board로 복사합니다.
이 demo는 고정된 `mxq/yolo11s-seg.mxq` model을 사용합니다.
Board에는 `dsp_firmware_load`, `flock`, `fuser` command가 필요합니다.

Root shell을 엽니다.
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
결과는 HDMI display(1280×720)로 출력됩니다.
종료하려면 `Ctrl+C`를 누릅니다.

옵션:

- `-d N`: camera index 0~63, 기본값 5
- `--conf C`: confidence threshold, `0 < C < 1`, 기본값 0.25
- `--iou I`: NMS IoU threshold, `0 < I < 1`, 기본값 0.45
- `--mask M`: mask threshold, `0 < M < 1`, 기본값 0.5
- `--warmup SEC`: 첫 HDMI output 후 제외 시간, 기본값 3초
- `--duration SEC`: warmup 후 측정 시간, 기본값 0은 signal 수신까지 실행
- `model.mxq` 또는 `-m model.mxq`: 호환되는 640×480 raw-head segmentation model 경로
- `-h`, `--help`: help 출력
