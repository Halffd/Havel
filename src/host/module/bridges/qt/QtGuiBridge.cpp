// QtGuiBridge.cpp — the Qt half of UIBridge.
//
// This TU is compiled into the optional havel_gui target only. Everything it
// touches is a QObject or a Qt-backed manager, so none of it may appear in a
// core-facing library: libhavel_core.a and libhavel_lang.a are embedded by
// Qt-free hosts (havel-wm, the cranelift shim, the AOT runtime) and must
// build and link without any Qt header or Qt symbol in their closure.
//
// The core reaches this code through weak-symbol registration hooks declared
// in src/host/module/bridges/UIBridge.cpp and guarded at the call site with
// `if (&fn) fn(...)`. When havel_gui is not linked the symbol resolves to
// null and the host function simply is not registered — the same contract
// installQtClipboardBridge already uses for clipboard.* / io.getClipboard.
//
// Layout rules for this file: no #undef, and Qt comes in through the repo's
// include wrapper (GUIManager.hpp already pulls in qt.hpp).

#include "../../ModularHostBridges.hpp"
#include "../BridgesInternal.hpp"
#include "extensions/gui/common/GUIManager.hpp"
#include "havel-lang/runtime/HostContext.hpp"

namespace havel::compiler {

// UIBridge::handleGUINotify — out-of-class definition of the static member
// declared in ModularHostBridges.hpp. The body lives here rather than in
// UIBridge.cpp because GUIManager derives from QObject and its definition
// transitively includes every Qt header.
Value UIBridge::handleGUINotify(const std::vector<Value> &args,
                                const HostContext *ctx) {
  if (!ctx || !ctx->guiManager) {
    return Value::makeBool(false);
  }
  if (args.size() < 2) {
    throw std::runtime_error("gui.notify() requires title and message");
  }

  const std::string title = strVal(args[0], ctx->vm);
  const std::string message = strVal(args[1], ctx->vm);

  std::string icon = "info";
  int durationMs = 0;
  if (args.size() > 2) {
    icon = strVal(args[2], ctx->vm);
  }
  if (args.size() > 3 && args[3].isInt()) {
    durationMs = static_cast<int>(args[3].asInt());
  }

  ctx->guiManager->showNotification(title, message, icon, durationMs);
  return Value::makeBool(true);
}

// Weak hook consumed by UIBridge::install.
void installQtGuiBridge(PipelineOptions &options, const HostContext *ctx) {
  options.host_functions["gui.notify"] = [ctx](const auto &args) {
    return UIBridge::handleGUINotify(args, ctx);
  };
}

} // namespace havel::compiler
