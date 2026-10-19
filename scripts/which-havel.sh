#!/usr/bin/env bash
# which-havel.sh - report every built havel binary and which one to actually run.
#
# The problem this exists for: a checkout accumulates several build directories
# (build-debug, build-release, build-tsan, build-crane-*, ...) whose binaries
# are built from DIFFERENT commits, and picking the wrong one silently tests
# old code. Two traps seen live:
#
#   1. Running ./build-debug/havel from /host/havel-3 while the user runs
#      /host/havel/build-debug/havel - same filename, different trees.
#   2. Rebuilding only the `havel` target, which does NOT rebuild the module
#      plugins. The binary then loads a stale modules/havel_mod_*.so and the
#      edit under test appears to do nothing.
#
# A third trap is that "newest file" is not "newest code": a build dir can be
# touched more recently while carrying an OLDER commit. So every binary is
# asked what commit it actually is, and that commit is compared against the
# repo's HEAD rather than trusting timestamps.
#
# Usage:
#   scripts/which-havel.sh              table + recommendation
#   scripts/which-havel.sh --json       machine-readable
#   scripts/which-havel.sh --path       print only the recommended path
#
# Exit status:
#   0  a binary matching HEAD was found (stale modules are still reported)
#   1  no binary matches HEAD, or none is runnable - read the table
#   2  no havel binary found at all

set -u

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_DIR" || exit 2

JSON=0
PATH_ONLY=0
for arg in "$@"; do
  case "$arg" in
    --json) JSON=1 ;;
    --path) PATH_ONLY=1 ;;
    -h | --help)
      sed -n '2,30p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
      exit 0
      ;;
    *)
      echo "unknown option: $arg" >&2
      exit 2
      ;;
  esac
done

# --- repo state -------------------------------------------------------------

if git rev-parse --git-dir >/dev/null 2>&1; then
  HEAD_SHA="$(git rev-parse HEAD 2>/dev/null)"
  HEAD_SHORT="$(git rev-parse --short=10 HEAD 2>/dev/null)"
  # dirty = tracked files modified; untracked files do not affect compiled output
  if [ -n "$(git status --porcelain --untracked-files=no 2>/dev/null)" ]; then
    DIRTY="dirty"
  else
    DIRTY="clean"
  fi
else
  HEAD_SHA=""
  HEAD_SHORT="(no git repo)"
  DIRTY="unknown"
fi

# --- collect binaries -------------------------------------------------------

# Every directory that looks like a build dir and holds a havel binary.
shopt -s nullglob
candidates=()
for dir in build-*; do
  [ -d "$dir" ] || continue
  [ -f "$dir/havel" ] && candidates+=("$dir/havel")
done
# Repos often sit next to each other (/host/havel, /host/havel-3); include
# sibling trees so a wrong-tree binary is visible rather than invisible. A
# relative "../havel-3/..." can name THIS tree again, so dedupe on the
# resolved real path rather than on the string.
for sib in ../havel*/build-*/havel; do
  [ -f "$sib" ] && candidates+=("$sib")
done
shopt -u nullglob

deduped=()
declare -A seen_real=()
for bin in "${candidates[@]}"; do
  real="$(readlink -f "$bin" 2>/dev/null || printf '%s' "$bin")"
  [ -n "${seen_real[$real]:-}" ] && continue
  seen_real[$real]=1
  deduped+=("$bin")
done
candidates=("${deduped[@]}")

if [ "${#candidates[@]}" -eq 0 ]; then
  if [ "$JSON" -eq 1 ]; then
    echo '{"error":"no havel binary found"}'
  else
    echo "no havel binary found under $REPO_DIR/build-*" >&2
  fi
  exit 2
fi

# --- per-binary inspection --------------------------------------------------

# Ask the binary what commit it is. Trusting the file mtime is what makes this
# script necessary, so the answer is taken from the binary itself.
probe_commit() {
  local bin="$1" out=""
  out="$(timeout 30 "$bin" --version 2>/dev/null | grep -a -m1 '^commit' || true)"
  [ -n "$out" ] || { printf "unknown"; return; }
  local sha
  sha="$(printf '%s' "$out" | awk '{print $2}')"
  printf '%s' "${sha:-unknown}"
}

# havel_mod_<name>.so is built from src/modules/<name>/. A .so older than its
# own sources is the "I rebuilt but nothing changed" trap.
stale_modules() {
  local bindir="$1" n=0 stale=""
  [ -d "$bindir/modules" ] || { printf "0"; return; }
  for so in "$bindir"/modules/havel_mod_*.so; do
    [ -f "$so" ] || continue
    local name
    name="$(basename "$so" .so)"
    name="${name#havel_mod_}"
    local srcdir="src/modules/$name"
    [ -d "$srcdir" ] || continue
    local newest
    newest="$(find "$srcdir" -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -newer "$so" -print -quit 2>/dev/null)"
    if [ -n "$newest" ]; then
      n=$((n + 1))
      [ -z "$stale" ] && stale="$name"
    fi
  done
  if [ "$n" -gt 0 ]; then
    if [ "$n" -eq 1 ]; then
      printf '1 (%s)' "$stale"
    else
      printf '%d (incl. %s)' "$n" "$stale"
    fi
  else
    printf '0'
  fi
}

matches_head() {
  local c="$1"
  [ -n "$HEAD_SHA" ] || return 1
  [ "$c" = "$HEAD_SHA" ] && return 0
  # a short prefix is still a match
  case "$HEAD_SHA" in
    "$c"*) return 0 ;;
  esac
  return 1
}

is_ancestor() {
  local c="$1"
  [ -n "$HEAD_SHA" ] || return 1
  git merge-base --is-ancestor "$c" HEAD 2>/dev/null
}

rows=""
best=""
best_rank=99

# Newest first: the reader wants the top row to be the newest, but correctness
# never depends on this ordering - the VS HEAD column is what decides.
sorted=()
while IFS=$'\t' read -r _m path; do
  [ -n "$path" ] && sorted+=("$path")
done < <(
  for bin in "${candidates[@]}"; do
    printf '%s\t%s\n' "$(stat -c '%Y' "$bin" 2>/dev/null || echo 0)" "$bin"
  done | sort -rn
)

for bin in "${sorted[@]}"; do
  bindir="$(dirname "$bin")"
  # Label which checkout this binary belongs to. "build-debug/havel" and
  # "/host/havel/build-debug/havel" are the same filename in different trees;
  # that is exactly the mix-up this script exists to prevent.
  case "$bindir" in
    "$REPO_DIR" | ".") tree="(this tree)" ;;
    /*) tree="$bindir" ;;
    *) tree="$(readlink -f "$bindir" 2>/dev/null || printf '%s' "$bindir")" ;;
  esac

  commit="$(probe_commit "$bin")"
  mtime="$(stat -c '%y' "$bin" 2>/dev/null | cut -d. -f1)"
  size="$(du -h "$bin" 2>/dev/null | cut -f1)"
  if [ -x "$bin" ]; then
    runnable="yes"
  else
    runnable="NO"
  fi
  if matches_head "$commit"; then
    rel="HEAD"
    rank=0
  elif is_ancestor "$commit"; then
    rel="ancestor"
    rank=1
  else
    rel="DIVERGED"
    rank=3
  fi
  stale="$(stale_modules "$bindir")"

  # Prefer HEAD, then the most recent mtime among the rest.
  if [ "$runnable" = yes ]; then
    if [ "$rel" = "HEAD" ]; then
      if [ "$best_rank" -gt 0 ]; then
        best="$bin"
        best_rank=0
      fi
    elif [ "$best_rank" -eq 99 ]; then
      best="$bin"
      best_rank=1
    fi
  fi

  rows="${rows}${bin}|${tree}|${mtime}|${size}|${commit}|${rel}|${runnable}|${stale}"$'\n'
done

# --- output -----------------------------------------------------------------

if [ "$PATH_ONLY" -eq 1 ]; then
  [ -n "$best" ] && printf '%s\n' "$best"
  exit 0
fi

if [ "$JSON" -eq 1 ]; then
  printf '{"repo":"%s","head":"%s","head_short":"%s","dirty":"%s","recommended":"%s","binaries":[' \
    "$REPO_DIR" "$HEAD_SHA" "$HEAD_SHORT" "$DIRTY" "$best"
  first=1
  while IFS='|' read -r bin tree mtime size commit rel runnable stale; do
    [ -z "$bin" ] && continue
    [ "$first" -eq 0 ] && printf ','
    first=0
    printf '{"path":"%s","tree":"%s","mtime":"%s","size":"%s","commit":"%s","relation":"%s","executable":"%s","stale_modules":"%s"}' \
      "$bin" "$tree" "$mtime" "$size" "$commit" "$rel" "$runnable" "$stale"
  done <<<"$rows"
  printf ']}\n'
  exit 0
fi

echo "repo:    $REPO_DIR"
echo "HEAD:    $HEAD_SHORT ($DIRTY)"
echo
printf '%-46s %-28s %-19s %-12s %-9s %-5s %s\n' \
  "PATH" "TREE" "BUILT (mtime)" "COMMIT" "VS HEAD" "EXEC" "STALE MOD."
while IFS='|' read -r bin tree mtime size commit rel runnable stale; do
  [ -z "$bin" ] && continue
  short="${commit:0:10}"
  printf '%-46s %-28s %-19s %-12s %-9s %-5s %s\n' \
    "$bin" "$tree" "$mtime" "$short" "$rel" "$runnable" "$stale"
done <<<"$rows"

echo
if [ "$best_rank" -eq 0 ]; then
  echo "USE:  $best"
  echo "      matches HEAD."
elif [ -n "$best" ]; then
  echo "USE:  $best"
  echo "      NO binary matches HEAD ($HEAD_SHORT). This is the closest usable one"
  echo "      and it is an ancestor - it does NOT contain recent commits."
  echo "      Rebuild before trusting results:  cmake --build build-debug -j\$(nproc)"
else
  echo "USE:  (none)"
  echo "      No runnable havel binary was found."
fi

# 0 only when a runnable binary actually matches HEAD. Anything else - an
# ancestor binary, a diverged one, nothing runnable at all - is a non-zero
# exit so a caller cannot mistake "there is a binary somewhere" for "this is
# the binary you want".
[ "$best_rank" -eq 0 ] && exit 0
exit 1
