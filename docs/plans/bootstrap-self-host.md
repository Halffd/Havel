---
plan name: bootstrap-self-host
plan description: Self-hosted compiler bootstrap
plan status: active
---

## Idea
Replace C++ bootstrap compiler with self-hosted Havel compiler in modules/lang/. The C++ parser/lexer/compiler are frozen (read-only). All new compiler work happens in .hv files. Deletion criteria: lexer.hv tokenizes itself → delete Lexer.cpp; pratt.hv parses itself → delete Parser.cpp; emitter.hv emits correct bytecode for itself → delete ByteCompiler.cpp. VM, GC, FFI, host modules stay C++ permanently.

## Implementation
- Assess current state: run modules/lang/ tests, identify what passes and what fails
- Fix lexer.hv to tokenize itself (produce same token stream as C++ Lexer.cpp for lexer.hv)
- Fix pratt.hv to parse itself (produce valid AST for all modules/lang/*.hv)
- Fix emitter.hv to emit correct bytecode for all modules/lang/*.hv
- End-to-end test: modules/lang/ compiles itself via havel VM, output runs correctly
- Delete Parser.cpp + split files once pratt.hv self-parses
- Delete Lexer.cpp once lexer.hv self-tokenizes
- Delete ByteCompiler.cpp + split files once emitter.hv self-emits

## Required Specs
<!-- SPECS_START -->
- remove-call-host
- bootstrap-compiler
- conditional-hotkeys
<!-- SPECS_END -->