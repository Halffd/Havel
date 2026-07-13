#pragma once

#include <cstdint>
#include <vector>
#include <string>

namespace havel {

struct AltTabInfo {
    std::string title;
    std::string className;
    std::string processName;
    uint64_t windowId = 0;
    bool active = false;
};

class IAltTabBackend {
public:
    virtual ~IAltTabBackend() = default;
    virtual void show() = 0;
    virtual void hide() = 0;
    virtual void next() = 0;
    virtual void prev() = 0;
    virtual bool isVisible() const = 0;
    virtual void select() = 0;
    virtual void refresh() = 0;
    virtual void setThumbnailSize(int width, int height) = 0;
    virtual int getThumbnailWidth() const = 0;
    virtual int getThumbnailHeight() const = 0;
    virtual void setMaxVisibleWindows(int count) = 0;
    virtual int getMaxVisibleWindows() const = 0;
    virtual void setAnimationsEnabled(bool enabled) = 0;
    virtual bool isAnimationsEnabled() const = 0;
    virtual std::vector<std::string> getWindows() const = 0;
    virtual int getWindowCount() const = 0;
};
} // namespace havel
