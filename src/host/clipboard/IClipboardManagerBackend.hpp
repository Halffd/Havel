#pragma once

#include <string>
#include <vector>

namespace havel::host {

class IClipboardManagerBackend {
public:
    virtual ~IClipboardManagerBackend() = default;

    virtual void addToHistory(const std::string &text) = 0;
    virtual void clearHistory() = 0;
    virtual std::string getHistoryItem(int index) const = 0;
    virtual int getHistoryCount() const = 0;
    virtual void pasteHistoryItem(int index) = 0;

    virtual void show() = 0;
    virtual void hide() = 0;
    virtual void toggleVisibility() = 0;
    virtual bool isEnabled() const = 0;
    virtual void enable() = 0;
    virtual void disable() = 0;
};

} // namespace havel::host
