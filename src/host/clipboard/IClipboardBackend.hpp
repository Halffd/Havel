#pragma once

#include <string>
#include <vector>

namespace havel::host {

class IClipboardBackend {
public:
    virtual ~IClipboardBackend() = default;

    virtual std::string getText() const = 0;
    virtual bool setText(const std::string &text) = 0;
    virtual bool clear() = 0;
    virtual bool hasText() const = 0;

    virtual std::string getImage() const = 0;
    virtual bool setImage(const std::string &base64Png) = 0;
    virtual bool hasImage() const = 0;

    virtual std::vector<std::string> getFiles() const = 0;
    virtual bool setFiles(const std::vector<std::string> &paths) = 0;
    virtual bool hasFiles() const = 0;
};

} // namespace havel::host
