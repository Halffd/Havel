#pragma once

#include "BytecodeIR.hpp"
#include "../semantic/LexicalResolver.hpp"
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

struct PipelineOptions {
  std::string compile_unit_name = "unit";
  std::string snapshot_dir;
  bool write_snapshot_artifact = false;
  bool debugBytecode = false;
	std::unordered_map<std::string, BytecodeHostFunction> host_functions;
	VM *vm_override = nullptr;
  std::function<void(VM &)> vm_setup;
  std::function<void(VM *)> system_object_initializer;  // Create system object with proper namespacing
};

struct BytecodeSmokeResult {
  Value return_value = nullptr;
  CompileSnapshot snapshot;
};

// Parse -> compile -> execute the given source using the Phase 1 bytecode path.
BytecodeSmokeResult runBytecodePipeline(const std::string &source,
                                        const std::string &entry_function = "__main__");

BytecodeSmokeResult runBytecodePipeline(
    const std::string &source, const std::string &entry_function,
    const PipelineOptions &options);

} // namespace havel::compiler
