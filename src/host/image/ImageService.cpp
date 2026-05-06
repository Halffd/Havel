/*
 * ImageService.cpp - Image processing service backed by OpenCV
 */
#include "ImageService.hpp"

#ifdef HAVE_OPENCV

#ifdef COUNT
#undef COUNT
#endif
#ifdef EPS
#undef EPS
#endif

#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <mutex>

namespace havel::host {

struct ImageEntry {
    cv::Mat mat;
};

struct ImageService::Impl {
    std::unordered_map<int64_t, std::shared_ptr<ImageEntry>> images;
    std::atomic<int64_t> nextId{1};
    std::mutex mtx;

    int64_t store(const cv::Mat& img) {
        auto entry = std::make_shared<ImageEntry>();
        entry->mat = img.clone();
        int64_t id = nextId.fetch_add(1);
        std::lock_guard<std::mutex> lock(mtx);
        images[id] = entry;
        return id;
    }

    cv::Mat* get(int64_t id) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = images.find(id);
        if (it == images.end()) return nullptr;
        return &(it->second->mat);
    }

    const cv::Mat* getConst(int64_t id) const {
        auto it = images.find(id);
        if (it == images.end()) return nullptr;
        return &(it->second->mat);
    }

    void erase(int64_t id) {
        std::lock_guard<std::mutex> lock(mtx);
        images.erase(id);
    }

    bool has(int64_t id) const {
        return images.find(id) != images.end();
    }
};

ImageService::ImageService() : impl_(std::make_unique<Impl>()) {}
ImageService::~ImageService() = default;

int64_t ImageService::load(const std::string& path) {
    cv::Mat img = cv::imread(path, cv::IMREAD_UNCHANGED);
    if (img.empty()) return 0;
    if (img.channels() == 3) {
        cv::cvtColor(img, img, cv::COLOR_BGR2RGBA);
    } else if (img.channels() == 1) {
        cv::cvtColor(img, img, cv::COLOR_GRAY2RGBA);
    } else if (img.channels() == 4) {
        cv::cvtColor(img, img, cv::COLOR_BGRA2RGBA);
    }
    return impl_->store(img);
}

bool ImageService::save(int64_t handle, const std::string& path) {
    cv::Mat* img = impl_->get(handle);
    if (!img) return false;

    cv::Mat out;
    if (img->channels() == 4) {
        cv::cvtColor(*img, out, cv::COLOR_RGBA2BGRA);
    } else if (img->channels() == 3) {
        cv::cvtColor(*img, out, cv::COLOR_RGB2BGR);
    } else {
        out = *img;
    }
    return cv::imwrite(path, out);
}

void ImageService::release(int64_t handle) {
    impl_->erase(handle);
}

bool ImageService::isValid(int64_t handle) const {
    return impl_->has(handle);
}

ImageInfo ImageService::info(int64_t handle) const {
    ImageInfo info;
    auto* img = impl_->getConst(handle);
    if (!img) return info;
    info.width = img->cols;
    info.height = img->rows;
    info.channels = img->channels();
    return info;
}

int ImageService::width(int64_t handle) const {
    auto* img = impl_->getConst(handle);
    return img ? img->cols : 0;
}

int ImageService::height(int64_t handle) const {
    auto* img = impl_->getConst(handle);
    return img ? img->rows : 0;
}

int ImageService::channels(int64_t handle) const {
    auto* img = impl_->getConst(handle);
    return img ? img->channels() : 0;
}

int64_t ImageService::resize(int64_t handle, int w, int h) {
    auto* img = impl_->get(handle);
    if (!img || w <= 0 || h <= 0) return 0;
    cv::Mat result;
    cv::resize(*img, result, cv::Size(w, h));
    return impl_->store(result);
}

int64_t ImageService::crop(int64_t handle, int x, int y, int w, int h) {
    auto* img = impl_->get(handle);
    if (!img) return 0;
    if (x < 0 || y < 0 || x + w > img->cols || y + h > img->rows) return 0;
    cv::Rect roi(x, y, w, h);
    cv::Mat result = (*img)(roi).clone();
    return impl_->store(result);
}

int64_t ImageService::rotate(int64_t handle, double angle) {
    auto* img = impl_->get(handle);
    if (!img) return 0;
    cv::Point2f center(img->cols / 2.0f, img->rows / 2.0f);
    cv::Mat rot = cv::getRotationMatrix2D(center, angle, 1.0);
    cv::Mat result;
    cv::warpAffine(*img, result, rot, img->size());
    return impl_->store(result);
}

int64_t ImageService::blur(int64_t handle, int ksize) {
    auto* img = impl_->get(handle);
    if (!img || ksize <= 0) return 0;
    if (ksize % 2 == 0) ksize++;
    cv::Mat result;
    cv::GaussianBlur(*img, result, cv::Size(ksize, ksize), 0);
    return impl_->store(result);
}

int64_t ImageService::grayscale(int64_t handle) {
    auto* img = impl_->get(handle);
    if (!img) return 0;
    cv::Mat result;
    if (img->channels() == 4) {
        cv::cvtColor(*img, result, cv::COLOR_RGBA2GRAY);
    } else if (img->channels() == 3) {
        cv::cvtColor(*img, result, cv::COLOR_RGB2GRAY);
    } else {
        result = img->clone();
    }
    return impl_->store(result);
}

int64_t ImageService::edges(int64_t handle, double threshold1, double threshold2) {
    auto* img = impl_->get(handle);
    if (!img) return 0;
    cv::Mat gray;
    if (img->channels() > 1) {
        cv::cvtColor(*img, gray, cv::COLOR_RGBA2GRAY);
    } else {
        gray = *img;
    }
    cv::Mat result;
    cv::Canny(gray, result, threshold1, threshold2);
    return impl_->store(result);
}

int64_t ImageService::threshold(int64_t handle, double thresh, double maxval, int type) {
    auto* img = impl_->get(handle);
    if (!img) return 0;
    cv::Mat gray;
    if (img->channels() > 1) {
        cv::cvtColor(*img, gray, cv::COLOR_RGBA2GRAY);
    } else {
        gray = *img;
    }
    cv::Mat result;
    cv::threshold(gray, result, thresh, maxval, type);
    return impl_->store(result);
}

int64_t ImageService::flip(int64_t handle, int code) {
    auto* img = impl_->get(handle);
    if (!img) return 0;
    cv::Mat result;
    cv::flip(*img, result, code);
    return impl_->store(result);
}

int64_t ImageService::blend(int64_t handle1, int64_t handle2, double alpha) {
    auto* img1 = impl_->get(handle1);
    auto* img2 = impl_->get(handle2);
    if (!img1 || !img2) return 0;
    if (img1->size() != img2->size() || img1->channels() != img2->channels()) return 0;
    cv::Mat result;
    cv::addWeighted(*img1, alpha, *img2, 1.0 - alpha, 0.0, result);
    return impl_->store(result);
}

bool ImageService::getPixel(int64_t handle, int x, int y, int& r, int& g, int& b, int& a) {
    auto* img = impl_->get(handle);
    if (!img || x < 0 || y < 0 || x >= img->cols || y >= img->rows) return false;

    if (img->channels() == 4) {
        cv::Vec4b px = img->at<cv::Vec4b>(y, x);
        r = px[0]; g = px[1]; b = px[2]; a = px[3];
    } else if (img->channels() == 3) {
        cv::Vec3b px = img->at<cv::Vec3b>(y, x);
        r = px[0]; g = px[1]; b = px[2]; a = 255;
    } else if (img->channels() == 1) {
        uchar v = img->at<uchar>(y, x);
        r = g = b = v; a = 255;
    } else {
        return false;
    }
    return true;
}

bool ImageService::setPixel(int64_t handle, int x, int y, int r, int g, int b, int a) {
    auto* img = impl_->get(handle);
    if (!img || x < 0 || y < 0 || x >= img->cols || y >= img->rows) return false;

    if (img->channels() == 4) {
        img->at<cv::Vec4b>(y, x) = cv::Vec4b(r, g, b, a);
    } else if (img->channels() == 3) {
        img->at<cv::Vec3b>(y, x) = cv::Vec3b(r, g, b);
    } else if (img->channels() == 1) {
        img->at<uchar>(y, x) = static_cast<uchar>(r);
    } else {
        return false;
    }
    return true;
}

int64_t ImageService::create(int width, int height, int r, int g, int b, int a) {
    cv::Mat img(height, width, CV_8UC4, cv::Scalar(r, g, b, a));
    return impl_->store(img);
}

int64_t ImageService::fromRGBA(const std::vector<uint8_t>& data, int width, int height) {
    if (data.size() < static_cast<size_t>(width * height * 4)) return 0;
    cv::Mat img(height, width, CV_8UC4);
    std::memcpy(img.data, data.data(), width * height * 4);
    return impl_->store(img);
}

std::vector<uint8_t> ImageService::toRGBA(int64_t handle) {
    auto* img = impl_->get(handle);
    if (!img) return {};

    cv::Mat rgba;
    if (img->channels() == 4) {
        rgba = *img;
    } else if (img->channels() == 3) {
        cv::cvtColor(*img, rgba, cv::COLOR_RGB2RGBA);
    } else if (img->channels() == 1) {
        cv::cvtColor(*img, rgba, cv::COLOR_GRAY2RGBA);
    } else {
        return {};
    }

    std::vector<uint8_t> result;
    result.resize(rgba.total() * 4);
    std::memcpy(result.data(), rgba.data, result.size());
    return result;
}

int64_t ImageService::matchTemplate(int64_t screenHandle, int64_t templHandle, float threshold, int& outX, int& outY, float& outConf) {
    auto* screen = impl_->get(screenHandle);
    auto* tmpl = impl_->get(templHandle);
    if (!screen || !tmpl) return -1;

    cv::Mat result;
    cv::matchTemplate(*screen, *tmpl, result, cv::TM_CCOEFF_NORMED);

    double minVal, maxVal;
    cv::Point minLoc, maxLoc;
    cv::minMaxLoc(result, &minVal, &maxVal, &minLoc, &maxLoc);

    if (maxVal >= threshold) {
        outX = maxLoc.x;
        outY = maxLoc.y;
        outConf = static_cast<float>(maxVal);
        return 1;
    }
    outX = 0;
    outY = 0;
    outConf = 0.0f;
    return 0;
}

void ImageService::releaseAll() {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->images.clear();
}

} // namespace havel::host

#else // !HAVE_OPENCV

#include <stdexcept>

namespace havel::host {

struct ImageService::Impl {};

ImageService::ImageService() : impl_(std::make_unique<Impl>()) {}
ImageService::~ImageService() = default;

int64_t ImageService::load(const std::string&) { return 0; }
bool ImageService::save(int64_t, const std::string&) { return false; }
void ImageService::release(int64_t) {}
bool ImageService::isValid(int64_t) const { return false; }
ImageInfo ImageService::info(int64_t) const { return {}; }
int ImageService::width(int64_t) const { return 0; }
int ImageService::height(int64_t) const { return 0; }
int ImageService::channels(int64_t) const { return 0; }
int64_t ImageService::resize(int64_t, int, int) { return 0; }
int64_t ImageService::crop(int64_t, int, int, int, int) { return 0; }
int64_t ImageService::rotate(int64_t, double) { return 0; }
int64_t ImageService::blur(int64_t, int) { return 0; }
int64_t ImageService::grayscale(int64_t) { return 0; }
int64_t ImageService::edges(int64_t, double, double) { return 0; }
int64_t ImageService::threshold(int64_t, double, double, int) { return 0; }
int64_t ImageService::flip(int64_t, int) { return 0; }
int64_t ImageService::blend(int64_t, int64_t, double) { return 0; }
bool ImageService::getPixel(int64_t, int, int, int&, int&, int&, int&) { return false; }
bool ImageService::setPixel(int64_t, int, int, int, int, int, int) { return false; }
int64_t ImageService::create(int, int, int, int, int, int) { return 0; }
int64_t ImageService::fromRGBA(const std::vector<uint8_t>&, int, int) { return 0; }
std::vector<uint8_t> ImageService::toRGBA(int64_t) { return {}; }
int64_t ImageService::matchTemplate(int64_t, int64_t, float, int&, int&, float&) { return -1; }
void ImageService::releaseAll() {}

} // namespace havel::host

#endif // HAVE_OPENCV
