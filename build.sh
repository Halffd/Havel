#!/bin/bash

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

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
    check_lib() {
        local lib=$1
        if pkg-config --exists "$lib" 2>/dev/null; then
            local version
            version=$(pkg-config --modversion "$lib" 2>/dev/null || echo "found")
            echo -e "  ${GREEN}✓${NC} $lib ($version)"
            return 0
        else
            echo -e "  ${RED}✗${NC} $lib (not found)"
            return 1
        fi
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
    check_lib "libpipewire-0.3" || true
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

if [[ $# -eq 0 ]] || [[ "$1" =~ ^(build|clean|rebuild|run|test|all|detect|info|help|--help|-h)$ ]]; then
    BUILD_MODE=6
else
    BUILD_MODE=$1
    shift
fi

BUILD_TYPE="Release"
BUILD_DIR="build"
LOG_DIR="logs"
THREADS=${THREADS:-$(detect_cores)}

declare -A BUILD_CONFIGS=(
    [0]="Debug,ON,ON,ON,build-debug"
    [1]="Release,OFF,OFF,ON,build-release"
    [2]="Debug,OFF,ON,ON,build-debug"
    [3]="Debug,OFF,OFF,OFF,build-debug"
    [4]="Debug,ON,ON,OFF,build-debug"
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
    log "INFO" "Build Dir: ${SCRIPT_DIR}/${BUILD_DIR}" "${BLUE}"
    log "INFO" "Source Dir: ${SCRIPT_DIR}" "${BLUE}"
    echo ""
    log "INFO" "Features:" "${CYAN}"
    log "INFO" "  Tests:     $([[ "$ENABLE_TESTS" == "ON" ]] && echo "ENABLED" || echo "DISABLED")" "${BLUE}"
    log "INFO" "  Havel Lang: $([[ "$ENABLE_HAVEL_LANG" == "ON" ]] && echo "ENABLED" || echo "DISABLED")" "${BLUE}"
    log "INFO" "  LLVM JIT:  $([[ "$ENABLE_LLVM" == "ON" ]] && echo "ENABLED" || echo "DISABLED")" "${BLUE}"
    case $BUILD_MODE in
        0) echo -e "  ${GREEN}→${NC} Standard development build" ;;
        1) echo -e "  ${GREEN}→${NC} Minimal release build" ;;
        2) echo -e "  ${GREEN}→${NC} Quick debug build" ;;
        3) echo -e "  ${GREEN}→${NC} Test-focused development" ;;
        4) echo -e "  ${YELLOW}→${NC} Debug with tests, no LLVM" ;;
        5) echo -e "  ${GREEN}→${NC} Full-featured release" ;;
        6) echo -e "  ${YELLOW}→${NC} Debug without LLVM (default)" ;;
        7) echo -e "  ${YELLOW}→${NC} Lightweight release" ;;
        8) echo -e "  ${YELLOW}→${NC} Pure language development" ;;
        9) echo -e "  ${YELLOW}→${NC} Feature-complete release" ;;
    esac
}

detect() {
    log "INFO" "=== SYSTEM DETECTION ===" "${BLUE}"
    echo ""
    log "INFO" "CPU Cores: $(nproc 2>/dev/null || echo unknown)" "${BLUE}"
    log "INFO" "Memory: $(free -h 2>/dev/null | awk '/^Mem:/ {print $2}' || echo unknown)" "${BLUE}"
    echo ""
    detect_llvm || true
    echo ""
    detect_libraries
}

clean() {
    log "INFO" "Cleaning ${BUILD_DIR}..." "${YELLOW}"
    rm -rf "${SCRIPT_DIR:?}/${BUILD_DIR}"
    rm -f "${BUILD_LOG}"
}

build() {
    show_config
    log "INFO" "Building in ${BUILD_TYPE} mode with ${THREADS} threads..." "${BLUE}"
    mkdir -p "${SCRIPT_DIR}/${BUILD_DIR}"

    local cmake_cmd="cmake -B ${SCRIPT_DIR}/${BUILD_DIR}"
    cmake_cmd+=" -DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
    cmake_cmd+=" -DCMAKE_C_COMPILER=clang"
    cmake_cmd+=" -DCMAKE_CXX_COMPILER=clang++"
    cmake_cmd+=" -DUSE_CLANG=ON"
    cmake_cmd+=" -DENABLE_LLVM=${ENABLE_LLVM}"
    if [[ "$ENABLE_LLVM" == "ON" ]]; then
        llvm_bin_dir="$(llvm-config --bindir 2>/dev/null || echo "/usr/bin")"
        llvm_base_dir="$(llvm-config --prefix 2>/dev/null || echo "/usr")"
        cmake_cmd+=" -DLLVM_DIR=$llvm_base_dir/lib/cmake/llvm \
        -DCMAKE_C_COMPILER=$llvm_base_dir/bin/clang \
        -DCMAKE_CXX_COMPILER=$llvm_base_dir/bin/clang++ \
        -DCMAKE_LINKER=$llvm_base_dir/bin/ld.lld"
    fi
    cmake_cmd+=" -DENABLE_TESTS=${ENABLE_TESTS}"
    cmake_cmd+=" -DENABLE_HAVEL_LANG=${ENABLE_HAVEL_LANG}"
    cmake_cmd+=" ${SCRIPT_DIR}"

    log "INFO" "CMake command: ${cmake_cmd}" "${YELLOW}"

    if ! eval "${cmake_cmd}" 2>&1 | tee -a "${BUILD_LOG}"; then
        log "ERROR" "CMake configuration failed" "${RED}"
        exit 1
    fi

    if ! cmake --build "${SCRIPT_DIR}/${BUILD_DIR}" -j"${THREADS}" 2>&1 | tee -a "${BUILD_LOG}"; then
        log "ERROR" "Build failed" "${RED}"
        exit 1
    fi

    log "SUCCESS" "Build completed successfully" "${GREEN}"
}

run() {
    local executable="${SCRIPT_DIR}/${BUILD_DIR}/havel"
    if [[ ! -f "$executable" ]]; then
        log "ERROR" "Executable not found: ${executable}" "${RED}"
        exit 1
    fi
    log "INFO" "Running ${executable}..." "${YELLOW}"
    "${executable}" "$@"
}

test_suite() {
    if [[ "$ENABLE_TESTS" != "ON" ]]; then
        log "ERROR" "Tests disabled in mode ${BUILD_MODE}" "${RED}"
        exit 1
    fi
    log "INFO" "Running tests..." "${BLUE}"
    local pass_count=0
    local fail_count=0
    local test_count=0
    while IFS= read -r test_exe; do
        if [[ -f "$test_exe" && -x "$test_exe" ]]; then
            log "INFO" "Running $(basename "$test_exe")..." "${YELLOW}"
            if "$test_exe" 2>&1 | tee -a "${BUILD_LOG}"; then
                ((pass_count++))
            else
                ((fail_count++))
            fi
            ((test_count++))
        fi
    done < <(find "${SCRIPT_DIR}/${BUILD_DIR}" -maxdepth 1 -name 'test_*' -type f -executable 2>/dev/null)
    if [[ $test_count -eq 0 ]]; then
        log "WARNING" "No test executables found" "${YELLOW}"
    else
        log "INFO" "Tests: ${pass_count} passed, ${fail_count} failed (${test_count} total)" "${GREEN}"
    fi
}

usage() {
    echo -e "${CYAN}havel build system${NC}"
    echo ""
    echo -e "${YELLOW}Usage:${NC} $0 [mode] [commands...]"
    echo ""
    echo -e "${YELLOW}Modes:${NC}"
    echo "  0    Debug  + Tests + Havel Lang + LLVM"
    echo "  1    Release + no Tests + no Havel Lang + LLVM"
    echo "  2    Debug  + no Tests + Havel Lang + LLVM"
    echo "  3    Debug  + no Tests + no Havel Lang + no LLVM"
    echo -e "  4    Debug  + Tests + Havel Lang + no LLVM"
    echo "  5    Release + Tests + Havel Lang + LLVM"
    echo -e "  6    Debug  + Tests + Havel Lang + no LLVM     ${GREEN}← default${NC}"
    echo "  7    Release + no Tests + no Havel Lang + no LLVM"
    echo "  8    Debug  + no Tests + Havel Lang + no LLVM"
    echo "  9    Release + Tests + Havel Lang + no LLVM"
    echo "  10   Debug  + no Tests + Havel Lang + LLVM  (build/)"
    echo "  11   Release + no Tests + Havel Lang + LLVM (build/)"
    echo ""
    echo -e "${YELLOW}Commands:${NC}"
    echo "  build      Configure and build"
    echo "  clean      Remove build directory"
    echo "  rebuild    clean + build"
    echo "  run        Run the havel executable"
    echo "  test       Run test suite"
    echo "  all        clean + build + run"
    echo "  detect     Detect system libraries and LLVM"
    echo "  info       Show build configuration"
    echo ""
    echo -e "${YELLOW}Options:${NC}"
    echo "  -h, --help   Show this help"
    echo ""
    echo -e "${YELLOW}Environment:${NC}"
    echo "  THREADS=N    Parallel build jobs (default: auto, currently $(detect_cores))"
    echo ""
    echo -e "${YELLOW}Examples:${NC}"
    echo "  $0                 # mode 6 debug build (default)"
    echo "  $0 build           # mode 6 debug build"
    echo "  $0 rebuild         # clean + build mode 6"
    echo "  $0 6 clean build   # explicit mode 6"
    echo "  $0 9 build         # release no LLVM"
    echo "  $0 0 rebuild       # full debug with LLVM"
    echo "  THREADS=4 $0 build # 4 threads"
    echo ""
    echo -e "${YELLOW}Logs:${NC} ${LOG_DIR}/build-mode[X]-[type].log"
    exit 0
}

process_commands() {
    if [[ $# -eq 0 ]]; then
        build
        return
    fi
    while [[ $# -gt 0 ]]; do
        case "$1" in
            build)   build ;;
            clean)   clean ;;
            rebuild) clean; build ;;
            run)     shift; run "$@"; break ;;
            test)    test_suite ;;
            all)     clean; build; shift; run "$@"; break ;;
            detect|info) detect; show_config ;;
            -h|--help|help) usage ;;
            *) log "ERROR" "Unknown command: $1" "${RED}"; usage ;;
        esac
        shift
    done
}

export LD_LIBRARY_PATH=$OLD_LD_LIBRARY_PATH

process_commands "$@"
