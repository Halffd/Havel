#pragma once
#include "IAltTabBackend.hpp"
#include <memory>

namespace havel {

class AltTabService {
public:
    AltTabService();
    ~AltTabService();
    static AltTabService& instance() {
        static AltTabService svc;
        return svc;
    }
    void setBackend(std::unique_ptr<IAltTabBackend> backend);
    void show();
    void hide();
    void next();
    void prev();
    bool isVisible() const;
    void select();
    void refresh();
    void setThumbnailSize(int width, int height);
    int getThumbnailWidth() const;
    int getThumbnailHeight() const;
    void setMaxVisibleWindows(int count);
    int getMaxVisibleWindows() const;
    void setAnimationsEnabled(bool enabled);
    bool isAnimationsEnabled() const;
    std::vector<AltTabInfo> getWindows() const;
    int getWindowCount() const;
    void toggle();
    void previous();
private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};

} // namespace havel
