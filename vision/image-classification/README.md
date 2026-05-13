# Image Classification

## Build

```bash
cmake -S . -B build
cmake --build build -j8
```

## Run

```bash
./build/inference_yaml ./resnet18_torchvision.mxq ./config.yaml ./image.jpg
```
