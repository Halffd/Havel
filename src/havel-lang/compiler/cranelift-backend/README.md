# Cranelift backend prototype (TODO.md #24, section 42 step 15)

Lowers validated Havel bytecode (int arithmetic + locals/stack subset) to
native code via Cranelift 0.121.

- Value words are the C++ NaN-boxed payloads (src/havel-lang/core/Value.hpp);
  the Rust pack/unpack helpers are bit-identical and asserted against
  `Value::rawBits()` in the C test driver.
- Non-int operands route through the Runtime ABI bridge (havel_vm_add,
  havel_vm_lt) resolved via dlsym from the embedding process - semantics
  stay in the runtime, never duplicated here (TODO #24).
- Built opt-in via CMake `ENABLE_CRANELIFT` (OFF by default; requires cargo).
  The `cranelift_backend_test` C test links the staticlib and verifies the
  C ABI (hclb_*) against C++ Value bits, then compiles and runs `a+b`,
  constant folds `(2+3)<10 -> true`, and sign handling `-40 + -40 -> -80`.
- The hclb_* surface mirrors the CompilerBackend contract
  (src/havel-lang/compiler/core/Backend.hpp); attaching it as a real
  CompilerBackend is the follow-up slice once the lowering covers enough
  opcodes to be worth tiering in.
