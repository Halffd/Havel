#pragma once

#include <string>
#include <vector>
#include <memory>

namespace havel {

class ModuleLoader {
public:
    struct LoadedModule {
        std::string name;
        std::string path;
        std::string source;
    };

    ModuleLoader() = default;
    ~ModuleLoader() = default;

    std::shared_ptr<LoadedModule> loadModule(const std::string& name);
    std::shared_ptr<LoadedModule> loadModuleFromPath(const std::string& path);
    std::vector<std::string> searchPaths() const;
    void addSearchPath(const std::string& path);
};

} // namespace havel
