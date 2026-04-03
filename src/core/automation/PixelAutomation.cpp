#include "PixelAutomation.hpp"
#include "ai/ocr/OCR.hpp"
#include "core/IO.hpp"
#ifdef HAVE_QT_EXTENSION
#include "extensions/gui/screenshot_manager/ScreenshotManager.hpp"
#endif
#include "window/WindowManager.hpp"
#ifdef HAVE_QT_EXTENSION
#include <QCursor>
#include <QGuiApplication>
#include <QScreen>
#endif
#include <chrono>
#include <thread>

#ifdef LINUX_USED
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>
#endif

namespace havel {

// ============================================================================
// Color Implementation
// ============================================================================

Color Color::fromHex(const std::string &hex) {
  std::string h = hex;
  // Remove # prefix if present
  if (!h.empty() && h[0] == '#') {
    h = h.substr(1);
  }

  Color result;

  if (h.length() >= 6) {
    result.r = std::stoi(h.substr(0, 2), nullptr, 16);
    result.g = std::stoi(h.substr(2, 2), nullptr, 16);
    result.b = std::stoi(h.substr(4, 2), nullptr, 16);

    if (h.length() >= 8) {
      result.a = std::stoi(h.substr(6, 2), nullptr, 16);
    }
  }

  return result;
}

std::string Color::toHex() const {
  char buf[32];
  if (a == 255) {
    snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
  } else {
    snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", r, g, b, a);
  }
  return std::string(buf);
}

bool Color::near(const Color &other, int tolerance) const {
  return std::abs(r - other.r) <= tolerance &&
         std::abs(g - other.g) <= tolerance &&
         std::abs(b - other.b) <= tolerance;
}

int Color::distance(const Color &other) const {
  return std::abs(r - other.r) + std::abs(g - other.g) + std::abs(b - other.b);
}

// ============================================================================
// ScreenRegion Implementation
// ============================================================================

#ifdef HAVE_QT_EXTENSION
QRect ScreenRegion::toQRect() const { return QRect(x, y, w, h); }
#endif

cv::Rect ScreenRegion::toCvRect() const { return cv::Rect(x, y, w, h); }

ScreenRegion ScreenRegion::fullScreen() {
#ifdef HAVE_QT_EXTENSION
  QScreen *primaryScreen = QGuiApplication::primaryScreen();
  if (primaryScreen) {
    QRect geometry = primaryScreen->geometry();
    return ScreenRegion(geometry.x(), geometry.y(), geometry.width(),
                        geometry.height());
  }
#endif
  return ScreenRegion(0, 0, 1920, 1080);
}

// ============================================================================
// ImageMatch Implementation
// ============================================================================

void ImageMatch::click(const std::string &button) const {
  if (found) {
    // Move to center and click
    // Note: These would need an IO instance to work
    // For now, just print what would happen
    // IO::MouseMoveTo(centerX(), centerY());
    // std::this_thread::sleep_for(std::chrono::milliseconds(50));
    // IO::MouseClick(1);  // Left button
  }
}

void ImageMatch::moveTo() const {
  if (found) {
    // Note: This would need an IO instance to work
    // IO::MouseMoveTo(centerX(), centerY());
  }
}

// ============================================================================
// ScreenshotCache Implementation
// ============================================================================

ScreenshotCache::ScreenshotCache() {}

ScreenshotCache::~ScreenshotCache() {}

void ScreenshotCache::capture() {
#ifdef HAVE_QT_EXTENSION
  QScreen *primaryScreen = QGuiApplication::primaryScreen();
  if (primaryScreen) {
    QPixmap pixmap = primaryScreen->grabWindow(0);
    QImage image = pixmap.toImage();

    // Convert to OpenCV format (BGR)
    cachedScreenshot =
        cv::Mat(image.height(), image.width(), CV_8UC4,
                const_cast<uchar *>(image.bits()), image.bytesPerLine());
    cv::cvtColor(cachedScreenshot, cachedScreenshot, cv::COLOR_BGRA2BGR);
  }
#endif
  captureTime = std::chrono::steady_clock::now();
}

void ScreenshotCache::captureRegion(const ScreenRegion &region) {
#ifdef HAVE_QT_EXTENSION
  QScreen *primaryScreen = QGuiApplication::primaryScreen();
  if (primaryScreen) {
    QPixmap pixmap =
        primaryScreen->grabWindow(0, region.x, region.y, region.w, region.h);
    QImage image = pixmap.toImage();

    cachedScreenshot =
        cv::Mat(image.height(), image.width(), CV_8UC4,
                const_cast<uchar *>(image.bits()), image.bytesPerLine());
    cv::cvtColor(cachedScreenshot, cachedScreenshot, cv::COLOR_BGRA2BGR);
  }
#endif
  captureTime = std::chrono::steady_clock::now();
}

void ScreenshotCache::clear() { cachedScreenshot.release(); }

void ScreenshotCache::setExpiry(int ms) { expiryMs = ms; }

bool ScreenshotCache::isExpired() const {
  if (expiryMs <= 0)
    return false;

  auto now = std::chrono::steady_clock::now();
  auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - captureTime)
          .count();

  return elapsed > expiryMs;
}

// ============================================================================
// PixelAutomation Implementation
// ============================================================================

PixelAutomation::PixelAutomation()
    : cache(std::make_unique<ScreenshotCache>()) {}

PixelAutomation::~PixelAutomation() {}

cv::Mat PixelAutomation::captureScreenInternal() {
  if (cacheEnabled && cache->isValid() && !cache->isExpired()) {
    return cache->get().clone();
  }

  cache->capture();
  return cache->get().clone();
}

Color PixelAutomation::getPixel(int x, int y) {
#ifdef HAVE_QT_EXTENSION
  QScreen *primaryScreen = QGuiApplication::primaryScreen();
  if (!primaryScreen) {
    return Color(0, 0, 0);
  }

  // Hide cursor during capture to avoid capturing cursor color
  bool cursorHidden = false;
  int cursorX = 0, cursorY = 0;

#ifdef LINUX_USED
  Display *display = havel::DisplayManager::GetDisplay();
  if (display) {
    // Get cursor position
    Window root, child;
    int rootX, rootY, winX, winY;
    unsigned int mask;
    if (XQueryPointer(display, DefaultRootWindow(display), &root, &child,
                      &rootX, &rootY, &winX, &winY, &mask)) {
      cursorX = rootX;
      cursorY = rootY;

      // Hide cursor if it's near the capture area
      if (std::abs(cursorX - x) < 10 && std::abs(cursorY - y) < 10) {
        XFixesHideCursor(display, DefaultRootWindow(display));
        XFlush(display);
        cursorHidden = true;
        // Small delay to ensure cursor is hidden
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
    }
  }
#endif

  // Capture a small region to ensure we get the pixel reliably
  // grabWindow can have issues with single pixels on some systems
  int captureSize = 3;
  QPixmap pixmap =
      primaryScreen->grabWindow(0, x - 1, y - 1, captureSize, captureSize);

  // Restore cursor
#ifdef LINUX_USED
  if (display) {
    if (cursorHidden) {
      XFixesShowCursor(display, DefaultRootWindow(display));
      XFlush(display);
    }
    XCloseDisplay(display);
  }
#endif

  if (pixmap.isNull()) {
    return Color(0, 0, 0);
  }

  QImage image = pixmap.toImage();
  // Get center pixel of captured region
  QRgb rgb = image.pixel(1, 1);
  return Color(qRed(rgb), qGreen(rgb), qBlue(rgb), qAlpha(rgb));
#else
  (void)x; (void)y;
  return Color(0, 0, 0);
#endif
}

bool PixelAutomation::pixelMatch(int x, int y, const Color &expectedColor,
                                 int tolerance) {
  Color actual = getPixel(x, y);
  return actual.near(expectedColor, tolerance);
}

bool PixelAutomation::pixelMatch(int x, int y, const std::string &hexColor,
                                 int tolerance) {
  Color expected = Color::fromHex(hexColor);
  Color actual = getPixel(x, y);

  // Debug: log the colors being compared
  printf("DEBUG: pixelMatch(%d,%d) expected=%s actual=%s tolerance=%d "
         "r_diff=%d g_diff=%d b_diff=%d result=%d\n",
         x, y, expected.toHex().c_str(), actual.toHex().c_str(), tolerance,
         std::abs(actual.r - expected.r), std::abs(actual.g - expected.g),
         std::abs(actual.b - expected.b), actual.near(expected, tolerance));

  return actual.near(expected, tolerance);
}

bool PixelAutomation::waitPixel(int x, int y, const Color &expectedColor,
                                int tolerance, int timeout) {
  auto startTime = std::chrono::steady_clock::now();

  while (true) {
    if (pixelMatch(x, y, expectedColor, tolerance)) {
      return true;
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - startTime)
                       .count();

    if (elapsed >= timeout) {
      return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

bool PixelAutomation::waitPixel(int x, int y, const std::string &hexColor,
                                int tolerance, int timeout) {
  Color expected = Color::fromHex(hexColor);
  return waitPixel(x, y, expected, tolerance, timeout);
}

std::vector<TemplateMatchResult>
PixelAutomation::matchTemplate(const cv::Mat &screen,
                               const cv::Mat &templateImg, float threshold) {

  std::vector<TemplateMatchResult> matches;

  if (screen.empty() || templateImg.empty()) {
    return matches;
  }

  // Ensure same color space
  cv::Mat screenGray, templateGray;
  if (screen.channels() > 1) {
    cv::cvtColor(screen, screenGray, cv::COLOR_BGR2GRAY);
  } else {
    screenGray = screen;
  }

  if (templateImg.channels() > 1) {
    cv::cvtColor(templateImg, templateGray, cv::COLOR_BGR2GRAY);
  } else {
    templateGray = templateImg;
  }

  // Template matching
  cv::Mat result;
  cv::matchTemplate(screenGray, templateGray, result, cv::TM_CCOEFF_NORMED);

  // Find matches above threshold
  cv::Mat matchLocations;
  cv::findNonZero(result >= threshold, matchLocations);

  if (matchLocations.empty()) {
    return matches;
  }

  // Convert to vector of points
  std::vector<cv::Point> points;
  matchLocations.convertTo(points, CV_32S);

  // Non-maximum suppression (simple version - skip nearby matches)
  std::vector<bool> kept(points.size(), true);
  int minDistance = std::max(templateGray.cols, templateGray.rows) / 2;

  for (size_t i = 0; i < points.size(); i++) {
    if (!kept[i])
      continue;

    for (size_t j = i + 1; j < points.size(); j++) {
      if (!kept[j])
        continue;

      int dx = points[i].x - points[j].x;
      int dy = points[i].y - points[j].y;

      if (std::abs(dx) < minDistance && std::abs(dy) < minDistance) {
        // Keep the one with higher confidence
        float confI = result.at<float>(points[i].y, points[i].x);
        float confJ = result.at<float>(points[j].y, points[j].x);

        if (confI < confJ) {
          kept[i] = false;
        } else {
          kept[j] = false;
        }
      }
    }
  }

  // Build results
  for (size_t i = 0; i < points.size(); i++) {
    if (kept[i]) {
      TemplateMatchResult res;
      res.location = points[i];
      res.confidence = result.at<float>(points[i].y, points[i].x);
      matches.push_back(res);
    }
  }

  return matches;
}

ImageMatch PixelAutomation::findImage(const std::string &imagePath,
                                      const ScreenRegion &region,
                                      float threshold) {
  // Load template image
  cv::Mat templateImg = cv::imread(imagePath);
  if (templateImg.empty()) {
    return ImageMatch();
  }

  // Capture screen (or region)
  cv::Mat screen;
  if (region.w > 0 && region.h > 0) {
    screen = captureScreenInternal();
    screen = screen(region.toCvRect());
  } else {
    screen = captureScreenInternal();
  }

  // Match template
  auto matches = matchTemplate(screen, templateImg, threshold);

  if (matches.empty()) {
    return ImageMatch();
  }

  // Return best match
  auto &best = matches[0];

  // Adjust coordinates if searching in region
  int offsetX = (region.w > 0) ? region.x : 0;
  int offsetY = (region.h > 0) ? region.y : 0;

  return ImageMatch(best.location.x + offsetX, best.location.y + offsetY,
                    templateImg.cols, templateImg.rows, best.confidence);
}

ImageMatch PixelAutomation::waitImage(const std::string &imagePath,
                                      const ScreenRegion &region, int timeout,
                                      float threshold) {
  auto startTime = std::chrono::steady_clock::now();

  while (true) {
    ImageMatch match = findImage(imagePath, region, threshold);

    if (match.found) {
      return match;
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - startTime)
                       .count();

    if (elapsed >= timeout) {
      return ImageMatch();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

bool PixelAutomation::existsImage(const std::string &imagePath,
                                  const ScreenRegion &region, float threshold) {
  ImageMatch match = findImage(imagePath, region, threshold);
  return match.found;
}

int PixelAutomation::countImage(const std::string &imagePath,
                                const ScreenRegion &region, float threshold) {
  // Load template image
  cv::Mat templateImg = cv::imread(imagePath);
  if (templateImg.empty()) {
    return 0;
  }

  // Capture screen (or region)
  cv::Mat screen;
  if (region.w > 0 && region.h > 0) {
    screen = captureScreenInternal();
    screen = screen(region.toCvRect());
  } else {
    screen = captureScreenInternal();
  }

  auto matches = matchTemplate(screen, templateImg, threshold);
  return static_cast<int>(matches.size());
}

std::vector<ImageMatch>
PixelAutomation::findAllImage(const std::string &imagePath,
                              const ScreenRegion &region, float threshold) {
  // Load template image
  cv::Mat templateImg = cv::imread(imagePath);
  if (templateImg.empty()) {
    return {};
  }

  // Capture screen (or region)
  cv::Mat screen;
  if (region.w > 0 && region.h > 0) {
    screen = captureScreenInternal();
    screen = screen(region.toCvRect());
  } else {
    screen = captureScreenInternal();
  }

  auto matches = matchTemplate(screen, templateImg, threshold);

  // Convert to ImageMatch vector
  std::vector<ImageMatch> results;
  int offsetX = (region.w > 0) ? region.x : 0;
  int offsetY = (region.h > 0) ? region.y : 0;

  for (auto &match : matches) {
    results.emplace_back(match.location.x + offsetX, match.location.y + offsetY,
                         templateImg.cols, templateImg.rows, match.confidence);
  }

  return results;
}

std::string PixelAutomation::readText(const ScreenRegion &region) {
  return readText(region, "eng", "");
}

std::string PixelAutomation::readText(const ScreenRegion &region,
                                      const std::string &language,
                                      const std::string &whitelist) {
  // Capture region
  cv::Mat screen;
  if (region.w > 0 && region.h > 0) {
    screen = captureScreenInternal();
    screen = screen(region.toCvRect());
  } else {
    screen = captureScreenInternal();
  }

  // Preprocess for better OCR
  screen = OCR::preprocessForText(screen);

  // Run OCR
  OCR::OCRConfig config;
  config.language = language;
  config.dataPath = "/usr/share/tessdata";
  config.psm = OCR::PageSegmentationMode::SINGLE_BLOCK;
  config.charWhitelist = whitelist;

  OCR ocr(config);
  OCR::OCRResult result = ocr.recognize(screen);

  return result.text;
}

void PixelAutomation::setCacheEnabled(bool enabled, int expiryMs) {
  cacheEnabled = enabled;
  if (enabled) {
    cache->setExpiry(expiryMs);
  }
}

void PixelAutomation::captureScreen() { cache->capture(); }

void PixelAutomation::clearCache() { cache->clear(); }

ScreenRegion PixelAutomation::region(int x, int y, int w, int h) {
  return ScreenRegion(x, y, w, h);
}

ScreenRegion PixelAutomation::fullScreen() {
  return ScreenRegion::fullScreen();
}

} // namespace havel
