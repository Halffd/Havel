#include "ScreenCapture.hpp"

#include <atomic>

namespace havel {
namespace {
std::atomic<const ScreenProvider *> g_provider{nullptr};
} // namespace

void setScreenProvider(const ScreenProvider *provider) {
  g_provider.store(provider, std::memory_order_release);
}

const ScreenProvider *screenProvider() {
  return g_provider.load(std::memory_order_acquire);
}

} // namespace havel
