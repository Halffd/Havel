#!/bin/bash

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Build directory
BUILD_DIR="build"
LOG_DIR="logs"
BUILD_LOG="${LOG_DIR}/build.log"

# Ensure log directory exists
mkdir -p "${LOG_DIR}"

# Function to log messages to both console and file
log() {
    local level=$1
    local message=$2
    local color=$3
    echo -e "${color}[${level}] ${message}${NC}" | tee -a "${BUILD_LOG}"
}

# Function to check if last command succeeded
check_status() {
    if [ $? -eq 0 ]; then
        log "SUCCESS" "$1" "${GREEN}"
    else
        log "ERROR" "$1" "${RED}"
        exit 1
    fi
}

# Clean build
clean() {
    log "INFO" "Cleaning build directory..." "${YELLOW}"
    rm -rf "${BUILD_DIR}"
    rm -f "${BUILD_LOG}"
    check_status "Clean completed"
}

# Build project
build() {
    log "INFO" "Creating build directory..." "${YELLOW}"
    mkdir -p "${BUILD_DIR}"
    
    log "INFO" "Generating build files with CMake..." "${YELLOW}"
    (cd "${BUILD_DIR}" && cmake ..) 2>&1 | tee -a "${BUILD_LOG}"
    check_status "CMake generation"
    
    log "INFO" "Building project..." "${YELLOW}"
    (cd "${BUILD_DIR}" && make -j$(nproc)) 2>&1 | tee -a "${BUILD_LOG}"
    check_status "Build"
}

# Run the project
run() {
    if [ ! -f "${BUILD_DIR}/HvC" ]; then
        log "ERROR" "Executable not found. Build the project first." "${RED}"
        exit 1
    fi
    
    log "INFO" "Running HvC..." "${YELLOW}"
    "${BUILD_DIR}/HvC" "$@" 2>&1 | tee -a "${BUILD_LOG}"
}

# Show usage
usage() {
    echo "Usage: $0 [clean|build|run|all] [run_args...]"
    echo "  clean     - Clean build directory"
    echo "  build     - Build the project"
    echo "  run       - Run the project"
    echo "  all       - Clean, build, and run"
    echo "  run_args  - Optional arguments to pass to the program"
    exit 1
}

# Main script
case "$1" in
    "clean")
        clean
        ;;
    "build")
        build
        ;;
    "run")
        shift
        run "$@"
        ;;
    "all")
        shift
        clean
        build
        run "$@"
        ;;
    *)
        usage
        ;;
esac

exit 0 