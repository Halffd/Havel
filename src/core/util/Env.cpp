#include "Env.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <cstdlib>

#ifdef _WIN32
#include <io.h>
#define access _access
#define F_OK 0
#else
#include <glob.h>
#include <climits>
#endif

namespace fs = std::filesystem;
namespace havel {
// Path expansion and resolution
std::string Env::expand(const std::string& path) {
    if (path.empty()) return path;
    
    std::string result = expandTilde(path);
    result = expandVariables(result);
    
#ifndef _WIN32
    // Use wordexp for full shell expansion on Unix
    wordexp_t exp;
    if (wordexp(result.c_str(), &exp, WRDE_NOCMD) == 0) {
        if (exp.we_wordc > 0) {
            result = exp.we_wordv[0];
        }
        wordfree(&exp);
    }
#endif
    
    return result;
}

std::string Env::expandAll(const std::string& text) {
    return expandVariables(text);
}

std::string Env::resolve(const std::string& path) {
    try {
        return fs::weakly_canonical(expand(path)).string();
    } catch (...) {
        return expand(path);
    }
}

std::string Env::canonicalize(const std::string& path) {
    try {
        return fs::canonical(expand(path)).string();
    } catch (...) {
        return resolve(path);
    }
}

// Environment variable operations
std::string Env::get(const std::string& name, const std::string& defaultValue) {
    const char* value = std::getenv(name.c_str());
    return value ? std::string(value) : defaultValue;
}

bool Env::set(const std::string& name, const std::string& value, bool overwrite) {
    if (!overwrite && exists(name)) {
        return true;
    }
    
#ifdef _WIN32
    return SetEnvironmentVariableA(name.c_str(), value.c_str()) != 0;
#else
    return setenv(name.c_str(), value.c_str(), overwrite ? 1 : 0) == 0;
#endif
}

bool Env::unset(const std::string& name) {
#ifdef _WIN32
    return SetEnvironmentVariableA(name.c_str(), nullptr) != 0;
#else
    return unsetenv(name.c_str()) == 0;
#endif
}

bool Env::exists(const std::string& name) {
    return std::getenv(name.c_str()) != nullptr;
}

std::map<std::string, std::string> Env::getAll() {
    std::map<std::string, std::string> env;
    
#ifdef _WIN32
    LPCH envStrings = GetEnvironmentStringsA();
    if (envStrings) {
        char* current = envStrings;
        while (*current) {
            std::string line(current);
            size_t pos = line.find('=');
            if (pos != std::string::npos) {
                env[line.substr(0, pos)] = line.substr(pos + 1);
            }
            current += line.length() + 1;
        }
        FreeEnvironmentStringsA(envStrings);
    }
#else
    extern char** environ;
    for (char** current = environ; *current; ++current) {
        std::string line(*current);
        size_t pos = line.find('=');
        if (pos != std::string::npos) {
            env[line.substr(0, pos)] = line.substr(pos + 1);
        }
    }
#endif
    
    return env;
}

// System paths
std::string Env::home() {
#ifdef _WIN32
    return getSpecialFolder(CSIDL_PROFILE);
#else
    const char* home = getenv("HOME");
    if (home) return std::string(home);
    
    struct passwd* pw = getpwuid(getuid());
    return pw ? std::string(pw->pw_dir) : "/tmp";
#endif
}

std::string Env::temp() {
#ifdef _WIN32
    char tempPath[MAX_PATH];
    DWORD length = GetTempPathA(MAX_PATH, tempPath);
    return length > 0 ? std::string(tempPath, length - 1) : "C:\\temp";
#else
    const char* tmpdir = getenv("TMPDIR");
    if (tmpdir) return std::string(tmpdir);
    
    tmpdir = getenv("TMP");
    if (tmpdir) return std::string(tmpdir);
    
    return "/tmp";
#endif
}

std::string Env::current() {
    try {
        return fs::current_path().string();
    } catch (...) {
        return ".";
    }
}

std::string Env::executable() {
#ifdef _WIN32
    char path[MAX_PATH];
    if (GetModuleFileNameA(nullptr, path, MAX_PATH)) {
        return std::string(path);
    }
#else
    char path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len != -1) {
        path[len] = '\0';
        return std::string(path);
    }
#endif
    return "";
}

std::string Env::documents() {
#ifdef _WIN32
    return getSpecialFolder(CSIDL_MYDOCUMENTS);
#else
    return join({home(), "Documents"});
#endif
}

std::string Env::desktop() {
#ifdef _WIN32
    return getSpecialFolder(CSIDL_DESKTOP);
#else
    return join({home(), "Desktop"});
#endif
}

std::string Env::downloads() {
#ifdef _WIN32
    return getSpecialFolder(CSIDL_PROFILE) + "\\Downloads";
#else
    return join({home(), "Downloads"});
#endif
}

std::string Env::config() {
#ifdef _WIN32
    return getSpecialFolder(CSIDL_APPDATA);
#else
    std::string xdg = get("XDG_CONFIG_HOME");
    return xdg.empty() ? join({home(), ".config"}) : xdg;
#endif
}

std::string Env::cache() {
#ifdef _WIN32
    return getSpecialFolder(CSIDL_LOCAL_APPDATA);
#else
    std::string xdg = get("XDG_CACHE_HOME");
    return xdg.empty() ? join({home(), ".cache"}) : xdg;
#endif
}

std::string Env::data() {
#ifdef _WIN32
    return getSpecialFolder(CSIDL_APPDATA);
#else
    std::string xdg = get("XDG_DATA_HOME");
    return xdg.empty() ? join({home(), ".local", "share"}) : xdg;
#endif
}

// PATH operations
std::vector<std::string> Env::getPath() {
    return splitPath(get("PATH"));
}

bool Env::addToPath(const std::string& directory, bool prepend) {
    auto paths = getPath();
    
    // Remove if already exists
    paths.erase(std::remove(paths.begin(), paths.end(), directory), paths.end());
    
    if (prepend) {
        paths.insert(paths.begin(), directory);
    } else {
        paths.push_back(directory);
    }
    
#ifdef _WIN32
    std::string newPath = "";
    for (size_t i = 0; i < paths.size(); ++i) {
        if (i > 0) newPath += ";";
        newPath += paths[i];
    }
#else
    std::string newPath = "";
    for (size_t i = 0; i < paths.size(); ++i) {
        if (i > 0) newPath += ":";
        newPath += paths[i];
    }
#endif
    
    return set("PATH", newPath);
}

bool Env::removeFromPath(const std::string& directory) {
    auto paths = getPath();
    auto it = std::find(paths.begin(), paths.end(), directory);
    
    if (it == paths.end()) return true;
    
    paths.erase(it);
    
#ifdef _WIN32
    std::string newPath = "";
    for (size_t i = 0; i < paths.size(); ++i) {
        if (i > 0) newPath += ";";
        newPath += paths[i];
    }
#else
    std::string newPath = "";
    for (size_t i = 0; i < paths.size(); ++i) {
        if (i > 0) newPath += ":";
        newPath += paths[i];
    }
#endif
    
    return set("PATH", newPath);
}

std::string Env::which(const std::string& command) {
    auto results = whichAll(command);
    return results.empty() ? "" : results[0];
}

std::vector<std::string> Env::whichAll(const std::string& command) {
    std::vector<std::string> results;
    auto paths = getPath();
    
#ifdef _WIN32
    std::vector<std::string> extensions = {".exe", ".bat", ".cmd", ".com"};
    if (command.find('.') != std::string::npos) {
        extensions = {""};
    }
#else
    std::vector<std::string> extensions = {""};
#endif
    
    for (const auto& path : paths) {
        for (const auto& ext : extensions) {
            std::string fullPath = join({path, command + ext});
            if (isExecutable(fullPath)) {
                results.push_back(fullPath);
            }
        }
    }
    
    return results;
}

// Platform info
std::string Env::platform() {
#ifdef _WIN32
    return "windows";
#elif defined(__linux__)
    return "linux";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__FreeBSD__)
    return "freebsd";
#else
    return "unknown";
#endif
}

std::string Env::architecture() {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: return "x64";
        case PROCESSOR_ARCHITECTURE_ARM: return "arm";
        case PROCESSOR_ARCHITECTURE_ARM64: return "arm64";
        case PROCESSOR_ARCHITECTURE_INTEL: return "x86";
        default: return "unknown";
    }
#else
    // Simple detection - you might want to use uname() for more accuracy
    #ifdef __x86_64__
        return "x64";
    #elif defined(__i386__)
        return "x86";
    #elif defined(__aarch64__)
        return "arm64";
    #elif defined(__arm__)
        return "arm";
    #else
        return "unknown";
    #endif
#endif
}

std::string Env::hostname() {
    std::string Env::hostname() {
        #ifdef _WIN32
            char buffer[MAX_COMPUTERNAME_LENGTH + 1];
            DWORD size = sizeof(buffer);
            if (GetComputerNameA(buffer, &size)) {
                return std::string(buffer);
            }
            return get("COMPUTERNAME", "unknown");
        #else
            char buffer[256];
            if (gethostname(buffer, sizeof(buffer)) == 0) {
                buffer[sizeof(buffer) - 1] = '\0';  // Ensure null termination
                return std::string(buffer);
            }
            return get("HOSTNAME", "unknown");
        #endif
        }
        
        std::string Env::username() {
        #ifdef _WIN32
            char buffer[UNLEN + 1];
            DWORD size = sizeof(buffer);
            if (GetUserNameA(buffer, &size)) {
                return std::string(buffer);
            }
            return get("USERNAME", "unknown");
        #else
            const char* user = getenv("USER");
            if (user) return std::string(user);
            
            struct passwd* pw = getpwuid(getuid());
            return pw ? std::string(pw->pw_name) : "unknown";
        #endif
        }
        
        std::string Env::shell() {
        #ifdef _WIN32
            std::string comspec = get("COMSPEC");
            if (!comspec.empty()) return comspec;
            
            // Check for PowerShell
            if (isExecutable("C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe")) {
                return "powershell.exe";
            }
            return "cmd.exe";
        #else
            std::string shell = get("SHELL");
            if (!shell.empty()) return shell;
            
            struct passwd* pw = getpwuid(getuid());
            return pw && pw->pw_shell ? std::string(pw->pw_shell) : "/bin/sh";
        #endif
        }
        
        bool Env::isAdmin() {
        #ifdef _WIN32
            BOOL isAdmin = FALSE;
            HANDLE token = nullptr;
            
            if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
                TOKEN_ELEVATION elevation;
                DWORD size = sizeof(elevation);
                
                if (GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) {
                    isAdmin = elevation.TokenIsElevated;
                }
                CloseHandle(token);
            }
            return isAdmin != FALSE;
        #else
            return getuid() == 0;
        #endif
        }
        
        // Utility functions
        std::string Env::join(const std::vector<std::string>& paths) {
            if (paths.empty()) return "";
            if (paths.size() == 1) return paths[0];
            
            std::string result = paths[0];
            for (size_t i = 1; i < paths.size(); ++i) {
                if (!result.empty() && result.back() != '/' && result.back() != '\\') {
        #ifdef _WIN32
                    result += '\\';
        #else
                    result += '/';
        #endif
                }
                result += paths[i];
            }
            return result;
        }
        
        std::vector<std::string> Env::split(const std::string& path) {
            std::vector<std::string> parts;
            std::string current;
            
            for (char c : path) {
                if (c == '/' || c == '\\') {
                    if (!current.empty()) {
                        parts.push_back(current);
                        current.clear();
                    }
                } else {
                    current += c;
                }
            }
            
            if (!current.empty()) {
                parts.push_back(current);
            }
            
            return parts;
        }
        
        bool Env::isAbsolute(const std::string& path) {
            if (path.empty()) return false;
            
        #ifdef _WIN32
            return (path.length() >= 3 && path[1] == ':' && (path[2] == '\\' || path[2] == '/')) ||
                   (path.length() >= 2 && path[0] == '\\' && path[1] == '\\');
        #else
            return path[0] == '/';
        #endif
        }
        
        bool Env::isRelative(const std::string& path) {
            return !isAbsolute(path);
        }
        
        std::string Env::makeRelative(const std::string& path, const std::string& base) {
            try {
                fs::path p = fs::path(expand(path));
                fs::path b = base.empty() ? fs::current_path() : fs::path(expand(base));
                return fs::relative(p, b).string();
            } catch (...) {
                return path;
            }
        }
        
        std::string Env::makeAbsolute(const std::string& path, const std::string& base) {
            if (isAbsolute(path)) return expand(path);
            
            try {
                fs::path b = base.empty() ? fs::current_path() : fs::path(expand(base));
                return (b / path).string();
            } catch (...) {
                return path;
            }
        }
        
        // File system helpers
        bool Env::exists(const std::string& path) {
            return access(expand(path).c_str(), F_OK) == 0;
        }
        
        bool Env::isFile(const std::string& path) {
            try {
                return fs::is_regular_file(expand(path));
            } catch (...) {
                return false;
            }
        }
        
        bool Env::isDirectory(const std::string& path) {
            try {
                return fs::is_directory(expand(path));
            } catch (...) {
                return false;
            }
        }
        
        bool Env::isExecutable(const std::string& path) {
        #ifdef _WIN32
            return isFile(path);  // On Windows, if it's a file, we can try to execute it
        #else
            return access(expand(path).c_str(), X_OK) == 0;
        #endif
        }
        
        // Advanced features
        std::string Env::substitute(const std::string& text, const std::map<std::string, std::string>& vars) {
            std::string result = text;
            
            // First apply custom variables
            for (const auto& pair : vars) {
                std::string placeholder = "${" + pair.first + "}";
                size_t pos = 0;
                while ((pos = result.find(placeholder, pos)) != std::string::npos) {
                    result.replace(pos, placeholder.length(), pair.second);
                    pos += pair.second.length();
                }
            }
            
            // Then expand environment variables
            return expandVariables(result);
        }
        
        std::vector<std::string> Env::glob(const std::string& pattern) {
            std::vector<std::string> matches;
            
        #ifdef _WIN32
            // Windows glob implementation using FindFirstFile/FindNextFile
            WIN32_FIND_DATAA findData;
            HANDLE hFind = FindFirstFileA(expand(pattern).c_str(), &findData);
            
            if (hFind != INVALID_HANDLE_VALUE) {
                do {
                    matches.push_back(findData.cFileName);
                } while (FindNextFileA(hFind, &findData));
                FindClose(hFind);
            }
        #else
            glob_t globResult;
            if (::glob(expand(pattern).c_str(), GLOB_TILDE, nullptr, &globResult) == 0) {
                for (size_t i = 0; i < globResult.gl_pathc; ++i) {
                    matches.push_back(globResult.gl_pathv[i]);
                }
                globfree(&globResult);
            }
        #endif
            
            return matches;
        }
        
        void Env::setEnvironment(const std::map<std::string, std::string>& env, bool merge) {
            if (!merge) {
                // Clear existing environment (platform-specific implementation needed)
                auto current = getAll();
                for (const auto& pair : current) {
                    unset(pair.first);
                }
            }
            
            for (const auto& pair : env) {
                set(pair.first, pair.second, true);
            }
        }
        
        std::map<std::string, std::string> Env::parseEnvFile(const std::string& filepath) {
            std::map<std::string, std::string> env;
            std::ifstream file(expand(filepath));
            std::string line;
            
            while (std::getline(file, line)) {
                // Skip empty lines and comments
                if (line.empty() || line[0] == '#') continue;
                
                size_t pos = line.find('=');
                if (pos != std::string::npos) {
                    std::string key = line.substr(0, pos);
                    std::string value = line.substr(pos + 1);
                    
                    // Remove quotes if present
                    if (value.length() >= 2 && 
                        ((value.front() == '"' && value.back() == '"') ||
                         (value.front() == '\'' && value.back() == '\''))) {
                        value = value.substr(1, value.length() - 2);
                    }
                    
                    env[key] = value;
                }
            }
            
            return env;
        }
        
        bool Env::loadEnvFile(const std::string& filepath, bool overwrite) {
            try {
                auto env = parseEnvFile(filepath);
                for (const auto& pair : env) {
                    set(pair.first, pair.second, overwrite);
                }
                return true;
            } catch (...) {
                return false;
            }
        }
        
        // Private helper methods
        std::string Env::expandTilde(const std::string& path) {
            if (path.empty() || path[0] != '~') return path;
            
            if (path.length() == 1 || path[1] == '/' || path[1] == '\\') {
                return home() + path.substr(1);
            }
            
            // Handle ~username (Unix only)
        #ifndef _WIN32
            size_t pos = path.find_first_of("/\\");
            std::string username = path.substr(1, pos == std::string::npos ? std::string::npos : pos - 1);
            
            struct passwd* pw = getpwnam(username.c_str());
            if (pw) {
                return std::string(pw->pw_dir) + (pos == std::string::npos ? "" : path.substr(pos));
            }
        #endif
            
            return path;
        }
        
        std::string Env::expandVariables(const std::string& text) {
            std::string result = text;
            std::regex varRegex(R"(\$\{([^}]+)\}|\$([A-Za-z_][A-Za-z0-9_]*))");
            std::smatch match;
            
            while (std::regex_search(result, match, varRegex)) {
                std::string varName = match[1].matched ? match[1].str() : match[2].str();
                std::string varValue = get(varName);
                
                result.replace(match.position(), match.length(), varValue);
            }
            
            return result;
        }
        
        std::string Env::getSpecialFolder(int csidl) {
        #ifdef _WIN32
            char path[MAX_PATH];
            if (SHGetFolderPathA(nullptr, csidl, nullptr, SHGFP_TYPE_CURRENT, path) == S_OK) {
                return std::string(path);
            }
        #endif
            return "";
        }
        
        std::vector<std::string> Env::splitPath(const std::string& pathStr) {
            std::vector<std::string> paths;
            std::string current;
            
        #ifdef _WIN32
            char delimiter = ';';
        #else
            char delimiter = ':';
        #endif
            
            for (char c : pathStr) {
                if (c == delimiter) {
                    if (!current.empty()) {
                        paths.push_back(current);
                        current.clear();
                    }
                } else {
                    current += c;
                }
            }
            
            if (!current.empty()) {
                paths.push_back(current);
            }
            
            return paths;
        }
    }
}