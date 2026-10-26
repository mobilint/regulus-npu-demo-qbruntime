# REGULUS NPU demo qbruntime

Mobilint REGULUS NPU용 데모 애플리케이션이며, `qbruntime`
(`#include <qbruntime/qbruntime.h>`)으로 빌드됩니다.

## 구성

- `regulus-ra/` — REGULUS-RA 보드 데모
  - `camera/` — 실시간 카메라 → NPU → HDMI 데모
  - `image/`  — 단일 이미지 추론 데모
- [`regulus-rb/`](regulus-rb/README.KR.md) — REGULUS-RB 보드 데모

빌드·실행 방법은 각 데모 디렉토리의 README 를 참고하세요.
