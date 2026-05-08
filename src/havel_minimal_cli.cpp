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

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: havel_vm <script.hv>" << std::endl;
        return 1;
    }

    std::string scriptPath = argv[1];
    std::cout << "Reading script: " << scriptPath << std::endl;
    std::string source = readFile(scriptPath);
    if (source.empty()) {
        std::cerr << "Failed to read file: " << scriptPath << std::endl;
        return 1;
    }

    try {
        std::cout << "Creating host context..." << std::endl;
        // Create a minimal host context
        havel::HostContext ctx;
        
        std::cout << "Initializing VM..." << std::endl;
        // We need a VM to register stdlib
        havel::compiler::VM vm(ctx);
        ctx.vm = &vm;

        std::cout << "Registering stdlib..." << std::endl;
        // Register pure stdlib
        havel::registerPureStdLib(vm);

        // Minimal pipeline options
        havel::compiler::PipelineOptions options;
        options.compile_unit_name = scriptPath;
        options.vm_override = &vm;
        
        // Setup simple host functions
        options.host_functions["print"] = [](const std::vector<havel::compiler::Value>& args) {
            for (size_t i = 0; i < args.size(); ++i) {
                std::cout << args[i].toString();
                if (i < args.size() - 1) std::cout << " ";
            }
            std::cout << std::endl;
            return havel::compiler::Value::makeNull();
        };

        std::cout << "Running pipeline..." << std::endl;
        auto result = havel::compiler::runBytecodePipeline(source, "__main__", options);
        
        std::cout << "Execution finished." << std::endl;
        if (result.return_value.isNumber()) {
            std::cout << "Return number: " << result.return_value.asNumber() << std::endl;
            return static_cast<int>(result.return_value.asNumber());
        } else if (!result.return_value.isNull()) {
             std::cout << "Return value: " << result.return_value.toString() << std::endl;
        }

    } catch (const std::exception& e) {
        std::cerr << "Caught exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
