/*
 * UIManager.cpp - UI backend manager implementation
 */
#include "UIManager.hpp"
#include "QtBackend.hpp"
#include "ExtensionUIBridge.hpp"

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
    if (apiName == "qt" || apiName == "QT") {
        return setBackend(UIBackend::Api::QT);
    } else if (apiName == "gtk" || apiName == "GTK") {
        return setBackend(UIBackend::Api::GTK);
    } else if (apiName == "imgui" || apiName == "IMGUI" || apiName == "ImGui") {
        return setBackend(UIBackend::Api::IMGUI);
    } else if (apiName == "auto" || apiName == "AUTO") {
        return setBackend(UIBackend::Api::AUTO);
    }
    return false;
}

UIBackend* UIManager::backend() {
    if (!backend_) {
        // Initialize with default (QT)
        setBackend(UIBackend::Api::QT);
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
            // Qt is always available in this build
            return true;
        case UIBackend::Api::GTK:
            // Try to load extension to check availability
            {
                auto extBridge = std::make_unique<ExtensionUIBridge>("gtk");
                return extBridge->loadExtension();
            }
        case UIBackend::Api::IMGUI:
            // Try to load extension to check availability
            {
                auto extBridge = std::make_unique<ExtensionUIBridge>("imgui");
                return extBridge->loadExtension();
            }
        case UIBackend::Api::AUTO:
            return true;
    }
    return false;
}

bool UIManager::isBackendAvailable(const std::string& apiName) const {
    if (apiName == "qt" || apiName == "QT") {
        return isBackendAvailable(UIBackend::Api::QT);
    } else if (apiName == "gtk" || apiName == "GTK") {
        return isBackendAvailable(UIBackend::Api::GTK);
    } else if (apiName == "imgui" || apiName == "IMGUI" || apiName == "ImGui") {
        return isBackendAvailable(UIBackend::Api::IMGUI);
    }
    return false;
}

UIBackend::Api UIManager::detectBestBackend() const {
    // Default to Qt
    if (isBackendAvailable(UIBackend::Api::QT)) {
        return UIBackend::Api::QT;
    }
    // Fallback order: GTK -> ImGui
    if (isBackendAvailable(UIBackend::Api::GTK)) {
        return UIBackend::Api::GTK;
    }
    if (isBackendAvailable(UIBackend::Api::IMGUI)) {
        return UIBackend::Api::IMGUI;
    }
    return UIBackend::Api::QT; // Default even if unavailable
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
            return std::make_unique<QtBackend>();
        case UIBackend::Api::GTK:
            // Load GTK extension backend
            {
                auto extBridge = std::make_unique<ExtensionUIBridge>("gtk");
                if (extBridge->loadExtension()) {
                    return extBridge;
                }
                return nullptr;
            }
        case UIBackend::Api::IMGUI:
            // Load ImGui extension backend
            {
                auto extBridge = std::make_unique<ExtensionUIBridge>("imgui");
                if (extBridge->loadExtension()) {
                    return extBridge;
                }
                return nullptr;
            }
        default:
            return nullptr;
    }
}

} // namespace havel::host
