// BridgeSelection.hpp — explicit host-side selection of optional bridges.
//
// libhavel_core.a and libhavel_lang.a are embedded by Qt-free hosts (havel-wm,
// the cranelift shim, the AOT runtime), so the core must not contain, call
// into, or *depend on the linkage of* any Qt code. Optional bridges are
// therefore wired in by the application that actually links them, not by the
// core pulling them out of a static archive.
//
// Why a registration slot and not a weak undefined symbol: ld does not extract
// an archive member to satisfy a weak reference. A weak hook only appears to
// work when the defining translation unit happens to be extracted for some
// unrelated reason, so registration silently depends on link order. The
// setter below creates a *strong* reference from the app's own translation
// unit, which is the one thing the linker must resolve.
//
// Qt-free hosts simply never call the setter, the slot stays null, and the
// corresponding host functions are not registered.

#pragma once

#include <atomic>

namespace havel {

struct HostContext;
namespace compiler {
struct PipelineOptions;
} // namespace compiler
// Signature shared by the optional bridge installers: each one adds its own
// entries to the pipeline's host function table.
using QtBridgeInstaller = void (*)(compiler::PipelineOptions &,
                                   const HostContext *);

/// Registers the installer for the Qt bridge (libhavel_gui). Called once by
/// the application entry point before any pipeline is built. Passing nullptr
/// clears the selection.
void setQtBridgeInstaller(QtBridgeInstaller installer);

/// The registered installer, or nullptr when the host did not select a Qt
/// bridge. Read by UIBridge::install().
QtBridgeInstaller qtBridgeInstaller();

} // namespace havel
