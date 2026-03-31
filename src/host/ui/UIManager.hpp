/*
 * UIManager.hpp - Manages UI backend selection and lifecycle
 *
 * Provides unified access to UI functionality with swappable backends.
 * Qt is the default backend.
 */
#pragma once

#include "UIBackend.hpp"
#include <memory>
#include <string>

namespace havel::host {

/**
 * UIManager - Singleton manager for UI backend
 *
 * Usage:
 *   auto& ui = UIManager::instance();
 *   ui.setBackend(UIBackend::Api::QT);  // or GTK, IMGUI
 *   auto window = ui.backend()->window("Title");
 */
class UIManager {
public:
  static UIManager &instance();

  // Backend management
  bool setBackend(UIBackend::Api api);
  bool setBackend(const std::string &apiName);

  // Get current backend (initializes default if needed)
  UIBackend *backend();

  // Get current API
  UIBackend::Api currentApi() const;
  std::string currentApiName() const;

  // Check if backend is available
  bool isBackendAvailable(UIBackend::Api api) const;
  bool isBackendAvailable(const std::string &apiName) const;

  // Auto-detect best available backend
  UIBackend::Api detectBestBackend() const;

  // Shutdown current backend
  void shutdown();

  // Check if initialized
  bool isInitialized() const;

private:
  UIManager() = default;
  ~UIManager() = default;
  UIManager(const UIManager &) = delete;
  UIManager &operator=(const UIManager &) = delete;

  std::unique_ptr<UIBackend> backend_;
  UIBackend::Api currentApi_ = UIBackend::Api::AUTO;
  bool initialized_ = false;

  // Factory methods for backends
  std::unique_ptr<UIBackend> createBackend(UIBackend::Api api);
};

} // namespace havel::host
