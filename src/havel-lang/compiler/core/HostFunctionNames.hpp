#pragma once

// Rust-like source of truth for host function names.
//
// The literal host-function registration call (registerHostFunction,
// host_functions[...], registerFunction) IS the declaration of a
// script-callable name. The canonical name list (kHostFunctionNames) is
// GENERATED from those declarations by scripts/gen_host_function_names.py and
// emitted into HostFunctionNames.generated.hpp. (The wrapper and the
// generated file are excluded from the scan so their own prose cannot
// self-pollute the list.)
//
// This header is a thin wrapper so callers keep a stable include path.
// HavelLauncher::runBuild (the compile-time-only strict-mode path with no VM
// instance) consumes the generated list, so a runtime registration can never
// silently go unknown to --build the way the removed hand-maintained literal
// did (it falsely rejected deleteFile/click and every other bridge name).
//
// The generated file is produced at build time (wired in CMakeLists.txt as a
// pre-build step); staleness is caught by
// scripts/check_havel_launcher_globals.sh.
#include "havel-lang/compiler/core/HostFunctionNames.generated.hpp"
