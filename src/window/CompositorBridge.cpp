#include "CompositorBridge.hpp"
#include "utils/Logger.hpp"
#include "utils/Util.hpp"
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <memory>
#include <array>

#ifdef __linux__
#include <unistd.h>
#endif

namespace havel {

CompositorBridge::CompositorBridge() {
    compositorType = DetectCompositor();
    
    if (compositorType != CompositorType::Unknown) {
        info("Compositor bridge initialized for: {}", 
             compositorType == CompositorType::KWin ? "KWin" : "wlroots");
    } else {
        debug("Compositor bridge not available (not running on KWin or wlroots)");
    }
}

CompositorBridge::~CompositorBridge() {
    Stop();
}

CompositorBridge::CompositorType CompositorBridge::DetectCompositor() {
    // Check environment variables
    const char* waylandDisplay = std::getenv("WAYLAND_DISPLAY");
    if (!waylandDisplay) {
        return CompositorType::Unknown;
    }
    
    // Check for KWin
    const char* kdeSession = std::getenv("KDE_SESSION_VERSION");
    if (kdeSession) {
        // Verify KWin is actually running
        if (system("pgrep -x kwin_wayland >/dev/null 2>&1") == 0) {
            return CompositorType::KWin;
        }
    }
    
    // Check for wlroots compositors
    const char* swaySocket = std::getenv("SWAYSOCK");
    if (swaySocket) {
        return CompositorType::Sway;
    }
    
    const char* hyprlandInstance = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (hyprlandInstance) {
        return CompositorType::Hyprland;
    }
    
    // Check for other wlroots compositors by process name
    if (system("pgrep -x river >/dev/null 2>&1") == 0) {
        return CompositorType::River;
    }
    
    if (system("pgrep -x wayfire >/dev/null 2>&1") == 0) {
        return CompositorType::Wayfire;
    }
    
    return CompositorType::Unknown;
}

void CompositorBridge::Start() {
    if (compositorType == CompositorType::Unknown) {
        debug("Compositor bridge not starting (unsupported compositor)");
        return;
    }
    
    if (running.load()) {
        warn("Compositor bridge already running");
        return;
    }
    
    running = true;
    monitorThread = std::thread(&CompositorBridge::MonitoringLoop, this);
    info("Compositor bridge started");
}

void CompositorBridge::Stop() {
    if (!running.load()) {
        return;
    }
    
    running = false;
    if (monitorThread.joinable()) {
        monitorThread.join();
    }
    info("Compositor bridge stopped");
}

CompositorBridge::WindowInfo CompositorBridge::GetActiveWindow() const {
    std::lock_guard<std::mutex> lock(cacheMutex);
    return cachedWindowInfo;
}

void CompositorBridge::MonitoringLoop() {
    while (running.load()) {
        WindowInfo info;
        
        try {
            switch (compositorType) {
                case CompositorType::KWin:
                    info = QueryKWin();
                    break;
                    
                case CompositorType::Sway:
                case CompositorType::Hyprland:
                case CompositorType::River:
                case CompositorType::Wayfire:
                    info = QueryWlroots();
                    break;
                    
                default:
                    break;
            }
            
            if (info.valid) {
                std::lock_guard<std::mutex> lock(cacheMutex);
                cachedWindowInfo = info;
            }
        } catch (const std::exception& e) {
            error("Error querying compositor: {}", e.what());
        }
        
        std::this_thread::sleep_for(pollInterval);
    }
}

CompositorBridge::WindowInfo CompositorBridge::QueryKWin() {
    WindowInfo info;
    
    // KWin exposes active window via KWin Scripts
    // Read from the script's output file if it exists
    
    // Path where KWin script writes active window info
    // This requires a KWin script to be installed (see below)
    std::string scriptOutputPath = std::string(std::getenv("HOME")) + 
                                   "/.local/share/kwin/scripts/havelbridge/activewindow.txt";
    
    std::ifstream file(scriptOutputPath);
    if (!file.is_open()) {
        debug("KWin bridge file not found, install HavelBridge KWin script");
        return info;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        if (line.find("title=") == 0) {
            info.title = line.substr(6);
        } else if (line.find("appid=") == 0) {
            info.appId = line.substr(6);
        } else if (line.find("pid=") == 0) {
            try {
                info.pid = std::stoi(line.substr(4));
            } catch (...) {
                info.pid = 0;
            }
        }
    }
    
    file.close();
    
    if (!info.title.empty() || !info.appId.empty()) {
        info.valid = true;
        debug("KWin active window: title='{}' appId='{}' pid={}", 
              info.title, info.appId, info.pid);
    }
    
    return info;
}

CompositorBridge::WindowInfo CompositorBridge::QueryWlroots() {
    WindowInfo info;
    
    std::string command;
    
    switch (compositorType) {
        case CompositorType::Sway:
            command = "swaymsg -t get_tree 2>/dev/null";
            break;
        case CompositorType::Hyprland:
            command = "hyprctl activewindow -j 2>/dev/null";
            break;
        default:
            // For River, Wayfire, etc., no standard IPC yet
            return info;
    }
    
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        debug("Failed to execute compositor query command");
        return info;
    }
    
    std::ostringstream output;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        output << buffer;
    }
    pclose(pipe);
    
    std::string result = output.str();
    if (result.empty()) {
        return info;
    }
    
    // Simple JSON parsing (avoiding dependency on full JSON library)
    // This is fragile but works for our limited use case
    
    auto extractJsonString = [&result](const std::string& key) -> std::string {
        std::string searchKey = "\"" + key + "\":\"";
        size_t pos = result.find(searchKey);
        if (pos == std::string::npos) {
            searchKey = "\"" + key + "\": \"";  // Try with space
            pos = result.find(searchKey);
        }
        if (pos != std::string::npos) {
            pos += searchKey.length();
            size_t endPos = result.find("\"", pos);
            if (endPos != std::string::npos) {
                return result.substr(pos, endPos - pos);
            }
        }
        return "";
    };
    
    auto extractJsonInt = [&result](const std::string& key) -> int {
        std::string searchKey = "\"" + key + "\":";
        size_t pos = result.find(searchKey);
        if (pos == std::string::npos) {
            searchKey = "\"" + key + "\": ";  // Try with space
            pos = result.find(searchKey);
        }
        if (pos != std::string::npos) {
            pos += searchKey.length();
            // Skip whitespace
            while (pos < result.length() && std::isspace(result[pos])) {
                pos++;
            }
            size_t endPos = pos;
            while (endPos < result.length() && std::isdigit(result[endPos])) {
                endPos++;
            }
            if (endPos > pos) {
                try {
                    return std::stoi(result.substr(pos, endPos - pos));
                } catch (...) {
                    return 0;
                }
            }
        }
        return 0;
    };
    
    if (compositorType == CompositorType::Sway) {
        // For Sway, we need to find the focused node
        // Look for "focused": true
        size_t focusedPos = result.find("\"focused\": true");
        if (focusedPos != std::string::npos) {
            // Search backwards for the start of this object
            size_t objectStart = result.rfind("{", focusedPos);
            if (objectStart != std::string::npos) {
                std::string focusedObject = result.substr(objectStart, 
                                                         result.find("}", focusedPos) - objectStart);
                
                // Extract from this substring
                auto extractFromSubstr = [&focusedObject](const std::string& key) -> std::string {
                    std::string searchKey = "\"" + key + "\":\"";
                    size_t pos = focusedObject.find(searchKey);
                    if (pos != std::string::npos) {
                        pos += searchKey.length();
                        size_t endPos = focusedObject.find("\"", pos);
                        if (endPos != std::string::npos) {
                            return focusedObject.substr(pos, endPos - pos);
                        }
                    }
                    return "";
                };
                
                info.title = extractFromSubstr("name");
                info.appId = extractFromSubstr("app_id");
                info.pid = extractJsonInt("pid");
            }
        }
    } else if (compositorType == CompositorType::Hyprland) {
        info.title = extractJsonString("title");
        info.appId = extractJsonString("class");
        info.pid = extractJsonInt("pid");
    }
    
    if (!info.title.empty() || !info.appId.empty()) {
        info.valid = true;
        debug("wlroots active window: title='{}' appId='{}' pid={}", 
              info.title, info.appId, info.pid);
    }
    
    return info;
}

// Static methods for qdbus communication
bool CompositorBridge::IsKDERunning() {
    // Check if KDE session is active
    const char* kdeSession = std::getenv("KDE_SESSION_VERSION");
    if (kdeSession && std::string(kdeSession) == "5") {
        return true;
    }

    const char* desktopSession = std::getenv("DESKTOP_SESSION");
    if (desktopSession && std::string(desktopSession).find("plasma") != std::string::npos) {
        return true;
    }

    // Check if KWin is running
    return system("pgrep -x kwin_wayland >/dev/null 2>&1") == 0 ||
           system("pgrep -x kwin_x11 >/dev/null 2>&1") == 0;
}

bool CompositorBridge::SendKWinZoomCommand(const std::string& command) {
    std::string fullCommand = "qdbus " + command + " 2>/dev/null";
    int result = system(fullCommand.c_str());
    return result == 0;
}

std::string CompositorBridge::SendKWinZoomCommandWithOutput(const std::string& command) {
    std::string fullCommand = "qdbus " + command + " 2>/dev/null";

    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(fullCommand.c_str(), "r"), pclose);

    if (!pipe) {
        error("Failed to execute qdbus command: {}", command);
        return "";
    }

    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }

    // Remove trailing newline if present
    if (!result.empty() && result.back() == '\n') {
        result.pop_back();
    }

    return result;
}

} // namespace havel