# 자세 추정 (Pose Estimation)

입력 이미지 1장 → NPU YOLOv8 자세 추정 → 결과 이미지(`*_result.jpg`) 저장.

이 저장소에는 이미 컴파일된 `inference` 바이너리가 포함되어 있어 보드에서 바로
실행할 수 있습니다. 소스를 수정한 경우에만 아래 절차로 다시 빌드하세요.

## 빌드 (HOST, 선택)

호스트에서 크로스 툴체인을 활성화한 뒤 빌드합니다.

```bash
# LD_LIBRARY_PATH 가 설정돼 있으면(예: 빌드 컨테이너 내부) 먼저 해제합니다.
unset LD_LIBRARY_PATH

# build.sh 가 크로스 툴체인
# (/opt/crosstools/mobilint/1.0.0/v3.4.0/environment-setup-cortexa53-mobilint-linux)
# 을 활성화하고 cmake + make 를 실행합니다.
bash build.sh
```

`inference` 바이너리가 생성됩니다.

## 실행 (REGULUS)

`inference` 바이너리를 `yolov8n-pose.mxq`, 테스트 이미지와 함께 보드로 복사한 뒤 보드에서 실행합니다.

```bash
./inference ./yolov8n-pose.mxq ./test-img_pose-est.png
```

결과 이미지는 입력 이미지 옆에 `*_result.jpg` 로 저장됩니다.
