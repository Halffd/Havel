#include "ZoomService.hpp"
#include "utils/Logger.hpp"
#include <cstdio>
#include <cmath>

#ifdef HAVE_X11
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

namespace havel::modules::zoom {

ZoomService::~ZoomService() {
    stop();
}

bool ZoomService::start() {
    if (running_.load()) return true;
    running_.store(true);
    havel::Logger::getInstance().debug("ZoomService: started");
    return true;
}

void ZoomService::stop() {
    if (!running_.load()) return;
    running_.store(false);
    if (renderThread_.joinable()) {
        renderThread_.join();
    }
    havel::Logger::getInstance().debug("ZoomService: stopped");
}

void ZoomService::setRegion(int x, int y, int w, int h) {
    std::lock_guard<std::mutex> lock(regionMutex_);
    region_.x = x;
    region_.y = y;
    region_.width = w;
    region_.height = h;
}

ZoomRegion ZoomService::getRegion() const {
    std::lock_guard<std::mutex> lock(regionMutex_);
    return region_;
}

void ZoomService::capture() {
    havel::Logger::getInstance().debug("ZoomService: capture full screen");
}

void ZoomService::captureRegion(int x, int y, int w, int h) {
    havel::Logger::getInstance().debug("ZoomService: capture region");
    (void)x; (void)y; (void)w; (void)h;
}

std::vector<int> ZoomService::getPixelColor(int x, int y) {
#ifdef HAVE_X11
    Display* disp = XOpenDisplay(nullptr);
    if (disp) {
        XColor color;
        XImage* img = XGetImage(disp, RootWindow(disp, DefaultScreen(disp)),
                                 x, y, 1, 1, AllPlanes, ZPixmap);
        if (img) {
            unsigned long pixel = XGetPixel(img, 0, 0);
            color.pixel = pixel;
            XQueryColor(disp, DefaultColormap(disp, DefaultScreen(disp)), &color);
            XDestroyImage(img);
            XCloseDisplay(disp);
            return {static_cast<int>(color.red >> 8),
                    static_cast<int>(color.green >> 8),
                    static_cast<int>(color.blue >> 8)};
        }
        XCloseDisplay(disp);
    }
#endif
    (void)x; (void)y;
    return {0, 0, 0};
}

std::string ZoomService::getPixelColorHex(int x, int y) {
    auto rgb = getPixelColor(x, y);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", rgb[0], rgb[1], rgb[2]);
    return std::string(buf);
}

} // namespace havel::modules::zoom
