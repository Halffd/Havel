#include "UIManager.hpp"
#include "ExtensionUIBridge.hpp"
#include "c/ToolkitPlugin.h"
#include "loader/Loader.h"
#include "../screenshot/ScreenshotService.hpp"
#include "../window/AltTabService.hpp"
#include "../clipboard/Clipboard.hpp"

#ifdef HAVE_QT_EXTENSION
#include "QtBackend.hpp"
#include "../../extensions/qt/QtScreenshotBackend.hpp"
#include "../../extensions/qt/QtAltTabBackend.hpp"
#include "../../extensions/qt/QtClipboardBackend.hpp"
#include "../../extensions/qt/QtClipboardManagerBackend.hpp"
#endif

#ifdef HAVE_GTK_BACKEND
#include "GtkBackend.hpp"
#endif

#ifdef HAVE_IMGUI_BACKEND
#include "ImGuiBackend.hpp"
#endif

#ifdef HAVE_QT_EXTENSION
#include "../../extensions/gui/clipboard_manager/ClipboardManager.hpp"
#endif

namespace havel::host {

UIManager& UIManager::instance() {
    static UIManager inst;
    return inst;
}

void UIManager::destroyBackend() {
    if (!backend_) return;
    if (auto fn = backend_->getDestroyFn()) {
        fn(backend_.get());
        backend_.release();
    }
    backend_.reset();
}

bool UIManager::setBackend(UIBackend::Api api) {
    if (api == UIBackend::Api::AUTO) {
        api = detectBestBackend();
    }

    if (currentApi_ == api && backend_) {
        return true;
    }

    if (backend_) {
        backend_->shutdown();
        destroyBackend();
    }

    backend_ = createBackend(api);
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
    return backend_.get();
}

UIBackend::Api UIManager::currentApi() const {
    return currentApi_;
}

std::string UIManager::currentApiName() const {
    if (!backend_) {
        return "none";
    }
    return backend_.get()->getApiName();
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
        backend_.reset();
    }
    initialized_ = false;
    currentApi_ = UIBackend::Api::AUTO;
}

bool UIManager::isInitialized() const {
    return initialized_;
}

std::string UIManager::toolkitNameForApi(UIBackend::Api api) const {
    switch (api) {
    case UIBackend::Api::QT: return "qt";
    case UIBackend::Api::GTK: return "gtk";
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
    if (!toolkit.abi || !toolkit.abi->register_extension_functions) return;
    toolkit.abi->register_extension_functions(nullptr);
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
        installToolkitBackendsInProcess("qt");
        return std::make_unique<QtBackend>();
#else
    {
        auto extBridge = std::make_unique<ExtensionUIBridge>("qt");
        if (extBridge->loadExtension()) {
            return extBridge;
        }
        // Try toolkit .so loading
        HavelLoader *loader = havel_loader_create();
        havel_loader_add_toolkit_paths(loader);
        const HavelToolkitABI *tkAbi = havel_loader_load_toolkit(loader, "qt");
        if (tkAbi && tkAbi->create_ui_backend) {
            loadedToolkitAbi_ = tkAbi;
            installToolkitBackends(tkAbi);
            void *raw = tkAbi->create_ui_backend();
            return std::unique_ptr<UIBackend>(castUIBackend(raw));
        }
        return nullptr;
    }
#endif
    case UIBackend::Api::GTK:
#if defined(HAVE_GTK_BACKEND)
        return std::make_unique<GtkBackend>();
#else
        return nullptr;
#endif
    case UIBackend::Api::IMGUI:
#if defined(HAVE_IMGUI_BACKEND)
        return std::make_unique<ImGuiBackend>();
#else
        return nullptr;
#endif
    default:
        return nullptr;
    }
}

bool UIManager::installToolkitBackends(const HavelToolkitABI *abi) {
    if (!abi) return false;
    bool any = false;

    if (abi->create_screenshot_backend) {
        auto *raw = abi->create_screenshot_backend();
        auto *backend = castScreenshotBackend(raw);
        havel::host::ScreenshotService::getInstance().setBackend(
            std::unique_ptr<havel::host::IScreenshotBackend>(backend));
        any = true;
    }

    if (abi->create_alttab_backend) {
        auto *raw = abi->create_alttab_backend();
        auto *backend = castAltTabBackend(raw);
        // AltTabService needs a setBackend call — but we need to find it
        // If it's registered in ServiceRegistry, get it from there
        any = true;
    }

    if (abi->create_clipboard_backend) {
        auto *raw = abi->create_clipboard_backend();
        auto *backend = castClipboardBackend(raw);
        // Clipboard is typically created per-module, not a singleton
        (void)backend;
        any = true;
    }

    return any;
}

bool UIManager::installToolkitBackendsInProcess(const std::string &toolkitName) {
    if (toolkitName == "qt") {
#ifdef HAVE_QT_EXTENSION
        havel::host::ScreenshotService::getInstance().setBackend(
            std::make_unique<havel::host::QtScreenshotBackend>());
        return true;
#else
        return false;
#endif
    }
    return false;
}

} // namespace havel::host
