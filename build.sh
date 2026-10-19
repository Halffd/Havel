#!/bin/bash
# build.sh - unified build, test and fleet orchestrator for havel.
#
# One entry point for every build directory in this tree and in sibling
# checkouts (/host/havel, /host/havel-2, ...). Replaces the mode-number-only
# CLI, which could not answer "what is built where, how long does it take,
# and what can I safely delete".
#
# Design rules this script obeys:
#   * Foreground only. Builds and tests run attached and report their real
#     exit status. Nothing is detached and polled (AGENTS.md execution rule).
#   * Never trust a file mtime for "which build is current". Every binary is
#     asked what commit it contains (see cmd_which).
#   * Rebuilding the havel target does not rebuild module plugins. Stale
#     plugins are detected and reported, not assumed away.
#
# Usage summary (full help: build.sh help):
#   build.sh [MODE] build|rebuild|clean|test|run
#   build.sh status            what is built where, and how stale
#   build.sh which             recommended binary (delegates to which-havel.sh)
#   build.sh fleet status|build|test|gc     across all sibling workspaces
#   build.sh jobs|pause|resume|stop         manage running builds/tests
#   build.sh limit show|set                 resource limits
#   build.sh gc [--prune --older-than 14d --yes]   reclaim disk
#   build.sh report              build/test timing averages
#   build.sh doctor              environment check
set -uo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
DIM='\033[2m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
STATE_DIR="${HAVEL_BUILD_STATE:-$HOME/.cache/havel-build}"
TIMES_DB="$STATE_DIR/times.jsonl"
LIMITS_CONF="$STATE_DIR/limits.conf"
LOG_DIR="${LOG_DIR:-$STATE_DIR/logs}"
mkdir -p "$STATE_DIR" "$LOG_DIR" 2>/dev/null

# ---------------------------------------------------------------------------
# limits: defaults, overridable via `limit set`, persisted in limits.conf
# ---------------------------------------------------------------------------
DEFAULT_MAX_PARALLEL=1      # fleet/matrix concurrency (foreground scheduler)
DEFAULT_TIMEOUT=0           # 0 = no timeout
DEFAULT_CCACHE_SIZE="5G"
LIMIT_MAX_PARALLEL="$DEFAULT_MAX_PARALLEL"
LIMIT_TIMEOUT="$DEFAULT_TIMEOUT"
LIMIT_CCACHE_SIZE="$DEFAULT_CCACHE_SIZE"

load_limits() {
  [ -f "$LIMITS_CONF" ] || return 0
  local k v
  while IFS='=' read -r k v; do
    case "$k" in
      max_parallel) LIMIT_MAX_PARALLEL="$v" ;;
      timeout) LIMIT_TIMEOUT="$v" ;;
      ccache_size) LIMIT_CCACHE_SIZE="$v" ;;
    esac
  done < "$LIMITS_CONF"
}

save_limits() {
  cat > "$LIMITS_CONF" <<EOF
max_parallel=$LIMIT_MAX_PARALLEL
timeout=$LIMIT_TIMEOUT
ccache_size=$LIMIT_CCACHE_SIZE
EOF
}
load_limits

say()  { echo -e "$2$1${NC}"; }
warn() { echo -e "${YELLOW}[warn]${NC} $1"; }
err()  { echo -e "${RED}[error]${NC} $1" >&2; }
ok()   { echo -e "${GREEN}[ok]${NC} $1"; }
die()  { err "$1"; exit 1; }

# ---------------------------------------------------------------------------
# timing database: one JSON object per line, appended per run
# ---------------------------------------------------------------------------
record_time() { # workspace mode command seconds rc
  local ws="$1" mode="$2" cmd="$3" secs="$4" rc="$5"
  printf '{"ts":"%s","workspace":"%s","mode":"%s","command":"%s","seconds":%s,"rc":%s}\n' \
    "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$ws" "$mode" "$cmd" "$secs" "$rc" \
    >> "$TIMES_DB" 2>/dev/null
}

# ---------------------------------------------------------------------------
# workspaces: this tree plus siblings that look like havel checkouts
# ---------------------------------------------------------------------------
WORKSPACES=()
FLAGS_JSON=0
FLAGS_DRYRUN=0
OPT_THREADS=""
OPT_MODE=""
OPT_YES=0
OPT_TEST_KIND="gate"

find_workspaces() {
  WORKSPACES=()
  local d
  WORKSPACES+=("$SCRIPT_DIR")
  for d in "$SCRIPT_DIR"/../havel*; do
    [ -d "$d" ] || continue
    d="$(readlink -f "$d")"
    [ "$d" = "$SCRIPT_DIR" ] && continue
    [ -f "$d/CMakeLists.txt" ] || continue
    WORKSPACES+=("$d")
  done
}

ws_name() { basename "$1"; }

# ---------------------------------------------------------------------------
# modes: identical numbering to the previous build.sh, so existing habits and
# docs keep working. Fields: TYPE,TESTS,LANG,LLVM,HEADLESS,TSAN,HVDB,DAP,HVTEST,DIR
# ---------------------------------------------------------------------------
declare -A BUILD_CONFIGS=(
  [0]="Debug,ON,ON,ON,OFF,OFF,ON,ON,ON,build-debug"
  [1]="Release,OFF,OFF,ON,OFF,OFF,ON,ON,ON,build-release"
  [2]="Debug,OFF,ON,ON,OFF,OFF,ON,ON,ON,build-debug"
  [3]="Debug,OFF,OFF,OFF,OFF,OFF,ON,ON,ON,build-debug"
  [4]="Debug,ON,ON,OFF,OFF,OFF,ON,ON,ON,build-debug"
  [5]="Release,ON,ON,ON,OFF,OFF,ON,ON,ON,build-release"
  [6]="Debug,ON,ON,OFF,OFF,OFF,ON,ON,ON,build-debug"
  [7]="Release,OFF,OFF,OFF,OFF,OFF,ON,ON,ON,build-release"
  [8]="Debug,OFF,ON,OFF,OFF,OFF,ON,ON,ON,build-debug"
  [9]="Release,ON,ON,OFF,OFF,OFF,ON,ON,ON,build-release"
  [10]="Debug,OFF,ON,ON,OFF,OFF,ON,ON,ON,build"
  [11]="Release,OFF,ON,ON,OFF,OFF,ON,ON,ON,build"
  [12]="Debug,ON,ON,OFF,ON,OFF,ON,ON,ON,build-headless"
  [13]="Release,OFF,ON,OFF,ON,OFF,ON,ON,ON,build-headless"
  [14]="Debug,OFF,ON,OFF,ON,OFF,ON,ON,ON,build-headless"
  [15]="Release,ON,ON,OFF,ON,OFF,ON,ON,ON,build-headless"
  [16]="Debug,OFF,ON,OFF,OFF,ON,ON,ON,ON,build-tsan"
  [17]="Debug,ON,ON,ON,OFF,OFF,OFF,OFF,ON,build-crane"
  [18]="Debug,ON,ON,OFF,OFF,OFF,OFF,OFF,ON,build-crane-nollvm"
  [19]="Release,ON,ON,ON,OFF,OFF,OFF,OFF,ON,build-crane-release"
  [20]="Release,ON,ON,OFF,OFF,OFF,OFF,OFF,ON,build-nollvm"
)
DEFAULT_MODE=6

resolve_mode() {
  local m="${1:-$DEFAULT_MODE}"
  [ -n "${BUILD_CONFIGS[$m]:-}" ] || die "unknown mode '$m' (valid: $(echo "${!BUILD_CONFIGS[@]}" | tr ' ' '\n' | sort -n | tr '\n' ' '))"
  echo "$m"
}

mode_dir() {
  local m="$1" fields dir
  fields="${BUILD_CONFIGS[$m]}"
  dir="$(cut -d, -f10 <<<"$fields")"
  echo "$1/$2"
}

# ---------------------------------------------------------------------------
# build / test core (single workspace) - mostly the previous logic, which
# encodes several hard-won compiler-resolution fixes. Kept verbatim where the
# comments explain a real failure mode.
# ---------------------------------------------------------------------------
detect_cores() {
  local cores
  if cores=$(nproc 2>/dev/null); then echo "$cores"; else echo 4; fi
}

MAX_AUTODETECT_THREADS=2
resolve_threads() {
  if [ -n "$OPT_THREADS" ]; then echo "$OPT_THREADS"; return; fi
  if [ -n "${THREADS:-}" ]; then echo "$THREADS"; return; fi
  local n; n=$(detect_cores)
  [ "$n" -gt "$MAX_AUTODETECT_THREADS" ] && n=$MAX_AUTODETECT_THREADS
  echo "$n"
}

llvm_tool() {
  local base="$1" tool="$2" p
  for p in "$base/bin/$tool" "/usr/bin/$tool"; do
    [ -x "$p" ] && { echo "$p"; return 0; }
  done
  command -v "$tool" 2>/dev/null
}

ws_build() { # workspace mode threads
  local ws="$1" m="$2" threads="$3"
  local fields type tests lang llvm headless tsan hvdb dap hvtest bdir
  IFS=',' read -r type tests lang llvm headless tsan hvdb dap hvtest bdir <<<"${BUILD_CONFIGS[$m]}"
  local dir="$ws/$bdir"
  mkdir -p "$dir"

  local c_compiler="clang" cxx_compiler="clang++" llvm_base_dir=""
  if [ "$llvm" = "ON" ]; then
    llvm_base_dir="$(llvm-config --prefix 2>/dev/null || echo /usr)"
    c_compiler="$(llvm_tool "$llvm_base_dir" clang)"
    cxx_compiler="$(llvm_tool "$llvm_base_dir" clang++)"
    [ -z "$c_compiler" ] && { err "clang not found for LLVM build"; return 1; }
  fi

  local -a cmake_cmd=(cmake -B "$dir"
    -DCMAKE_BUILD_TYPE="$type"
    -DCMAKE_C_COMPILER="$c_compiler"
    -DCMAKE_CXX_COMPILER="$cxx_compiler"
    -DUSE_CLANG=ON
    -DENABLE_LLVM="$llvm")
  if [ "$llvm" = "ON" ]; then
    cmake_cmd+=(-DLLVM_DIR="$llvm_base_dir/lib/cmake/llvm"
      -DCMAKE_AR="$(llvm_tool "$llvm_base_dir" llvm-ar)"
      -DCMAKE_RANLIB="$(llvm_tool "$llvm_base_dir" llvm-ranlib)")
    local lld; lld="$(llvm_tool "$llvm_base_dir" ld.lld)"
    [ -n "$lld" ] && cmake_cmd+=(-DCMAKE_LINKER="$lld")
  fi
  cmake_cmd+=(
    -DENABLE_TESTS="$tests" -DENABLE_HAVEL_LANG="$lang"
    -DENABLE_HVDB="$hvdb" -DENABLE_HAVEL_DAP="$dap" -DENABLE_HVTEST="$hvtest"
    -DENABLE_HEADLESS="$headless" -DENABLE_TSAN="$tsan")
  if [ "$lang" = "ON" ]; then cmake_cmd+=(-DENABLE_MODULE_PLUGINS=ON); fi
  if [ "$headless" = "ON" ]; then cmake_cmd+=(-DENABLE_QT=OFF -DENABLE_QT_UI_BACKEND=OFF); fi

  say "configure: $(ws_name "$ws") mode $m -> $bdir" "$BLUE"
  [ "$FLAGS_DRYRUN" = 1 ] && { say "  (dry-run) ${cmake_cmd[*]}" "$DIM"; return 0; }

  if ! "${cmake_cmd[@]}" >"$LOG_DIR/configure-$(ws_name "$ws")-$bdir.log" 2>&1; then
    err "configure failed: $(ws_name "$ws") mode $m (log: $LOG_DIR/configure-$(ws_name "$ws")-$bdir.log)"
    return 1
  fi

  say "build: ${threads} threads" "$BLUE"
  local t0 t1 rc
  t0=$(date +%s)
  cmake --build "$dir" -j"$threads" >"$LOG_DIR/build-$(ws_name "$ws")-$bdir.log" 2>&1
  rc=$?
  t1=$(date +%s)
  if [ $rc -ne 0 ]; then
    err "build failed rc=$rc (log: $LOG_DIR/build-$(ws_name "$ws")-$bdir.log)"
    tail -30 "$LOG_DIR/build-$(ws_name "$ws")-$bdir.log" >&2
  else
    ok "build ok in $((t1 - t0))s"
  fi
  return $rc
}

ws_test() { # workspace mode kind
  local ws="$1" m="$2" kind="$3"
  local fields bdir
  IFS=',' read -r _ _ _ _ _ _ _ _ _ bdir <<<"${BUILD_CONFIGS[$m]}"
  local dir="$ws/$bdir"
  [ -x "$dir/hvtest" ] || [ -x "$dir/ctest" ] || {
    err "no test runner in $dir (build mode $m first)"; return 1; }
  local t0 t1 rc logf="$LOG_DIR/test-$(ws_name "$ws")-$bdir-$kind.log"
  t0=$(date +%s)
  case "$kind" in
    gate)   "$dir/hvtest" --smoke --slow-too --timeout 300 ;;
    smoke)  "$dir/hvtest" --smoke --timeout 180 ;;
    ctest)  ctest --test-dir "$dir" --output-on-failure -j"$(resolve_threads)" ;;
    quick)
      local n
      n=$(ctest --test-dir "$dir" -N 2>/dev/null | grep -c '^  Test ')
      [ "${n:-0}" -eq 0 ] && { err "no ctest entries"; return 1; }
      ctest --test-dir "$dir" -j"$(resolve_threads)" -R "config_store_test|window_manager_detector_test" --output-on-failure
      ;;
    *) die "unknown test kind '$kind' (gate|smoke|ctest|quick)" ;;
  esac
  rc=$?
  t1=$(date +%s)
  local col=$GREEN; [ $rc -ne 0 ] && col=$RED
  say "tests rc=$rc in $((t1 - t0))s (log: $logf)" "$col"
  return $rc
}

# ---------------------------------------------------------------------------
# status: one table across all workspaces and build dirs
# ---------------------------------------------------------------------------
dir_size_h() { du -sh "$1" 2>/dev/null | cut -f1; }

binary_commit() {
  timeout 30 "$1" --version 2>/dev/null | grep -a -m1 '^commit' | awk '{print $2}'
}

cmd_status() {
  find_workspaces
  local head_sha head_short dirty
  head_sha="$(git -C "$SCRIPT_DIR" rev-parse HEAD 2>/dev/null || echo "")"
  head_short="$(git -C "$SCRIPT_DIR" rev-parse --short=10 HEAD 2>/dev/null || echo '?')"
  if [ -n "$(git -C "$SCRIPT_DIR" status --porcelain --untracked-files=no 2>/dev/null)" ]; then dirty=dirty; else dirty=clean; fi
  say "repo:   $SCRIPT_DIR" "$CYAN"
  say "HEAD:   $head_short ($dirty)" "$CYAN"
  echo
  printf '%-14s %-28s %-19s %-11s %-9s %-6s %s\n' \
    WORKSPACE BUILD-DIR BUILT COMMIT VS-HEAD SIZE STALE-MOD
  local total_bytes=0
  for ws in "${WORKSPACES[@]}"; do
    local d
    for d in "$ws"/build-*/; do
      [ -f "$d/havel" ] || continue
      local commit rel stale_n behind="-"
      commit="$(binary_commit "$d/havel")"
      commit="${commit:-unknown}"
      if [ -n "$head_sha" ]; then
        if [ "$commit" = "$head_sha" ]; then rel=HEAD; behind=0
        elif git -C "$SCRIPT_DIR" merge-base --is-ancestor "$commit" HEAD 2>/dev/null; then
          rel=ancestor; behind=$(git -C "$SCRIPT_DIR" rev-list --count "$commit"..HEAD 2>/dev/null || echo '?')
        else rel=DIVERGED; fi
      else rel=?; fi
      stale_n=0
      local so name srcdir newest
      for so in "$d"modules/havel_mod_*.so; do
        [ -f "$so" ] || continue
        name="$(basename "$so" .so)"; name="${name#havel_mod_}"
        srcdir="$ws/src/modules/$name"
        [ -d "$srcdir" ] || continue
        newest="$(find "$srcdir" -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -newer "$so" -print -quit 2>/dev/null)"
        [ -n "$newest" ] && stale_n=$((stale_n + 1))
      done
      local sz; sz="$(dir_size_h "$d")"
      printf '%-14s %-28s %-19s %-11s %-9s %-6s %s\n' \
        "$(ws_name "$ws")" "$(basename "$d")" \
        "$(stat -c '%y' "$d/havel" 2>/dev/null | cut -d. -f1)" \
        "${commit:0:10}" "$rel$([ "$behind" != "-" ] && [ "$behind" != "0" ] && echo " (-$behind)")" \
        "$sz" "$stale_n"
    done
  done
  echo
  local gb; gb=$(du -sch "$SCRIPT_DIR"/../havel*/build-*/ 2>/dev/null | tail -1 | cut -f1)
  say "total across workspaces: $gb   (free: $(df -h "$SCRIPT_DIR" | tail -1 | awk '{print $4}'))" "$DIM"
  say "tip: build.sh gc --report for reclaim candidates" "$DIM"
}

cmd_which() {
  local script="$SCRIPT_DIR/scripts/which-havel.sh"
  [ -x "$script" ] || die "scripts/which-havel.sh missing"
  exec "$script" "$@"
}

# ---------------------------------------------------------------------------
# jobs: live process discovery, so pause/resume/stop work on anything running
# (including other agents' builds), without detached-job bookkeeping.
# ---------------------------------------------------------------------------
PATTERNS='cmake|ninja|make|clang|clang\+\+|hvtest|ctest|havel '

cmd_jobs() {
  printf '%-8s %-8s %-9s %-6s %s\n' PID PPID ELAPSED CPU% CMD
  local pid ppid etime pcpu comm
  ps -eo pid,ppid,etime,pcpu,comm,args --sort=-pid 2>/dev/null \
   | awk -v pat="$PATTERNS" '$5 ~ pat {print}' \
   | grep -avE "grep|awk" \
   | while read -r pid ppid etime pcpu comm args; do
       printf '%-8s %-8s %-9s %-6s %s\n' "$pid" "$ppid" "$etime" "$pcpu" "$comm"
     done
}

# Collect a process and its DESCENDANTS. Deliberately not the process group:
# a build started from a shell shares that shell's pgid, so a group-wide
# signal stops the caller's own shell with it. Found the hard way - the first
# version of pause froze the terminal it was run from.
collect_tree() {
  local root="$1" kids p k
  printf '%s\n' "$root"
  kids="$(ps -o pid= --ppid "$root" 2>/dev/null | tr -d ' ')"
  for k in $kids; do collect_tree "$k"; done
}

job_signal() { # pid signal name
  local pid="$1" sig="$2"
  [ -n "$pid" ] || die "usage: build.sh $3 <pid>"
  kill -0 "$pid" 2>/dev/null || die "pid $pid is not running"
  [ "$pid" = "$$" ] && die "refusing to signal my own pid"
  local pids
  pids="$(collect_tree "$pid" | tr '\n' ' ')"
  for p in $pids; do kill "-$sig" "$p" 2>/dev/null || true; done
  case "$sig" in
    STOP) ok "paused pid $pid (+${pids// /,})" ;;
    CONT) ok "resumed pid $pid" ;;
    TERM) ok "stopping pid $pid (SIGTERM)"
          sleep 5
          for p in $pids; do kill -KILL "$p" 2>/dev/null || true; done
          ok "stopped" ;;
  esac
}

cmd_limit() {
  case "${1:-show}" in
    show)
      say "limits ($LIMITS_CONF)" "$CYAN"
      echo "  max_parallel = $LIMIT_MAX_PARALLEL   (fleet/matrix concurrency)"
      echo "  timeout      = $LIMIT_TIMEOUT s       (0 = none)"
      echo "  ccache_size  = $LIMIT_CCACHE_SIZE"
      echo "  threads      = ${OPT_THREADS:-auto, cap $MAX_AUTODETECT_THREADS} (per build; --threads or THREADS= to override)"
      ;;
    set)
      shift || true
      local k="${1:-}" v="${2:-}"
      case "$k" in
        max-parallel|max_parallel) LIMIT_MAX_PARALLEL="$v" ;;
        timeout) LIMIT_TIMEOUT="$v" ;;
        ccache-size|ccache_size) LIMIT_CCACHE_SIZE="$v"; command -v ccache >/dev/null && ccache -M "$v" >/dev/null && ok "ccache max_size=$v" ;;
        *) die "limit set <max-parallel|timeout|ccache-size> <value>" ;;
      esac
      save_limits; ok "saved: $(tr '\n' ' ' < "$LIMITS_CONF")"
      ;;
    *) die "usage: limit [show|set ...]" ;;
  esac
}

# ---------------------------------------------------------------------------
# fleet: run a command in every workspace, sequentially (foreground rule)
# ---------------------------------------------------------------------------
cmd_fleet() {
  local sub="${1:-status}"; shift || true
  find_workspaces
  case "$sub" in
    status) cmd_status ;;
    build|test|rebuild)
      # Call the workspace-scoped functions directly. Re-invoking build.sh
      # would be wrong twice: SCRIPT_DIR is derived from the script's own
      # location, so every child would have configured and built THIS tree's
      # build dir regardless of which workspace it claimed to target, and the
      # --dry-run/--threads flags would not have carried over.
      local m threads rc any_fail=0 t0 t1
      m=$(resolve_mode "$OPT_MODE")
      threads=$(resolve_threads)
      for ws in "${WORKSPACES[@]}"; do
        say "=== $(ws_name "$ws") (mode $m) ===" "$CYAN"
        t0=$(date +%s)
        case "$sub" in
          build)   ws_build "$ws" "$m" "$threads"; rc=$? ;;
          rebuild) rm -rf "$ws/$(cut -d, -f10 <<<"${BUILD_CONFIGS[$m]}")"
                   ws_build "$ws" "$m" "$threads"; rc=$? ;;
          test)    ws_test "$ws" "$m" "$OPT_TEST_KIND"; rc=$? ;;
        esac
        t1=$(date +%s)
        record_time "$(ws_name "$ws")" "$m" "fleet:$sub" $((t1 - t0)) $rc
        [ $rc -ne 0 ] && { any_fail=1; err "$(ws_name "$ws") failed rc=$rc"; }
      done
      if [ $any_fail -eq 0 ]; then ok "fleet $sub: all workspaces ok"; else err "fleet $sub: failures above"; fi
      return $any_fail
      ;;
    gc) shift; cmd_gc "$@" ;;
    *) die "fleet <status|build|test|rebuild|gc>" ;;
  esac
}

# ---------------------------------------------------------------------------
# gc: report + optional pruning / dedupe / ccache maintenance
# ---------------------------------------------------------------------------
sha256_of() { sha256sum "$1" 2>/dev/null | cut -d' ' -f1; }

cmd_gc() {
  local prune=0 older_days=14 do_ccache_zero=0 do_hardlink=0 report_only=1
  while [ $# -gt 0 ]; do
    case "$1" in
      --prune) prune=1 ;;
      --older-than) shift; older_days="${1%%d*}" ;;
      --ccache-zero) do_ccache_zero=1 ;;
      --hardlink) do_hardlink=1 ;;
      --yes) OPT_YES=1 ;;
      --report) report_only=1 ;;
      *) die "gc: unknown option $1" ;;
    esac; shift
  done
  find_workspaces

  say "disk report" "$CYAN"
  local grand=0
  for ws in "${WORKSPACES[@]}"; do
    for d in "$ws"/build-*/; do
      [ -d "$d" ] || continue
      local bytes sz age
      bytes=$(du -sb "$d" 2>/dev/null | cut -f1); grand=$((grand + ${bytes:-0}))
      sz=$(dir_size_h "$d")
      age=$(( ( $(date +%s) - $(stat -c '%Y' "$d") ) / 86400 ))
      printf '  %-12s %-24s %-8s %3dd old\n' "$(ws_name "$ws")" "$(basename "$d")" "$sz" "$age"
      if [ $prune -eq 1 ] && [ "$age" -ge "$older_days" ]; then
        if [ "$OPT_YES" = 1 ]; then
          rm -rf "$d" && ok "pruned $(basename "$d") (${age}d old)"
        else
          warn "would prune $(basename "$d") (${age}d) - rerun with --yes"
        fi
      fi
    done
  done
  printf '  %-38s %s\n' "TOTAL" "$(numfmt --to=iec "$grand" 2>/dev/null || echo "${grand}B")"

  if [ $do_hardlink -eq 1 ]; then
    say "dedupe: identical files across workspaces (hardlinks)" "$CYAN"
    local -A byhash
    local f h
    for f in "$SCRIPT_DIR"/../havel*/build-*/modules/havel_mod_*.so \
             "$SCRIPT_DIR"/../havel*/build-*/toolkits/*.so; do
      [ -f "$f" ] || continue
      h="$(sha256_of "$f")"; [ -z "$h" ] && continue
      if [ -n "${byhash[$h]:-}" ]; then
        local other="${byhash[$h]}"
        if [ "$(stat -c '%d' "$f")" = "$(stat -c '%d' "$other")" ]; then
          if [ "$OPT_YES" = 1 ]; then
            rm -f "$f" && ln "$other" "$f" && ok "hardlinked $(basename "$f")"
          else
            say "  would hardlink: $(basename "$f")  $(du -h "$f" | cut -f1)" "$DIM"
          fi
        fi
      else
        byhash[$h]="$f"
      fi
    done
    [ "$OPT_YES" = 0 ] && say "  (dry-run: add --yes to apply)" "$DIM"
  fi

  if command -v ccache >/dev/null; then
    say "ccache" "$CYAN"
    ccache -s 2>/dev/null | grep -E "Cacheable calls|Hits|Misses|Cache size" | sed 's/^/  /'
    if [ $do_ccache_zero -eq 1 ]; then
      if [ "$OPT_YES" = 1 ]; then ccache -C >/dev/null && ok "ccache cleared"
      else warn "ccache-zero needs --yes"; fi
    fi
    say "  (shared cache dir: all workspaces reuse compiled objects)" "$DIM"
  fi
}

# ---------------------------------------------------------------------------
# report: timing averages from the recorded runs
# ---------------------------------------------------------------------------
cmd_report() {
  [ -f "$TIMES_DB" ] || { warn "no timing data yet ($TIMES_DB)"; return 0; }
  say "build/test timing (from $TIMES_DB)" "$CYAN"
  printf '%-14s %-8s %-8s %5s %8s %8s %8s\n' WORKSPACE MODE COMMAND RUNS AVG-s MIN-s MAX-s
  python3 - "$TIMES_DB" <<'PY'
import json,sys,collections
rows=[json.loads(l) for l in open(sys.argv[1]) if l.strip()]
g=collections.defaultdict(list)
for r in rows: g[(r['workspace'],r['mode'],r['command'])].append(r['seconds'])
for k in sorted(g):
    v=g[k]
    print(f"{k[0]:<14} {k[1]:<8} {k[2]:<8} {len(v):5d} {sum(v)/len(v):8.1f} {min(v):8.1f} {max(v):8.1f}")
allv=[s for v in g.values() for s in v]
if allv: print(f"\noverall: {len(allv)} runs, avg {sum(allv)/len(allv):.1f}s")
PY
}

# ---------------------------------------------------------------------------
# doctor
# ---------------------------------------------------------------------------
cmd_doctor() {
  say "environment" "$CYAN"
  local t
  for t in cmake ninja make clang clang++ llvm-config ccache pkg-config git python3; do
    if command -v "$t" >/dev/null; then printf '  ok   %-12s %s\n' "$t" "$(command -v "$t")"
    else printf '  MISS %-12s\n' "$t"; fi
  done
  say "disk" "$CYAN"; df -h "$SCRIPT_DIR" | tail -1 | sed 's/^/  /'
  say "limits" "$CYAN"; cmd_limit show
  say "timing db" "$CYAN"
  [ -f "$TIMES_DB" ] && echo "  $(wc -l < "$TIMES_DB") recorded runs" || echo "  empty"
}

# ---------------------------------------------------------------------------
# legacy-compatible single-workspace commands
# ---------------------------------------------------------------------------
do_build() {
  local m threads t0 t1
  m=$(resolve_mode "$OPT_MODE"); threads=$(resolve_threads)
  t0=$(date +%s)
  ws_build "$SCRIPT_DIR" "$m" "$threads"; local rc=$?
  t1=$(date +%s)
  record_time "$(ws_name "$SCRIPT_DIR")" "$m" build $((t1 - t0)) $rc
  return $rc
}
do_rebuild() {
  local m; m=$(resolve_mode "$OPT_MODE")
  say "clean + build (mode $m)" "$BLUE"
  rm -rf "$SCRIPT_DIR/$(cut -d, -f10 <<<"${BUILD_CONFIGS[$m]}")"
  do_build
}
do_test() {
  local m; m=$(resolve_mode "$OPT_MODE")
  local t0 t1 rc
  t0=$(date +%s)
  ws_test "$SCRIPT_DIR" "$m" "$OPT_TEST_KIND"; rc=$?
  t1=$(date +%s)
  record_time "$(ws_name "$SCRIPT_DIR")" "$m" "test:$OPT_TEST_KIND" $((t1 - t0)) $rc
  return $rc
}
do_run() {
  local m; m=$(resolve_mode "$OPT_MODE")
  local fields bdir; IFS=',' read -r _ _ _ _ _ _ _ _ _ bdir <<<"${BUILD_CONFIGS[$m]}"
  local bin="$SCRIPT_DIR/$bdir/havel"
  [ -x "$bin" ] || die "$bin missing - build first (build.sh --mode $m build)"
  say "running $bin (commit $(binary_commit "$bin" | cut -c1-10))" "$CYAN"
  exec "$bin" "$@"
}

# ---------------------------------------------------------------------------
# help
# ---------------------------------------------------------------------------
help() {
  cat <<'EOF'
havel unified build system

Usage: build.sh [--mode N] [--threads N] [--json] [--dry-run] <command> [args]

Build lifecycle
  build                configure + build (mode default 6)
  rebuild              clean + build
  clean                remove the build dir for the mode
  run [-- args]        run the built havel binary
Modes: 0-20, same numbering as before (`build.sh 5 build`). Mode table in build.sh.

Tests
  test [--kind gate|smoke|ctest|quick]
       gate  = hvtest --smoke --slow-too --timeout 300   (the pre-merge gate)
       smoke = hvtest --smoke --timeout 180
       ctest = full ctest
       quick = fast gtest subset

Fleet (all sibling workspaces: /host/havel, /host/havel-2, ...)
  fleet status         what is built where, sizes, staleness
  fleet build          build every workspace (--mode N)
  fleet test           test every workspace
  fleet gc             disk report everywhere

Introspection
  status               single-workspace status table
  which                recommended binary (which-havel.sh)
  doctor               toolchain + disk + limits check
  report               timing averages per workspace/mode/command

Resource management
  jobs                 list running build/test processes (all agents)
  pause <pid>          SIGSTOP the process group
  resume <pid>         SIGCONT
  stop <pid>           SIGTERM, then SIGKILL after 5s
  limit show|set <max-parallel|timeout|ccache-size> <v>

Disk
  gc [--report]                       sizes + reclaim candidates
  gc --prune --older-than 14d --yes   delete build dirs idle >= N days
  gc --hardlink --yes                 dedupe identical plugin .so across trees
  gc --ccache-zero --yes              clear the shared compiler cache

Options
  --mode N             build mode
  -j, --threads N      parallel jobs (default: auto, capped at 2)
  --kind K             test kind for `test`
  --yes                apply destructive actions
  --dry-run            print commands, run nothing
  -h, --help           this help

Logs:  ~/.cache/havel-build/logs/
EOF
}

# ---------------------------------------------------------------------------
# argument parsing: global flags, optional leading MODE, then a command
# ---------------------------------------------------------------------------
COMMANDS=(build rebuild clean test run status which fleet jobs pause resume stop limit gc report doctor detect info help)
is_command() { local c; for c in "${COMMANDS[@]}"; do [ "$c" = "$1" ] && return 0; done; return 1; }

POSITIONAL=()
_seen_cmd=0
while [ $# -gt 0 ]; do
  case "$1" in
    --mode|-m) OPT_MODE="$2"; shift 2 ;;
    -j|--threads) OPT_THREADS="$2"; shift 2 ;;
    --kind) OPT_TEST_KIND="$2"; shift 2 ;;
    --json) FLAGS_JSON=1; shift ;;
    --dry-run) FLAGS_DRYRUN=1; shift ;;
    --yes) OPT_YES=1; shift ;;
    -h|--help|help) help; exit 0 ;;
    --*)
      # Flags after the command belong to that command (e.g. `which --path`,
      # `gc --prune`), so pass them through instead of failing the global
      # parse. Before any command they are a typo and should fail loudly.
      if [ "$_seen_cmd" = 1 ]; then POSITIONAL+=("$1"); shift
      else die "unknown option $1 (build.sh help)"; fi ;;
    *)  if [ "$_seen_cmd" = 0 ] && [[ "$1" =~ ^[0-9]+$ ]] && [ -n "${BUILD_CONFIGS[$1]:-}" ]; then
          OPT_MODE="$1"; shift
        else
          is_command "$1" && _seen_cmd=1
          POSITIONAL+=("$1"); shift
        fi ;;
  esac
done

# flags may appear before or after the command; the first positional is it.
if [ ${#POSITIONAL[@]} -eq 0 ]; then do_build; exit $?; fi
CMD="${POSITIONAL[0]}"
shift_arg() { POSITIONAL=("${POSITIONAL[@]:1}"); }
shift_arg
set -- "${POSITIONAL[@]}" 
case "$CMD" in
  build)   do_build ;;
  rebuild) do_rebuild ;;
  clean)   m=$(resolve_mode "$OPT_MODE"); rm -rf "$SCRIPT_DIR/$(cut -d, -f10 <<<"${BUILD_CONFIGS[$m]}")"; ok "removed mode $m build dir" ;;
  test)    do_test ;;
  run)     do_run "$@" ;;
  status)  cmd_status ;;
  which)   cmd_which "$@" ;;
  fleet)   cmd_fleet "$@" ;;
  jobs)    cmd_jobs ;;
  pause)   job_signal "$1" STOP pause ;;
  resume)  job_signal "$1" CONT resume ;;
  stop)    job_signal "$1" TERM stop ;;
  limit)   cmd_limit "$@" ;;
  gc)      cmd_gc "$@" ;;
  report)  cmd_report ;;
  doctor)  cmd_doctor ;;
  detect|info) cmd_doctor ;;
  help)    help ;;
  *)       die "unknown command '$CMD'" ;;
esac
