#include "AltTabService.hpp"
#include "extensions/gui/alt_tab/AltTab.hpp"
#include "window/WindowQuery.hpp"

namespace havel::host {

AltTabService::AltTabService() {
  try {
    m_altTab = std::make_shared<havel::AltTabWindow>();
  } catch (...) {
    m_altTab = nullptr;
  }
}

AltTabService::~AltTabService() {}

void AltTabService::show() {
  if (m_altTab) m_altTab->showAltTab();
}

void AltTabService::hide() {
  if (m_altTab) m_altTab->hideAltTab();
}

void AltTabService::toggle() {
  if (m_altTab) {
    if (m_altTab->isVisible()) {
      m_altTab->hideAltTab();
    } else {
      m_altTab->showAltTab();
    }
  }
}

void AltTabService::next() {
  if (m_altTab) m_altTab->nextWindow();
}

void AltTabService::previous() {
  if (m_altTab) m_altTab->prevWindow();
}

void AltTabService::select() {
  if (m_altTab) m_altTab->selectCurrentWindow();
}

void AltTabService::refresh() {
  if (m_altTab) m_altTab->refreshWindows();
}

std::vector<AltTabInfo> AltTabService::getWindows() const {
  if (!m_altTab) {
    auto allWindows = havel::WindowQuery::getAll();
    std::vector<AltTabInfo> result;
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

  return {};
}

int AltTabService::getWindowCount() const {
  return static_cast<int>(getWindows().size());
}

void AltTabService::setThumbnailSize(int width, int height) {
  if (m_altTab) m_altTab->setThumbnailSize(width, height);
}

int AltTabService::getThumbnailWidth() const {
  return m_altTab ? 100 : 100;
}

int AltTabService::getThumbnailHeight() const {
  return m_altTab ? 75 : 75;
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
