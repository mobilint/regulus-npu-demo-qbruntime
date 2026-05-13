#!/bin/bash

rm -rf build
mkdir build
cd build
source /opt/crosstools/mobilint/1.0.0/v3.4.0/environment-setup-cortexa53-mobilint-linux
cmake ..
make -j$(nproc)
cd ..
cp build/inference ./inference
