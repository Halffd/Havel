/* FileModule.cpp - VM-native stdlib module */
#include "FileModule.hpp"

namespace havel::stdlib {

// Implementation moved from header to avoid lambda mangling issues

inline void registerModuleVM(compiler::HostBridge& bridge) {
    auto* vm = bridge.context().vm;
    auto& options = bridge.options();
    
    // Helper: convert BytecodeValue to string
    auto valueToString = [](const compiler::BytecodeValue& v) -> std::string {
        if (std::holds_alternative<std::string>(v)) return std::get<std::string>(v);
        if (std::holds_alternative<int64_t>(v)) return std::to_string(std::get<int64_t>(v));
        if (std::holds_alternative<double>(v)) {
            double val = std::get<double>(v);
            if (val == std::floor(val) && std::abs(val) < 1e15) {
                return std::to_string(static_cast<long long>(val));
            }
            std::ostringstream oss;
            oss.precision(15);
            oss << val;
            return oss.str();
        }
        if (std::holds_alternative<bool>(v)) return std::get<bool>(v) ? "true" : "false";
        return "";
    };
    
    // file.read(path) - Read entire file content
    options.host_functions["file.read"] = [=, [valueToString]bridge, [valueToString]valueToString](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("file.read() requires path");
        std::string path = valueToString(args[0]);
        std::ifstream file(path);
        if (!file.is_open()) throw std::runtime_error("Cannot open file: " + path);
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return compiler::BytecodeValue(content);
    };
    
    // file.write(path, content) - Write content to file
    options.host_functions["file.write"] = [=, [valueToString]bridge, [valueToString]valueToString](const std::vector<compiler::BytecodeValue>& args) {
        if (args.size() < 2) throw std::runtime_error("file.write() requires (path, content)");
        std::string path = valueToString(args[0]);
        std::string content = valueToString(args[1]);
        std::ofstream file(path);
        if (!file.is_open()) throw std::runtime_error("Cannot write to file: " + path);
        file << content;
        return compiler::BytecodeValue(true);
    };
    
    // file.exists(path) - Check if file exists
    options.host_functions["file.exists"] = [=, [valueToString]bridge, [valueToString]valueToString](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("file.exists() requires path");
        std::string path = valueToString(args[0]);
        return compiler::BytecodeValue(std::filesystem::exists(path));
    };
    
    // file.isDir(path) - Check if path is directory
    options.host_functions["file.isDir"] = [=, [valueToString]bridge, [valueToString]valueToString](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("file.isDir() requires path");
        std::string path = valueToString(args[0]);
        return compiler::BytecodeValue(std::filesystem::is_directory(path));
    };
    
    // file.delete(path) - Delete file or directory
    options.host_functions["file.delete"] = [=, [valueToString]bridge, [valueToString]valueToString](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("file.delete() requires path");
        std::string path = valueToString(args[0]);
        std::error_code ec;
        bool removed = std::filesystem::remove(path, ec);
        return compiler::BytecodeValue(removed && !ec);
    };
    
    // file.mkdir(path) - Create directory
    options.host_functions["file.mkdir"] = [=, [valueToString]bridge, [valueToString]valueToString](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("file.mkdir() requires path");
        std::string path = valueToString(args[0]);
        std::error_code ec;
        bool created = std::filesystem::create_directory(path, ec);
        return compiler::BytecodeValue(created && !ec);
    };
    
    // file.list(path) - List directory contents
    options.host_functions["file.list"] = [valueToString, &vm](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("file.list() requires path");
        std::string path = valueToString(args[0]);
        auto arr = ((havel::compiler::VM*)(vm))->createHostArray();
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
            vm.pushHostArrayValue(arr, compiler::BytecodeValue(entry.path().filename().string()));
        }
        return compiler::BytecodeValue(arr);
    };
    
    // file.size(path) - Get file size in bytes
    options.host_functions["file.size"] = [=, [valueToString]bridge, [valueToString]valueToString](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("file.size() requires path");
        std::string path = valueToString(args[0]);
        std::error_code ec;
        auto size = std::filesystem::file_size(path, ec);
        return compiler::BytecodeValue(ec ? static_cast<int64_t>(-1) : static_cast<int64_t>(size));
    };
    
    // file.abs(path) - Get absolute path
    options.host_functions["file.abs"] = [=, [valueToString]bridge, [valueToString]valueToString](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("file.abs() requires path");
        std::string path = valueToString(args[0]);
        std::error_code ec;
        auto abs = std::filesystem::absolute(path, ec);
        return compiler::BytecodeValue(abs.string());
    };
    
    // file.join(path1, path2, ...) - Join path components
    options.host_functions["file.join"] = [=, [valueToString]bridge, [valueToString]valueToString](const std::vector<compiler::BytecodeValue>& args) {
        if (args.empty()) throw std::runtime_error("file.join() requires at least 1 path");
        std::filesystem::path result = valueToString(args[0]);
        for (size_t i = 1; i < args.size(); ++i) {
            result /= valueToString(args[i]);
        }
        return compiler::BytecodeValue(result.string());
    };
    
    // Register file object via vm_setup
    bridge.addVmSetup([](compiler::VM& vm) {
        auto fileObj = vm.createHostObject();
        vm.setHostObjectField(fileObj, "read", compiler::HostFunctionRef{.name = "file.read"});
        vm.setHostObjectField(fileObj, "write", compiler::HostFunctionRef{.name = "file.write"});
        vm.setHostObjectField(fileObj, "exists", compiler::HostFunctionRef{.name = "file.exists"});
        vm.setHostObjectField(fileObj, "isDir", compiler::HostFunctionRef{.name = "file.isDir"});
        vm.setHostObjectField(fileObj, "delete", compiler::HostFunctionRef{.name = "file.delete"});
        vm.setHostObjectField(fileObj, "mkdir", compiler::HostFunctionRef{.name = "file.mkdir"});
        vm.setHostObjectField(fileObj, "list", compiler::HostFunctionRef{.name = "file.list"});
        vm.setHostObjectField(fileObj, "size", compiler::HostFunctionRef{.name = "file.size"});
        vm.setHostObjectField(fileObj, "abs", compiler::HostFunctionRef{.name = "file.abs"});
        vm.setHostObjectField(fileObj, "join", compiler::HostFunctionRef{.name = "file.join"});
        vm.setGlobal("file", fileObj);
    });
}



} // namespace havel::stdlib
