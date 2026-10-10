// ToolkitBridge.cpp — Qt-free installer for the plugin toolkit bridge.
//
// installQtBridge used to live in havel_gui (QtBridge.cpp) and was pulled
// into the executable by a strong reference from main.cpp. The executable no
// longer links havel_gui at all: Qt enters the process exclusively through
// havel_toolkit_qt.so, loaded by UIManager::ensureQtToolkit. This file is the
// replacement installer — it lives in the core so it is available to every
// embedder that wants the toolkit bridge, and it stays Qt-free.
//
// It does two things, both ordered exactly like the old flow:
//   1. ensureQtToolkit() dlopens the toolkit plugin and runs its
//      install_factories slot, which performs the registrations havel_gui's
//      static initializer used to run before main() (UI/screenshot/clipboard
//      backends, pixel service, screen provider). If dlopen fails — the Qt
//      version skew this whole seam exists for — it logs once and the slots
//      stay empty: degraded, not unbootable.
//   2. It adds the same pipeline host functions the Qt bridge added. Their
//      bodies are null-guarded identically: clipboard.get/set/clear and
//      io.getClipboard return null/false unless the host provisioned a
//      clipboard manager, and gui.notify returns false unless it provisioned
//      a GUI manager. No embedder provisions those managers today
//      (createHostAPI passes nullptr for every Qt manager), so the observable
//      behaviour is byte-for-byte the historical one. A host that does
//      provision a manager must answer through the toolkit ABI instead; the
//      manager pointers themselves are Qt objects this TU must never touch.

#include "havel-lang/compiler/vm/VM.hpp"
#include "havel-lang/core/PipelineOptions.hpp"
#include "havel-lang/runtime/HostContext.hpp"
#include "host/ui/UIManager.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace havel::compiler {

namespace {

// clipboard.get / io.getClipboard
Value toolkitClipboardGet(const std::vector<Value> &args, const HostContext *ctx) {
  (void)args;
  auto *vm = static_cast<VM *>(ctx ? ctx->vm : nullptr);
  if (!vm || !ctx->clipboardManager) {
    return Value::makeNull();
  }
  // Unreachable today: the only reachable path has a null clipboardManager.
  // With havel_gui out of the executable the host answers clipboard reads
  // through the toolkit ABI; it provisions managers via createHostAPI, and
  // none do. Returning null keeps the old manager-less behaviour.
  return Value::makeNull();
}

// clipboard.set
Value toolkitClipboardSet(const std::vector<Value> &args, const HostContext *ctx) {
  auto *vm = static_cast<VM *>(ctx ? ctx->vm : nullptr);
  if (!vm || args.empty() || !ctx->clipboardManager) {
    return Value::makeBool(false);
  }
  // Unreachable today — see toolkitClipboardGet. The Qt side used
  // vm->resolveStringKey(args[0]) and QClipboard::setText here.
  (void)vm;
  return Value::makeBool(false);
}

// clipboard.clear
Value toolkitClipboardClear(const std::vector<Value> &args, const HostContext *ctx) {
  (void)args;
  if (!ctx || !ctx->clipboardManager) {
    return Value::makeBool(false);
  }
  // Unreachable today — see toolkitClipboardGet.
  return Value::makeBool(false);
}

// gui.notify — the Qt implementation is GuiManager::showNotification, which
// this TU cannot name. Same guard, same arg validation, then a false return:
// if a host ever provisions a GUI manager it has to answer notifications
// through the toolkit ABI.
Value toolkitNotify(const std::vector<Value> &args, const HostContext *ctx) {
  if (!ctx || !ctx->guiManager) {
    return Value::makeBool(false);
  }
  if (args.size() < 2) {
    throw std::runtime_error("gui.notify() requires title and message");
  }
  return Value::makeBool(false);
}

} // namespace

void installToolkitBridge(PipelineOptions &options, const HostContext *ctx) {
  // Load the Qt toolkit and run its install_factories slot. On failure this
  // logs once (dlopen error) and the in-process registrations stay empty.
  havel::host::UIManager::instance().ensureQtToolkit();

  options.host_functions["clipboard.get"] = [ctx](const auto &args) {
    return toolkitClipboardGet(args, ctx);
  };
  options.host_functions["clipboard.set"] = [ctx](const auto &args) {
    return toolkitClipboardSet(args, ctx);
  };
  options.host_functions["clipboard.clear"] = [ctx](const auto &args) {
    return toolkitClipboardClear(args, ctx);
  };
  options.host_functions["io.getClipboard"] = [ctx](const auto &args) {
    return toolkitClipboardGet(args, ctx);
  };
  options.host_functions["gui.notify"] = [ctx](const auto &args) {
    return toolkitNotify(args, ctx);
  };
}

} // namespace havel::compiler