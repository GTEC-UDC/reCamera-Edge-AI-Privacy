#!/bin/bash
set -e

# Script to build the default (x86_64) project
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

# Process command line arguments
BUILD_DEBUG=OFF
CLEAN=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --debug)
            BUILD_DEBUG=ON
            shift
            ;;
        --clean)
            CLEAN=true
            shift
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--debug] [--clean]"
            exit 1
            ;;
    esac
done

# Create or clean build directory
if [ "$CLEAN" = true ] && [ -d "$BUILD_DIR" ]; then
    echo "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure and build
echo "Configuring build with DEBUG=$BUILD_DEBUG..."
cmake .. -DBUILD_DEBUG=$BUILD_DEBUG

echo "Building..."
make -j$(nproc)

echo "Build complete! Executables are in $BUILD_DIR"
echo "You can run the application with:"
echo "  $BUILD_DIR/video_anonymizer [options]"
