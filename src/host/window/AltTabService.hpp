#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

#ifdef HAVE_QT_EXTENSION
namespace havel { class AltTabWindow; }
#endif

namespace havel::host {

struct AltTabInfo {
  std::string title;
  std::string className;
  std::string processName;
  int64_t windowId = 0;
  bool active = false;
};

class AltTabService {
public:
  AltTabService();
  ~AltTabService();

  void show();
  void hide();
  void toggle();

  void next();
  void previous();
  void select();

  void refresh();
  std::vector<AltTabInfo> getWindows() const;
  int getWindowCount() const;

  void setThumbnailSize(int width, int height);
  int getThumbnailWidth() const;
  int getThumbnailHeight() const;
  void setMaxVisibleWindows(int count);
  int getMaxVisibleWindows() const;
  void setAnimationsEnabled(bool enabled);
  bool isAnimationsEnabled() const;

private:
#ifdef HAVE_QT_EXTENSION
  std::shared_ptr<havel::AltTabWindow> m_altTab;
#endif
};

} // namespace havel::host
