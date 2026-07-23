# 인스턴스 분할 (Instance Segmentation)

## 빌드 (HOST)

빌드 전에 호스트 머신에서 크로스 툴체인을 소싱하세요.

```bash
source /opt/crosstools/mobilint/1.0.0/v3.4.0/environment-setup-cortexa53-mobilint-linux
bash compile.sh
```

## 실행 (REGULUS)

`inference` 와 `yolov8n-seg.mxq` 를 REGULUS 장치로 복사한 뒤 실행하세요:

```bash
./inference ./yolov8n-seg.mxq ./test-img_seg.png
```
