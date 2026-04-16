#!/bin/bash

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

log() {
    local level=$1
    local message=$2
    local color=$3
    echo -e "${color}[${level}]${NC} ${message}" | tee -a "${BUILD_LOG:-/dev/null}"
}

detect_cores() {
    local cores
    if cores=$(nproc 2>/dev/null) || cores=$(sysctl -n hw.ncpu 2>/dev/null) || cores=$(grep -c ^processor /proc/cpuinfo 2>/dev/null); then
        echo "$cores"
    else
        echo 4
    fi
}

detect_libraries() {
    local libs_info=""
    
    check_lib() {
        local lib=$1
        if pkg-config --exists "$lib" 2>/dev/null; then
            local version
            version=$(pkg-config --modversion "$lib" 2>/dev/null || echo "found")
            echo "  ${GREEN}✓${NC} $lib ($version)"
            return 0
        else
            echo "  ${RED}✗${NC} $lib (not found)"
            return 1
        fi
    }
    
    check_lib_quiet() {
        local lib=$1
        if pkg-config --exists "$lib" 2>/dev/null; then
            return 0
        fi
        return 1
    }
    
    log "INFO" "Detecting system libraries..." "${BLUE}"
    echo ""
    echo "Core Dependencies:"
    check_lib "x11" || true
    check_lib "xrandr" || true
    check_lib "xinerama" || true
    check_lib "xcomposite" || true
    check_lib "xtst" || true
    check_lib "xi" || true
    check_lib "xfixes" || true
    check_lib "xdamage" || true
    check_lib "spdlog" || true
    check_lib "nlohmann_json" || true
    
    echo ""
    echo "Audio/Media:"
    check_lib "libpulse" || true
    check_lib "libpipewire-0.3" || check_lib "libpipewire-0.3" || true
    check_lib "alsa" || true
    
    echo ""
    echo "GUI Frameworks:"
    check_lib "Qt6Core" || check_lib "qt6-base" || true
    check_lib "gtk-4.0" || true
    
    echo ""
    echo "Additional:"
    check_lib "lua5.4" || check_lib "lua" || true
    check_lib "libcurl" || true
    check_lib "libmpv" || true
    check_lib "minizip" || true
    check_lib "libepoxy" || true
    
    echo ""
}

detect_llvm() {
    if command -v llvm-config &>/dev/null; then
        local llvm_version
        llvm_version=$(llvm-config --version 2>/dev/null || echo "unknown")
        log "INFO" "LLVM found: ${llvm_version}" "${GREEN}"
        return 0
    else
        log "INFO" "LLVM not found (JIT disabled)" "${YELLOW}"
        return 1
    fi
}

export CC=clang
export CXX=clang++
OLD_LD_LIBRARY_PATH=${LD_LIBRARY_PATH:-}
unset LD_LIBRARY_PATH

BUILD_MODE=${1:-0}
shift 2>/dev/null || true

BUILD_TYPE="Release"
BUILD_DIR="build"
LOG_DIR="logs"
THREADS=${THREADS:-$(detect_cores)}

declare -A BUILD_CONFIGS=(
    [0]="Debug,ON,ON,ON,build-debug"
    [1]="Release,OFF,OFF,ON,build-release"
    [2]="Debug,OFF,ON,ON,build-debug"
    [3]="Debug,OFF,OFF,OFF,build-debug"
    [5]="Release,ON,ON,ON,build-release"
    [6]="Debug,ON,ON,OFF,build-debug"
    [7]="Release,OFF,OFF,OFF,build-release"
    [8]="Debug,OFF,ON,OFF,build-debug"
    [9]="Release,ON,ON,OFF,build-release"
    [10]="Debug,OFF,ON,ON,build"
    [11]="Release,OFF,ON,ON,build"
)

if [[ "$BUILD_MODE" =~ ^[0-9]+$ ]] && [[ -n "${BUILD_CONFIGS[$BUILD_MODE]:-}" ]]; then
    IFS=',' read -r BUILD_TYPE ENABLE_TESTS ENABLE_HAVEL_LANG ENABLE_LLVM BUILD_DIR <<<"${BUILD_CONFIGS[$BUILD_MODE]}"
    
    if [[ "$ENABLE_LLVM" == "ON" && "$ENABLE_HAVEL_LANG" == "OFF" ]]; then
        log "WARNING" "LLVM requires Havel Lang - enabling automatically" "${YELLOW}"
        ENABLE_HAVEL_LANG="ON"
    fi
else
    log "ERROR" "Invalid build mode: $BUILD_MODE" "${RED}"
    echo "Valid modes: ${!BUILD_CONFIGS[@]}"
    exit 1
fi

BUILD_LOG="${LOG_DIR}/build-mode${BUILD_MODE}-${BUILD_TYPE,,}.log"
mkdir -p "${LOG_DIR}"

show_config() {
    log "INFO" "=== BUILD CONFIGURATION ===" "${BLUE}"
    log "INFO" "Mode: ${BUILD_MODE}" "${BLUE}"
    log "INFO" "Type: ${BUILD_TYPE}" "${BLUE}"
    log "INFO" "Threads: ${THREADS}" "${BLUE}"
    log "INFO" "Build Dir: ${BUILD_DIR}" "${BLUE}"
    echo ""
    log "INFO" "Features:" "${CYAN}"
    log "INFO" "  Tests: $([[ "$ENABLE_TESTS" == "ON" ]] && echo "${GREEN}ENABLED${NC}" || echo "${RED}DISABLED${NC}")" "${BLUE}"
    log "INFO" "  Havel Lang: $([[ "$ENABLE_HAVEL_LANG" == "ON" ]] && echo "${GREEN}ENABLED${NC}" || echo "${RED}DISABLED${NC}")" "${BLUE}"
    log "INFO" "  LLVM JIT: $([[ "$ENABLE_LLVM" == "ON" ]] && echo "${GREEN}ENABLED${NC}" || echo "${RED}DISABLED${NC}")" "${BLUE}"
    
    case $BUILD_MODE in
        0) echo "  ${GREEN}→${NC} Standard development build" ;;
        1) echo "  ${GREEN}→${NC} Minimal release build" ;;
        2) echo "  ${GREEN}→${NC} Quick debug build" ;;
        3) echo "  ${GREEN}→${NC} Test-focused development" ;;
        5) echo "  ${GREEN}→${NC} Full-featured release" ;;
        6) echo "  ${YELLOW}→${NC} Debug without LLVM complexity" ;;
        7) echo "  ${YELLOW}→${NC} Lightweight release (no LLVM)" ;;
        8) echo "  ${YELLOW}→${NC} Pure language development" ;;
        9) echo "  ${YELLOW}→${NC} Feature-complete release (no LLVM)" ;;
    esac
}

detect() {
    log "INFO" "=== SYSTEM DETECTION ===" "${BLUE}"
    echo ""
    log "INFO" "CPU Cores: $(nproc 2>/dev/null || echo "unknown")" "${BLUE}"
    log "INFO" "Memory: $(free -h 2>/dev/null | awk '/^Mem:/ {print $2}' || echo "unknown")" "${BLUE}"
    echo ""
    detect_llvm || true
    echo ""
    detect_libraries
}

clean() {
    log "INFO" "Cleaning ${BUILD_DIR}..." "${YELLOW}"
    rm -rf "${BUILD_DIR}"
    rm -f "${BUILD_LOG}"
}

build() {
    show_config
    
    log "INFO" "Building in ${BUILD_TYPE} mode with ${THREADS} threads..." "${BLUE}"
    mkdir -p "${BUILD_DIR}"
    
    local cmake_cmd="cmake -B ${BUILD_DIR}"
    cmake_cmd+=" -DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
    cmake_cmd+=" -DCMAKE_C_COMPILER=clang"
    cmake_cmd+=" -DCMAKE_CXX_COMPILER=clang++"
    cmake_cmd+=" -DUSE_CLANG=ON"
    cmake_cmd+=" -DENABLE_LLVM=${ENABLE_LLVM}"
    cmake_cmd+=" -DENABLE_TESTS=${ENABLE_TESTS}"
    cmake_cmd+=" -DENABLE_HAVEL_LANG=${ENABLE_HAVEL_LANG}"
    cmake_cmd+=" .."
    
    log "INFO" "CMake command: ${cmake_cmd}" "${YELLOW}"
    
    if ! eval "${cmake_cmd}" 2>&1 | tee -a "${BUILD_LOG}"; then
        log "ERROR" "CMake configuration failed" "${RED}"
        exit 1
    fi
    
    log "INFO" "Building with make -j${THREADS}..." "${YELLOW}"
    
    if ! cmake --build "${BUILD_DIR}" -j"${THREADS}" 2>&1 | tee -a "${BUILD_LOG}"; then
        log "ERROR" "Build failed" "${RED}"
        exit 1
    fi
    
    log "SUCCESS" "Build completed successfully" "${GREEN}"
}

run() {
    local executable="${BUILD_DIR}/havel"
    
    if [[ ! -f "$executable" ]]; then
        log "ERROR" "Executable not found: ${executable}" "${RED}"
        exit 1
    fi
    
    log "INFO" "Running ${executable}..." "${YELLOW}"
    "${executable}" "$@"
}

test() {
    if [[ "$ENABLE_TESTS" != "ON" ]]; then
        log "ERROR" "Tests disabled in mode ${BUILD_MODE}" "${RED}"
        exit 1
    fi
    
    log "INFO" "Running tests..." "${BLUE}"
    
    local test_count=0
    while IFS= read -r test_exe; do
        if [[ -f "$test_exe" && -x "$test_exe" ]]; then
            log "INFO" "Running $(basename "$test_exe")..." "${YELLOW}"
            if "$test_exe" 2>&1 | tee -a "${BUILD_LOG}"; then
                ((test_count++))
            fi
        fi
    done < <(find "${BUILD_DIR}" -maxdepth 1 -name 'test_*' -type f -executable 2>/dev/null)
    
    if [[ $test_count -eq 0 ]]; then
        log "WARNING" "No test executables found" "${YELLOW}"
    else
        log "SUCCESS" "Ran ${test_count} test suites" "${GREEN}"
    fi
}

usage() {
    cat <<EOF
Usage: $0 [mode] [command] [options]

Modes:
  0         Debug + Tests + Havel Lang + LLVM (default)
  1         Release + No Tests + No Havel Lang + LLVM
  2         Debug + No Tests + Havel Lang + LLVM
  3         Debug + No Tests + No Havel Lang + LLVM
  5         Release + Tests + Havel Lang + LLVM
  6-9       LLVM-free modes (faster builds)

Commands:
  build     Build the project
  clean     Clean build directory
  run       Run the executable
  test      Run tests
  all       Clean + build + run
  detect    Detect system libraries and features
  info      Show build configuration

Environment Variables:
  THREADS   Number of parallel build jobs (default: auto-detect)

Examples:
  $0 build              # Mode 0: Full debug with LLVM
  $0 6 build            # Mode 6: Debug without LLVM
  $0 9 all              # Mode 9: Full release without LLVM
  THREADS=4 $0 build    # Use 4 threads

Logs: ${LOG_DIR}/build-mode[X]-[type].log
EOF
    exit 1
}

process_commands() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            build)
                build
                ;;
            clean)
                clean
                ;;
            rebuild)
                clean
                build
                ;;
            run)
                shift
                run "$@"
                break
                ;;
            test)
                test
                ;;
            all)
                clean
                build
                shift
                run "$@"
                break
                ;;
            detect|info)
                detect
                show_config
                ;;
            -h|--help|help)
                usage
                ;;
            *)
                log "ERROR" "Unknown command: $1" "${RED}"
                usage
                ;;
        esac
        shift
    done
}

export LD_LIBRARY_PATH=$OLD_LD_LIBRARY_PATH

process_commands "$@"
