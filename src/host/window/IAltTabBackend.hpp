#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace havel::host {

class IAltTabBackend {
public:
    virtual ~IAltTabBackend() = default;

    virtual void show() = 0;
    virtual void hide() = 0;
    virtual bool isVisible() const = 0;
    virtual void next() = 0;
    virtual void previous() = 0;
    virtual void select() = 0;
    virtual void refresh() = 0;
    virtual void setThumbnailSize(int width, int height) = 0;
};

} // namespace havel::host
