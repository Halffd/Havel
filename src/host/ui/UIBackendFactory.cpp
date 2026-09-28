#include "UIBackendFactory.hpp"

#include <array>
#include <atomic>
#include <mutex>

namespace havel::host {
namespace {

// Derived from the enum, so adding an api cannot silently miss a slot.
constexpr std::size_t kApiCount =
    static_cast<std::size_t>(UIBackend::Api::AUTO) + 1;
std::array<std::atomic<UIBackendFactoryFn>, kApiCount> g_ui_factories{};
std::atomic<ScreenshotBackendFactoryFn> g_screenshot_factory{nullptr};
std::mutex g_screenshot_mutex;
std::string g_screenshot_toolkit;

std::size_t slot(UIBackend::Api api) {
  return static_cast<std::size_t>(api);
}

} // namespace

void registerUIBackendFactory(UIBackend::Api api, UIBackendFactoryFn fn) {
  if (slot(api) >= kApiCount) {
    return;
  }
  g_ui_factories[slot(api)].store(fn, std::memory_order_release);
}

void registerInProcessScreenshotBackendFactory(const std::string &toolkit,
                                              ScreenshotBackendFactoryFn fn) {
  std::lock_guard<std::mutex> lock(g_screenshot_mutex);
  g_screenshot_toolkit = toolkit;
  g_screenshot_factory.store(fn, std::memory_order_release);
}

bool hasUIBackendFactory(UIBackend::Api api) {
  if (slot(api) >= kApiCount) {
    return false;
  }
  return g_ui_factories[slot(api)].load(std::memory_order_acquire) != nullptr;
}

std::unique_ptr<UIBackend> createRegisteredUIBackend(UIBackend::Api api) {
  if (slot(api) >= kApiCount) {
    return nullptr;
  }
  UIBackendFactoryFn fn =
      g_ui_factories[slot(api)].load(std::memory_order_acquire);
  return fn ? fn() : nullptr;
}

std::unique_ptr<IScreenshotBackend>
createRegisteredScreenshotBackend(const std::string &toolkit) {
  ScreenshotBackendFactoryFn fn = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_screenshot_mutex);
    if (g_screenshot_toolkit != toolkit) {
      return nullptr;
    }
    fn = g_screenshot_factory.load(std::memory_order_acquire);
  }
  return fn ? fn() : nullptr;
}

} // namespace havel::host
