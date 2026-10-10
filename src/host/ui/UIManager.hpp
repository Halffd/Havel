#pragma once

#include "UIBackend.hpp"
#include "dl/Loader.hpp"
#include "havel-lang/common/Export.hpp"
#include <memory>
#include <optional>
#include <string>

struct HavelToolkitABI;

namespace havel::host {

// HAVEL_EXPORT: the ui module plugin (havel_mod_ui.so) resolves these
// methods from the main executable at dlopen time. Release builds compile
// with -fvisibility=hidden, so without the explicit attribute the plugin
// fails to load on undefined symbols.
class HAVEL_EXPORT UIManager {
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

    // Force backend choice for the next backend() creation ("qt", "gtk",
    // "imgui", "auto"). Applied lazily: nothing loads until something asks
    // for a backend. An unavailable request falls back to auto detection,
    // so `--ui gtk` on a machine without the GTK toolkit/plugin degrades
    // instead of dying.
    static void setPreferredBackend(const std::string &apiName);
    static std::string preferredBackend();

    void shutdown();

    bool isInitialized() const;

    // Load the Qt toolkit plugin and run its install_factories slot
    // (in-process UI/screenshot/clipboard factories, pixel service, screen
    // provider). Safe to call any number of times: the plugin is dlopen'd at
    // most once per process. Returns the loaded ABI, or nullptr when it is
    // unavailable — the Qt version-skew case, which degrades to a logged
    // message instead of an unbootable binary.
    const HavelToolkitABI *ensureQtToolkit();

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
