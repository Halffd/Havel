// QtScreenCapture.cpp — the Qt half of pixel automation.
//
// Compiled into havel_gui only. The core asks for screen pixels through
// core/automation/ScreenCapture.hpp; this is what answers. The body is the code
// that used to sit inline in PixelAutomation.cpp behind HAVE_QT_EXTENSION,
// unchanged in behaviour: no QApplication is created here, so a host without one
// gets a null screen and degrades exactly as before.

#include "core/automation/ScreenCapture.hpp"

#include <QGuiApplication>
#include <QImage>
#include <QPixmap>
#include <QRect>
#include <QScreen>

#include <algorithm>
#include <cstddef>

namespace {

bool qtBounds(havel::ScreenBounds &out) {
  QScreen *screen = QGuiApplication::primaryScreen();
  if (!screen) {
    return false;
  }
  const QRect geometry = screen->geometry();
  out.x = geometry.x();
  out.y = geometry.y();
  out.w = geometry.width();
  out.h = geometry.height();
  return true;
}

bool qtCapture(const havel::ScreenBounds &region, havel::ScreenPixels &out) {
  QScreen *screen = QGuiApplication::primaryScreen();
  if (!screen) {
    return false;
  }

  QPixmap pixmap;
  if (region.w > 0 && region.h > 0) {
    pixmap = screen->grabWindow(0, region.x, region.y, region.w, region.h);
  } else {
    pixmap = screen->grabWindow(0);
  }
  if (pixmap.isNull()) {
    return false;
  }

  // Format_ARGB32 is BGRA in memory, which is what the core's
  // cv::COLOR_BGRA2BGR conversion expects. convertTo() also drops any row
  // padding, so the copy is tightly packed.
  const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
  if (image.isNull()) {
    return false;
  }

  out.w = image.width();
  out.h = image.height();
  out.bgra.resize(static_cast<size_t>(out.w) * static_cast<size_t>(out.h) * 4);
  for (int y = 0; y < out.h; ++y) {
    const uchar *src = image.constScanLine(y);
    std::copy(src, src + static_cast<size_t>(out.w) * 4,
              out.bgra.begin() + static_cast<ptrdiff_t>(y) * out.w * 4);
  }
  return true;
}

const havel::ScreenProvider kQtScreenProvider{&qtBounds, &qtCapture};

} // namespace

namespace {
// PixelAutomation is reachable from the language host (HostAPI.cpp) without any
// bridge installation, so the provider has to exist before the first pixel call
// rather than at some later "install everything" point. A static initialiser in
// this file would *not* do it: every symbol here is local, so the linker has no
// reason to extract this member out of libhavel_gui.a and the initialiser would
// never run. An exported function called from the already-strongly-extracted
// Qt bridge initialiser is what actually guarantees both extraction and order.
} // namespace

namespace havel::qt {
void installQtScreenCapture() {
  static const bool registered = [] {
    setScreenProvider(&kQtScreenProvider);
    return true;
  }();
  (void)registered;
}
} // namespace havel::qt
