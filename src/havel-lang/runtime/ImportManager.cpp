/*
 * ImportManager.cpp
 * 
 * Script import system implementation.
 */
#include "ImportManager.hpp"
#include "Environment.hpp"
#include "Interpreter.hpp"
#include "utils/Logger.hpp"
#include <fstream>
#include <filesystem>

namespace havel {

bool ImportManager::importScript(const std::string& filePath, const std::string& alias) {
    std::lock_guard<std::mutex> lock(mutex);
    
    // Check if already imported
    if (modules.find(alias) != modules.end()) {
        warn("Module '{}' already imported", alias);
        return false;
    }
    
    // Check if file exists
    std::ifstream file(filePath);
    if (!file.good()) {
        error("Import file not found: {}", filePath);
        return false;
    }
    file.close();
    
    // Create module entry
    ImportedModule module;
    module.alias = alias;
    module.filePath = filePath;
    module.environment = std::make_shared<Environment>();
    module.loaded = false;
    
    // Read and execute the script in its own environment
    std::ifstream scriptFile(filePath);
    std::string sourceCode((std::istreambuf_iterator<char>(scriptFile)),
                            std::istreambuf_iterator<char>());
    scriptFile.close();
    
    info("Importing script: {} as '{}'", filePath, alias);
    
    // TODO: Execute script in module environment
    // For now, just mark as loaded
    module.loaded = true;
    
    modules[alias] = std::move(module);
    
    return true;
}

ImportedModule* ImportManager::getModule(const std::string& alias) {
    std::lock_guard<std::mutex> lock(mutex);
    
    auto it = modules.find(alias);
    if (it != modules.end()) {
        return &it->second;
    }
    
    return nullptr;
}

bool ImportManager::isImported(const std::string& alias) const {
    std::lock_guard<std::mutex> lock(mutex);
    return modules.find(alias) != modules.end();
}

const std::unordered_map<std::string, ImportedModule>& ImportManager::getModules() const {
    return modules;
}

void ImportManager::exportValue(const std::string& moduleName, const std::string& name, HavelValue value) {
    std::lock_guard<std::mutex> lock(mutex);
    
    auto it = modules.find(moduleName);
    if (it != modules.end()) {
        it->second.exports[name] = std::move(value);
    } else {
        error("Cannot export to unknown module: {}", moduleName);
    }
}

HavelValue ImportManager::getExportedValue(const std::string& moduleName, const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex);
    
    auto it = modules.find(moduleName);
    if (it != modules.end()) {
        auto expIt = it->second.exports.find(name);
        if (expIt != it->second.exports.end()) {
            return expIt->second;
        }
    }
    
    return HavelValue(nullptr);
}

void ImportManager::clear() {
    std::lock_guard<std::mutex> lock(mutex);
    modules.clear();
}

} // namespace havel
