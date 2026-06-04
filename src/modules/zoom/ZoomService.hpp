#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include <thread>
#include <mutex>

namespace havel::modules::zoom {

enum class ZoomFilter : int { Nearest = 0, Bilinear = 1, Sharpen = 2, Lanczos = 3 };

struct ZoomRegion {
    int x = 0;
    int y = 0;
    int width = 1920;
    int height = 1080;
};

class ZoomService {
public:
    static ZoomService& instance() {
        static ZoomService inst;
        return inst;
    }

    bool start();
    void stop();
    bool isRunning() const { return running_.load(); }

    void setScale(float s) { scale_.store(s); }
    float getScale() const { return scale_.load(); }

    void setRegion(int x, int y, int w, int h);
    ZoomRegion getRegion() const;

    void setFilter(ZoomFilter f) { filter_.store(f); }
    ZoomFilter getFilter() const { return filter_.load(); }

    void setLocked(bool l) { locked_.store(l); }
    bool isLocked() const { return locked_.load(); }

    void setFollowCursor(bool f) { followCursor_.store(f); }
    bool isFollowCursor() const { return followCursor_.load(); }

    void setColorInvert(bool i) { colorInvert_.store(i); }
    bool isColorInvert() const { return colorInvert_.load(); }

    void setBrightness(float b) { brightness_.store(b); }
    float getBrightness() const { return brightness_.load(); }

    void setContrast(float c) { contrast_.store(c); }
    float getContrast() const { return contrast_.load(); }

    void capture();
    void captureRegion(int x, int y, int w, int h);
    std::vector<int> getPixelColor(int x, int y);
    std::string getPixelColorHex(int x, int y);

    int getScreenWidth() const { return region_.width; }
    int getScreenHeight() const { return region_.height; }

private:
    ZoomService() = default;
    ~ZoomService();

    std::atomic<bool> running_{false};
    std::atomic<float> scale_{2.0f};
    std::atomic<ZoomFilter> filter_{ZoomFilter::Bilinear};
    std::atomic<bool> locked_{false};
    std::atomic<bool> followCursor_{true};
    std::atomic<bool> colorInvert_{false};
    std::atomic<float> brightness_{1.0f};
    std::atomic<float> contrast_{1.0f};

    mutable std::mutex regionMutex_;
    ZoomRegion region_;
    std::thread renderThread_;
};

} // namespace havel::modules::zoom
