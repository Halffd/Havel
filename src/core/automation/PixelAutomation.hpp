#pragma once

#include <string>
#include <memory>
#include <vector>
#include <chrono>

#ifdef HAVE_QT_EXTENSION
#include <qt.hpp>
#include <QRect>
#endif

namespace havel {

struct Color {
    int r = 0, g = 0, b = 0, a = 255;

    Color() = default;
    Color(int r, int g, int b, int a = 255) : r(r), g(g), b(b), a(a) {}

    static Color fromHex(const std::string& hex);
    std::string toHex() const;
    bool near(const Color& other, int tolerance = 0) const;
    int distance(const Color& other) const;
};

struct ScreenRegion {
    int x = 0, y = 0, w = 0, h = 0;

    ScreenRegion() = default;
    ScreenRegion(int x, int y, int w, int h) : x(x), y(y), w(w), h(h) {}

#ifdef HAVE_QT_EXTENSION
    QRect toQRect() const;
#endif

    static ScreenRegion fullScreen();
};

struct ImageMatch {
    bool found = false;
    int x = 0, y = 0, w = 0, h = 0;
    float confidence = 0.0f;

    ImageMatch() = default;
    ImageMatch(bool found, int x, int y, int w, int h, float conf = 1.0f)
    : found(found), x(x), y(y), w(w), h(h), confidence(conf) {}

    int centerX() const { return x + w / 2; }
    int centerY() const { return y + h / 2; }

    void click(const std::string& button = "left") const;
    void moveTo() const;
};

struct PixelMatch {
    bool matched = false;
    Color color;
    int x = 0, y = 0;

    PixelMatch() = default;
    PixelMatch(bool m, const Color& c, int x, int y)
        : matched(m), color(c), x(x), y(y) {}
};

class PixelAutomation {
public:
    PixelAutomation();
    ~PixelAutomation();

    Color getPixel(int x, int y);
    bool pixelMatch(int x, int y, const Color& expectedColor, int tolerance = 0);
    bool pixelMatch(int x, int y, const std::string& hexColor, int tolerance = 0);
    bool waitPixel(int x, int y, const Color& expectedColor, int tolerance = 0, int timeout = 5000);
    bool waitPixel(int x, int y, const std::string& hexColor, int tolerance = 0, int timeout = 5000);

    ImageMatch findImage(const std::string& imagePath,
                         const ScreenRegion& region = ScreenRegion(),
                         float threshold = 0.9f);
    ImageMatch waitImage(const std::string& imagePath,
                         const ScreenRegion& region = ScreenRegion(),
                         int timeout = 5000,
                         float threshold = 0.9f);
    bool existsImage(const std::string& imagePath,
                     const ScreenRegion& region = ScreenRegion(),
                     float threshold = 0.9f);
    int countImage(const std::string& imagePath,
                   const ScreenRegion& region = ScreenRegion(),
                   float threshold = 0.9f);
    std::vector<ImageMatch> findAllImage(const std::string& imagePath,
                                         const ScreenRegion& region = ScreenRegion(),
                                         float threshold = 0.9f);

    std::string readText(const ScreenRegion& region = ScreenRegion());
    std::string readText(const ScreenRegion& region,
                         const std::string& language,
                         const std::string& whitelist = "");

    void setCacheEnabled(bool enabled, int expiryMs = 100);
    void captureScreen();
    void clearCache();

    static ScreenRegion region(int x, int y, int w, int h);
    static ScreenRegion fullScreen();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace havel
