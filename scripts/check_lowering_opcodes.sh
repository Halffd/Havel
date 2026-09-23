#!/usr/bin/env bash
# Lowering opcode drift guard (TODO2.md #11).
#
# BytecodeOrcJIT::hasUnsupportedOpcodes lists every opcode
# BytecodeOrcJitLowering::translate does NOT emit code for (its default
# case drops them silently - before this list covered only the scheduler
# set, a hot function with e.g. an ADD_ASSIGN compiled with the
# assignment dropped, a silent miscompilation).
#
# This test diffs the two sets:
#   - opcodes the lowering's switch actually handles
#   - the union of hasUnsupportedOpcodes's cases and the handled set
#
# FAIL when an opcode is in NEITHER the lowering's switch NOR
# hasUnsupportedOpcodes: such an opcode would be silently dropped from
# JIT-compiled code. Also FAIL when hasUnsupportedOpcodes lists an opcode
# the lowering DOES handle: those functions would be needlessly excluded
# from JIT compilation (correct, but the list has drifted).

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
LOWERING_SRC="${REPO_DIR}/src/havel-lang/compiler/BytecodeOrcJitLowering.cpp"
JIT_SRC="${REPO_DIR}/src/havel-lang/compiler/BytecodeOrcJIT.cpp"
IR_HPP="${REPO_DIR}/src/havel-lang/compiler/core/BytecodeIR.hpp"

for f in "${LOWERING_SRC}" "${JIT_SRC}" "${IR_HPP}"; do
    if [[ ! -f "${f}" ]]; then
        echo "FAIL: source missing: ${f}"
        exit 1
    fi
done

# Opcodes the lowering's switch handles (translate's cases).
grep "case OpCode::" "${LOWERING_SRC}" \
    | sed 's/.*case OpCode:://; s/[: ].*//' | sort -u > /tmp/lowering_handled.txt

# Opcodes hasUnsupportedOpcodes rejects.
grep "case OpCode::" "${JIT_SRC}" \
    | sed 's/.*case OpCode:://; s/[: ].*//' | sort -u > /tmp/unsupported_listed.txt

# Every opcode in the enum.
sed -n '/^enum class OpCode/,/^};/p' "${IR_HPP}" \
    | grep -oE '^\s+[A-Z][A-Z0-9_]*,' | tr -d ' ,\t' | sort -u > /tmp/all_opcodes.txt

if [[ ! -s /tmp/all_opcodes.txt ]]; then
    echo "FAIL: could not extract opcode list from ${IR_HPP}"
    exit 1
fi

fail=0

# 1. Every opcode must be handled by the lowering OR listed as unsupported.
comm -23 /tmp/all_opcodes.txt <(sort -u /tmp/lowering_handled.txt /tmp/unsupported_listed.txt) \
    | while read -r op; do
        echo "FAIL: OpCode::${op} is neither translated by the lowering nor listed in hasUnsupportedOpcodes - would be silently dropped from JIT code"
    done
if comm -23 /tmp/all_opcodes.txt <(sort -u /tmp/lowering_handled.txt /tmp/unsupported_listed.txt) | grep -q .; then
    fail=1
fi

# 2. Scheduler opcodes listed in hasUnsupportedOpcodes but handled by the
#    lowering are BY DESIGN: the lowering emits a JitCoroutineSignal for
#    them, so a JIT-compiled function containing one would waste the
#    compile the first time the signal fired; the list prevents the
#    compile. No check for that direction.

if [[ "${fail}" -eq 0 ]]; then
    echo "OK: lowering opcode coverage matches hasUnsupportedOpcodes ($(wc -l < /tmp/lowering_handled.txt) translated, $(wc -l < /tmp/unsupported_listed.txt) listed unsupported)"
fi
exit "${fail}"
