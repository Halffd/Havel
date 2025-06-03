#!/bin/bash

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default build mode (0 = Debug, 1 = Release)
BUILD_MODE=0
BUILD_TYPE="Debug"
BUILD_DIR="build"
LOG_DIR="logs"
BUILD_LOG="${LOG_DIR}/build.log"

# Parse build mode from environment or first arg
if [[ "$1" =~ ^[01]$ ]]; then
    BUILD_MODE=$1
    shift
    if [[ $BUILD_MODE -eq 1 ]]; then
        BUILD_TYPE="Release"
        BUILD_DIR="build-release"
    else
        BUILD_TYPE="Debug" 
        BUILD_DIR="build-debug"
    fi
fi

# Update log file path based on build type
BUILD_LOG="${LOG_DIR}/build-${BUILD_TYPE,,}.log"

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
    log "INFO" "Cleaning ${BUILD_TYPE} build directory..." "${YELLOW}"
    rm -rf "${BUILD_DIR}"
    rm -f "${BUILD_LOG}"
    check_status "Clean completed"
}

# Build project
build() {
    log "INFO" "Building in ${BUILD_TYPE} mode..." "${BLUE}"
    
    if [[ $BUILD_MODE -eq 1 ]]; then
        log "INFO" "🚀 Release flags: -O3 -march=native -flto -mavx2 -mfma" "${GREEN}"
    else
        log "INFO" "🐛 Debug flags: -O0 -g -Wall -Wextra" "${YELLOW}"
    fi
    
    log "INFO" "Creating build directory..." "${YELLOW}"
    mkdir -p "${BUILD_DIR}"

    log "INFO" "Generating build files with CMake..." "${YELLOW}"
    (cd "${BUILD_DIR}" && cmake -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" ..) 2>&1 | tee -a "${BUILD_LOG}"
    check_status "CMake generation"

    log "INFO" "Building project with $(nproc) parallel jobs..." "${YELLOW}"
    (cd "${BUILD_DIR}" && make -j$(nproc)) 2>&1 | tee -a "${BUILD_LOG}"
    check_status "Build"
    
    # Show build results
    if [[ -f "${BUILD_DIR}/hvc" ]]; then
        local size=$(du -h "${BUILD_DIR}/hvc" | cut -f1)
        log "SUCCESS" "✅ hvc built successfully (${size})" "${GREEN}"
    fi
    
    if [[ -f "${BUILD_DIR}/test_havel" ]]; then
        local size=$(du -h "${BUILD_DIR}/test_havel" | cut -f1)
        log "SUCCESS" "✅ test_havel built successfully (${size})" "${GREEN}"
    fi
}

# Run the project
run() {
    local executable="hvc"
    
    # Check for different possible executable names
    if [[ -f "${BUILD_DIR}/hvc" ]]; then
        executable="hvc"
    elif [[ -f "${BUILD_DIR}/HvC" ]]; then
        executable="HvC"
    elif [[ -f "${BUILD_DIR}/test_havel" ]]; then
        executable="test_havel"
        log "INFO" "Running Havel language tests..." "${BLUE}"
    else
        log "ERROR" "No executable found in ${BUILD_DIR}/. Build the project first." "${RED}"
        exit 1
    fi

    log "INFO" "Running ${executable} (${BUILD_TYPE} build)..." "${YELLOW}"
    "${BUILD_DIR}/${executable}" "$@" 2>&1 | tee -a "${BUILD_LOG}"
}

# Test Havel language specifically
test() {
    if [[ ! -f "${BUILD_DIR}/test_havel" ]]; then
        log "ERROR" "test_havel not found. Build the project first." "${RED}"
        exit 1
    fi

    log "INFO" "Running Havel language tests..." "${BLUE}"
    "${BUILD_DIR}/test_havel" "$@" 2>&1 | tee -a "${BUILD_LOG}"
}

# Show usage
usage() {
    echo "Usage: $0 [0|1] [command] [args...]"
    echo ""
    echo "Build modes:"
    echo "  0 (default) - Debug build (-O0 -g)"
    echo "  1           - Release build (-O3 + optimizations)"
    echo ""
    echo "Commands:"
    echo "  clean     - Clean build directory"
    echo "  build     - Build the project"
    echo "  run       - Run the main executable"
    echo "  test      - Run Havel language tests"
    echo "  all       - Clean, build, and run"
    echo ""
    echo "Examples:"
    echo "  $0 build           # Debug build"
    echo "  $0 1 build         # Release build"
    echo "  $0 0 all           # Clean debug build and run"
    echo "  $0 1 clean build   # Clean release build"
    echo "  $0 test            # Run Havel tests (debug)"
    echo "  $0 1 test          # Run Havel tests (release)"
    echo ""
    echo "Logs are saved to: ${LOG_DIR}/"
    exit 1
}

# Process multiple commands
process_commands() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            "clean")
                clean
                shift
                ;;
            "build")
                build
                shift
                ;;
            "run")
                shift
                run "$@"
                break  # run consumes remaining args
                ;;
            "test")
                shift
                test "$@"
                break  # test consumes remaining args
                ;;
            "all")
                shift
                clean
                build
                run "$@"
                break  # run consumes remaining args
                ;;
            *)
                log "ERROR" "Unknown command: $1" "${RED}"
                usage
                ;;
        esac
    done
}

# Main script
if [[ $# -eq 0 ]]; then
    usage
fi

# Show current build mode
log "INFO" "Build mode: ${BUILD_TYPE} (${BUILD_MODE})" "${BLUE}"

# Process all commands
process_commands "$@"

exit 0