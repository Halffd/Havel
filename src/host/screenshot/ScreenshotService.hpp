#pragma once
#include <vector>
#include <string>
#include <QImage>
#include <QScreen>

namespace havel::host {

class ScreenshotService {
public:
    static ScreenshotService& getInstance() {
        static ScreenshotService instance;
        return instance;
    }

    std::vector<unsigned char> captureFullDesktop();
    std::vector<unsigned char> captureMonitor(int index);
    std::vector<unsigned char> captureActiveWindow();
    std::vector<unsigned char> captureRegion(int x, int y, int width, int height);
    int getMonitorCount() const;
    std::vector<int> getMonitorGeometry(int index) const;
    
    // Internal helpers
    std::vector<unsigned char> captureFromScreen(QScreen* screen);
    std::vector<unsigned char> imageToRGBA(const QImage& image);

private:
    ScreenshotService() = default;
};

} // namespace havel::host
