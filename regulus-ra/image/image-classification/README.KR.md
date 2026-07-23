# 이미지 분류 (Image Classification)

## 빌드 (HOST)

빌드 전에 호스트 머신에서 크로스 툴체인을 소싱하세요.

```bash
source /opt/crosstools/mobilint/1.0.0/v3.4.0/environment-setup-cortexa53-mobilint-linux
bash compile.sh
```

## 실행 (REGULUS)

`inference`, `resnet18_torchvision.mxq`, `imagenet.txt`, 그리고 테스트 이미지를 REGULUS 장치로 복사한 뒤 실행하세요:

```bash
./inference ./resnet18_torchvision.mxq ./test-image_cls.png
```

전처리(Resize 256 → CenterCrop 224 → Normalize torch)와 top-5 분류 후처리는
`inference.cc` 안에 내장되어 있습니다. 클래스 라벨 목록은 작업 디렉토리의
`imagenet.txt` 에서 읽습니다.
