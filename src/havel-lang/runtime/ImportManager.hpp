/*
 * ImportManager.hpp
 * 
 * Script import system for Havel language.
 * Supports: use "file.hv" as alias
 */
#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>

namespace havel {

class Environment;
struct HavelValue;

/**
 * ImportedModule - Represents an imported script module
 */
struct ImportedModule {
    std::string alias;
    std::string filePath;
    std::shared_ptr<Environment> environment;
    std::unordered_map<std::string, HavelValue> exports;
    bool loaded = false;
};

/**
 * ImportManager - Manage script imports
 * 
 * Usage in Havel:
 *   use "gaming.hv" as game
 *   use "work.hv" as work
 *   
 *   game.start()
 *   work.active
 */
class ImportManager {
public:
    ImportManager() = default;
    ~ImportManager() = default;
    
    // Import a script file
    bool importScript(const std::string& filePath, const std::string& alias);
    
    // Get an imported module
    ImportedModule* getModule(const std::string& alias);
    
    // Check if module is imported
    bool isImported(const std::string& alias) const;
    
    // Get all imported modules
    const std::unordered_map<std::string, ImportedModule>& getModules() const;
    
    // Export a value from a module
    void exportValue(const std::string& moduleName, const std::string& name, HavelValue value);
    
    // Get exported value from module
    HavelValue getExportedValue(const std::string& moduleName, const std::string& name);
    
    // Clear all imports (for reload)
    void clear();
    
private:
    std::unordered_map<std::string, ImportedModule> modules;
    mutable std::mutex mutex;
};

} // namespace havel
