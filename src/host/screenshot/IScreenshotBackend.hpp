#pragma once

#include <vector>
#include <string>
#include <cstdint>

namespace havel::host {

class IScreenshotBackend {
public:
    virtual ~IScreenshotBackend() = default;

    virtual std::vector<unsigned char> captureFullDesktop() = 0;
    virtual std::vector<unsigned char> captureMonitor(int index) = 0;
    virtual std::vector<unsigned char> captureActiveWindow() = 0;
    virtual std::vector<unsigned char> captureRegion(int x, int y, int width, int height) = 0;
    virtual int getMonitorCount() const = 0;
    virtual std::vector<int> getMonitorGeometry(int index) const = 0;
};

} // namespace havel::host
