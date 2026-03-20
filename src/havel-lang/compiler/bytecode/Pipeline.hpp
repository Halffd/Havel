#pragma once

#include "BytecodeIR.hpp"
#include "LexicalResolver.hpp"
#include <string>

namespace havel::compiler {

struct CompileSnapshot {
  std::string resolver;
  std::string bytecode;
  std::string artifact_path;
};

struct PipelineOptions {
  std::string compile_unit_name = "unit";
  std::string snapshot_dir;
  bool write_snapshot_artifact = false;
};

struct BytecodeSmokeResult {
  BytecodeValue return_value = nullptr;
  CompileSnapshot snapshot;
};

// Parse -> compile -> execute the given source using the Phase 1 bytecode path.
BytecodeSmokeResult runBytecodePipeline(const std::string &source,
                                        const std::string &entry_function = "__main__");

BytecodeSmokeResult runBytecodePipeline(
    const std::string &source, const std::string &entry_function,
    const PipelineOptions &options);

} // namespace havel::compiler
