# 객체 검출

입력 이미지 1장에 YOLO11s 객체 검출을 수행합니다. 표시된 결과 이미지는
`*_result.jpg`로 저장합니다.

처리 단계는 다음과 같습니다.

1. CPU는 이미지를 읽고 BGR data를 SP500 shared memory에 한 번 복사합니다.
2. DSP의 `sp500_letterbox`가 padding 값 114로 640 × 640 letterbox를 적용하고
   BGR을 RGB로 변환합니다. 4096-byte alignment의 shared input에 직접 씁니다.
   이 input은 `PinnedMemory<uint8_t>`로 연결합니다.
3. `inferPinnedMemory`가 DSP가 준비한 input으로 NPU를 실행합니다. NPU는 shared output에 직접 씁니다.
   DSP는 output copy 없이 이 memory를 읽습니다.
4. DSP는 결과를 decode합니다. Confidence filter와 NMS를 적용합니다.
5. CPU는 검출 결과를 그리고 결과 이미지를 저장합니다.

NPU input과 output은 DSP shared memory를 사용하므로 장치 사이의 추가 copy가 없습니다.
CPU가 읽은 BGR 이미지를 한 번 복사하므로 전체 pipeline은 zero-copy가 아닙니다.
원본 좌표는 DSP가 반환한 scale과 padding으로 복원합니다.

## 빌드 (HOST, 선택)

빌드에는 Mobilint SDK 2.0.0 toolchain v4.1.0 이상이 필요합니다. `build.sh`는
설치된 지원 버전 중 최신 버전을 선택합니다.

```bash
bash build.sh
```

다른 설치 버전을 선택하려면 해당 environment script를 지정합니다.

```bash
TOOLCHAIN_ENV="/opt/crosstools/mobilint/2.0.0/v<version>/environment-setup-cortexa53-mobilint-linux" bash build.sh
```

빌드 결과는 `./inference`입니다.

## 실행 (BOARD)

```bash
dsp_firmware_load
./inference ./yolo11s.mxq ./test-img_obj-det.png
```

결과 이미지는 입력 이미지와 같은 directory에 `*_result.jpg`로 저장됩니다.
