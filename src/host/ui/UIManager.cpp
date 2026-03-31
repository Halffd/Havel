/*
 * UIManager.cpp - UI backend manager implementation
 */
#include "UIManager.hpp"
#include "ExtensionUIBridge.hpp"

// Include native backends conditionally based on compile-time flags
#ifdef HAVE_GTK_BACKEND
#include "GtkBackend.hpp"
#endif

#ifdef HAVE_IMGUI_BACKEND
#include "ImGuiBackend.hpp"
#endif

namespace havel::host {

UIManager& UIManager::instance() {
    static UIManager instance;
    return instance;
}

bool UIManager::setBackend(UIBackend::Api api) {
    if (api == UIBackend::Api::AUTO) {
        api = detectBestBackend();
    }
    
    // Check if already using this backend
    if (currentApi_ == api && backend_ != nullptr) {
        return true;
    }
    
    // Shutdown current backend
    if (backend_) {
        backend_->shutdown();
        backend_.reset();
    }
    
    // Create new backend
    backend_ = createBackend(api);
    if (!backend_) {
        return false;
    }
    
    // Initialize
    if (!backend_->initialize()) {
        backend_.reset();
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
    if (!backend_) {
        // Initialize with default (try Qt extension first)
        #if defined(HAVE_QT_EXTENSION)
            setBackend(UIBackend::Api::QT);
        #elif defined(HAVE_GTK_BACKEND)
            setBackend(UIBackend::Api::GTK);
        #elif defined(HAVE_IMGUI_BACKEND)
            setBackend(UIBackend::Api::IMGUI);
        #else
            setBackend(UIBackend::Api::QT); // Fallback
        #endif
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
    return backend_->getApiName();
}

bool UIManager::isBackendAvailable(UIBackend::Api api) const {
    switch (api) {
        case UIBackend::Api::QT:
            // Check if Qt extension is available
            {
                auto extBridge = std::make_unique<ExtensionUIBridge>("qt");
                return extBridge->isExtensionLoaded();
            }
        case UIBackend::Api::GTK:
            #if defined(HAVE_GTK_BACKEND)
                return true;
            #else
                return false;
            #endif
        case UIBackend::Api::IMGUI:
            #if defined(HAVE_IMGUI_BACKEND)
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
    // Check Qt extension availability first
    if (isBackendAvailable(UIBackend::Api::QT)) {
        return UIBackend::Api::QT;
    }
    
    #if defined(HAVE_GTK_BACKEND)
        if (isBackendAvailable(UIBackend::Api::GTK)) {
            return UIBackend::Api::GTK;
        }
    #endif
    
    #if defined(HAVE_IMGUI_BACKEND)
        if (isBackendAvailable(UIBackend::Api::IMGUI)) {
            return UIBackend::Api::IMGUI;
        }
    #endif
    
    // Default fallback
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

std::unique_ptr<UIBackend> UIManager::createBackend(UIBackend::Api api) {
    switch (api) {
        case UIBackend::Api::QT:
            // Load Qt extension dynamically
            {
                auto extBridge = std::make_unique<ExtensionUIBridge>("qt");
                if (extBridge->loadExtension()) {
                    return extBridge;
                }
                return nullptr;
            }
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

} // namespace havel::host
