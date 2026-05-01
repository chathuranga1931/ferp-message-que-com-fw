#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake ..
make -j$(sysctl -n hw.logicalcpu)

echo ""
echo "Build complete: $BUILD_DIR/ferp-com-simulator"
