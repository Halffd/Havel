#pragma once

#include <string>
#include <vector>
#include <sstream>

namespace havel::compiler {

// Bytecode disassembler for debugging and analysis
class BytecodeDisassembler {
public:
    struct Options {
        bool showOffsets = true;
        bool showHex = false;
        int indent = 0;
    };

    explicit BytecodeDisassembler(const Options& options)
        : options_(options) {}

    // Disassemble a single instruction
    std::string disassembleInstruction(const uint8_t* code, size_t offset);

    // Disassemble a chunk of bytecode
    std::string disassembleChunk(const uint8_t* code, size_t length, const std::string& name = "");

    // Get disassembly of entire function
    std::string disassembleFunction(const std::string& functionName);

private:
    Options options_;
    std::ostringstream output_;

    void writeInstruction(const std::string& name, const std::vector<std::string>& operands = {});
    void writeOffset(size_t offset);
    void writeHex(const uint8_t* bytes, size_t count);
};

} // namespace havel::compiler
