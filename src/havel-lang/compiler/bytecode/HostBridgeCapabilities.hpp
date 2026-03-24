#pragma once

/**
 * HostBridgeCapabilities.hpp - Capability gating for security/sandboxing
 *
 * Controls which host features are available to scripts.
 * Used for:
 * - Sandboxing untrusted scripts
 * - Partial embedding (headless mode, CLI-only)
 * - User permission prompts
 */

#include <string>
#include <vector>
#include <set>

namespace havel::compiler {

struct HostBridgeCapabilities {
  bool ioControl = true;              // send, mouse, keyboard
  bool fileIO = true;                 // readFile, writeFile
  bool processExec = true;            // execute, getpid
  bool windowControl = true;          // window operations
  bool hotkeyControl = true;          // hotkey registration
  bool modeControl = true;            // mode system
  bool clipboardAccess = true;        // clipboard get/set
  bool screenshotAccess = true;       // screenshots
  bool asyncOps = true;               // sleep, timers
  bool audioControl = true;           // volume, mute
  bool brightnessControl = true;      // brightness, temperature
  bool automationControl = true;      // auto-clicker, automation
  bool browserControl = true;         // browser automation
  bool textChunkerAccess = true;      // text chunking
  bool inputRemapping = true;         // key remapping, macros
  bool altTabControl = true;          // window switcher

  // Convenience presets
  static HostBridgeCapabilities Full() { return {}; }

  static HostBridgeCapabilities Sandbox() {
    HostBridgeCapabilities caps;
    caps.fileIO = false;
    caps.processExec = false;
    caps.hotkeyControl = false;
    caps.windowControl = false;
    caps.automationControl = false;
    caps.browserControl = false;
    caps.inputRemapping = false;
    return caps;
  }

  static HostBridgeCapabilities Minimal() {
    HostBridgeCapabilities caps;
    caps.ioControl = true;  // Only basic IO
    caps.fileIO = false;
    caps.processExec = false;
    caps.windowControl = false;
    caps.hotkeyControl = false;
    caps.modeControl = false;
    caps.clipboardAccess = false;
    caps.screenshotAccess = false;
    caps.asyncOps = true;
    caps.audioControl = false;
    caps.brightnessControl = false;
    caps.automationControl = false;
    caps.browserControl = false;
    caps.textChunkerAccess = false;
    caps.inputRemapping = false;
    caps.altTabControl = false;
    return caps;
  }

  // Get required capabilities for a module
  static std::set<std::string> getRequiredCapabilities(const std::string &moduleName) {
    if (moduleName == "io") return {"ioControl"};
    if (moduleName == "file") return {"fileIO"};
    if (moduleName == "process") return {"processExec"};
    if (moduleName == "window") return {"windowControl"};
    if (moduleName == "hotkey") return {"hotkeyControl"};
    if (moduleName == "mode") return {"modeControl"};
    if (moduleName == "clipboard") return {"clipboardAccess"};
    if (moduleName == "screenshot") return {"screenshotAccess"};
    if (moduleName == "async") return {"asyncOps"};
    if (moduleName == "audio") return {"audioControl"};
    if (moduleName == "brightness") return {"brightnessControl"};
    if (moduleName == "automation") return {"automationControl"};
    if (moduleName == "browser") return {"browserControl"};
    if (moduleName == "textchunker") return {"textChunkerAccess"};
    if (moduleName == "mapmanager") return {"inputRemapping"};
    if (moduleName == "alttab") return {"altTabControl"};
    return {};
  }

  // Check if capability is enabled
  bool hasCapability(const std::string &cap) const {
    if (cap == "ioControl") return ioControl;
    if (cap == "fileIO") return fileIO;
    if (cap == "processExec") return processExec;
    if (cap == "windowControl") return windowControl;
    if (cap == "hotkeyControl") return hotkeyControl;
    if (cap == "modeControl") return modeControl;
    if (cap == "clipboardAccess") return clipboardAccess;
    if (cap == "screenshotAccess") return screenshotAccess;
    if (cap == "asyncOps") return asyncOps;
    if (cap == "audioControl") return audioControl;
    if (cap == "brightnessControl") return brightnessControl;
    if (cap == "automationControl") return automationControl;
    if (cap == "browserControl") return browserControl;
    if (cap == "textChunkerAccess") return textChunkerAccess;
    if (cap == "inputRemapping") return inputRemapping;
    if (cap == "altTabControl") return altTabControl;
    return false;
  }
};

} // namespace havel::compiler
