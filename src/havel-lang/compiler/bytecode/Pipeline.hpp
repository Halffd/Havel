#pragma once

#include "BytecodeIR.hpp"
#include <string>

namespace havel::compiler {

struct BytecodeSmokeResult {
  BytecodeValue return_value = nullptr;
};

// Parse -> compile -> execute the given source using the Phase 1 bytecode path.
BytecodeSmokeResult runBytecodePipeline(const std::string &source,
                                        const std::string &entry_function = "__main__");

} // namespace havel::compiler
