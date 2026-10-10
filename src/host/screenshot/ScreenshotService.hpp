#pragma once

#include "IScreenshotBackend.hpp"
#include "havel-lang/common/Export.hpp"
#include <vector>
#include <string>
#include <memory>

namespace havel::host {

class HAVEL_EXPORT ScreenshotService {
public:
    static ScreenshotService& getInstance();

    void setBackend(std::unique_ptr<IScreenshotBackend> backend);
    IScreenshotBackend* backend() const;
    bool hasBackend() const { return backend_ != nullptr; }

    ScreenshotResult captureFullDesktop(const ScreenshotStyle& style = {});
    ScreenshotResult captureMonitor(int index, const ScreenshotStyle& style = {});
    ScreenshotResult captureActiveWindow(const ScreenshotStyle& style = {});
    ScreenshotResult captureRegion(int x, int y, int width, int height, const ScreenshotStyle& style = {});
    // Save the capture's RGBA buffer as PNG. Returns false on backend
    // error or when OpenCV isn't present in the build.
    bool saveToFile(const ScreenshotResult& result, const std::string& path);
    int getMonitorCount() const;
    std::vector<int> getMonitorGeometry(int index) const;

private:
    ScreenshotService() = default;
    std::unique_ptr<IScreenshotBackend> backend_;
};

} // namespace havel::host
