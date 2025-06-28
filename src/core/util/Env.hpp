#pragma once

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <optional>
#include <regex>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <userenv.h>
#pragma comment(lib, "userenv.lib")
#pragma comment(lib, "shell32.lib")
#else
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#include <wordexp.h>
#endif
namespace havel {
class Env {
public:
    // Singleton access
    static Env& instance() {
        static Env env;
        return env;
    }
    
    // Path expansion and resolution
    static std::string expand(const std::string& path);
    static std::string expandAll(const std::string& text);
    static std::string resolve(const std::string& path);
    static std::string canonicalize(const std::string& path);
    
    // Environment variable operations
    static std::string get(const std::string& name, const std::string& defaultValue = "");
    static bool set(const std::string& name, const std::string& value, bool overwrite = true);
    static bool unset(const std::string& name);
    static bool exists(const std::string& name);
    static std::map<std::string, std::string> getAll();
    
    // System paths
    static std::string home();
    static std::string temp();
    static std::string current();
    static std::string executable();
    static std::string documents();
    static std::string desktop();
    static std::string downloads();
    static std::string config();
    static std::string cache();
    static std::string data();
    
    // PATH operations
    static std::vector<std::string> getPath();
    static bool addToPath(const std::string& directory, bool prepend = false);
    static bool removeFromPath(const std::string& directory);
    static std::string which(const std::string& command);
    static std::vector<std::string> whichAll(const std::string& command);
    
    // Platform info
    static std::string platform();
    static std::string architecture();
    static std::string hostname();
    static std::string username();
    static std::string shell();
    static bool isAdmin();
    
    // Utility functions
    static std::string join(const std::vector<std::string>& paths);
    static std::vector<std::string> split(const std::string& path);
    static bool isAbsolute(const std::string& path);
    static bool isRelative(const std::string& path);
    static std::string makeRelative(const std::string& path, const std::string& base = "");
    static std::string makeAbsolute(const std::string& path, const std::string& base = "");
    
    // File system helpers
    static bool exists(const std::string& path);
    static bool isFile(const std::string& path);
    static bool isDirectory(const std::string& path);
    static bool isExecutable(const std::string& path);
    
    // Advanced features
    static std::string substitute(const std::string& text, const std::map<std::string, std::string>& vars = {});
    static std::vector<std::string> glob(const std::string& pattern);
    static void setEnvironment(const std::map<std::string, std::string>& env, bool merge = true);
    static std::map<std::string, std::string> parseEnvFile(const std::string& filepath);
    static bool loadEnvFile(const std::string& filepath, bool overwrite = false);

private:
    Env() = default;
    ~Env() = default;
    Env(const Env&) = delete;
    Env& operator=(const Env&) = delete;
    
    static std::string expandTilde(const std::string& path);
    static std::string expandVariables(const std::string& text);
    static std::string getSpecialFolder(int csidl);
    static std::string unixExpandPath(const std::string& path);
    static std::vector<std::string> splitPath(const std::string& pathStr);
};
}