/*
 * EasyImport.hpp - One-stop header for common extension library imports
 * 
 * PURPOSE:
 * - Centralized includes for common system libraries
 * - Version detection and compatibility shims
 * - Automatic library feature detection
 * 
 * USAGE:
 *   #include "extensions/EasyImport.hpp"
 *   
 *   // All common headers are now available:
 *   // - Standard library
 *   // - GTK/Qt if available
 *   // - SDL, OpenCV, etc.
 * 
 * This simplifies extension development by providing one include
 * that brings in everything commonly needed.
 */

#pragma once

// ============================================================================
// STANDARD LIBRARY (always available)
// ============================================================================

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <memory>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <sstream>
#include <fstream>
#include <iostream>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>

// ============================================================================
// HAVEL EXTENSION API (core)
// ============================================================================

#include "HavelCAPI.h"
#include "ExtensionAPI.hpp"
#include "DynamicLoader.hpp"
#include "ExtensionMacros.hpp"

// ============================================================================
// SYSTEM LIBRARIES (conditional)
// ============================================================================

// Platform-specific includes
#ifdef _WIN32
    #include <windows.h>
    #include <shlwapi.h>
#elif defined(__APPLE__)
    #include <mach/mach.h>
    #include <mach/mach_time.h>
    #include <dispatch/dispatch.h>
#else // Linux
    #include <unistd.h>
    #include <dlfcn.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <pthread.h>
#endif

// POSIX (most Unix-like systems)
#ifndef _WIN32
    #include <dirent.h>
    #include <errno.h>
    #include <signal.h>
#endif

// ============================================================================
// OPTIONAL LIBRARY DETECTION & IMPORTS
// ============================================================================

// Try to include GTK if available
#if __has_include(<gtk/gtk.h>)
    #include <gtk/gtk.h>
    #define HAVEL_EXT_HAS_GTK 1
#else
    #define HAVEL_EXT_HAS_GTK 0
#endif

// Try to include Qt if available
#if __has_include(<QtCore/QtCore>)
    #include <QtCore/QtCore>
    #define HAVEL_EXT_HAS_QT 1
#else
    #define HAVEL_EXT_HAS_QT 0
#endif

// Try to include SDL if available
#if __has_include(<SDL2/SDL.h>)
    #include <SDL2/SDL.h>
    #define HAVEL_EXT_HAS_SDL 1
#else
    #define HAVEL_EXT_HAS_SDL 0
#endif

// Try to include OpenCV if available
#if __has_include(<opencv2/opencv.hpp>)
    #include <opencv2/opencv.hpp>
    #define HAVEL_EXT_HAS_OPENCV 1
#else
    #define HAVEL_EXT_HAS_OPENCV 0
#endif

// Try to include OpenGL if available
#if __has_include(<GL/gl.h>) || __has_include(<OpenGL/gl.h>)
    #ifdef __APPLE__
        #include <OpenGL/gl.h>
    #else
        #include <GL/gl.h>
    #endif
    #define HAVEL_EXT_HAS_OPENGL 1
#else
    #define HAVEL_EXT_HAS_OPENGL 0
#endif

// Try to include FFmpeg if available
#if __has_include(<libavcodec/avcodec.h>)
    extern "C" {
        #include <libavcodec/avcodec.h>
        #include <libavformat/avformat.h>
        #include <libavutil/avutil.h>
    }
    #define HAVEL_EXT_HAS_FFMPEG 1
#else
    #define HAVEL_EXT_HAS_FFMPEG 0
#endif

// Try to include FontConfig if available
#if __has_include(<fontconfig/fontconfig.h>)
    #include <fontconfig/fontconfig.h>
    #define HAVEL_EXT_HAS_FONTCONFIG 1
#else
    #define HAVEL_EXT_HAS_FONTCONFIG 0
#endif

// Try to include ImageMagick if available
#if __has_include(<Magick++.h>)
    #include <Magick++.h>
    #define HAVEL_EXT_HAS_IMAGEMAGICK 1
#else
    #define HAVEL_EXT_HAS_IMAGEMAGICK 0
#endif

// Try to include libpng/libjpeg
#if __has_include(<png.h>)
    #include <png.h>
    #define HAVEL_EXT_HAS_LIBPNG 1
#else
    #define HAVEL_EXT_HAS_LIBPNG 0
#endif

#if __has_include(<jpeglib.h>)
    #include <jpeglib.h>
    #define HAVEL_EXT_HAS_LIBJPEG 1
#else
    #define HAVEL_EXT_HAS_LIBJPEG 0
#endif

// ============================================================================
// AUDIO LIBRARIES
// ============================================================================

// PulseAudio
#if __has_include(<pulse/simple.h>)
    #include <pulse/simple.h>
    #define HAVEL_EXT_HAS_PULSEAUDIO 1
#else
    #define HAVEL_EXT_HAS_PULSEAUDIO 0
#endif

// ALSA
#if __has_include(<alsa/asoundlib.h>)
    #include <alsa/asoundlib.h>
    #define HAVEL_EXT_HAS_ALSA 1
#else
    #define HAVEL_EXT_HAS_ALSA 0
#endif

// ============================================================================
// NETWORKING LIBRARIES
// ============================================================================

// Try to include libcurl if available
#if __has_include(<curl/curl.h>)
    #include <curl/curl.h>
    #define HAVEL_EXT_HAS_LIBCURL 1
#else
    #define HAVEL_EXT_HAS_LIBCURL 0
#endif

// Try to include OpenSSL if available
#if __has_include(<openssl/ssl.h>)
    #include <openssl/ssl.h>
    #include <openssl/err.h>
    #define HAVEL_EXT_HAS_OPENSSL 1
#else
    #define HAVEL_EXT_HAS_OPENSSL 0
#endif

// ============================================================================
// DATABASE LIBRARIES
// ============================================================================

// SQLite (commonly available)
#if __has_include(<sqlite3.h>)
    #include <sqlite3.h>
    #define HAVEL_EXT_HAS_SQLITE 1
#else
    #define HAVEL_EXT_HAS_SQLITE 0
#endif

// ============================================================================
// FEATURE DETECTION HELPERS
// ============================================================================

namespace havel {
namespace ext {

// Check which libraries are available at compile time
struct CompileFeatures {
    static constexpr bool hasGtk = HAVEL_EXT_HAS_GTK;
    static constexpr bool hasQt = HAVEL_EXT_HAS_QT;
    static constexpr bool hasSdl = HAVEL_EXT_HAS_SDL;
    static constexpr bool hasOpenCV = HAVEL_EXT_HAS_OPENCV;
    static constexpr bool hasOpenGL = HAVEL_EXT_HAS_OPENGL;
    static constexpr bool hasFFmpeg = HAVEL_EXT_HAS_FFMPEG;
    static constexpr bool hasFontConfig = HAVEL_EXT_HAS_FONTCONFIG;
    static constexpr bool hasImageMagick = HAVEL_EXT_HAS_IMAGEMAGICK;
    static constexpr bool hasLibPNG = HAVEL_EXT_HAS_LIBPNG;
    static constexpr bool hasLibJPEG = HAVEL_EXT_HAS_LIBJPEG;
    static constexpr bool hasPulseAudio = HAVEL_EXT_HAS_PULSEAUDIO;
    static constexpr bool hasALSA = HAVEL_EXT_HAS_ALSA;
    static constexpr bool hasLibCurl = HAVEL_EXT_HAS_LIBCURL;
    static constexpr bool hasOpenSSL = HAVEL_EXT_HAS_OPENSSL;
    static constexpr bool hasSQLite = HAVEL_EXT_HAS_SQLITE;
};

// Helper to get feature summary
inline std::string getFeatureSummary() {
    std::string result = "Available features:\n";
    if (CompileFeatures::hasGtk) result += "  - GTK\n";
    if (CompileFeatures::hasQt) result += "  - Qt\n";
    if (CompileFeatures::hasSdl) result += "  - SDL\n";
    if (CompileFeatures::hasOpenCV) result += "  - OpenCV\n";
    if (CompileFeatures::hasOpenGL) result += "  - OpenGL\n";
    if (CompileFeatures::hasFFmpeg) result += "  - FFmpeg\n";
    if (CompileFeatures::hasFontConfig) result += "  - FontConfig\n";
    if (CompileFeatures::hasImageMagick) result += "  - ImageMagick\n";
    if (CompileFeatures::hasLibPNG) result += "  - libpng\n";
    if (CompileFeatures::hasLibJPEG) result += "  - libjpeg\n";
    if (CompileFeatures::hasPulseAudio) result += "  - PulseAudio\n";
    if (CompileFeatures::hasALSA) result += "  - ALSA\n";
    if (CompileFeatures::hasLibCurl) result += "  - libcurl\n";
    if (CompileFeatures::hasOpenSSL) result += "  - OpenSSL\n";
    if (CompileFeatures::hasSQLite) result += "  - SQLite\n";
    return result;
}

} // namespace ext
} // namespace havel

// ============================================================================
// CONVENIENCE MACROS
// ============================================================================

// Check feature at compile time
#define EXT_HAS_FEATURE(name) HAVEL_EXT_HAS_##name

// Conditional compilation based on feature availability
#define EXT_IF_FEATURE(name, code) \
    do { \
        if (HAVEL_EXT_HAS_##name) { \
            code; \
        } \
    } while(0)

// Assert feature is available
#define EXT_REQUIRE_FEATURE(name, msg) \
    static_assert(HAVEL_EXT_HAS_##name, msg)

// ============================================================================
// VERSION HELPERS
// ============================================================================

#define EXT_MAKE_VERSION(major, minor, patch) ((major << 16) | (minor << 8) | patch)
#define EXT_VERSION_MAJOR(v) ((v >> 16) & 0xFF)
#define EXT_VERSION_MINOR(v) ((v >> 8) & 0xFF)
#define EXT_VERSION_PATCH(v) (v & 0xFF)
