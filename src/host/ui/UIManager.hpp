#pragma once

#include "UIBackend.hpp"
#include "loader/Loader.hpp"
#include <memory>
#include <optional>
#include <string>

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

    void setExtensionApi(void *api) { extension_api_ = api; }
    void *getExtensionApi() const { return extension_api_; }

    void registerToolkitExtensions(const ToolkitPlugin &toolkit) const;

private:
    UIManager() = default;
    ~UIManager() { destroyBackend(); }
    UIManager(const UIManager &) = delete;
    UIManager &operator=(const UIManager &) = delete;

    UIBackend *backend_ = nullptr;
    UIBackend::Api currentApi_ = UIBackend::Api::AUTO;
    bool initialized_ = false;
    void *extension_api_ = nullptr;

    void destroyBackend();

    std::unique_ptr<UIBackend> createBackend(UIBackend::Api api);

    std::string toolkitNameForApi(UIBackend::Api api) const;
    std::optional<ToolkitPlugin> tryLoadToolkit(UIBackend::Api api) const;
};

} // namespace havel::host
