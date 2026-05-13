# Instance Segmentation

## Build

```bash
cmake -S . -B build
cmake --build build -j8
```

## Run

```bash
./build/inference ./yolov8n-seg.mxq ./image.jpg
```
