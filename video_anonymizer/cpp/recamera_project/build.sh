#!/bin/bash
set -e

# Script to build the ReCamera (RISC-V) project
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

# Define colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to display errors/warnings/success
print_error() { echo -e "${RED}ERROR: $1${NC}"; }
print_warning() { echo -e "${YELLOW}WARNING: $1${NC}"; }
print_success() { echo -e "${GREEN}$1${NC}"; }

# Process command line arguments
CLEAN=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --clean)
            CLEAN=true
            shift
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--clean]"
            exit 1
            ;;
    esac
done

# Check if SG200X_SDK_PATH is set
if [ -z "$SG200X_SDK_PATH" ]; then
    print_warning "SG200X_SDK_PATH environment variable is not set."
    # Here you can set the default path to the reCamera SDK of your development machine
    export SG200X_SDK_PATH="/path/to/your/sg2002_recamera_emmc/"
    print_warning "Setting SG200X_SDK_PATH to default path: $SG200X_SDK_PATH"
    exit 1
fi

# Verify SDK path exists
if [ ! -d "$SG200X_SDK_PATH" ]; then
    print_error "The specified SG200X_SDK_PATH does not exist: $SG200X_SDK_PATH"
    exit 1
fi

# Check for RISC-V toolchain
if ! command -v riscv64-unknown-linux-musl-gcc &> /dev/null; then
    print_warning "RISC-V toolchain (riscv64-unknown-linux-musl-gcc) not found in PATH."
    print_warning "Adding $SG200X_SDK_PATH/../../host-tools/gcc/riscv64-linux-musl-x86_64/bin to PATH"
    export PATH=$SG200X_SDK_PATH/../../host-tools/gcc/riscv64-linux-musl-x86_64/bin:$PATH
    if ! command -v riscv64-unknown-linux-musl-gcc &> /dev/null; then
        print_error "Failed to add RISC-V toolchain to PATH."
        exit 1
    fi
fi

# Create or clean build directory
if [ "$CLEAN" = true ] && [ -d "$BUILD_DIR" ]; then
    print_success "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure and build
print_success "Configuring build for ReCamera platform..."
cmake ..

if [ $? -eq 0 ]; then
    print_success "Building..."
    make -j$(nproc)
    
    if [ $? -eq 0 ]; then
        print_success "Build completed successfully!"
    else
        print_error "Build failed during make stage."
        exit 1
    fi
else
    print_error "Build failed during CMake configuration stage."
    exit 1
fi 