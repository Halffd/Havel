// PixelAutomationFactory.hpp — registration slot for the optional pixel service.
//
// The concrete PixelAutomationService uses Qt (QApplication for the screen,
// QImage for PNG output) and therefore lives in havel_gui. Core-facing callers
// (HostModules.cpp, PixelModule.cpp) must not name it: a single reference in
// libhavel_core.a forces every embedder — havel-wm, the cranelift AOT shim, the
// AOT runtime — to link Qt.
//
// So construction moves behind this slot, exactly like UIBackendFactory.hpp
// does for the UI backends. havel_gui registers the factory during start-up;
// a Qt-free host never does, the slot stays null, and the pixel module finds no
// service in the registry (its previous HAVE_QT_EXTENSION-less behaviour).

#pragma once

#include "IPixelAutomation.hpp"

#include <memory>

namespace havel::host {

using PixelAutomationFactory = std::shared_ptr<IPixelAutomation> (*)();

// Registration happens once, from the side that owns the implementation, before
// the first service registry initialisation. Passing nullptr clears it.
void setPixelAutomationFactory(PixelAutomationFactory factory);

// The registered factory, or nullptr when the host has no Qt pixel backend.
PixelAutomationFactory pixelAutomationFactory();

// Convenience: the registered factory's result, or nullptr when unset.
std::shared_ptr<IPixelAutomation> createPixelAutomation();

} // namespace havel::host
