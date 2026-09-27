#include "UIManager.hpp"
#include "UIBackendFactory.hpp"
#include "c/ToolkitPlugin.h"
#include "dl/Loader.h"
#include "../screenshot/ScreenshotService.hpp"
#include "../window/AltTabService.hpp"
#include "../clipboard/Clipboard.hpp"

// GTK and ImGui are not Qt, so they stay constructed here: they compile into
// libhavel_core.a and their headers drag in no Qt symbol. Only the Qt backend
// moved out, into host/ui/UIBackendFactory.hpp.
#ifdef HAVE_GTK_BACKEND
#include "GtkBackend.hpp"
#endif

#ifdef HAVE_IMGUI_BACKEND
#include "ImGuiBackend.hpp"
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
        // Qt availability is a runtime property now: the target that owns Qt
        // registers a factory, so a Qt-free build reports "unavailable" by
        // having no factory rather than by an undefined macro.
        return hasUIBackendFactory(UIBackend::Api::QT);
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
        // The in-process Qt path has always installed the in-process screenshot
        // backend as a side effect of building the UI backend, so keep that.
        installToolkitBackendsInProcess("qt");
        // Constructed by havel_gui, not here: this file is in libhavel_core.a
        // and a Qt-free host must link it without pulling in Qt.
        return createRegisteredUIBackend(UIBackend::Api::QT);
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

bool UIManager::installToolkitBackends(const HavelToolkitABI *abi) {
    // No Qt here: this only calls the toolkit ABI and Qt-free service setters,
    // so it must not be gated on the build having a Qt extension.
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
        havel::AltTabService::instance().setBackend(std::unique_ptr<havel::IAltTabBackend>(backend));
        any = true;
    }

    if (abi->create_clipboard_backend) {
        auto *raw = abi->create_clipboard_backend();
        auto *backend = castClipboardBackend(raw);
        (void)backend;
        any = true;
    }

    return any;
}

bool UIManager::installToolkitBackendsInProcess(const std::string &toolkitName) {
    auto backend = createRegisteredScreenshotBackend(toolkitName);
    if (!backend) {
        return false;
    }
    havel::host::ScreenshotService::getInstance().setBackend(std::move(backend));
    // AltTab backend created lazily on first use (after QApplication exists)
    return true;
}

} // namespace havel::host
