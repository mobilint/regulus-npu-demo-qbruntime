#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
TOOLCHAIN_ROOT="/opt/crosstools/mobilint/2.0.0"
TOOLCHAIN_SCRIPT="environment-setup-cortexa53-mobilint-linux"
MIN_TOOLCHAIN_VERSION="4.1.0"

version_is_supported() {
    local version="$1"
    [[ "$version" =~ ^[0-9]+([.][0-9]+)*$ ]] &&
        [[ "$(printf '%s\n' "$MIN_TOOLCHAIN_VERSION" "$version" | sort -V | head -n 1)" == "$MIN_TOOLCHAIN_VERSION" ]]
}

if [[ -n "${TOOLCHAIN_ENV:-}" ]]; then
    toolchain_env="$TOOLCHAIN_ENV"
else
    toolchain_env=""
    mapfile -t toolchain_candidates < <(
        find "$TOOLCHAIN_ROOT" -mindepth 2 -maxdepth 2 -type f \
            -path "$TOOLCHAIN_ROOT/v*/$TOOLCHAIN_SCRIPT" -print 2>/dev/null |
            sort -V
    )

    for ((index = ${#toolchain_candidates[@]} - 1; index >= 0; index--)); do
        candidate="${toolchain_candidates[index]}"
        version="$(basename -- "$(dirname -- "$candidate")")"
        version="${version#v}"
        if version_is_supported "$version"; then
            toolchain_env="$candidate"
            break
        fi
    done
fi

if [[ -z "$toolchain_env" || ! -f "$toolchain_env" ]]; then
    printf '%s\n' \
        "ERROR: A supported Mobilint cross-toolchain environment script was not found." \
        "Install Mobilint SDK 2.0.0 toolchain v${MIN_TOOLCHAIN_VERSION} or later." \
        "Expected path: $TOOLCHAIN_ROOT/v<version>/$TOOLCHAIN_SCRIPT" \
        "You can also set TOOLCHAIN_ENV to the installed environment script." >&2
    exit 1
fi

unset LD_LIBRARY_PATH

# Yocto environment scripts can read unset variables. Disable nounset only while sourcing.
set +u
if ! source "$toolchain_env"; then
    set -u
    printf 'ERROR: Failed to source toolchain environment: %s\n' "$toolchain_env" >&2
    exit 1
fi
set -u

cmake -S "$SCRIPT_DIR" -B "$SCRIPT_DIR/build"
cmake --build "$SCRIPT_DIR/build" --parallel "$(nproc)"
cmake -E copy "$SCRIPT_DIR/build/inference" "$SCRIPT_DIR/inference"
