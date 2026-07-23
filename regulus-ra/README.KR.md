# 비전 모델 샘플 코드 (SAMPLE CODES FOR VISION MODELS)

## 1. 빌드

각 task 디렉토리로 이동합니다:
```bash
$ cd object-detection
```
`compile.sh` 안의 환경 설정 경로를 확인하고 필요하면 수정합니다:
```bash
source /opt/crosstools/mobilint/1.0.0/v3.4.0/environment-setup-cortexa53-mobilint-linux
```
빌드:
```bash
$ ./compile.sh
```

빌드된 바이너리를 Regulus 보드로 전송한 뒤 실행합니다.

## 2. 추론

각 `compile.sh` 는 빌드된 `inference` 바이너리를 해당 task 디렉토리로 복사합니다.
그 바이너리를 모델(`.mxq`), 테스트 이미지와 함께 Regulus 보드로 전송한 뒤,
task 디렉토리에서 실행합니다.

```bash
$ ./inference ./yolov8n-seg.mxq ./test-img_seg.png
```

`argv[1]` : `.mxq` 모델 경로  
(주의 : 모델 파일 이름에는 `["yolo", "ssd"]` 중 하나와 `["face", "seg", "pose", ""]` 중 하나가 포함되어야 합니다.)

`argv[2]` : 입력 이미지 경로

전처리/후처리 파라미터(`conf_thres`, `iou_thres`, 이미지 크기 등)는 각 task의
`inference.cc` 에 정의되어 있습니다. 결과 이미지는 입력 이미지와 같은 위치에
`*_result.jpg` 로 저장됩니다.

이미지 분류의 경우 전처리(Resize → CenterCrop → Normalize)와 top-5 출력이
`inference.cc` 에 내장되어 있으며, 클래스 라벨은 작업 디렉토리의 `imagenet.txt`
에서 읽습니다.


## 3. 테스트된 모델

다음 모델들은 이 저장소의 **샘플 코드**로 테스트되었습니다.  
(참고: 이것이 Regulus 보드에서 지원하는 모델 전체 목록은 아닙니다.)

### 이미지 분류 (Image Classification)

| Model | Input Size <br> (H, W, C) |
|------------|------------|
| alexnet | (224, 224, 3) |
| densenet121 | (224, 224, 3) |
| densenet169 | (224, 224, 3) |
| densenet201 | (224, 224, 3) |
| googlenet | (224, 224, 3) |
| mnasnet1_0 | (224, 224, 3) |
| mnasnet1_3 | (224, 224, 3) |
| mobilenet_v2 | (224, 224, 3) |
| regnet_x_400mf | (224, 224, 3) |
| regnet_x_800mf | (224, 224, 3) |
| regnet_x_1_6gf | (224, 224, 3) |
| regnet_x_3_2gf | (224, 224, 3) |
| regnet_x_8gf | (224, 224, 3) |
| regnet_x_16gf | (224, 224, 3) |
| regnet_x_32gf | (224, 224, 3) |
| regnet_y_400mf | (224, 224, 3) |
| regnet_y_800mf | (224, 224, 3) |
| regnet_y_1_6gf | (224, 224, 3) |
| regnet_y_3_2gf | (224, 224, 3) |
| regnet_y_8gf | (224, 224, 3) |
| regnet_y_16gf | (224, 224, 3) |
| regnet_y_32gf | (224, 224, 3) |
| resnet18 | (224, 224, 3) |
| resnet34 | (224, 224, 3) |
| resnet50 | (224, 224, 3) |
| resnet101 | (224, 224, 3) |
| resnet152 | (224, 224, 3) |
| resnext50_32x4d | (224, 224, 3) |
| resnext101_32x8d | (224, 224, 3) |
| resnext101_64x4d | (224, 224, 3) |
| shufflenet_v2_x1_0 | (224, 224, 3) |
| shufflenet_v2_x1_5 | (224, 224, 3) |
| shufflenet_v2_x2_0 | (224, 224, 3) |
| vgg11 | (224, 224, 3) |
| vgg11_bn | (224, 224, 3) |
| vgg13 | (224, 224, 3) |
| vgg13_bn | (224, 224, 3) |
| vgg16 | (224, 224, 3) |
| vgg16_bn | (224, 224, 3) |
| vgg19 | (224, 224, 3) |
| vgg19_bn | (224, 224, 3) |
| wide_resnet50_2 | (224, 224, 3) |
| wide_resnet101_2 | (224, 224, 3) |

### 객체 검출 (Object Detection)

| Model | Input Size <br> (H, W, C) |
|------------|------------|
| ssd_mobilenet_v1 | (300, 300, 3) |
| yolov3 | (640, 640, 3) |
| yolov3u | (640, 640, 3) |
| yolov3-spp | (640, 640, 3) |
| yolov3-sppu | (640, 640, 3) |
| yolov3-tiny | (640, 640, 3) |
| yolov3-tinyu | (640, 640, 3) |
| yolov5n | (640, 640, 3) |
| yolov5s | (640, 640, 3) |
| yolov5m | (640, 640, 3) |
| yolov5l | (640, 640, 3) |
| yolov5x | (640, 640, 3) |
| yolov5nu | (640, 640, 3) |
| yolov5su | (640, 640, 3) |
| yolov5mu | (640, 640, 3) |
| yolov5lu | (640, 640, 3) |
| yolov5xu | (640, 640, 3) |
| yolov5n6 | (1280, 1280, 3) |
| yolov5s6 | (1280, 1280, 3) |
| yolov5m6 | (1280, 1280, 3) |
| yolov5l6 | (1280, 1280, 3) |
| yolov5x6 | (1280, 1280, 3) |
| yolov7 | (640, 640, 3) |
| yolov7x | (640, 640, 3) |
| yolov8n | (640, 640, 3) |
| yolov8s | (640, 640, 3) |
| yolov8m | (640, 640, 3) |
| yolov8l | (640, 640, 3) |
| yolov8x | (640, 640, 3) |
| yolov9t | (640, 640, 3) |
| yolov9s | (640, 640, 3) |
| yolov9m | (640, 640, 3) |
| yolov9c | (640, 640, 3) |

### 인스턴스 분할 (Instance Segmentation)

| Model | Input Size <br> (H, W, C) |
|------------|------------|
| yolov5n-seg | (640, 640, 3) |
| yolov5s-seg | (640, 640, 3) |
| yolov5m-seg | (640, 640, 3) |
| yolov5l-seg | (640, 640, 3) |
| yolov5x-seg | (640, 640, 3) |
| yolov8n-seg | (640, 640, 3) |
| yolov8s-seg | (640, 640, 3) |
| yolov8m-seg | (640, 640, 3) |
| yolov8l-seg | (640, 640, 3) |
| yolov8x-seg | (640, 640, 3) |
| yolov9c-seg | (640, 640, 3) |

### 자세 추정 (Pose Estimation)

| Model | Input Size <br> (H, W, C) |
|------------|------------|
| yolov8n-pose | (640, 640, 3) |
| yolov8s-pose | (640, 640, 3) |
| yolov8m-pose | (640, 640, 3) |
| yolov8l-pose | (640, 640, 3) |

### 얼굴 검출 (Face Detection)

| Model | Input Size <br> (H, W, C) |
|------------|------------|
| [yolov8n-face](https://github.com/derronqi/yolov8-face) | (640, 640, 3) |
