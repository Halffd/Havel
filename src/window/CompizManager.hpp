#pragma once
#include <string>
#include <memory>
#include <vector>
#include <optional>
#include <sys/socket.h>
#include <sys/un.h>
#include <cstdint>

class CompizManager {
public:
    CompizManager();
    ~CompizManager() noexcept;
    
    // Rule of five
    CompizManager(const CompizManager&) = delete;
    CompizManager& operator=(const CompizManager&) = delete;
    CompizManager(CompizManager&&) noexcept = default;
    CompizManager& operator=(CompizManager&&) noexcept = default;

    // Core functionality
    bool StartCompiz();
    bool StopCompiz();
    bool RestartCompiz();
    bool SwitchToDefaultWM();
    bool SwitchToCompiz();
    
    // Configuration
    bool SetPlugin(const std::string& plugin, bool enabled);
    bool SetPluginOption(const std::string& plugin, const std::string& option, const std::string& value);
    std::optional<std::string> GetPluginOption(const std::string& plugin, const std::string& option) const;
    std::vector<std::string> GetActivePlugins() const;
    
    // State checks
    bool IsRunning() const noexcept;
    bool IsDefaultWM() const noexcept;
    std::string GetDefaultWM() const;

    // Plugin-specific controls
    
    // Scale plugin
    bool EnableScale(bool enabled = true);
    bool SetScaleInitiate(const std::string& key = "Super+w");
    bool SetScaleAllInitiate(const std::string& key = "Super+Shift+w");
    
    // Application Switcher
    bool EnableSwitcher(bool enabled = true);
    bool SetSwitcherNextKey(const std::string& key = "Alt+Tab");
    bool SetSwitcherPrevKey(const std::string& key = "Alt+Shift+Tab");
    bool SetSwitcherStyle(const std::string& style = "Cover"); // Cover, Ring, etc.
    
    // Desktop Wall
    bool EnableWall(bool enabled = true);
    bool SetWallEdgeFlip(bool enabled = true);
    bool SetWallEdgeFlipPointer(bool enabled = true);
    bool SetWallEdgeFlipDnd(bool enabled = true);
    bool SetWallSlideDuration(int duration = 250); // milliseconds
    bool SetWallSlideAnimation(int duration = 250); // milliseconds
    
    // Enhanced Zoom
    bool EnableZoom(bool enabled = true);
    bool SetZoomIn(const std::string& key = "Super+plus");
    bool SetZoomOut(const std::string& key = "Super+minus");
    bool SetZoomFactor(float factor = 2.0f);
    
    // Expo
    bool EnableExpo(bool enabled = true);
    bool SetExpoInitiate(const std::string& key = "Super+e");
    bool SetExpoAnimation(const std::string& type = "zoom"); // zoom, fade, etc.
    
    // Grid
    bool EnableGrid(bool enabled = true);
    bool SetGridLeftMaximize(const std::string& key = "Super+Left");
    bool SetGridRightMaximize(const std::string& key = "Super+Right");
    bool SetGridTopMaximize(const std::string& key = "Super+Up");
    bool SetGridBottomMaximize(const std::string& key = "Super+Down");
    
    // Wobbly Windows
    bool EnableWobbly(bool enabled = true);
    bool SetWobblyFriction(float friction = 3.0f);
    bool SetWobblySpringK(float springK = 8.0f);
    
    // Cube
    bool EnableCube(bool enabled = true);
    bool SetCubeRotation(const std::string& key = "Ctrl+Alt+Left");
    bool SetCubeUnfold(const std::string& key = "Ctrl+Alt+Down");
    bool SetCubeOpacity(float opacity = 0.8f);
    
    // Animation
    bool EnableAnimation(bool enabled = true);
    bool SetOpenAnimation(const std::string& type = "zoom");
    bool SetCloseAnimation(const std::string& type = "zoom");
    bool SetMinimizeAnimation(const std::string& type = "zoom");
    
    // Snap
    bool EnableSnap(bool enabled = true);
    bool SetSnapType(const std::string& type = "screen"); // screen, window
    bool SetSnapThreshold(int threshold = 10);

private:
    class CompizManagerImpl {
    public:
        CompizManagerImpl();
        ~CompizManagerImpl() noexcept;
        
        int dbusSocket;
        std::string dbusAddress;
        uint32_t serial;
        std::string defaultWM;
        
        bool Connect();
        void Disconnect() noexcept;
        std::vector<uint8_t> CreateMessage(const std::string& dest,
                                         const std::string& path,
                                         const std::string& interface,
                                         const std::string& method,
                                         const std::vector<std::string>& args = {}) const;
    };
    
    std::unique_ptr<CompizManagerImpl> pImpl;
    
    bool SendDBusMessage(const std::string& dest,
                        const std::string& path,
                        const std::string& interface,
                        const std::string& method,
                        const std::vector<std::string>& args = {}) const;
                        
    std::optional<std::string> GetDBusProperty(const std::string& interface,
                                             const std::string& path,
                                             const std::string& property) const;

    // Helper methods for plugin configuration
    bool SetPluginKey(const std::string& plugin, 
                     const std::string& action,
                     const std::string& key);
                     
    bool SetPluginValue(const std::string& plugin,
                       const std::string& option,
                       const std::string& value);
                       
    bool SetPluginNumValue(const std::string& plugin,
                          const std::string& option,
                          float value);
}; 