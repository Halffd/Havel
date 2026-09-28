#pragma once

// BridgesInternal.hpp — shared helpers for the per-domain bridge
// translation units. This content was centralized when the monolithic
// src/host/module/ModularHostBridges.cpp (~7.9k lines) was split.

// Qt-free by contract: this header is the common include closure of every
// core-facing bridge TU, so it must not pull in qt.hpp or the Qt-backed
// managers from src/extensions/gui. Qt-only handlers live in
// src/host/module/bridges/qt/QtBridge.cpp and reach the core through the
// explicit bridge selection slot (see ../BridgeSelection.hpp).
#include "../../host/window/WindowService.hpp"
#include "../../utils/Logger.hpp"
#include "../../utils/DebugFlags.hpp"
#include "havel-lang/compiler/runtime/EventQueue.hpp"
#include "havel-lang/stdlib/HotkeyModule.hpp"
#include "havel-lang/runtime/concurrency/DependencyTracker.hpp"
#include "core/config/ConfigManager.hpp"
#include "core/detect/HardwareDetector.hpp"
#include "core/display/DisplayManager.hpp"
#include "core/hotkey/HotkeyManager.hpp"
#include "core/io/IO.hpp"
#include "core/io/EventListener.hpp"
#include "core/BrightnessManager.hpp"
#include <csignal>

#include "havel-lang/compiler/vm/VMApi.hpp"
#include "havel-lang/runtime/concurrency/Scheduler.hpp"
#include "host/app/AppService.hpp"
#include "host/audio/AudioService.hpp"
// AutomationSuite removed (deprecated)
#include "host/automation/AutomationService.hpp"
#include "host/browser/BrowserService.hpp"
#include "host/chunker/TextChunkerService.hpp"
#include "host/filesystem/FileSystemService.hpp"
#include "host/hotkey/HotkeyService.hpp"
#include "host/io/MapManagerService.hpp"
#include "host/media/MediaService.hpp"
#include "host/mouse/MouseService.hpp"
#include "host/network/NetworkService.hpp"
#include "host/process/ProcessService.hpp"
// Backend-abstracted host services: pImpl delegators whose headers carry no
// Qt. The concrete backends live in the optional GUI bridge targets.
#include "host/screenshot/ScreenshotService.hpp"
#include "host/window/AltTabService.hpp"
#include "host/window/WindowService.hpp"
#include "core/media/AudioManager.hpp"
#include "core/process/Launcher.hpp"
#include "core/window/WindowManager.hpp"
#include "core/window/WindowManagerDetector.hpp"

#include <atomic>
#include <algorithm>
#include <ctime>
#include <deque>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>

namespace havel::compiler {

// Resolve a string-ish Value to std::string. Used across bridges and by the
// Qt bridge TU, so it is a shared inline rather than a per-TU static.
inline std::string strVal(const Value &v, const compiler::VM *vm) {
    if (vm && (v.isStringValId() || v.isStringId())) return vm->resolveStringKey(v);
    return v.toString();
}

} // namespace havel::compiler
