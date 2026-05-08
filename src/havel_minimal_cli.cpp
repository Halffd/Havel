#include "havel-lang/compiler/core/Pipeline.hpp"
#include "havel-lang/compiler/vm/VM.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"
#include "havel-lang/runtime/StdLibModules.hpp"
#include "havel-lang/runtime/HostContext.hpp"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>

// Helper to read file content
std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return "";
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

namespace havel {
    // We'll implement a minimal version of this for testing if needed
    // or just use the one from StdLibModules if we can mock HostBridge
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: havel_vm <script.hv>" << std::endl;
        return 1;
    }

    std::string scriptPath = argv[1];
    std::string source = readFile(scriptPath);
    if (source.empty()) {
        std::cerr << "Failed to read file: " << scriptPath << std::endl;
        return 1;
    }

    try {
        // Minimal pipeline options
        havel::compiler::PipelineOptions options;
        options.compile_unit_name = scriptPath;
        
        // Setup simple host functions if needed
        options.host_functions["print"] = [](const std::vector<havel::compiler::Value>& args) {
            for (const auto& arg : args) {
                std::cout << arg.toString() << " ";
            }
            std::cout << std::endl;
            return havel::compiler::Value::makeNull();
        };

        auto result = havel::compiler::runBytecodePipeline(source, "__main__", options);
        
        if (result.return_value.isNumber()) {
            std::cout << "Exit code: " << result.return_value.asNumber() << std::endl;
        } else if (!result.return_value.isNull()) {
            std::cout << "Return value: " << result.return_value.toString() << std::endl;
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
