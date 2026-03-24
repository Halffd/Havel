#pragma once

/**
 * HostBridgeCapabilities.hpp - Type-safe capability gating
 *
 * Uses bitmask for performance and compiler safety.
 */

#include <cstdint>
#include <string>
#include <type_traits>

namespace havel::compiler {

/**
 * Capability bitmask - type-safe, fast, compiler-checked
 */
enum class Capability : uint64_t {
  None = 0,
  IO = (1ULL << 0),              // send, mouse, keyboard
  FileIO = (1ULL << 1),          // readFile, writeFile
  ProcessExec = (1ULL << 2),     // execute, getpid
  WindowControl = (1ULL << 3),   // window operations
  HotkeyControl = (1ULL << 4),   // hotkey registration
  ModeControl = (1ULL << 5),     // mode system
  ClipboardAccess = (1ULL << 6), // clipboard get/set
  ScreenshotAccess = (1ULL << 7),// screenshots
  AsyncOps = (1ULL << 8),        // sleep, timers
  AudioControl = (1ULL << 9),    // volume, mute
  BrightnessControl = (1ULL << 10), // brightness, temperature
  AutomationControl = (1ULL << 11), // auto-clicker, automation
  BrowserControl = (1ULL << 12),    // browser automation
  TextChunkerAccess = (1ULL << 13), // text chunking
  InputRemapping = (1ULL << 14),    // key remapping, macros
  AltTabControl = (1ULL << 15),     // window switcher
  
  // Presets
  Full = ~0ULL,
  Sandbox = ~(FileIO | ProcessExec | HotkeyControl | WindowControl | 
              AutomationControl | BrowserControl | InputRemapping),
  Minimal = IO | AsyncOps,
};

// Bitwise operators for Capability
inline Capability operator|(Capability a, Capability b) {
  using T = std::underlying_type_t<Capability>;
  return static_cast<Capability>(static_cast<T>(a) | static_cast<T>(b));
}

inline Capability operator&(Capability a, Capability b) {
  using T = std::underlying_type_t<Capability>;
  return static_cast<Capability>(static_cast<T>(a) & static_cast<T>(b));
}

inline Capability operator~(Capability a) {
  using T = std::underlying_type_t<Capability>;
  return static_cast<Capability>(~static_cast<T>(a));
}

inline Capability& operator|=(Capability& a, Capability b) {
  a = a | b;
  return a;
}

inline bool hasCapability(Capability caps, Capability test) {
  using T = std::underlying_type_t<Capability>;
  return (static_cast<T>(caps & test) & static_cast<T>(test)) != 0;
}

/**
 * HostBridgeCapabilities - Capability container
 */
struct HostBridgeCapabilities {
  Capability caps = Capability::Full;

  // Convenience constructors
  static HostBridgeCapabilities Full() { return {Capability::Full}; }
  static HostBridgeCapabilities Sandbox() { return {Capability::Sandbox}; }
  static HostBridgeCapabilities Minimal() { return {Capability::Minimal}; }

  // Check capability
  bool has(Capability test) const { return havel::compiler::hasCapability(caps, test); }
  
  // Set capability
  void set(Capability cap, bool enabled = true) {
    if (enabled) {
      caps = caps | cap;
    } else {
      caps = caps & ~cap;
    }
  }

  // Module capability requirements
  static Capability requiredForModule(const std::string &name) {
    if (name == "io") return Capability::IO;
    if (name == "file") return Capability::FileIO;
    if (name == "process") return Capability::ProcessExec;
    if (name == "window") return Capability::WindowControl;
    if (name == "hotkey") return Capability::HotkeyControl;
    if (name == "mode") return Capability::ModeControl;
    if (name == "clipboard") return Capability::ClipboardAccess;
    if (name == "screenshot") return Capability::ScreenshotAccess;
    if (name == "async") return Capability::AsyncOps;
    if (name == "audio") return Capability::AudioControl;
    if (name == "brightness") return Capability::BrightnessControl;
    if (name == "automation") return Capability::AutomationControl;
    if (name == "browser") return Capability::BrowserControl;
    if (name == "textchunker") return Capability::TextChunkerAccess;
    if (name == "mapmanager") return Capability::InputRemapping;
    if (name == "alttab") return Capability::AltTabControl;
    return Capability::None;
  }
};

} // namespace havel::compiler
