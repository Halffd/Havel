#pragma once

#include "IAltTabBackend.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <memory>

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

    void setBackend(std::unique_ptr<IAltTabBackend> backend);
    IAltTabBackend* backend() const;
    bool hasBackend() const { return backend_ != nullptr; }

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
    std::unique_ptr<IAltTabBackend> backend_;
};

} // namespace havel::host
