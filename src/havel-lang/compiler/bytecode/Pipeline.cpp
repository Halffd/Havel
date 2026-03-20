#include "Pipeline.hpp"

#include "ByteCompiler.hpp"
#include "VM.hpp"
#include "../../parser/Parser.h"

#include <stdexcept>

namespace havel::compiler {

BytecodeSmokeResult runBytecodePipeline(const std::string &source,
                                        const std::string &entry_function) {
  parser::Parser parser;
  auto program = parser.produceAST(source);
  if (!program) {
    throw std::runtime_error("Bytecode smoke pipeline failed: parser returned null AST");
  }

  ByteCompiler compiler;
  auto chunk = compiler.compile(*program);
  if (!chunk) {
    throw std::runtime_error("Bytecode smoke pipeline failed: compiler returned null chunk");
  }

  VM vm;
  BytecodeSmokeResult result;
  result.return_value = vm.execute(*chunk, entry_function);
  return result;
}

} // namespace havel::compiler
