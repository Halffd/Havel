/*
 * ImageService.hpp - Image processing service backed by OpenCV
 *
 * Provides load, save, resize, crop, rotate, blur, grayscale, edges,
 * and pixel-level access. All operations return plain C++ types.
 */
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <unordered_map>

namespace havel::host {

struct ImageInfo {
    int width = 0;
    int height = 0;
    int channels = 0;
    std::string format;
};

class ImageService {
public:
    ImageService();
    ~ImageService();

    int64_t load(const std::string& path);
    bool save(int64_t handle, const std::string& path);
    void release(int64_t handle);
    bool isValid(int64_t handle) const;

    ImageInfo info(int64_t handle) const;

    int width(int64_t handle) const;
    int height(int64_t handle) const;
    int channels(int64_t handle) const;

    int64_t resize(int64_t handle, int width, int height);
    int64_t crop(int64_t handle, int x, int y, int w, int h);
    int64_t rotate(int64_t handle, double angle);
    int64_t blur(int64_t handle, int ksize);
    int64_t grayscale(int64_t handle);
    int64_t edges(int64_t handle, double threshold1, double threshold2);
    int64_t threshold(int64_t handle, double thresh, double maxval, int type);
    int64_t flip(int64_t handle, int code);
    int64_t blend(int64_t handle1, int64_t handle2, double alpha);

    bool getPixel(int64_t handle, int x, int y, int& r, int& g, int& b, int& a);
    bool setPixel(int64_t handle, int x, int y, int r, int g, int b, int a);

    int64_t create(int width, int height, int r, int g, int b, int a);
    int64_t fromRGBA(const std::vector<uint8_t>& data, int width, int height);
    std::vector<uint8_t> toRGBA(int64_t handle);

    int64_t matchTemplate(int64_t screen, int64_t templ, float threshold, int& outX, int& outY, float& outConf);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace havel::host
