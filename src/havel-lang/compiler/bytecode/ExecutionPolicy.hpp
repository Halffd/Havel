#pragma once

/**
 * ExecutionPolicy.hpp - Optional execution restrictions
 *
 * NOT for core language security.
 * For embedding contexts that want sandboxing.
 *
 * Use cases:
 * - App embedding Havel runtime (restrict plugins)
 * - Running untrusted scripts (marketplace, downloads)
 * - Safe modes (debug, CI, dry-run)
 *
 * Default policy: FULL ACCESS (no friction for normal users)
 */

#include <cstdint>
#include <string>
#include <functional>
#include <unordered_set>

namespace havel::compiler {

/**
 * ExecutionPolicy - Optional permission layer
 *
 * Embedders can restrict what scripts can do.
 * Normal users: use DefaultPolicy() = full access.
 */
class ExecutionPolicy {
public:
  // Permission check function
  using PermissionFn = std::function<bool(const std::string &action)>;

  ExecutionPolicy() = default;
  explicit ExecutionPolicy(PermissionFn checker) : checker_(std::move(checker)) {}

  // Check if action is allowed
  bool canExecute(const std::string &action) const {
    if (!checker_) return true;  // No checker = allow all
    return checker_(action);
  }

  // Preset policies
  static ExecutionPolicy DefaultPolicy() {
    // Full access - no restrictions
    return ExecutionPolicy();
  }

  static ExecutionPolicy SandboxPolicy() {
    // Restricted - no file/process/window access
    return ExecutionPolicy([](const std::string &action) {
      // Allow: basic IO, clipboard, async
      if (action.find("io.send") == 0) return true;
      if (action.find("io.mouse") == 0) return true;
      if (action.find("clipboard") == 0) return true;
      if (action.find("async") == 0) return true;
      // Deny: file, process, window, hotkey
      if (action.find("file") == 0) return false;
      if (action.find("process") == 0) return false;
      if (action.find("window") == 0) return false;
      if (action.find("hotkey") == 0) return false;
      if (action.find("automation") == 0) return false;
      if (action.find("browser") == 0) return false;
      // Default: allow
      return true;
    });
  }

  static ExecutionPolicy ReadOnlyPolicy() {
    // Read-only file access, no writes/exec
    return ExecutionPolicy([](const std::string &action) {
      if (action.find("readFile") == 0) return true;
      if (action.find("writeFile") == 0) return false;
      if (action.find("deleteFile") == 0) return false;
      if (action.find("execute") == 0) return false;
      // Allow everything else
      return true;
    });
  }

  // Block specific actions
  void block(const std::string &action) {
    blocked_.insert(action);
  }

  // Allow specific actions (when using restrictive default)
  void allow(const std::string &action) {
    allowed_.insert(action);
  }

private:
  PermissionFn checker_;
  std::unordered_set<std::string> blocked_;
  std::unordered_set<std::string> allowed_;
};

/**
 * Capability bitmask - for module-level grouping
 *
 * Optional - used by ExecutionPolicy presets.
 * Normal users don't need this.
 */
enum class Capability : uint64_t {
  None = 0,
  IO = (1ULL << 0),
  FileIO = (1ULL << 1),
  ProcessExec = (1ULL << 2),
  WindowControl = (1ULL << 3),
  HotkeyControl = (1ULL << 4),
  ModeControl = (1ULL << 5),
  ClipboardAccess = (1ULL << 6),
  ScreenshotAccess = (1ULL << 7),
  AsyncOps = (1ULL << 8),
  AudioControl = (1ULL << 9),
  BrightnessControl = (1ULL << 10),
  AutomationControl = (1ULL << 11),
  BrowserControl = (1ULL << 12),
  TextChunkerAccess = (1ULL << 13),
  InputRemapping = (1ULL << 14),
  AltTabControl = (1ULL << 15),
  
  // Presets
  Full = ~0ULL,
  Sandbox = ~(FileIO | ProcessExec | HotkeyControl | WindowControl | 
              AutomationControl | BrowserControl | InputRemapping),
  Minimal = IO | AsyncOps | ClipboardAccess,
};

// Bitwise operators
inline Capability operator|(Capability a, Capability b) {
  using T = std::underlying_type_t<Capability>;
  return static_cast<Capability>(static_cast<T>(a) | static_cast<T>(b));
}

inline Capability operator&(Capability a, Capability b) {
  using T = std::underlying_type_t<Capability>;
  return static_cast<Capability>(static_cast<T>(a) & static_cast<T>(b));
}

inline bool hasCapability(Capability caps, Capability test) {
  using T = std::underlying_type_t<Capability>;
  return (static_cast<T>(caps & test) & static_cast<T>(test)) != 0;
}

// Map module name to capability
inline Capability capabilityForModule(const std::string &name) {
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

} // namespace havel::compiler
