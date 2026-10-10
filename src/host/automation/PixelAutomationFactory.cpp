// PixelAutomationFactory.cpp — the Qt-free registration slot.
//
// Deliberately tiny and dependency-free: it is the only symbol the core needs
// to resolve a pixel service, and it must not drag anything out of havel_gui.

#include "PixelAutomationFactory.hpp"

#include <atomic>

namespace havel::host {

namespace {
std::atomic<PixelAutomationFactory> g_factory{nullptr};
} // namespace

void setPixelAutomationFactory(PixelAutomationFactory factory) {
    g_factory.store(factory, std::memory_order_release);
}

PixelAutomationFactory pixelAutomationFactory() {
    return g_factory.load(std::memory_order_acquire);
}

std::shared_ptr<IPixelAutomation> createPixelAutomation() {
    auto factory = pixelAutomationFactory();
    return factory ? factory() : nullptr;
}

} // namespace havel::host
