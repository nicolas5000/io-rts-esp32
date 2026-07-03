#!/usr/bin/env bash
set -e

SRC_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SRC_DIR}/build"

echo "=== Building JsonStreamingParser Test ==="
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"
cmake "${SRC_DIR}"
make -j$(nproc)

echo ""
echo "=== Running Test ==="
./test_parser
