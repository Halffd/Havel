#!/bin/bash

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color
export CC=clang
export CXX=clang++
OLD_LD_LIBRARY_PATH=$LD_LIBRARY_PATH
unset LD_LIBRARY_PATH

# Default build mode
BUILD_MODE=0
BUILD_TYPE="Debug"
BUILD_DIR="build"
LOG_DIR="logs"
THREADS=3

# Build mode configurations
# Format: "BuildType,EnableTests,EnableHavelLang,EnableLLVM,BuildDir"
declare -A BUILD_CONFIGS=(
    [0]="Debug,ON,ON,ON,build-debug"              # 0: debug, tests, lang, llvm
    [1]="Release,OFF,OFF,ON,build-release"        # 1: release, no tests, no lang, llvm  
    [2]="Debug,OFF,OFF,ON,build-debug-minimal"    # 2: debug, no tests, no lang, llvm
    [3]="Debug,OFF,OFF,OFF,build-debug-fast"      # 3: debug, tests, no lang, llvm
    [5]="Release,ON,ON,ON,build-release-full"     # 5: release, tests, lang, llvm
    
    # LLVM-specific modes
    [6]="Debug,ON,ON,OFF,build-debug-nollvm"      # 6: debug, tests, lang, NO llvm
    [7]="Release,OFF,OFF,OFF,build-release-nollvm" # 7: release, no tests, no lang, NO llvm
    [8]="Debug,OFF,ON,OFF,build-debug-pure"       # 8: debug, no tests, lang, NO llvm
    [9]="Release,ON,ON,OFF,build-release-pure"    # 9: release, tests, lang, NO llvm
)

# Parse build mode from environment or first arg
if [[ "$1" =~ ^[012356789]$ ]]; then
    BUILD_MODE=$1
    shift
    
    # Parse configuration
    IFS=',' read -r BUILD_TYPE ENABLE_TESTS ENABLE_HAVEL_LANG ENABLE_LLVM BUILD_DIR <<< "${BUILD_CONFIGS[$BUILD_MODE]}"
    
    # Handle missing mode
    if [[ -z "$BUILD_TYPE" ]]; then
        echo -e "${RED}[ERROR] Invalid build mode: $BUILD_MODE${NC}"
        echo "Valid modes: 0, 1, 2, 3, 5, 6, 7, 8, 9"
        exit 1
    fi
    
    # Fix logical inconsistency: If LLVM is enabled, Havel Lang must also be enabled
    if [[ "$ENABLE_LLVM" == "ON" && "$ENABLE_HAVEL_LANG" == "OFF" ]]; then
        log "WARNING" "LLVM requires Havel Lang - enabling Havel Lang automatically" "${YELLOW}"
        ENABLE_HAVEL_LANG="ON"
    fi
fi

# Update log file path based on build type and mode
BUILD_LOG="${LOG_DIR}/build-mode${BUILD_MODE}-${BUILD_TYPE,,}.log"

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

# Show build configuration
show_config() {
    log "INFO" "=== BUILD CONFIGURATION ===" "${BLUE}"
    log "INFO" "Mode: ${BUILD_MODE}" "${BLUE}"
    log "INFO" "Type: ${BUILD_TYPE}" "${BLUE}"
    log "INFO" "Tests: $([ "$ENABLE_TESTS" = "ON" ] && echo "ENABLED" || echo "DISABLED")" "${BLUE}"
    log "INFO" "Havel Lang: $([ "$ENABLE_HAVEL_LANG" = "ON" ] && echo "ENABLED" || echo "DISABLED")" "${BLUE}"
    log "INFO" "LLVM JIT: $([ "$ENABLE_LLVM" = "ON" ] && echo "ENABLED" || echo "DISABLED")" "${BLUE}"
    log "INFO" "Build Dir: ${BUILD_DIR}" "${BLUE}"
    
    # Show what this mode is good for
    case $BUILD_MODE in
        0) log "INFO" "📋 Standard development build" "${GREEN}" ;;
        1) log "INFO" "🚀 Minimal release build" "${GREEN}" ;;
        2) log "INFO" "🔧 Quick debug build" "${GREEN}" ;;
        3) log "INFO" "🧪 Test-focused development" "${GREEN}" ;;
        5) log "INFO" "💎 Full-featured release" "${GREEN}" ;;
        6) log "INFO" "🚫 Debug without LLVM complexity" "${YELLOW}" ;;
        7) log "INFO" "📦 Lightweight release (no LLVM)" "${YELLOW}" ;;
        8) log "INFO" "🔍 Pure language development" "${YELLOW}" ;;
        9) log "INFO" "✨ Feature-complete release (no LLVM)" "${YELLOW}" ;;
    esac
    
    if [[ "$BUILD_TYPE" = "Release" ]]; then
        log "INFO" "🚀 Release flags: -O3 -march=native -flto -ffast-math" "${GREEN}"
    else
        log "INFO" "🐛 Debug flags: -O0 -g -Wall -Wextra" "${YELLOW}"
    fi
    
    if [[ "$ENABLE_LLVM" = "OFF" ]]; then
        log "WARNING" "⚡ LLVM disabled - no JIT compilation" "${YELLOW}"
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
    show_config
    
    log "INFO" "Building in ${BUILD_TYPE} mode..." "${BLUE}"
    
    log "INFO" "Creating build directory..." "${YELLOW}"
    mkdir -p "${BUILD_DIR}"

    log "INFO" "Generating build files with CMake..." "${YELLOW}"
    
    # Build CMake command based on configuration
    local cmake_cmd="cmake"
    cmake_cmd+=" -DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
    cmake_cmd+=" -DUSE_CLANG=ON"
    cmake_cmd+=" -DENABLE_LLVM=${ENABLE_LLVM}"
    cmake_cmd+=" -DENABLE_TESTS=${ENABLE_TESTS}"
    cmake_cmd+=" -DENABLE_HAVEL_LANG=${ENABLE_HAVEL_LANG}"
    cmake_cmd+=" .."
    
    log "INFO" "CMake command: ${cmake_cmd}" "${YELLOW}"
    (cd "${BUILD_DIR}" && eval "${cmake_cmd}") 2>&1 | tee -a "${BUILD_LOG}"
    check_status "CMake generation"

    log "INFO" "Building project with ${THREADS} parallel jobs..." "${YELLOW}"
    (cd "${BUILD_DIR}" && make -j${THREADS}) 2>&1 | tee -a "${BUILD_LOG}"
    check_status "Build"
    
    # Show build results
    log "INFO" "=== BUILD RESULTS ===" "${GREEN}"
    
    if [[ -f "${BUILD_DIR}/havel" ]]; then
        local size=$(du -h "${BUILD_DIR}/havel" | cut -f1)
        log "SUCCESS" "✅ havel built successfully (${size})" "${GREEN}"
    fi
    
    if [[ "$ENABLE_TESTS" = "ON" ]]; then
        for test_exe in test_havel test_gui files_test main_test utils_test; do
            if [[ -f "${BUILD_DIR}/${test_exe}" ]]; then
                local size=$(du -h "${BUILD_DIR}/${test_exe}" | cut -f1)
                log "SUCCESS" "✅ ${test_exe} built successfully (${size})" "${GREEN}"
            fi
        done
    fi
    
    if [[ "$ENABLE_HAVEL_LANG" = "ON" ]]; then
        if [[ -f "${BUILD_DIR}/libhavel_lang.a" ]]; then
            local size=$(du -h "${BUILD_DIR}/libhavel_lang.a" | cut -f1)
            local llvm_status=$([ "$ENABLE_LLVM" = "ON" ] && echo "with LLVM JIT" || echo "interpreter only")
            log "SUCCESS" "✅ libhavel_lang.a built successfully (${size}) - ${llvm_status}" "${GREEN}"
        fi
    fi
}

# Run the project
run() {
    local executable="havel"
    
    # Check for executable
    if [[ -f "${BUILD_DIR}/havel" ]]; then
        executable="havel"
    elif [[ -f "${BUILD_DIR}/test_havel" ]]; then
        executable="test_havel"
        log "INFO" "Running Havel language tests..." "${BLUE}"
    else
        log "ERROR" "No executable found in ${BUILD_DIR}/. Build the project first." "${RED}"
        exit 1
    fi

    log "INFO" "Running ${executable} (mode ${BUILD_MODE}: ${BUILD_TYPE})..." "${YELLOW}"
    
    if [[ "$ENABLE_LLVM" = "OFF" ]]; then
        log "WARNING" "Running without LLVM JIT - only interpreter available" "${YELLOW}"
    fi
    
    "${BUILD_DIR}/${executable}" "$@" 2>&1 | tee -a "${BUILD_LOG}"
}

# Test Havel language specifically
test() {
    if [[ "$ENABLE_TESTS" = "OFF" ]]; then
        log "ERROR" "Tests are disabled in build mode ${BUILD_MODE}" "${RED}"
        exit 1
    fi

    log "INFO" "Running all available tests..." "${BLUE}"
    local test_count=0
    
    for test_exe in test_havel test_gui files_test main_test utils_test; do
        if [[ -f "${BUILD_DIR}/${test_exe}" ]]; then
            log "INFO" "Running ${test_exe}..." "${YELLOW}"
            "${BUILD_DIR}/${test_exe}" "$@" 2>&1 | tee -a "${BUILD_LOG}"
            ((test_count++))
        fi
    done
    
    if [[ $test_count -eq 0 ]]; then
        log "ERROR" "No test executables found. Build the project first." "${RED}"
        exit 1
    fi
    
    local llvm_note=""
    if [[ "$ENABLE_LLVM" = "OFF" ]]; then
        llvm_note=" (LLVM JIT tests skipped)"
    fi
    
    log "SUCCESS" "Ran ${test_count} test suite(s)${llvm_note}" "${GREEN}"
}
# Show usage
usage() {
    echo "Usage: $0 [mode] [command] [args...]"
    echo ""
    echo "Standard build modes:"
    echo "  0 (default) - Debug + Tests + Havel Lang + LLVM"
    echo "  1           - Release + No Tests + No Havel Lang + LLVM"  
    echo "  2           - Debug + No Tests + No Havel Lang + LLVM"
    echo "  3           - Debug + Tests + No Havel Lang + LLVM"
    echo "  5           - Release + Tests + Havel Lang + LLVM"
    echo ""
    echo "LLVM-free modes (faster builds, no JIT):"
    echo "  6           - Debug + Tests + Havel Lang + NO LLVM"
    echo "  7           - Release + No Tests + No Havel Lang + NO LLVM"
    echo "  8           - Debug + No Tests + Havel Lang + NO LLVM"
    echo "  9           - Release + Tests + Havel Lang + NO LLVM"
    echo ""
    echo "Commands:"
    echo "  clean     - Clean build directory"
    echo "  build     - Build the project"
    echo "  run       - Run the main executable"
    echo "  test      - Run all available tests"
    echo "  all       - Clean, build, and run"
    echo ""
    echo "Examples:"
    echo "  $0 build           # Mode 0: Full debug with LLVM"
    echo "  $0 6 build         # Mode 6: Debug without LLVM complexity"
    echo "  $0 9 all           # Mode 9: Full release without LLVM"
    echo "  $0 7 clean build   # Mode 7: Lightweight release"
    echo ""
    echo "Use modes 6-9 for:"
    echo "  - Faster builds (no LLVM compilation)"
    echo "  - Systems without LLVM development libraries"
    echo "  - Testing interpreter-only functionality"
    echo "  - CI/CD environments"
    echo ""
    echo "Note: LLVM mode automatically enables Havel Lang (required dependency)"
    echo "Logs are saved to: ${LOG_DIR}/build-mode[X]-[type].log"
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

# Process all commands
process_commands "$@"
LD_LIBRARY_PATH=$OLD_LD_LIBRARY_PATH

exit 0