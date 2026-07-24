#!/bin/bash

TOOLCHAIN=/opt/crosstools/mobilint/1.0.0/v3.4.0/environment-setup-cortexa53-mobilint-linux

# 1) If the cross-toolchain is not installed at the expected path, guide the
#    user to install it and stop.
if [ ! -f "$TOOLCHAIN" ]; then
    echo "ERROR: cross-toolchain not found at:" >&2
    echo "         $TOOLCHAIN" >&2
    echo "" >&2
    echo "Install the Mobilint REGULUS cross-toolchain (SDK) so that the" >&2
    echo "environment-setup script exists at the path above, then re-run this script." >&2
    exit 1
fi

# 2) The toolchain is installed. The Yocto SDK environment-setup aborts when
#    LD_LIBRARY_PATH is set, so clear it automatically before activating.
unset LD_LIBRARY_PATH
source "$TOOLCHAIN"

set -e

rm -rf build
mkdir build
cd build
cmake ..
make -j$(nproc)
cd ..
cp build/inference ./inference
