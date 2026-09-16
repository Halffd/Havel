#pragma once

#include "IScreenshotBackend.hpp"
#include <vector>
#include <string>
#include <memory>

namespace havel::host {

class ScreenshotService {
public:
    static ScreenshotService& getInstance();

    void setBackend(std::unique_ptr<IScreenshotBackend> backend);
    IScreenshotBackend* backend() const;
    bool hasBackend() const { return backend_ != nullptr; }

    ScreenshotResult captureFullDesktop(const ScreenshotStyle& style = {});
    ScreenshotResult captureMonitor(int index, const ScreenshotStyle& style = {});
    ScreenshotResult captureActiveWindow(const ScreenshotStyle& style = {});
    ScreenshotResult captureRegion(int x, int y, int width, int height, const ScreenshotStyle& style = {});
    int getMonitorCount() const;
    std::vector<int> getMonitorGeometry(int index) const;

private:
    ScreenshotService() = default;
    std::unique_ptr<IScreenshotBackend> backend_;
};

} // namespace havel::host
