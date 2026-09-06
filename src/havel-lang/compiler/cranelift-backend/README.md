# Cranelift backend prototype (TODO.md #24, section 42 step 15)

Lowers validated Havel bytecode (int arithmetic + locals/stack + control
flow subset) to native code via Cranelift 0.121.

- Value words are the C++ NaN-boxed payloads (src/havel-lang/core/Value.hpp);
  the Rust pack/unpack helpers are bit-identical and asserted against
  `Value::rawBits()` in the C test driver.
- Control flow: leader analysis identical to the C++ `reconstruct_cfg`
  (CFGIntegration.hpp) builds one Cranelift block per leader; JUMP and
  JUMP_IF_FALSE lower to branch/jump, fall-through edges become explicit
  jumps, and loops lower to native loops. Locals become SSA variables
  (`declare_var`/`def_var`/`use_var`), so values flow across blocks and
  backedges with the phi insertion done by Cranelift's SSA builder
  (Braun et al. 2013), sealed lazily per the canonical frontend pattern.
- Comparisons (EQ/NEQ/LT/LTE/GT/GTE) and arithmetic (ADD/SUB/MUL) lower
  speculatively: int operands take an inline unboxed path, anything else
  routes through the Runtime ABI bridge (havel_vm_add & co) resolved from
  the embedding process via dlsym - semantics stay in the runtime, never
  duplicated here (TODO #24).
- JUMP_IF_FALSE inlines scalar truthiness (null/bool/int/double, matching
  VM::isTruthy for those shapes) and bridges havel_vm_is_truthy for
  extended-tag values.
- Non-int operand paths emit the bridge call unconditionally and select
  the applicable result - the bridges are pure runtime predicates, so
  this keeps the lowering free of branch-heavy CFG plumbing.

Built opt-in via CMake `ENABLE_CRANELIFT` (OFF by default; requires cargo
with a writable CARGO_HOME). The `cranelift_proto_driver` C test links the
staticlib and runs under ctest when enabled; `cargo test` runs the Rust
side (13 tests: boxing bit-exactness, identity, arithmetic, negative
int48 handling, constant conditions, branches, a summation loop with a
backedge, comparison fast path).

The hclb_* C ABI mirrors the CompilerBackend contract
(src/havel-lang/compiler/core/Backend.hpp); attaching it as a real
CompilerBackend is the follow-up slice once the opcode subset covers
enough of the language (calls, objects) to be worth tiering in.
