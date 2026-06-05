#pragma once

#include "UIBackend.hpp"
#include "loader/Loader.hpp"
#include <memory>
#include <optional>
#include <string>

struct HavelToolkitABI;

namespace havel::host {

class UIManager {
public:
    static UIManager &instance();

    bool setBackend(UIBackend::Api api);
    bool setBackend(const std::string &apiName);

    UIBackend *backend();

    UIBackend::Api currentApi() const;
    std::string currentApiName() const;

    bool isBackendAvailable(UIBackend::Api api) const;
    bool isBackendAvailable(const std::string &apiName) const;

    UIBackend::Api detectBestBackend() const;

    void shutdown();

    bool isInitialized() const;

    void registerToolkitExtensions(const ToolkitPlugin &toolkit) const;

  // Toolkit plugin backend installation
  bool installToolkitBackends(const HavelToolkitABI *abi);
  bool installToolkitBackendsInProcess(const std::string &toolkitName);
  const HavelToolkitABI* loadedToolkitAbi() const { return loadedToolkitAbi_; }

private:
    UIManager() = default;
    ~UIManager() { destroyBackend(); }
    UIManager(const UIManager &) = delete;
    UIManager &operator=(const UIManager &) = delete;

    std::unique_ptr<UIBackend> backend_;
    UIBackend::Api currentApi_ = UIBackend::Api::AUTO;
    bool initialized_ = false;
    const HavelToolkitABI *loadedToolkitAbi_ = nullptr;

    void destroyBackend();

    std::unique_ptr<UIBackend> createBackend(UIBackend::Api api);

    std::string toolkitNameForApi(UIBackend::Api api) const;
    std::optional<ToolkitPlugin> tryLoadToolkit(UIBackend::Api api) const;
};

} // namespace havel::host
