# 인스턴스 분할

입력 이미지 1장에 YOLO11s 인스턴스 분할을 수행합니다. 표시된 결과 이미지는
`*_result.jpg`로 저장합니다.

처리 단계는 다음과 같습니다.

1. CPU는 이미지를 읽고 BGR 데이터를 SP500 공유 메모리에 한 번 복사합니다.
2. DSP의 `sp500_letterbox`가 패딩 값 114로 640 × 640 레터박스를 적용합니다.
   BGR을 RGB로 변환합니다. 4096바이트 정렬의 공유 입력 메모리에 직접 씁니다.
   이 입력은 `PinnedMemory<uint8_t>`로 연결합니다.
3. `inferPinnedMemory`가 NPU를 실행합니다. NPU는 원시 출력 10개를 4096바이트
   정렬의 공유 메모리에 직접 씁니다.
4. DSP는 박스와 마스크를 디코딩합니다. 신뢰도 필터와 NMS를 적용합니다.
5. CPU는 DSP 이진 마스크를 원본 이미지 좌표로 변환합니다. 결과를 그리고
   결과 이미지를 저장합니다.

NPU 입력과 출력은 DSP 공유 메모리를 사용합니다. 따라서 장치 사이에 추가
복사가 없습니다. CPU가 읽은 BGR 이미지를 한 번 복사하므로 전체 파이프라인은
제로 카피가 아닙니다. 프로그램은 DSP가 반환한 레터박스 형상을 사용합니다.

## 빌드 (HOST, 선택)

빌드된 `inference` 실행파일이 포함되어 있습니다. 필요한 경우에만 재빌드합니다.
`build.sh`가 `LD_LIBRARY_PATH`를 해제하고 선택한 툴체인을 활성화합니다.

빌드에는 Mobilint SDK 2.0.0 툴체인 v4.1.0 이상이 필요합니다. `build.sh`는
설치된 지원 버전 중 최신 버전을 선택합니다.

```bash
bash build.sh
```

다른 설치 버전을 선택하려면 해당 환경 스크립트를 지정합니다.

```bash
TOOLCHAIN_ENV="/opt/crosstools/mobilint/2.0.0/v<version>/environment-setup-cortexa53-mobilint-linux" bash build.sh
```

빌드 결과는 `./inference`입니다.

## 실행 (BOARD)

데모 디렉토리 전체를 보드로 복사하고 해당 디렉토리에서 실행합니다.
최대 32개 instance를 반환하며 packed mask에 12.5 MiB를 할당합니다.

```bash
dsp_firmware_load
./inference ./yolo11s-seg.mxq ./test-img_seg.png
```

결과 이미지는 입력 이미지와 같은 디렉터리에 `*_result.jpg`로 저장됩니다.
