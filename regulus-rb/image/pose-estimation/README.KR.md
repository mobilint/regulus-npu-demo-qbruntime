# 자세 추정

입력 이미지 1장에 YOLO11s 자세 추정을 수행합니다. 표시된 결과 이미지는
`*_result.jpg`로 저장합니다.

DSP는 padding 값 114로 640 × 640 letterbox를 적용하고 BGR을 RGB로 변환합니다.
NPU는 shared pinned input과 output memory를 사용합니다. DSP는 자세 결과를
decode하고 NMS를 적용합니다. CPU는 box, COCO skeleton, confidence가 0.5 이상인
keypoint를 그립니다.

## 빌드 (HOST, 선택)

빌드된 `inference` 실행파일이 포함되어 있습니다. 필요한 경우에만 재빌드합니다.
`build.sh`가 `LD_LIBRARY_PATH`를 해제하고 선택한 툴체인을 활성화합니다.

빌드에는 Mobilint SDK 2.0.0 toolchain v4.1.0 이상이 필요합니다. `build.sh`는
설치된 지원 버전 중 최신 버전을 선택합니다.

```bash
bash build.sh
```

빌드 결과는 `./inference`입니다.

## 실행 (BOARD)

데모 디렉토리 전체를 보드로 복사하고 해당 디렉토리에서 실행합니다.

```bash
dsp_firmware_load
./inference ./yolo11s-pose.mxq ./test-img_pose-est.png
```

결과 이미지는 입력 이미지와 같은 directory에 `*_result.jpg`로 저장됩니다.
