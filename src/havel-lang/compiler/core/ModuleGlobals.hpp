#pragma once

// Rust-like source of truth for module globals.
//
// The literal setGlobal(name, ...) declaration in each module / VM host
// bridge IS the declaration of a script-visible global. The canonical
// global-name list (kModuleGlobals) is GENERATED from those declarations by
// scripts/gen_module_globals.py and emitted into ModuleGlobals.generated.hpp.
// (The wrapper and the generated file are excluded from the scan so their own
// prose cannot self-pollute the list.)
//
// This header is a thin wrapper so callers keep a stable include path. The
// hand-maintained duplicate list that previously lived here has been removed:
// strict-mode resolution (runBuild, the pipeline fallback) consumes the
// generated list, so a runtime setGlobal(name) can never drift out of sync the
// way a hand-maintained literal could.
//
// The generated file is produced at build time (wired in CMakeLists.txt as a
// pre-build step) and should also be regenerated/checked in when module
// sources change.
#include "havel-lang/compiler/core/ModuleGlobals.generated.hpp"
