#include "havel-lang/compiler/runtime/DebugUtils.hpp"
#include "havel-lang/compiler/runtime/RuntimeSupport.hpp"
#include <iostream>
#include <fstream>
#include <vector>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: hvdump <file.hvc>" << std::endl;
        return 1;
    }
    std::string path = argv[1];
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open: " << path << std::endl;
        return 1;
    }
    
    // Read the whole file
    std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    
    // Deserialize bytecode chunk
    havel::compiler::ValueSerializer serializer;
    auto chunk = serializer.deserializeChunk(buffer);
    if (!chunk) {
        std::cerr << "Failed to deserialize chunk" << std::endl;
        return 1;
    }
    
    havel::compiler::BytecodeDisassembler d(*chunk);
    havel::compiler::BytecodeDisassembler::Options opts;
    
    std::cout << d.disassemble(opts) << std::endl;
    return 0;
}
