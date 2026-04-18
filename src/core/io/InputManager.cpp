#include "InputManager.hpp"
#include "HotkeyExecutor.hpp"
#include "utils/Logger.hpp"

namespace havel {

InputManager::InputManager() = default;

InputManager::~InputManager() {
    Stop();
}

bool InputManager::Start(InputBackendType backend) {
    if (running_.load()) {
        warn("InputManager already running");
        return false;
    }

    if (backend == InputBackendType::Unknown) {
        backend = InputBackend::DetectBestBackend();
    }

    backend_ = InputBackend::Create(backend);
    if (!backend_) {
        error("InputManager: Failed to create backend {}", static_cast<int>(backend));
        return false;
    }

    if (!backend_->Init()) {
        error("InputManager: Failed to initialize backend");
        return false;
    }

    backend_->SetKeyCallback([this](const KeyEvent &e) { ProcessKeyEvent(e); });
    backend_->SetMouseCallback([this](const MouseEvent &e) { ProcessMouseEvent(e); });

    auto devices = backend_->EnumerateDevices();
    for (const auto &dev : devices) {
        if (!backend_->OpenDevice(dev.path)) {
            warn("InputManager: Failed to open device {}", dev.path);
        }
    }

    running_.store(true);
    eventThread_ = std::thread(&InputManager::EventLoop, this);

    info("InputManager: Started with {} backend", backend_->GetName());
    return true;
}

void InputManager::Stop() {
    if (!running_.load()) return;

    running_.store(false);

    if (eventThread_.joinable()) {
        eventThread_.join();
    }

    if (backend_) {
        backend_->Shutdown();
    }
}

void InputManager::SetBackend(InputBackendType type) {
    if (running_.load()) {
        warn("InputManager: Cannot change backend while running");
        return;
    }

    backend_ = InputBackend::Create(type);
}

InputBackendType InputManager::GetBackendType() const {
    return backend_ ? backend_->GetType() : InputBackendType::Unknown;
}

bool InputManager::GrabInput() {
    if (!backend_ || !backend_->SupportsGrab()) {
        return false;
    }

    auto devices = backend_->EnumerateDevices();
    for (const auto &dev : devices) {
        if (!backend_->GrabDevice(dev.path)) {
            warn("InputManager: Failed to grab device {}", dev.path);
        }
    }

    grabbed_.store(true);
    return true;
}

void InputManager::UngrabInput() {
    if (backend_) {
        backend_->UngrabAllDevices();
    }
    grabbed_.store(false);
}

bool InputManager::GetKeyState(uint32_t code) const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    auto it = keyStates_.find(code);
    return it != keyStates_.end() && it->second;
}

uint32_t InputManager::GetModifiers() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return modifiers_;
}

std::pair<int, int> InputManager::GetMousePosition() const {
    return backend_ ? backend_->GetMousePosition() : std::make_pair(0, 0);
}

void InputManager::SetKeyCallback(InputBackend::KeyCallback cb) {
    if (backend_) {
        backend_->SetKeyCallback(std::move(cb));
    }
}

void InputManager::SetMouseCallback(InputBackend::MouseCallback cb) {
    if (backend_) {
        backend_->SetMouseCallback(std::move(cb));
    }
}

void InputManager::RegisterHotkey(int id, uint32_t code, uint32_t modifiers, HotkeyCallback cb) {
    std::lock_guard<std::mutex> lock(hotkeyMutex_);
    HotkeyInfo info;
    info.code = code;
    info.modifiers = modifiers;
    info.callback = std::move(cb);
    hotkeys_[id] = std::move(info);
}

void InputManager::UnregisterHotkey(int id) {
    std::lock_guard<std::mutex> lock(hotkeyMutex_);
    hotkeys_.erase(id);
}

void InputManager::EventLoop() {
    while (running_.load()) {
        if (backend_) {
            backend_->PollEvents(100);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void InputManager::ProcessKeyEvent(const KeyEvent &event) {
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        keyStates_[event.code] = event.down;
        modifiers_ = event.modifiers;
    }

    EvaluateHotkeys(event.code, event.down, event.modifiers);
}

void InputManager::ProcessMouseEvent(const MouseEvent &event) {
}

bool InputManager::EvaluateHotkeys(uint32_t code, bool down, uint32_t modifiers) {
    if (!down) return false;

    std::vector<std::pair<int, HotkeyCallback>> matched;

    {
        std::lock_guard<std::mutex> lock(hotkeyMutex_);
        for (const auto &[id, info] : hotkeys_) {
            if (info.code != code) continue;

            if ((info.modifiers & modifiers) == info.modifiers) {
                matched.emplace_back(id, info.callback);
            }
        }
    }

    for (auto &[id, cb] : matched) {
        if (hotkeyExecutor_) {
            hotkeyExecutor_->submit([cb]() {
                try { cb(); }
                catch (const std::exception &e) { error("Hotkey callback: {}", e.what()); }
            });
        } else {
            std::thread([cb]() {
                try { cb(); }
                catch (const std::exception &e) { error("Hotkey callback: {}", e.what()); }
            }).detach();
        }
    }

    return !matched.empty();
}

}
