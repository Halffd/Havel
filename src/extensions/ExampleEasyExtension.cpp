/*
 * ExampleEasyExtension.cpp - Demonstrates easy extension development
 * 
 * This shows how the new ExtensionMacros and EasyImport simplify
 * extension development:
 * 
 * 1. Single include brings in everything needed
 * 2. Automatic library detection (GTK, Qt, SDL, etc.)
 * 3. Simple EXTENSION macro for registration
 * 4. Easy function registration with error handling
 */

#include "extensions/EasyImport.hpp"

// ============================================================================
// EXAMPLE 1: Simple extension with auto-library detection
// ============================================================================

// Try to load GTK automatically (works if available, gracefully fails if not)
EXTENSION_USE_GTK();

// Try to load SDL automatically
EXTENSION_USE_SDL();

// Try to load OpenCV automatically
EXTENSION_USE_OPENCV();

// Register the extension with metadata
EXTENSION(example_easy, "1.0.0", "Example extension demonstrating easy development") {
    EXT_LOG_INFO("Initializing example_easy extension");
    EXT_LOG_INFO("Platform: %s, Arch: %s, Mode: %s", 
                 EXT_PLATFORM, EXT_ARCH, EXT_LIBRARY_MODE);
    
    // Check which libraries were auto-detected at runtime
    EXT_LOG_INFO("Runtime library detection:");
    EXT_LOG_INFO("  GTK: %s", GTK_LOADED ? "available" : "not available");
    EXT_LOG_INFO("  SDL: %s", SDL_LOADED ? "available" : "not available");
    EXT_LOG_INFO("  OpenCV: %s", CV_LOADED ? "available" : "not available");
    
    // Register a simple function
    EXT_REGISTER_SAFE(api, "example", "hello", [](const std::vector<havel::Value>& args) {
        (void)args;
        EXT_RETURN_STRING(api, "Hello from easy extension!");
    });
    
    // Register function that uses detected libraries if available
    EXT_REGISTER_SAFE(api, "example", "check_libs", [](const std::vector<havel::Value>& args) {
        (void)args;
        std::string result = "Libraries:\n";
        result += "GTK: " + std::string(GTK_LOADED ? "yes" : "no") + "\n";
        result += "SDL: " + std::string(SDL_LOADED ? "yes" : "no") + "\n";
        result += "OpenCV: " + std::string(CV_LOADED ? "yes" : "no") + "\n";
        EXT_RETURN_STRING(api, result);
    });
    
    // Register function with argument handling
    EXT_REGISTER_SAFE(api, "example", "add", [](const std::vector<havel::Value>& args) {
        if (EXT_ARG_COUNT(args) < 2) {
            EXT_RETURN_NULL(api);
        }
        int a = EXT_ARG_INT(args, 0);
        int b = EXT_ARG_INT(args, 1);
        EXT_RETURN_INT(api, a + b);
    });
    
    EXT_LOG_INFO("example_easy extension registered 3 functions");
}

// ============================================================================
// EXAMPLE 2: Library-specific extension (only registers if lib available)
// ============================================================================

#ifdef EXTENSION_USE_FFMPEG
EXTENSION(video_tools, "1.0.0", "Video processing tools (requires FFmpeg)") {
    if (!FFMPEG_LOADED) {
        EXT_LOG_ERROR("FFmpeg not available, video_tools extension disabled");
        return;
    }
    
    EXT_LOG_INFO("FFmpeg loaded, registering video tools");
    
    // Use FFmpeg functions via the dynamic loader
    // auto avcodec_version = FFMPEG_GET_FN(unsigned int (*)(void), "avcodec_version");
    
    EXT_REGISTER_SAFE(api, "video", "version", [](const std::vector<havel::Value>& args) {
        (void)args;
        EXT_RETURN_STRING(api, "FFmpeg video tools v1.0");
    });
}
#endif

// ============================================================================
// EXAMPLE 3: Conditional extension based on compile-time features
// ============================================================================

#if EXT_HAS_FEATURE(SQLITE)
EXTENSION(sqlite_helper, "1.0.0", "SQLite helper functions") {
    EXT_LOG_INFO("SQLite is available at compile time");
    
    EXT_REGISTER_SAFE(api, "sqlite", "version", [](const std::vector<havel::Value>& args) {
        (void)args;
        EXT_RETURN_STRING(api, sqlite3_libversion());
    });
}
#endif

// ============================================================================
// EXAMPLE 4: Extension using multiple auto-detected libraries
// ============================================================================

EXTENSION(multi_lib, "1.0.0", "Multi-library extension") {
    EXT_LOG_INFO("Checking available libraries...");
    
    int libCount = 0;
    if (GTK_LOADED) libCount++;
    if (QT_LOADED) libCount++;
    if (SDL_LOADED) libCount++;
    if (CV_LOADED) libCount++;
    if (X11_LOADED) libCount++;
    
    EXT_LOG_INFO("Found %d loadable libraries", libCount);
    
    EXT_REGISTER_SAFE(api, "multi", "count", [libCount](const std::vector<havel::Value>& args) {
        (void)args;
        EXT_RETURN_INT(api, libCount);
    });
    
    EXT_REGISTER_SAFE(api, "multi", "features", [](const std::vector<havel::Value>& args) {
        (void)args;
        std::string features = havel::ext::getFeatureSummary();
        EXT_RETURN_STRING(api, features);
    });
}

// ============================================================================
// EXAMPLE 5: Platform-specific extension
// ============================================================================

#if EXT_PLATFORM == "linux"
EXTENSION(linux_tools, "1.0.0", "Linux-specific tools") {
    EXT_LOG_INFO("Running on Linux, registering Linux-specific functions");
    
    EXT_REGISTER_SAFE(api, "linux", "pid", [](const std::vector<havel::Value>& args) {
        (void)args;
        EXT_RETURN_INT(api, getpid());
    });
}
#endif
