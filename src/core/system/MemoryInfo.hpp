/*
 * MemoryInfo.hpp
 * 
 * Memory information utilities for Havel language.
 * Provides RAM and swap information.
 */
#pragma once

#include <cstdint>

namespace havel {

struct MemoryInfo {
    // Get total RAM in bytes
    static uint64_t total();
    
    // Get used RAM in bytes
    static uint64_t used();
    
    // Get free RAM in bytes
    static uint64_t free();
    
    // Get total swap in bytes
    static uint64_t swapTotal();
    
    // Get used swap in bytes
    static uint64_t swapUsed();
    
    // Get free swap in bytes
    static uint64_t swapFree();
};

} // namespace havel
