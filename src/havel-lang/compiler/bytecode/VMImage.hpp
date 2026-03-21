/*
 * VMImage.hpp
 *
 * VM-managed image representation.
 * 
 * Image data is stored in GC heap via ObjectRef.
 * This struct is a lightweight wrapper with metadata.
 */
#pragma once

#include "GC.hpp"
#include <cstdint>

namespace havel::compiler {

/**
 * Pixel format for VM images
 */
enum class PixelFormat : uint8_t {
    RGBA8,    // 4 bytes per pixel (R, G, B, A)
    RGB8,     // 3 bytes per pixel (R, G, B)
    GRAY8,    // 1 byte per pixel
    BGRA8,    // 4 bytes per pixel (B, G, R, A) - Qt default
};

/**
 * VMImage - GC-managed image representation
 * 
 * Image data is stored in GC heap as a byte array.
 * This struct contains metadata and a reference to the GC object.
 * 
 * Usage:
 *   VMImage img = vm.createImageFromRGBA(width, height, data);
 *   // img is automatically GC-managed via object_ref
 */
struct VMImage {
    int32_t width = 0;
    int32_t height = 0;
    int32_t stride = 0;  // bytes per row
    PixelFormat format = PixelFormat::BGRA8;
    ObjectRef object_ref;  // GC-managed object containing byte array
    
    // Helper: check if valid
    bool isValid() const {
        return width > 0 && height > 0 && object_ref.id != 0;
    }
    
    // Helper: total size in bytes
    size_t sizeInBytes() const {
        return stride > 0 ? static_cast<size_t>(stride) * height : 0;
    }
};

} // namespace havel::compiler
