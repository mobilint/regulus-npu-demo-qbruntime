# 1-mipi-camera-3-models-hdmi-out

1 MIPI camera -> NPU 3 models -> HDMI display (1920 * 1080)

1. Build demo
   ```
   make -j$(nproc)
   ```
2. Check usb camera device name
   ```
   ./check.video.sh
   ```
3. Run demo
   ```
   Use MIPI camera device number you get from 2.
   ./demo -d 4
   ```
