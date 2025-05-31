#include "ImageHandler.hpp"
#include <jpeglib.h>
#include <algorithm>
#include <cstring>

namespace havel {

std::vector<uint8_t> ImageHandler::SurfaceToPNG(cairo_surface_t* surface) {
    std::vector<uint8_t> result;
    
    cairo_surface_write_to_png_stream(surface, WriteToVector, &result);
    
    return result;
}

std::vector<uint8_t> ImageHandler::SurfaceToJPEG(cairo_surface_t* surface, int quality) {
    std::vector<uint8_t> result;
    
    // Get surface data
    int width = cairo_image_surface_get_width(surface);
    int height = cairo_image_surface_get_height(surface);
    int stride = cairo_image_surface_get_stride(surface);
    unsigned char* data = cairo_image_surface_get_data(surface);
    
    // Set up JPEG compression
    jpeg_compress_struct cinfo;
    jpeg_error_mgr jerr;
    
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    
    // Set up output buffer
    unsigned char* outbuffer = nullptr;
    unsigned long outsize = 0;
    jpeg_mem_dest(&cinfo, &outbuffer, &outsize);
    
    // Set image properties
    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 4;
    cinfo.in_color_space = JCS_RGB;
    
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    
    // Start compression
    jpeg_start_compress(&cinfo, TRUE);
    
    // Write scanlines
    std::vector<unsigned char> row(width * 3);
    while (cinfo.next_scanline < cinfo.image_height) {
        unsigned char* in_row = data + cinfo.next_scanline * stride;
        unsigned char* out_row = row.data();
        
        // Convert ARGB to RGB
        for (int x = 0; x < width; x++) {
            out_row[x*3] = in_row[x*4+2];     // Red
            out_row[x*3+1] = in_row[x*4+1];   // Green
            out_row[x*3+2] = in_row[x*4];     // Blue
        }
        
        JSAMPROW row_pointer = out_row;
        jpeg_write_scanlines(&cinfo, &row_pointer, 1);
    }
    
    // Finish compression
    jpeg_finish_compress(&cinfo);
    
    // Copy to result vector
    result.assign(outbuffer, outbuffer + outsize);
    
    // Clean up
    free(outbuffer);
    jpeg_destroy_compress(&cinfo);
    
    return result;
}

std::vector<uint8_t> ImageHandler::SurfaceToBMP(cairo_surface_t* surface) {
    std::vector<uint8_t> result;
    
    // Get surface data
    int width = cairo_image_surface_get_width(surface);
    int height = cairo_image_surface_get_height(surface);
    int stride = cairo_image_surface_get_stride(surface);
    unsigned char* data = cairo_image_surface_get_data(surface);
    
    // BMP header size
    const int headerSize = 54;
    const int rowSize = ((width * 3 + 3) / 4) * 4;
    const int imageSize = rowSize * height;
    const int fileSize = headerSize + imageSize;
    
    // Allocate result vector
    result.resize(fileSize);
    
    // Write BMP header
    unsigned char* header = result.data();
    std::memset(header, 0, headerSize);
    
    // File header
    header[0] = 'B';
    header[1] = 'M';
    *reinterpret_cast<uint32_t*>(header + 2) = fileSize;
    *reinterpret_cast<uint32_t*>(header + 10) = headerSize;
    
    // Info header
    *reinterpret_cast<uint32_t*>(header + 14) = 40;
    *reinterpret_cast<uint32_t*>(header + 18) = width;
    *reinterpret_cast<uint32_t*>(header + 22) = height;
    *reinterpret_cast<uint16_t*>(header + 26) = 1;
    *reinterpret_cast<uint16_t*>(header + 28) = 24;
    *reinterpret_cast<uint32_t*>(header + 34) = imageSize;
    
    // Write pixel data
    unsigned char* out = result.data() + headerSize;
    for (int y = height - 1; y >= 0; y--) {
        unsigned char* in_row = data + y * stride;
        unsigned char* out_row = out + (height - 1 - y) * rowSize;
        
        // Convert ARGB to BGR
        for (int x = 0; x < width; x++) {
            out_row[x*3] = in_row[x*4];     // Blue
            out_row[x*3+1] = in_row[x*4+1]; // Green
            out_row[x*3+2] = in_row[x*4+2]; // Red
        }
    }
    
    return result;
}

cairo_status_t ImageHandler::WriteToVector(void* closure, const unsigned char* data, unsigned int length) {
    auto* vec = static_cast<std::vector<uint8_t>*>(closure);
    vec->insert(vec->end(), data, data + length);
    return CAIRO_STATUS_SUCCESS;
}

cairo_status_t ImageHandler::ReadFromVector(void* closure, unsigned char* data, unsigned int length) {
    auto* src = static_cast<const std::vector<uint8_t>*>(closure);
    static size_t offset = 0;
    
    if (offset >= src->size()) return CAIRO_STATUS_READ_ERROR;
    
    size_t remaining = std::min(static_cast<size_t>(length), src->size() - offset);
    std::copy_n(src->begin() + offset, remaining, data);
    offset += remaining;
    
    return CAIRO_STATUS_SUCCESS;
}

} // namespace havel 