#include "UIManager.hpp"
#include "loader/Loader.hpp"
#include "loader/ToolkitPlugin.h"

#ifdef HAVE_QT_EXTENSION
#include "QtBackend.hpp"
#endif

#ifdef HAVE_GTK_BACKEND
#include "GtkBackend.hpp"
#endif

#ifdef HAVE_IMGUI_BACKEND
#include "ImGuiBackend.hpp"
#endif

#include <iostream>

namespace havel::host {

UIManager& UIManager::instance() {
    static UIManager inst;
    return inst;
}

void UIManager::destroyBackend() {
    if (!backend_) return;
    if (auto fn = backend_->getDestroyFn()) {
        fn(backend_);
    } else {
        delete backend_;
    }
    backend_ = nullptr;
}

bool UIManager::setBackend(UIBackend::Api api) {
    if (api == UIBackend::Api::AUTO) {
        api = detectBestBackend();
    }

    if (currentApi_ == api && backend_ != nullptr) {
        return true;
    }

    if (backend_) {
        backend_->shutdown();
        destroyBackend();
    }

    auto created = createBackend(api);
    backend_ = created.release();
    if (!backend_) {
        initialized_ = true;
        return false;
    }

    if (!backend_->initialize()) {
        destroyBackend();
        return false;
    }

    currentApi_ = api;
    initialized_ = true;
    return true;
}

bool UIManager::setBackend(const std::string& apiName) {
    if (apiName == "qt" || apiName == "QT" || apiName == "Qt") {
        return setBackend(UIBackend::Api::QT);
    } else if (apiName == "gtk" || apiName == "GTK" || apiName == "Gtk") {
        return setBackend(UIBackend::Api::GTK);
    } else if (apiName == "imgui" || apiName == "IMGUI" || apiName == "ImGui") {
        return setBackend(UIBackend::Api::IMGUI);
    } else if (apiName == "auto" || apiName == "AUTO" || apiName == "Auto") {
        return setBackend(UIBackend::Api::AUTO);
    }
    return false;
}

UIBackend* UIManager::backend() {
    if (!backend_ && !initialized_) {
        initialized_ = true;
        setBackend(detectBestBackend());
    }
    return backend_;
}

UIBackend::Api UIManager::currentApi() const {
    return currentApi_;
}

std::string UIManager::currentApiName() const {
    if (!backend_) {
        return "none";
    }
    return backend_->getApiName();
}

bool UIManager::isBackendAvailable(UIBackend::Api api) const {
    if (tryLoadToolkit(api)) {
        return true;
    }

    switch (api) {
    case UIBackend::Api::QT:
#ifdef HAVE_QT_EXTENSION
        return true;
#else
        return false;
#endif
    case UIBackend::Api::GTK:
#ifdef HAVE_GTK_BACKEND
        return true;
#else
        return false;
#endif
    case UIBackend::Api::IMGUI:
#ifdef HAVE_IMGUI_BACKEND
        return true;
#else
        return false;
#endif
    case UIBackend::Api::AUTO:
        return true;
    }
    return false;
}

bool UIManager::isBackendAvailable(const std::string& apiName) const {
    if (apiName == "qt" || apiName == "QT" || apiName == "Qt") {
        return isBackendAvailable(UIBackend::Api::QT);
    } else if (apiName == "gtk" || apiName == "GTK" || apiName == "Gtk") {
        return isBackendAvailable(UIBackend::Api::GTK);
    } else if (apiName == "imgui" || apiName == "IMGUI" || apiName == "ImGui") {
        return isBackendAvailable(UIBackend::Api::IMGUI);
    }
    return false;
}

UIBackend::Api UIManager::detectBestBackend() const {
    if (isBackendAvailable(UIBackend::Api::QT)) {
        return UIBackend::Api::QT;
    }
    if (isBackendAvailable(UIBackend::Api::GTK)) {
        return UIBackend::Api::GTK;
    }
    if (isBackendAvailable(UIBackend::Api::IMGUI)) {
        return UIBackend::Api::IMGUI;
    }
    return UIBackend::Api::QT;
}

void UIManager::shutdown() {
    if (backend_) {
        backend_->shutdown();
        destroyBackend();
    }
    initialized_ = false;
    currentApi_ = UIBackend::Api::AUTO;
}

bool UIManager::isInitialized() const {
    return initialized_;
}

std::string UIManager::toolkitNameForApi(UIBackend::Api api) const {
    switch (api) {
    case UIBackend::Api::QT:    return "qt";
    case UIBackend::Api::GTK:   return "gtk";
    case UIBackend::Api::IMGUI: return "imgui";
    default: return "";
    }
}

std::optional<ToolkitPlugin> UIManager::tryLoadToolkit(UIBackend::Api api) const {
    std::string name = toolkitNameForApi(api);
    if (name.empty()) return std::nullopt;

    static Loader loader;
    static bool paths_added = false;
    if (!paths_added) {
        loader.addToolkitPaths();
        paths_added = true;
    }

    return loader.loadToolkitPlugin(name);
}

void UIManager::registerToolkitExtensions(const ToolkitPlugin &toolkit) const {
    if (!toolkit.abi || !toolkit.abi->register_extension_functions || !extension_api_) return;
    toolkit.abi->register_extension_functions(extension_api_);
}

std::unique_ptr<UIBackend> UIManager::createBackend(UIBackend::Api api) {
    auto toolkit = tryLoadToolkit(api);
    if (toolkit && toolkit->abi->create_ui_backend && toolkit->abi->destroy_ui_backend) {
        void *raw = toolkit->abi->create_ui_backend();
        if (!raw) return nullptr;

        auto *destroy_fn = toolkit->abi->destroy_ui_backend;
        UIBackend *backend = castUIBackend(raw);
        backend->setDestroyFn(destroy_fn);

        registerToolkitExtensions(*toolkit);

        return std::unique_ptr<UIBackend>(backend);
    }

    switch (api) {
    case UIBackend::Api::QT:
#ifdef HAVE_QT_EXTENSION
        return std::make_unique<QtBackend>();
#else
        return nullptr;
#endif
    case UIBackend::Api::GTK:
#ifdef HAVE_GTK_BACKEND
        return std::make_unique<GtkBackend>();
#else
        return nullptr;
#endif
    case UIBackend::Api::IMGUI:
#ifdef HAVE_IMGUI_BACKEND
        return std::make_unique<ImGuiBackend>();
#else
        return nullptr;
#endif
    default:
        return nullptr;
    }
}

} // namespace havel::host
