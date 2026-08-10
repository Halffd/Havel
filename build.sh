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

# Parse ASAN/UBSAN/TSAN flags first (before mode/command detection)
ASAN_LEVEL="default"
ASAN_FULL=false
FSANITIZE_ARGS=()
ENABLE_TSAN_FLAG=false
ASAN_EXPLICITLY_DISABLED=false
UBSAN_EXPLICITLY_DISABLED=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --asanl|--asan-level)
            ASAN_LEVEL="${2:-default}"
            shift 2
            ;;
        --asan-full)
            ASAN_FULL=true
            shift
            ;;
        --fsan|--fsanitize)
            FSANITIZE_ARGS+=("$2")
            shift 2
            ;;
        --tsan|--enable-tsan)
            ENABLE_TSAN_FLAG=true
            shift
            ;;
        --ubsan-full)
            export UBSAN_ALIGNMENT=ON
            export UBSAN_BOOL=ON
            export UBSAN_ENUM=ON
            export UBSAN_FLOAT_CAST_OVERFLOW=ON
            export UBSAN_FLOAT_DIVIDE_BY_ZERO=ON
            export UBSAN_FUNCTION=ON
            export UBSAN_INTEGER=ON
            export UBSAN_NULL=ON
            export UBSAN_POINTER_OVERFLOW=ON
            export UBSAN_RETURN=ON
            export UBSAN_SHIFT=ON
            export UBSAN_SIGNED_INTEGER_OVERFLOW=ON
            export UBSAN_UNREACHABLE=ON
            export UBSAN_VLA_BOUND=ON
            export UBSAN_ABORT_ON_ERROR=ON
            shift
            ;;
        --no-asan)
            ASAN_EXPLICITLY_DISABLED=true
            export ASAN_DETECT_LEAKS=OFF
            export ASAN_DETECT_ODR_VIOLATION=OFF
            export ASAN_DETECT_STACK_USE_AFTER_RETURN=OFF
            export ASAN_DETECT_INITIALIZATION_ORDER_FIASCO=OFF
            export ASAN_ALLOCATOR_MAY_RETURN_NULL=OFF
            export ASAN_ABORT_ON_ERROR=OFF
            shift
            ;;
        --no-ubsan)
            UBSAN_EXPLICITLY_DISABLED=true
            export UBSAN_ABORT_ON_ERROR=OFF
            export UBSAN_ALIGNMENT=OFF
            export UBSAN_BOOL=OFF
            export UBSAN_ENUM=OFF
            export UBSAN_FLOAT_CAST_OVERFLOW=OFF
            export UBSAN_FLOAT_DIVIDE_BY_ZERO=OFF
            export UBSAN_FUNCTION=OFF
            export UBSAN_INTEGER=OFF
            export UBSAN_NULL=OFF
            export UBSAN_POINTER_OVERFLOW=OFF
            export UBSAN_RETURN=OFF
            export UBSAN_SHIFT=OFF
            export UBSAN_SIGNED_INTEGER_OVERFLOW=OFF
            export UBSAN_UNREACHABLE=OFF
            export UBSAN_VLA_BOUND=OFF
            shift
            ;;
        *)
            break
            ;;
    esac
done

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
  [0]="Debug,ON,ON,ON,OFF,OFF,build-debug"
  [1]="Release,OFF,OFF,ON,OFF,OFF,build-release"
  [2]="Debug,OFF,ON,ON,OFF,OFF,build-debug"
  [3]="Debug,OFF,OFF,OFF,OFF,OFF,build-debug"
  [4]="Debug,ON,ON,OFF,OFF,OFF,build-debug"
  [5]="Release,ON,ON,ON,OFF,OFF,build-release"
  [6]="Debug,ON,ON,OFF,OFF,OFF,build-debug"
  [7]="Release,OFF,OFF,OFF,OFF,OFF,build-release"
  [8]="Debug,OFF,ON,OFF,OFF,OFF,build-debug"
  [9]="Release,ON,ON,OFF,OFF,OFF,build-release"
  [10]="Debug,OFF,ON,ON,OFF,OFF,build"
  [11]="Release,OFF,ON,ON,OFF,OFF,build"
  [12]="Debug,ON,ON,OFF,ON,OFF,build-headless"
  [13]="Release,OFF,ON,OFF,ON,OFF,build-headless"
  [14]="Debug,OFF,ON,OFF,ON,OFF,build-headless"
  [15]="Release,ON,ON,OFF,ON,OFF,build-headless"
  [16]="Debug,OFF,ON,OFF,OFF,ON,build-tsan"
)

if [[ "$BUILD_MODE" =~ ^[0-9]+$ ]] && [[ -n "${BUILD_CONFIGS[$BUILD_MODE]:-}" ]]; then
    IFS=',' read -r BUILD_TYPE ENABLE_TESTS ENABLE_HAVEL_LANG ENABLE_LLVM ENABLE_HEADLESS ENABLE_TSAN BUILD_DIR <<<"${BUILD_CONFIGS[$BUILD_MODE]}"
    if [[ "$ENABLE_LLVM" == "ON" && "$ENABLE_HAVEL_LANG" == "OFF" ]]; then
        log "WARNING" "LLVM requires Havel Lang - enabling automatically" "${YELLOW}"
        ENABLE_HAVEL_LANG="ON"
    fi
else
    log "ERROR" "Invalid build mode: $BUILD_MODE" "${RED}"
    echo "Valid modes: ${!BUILD_CONFIGS[@]}"
    exit 1
fi

# Apply ASAN level flags (unless explicitly disabled)
if [[ "$ASAN_EXPLICITLY_DISABLED" != "true" ]]; then
    case "$ASAN_LEVEL" in
        none|off)
            export ASAN_DETECT_LEAKS=OFF
            export ASAN_DETECT_ODR_VIOLATION=OFF
            export ASAN_DETECT_STACK_USE_AFTER_RETURN=OFF
            export ASAN_DETECT_INITIALIZATION_ORDER_FIASCO=OFF
            export ASAN_ALLOCATOR_MAY_RETURN_NULL=OFF
            export ASAN_ABORT_ON_ERROR=OFF
            ;;
        minimal)
            export ASAN_DETECT_LEAKS=ON
            export ASAN_DETECT_ODR_VIOLATION=ON
            export ASAN_DETECT_STACK_USE_AFTER_RETURN=OFF
            export ASAN_DETECT_INITIALIZATION_ORDER_FIASCO=OFF
            export ASAN_ALLOCATOR_MAY_RETURN_NULL=OFF
            export ASAN_ABORT_ON_ERROR=OFF
            ;;
        default|standard)
            export ASAN_DETECT_LEAKS=ON
            export ASAN_DETECT_ODR_VIOLATION=ON
            export ASAN_DETECT_STACK_USE_AFTER_RETURN=ON
            export ASAN_DETECT_INITIALIZATION_ORDER_FIASCO=ON
            export ASAN_ALLOCATOR_MAY_RETURN_NULL=OFF
            export ASAN_ABORT_ON_ERROR=OFF
            ;;
        full|strict)
            export ASAN_DETECT_LEAKS=ON
            export ASAN_DETECT_ODR_VIOLATION=ON
            export ASAN_DETECT_STACK_USE_AFTER_RETURN=ON
            export ASAN_DETECT_INITIALIZATION_ORDER_FIASCO=ON
            export ASAN_ALLOCATOR_MAY_RETURN_NULL=ON
            export ASAN_ABORT_ON_ERROR=ON
            ;;
    esac
fi

# ASAN full preset
if [[ "$ASAN_FULL" == "true" ]]; then
    export ASAN_DETECT_LEAKS=ON
    export ASAN_DETECT_ODR_VIOLATION=ON
    export ASAN_DETECT_STACK_USE_AFTER_RETURN=ON
    export ASAN_DETECT_INITIALIZATION_ORDER_FIASCO=ON
    export ASAN_ALLOCATOR_MAY_RETURN_NULL=ON
    export ASAN_ABORT_ON_ERROR=ON
fi

# Additional fsanitize args
if [[ ${#FSANITIZE_ARGS[@]} -gt 0 ]]; then
    # Join with commas for CMake
    FSAN_JOINED=$(IFS=,; echo "${FSANITIZE_ARGS[*]}")
    export EXTRA_SANITIZERS="$FSAN_JOINED"
fi

# TSAN flag
if [[ "$ENABLE_TSAN_FLAG" == "true" ]]; then
    ENABLE_TSAN="ON"
fi

BUILD_LOG="${LOG_DIR}/build-mode${BUILD_MODE}-${BUILD_TYPE,,}.log"
mkdir -p "${LOG_DIR}"

# Sanitizer configuration (can be overridden via env vars)
ASAN_DETECT_LEAKS=${ASAN_DETECT_LEAKS:-ON}
ASAN_DETECT_ODR_VIOLATION=${ASAN_DETECT_ODR_VIOLATION:-ON}
ASAN_DETECT_STACK_USE_AFTER_RETURN=${ASAN_DETECT_STACK_USE_AFTER_RETURN:-ON}
ASAN_DETECT_INITIALIZATION_ORDER_FIASCO=${ASAN_DETECT_INITIALIZATION_ORDER_FIASCO:-ON}
ASAN_ALLOCATOR_MAY_RETURN_NULL=${ASAN_ALLOCATOR_MAY_RETURN_NULL:-OFF}
ASAN_ABORT_ON_ERROR=${ASAN_ABORT_ON_ERROR:-OFF}
UBSAN_ABORT_ON_ERROR=${UBSAN_ABORT_ON_ERROR:-OFF}
UBSAN_ALIGNMENT=${UBSAN_ALIGNMENT:-ON}
UBSAN_BOOL=${UBSAN_BOOL:-ON}
UBSAN_ENUM=${UBSAN_ENUM:-ON}
UBSAN_FLOAT_CAST_OVERFLOW=${UBSAN_FLOAT_CAST_OVERFLOW:-ON}
UBSAN_FLOAT_DIVIDE_BY_ZERO=${UBSAN_FLOAT_DIVIDE_BY_ZERO:-ON}
UBSAN_FUNCTION=${UBSAN_FUNCTION:-ON}
UBSAN_INTEGER=${UBSAN_INTEGER:-ON}
UBSAN_NULL=${UBSAN_NULL:-ON}
UBSAN_POINTER_OVERFLOW=${UBSAN_POINTER_OVERFLOW:-ON}
UBSAN_RETURN=${UBSAN_RETURN:-ON}
UBSAN_SHIFT=${UBSAN_SHIFT:-ON}
UBSAN_SIGNED_INTEGER_OVERFLOW=${UBSAN_SIGNED_INTEGER_OVERFLOW:-ON}
UBSAN_UNREACHABLE=${UBSAN_UNREACHABLE:-ON}
UBSAN_VLA_BOUND=${UBSAN_VLA_BOUND:-ON}

show_config() {
    log "INFO" "=== BUILD CONFIGURATION ===" "${BLUE}"
    log "INFO" "Mode: ${BUILD_MODE}" "${BLUE}"
    log "INFO" "Type: ${BUILD_TYPE}" "${BLUE}"
    log "INFO" "Threads: ${THREADS}" "${BLUE}"
    log "INFO" "Build Dir: ${SCRIPT_DIR}/${BUILD_DIR}" "${BLUE}"
    log "INFO" "Source Dir: ${SCRIPT_DIR}" "${BLUE}"
    echo ""
  log "INFO" "Features:" "${CYAN}"
  log "INFO" " Tests: $([[ "$ENABLE_TESTS" == "ON" ]] && echo "ENABLED" || echo "DISABLED")" "${BLUE}"
  log "INFO" " Havel Lang: $([[ "$ENABLE_HAVEL_LANG" == "ON" ]] && echo "ENABLED" || echo "DISABLED")" "${BLUE}"
  log "INFO" " LLVM JIT: $([[ "$ENABLE_LLVM" == "ON" ]] && echo "ENABLED" || echo "DISABLED")" "${BLUE}"
  log "INFO" " Headless: $([[ "$ENABLE_HEADLESS" == "ON" ]] && echo "ENABLED (no Qt)" || echo "DISABLED (with Qt)")" "${BLUE}"
  case $BUILD_MODE in
  0) echo -e " ${GREEN}→${NC} Standard development build" ;;
  1) echo -e " ${GREEN}→${NC} Minimal release build" ;;
  2) echo -e " ${GREEN}→${NC} Quick debug build" ;;
  3) echo -e " ${GREEN}→${NC} Test-focused development" ;;
  4) echo -e " ${YELLOW}→${NC} Debug with tests, no LLVM" ;;
  5) echo -e " ${GREEN}→${NC} Full-featured release" ;;
  6) echo -e " ${YELLOW}→${NC} Debug without LLVM (default)" ;;
  7) echo -e " ${YELLOW}→${NC} Lightweight release" ;;
  8) echo -e " ${YELLOW}→${NC} Pure language development" ;;
  9) echo -e " ${YELLOW}→${NC} Feature-complete release" ;;
  12) echo -e " ${YELLOW}→${NC} Debug headless (no Qt/GUI)" ;;
  13) echo -e " ${YELLOW}→${NC} Release headless (no Qt/GUI)" ;;
  14) echo -e " ${YELLOW}→${NC} Debug headless minimal (no Qt/GUI)" ;;
  15) echo -e " ${YELLOW}→${NC} Release headless with tests (no Qt/GUI)" ;;
  16) echo -e " ${YELLOW}→${NC} Debug with ThreadSanitizer (no ASAN)" ;;
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
	if [[ "$ENABLE_HAVEL_LANG" == "ON" ]]; then
		cmake_cmd+=" -DENABLE_MODULE_PLUGINS=ON"
	fi
	cmake_cmd+=" -DENABLE_HEADLESS=${ENABLE_HEADLESS}"
  if [[ "$ENABLE_HEADLESS" == "ON" ]]; then
    cmake_cmd+=" -DENABLE_QT=OFF -DENABLE_QT_UI_BACKEND=OFF"
  fi
  if [[ "$ENABLE_TSAN" == "ON" ]]; then
    cmake_cmd+=" -DENABLE_TSAN=ON"
  fi
  # ASAN/UBSAN configuration (for Debug builds without TSAN)
  if [[ "$BUILD_TYPE" == "Debug" && "$ENABLE_TSAN" != "ON" ]]; then
    cmake_cmd+=" -DASAN_DETECT_LEAKS=${ASAN_DETECT_LEAKS}"
    cmake_cmd+=" -DASAN_DETECT_ODR_VIOLATION=${ASAN_DETECT_ODR_VIOLATION}"
    cmake_cmd+=" -DASAN_DETECT_STACK_USE_AFTER_RETURN=${ASAN_DETECT_STACK_USE_AFTER_RETURN}"
    cmake_cmd+=" -DASAN_DETECT_INITIALIZATION_ORDER_FIASCO=${ASAN_DETECT_INITIALIZATION_ORDER_FIASCO}"
    cmake_cmd+=" -DASAN_ALLOCATOR_MAY_RETURN_NULL=${ASAN_ALLOCATOR_MAY_RETURN_NULL}"
    cmake_cmd+=" -DASAN_ABORT_ON_ERROR=${ASAN_ABORT_ON_ERROR}"
    cmake_cmd+=" -DUBSAN_ABORT_ON_ERROR=${UBSAN_ABORT_ON_ERROR}"
    cmake_cmd+=" -DUBSAN_ALIGNMENT=${UBSAN_ALIGNMENT}"
    cmake_cmd+=" -DUBSAN_BOOL=${UBSAN_BOOL}"
    cmake_cmd+=" -DUBSAN_ENUM=${UBSAN_ENUM}"
    cmake_cmd+=" -DUBSAN_FLOAT_CAST_OVERFLOW=${UBSAN_FLOAT_CAST_OVERFLOW}"
    cmake_cmd+=" -DUBSAN_FLOAT_DIVIDE_BY_ZERO=${UBSAN_FLOAT_DIVIDE_BY_ZERO}"
    cmake_cmd+=" -DUBSAN_FUNCTION=${UBSAN_FUNCTION}"
    cmake_cmd+=" -DUBSAN_INTEGER=${UBSAN_INTEGER}"
    cmake_cmd+=" -DUBSAN_NULL=${UBSAN_NULL}"
    cmake_cmd+=" -DUBSAN_POINTER_OVERFLOW=${UBSAN_POINTER_OVERFLOW}"
    cmake_cmd+=" -DUBSAN_RETURN=${UBSAN_RETURN}"
    cmake_cmd+=" -DUBSAN_SHIFT=${UBSAN_SHIFT}"
    cmake_cmd+=" -DUBSAN_SIGNED_INTEGER_OVERFLOW=${UBSAN_SIGNED_INTEGER_OVERFLOW}"
    cmake_cmd+=" -DUBSAN_UNREACHABLE=${UBSAN_UNREACHABLE}"
    cmake_cmd+=" -DUBSAN_VLA_BOUND=${UBSAN_VLA_BOUND}"
    if [[ -n "${EXTRA_SANITIZERS:-}" ]]; then
        cmake_cmd+=" -DEXTRA_SANITIZERS=${EXTRA_SANITIZERS}"
    fi
  fi
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

    # Build native gamma ramp library for FFI-based brightness module
    local gamma_ramp_c="${SCRIPT_DIR}/modules/app/gamma_ramp.c"
    local gamma_ramp_so="${SCRIPT_DIR}/modules/app/libgamma_ramp.so"
    if [[ -f "$gamma_ramp_c" ]]; then
        log "INFO" "Building native gamma ramp library..." "${BLUE}"
        if gcc -O2 -shared -fPIC -o "$gamma_ramp_so" "$gamma_ramp_c" -lm 2>/dev/null; then
            log "INFO" "  → libgamma_ramp.so built" "${GREEN}"
        else
            log "WARNING" "  → Failed to build libgamma_ramp.so (fallback: Havel loop)" "${YELLOW}"
        fi
    fi

    # Build self-hosted pipeline modules (out/modules/lang/*.hvc)
    local emit_script="${SCRIPT_DIR}/emit_pipeline.sh"
    local havel_bin="${SCRIPT_DIR}/${BUILD_DIR}/havel"
    if [[ -f "$emit_script" && -x "$havel_bin" ]]; then
        log "INFO" "Building self-hosted pipeline modules..." "${BLUE}"
        if bash "$emit_script" "$havel_bin" 2>&1; then
            log "INFO" "  → Self-hosted pipeline built" "${GREEN}"
        else
            log "WARNING" "  → Self-hosted pipeline build failed (fallback: C++ pipeline)" "${YELLOW}"
        fi
    fi
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
    
    # Run CTest (C++ unit tests)
    log "INFO" "Running CTest..." "${YELLOW}"
    if ctest --test-dir "${SCRIPT_DIR}/${BUILD_DIR}" --output-on-failure 2>&1 | tee -a "${BUILD_LOG}"; then
        ((pass_count++))
    else
        ((fail_count++))
    fi
    ((test_count++))
    
    # Run smoke tests via havel (no-self-hosted to avoid self-hosted pipeline hang)
    log "INFO" "Running script smoke tests..." "${YELLOW}"
    local smoke_tests=(
        "${SCRIPT_DIR}/scripts/smoke/arithmetic_add.hv"
        "${SCRIPT_DIR}/scripts/smoke/arithmetic_sub.hv"
        "${SCRIPT_DIR}/scripts/smoke/arithmetic_mul.hv"
        "${SCRIPT_DIR}/scripts/smoke/arithmetic_div.hv"
    )
    for test_file in "${smoke_tests[@]}"; do
        if [[ -f "$test_file" ]]; then
            log "INFO" "Running $(basename "$test_file")..." "${YELLOW}"
            if "${SCRIPT_DIR}/${BUILD_DIR}/havel" --no-self-hosted "$test_file" 2>&1 | grep -q "FAIL"; then
                log "ERROR" "FAIL: $(basename "$test_file")" "${RED}"
                ((fail_count++))
            else
                log "INFO" "PASS: $(basename "$test_file")" "${GREEN}"
                ((pass_count++))
            fi
            ((test_count++))
        fi
    done
    
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
  echo " 10 Debug + no Tests + Havel Lang + LLVM (build/)"
  echo " 11 Release + no Tests + Havel Lang + LLVM (build/)"
  echo ""
  echo -e "${YELLOW}Headless Modes (no Qt/GUI):${NC}"
  echo " 12 Debug + Tests + Havel Lang + no LLVM + Headless"
  echo " 13 Release + no Tests + Havel Lang + no LLVM + Headless"
  echo " 14 Debug + no Tests + Havel Lang + no LLVM + Headless"
  echo " 15 Release + Tests + Havel Lang + no LLVM + Headless"
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
    echo "  THREADS=N           Parallel build jobs (default: auto, currently $(detect_cores))"
    echo ""
    echo -e "${YELLOW}ASAN/UBSAN Flags:${NC}"
    echo "  --asanl, --asan-level LEVEL    ASAN level: none|minimal|default|full|strict"
    echo "  --asan-full                    Enable all ASAN checks (strict preset)"
    echo "  --fsanitize, --fsan SANITIZERS  Extra sanitizers (comma-separated: address,undefined,thread,memory)"
    echo "  --tsan, --enable-tsan          Enable ThreadSanitizer"
    echo "  --ubsan-full                   Enable all UBSAN checks"
    echo "  --no-asan                      Disable all ASAN checks"
    echo "  --no-ubsan                     Disable all UBSAN checks"
    echo ""
    echo -e "${YELLOW}ASAN/UBSAN Environment (Debug builds only):${NC}"
    echo "  ASAN_DETECT_LEAKS=ON|OFF"
    echo "  ASAN_DETECT_ODR_VIOLATION=ON|OFF"
    echo "  ASAN_DETECT_STACK_USE_AFTER_RETURN=ON|OFF"
    echo "  ASAN_DETECT_INITIALIZATION_ORDER_FIASCO=ON|OFF"
    echo "  ASAN_ALLOCATOR_MAY_RETURN_NULL=ON|OFF"
    echo "  ASAN_ABORT_ON_ERROR=ON|OFF"
    echo "  UBSAN_ABORT_ON_ERROR=ON|OFF"
    echo "  UBSAN_ALIGNMENT=ON|OFF"
    echo "  UBSAN_BOOL=ON|OFF"
    echo "  UBSAN_ENUM=ON|OFF"
    echo "  UBSAN_FLOAT_CAST_OVERFLOW=ON|OFF"
    echo "  UBSAN_FLOAT_DIVIDE_BY_ZERO=ON|OFF"
    echo "  UBSAN_FUNCTION=ON|OFF"
    echo "  UBSAN_INTEGER=ON|OFF"
    echo "  UBSAN_NULL=ON|OFF"
    echo "  UBSAN_POINTER_OVERFLOW=ON|OFF"
    echo "  UBSAN_RETURN=ON|OFF"
    echo "  UBSAN_SHIFT=ON|OFF"
    echo "  UBSAN_SIGNED_INTEGER_OVERFLOW=ON|OFF"
    echo "  UBSAN_UNREACHABLE=ON|OFF"
    echo "  UBSAN_VLA_BOUND=ON|OFF"
    echo "  ENABLE_TSAN=ON       Enable ThreadSanitizer (mode 16)"
    echo ""
    echo -e "${YELLOW}Runtime (ASAN_OPTIONS):${NC}"
    echo "  ASAN_OPTIONS=halt_on_error=1:detect_leaks=0:allocator_may_return_null=1"
    echo ""
    echo -e "${YELLOW}Examples:${NC}"
    echo "  $0                 # mode 6 debug build (default)"
    echo "  $0 build           # mode 6 debug build"
    echo "  $0 rebuild         # clean + build mode 6"
    echo "  $0 6 clean build   # explicit mode 6"
    echo "  $0 9 build         # release no LLVM"
  echo " $0 0 rebuild # full debug with LLVM"
  echo " $0 12 build # headless debug (no Qt)"
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
