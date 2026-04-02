#define CV__DEBUG_NS_START namespace cv {
#define CV__DEBUG_NS_END }
/*
 * image_extension.cpp - Native image processing extension
 *
 * Uses C ABI (HavelCAPI.h) for stability.
 * Demonstrates proper extension pattern:
 * - No captures in function pointers
 * - Opaque handles for resources
 * - Proper memory management
 */

#include "HavelCAPI.h"

#include <opencv2/opencv.hpp>
#include <unordered_map>
#include <memory>
#include <cstdio>

namespace {

/* Image handle system - opaque handles for VM */
std::unordered_map<int64_t, std::shared_ptr<cv::Mat>> g_images;
int64_t g_nextImageId = 1;

void cleanupImage(void* ptr) {
    int64_t id = reinterpret_cast<int64_t>(ptr);
    g_images.erase(id);
}

int64_t storeImage(const cv::Mat& img) {
    int64_t id = g_nextImageId++;
    g_images[id] = std::make_shared<cv::Mat>(img.clone());
    return id;
}

cv::Mat* getImage(int64_t id) {
    auto it = g_images.find(id);
    if (it == g_images.end()) {
        return nullptr;
    }
    return it->second.get();
}

/* Helper function wrappers - no captures, pure C function pointers */
HavelValue* img_load(int argc, HavelValue** argv) {
    if (argc < 1) return havel_new_null();
    
    const char* path = havel_get_string(argv[0]);
    if (!path) return havel_new_null();
    
    cv::Mat img = cv::imread(path);
    if (img.empty()) return havel_new_int(0);
    
    int64_t id = storeImage(img);
    return havel_new_handle(reinterpret_cast<void*>(id), cleanupImage);
}

HavelValue* img_save(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    const char* path = havel_get_string(argv[1]);
    
    if (!path) return havel_new_bool(0);
    
    cv::Mat* img = getImage(id);
    if (!img) return havel_new_bool(0);
    
    int result = cv::imwrite(path, *img) ? 1 : 0;
    return havel_new_bool(result);
}

HavelValue* img_width(int argc, HavelValue** argv) {
    if (argc < 1) return havel_new_int(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    cv::Mat* img = getImage(id);
    if (!img) return havel_new_int(0);
    
    return havel_new_int(img->cols);
}

HavelValue* img_height(int argc, HavelValue** argv) {
    if (argc < 1) return havel_new_int(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    cv::Mat* img = getImage(id);
    if (!img) return havel_new_int(0);
    
    return havel_new_int(img->rows);
}

HavelValue* img_resize(int argc, HavelValue** argv) {
    if (argc < 3) return havel_new_int(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    int64_t width = havel_get_int(argv[1]);
    int64_t height = havel_get_int(argv[2]);
    
    cv::Mat* img = getImage(id);
    if (!img) return havel_new_int(0);
    
    cv::Mat resized;
    cv::resize(*img, resized, cv::Size(width, height));
    
    int64_t newId = storeImage(resized);
    return havel_new_handle(reinterpret_cast<void*>(newId), cleanupImage);
}

HavelValue* img_rotate(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_int(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    double angle = havel_get_float(argv[1]);
    
    cv::Mat* img = getImage(id);
    if (!img) return havel_new_int(0);
    
    cv::Point2f center(img->cols / 2.0, img->rows / 2.0);
    cv::Mat rot = cv::getRotationMatrix2D(center, angle, 1.0);
    cv::Mat rotated;
    cv::warpAffine(*img, rotated, rot, img->size());
    
    int64_t newId = storeImage(rotated);
    return havel_new_handle(reinterpret_cast<void*>(newId), cleanupImage);
}

HavelValue* img_crop(int argc, HavelValue** argv) {
    if (argc < 5) return havel_new_int(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    int64_t x = havel_get_int(argv[1]);
    int64_t y = havel_get_int(argv[2]);
    int64_t w = havel_get_int(argv[3]);
    int64_t h = havel_get_int(argv[4]);
    
    cv::Mat* img = getImage(id);
    if (!img) return havel_new_int(0);
    
    cv::Rect roi(x, y, w, h);
    cv::Mat cropped = (*img)(roi);
    
    int64_t newId = storeImage(cropped);
    return havel_new_handle(reinterpret_cast<void*>(newId), cleanupImage);
}

HavelValue* img_blur(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_int(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    int64_t ksize = havel_get_int(argv[1]);
    
    cv::Mat* img = getImage(id);
    if (!img) return havel_new_int(0);
    
    cv::Mat blurred;
    cv::blur(*img, blurred, cv::Size(ksize, ksize));
    
    int64_t newId = storeImage(blurred);
    return havel_new_handle(reinterpret_cast<void*>(newId), cleanupImage);
}

HavelValue* img_grayscale(int argc, HavelValue** argv) {
    if (argc < 1) return havel_new_int(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    cv::Mat* img = getImage(id);
    if (!img) return havel_new_int(0);
    
    cv::Mat gray;
    cv::cvtColor(*img, gray, cv::COLOR_BGR2GRAY);
    
    int64_t newId = storeImage(gray);
    return havel_new_handle(reinterpret_cast<void*>(newId), cleanupImage);
}

HavelValue* img_edges(int argc, HavelValue** argv) {
    if (argc < 1) return havel_new_int(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    cv::Mat* img = getImage(id);
    if (!img) return havel_new_int(0);
    
    cv::Mat edges;
    cv::Canny(*img, edges, 100, 200);
    
    int64_t newId = storeImage(edges);
    return havel_new_handle(reinterpret_cast<void*>(newId), cleanupImage);
}

} /* anonymous namespace */

extern "C" void havel_extension_init(HavelAPI* api) {
    /* Register all image functions */
    api->register_function("image", "load", img_load);
    api->register_function("image", "save", img_save);
    api->register_function("image", "width", img_width);
    api->register_function("image", "height", img_height);
    api->register_function("image", "resize", img_resize);
    api->register_function("image", "rotate", img_rotate);
    api->register_function("image", "crop", img_crop);
    api->register_function("image", "blur", img_blur);
    api->register_function("image", "grayscale", img_grayscale);
    api->register_function("image", "edges", img_edges);
}
