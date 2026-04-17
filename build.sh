#!/bin/bash

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

SCRIPT_DIR="(cd"(dirname "${BASH_SOURCE[0]}")" && pwd)"

log() {
    local level=1 local message=$2 local color=$3 echo -e " ParseError: Can't use function '$' in math mode at position 21: … local message=$̲2 local col…{color}[level ]{NC} message"|tee − a"{BUILD_LOG:-/dev/null}"
}

detect_cores() {
    local cores
    if cores=(nproc2 >/dev/null)||cores =(sysctl -n hw.ncpu 2>/dev/null) || cores=$(grep -c ^processor /proc/cpuinfo 2>/dev/null); then
        echo "$cores"
    else
        echo 4
    fi
}

detect_libraries() {
    check_lib() {
        local lib=1 if pkg-config --exists "$lib" 2>/dev/null; then local version version= ParseError: Can't use function '$' in math mode at position 35: …nfig --exists "$̲lib" 2>/dev/nul…(pkg-config --modversion "lib"2 >/dev/null||echo"found")echo − e"{GREEN}✓{NC} $lib ($version)" return 0 else echo -e " ParseError: Can't use function '$' in math mode at position 6: {NC} $̲lib ($version)"…{RED}✗${NC} $lib (not found)"
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
        llvm_version=(llvm − config −−version2 >/dev/null||echo"unknown")log"INFO""LLVMfound :{llvm_version}" "GREEN"return0elselog"INFO""LLVMnotfound(JITdisabled)""{YELLOW}"
        return 1
    fi
}

export CC=clang
export CXX=clang++
OLD_LD_LIBRARY_PATH=${LD_LIBRARY_PATH:-}
unset LD_LIBRARY_PATH

Default to mode 6 (debug, no LLVM) if no args or only commands given
if [[ # -eq 0 ]] || [[ "$1" =~ ^(build|clean|rebuild|run|test|all|detect|info|help|--help|-h) ParseError: Expected 'EOF', got '#' at position 1: #̲ -eq 0 ]] || [[… ]]; then
    BUILD_MODE=6
else
    BUILD_MODE=$1
    shift
fi

BUILD_TYPE="Release"
BUILD_DIR="build"
LOG_DIR="logs"
THREADS={THREADS:- ParseError: Expected '}', got 'EOF' at end of input: {THREADS:-(detect_cores)}

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

if [[ "BUILD_MODE" = ^[ 0 − 9 ]+ ]] && [[ -n "{BUILD_CONFIGS[$BUILD_MODE]:-}" ]]; then IFS=',' read -r BUILD_TYPE ENABLE_TESTS ENABLE_HAVEL_LANG ENABLE_LLVM BUILD_DIR <<<" ParseError: Can't use function '$' in math mode at position 16: {BUILD_CONFIGS[$̲BUILD_MODE]:-}"…{BUILD_CONFIGS[$BUILD_MODE]}"

if [[ "$ENABLE_LLVM" == "ON" && "$ENABLE_HAVEL_LANG" == "OFF" ]]; then
    log "WARNING" "LLVM requires Havel Lang - enabling automatically" "${YELLOW}"
    ENABLE_HAVEL_LANG="ON"
fi

else
    log "ERROR" "Invalid build mode: BUILD_MODE""{RED}"
    echo "Valid modes: ${!BUILD_CONFIGS[@]}"
    exit 1
fi

BUILD_LOG="LOG_DIR/build − mode{BUILD_MODE}-BUILD_TYPE,, .log"mkdir − p"{LOG_DIR}"

show_config() {
    log "INFO" "=== BUILD CONFIGURATION ===" "BLUE"log"INFO""Mode :{BUILD_MODE}" "BLUE"log"INFO""Type :{BUILD_TYPE}" "BLUE"log"INFO""Threads :{THREADS}" "BLUE"log"INFO""BuildDir :{BUILD_DIR}" "BLUE"log"INFO""SourceDir :{SCRIPT_DIR}" "BLUE"echo""log"INFO""Features : ""{CYAN}"
    log "INFO" "  Tests: ([[ "$ENABLE_TESTS" == "ON" ]] && echo " ParseError: Can't use function '$' in math mode at position 6: ([[ "$̲ENABLE_TESTS" =…{GREEN}ENABLED${NC}" || echo "REDDISABLED{NC}")" "BLUE"log"INFO""HavelLang :([[ "ENABLE_HAVEL_LANG" == "ON" ]] && echo " ParseError: Expected 'EOF', got '&' at position 31: …NG" == "ON" ]] &̲& echo "{GREEN}ENABLED${NC}" || echo "REDDISABLED{NC}")" "BLUE"log"INFO""LLVMJIT :([[ "ENABLE_LLVM" == "ON" ]] && echo " ParseError: Expected 'EOF', got '&' at position 25: …VM" == "ON" ]] &̲& echo "{GREEN}ENABLED${NC}" || echo "REDDISABLED{NC}")" "${BLUE}"

case $BUILD_MODE in
    0) echo -e "  ${GREEN}→${NC} Standard development build" ;;
    1) echo -e "  ${GREEN}→${NC} Minimal release build" ;;
    2) echo -e "  ${GREEN}→${NC} Quick debug build" ;;
    3) echo -e "  ${GREEN}→${NC} Test-focused development" ;;
    4) echo -e "  ${YELLOW}→${NC} Debug with tests, no LLVM" ;;
    5) echo -e "  ${GREEN}→${NC} Full-featured release" ;;
    6) echo -e "  ${YELLOW}→${NC} Debug without LLVM complexity (default)" ;;
    7) echo -e "  ${YELLOW}→${NC} Lightweight release (no LLVM)" ;;
    8) echo -e "  ${YELLOW}→${NC} Pure language development" ;;
    9) echo -e "  ${YELLOW}→${NC} Feature-complete release (no LLVM)" ;;
esac

}

detect() {
    log "INFO" "=== SYSTEM DETECTION ===" "BLUE"echo""log"INFO""CPUCores :(nproc 2>/dev/null || echo "unknown")" "BLUE"log"INFO""Memory :(free -h 2>/dev/null | awk '/^Mem:/ {print 2}' || echo "unknown")" " ParseError: Expected 'EOF', got '}' at position 2: 2}̲' || echo "unkn…{BLUE}"
    echo ""
    detect_llvm || true
    echo ""
    detect_libraries
}

clean() {
    log "INFO" "Cleaning BUILD_DIR...""{YELLOW}"
    rm -rf "SCRIPT_DIR :?/{BUILD_DIR}"
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
cmake_cmd+=" -DENABLE_TESTS=${ENABLE_TESTS}"
cmake_cmd+=" -DENABLE_HAVEL_LANG=${ENABLE_HAVEL_LANG}"
cmake_cmd+=" ${SCRIPT_DIR}"

log "INFO" "CMake command: ${cmake_cmd}" "${YELLOW}"

if ! eval "${cmake_cmd}" 2>&1 | tee -a "${BUILD_LOG}"; then
    log "ERROR" "CMake configuration failed" "${RED}"
    exit 1
fi

log "INFO" "Building with cmake --build -j${THREADS}..." "${YELLOW}"

if ! cmake --build "${SCRIPT_DIR}/${BUILD_DIR}" -j"${THREADS}" 2>&1 | tee -a "${BUILD_LOG}"; then
    log "ERROR" "Build failed" "${RED}"
    exit 1
fi

log "SUCCESS" "Build completed successfully" "${GREEN}"

}

run() {
    local executable="SCRIPT_DIR/{BUILD_DIR}/havel"

if [[ ! -f "$executable" ]]; then
    log "ERROR" "Executable not found: ${executable}" "${RED}"
    exit 1
fi

log "INFO" "Running ${executable}..." "${YELLOW}"
"${executable}" "$@"

}

test_suite() {
    if [[ "ENABLE_TESTS" != "ON" ]]; thenlog"ERROR""Testsdisabledinmode{BUILD_MODE}" "${RED}"
        exit 1
    fi

log "INFO" "Running tests..." "${BLUE}"

local test_count=0
local pass_count=0
local fail_count=0

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
    log "SUCCESS" "Tests: ${pass_count} passed, ${fail_count} failed (${test_count} total)" "${GREEN}"
fi

}

usage() {
    cat <<EOF
(echo − e"{CYAN}havel build system${NC}")

(echo − e"{YELLOW}Usage:${NC} $0 [mode] [commands...]")

(echo − e"{YELLOW}Modes:NC")0Debug + Tests + HavelLang + LLVM1Release + noTests + noHavelLang + LLVM2Debug + noTests + HavelLang + LLVM3Debug + noTests + noHavelLang + noLLVM4Debug + Tests + HavelLang + noLLVM5Release + Tests + HavelLang + LLVM6Debug + Tests + HavelLang + noLLVM(echo -e "GREEN ← default{NC}")
  7    Release + no Tests + no Havel Lang + no LLVM
  8    Debug  + no Tests + Havel Lang + no LLVM
  9    Release + Tests + Havel Lang + no LLVM
  10   Debug  + no Tests + Havel Lang + LLVM  (build dir: build/)
  11   Release + no Tests + Havel Lang + LLVM (build dir: build/)

(echo − e"{YELLOW}Commands:${NC}")
  build      Configure and build
  clean      Remove build directory
  rebuild    clean + build
  run        Run the havel executable
  test       Run test suite
  all        clean + build + run
  detect     Detect system libraries and LLVM
  info       Show build configuration

(echo − e"{YELLOW}Options:${NC}")
  -h, --help   Show this help

(echo − e"{YELLOW}Environment:NC")THREADS = NParallelbuildjobs(default : auto − detect, currently(detect_cores))

(echo − e"{YELLOW}Examples:${NC}")
  $0                    # mode 6 debug build (default)
  $0 build              # mode 6 debug build
  $0 rebuild            # clean + build mode 6
  $0 6 clean build      # explicit mode 6
  $0 9 build            # release no LLVM
  $0 0 rebuild          # full debug with LLVM
  THREADS=4 $0 build    # use 4 threads

(echo − e"{YELLOW}Logs:NC{LOG_DIR}/build-mode[X]-[type].log")
EOF
    exit 0
}

process_commands() {
    if [[ $# -eq 0 ]]; then
        build
        return
    fi

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
            test_suite
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