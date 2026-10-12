// ClipboardBackendFactory.hpp — the registry that keeps Qt out of Clipboard.
//
// Clipboard (host/clipboard) used to carry its Qt implementation inline behind
// HAVE_QT_EXTENSION, which dragged Qt headers and symbols into libhavel_core.a.
// It now asks for a backend through this registry; the Qt implementation lives
// in src/extensions/qt/QtClipboardBackend.hpp and is registered from the Qt
// bridge. The core calls through the slot and never includes a Qt header, the
// same way UIBackendFactory.hpp keeps the UI backends out of UIManager.
//
// No backend registered means no registered clipboard access: Clipboard falls
// through to its Wayland/X11/external command paths exactly as it did when the
// inline Qt code found no QApplication.

#pragma once

#include "IClipboardBackend.hpp"

#include "havel-lang/common/Export.hpp"
#include <memory>

namespace havel::host {

// Plain function pointer, not std::function: the registry stores it in
// std::atomic, which requires a trivially copyable type.
using ClipboardBackendFactoryFn = std::unique_ptr<IClipboardBackend> (*)();

// Registration happens once, from the Qt side, before first use. Passing
// nullptr removes the factory.
HAVEL_EXPORT void registerClipboardBackendFactory(ClipboardBackendFactoryFn fn);

// Returns nullptr when no factory is registered, which is what Clipboard must
// treat as "no registered backend" instead of a compile-time macro.
std::unique_ptr<IClipboardBackend> createRegisteredClipboardBackend();

bool hasClipboardBackendFactory();

// Entry point defined by the Qt toolkit plugin
// (src/extensions/qt/qt_toolkit_factories.cpp); havel_gui keeps a copy for
// legacy consumers. It has to be called explicitly: the implementation shares
// no symbol with anything else, so a static initialiser inside it would never
// be extracted from the static archive and the factory would silently never
// register. The toolkit's install_factories slot calls it. Safe to call more
// than once. Declared here so the declaration stays Qt-free.
HAVEL_EXPORT void installQtClipboardBackend();

} // namespace havel::host
