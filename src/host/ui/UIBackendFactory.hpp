// UIBackendFactory.hpp — registry for the optional in-process UI backends.
//
// UIManager used to construct QtBackend, GtkBackend and ImGuiBackend itself,
// behind HAVE_QT_EXTENSION / HAVE_GTK_BACKEND / HAVE_IMGUI_BACKEND guards. Those
// headers are the reason UIManager.cpp carried Qt symbols into libhavel_core.a:
// Qt-free hosts embed that archive (havel-wm, the cranelift AOT shim, the AOT
// runtime) and must not be forced to link Qt just to have a UI manager exist.
//
// So the construction moved to the side that owns each backend. An optional
// target registers a factory here; the core UIManager only ever calls through
// the registry and never includes a backend header. This is the same shape as
// the bridge selection slot in src/host/module/BridgeSelection.hpp.
//
// Registration is expected once, during application start-up, before any
// UIManager::setBackend call.

#ifndef HAVEL_HOST_UI_UIBACKENDFACTORY_HPP
#define HAVEL_HOST_UI_UIBACKENDFACTORY_HPP

#include "UIBackend.hpp"
#include "../screenshot/IScreenshotBackend.hpp"

#include "havel-lang/common/Export.hpp"
#include <memory>
#include <string>

namespace havel::host {

using UIBackendFactoryFn = std::unique_ptr<UIBackend> (*)();
using ScreenshotBackendFactoryFn = std::unique_ptr<IScreenshotBackend> (*)();

// Registers the in-process factory for one backend kind. A later call for the
// same api replaces the earlier one; registering nullptr clears it, which is
// how a host shuts a backend down again.
HAVEL_EXPORT void registerUIBackendFactory(UIBackend::Api api, UIBackendFactoryFn fn);

// The in-process screenshot backend (as opposed to the plugin/ABI one) is
// selected by toolkit name, because only the Qt toolkit has one today.
HAVEL_EXPORT void registerInProcessScreenshotBackendFactory(const std::string &toolkit,
                                              ScreenshotBackendFactoryFn fn);

bool hasUIBackendFactory(UIBackend::Api api);

// Returns nullptr when no factory is registered, which is what UIManager must
// treat as "this api is unavailable" instead of a compile-time macro.
std::unique_ptr<UIBackend> createRegisteredUIBackend(UIBackend::Api api);
std::unique_ptr<IScreenshotBackend>
createRegisteredScreenshotBackend(const std::string &toolkit);

// Entry point defined by the Qt toolkit plugin
// (src/extensions/qt/qt_toolkit_factories.cpp); havel_gui keeps a copy for
// legacy consumers. A Qt-capable host that does not load the toolkit plugin —
// havel-wm, the cranelift AOT shim — calls this to get the same in-process Qt
// UI backend it used to construct implicitly from libhavel_core.a. It is safe
// to call more than once. Declared here so the declaration stays Qt-free.
HAVEL_EXPORT void installQtUIBackendFactories();

} // namespace havel::host

#endif // HAVEL_HOST_UI_UIBACKENDFACTORY_HPP
