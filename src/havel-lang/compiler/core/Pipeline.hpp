#pragma once

#include "BytecodeIR.hpp"
#include "havel-lang/core/PipelineOptions.hpp"
#include "../semantic/LexicalResolver.hpp"
#include "../semantic/TypeChecker.hpp"
#include <functional>
#include <unordered_map>
#include <string>

namespace havel::compiler {

class VM;

struct CompileSnapshot {
  std::string resolver;
  std::string bytecode;
  std::string artifact_path;
};

struct BytecodeSmokeResult {
  Value return_value = nullptr;
  CompileSnapshot snapshot;
};


BytecodeSmokeResult runBytecodePipeline(const std::string &source,
                                        const std::string &entry_function = "__main__");

BytecodeSmokeResult runBytecodePipeline(
    const std::string &source, const std::string &entry_function,
    const PipelineOptions &options);

// Compile source to bytecode chunk without executing.
// fromCache (optional out-param): set true when the chunk was served from
// the .hvc incremental cache instead of a fresh compile — diagnostics only,
// the chunk is semantically identical either way.
std::unique_ptr<BytecodeChunk> compileToBytecodeChunk(
    const std::string &source,
    const std::string &entry_function = "__main__",
    const PipelineOptions &options = PipelineOptions{},
    bool *fromCache = nullptr);

} // namespace havel::compiler
