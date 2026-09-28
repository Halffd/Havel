#include "ClipboardBackendFactory.hpp"

#include <atomic>

namespace havel::host {
namespace {

std::atomic<ClipboardBackendFactoryFn> g_clipboard_factory{nullptr};

} // namespace

void registerClipboardBackendFactory(ClipboardBackendFactoryFn fn) {
  g_clipboard_factory.store(fn, std::memory_order_release);
}

bool hasClipboardBackendFactory() {
  return g_clipboard_factory.load(std::memory_order_acquire) != nullptr;
}

std::unique_ptr<IClipboardBackend> createRegisteredClipboardBackend() {
  ClipboardBackendFactoryFn fn =
      g_clipboard_factory.load(std::memory_order_acquire);
  return fn ? fn() : nullptr;
}

} // namespace havel::host
