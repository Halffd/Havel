/* FileModule.cpp - VM-native stdlib module */
#include "FileModule.hpp"

using havel::compiler::BytecodeValue;
using havel::compiler::VMApi;

namespace havel::stdlib {

// Register file module with VMApi (stable API layer)
void registerFileModule(VMApi& api) {
    // readTextFile(path) - Read text file content
    api.registerFunction("readTextFile", [](const std::vector<BytecodeValue>& args) {
        if (args.empty())
            throw std::runtime_error("readTextFile() requires a file path");
        
        if (!std::holds_alternative<std::string>(args[0]))
            throw std::runtime_error("readTextFile() requires a string path");
        
        const auto& path = std::get<std::string>(args[0]);
        
        std::ifstream file(path);
        if (!file.is_open())
            throw std::runtime_error("Cannot open file: " + path);
        
        std::ostringstream content;
        content << file.rdbuf();
        file.close();
        
        return BytecodeValue(content.str());
    });
    
    // writeTextFile(path, content) - Write text file
    api.registerFunction("writeTextFile", [](const std::vector<BytecodeValue>& args) {
        if (args.size() < 2)
            throw std::runtime_error("writeTextFile() requires path and content");
        
        if (!std::holds_alternative<std::string>(args[0]) || !std::holds_alternative<std::string>(args[1]))
            throw std::runtime_error("writeTextFile() requires string arguments");
        
        const auto& path = std::get<std::string>(args[0]);
        const auto& content = std::get<std::string>(args[1]);
        
        std::ofstream file(path);
        if (!file.is_open())
            throw std::runtime_error("Cannot create file: " + path);
        
        file << content;
        file.close();
        
        return BytecodeValue(true);
    });
    
    // fileExists(path) - Check if file exists
    api.registerFunction("fileExists", [](const std::vector<BytecodeValue>& args) {
        if (args.empty())
            throw std::runtime_error("fileExists() requires a file path");
        
        if (!std::holds_alternative<std::string>(args[0]))
            throw std::runtime_error("fileExists() requires a string path");
        
        const auto& path = std::get<std::string>(args[0]);
        return BytecodeValue(std::filesystem::exists(path));
    });
    
    // fileSize(path) - Get file size in bytes
    api.registerFunction("fileSize", [](const std::vector<BytecodeValue>& args) {
        if (args.empty())
            throw std::runtime_error("fileSize() requires a file path");
        
        if (!std::holds_alternative<std::string>(args[0]))
            throw std::runtime_error("fileSize() requires a string path");
        
        const auto& path = std::get<std::string>(args[0]);
        
        if (!std::filesystem::exists(path))
            return BytecodeValue(static_cast<int64_t>(0));
        
        return BytecodeValue(static_cast<int64_t>(std::filesystem::file_size(path)));
    });
    
    // deleteFile(path) - Delete a file
    api.registerFunction("deleteFile", [](const std::vector<BytecodeValue>& args) {
        if (args.empty())
            throw std::runtime_error("deleteFile() requires a file path");
        
        if (!std::holds_alternative<std::string>(args[0]))
            throw std::runtime_error("deleteFile() requires a string path");
        
        const auto& path = std::get<std::string>(args[0]);
        
        if (!std::filesystem::exists(path))
            return BytecodeValue(false);
        
        return BytecodeValue(std::filesystem::remove(path));
    });
    
    // Register file object
    auto fileObj = api.makeObject();
    api.setField(fileObj, "readTextFile", api.makeFunctionRef("readTextFile"));
    api.setField(fileObj, "writeTextFile", api.makeFunctionRef("writeTextFile"));
    api.setField(fileObj, "fileExists", api.makeFunctionRef("fileExists"));
    api.setField(fileObj, "fileSize", api.makeFunctionRef("fileSize"));
    api.setField(fileObj, "deleteFile", api.makeFunctionRef("deleteFile"));
    api.setGlobal("File", fileObj);
}

} // namespace havel::stdlib
