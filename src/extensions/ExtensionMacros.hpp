/*
 * ExtensionMacros.hpp - Easy extension macros for dynamic/static library configuration
 * 
 * PURPOSE:
 * - Automatic library detection and configuration
 * - Simplified extension registration
 * - Static/dynamic linking helpers
 * - Auto-detection of common system libraries
 * 
 * USAGE:
 *   #include "extensions/ExtensionMacros.hpp"
 *   
 *   // Auto-detect and use GTK if available
 *   EXTENSION_AUTO_LIBRARY(GTK, gtk, "libgtk-4.so", "libgtk-3.so")
 *   
 *   EXTENSION(my_extension, "1.0.0", "My cool extension") {
 *       // Register functions using the detected GTK
 *       if (GTK_LOADED) {
 *           auto fn = GTK_GET_FN(void (*)(int), "gtk_init");
 *       }
 *   }
 */

#pragma once

#include "HavelCAPI.h"
#include "DynamicLoader.hpp"
#include "ExtensionAPI.hpp"
#include <cstdio>

// ============================================================================
// EXTENSION REGISTRATION MACROS
// ============================================================================

// Full-featured extension registration with metadata
#define EXTENSION(name, version, description) \
    static void name##_ext_body(havel::ExtensionAPI& api); \
    static const char* name##_version = version; \
    static const char* name##_description = description; \
    HAVEL_EXTENSION_CPP(name) { name##_ext_body(api); } \
    static void name##_ext_body(havel::ExtensionAPI& api)

// Simple extension (auto-generates name from filename)
#define EXTENSION_SIMPLE() \
    static void _havel_simple_ext_body(havel::ExtensionAPI& api); \
    HAVEL_EXTENSION_CPP(simple_ext) { _havel_simple_ext_body(api); } \
    static void _havel_simple_ext_body(havel::ExtensionAPI& api)

// Extension with custom init function name (for multiple extensions in one file)
#define EXTENSION_NAMED(name) \
    static void name##_ext_body(havel::ExtensionAPI& api); \
    extern "C" void havel_##name##_init(HavelAPI* c_api) { \
        havel::ExtensionAPI cpp_api(c_api); \
        name##_ext_body(cpp_api); \
    } \
    static void name##_ext_body(havel::ExtensionAPI& api)

// ============================================================================
// AUTO-LIBRARY DETECTION MACROS
// ============================================================================

// Try to load a library with multiple possible names
#define EXTENSION_AUTO_LIBRARY(name, prefix, ...) \
    static ::havel::DynamicLoader _##prefix##_loader; \
    static bool _##prefix##_initialized = []() { \
        const char* libs[] = { __VA_ARGS__ }; \
        for (const char* lib : libs) { \
            if (_##prefix##_loader.load(lib)) { \
                fprintf(stderr, "[Extension] Loaded " #name " from %s\n", lib); \
                return true; \
            } \
        } \
        fprintf(stderr, "[Extension] " #name " not available (tried " #__VA_ARGS__ ")\n"); \
        return false; \
    }();

// Check if library was loaded
#define EXTENSION_LIB_LOADED(prefix) (_##prefix##_initialized)

// Get function from library (returns nullptr if not loaded)
#define EXTENSION_LIB_GET_FN(prefix, type, fn_name) \
    (_##prefix##_initialized ? _##prefix##_loader.getSymbol<type>(fn_name) : nullptr)

// ============================================================================
// COMMON LIBRARY AUTO-CONFIGURATION
// ============================================================================

// GTK detection (tries GTK4, then GTK3)
#define EXTENSION_USE_GTK() \
    EXTENSION_AUTO_LIBRARY(GTK, gtk, "libgtk-4.so", "libgtk-3.so", "libgtk-4.so.1", "libgtk-3.so.0")
#define GTK_LOADED EXTENSION_LIB_LOADED(gtk)
#define GTK_GET_FN(type, name) EXTENSION_LIB_GET_FN(gtk, type, name)

// Qt detection (tries Qt6, then Qt5)
#define EXTENSION_USE_QT() \
    EXTENSION_AUTO_LIBRARY(Qt, qt, "libQt6Core.so", "libQt5Core.so", "libQt6Core.so.6", "libQt5Core.so.5")
#define QT_LOADED EXTENSION_LIB_LOADED(qt)
#define QT_GET_FN(type, name) EXTENSION_LIB_GET_FN(qt, type, name)

// SDL detection
#define EXTENSION_USE_SDL() \
    EXTENSION_AUTO_LIBRARY(SDL, sdl, "libSDL2-2.0.so", "libSDL2.so", "libSDL2-2.0.so.0")
#define SDL_LOADED EXTENSION_LIB_LOADED(sdl)
#define SDL_GET_FN(type, name) EXTENSION_LIB_GET_FN(sdl, type, name)

// OpenGL detection
#define EXTENSION_USE_OPENGL() \
    EXTENSION_AUTO_LIBRARY(OpenGL, gl, "libGL.so", "libGL.so.1", "libOpenGL.so")
#define GL_LOADED EXTENSION_LIB_LOADED(gl)
#define GL_GET_FN(type, name) EXTENSION_LIB_GET_FN(gl, type, name)

// OpenCV detection
#define EXTENSION_USE_OPENCV() \
    EXTENSION_AUTO_LIBRARY(OpenCV, cv, "libopencv_core.so", "libopencv_core.so.4", "libopencv_core.so.406")
#define CV_LOADED EXTENSION_LIB_LOADED(cv)
#define CV_GET_FN(type, name) EXTENSION_LIB_GET_FN(cv, type, name)

// FFmpeg detection
#define EXTENSION_USE_FFMPEG() \
    EXTENSION_AUTO_LIBRARY(FFmpeg, ffmpeg, "libavcodec.so", "libavcodec.so.58", "libavcodec.so.59")
#define FFMPEG_LOADED EXTENSION_LIB_LOADED(ffmpeg)
#define FFMPEG_GET_FN(type, name) EXTENSION_LIB_GET_FN(ffmpeg, type, name)

// Audio library detection (tries multiple)
#define EXTENSION_USE_AUDIO() \
    EXTENSION_AUTO_LIBRARY(Audio, audio, "libpulse.so", "libasound.so", "libpipewire-0.3.so")
#define AUDIO_LOADED EXTENSION_LIB_LOADED(audio)
#define AUDIO_GET_FN(type, name) EXTENSION_LIB_GET_FN(audio, type, name)

// FontConfig detection
#define EXTENSION_USE_FONTCONFIG() \
    EXTENSION_AUTO_LIBRARY(FontConfig, fc, "libfontconfig.so", "libfontconfig.so.1")
#define FC_LOADED EXTENSION_LIB_LOADED(fc)
#define FC_GET_FN(type, name) EXTENSION_LIB_GET_FN(fc, type, name)

// X11/Wayland detection
#define EXTENSION_USE_X11() \
    EXTENSION_AUTO_LIBRARY(X11, x11, "libX11.so", "libX11.so.6")
#define X11_LOADED EXTENSION_LIB_LOADED(x11)
#define X11_GET_FN(type, name) EXTENSION_LIB_GET_FN(x11, type, name)

#define EXTENSION_USE_WAYLAND() \
    EXTENSION_AUTO_LIBRARY(Wayland, wl, "libwayland-client.so", "libwayland-client.so.0")
#define WL_LOADED EXTENSION_LIB_LOADED(wl)
#define WL_GET_FN(type, name) EXTENSION_LIB_GET_FN(wl, type, name)

// ============================================================================
// CUSTOM LIBRARY CONFIGURATION
// ============================================================================

// Try multiple libraries in order of preference
#define EXTENSION_TRY_LIBRARIES(name, prefix, ...) \
    EXTENSION_AUTO_LIBRARY(name, prefix, __VA_ARGS__)

// Conditional library - only compile if library is available
#define EXTENSION_IF_LIBRARY(prefix, code) \
    do { \
        if (EXTENSION_LIB_LOADED(prefix)) { \
            code; \
        } \
    } while(0)

// Library with fallback
#define EXTENSION_LIBRARY_WITH_FALLBACK(name, prefix, primary, fallback) \
    static ::havel::DynamicLoader _##prefix##_loader; \
    static bool _##prefix##_initialized = []() { \
        if (_##prefix##_loader.load(primary)) { \
            fprintf(stderr, "[Extension] Using primary " #name " from %s\n", primary); \
            return true; \
        } \
        if (_##prefix##_loader.load(fallback)) { \
            fprintf(stderr, "[Extension] Using fallback " #name " from %s\n", fallback); \
            return true; \
        } \
        return false; \
    }();

// ============================================================================
// STATIC/DYNAMIC CONFIGURATION HELPERS
// ============================================================================

// Prefer static linking if STATIC_EXT is defined, otherwise dynamic
#ifdef STATIC_EXT
    #define EXT_LIBRARY_MODE "static"
    #define EXT_DYNAMIC_INIT() do {} while(0)
#else
    #define EXT_LIBRARY_MODE "dynamic"
    #define EXT_DYNAMIC_INIT() \
        fprintf(stderr, "[Extension] Running in dynamic mode\n")
#endif

// Platform detection
#ifdef _WIN32
    #define EXT_PLATFORM "windows"
    #define EXT_LIB_EXT ".dll"
#elif defined(__APPLE__)
    #define EXT_PLATFORM "macos"
    #define EXT_LIB_EXT ".dylib"
#else
    #define EXT_PLATFORM "linux"
    #define EXT_LIB_EXT ".so"
#endif

// Architecture detection
#if defined(__x86_64__) || defined(_M_X64)
    #define EXT_ARCH "x64"
#elif defined(__i386__) || defined(_M_IX86)
    #define EXT_ARCH "x86"
#elif defined(__aarch64__) || defined(_M_ARM64)
    #define EXT_ARCH "arm64"
#else
    #define EXT_ARCH "unknown"
#endif

// ============================================================================
// HELPER MACROS FOR EXTENSION DEVELOPMENT
// ============================================================================

// Log extension info
#define EXT_LOG_INFO(fmt, ...) \
    fprintf(stderr, "[Extension] " fmt "\n", ##__VA_ARGS__)

// Log extension error
#define EXT_LOG_ERROR(fmt, ...) \
    fprintf(stderr, "[Extension ERROR] " fmt "\n", ##__VA_ARGS__)

// Check if running in Havel VM
#define EXT_CHECK_API(api) \
    do { \
        if (!(api).raw()) { \
            EXT_LOG_ERROR("HavelAPI not available"); \
            return; \
        } \
    } while(0)

// Register function with error checking
#define EXT_REGISTER_SAFE(api, module, name, fn) \
    do { \
        try { \
            (api).registerFunction(module, name, fn); \
            EXT_LOG_INFO("Registered %s.%s", module, name); \
        } catch (...) { \
            EXT_LOG_ERROR("Failed to register %s.%s", module, name); \
        } \
    } while(0)

// Version check
#define EXT_CHECK_VERSION(api, required) \
    ((api).raw() && (api).raw()->version >= (required))

// ============================================================================
// MODULE NAMESPACE HELPER
// ============================================================================

#define EXT_BEGIN_NAMESPACE(name) namespace havel_ext_##name {
#define EXT_END_NAMESPACE() }
#define EXT_USING_NAMESPACE(name) using namespace havel_ext_##name;

// ============================================================================
// VALUE CONVERSION HELPERS (shortcuts)
// ============================================================================

#define EXT_RETURN_INT(api, val) \
    return havel::Value::fromInt((val), (api).raw())

#define EXT_RETURN_BOOL(api, val) \
    return havel::Value::fromBool((val), (api).raw())

#define EXT_RETURN_STRING(api, val) \
    return havel::Value::fromString((val), (api).raw())

#define EXT_RETURN_NULL(api) \
    return havel::Value::null((api).raw())

#define EXT_ARG_COUNT(args) ((args).size())
#define EXT_ARG_INT(args, idx) ((args)[(idx)].asInt())
#define EXT_ARG_BOOL(args, idx) ((args)[(idx)].asBool())
#define EXT_ARG_STRING(args, idx) ((args)[(idx)].asString())
