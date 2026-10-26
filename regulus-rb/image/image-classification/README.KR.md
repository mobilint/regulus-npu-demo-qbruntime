# 이미지 분류

입력 이미지 1장 → ResNet-18 `uint8` pinned 추론 → top-5 분류.
결과 이미지는 `*_result.jpg`로 저장합니다.
전체 흐름은 `inference.cc`에 있습니다.

CPU가 종횡비를 유지하면서 짧은 변을 256픽셀로 조정합니다.
그다음 중앙 224×224 영역을 자르고 BGR→RGB 변환을 수행합니다.
MXQ에 ImageNet normalization이 반영되어 있으므로 중복 적용하지 않습니다.
NPU는 `inferPinnedMemory()`로 실행합니다. Softmax와 top-5 선택은 CPU가 수행합니다.
SDK에 공개 classification/top-k DSP API가 없으므로 이 데모는 DSP를 사용하지 않습니다.
DSP letterbox로 이 model의 resize/center-crop을 대체하지 않습니다.

## 빌드 (HOST, 선택)

빌드된 `inference` 실행파일이 포함되어 있습니다. 재빌드에는 `v4.1.0+` 툴체인을 사용합니다.
이 데모 디렉토리에서 실행합니다.

```bash
bash build.sh
```

`build.sh`는 `LD_LIBRARY_PATH`를 해제하고 설치된 지원 툴체인 중 최신 버전을 활성화합니다.
툴체인을 지정하려면 `<version>`을 설치된 버전으로 바꿉니다.

```bash
TOOLCHAIN_ENV="/opt/crosstools/mobilint/2.0.0/v<version>/environment-setup-cortexa53-mobilint-linux" bash build.sh
```

## 실행 (BOARD)

데모 디렉토리 전체를 보드로 복사합니다. `imagenet.txt`는 MXQ와 같은 디렉토리에 둡니다.
복사한 디렉토리에서 실행합니다.

```bash
./inference ./resnet18_torchvision.mxq ./test-image_cls.png
```

Top-5와 CPU/NPU latency를 출력하고 `test-image_cls_result.jpg`를 저장합니다.
