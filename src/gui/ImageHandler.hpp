#pragma once
#include <cairo/cairo.h>
#include <vector>
#include <memory>

namespace havel {

class ImageHandler {
public:
    // Image format conversion
    static std::vector<uint8_t> SurfaceToPNG(cairo_surface_t* surface);
    static std::vector<uint8_t> SurfaceToJPEG(cairo_surface_t* surface, int quality = 90);
    static std::vector<uint8_t> SurfaceToBMP(cairo_surface_t* surface);
    
    // Surface creation from formats
    static std::unique_ptr<cairo_surface_t, void(*)(cairo_surface_t*)> 
    CreateFromPNG(const std::vector<uint8_t>& data);
    
    static std::unique_ptr<cairo_surface_t, void(*)(cairo_surface_t*)>
    CreateFromJPEG(const std::vector<uint8_t>& data);
    
    static std::unique_ptr<cairo_surface_t, void(*)(cairo_surface_t*)>
    CreateFromBMP(const std::vector<uint8_t>& data);

private:
    // Helper functions
    static cairo_status_t WriteToVector(void* closure, const unsigned char* data, unsigned int length);
    static cairo_status_t ReadFromVector(void* closure, unsigned char* data, unsigned int length);
};

} // namespace havel 