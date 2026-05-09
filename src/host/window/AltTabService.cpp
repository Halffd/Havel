#include "AltTabService.hpp"
#include "window/WindowQuery.hpp"

#ifdef HAVE_QT_EXTENSION
#include "extensions/gui/alt_tab/AltTab.hpp"
#include <QApplication>
#endif

namespace havel::host {

AltTabService::AltTabService() {
#ifdef HAVE_QT_EXTENSION
  try {
    if (qApp) {
      m_altTab = std::make_shared<havel::AltTabWindow>();
    }
  } catch (...) {}
#endif
}

AltTabService::~AltTabService() = default;

void AltTabService::show() {
#ifdef HAVE_QT_EXTENSION
  if (m_altTab) m_altTab->showAltTab();
#endif
}

void AltTabService::hide() {
#ifdef HAVE_QT_EXTENSION
  if (m_altTab) m_altTab->hideAltTab();
#endif
}

void AltTabService::toggle() {
#ifdef HAVE_QT_EXTENSION
  if (m_altTab) {
    if (m_altTab->isVisible()) {
      m_altTab->hideAltTab();
    } else {
      m_altTab->showAltTab();
    }
  }
#endif
}

void AltTabService::next() {
#ifdef HAVE_QT_EXTENSION
  if (m_altTab) m_altTab->nextWindow();
#endif
}

void AltTabService::previous() {
#ifdef HAVE_QT_EXTENSION
  if (m_altTab) m_altTab->prevWindow();
#endif
}

void AltTabService::select() {
#ifdef HAVE_QT_EXTENSION
  if (m_altTab) m_altTab->selectCurrentWindow();
#endif
}

void AltTabService::refresh() {
#ifdef HAVE_QT_EXTENSION
  if (m_altTab) m_altTab->refreshWindows();
#endif
}

std::vector<AltTabInfo> AltTabService::getWindows() const {
  auto allWindows = havel::WindowQuery::getAll();
  std::vector<AltTabInfo> result;
  result.reserve(allWindows.size());
  for (const auto& w : allWindows) {
    AltTabInfo info;
    info.title = w.title;
    info.className = w.windowClass;
    info.processName = w.exe;
    info.windowId = static_cast<int64_t>(w.id);
    info.active = false;
    result.push_back(info);
  }
  return result;
}

int AltTabService::getWindowCount() const {
  return static_cast<int>(getWindows().size());
}

void AltTabService::setThumbnailSize(int width, int height) {
#ifdef HAVE_QT_EXTENSION
  if (m_altTab) m_altTab->setThumbnailSize(width, height);
#endif
}

int AltTabService::getThumbnailWidth() const {
#ifdef HAVE_QT_EXTENSION
  return m_altTab ? 200 : 100;
#else
  return 100;
#endif
}

int AltTabService::getThumbnailHeight() const {
#ifdef HAVE_QT_EXTENSION
  return m_altTab ? 150 : 75;
#else
  return 75;
#endif
}

void AltTabService::setMaxVisibleWindows(int count) {
  (void)count;
}

int AltTabService::getMaxVisibleWindows() const {
  return 10;
}

void AltTabService::setAnimationsEnabled(bool enabled) {
  (void)enabled;
}

bool AltTabService::isAnimationsEnabled() const {
  return true;
}

} // namespace havel::host
