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

    std::vector<unsigned char> captureFullDesktop();
    std::vector<unsigned char> captureMonitor(int index);
    std::vector<unsigned char> captureActiveWindow();
    std::vector<unsigned char> captureRegion(int x, int y, int width, int height);
    int getMonitorCount() const;
    std::vector<int> getMonitorGeometry(int index) const;

private:
    ScreenshotService() = default;
    std::unique_ptr<IScreenshotBackend> backend_;
};

} // namespace havel::host
