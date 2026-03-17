// HostAPI.hpp
// Abstract interfaces for host system operations
// Split into focused interfaces to avoid god interface problem

#pragma once
#include <functional>
#include <string>
#include <vector>

namespace havel {

// Forward declarations
using pID = int;

/**
 * Window operations interface
 */
class IWindowAPI {
public:
  virtual ~IWindowAPI() = default;

  virtual std::string GetActiveWindowTitle() = 0;
  virtual std::string GetActiveWindowClass() = 0;
  virtual pID GetActiveWindowPID() = 0;
  virtual std::string GetActiveWindowProcess() = 0;
  virtual bool IsWindowInGroup(const std::string &windowTitle,
                               const std::string &groupName) = 0;
  virtual std::vector<std::string> GetGroupNames() = 0;
  virtual std::vector<std::string>
  GetGroupWindows(const std::string &groupName) = 0;
};

/**
 * Hotkey operations interface
 */
class IHotkeyAPI {
public:
  virtual ~IHotkeyAPI() = default;

  virtual bool RegisterHotkey(const std::string &key,
                              std::function<void()> callback) = 0;
  virtual bool UnregisterHotkey(int id) = 0;
  virtual void SuspendHotkeys(bool suspend) = 0;
  virtual bool AreHotkeysSuspended() const = 0;
  virtual std::string GetCurrentMode() const = 0;
  virtual void SetCurrentMode(const std::string &mode) = 0;
};

/**
 * IO operations interface
 */
class IIOAPI {
public:
  virtual ~IIOAPI() = default;

  virtual void SendKeys(const std::string &keys) = 0;
  virtual void SendKey(const std::string &key, bool press) = 0;
  virtual void MouseMove(int x, int y) = 0;
  virtual void MouseClick(int button) = 0;
  virtual void Scroll(int dy, int dx) = 0;
};

/**
 * Clipboard operations interface
 */
class IClipboardAPI {
public:
  virtual ~IClipboardAPI() = default;

  virtual std::string GetClipboardText() = 0;
  virtual void SetClipboardText(const std::string &text) = 0;
};

/**
 * Config operations interface
 */
class IConfigAPI {
public:
  virtual ~IConfigAPI() = default;

  virtual std::string GetConfigString(const std::string &key,
                                      const std::string &defaultVal) = 0;
  virtual void SetConfig(const std::string &key, const std::string &value) = 0;
};

/**
 * Base interface - aggregates all host API interfaces
 * Modules should depend on specific interfaces when possible
 */
class IHostAPI : public IWindowAPI,
                 public IHotkeyAPI,
                 public IIOAPI,
                 public IClipboardAPI,
                 public IConfigAPI {
public:
  virtual ~IHostAPI() = default;

  // Access to Config (needed by many modules)
  virtual class Configs &GetConfig() = 0;

  // Manager access for modules that need direct subsystem access
  // TODO: Replace with specific operation methods over time
  virtual class IO *GetIO() = 0;
  virtual class HotkeyManager *GetHotkeyManager() = 0;
  virtual class WindowManager *GetWindowManager() = 0;
  virtual class BrightnessManager *GetBrightnessManager() = 0;
  virtual class AudioManager *GetAudioManager() = 0;
  virtual class GUIManager *GetGUIManager() = 0;
  virtual class ScreenshotManager *GetScreenshotManager() = 0;
  virtual class ClipboardManager *GetClipboardManager() = 0;
  virtual class PixelAutomation *GetPixelAutomation() = 0;
  virtual class AutomationManager *GetAutomationManager() = 0;
  virtual class FileManager *GetFileManager() = 0;
  virtual class ProcessManager *GetProcessManager() = 0;
  virtual class MapManager *GetMapManager() = 0;
  virtual class ModeManager *GetModeManager() = 0;

  // Import manager for script imports
  virtual class ImportManager *GetImportManager() = 0;

  // Command line arguments access
  virtual const std::vector<std::string> &GetCommandLineArgs() = 0;

  // Update manager pointers (called after managers are created)
  virtual void SetHotkeyManager(class HotkeyManager *hm) = 0;
  virtual void SetIO(class IO *io) = 0;
};

/**
 * HostAPI - Concrete implementation
 * Composes subsystems rather than making IO do everything
 */
class HostAPI : public IHostAPI {
public:
  HostAPI(class IO *io, class HotkeyManager *hotkeyManager,
          class Configs &config, class WindowManager *windowManager = nullptr,
          class BrightnessManager *brightnessManager = nullptr,
          class AudioManager *audioManager = nullptr,
          class GUIManager *guiManager = nullptr,
          class ScreenshotManager *screenshotManager = nullptr,
          class ClipboardManager *clipboardManager = nullptr,
          class PixelAutomation *pixelAutomation = nullptr,
          class AutomationManager *automationManager = nullptr,
          class FileManager *fileManager = nullptr,
          class ProcessManager *processManager = nullptr,
          class MapManager *mapManager = nullptr,
          class ModeManager *modeManager = nullptr,
          const std::vector<std::string> &commandLineArgs = {});

  // IWindowAPI implementation
  std::string GetActiveWindowTitle() override;
  std::string GetActiveWindowClass() override;
  pID GetActiveWindowPID() override;
  std::string GetActiveWindowProcess() override;
  bool IsWindowInGroup(const std::string &windowTitle,
                       const std::string &groupName) override;
  std::vector<std::string> GetGroupNames() override;
  std::vector<std::string>
  GetGroupWindows(const std::string &groupName) override;

  // IHotkeyAPI implementation
  bool RegisterHotkey(const std::string &key,
                      std::function<void()> callback) override;
  bool UnregisterHotkey(int id) override;
  void SuspendHotkeys(bool suspend) override;
  bool AreHotkeysSuspended() const override;
  std::string GetCurrentMode() const override;
  void SetCurrentMode(const std::string &mode) override;

  // IIOAPI implementation
  void SendKeys(const std::string &keys) override;
  void SendKey(const std::string &key, bool press) override;
  void MouseMove(int x, int y) override;
  void MouseClick(int button) override;
  void Scroll(int dy, int dx) override;

  // IClipboardAPI implementation
  std::string GetClipboardText() override;
  void SetClipboardText(const std::string &text) override;

  // IConfigAPI implementation
  std::string GetConfigString(const std::string &key,
                              const std::string &defaultVal) override;
  void SetConfig(const std::string &key, const std::string &value) override;

  // IHostAPI implementation
  class Configs &GetConfig() override;

  // Manager access for modules
  class IO *GetIO() override;
  class HotkeyManager *GetHotkeyManager() override;
  class WindowManager *GetWindowManager() override;
  class BrightnessManager *GetBrightnessManager() override;
  class AudioManager *GetAudioManager() override;
  class GUIManager *GetGUIManager() override;
  class ScreenshotManager *GetScreenshotManager() override;
  class ClipboardManager *GetClipboardManager() override;
  class PixelAutomation *GetPixelAutomation() override;
  class AutomationManager *GetAutomationManager() override;
  class FileManager *GetFileManager() override;
  class ProcessManager *GetProcessManager() override;
  class MapManager *GetMapManager() override;
  class ModeManager *GetModeManager() override;
  class ImportManager *GetImportManager() override {
    return nullptr;
  } // TODO: implement

  // Command line arguments access
  const std::vector<std::string> &GetCommandLineArgs() override;

  // Update manager pointers (called after managers are created)
  void SetHotkeyManager(class HotkeyManager *hm) override;
  void SetIO(class IO *io) override;

private:
  class IO *io;
  class HotkeyManager *hotkeyManager;
  class Configs &config;
  class WindowManager *windowManager;
  class BrightnessManager *brightnessManager;
  class AudioManager *audioManager;
  class GUIManager *guiManager;
  class ScreenshotManager *screenshotManager;
  class ClipboardManager *clipboardManager;
  class PixelAutomation *pixelAutomation;
  class AutomationManager *automationManager;
  class FileManager *fileManager;
  class ProcessManager *processManager;
  class MapManager *mapManager;
  class ModeManager *modeManager;

  // Command line arguments
  std::vector<std::string> commandLineArgs;
};

} // namespace havel
