/*
 * ImportManager.hpp - Stubbed (interpreter removed)
 */
#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>

namespace havel {

// Stubbed - interpreter removed
class ImportManager {
public:
    ImportManager() = default;
    ~ImportManager() = default;

    bool importScript(const std::string&, const std::string&) { return false; }
    void* getModule(const std::string&) { return nullptr; }
    bool isImported(const std::string&) const { return false; }
    void clear() {}
};

} // namespace havel
