// BridgeSelection.cpp — the registration slot declared in BridgeSelection.hpp.
//
// Lives in the core (and is Qt-free) because both sides need it: the app
// writes the slot, the bridges read it.

#include "BridgeSelection.hpp"

#include <atomic>

namespace havel {

namespace {
std::atomic<QtBridgeInstaller> g_qtBridgeInstaller{nullptr};
} // namespace

void setQtBridgeInstaller(QtBridgeInstaller installer) {
  g_qtBridgeInstaller.store(installer, std::memory_order_release);
}

QtBridgeInstaller qtBridgeInstaller() {
  return g_qtBridgeInstaller.load(std::memory_order_acquire);
}

} // namespace havel
