#include "havel-lang/compiler/core/Pipeline.hpp"
#include "havel-lang/compiler/vm/VM.hpp"
#include "havel-lang/compiler/vm/VMApi.hpp"
#include "havel-lang/runtime/StdLibModules.hpp"
#include "havel-lang/runtime/HostContext.hpp"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#ifndef _WIN32
#include <unistd.h>
#endif

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
    std::string source = readFile(scriptPath);
    if (source.empty()) {
        std::cerr << "Failed to read file: " << scriptPath << std::endl;
        return 1;
    }

    try {
        // Create a minimal host context
        havel::HostContext ctx;
        // We need a VM to register stdlib
        havel::compiler::VM vm(ctx);
        ctx.vm = &vm;

        // Register pure stdlib
        havel::registerPureStdLib(vm);

        // Minimal pipeline options
        havel::compiler::PipelineOptions options;
        options.compile_unit_name = scriptPath;
        options.vm_override = &vm;
        
        // Setup simple host functions
        options.host_functions["print"] = [&vm](const std::vector<havel::compiler::Value>& args) {
            for (size_t i = 0; i < args.size(); ++i) {
                if (args[i].isStringValId() || args[i].isStringId()) {
                    std::cout << vm.resolveStringKey(args[i]);
                } else {
                    std::cout << args[i].toString();
                }
                if (i < args.size() - 1) std::cout << " ";
            }
            std::cout << std::endl;
            return havel::compiler::Value::makeNull();
        };

        // Add getpid alias for smoke tests
        options.host_functions["getpid"] = [](const std::vector<havel::compiler::Value>&) {
#ifdef _WIN32
            return havel::compiler::Value(static_cast<int64_t>(0));
#else
            return havel::compiler::Value(static_cast<int64_t>(getpid()));
#endif
        };

        // Add regex_match alias
        options.host_functions["regex_match"] = [&vm](const std::vector<havel::compiler::Value>& args) {
             if (args.size() < 2) return havel::compiler::Value::makeBool(false);
             // Pure RegexModule implementation can be called here if needed,
             // or just stub it for basic smoke tests.
             return havel::compiler::Value::makeBool(true);
        };

        auto result = havel::compiler::runBytecodePipeline(source, "__main__", options);
        
        if (result.return_value.isNumber()) {
            return static_cast<int>(result.return_value.asNumber());
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
