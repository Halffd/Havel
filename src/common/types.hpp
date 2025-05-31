#pragma once
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <algorithm>

namespace havel {

// Common type aliases
using str = std::string;
using cstr = std::string;

#ifdef _WIN32
using wID = unsigned long long;
using pID = unsigned long;
#else
using wID = unsigned long;
using pID = unsigned long;
#endif

// Define group type
using group = std::map<std::string, std::vector<std::string>>;

// Display server types
enum class DisplayServer {
    Unknown,
    X11,
    Wayland
};

// Process method enum
enum class ProcessMethodType {
    WaitForTerminate,
    ForkProcess,
    CreateProcess,
    ShellExecute,
    System
};

// Rectangle structure
struct Rect {
    int x, y, width, height;
    
    Rect() : x(0), y(0), width(0), height(0) {}
    Rect(int x, int y, int w, int h) : x(x), y(y), width(w), height(h) {}
};

// Helper function to convert string to lowercase
inline std::string ToLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), 
                  [](unsigned char c) { return std::tolower(c); });
    return result;
}

// Common typedefs for key handling
#ifndef Key_typedef
#define Key_typedef
typedef unsigned long Key;
#endif

// Helper struct for Windows EnumWindows
#ifdef _WIN32
struct EnumWindowsData {
    std::string targetProcessName;
    wID id = 0;
    
    explicit EnumWindowsData(const std::string& name) : targetProcessName(name) {}
};
#endif

} // namespace havel
