#pragma once

// BridgesInternal.hpp — shared helpers for the per-domain bridge
// translation units. This content was centralized when the monolithic
// src/host/module/ModularHostBridges.cpp (~7.9k lines) was split.

#ifdef HAVE_QT_EXTENSION
#include "qt.hpp"
#endif
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

#ifdef HAVE_QT_EXTENSION
#include "extensions/gui/clipboard_manager/ClipboardManager.hpp"
#include "extensions/gui/common/GUIManager.hpp"
#include "extensions/gui/screenshot_manager/ScreenshotManager.hpp"
// SettingsWindow removed (was part of deprecated AutomationSuite)
#endif
#include "havel-lang/runtime/Modules.hpp"
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
#ifdef HAVE_QT_EXTENSION
#include "host/screenshot/ScreenshotService.hpp"
#include "host/window/AltTabService.hpp"
#endif
#include "host/window/WindowService.hpp"
#include "core/media/AudioManager.hpp"
#include "core/process/Launcher.hpp"
#include "core/window/WindowManager.hpp"
#include "core/window/WindowManagerDetector.hpp"

#ifdef HAVE_QT_EXTENSION
#include <QClipboard>
#include <QString>
#endif
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

namespace {

// Resolve a string-ish Value to std::string. Used across bridges.
static std::string strVal(const Value &v, const compiler::VM *vm) {
    if (vm && (v.isStringValId() || v.isStringId())) return vm->resolveStringKey(v);
    return v.toString();
}


} // namespace

} // namespace havel::compiler
