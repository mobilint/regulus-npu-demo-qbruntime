# image-classification-resnet50

Mobilint REGULUS NPU에서 동작하는 ResNet-50 이미지 분류 데모입니다.

## 빌드 (HOST)

빌드 전에 호스트 머신에서 크로스 툴체인을 소싱하세요.

```bash
source /opt/crosstools/mobilint/1.0.0/v3.4.0/environment-setup-cortexa53-mobilint-linux
make
```

## 실행 (REGULUS)

`resnet50`, `resnet50.mxq`, `ILSVRC2012_val_00000001.JPEG` 를 REGULUS 장치로 복사한 뒤 실행하세요:

### C++

```bash
./resnet50
```

예상 출력:

```
label   : 65
predict : 65
```

### Python

```bash
python3 resnet50.py [path_to_mxq]
```

`path_to_mxq` 는 선택 사항입니다. 지정하지 않으면 현재 디렉토리의 `resnet50.mxq` 를 사용합니다.
