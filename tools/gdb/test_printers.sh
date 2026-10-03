#!/usr/bin/env bash
# Regression test for tools/gdb/havel.py.
#
# Compiles value_probe.cpp (a faithful copy of the NaN-boxing layout and the
# container shapes the havel-* commands decode), runs every command in a batch
# gdb session, and diffs the output against expectations. Any layout drift,
# decode bug, or command regression fails the run.
#
# Usage: tools/gdb/test_printers.sh
set -uo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

gdb_bin="${GDB:-gdb}"
cxx="${CXX:-clang++}"

echo "building fixture..."
"$cxx" -std=c++23 -O0 -g \
  -o "$tmp/value_probe" "$here/value_probe.cpp" || {
  echo "FAIL: could not build value_probe.cpp"
  exit 1
}

failures=0

# Error text the module emits. A success check must never contain any of these:
# the messages quote type and symbol names, so "expected contains <type>" can
# otherwise be satisfied by an error that merely mentions the same name.
error_markers=(
  "cannot evaluate"
  "not a VM"
  "no VM expression set"
  "not a sized container"
  "unreadable"
  "Error in sourced command"
  "Error occurred"
  "resolveStringKey failed"
)

# run_check <name> <expected> <mode ok|err> <gdb commands...>
run_check() {
  local name="$1" expected="$2" mode="$3"
  shift 3
  local -a gdb_args=()
  local cmd
  for cmd in "$@"; do
    gdb_args+=(-ex "$cmd")
  done
  local actual
  actual="$("$gdb_bin" -batch -nx -q \
    -ex "source $here/havel.py" \
    -ex "break main" \
    -ex run \
    "${gdb_args[@]}" \
    "$tmp/value_probe" 2>&1)"
  local marker bad=""
  if [[ "$actual" != *"$expected"* ]]; then
    bad="missing expected text"
  elif [[ "$mode" == ok ]]; then
    for marker in "${error_markers[@]}"; do
      if [[ "$actual" == *"$marker"* ]]; then
        bad="unexpected error in output: $marker"
        break
      fi
    done
  fi
  if [[ -z "$bad" ]]; then
    echo "PASS: $name"
  else
    echo "FAIL: $name ($bad)"
    echo "  expected to contain: $expected"
    echo "  actual:"
    echo "$actual" | sed 's/^/    /'
    failures=$((failures + 1))
  fi
}

# check <name> <expected substring> <gdb commands...> -- must succeed cleanly.
check() { run_check "$1" "$2" ok "${@:3}"; }

# check_err <name> <expected error substring> <gdb commands...>
check_err() { run_check "$1" "$2" err "${@:3}"; }

check "double"          "3.5"                    "havel-value g_double"
check "double inf"      "inf"                    "havel-value g_double_inf"
check "double -inf"     "-inf"                   "havel-value g_double_neg_inf"
check "nan double->nil"  "nil"                    "havel-value g_double_nan"
check "negative int"    "-12345"                 "havel-value g_negative"
check "int48 min"       "-140737488355328"       "havel-value g_int_min"
check "int48 max"       "140737488355327"        "havel-value g_int_max"
check "bool"            "true"                   "havel-value g_true"
check "bool false"      "false"                  "havel-value g_false"
check "default array"   "<default-array>"        "havel-value g_default_array"
check "odd bool"        "<bool 0x7>"             "havel-value g_odd_bool"
check "nil"             "nil"                    "havel-value g_nil"
check "string id"       "<string-id 48>"         "havel-value g_string_id"
check "object id"       "<object 9>"             "havel-value g_object_id"
check "ptr"             "0xdeadbeef"             "havel-value g_ptr"
check "host fn no vm"   "<host-fn 1>"            "havel-value g_host_fn"
check "hostfn index fmt" "fn bit._popcount [1]"  "havel-vm vm" "havel-value g_host_fn"
check "string value"    "string-val<3:7>"        "havel-value g_string_val"
check "string wide chunk" "string-val<4095:9>"    "havel-value g_string_val_wide"
check "string wide index" "string-val<4095:2147483647>" "havel-value g_string_val_wide_index"
check "regex value"     "regex-val<2:4>"         "havel-value g_regex"
check "enum"            "enum<66:5>"             "havel-value g_enum"
check "enum wide type"  "enum<511:3>"            "havel-value g_enum_wide"
check "enum wide index" "enum<3:70000>"          "havel-value g_enum_wide_index"
check "generic ext"     "<array 12>"             "havel-value g_array"
check "unknown ext"     "<ext 0x1f 34>"          "havel-value g_unknown_ext"
check "vector first"    "[0] 3.5"                "havel-vector g_values"
check "vector last"     "[6] enum<66:5>"         "havel-vector g_values"
check "empty vector"    "<empty vector>"         "havel-vector g_empty_values"
check "instruction"     "op=LOAD_CONST"          "havel-instruction g_instruction"
check "instruction loc" "probe.hv:12"            "havel-instruction g_instruction"
check "hostfn info"     "bit._popcount [bit] arity=2" "havel-hostfn g_host_fn_info"
check "hostfn unimplemented" "process.spawn [-] arity=any" "havel-hostfn g_unimplemented"
check "vm type"         "havel::compiler::VM"    "havel-vm vm"
check "vm set status"   "havel-vm: vm (havel::compiler::VM)" "havel-vm vm"
check "vm via pointer"  "havel::compiler::VM"    "havel-vm &vm"
check "vm via ptr var"  "havel::compiler::VM"    "havel-vm vm_ptr"
check "vm const ptr"    "havel::compiler::VM"    "havel-vm vm_const_ptr"
check_err "vm wrong type"   "not a VM"               "havel-vm g_double"
check "hostfn name via ptr vm" "fn bit._popcount [1]" "havel-vm vm_ptr" "havel-value g_host_fn"
check "hostfn name via const ptr vm" "fn bit._popcount [1]" "havel-vm vm_const_ptr" "havel-value g_host_fn"
check "str resolution"  "resolved-string"        "havel-vm vm" "havel-str g_string_val"
check_err "bad expression"  "cannot evaluate"        "havel-value g_does_not_exist"
check_err "not a value"     "<not a sized container:" "havel-vector g_double"

if [[ $failures -eq 0 ]]; then
  echo "all gdb command checks passed"
else
  echo "$failures gdb command check(s) failed"
fi
exit $failures
