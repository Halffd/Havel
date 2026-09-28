#pragma once

// ===== CompilationService =====
//
// Named boundary between compiler frontends and the runtime
// (self-hosted split ticket, docs/plans/execution-path-map.md §7-§9).
//
// The contract:
//   source  -> compile -> CompiledUnit
//   CompiledUnit -> load/execute on the VM
//
// "Execution must never require reparsing source" — the CompiledUnit
// carries the BytecodeChunk plus its source identity, so a caller can
// hold it, serialize it, or execute it without touching source again.
//
// Implementations:
//   NativeCompiler    — the C++ pipeline (Parser -> ByteCompiler), the
//                       primary path. Cache-validated via
//                       loadCachedScriptChunk (incremental serve path).
//   SelfHostedCompiler — the Havel-implemented lexer/pratt/emitter run on
//                       the VM (archived; bootstrap-verification only).
//
// The runtime must not care which implementation produced the chunk:
// both return the same CompiledUnit shape.

#include "havel-lang/compiler/core/BytecodeIR.hpp"
#include "havel-lang/compiler/core/Pipeline.hpp"

#include <memory>
#include <optional>
#include <string>

namespace havel::compiler {

/// The boundary object between compiler and runtime.
/// Carries everything a caller needs to execute or serialize a program
/// without re-touching source.
struct CompiledUnit {
    std::unique_ptr<BytecodeChunk> chunk;

    /// Canonical source path the unit was compiled from ("" for stdin/eval).
    std::string sourcePath;

    /// True when served from the .hvc incremental cache instead of a fresh
    /// compile. Diagnostics only — the chunk is semantically identical.
    bool fromCache = false;
};

/// Abstract compiler FRONTEND. Replaceable per the ticket contract:
/// NativeCompiler today, SelfHostedCompiler / JIT / AOT frontends later.
/// Deliberately named CompilerFrontend, NOT CompilerBackend: the existing
/// core/Backend.hpp CompilerBackend is the EXECUTION-backend contract
/// (VM/JIT function compile+execute); this is the source->unit frontend
/// contract. Different axes — neither may redefine the other.
class CompilerFrontend {
public:
    virtual ~CompilerFrontend() = default;

    /// Compile source text to a CompiledUnit. Throws on parse/semantic
    /// errors (same diagnostics as the C++ pipeline).
    virtual CompiledUnit compileSource(const std::string &source,
                                       const std::string &entryFunction,
                                       const PipelineOptions &options) = 0;
};

/// The primary implementation: the C++ pipeline with the incremental
/// serve path (loadCachedScriptChunk) inside compileToBytecodeChunk.
class NativeCompiler : public CompilerFrontend {
public:
    CompiledUnit compileSource(const std::string &source,
                               const std::string &entryFunction,
                               const PipelineOptions &options) override {
        CompiledUnit unit;
        unit.sourcePath = options.compile_unit_name;
        bool fromCache = false;
        unit.chunk = compileToBytecodeChunk(source, entryFunction, options,
                                            &fromCache);
        unit.fromCache = fromCache && unit.chunk != nullptr;
        return unit;
    }
};

} // namespace havel::compiler
