#include "WaylandClipboardBackend.hpp"
#include "WaylandProtocolClient.hpp"
#include "utils/Logger.hpp"

#include <wayland-client.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <algorithm>

extern "C" {
#include "wlr-data-control-unstable-v1-client-protocol.h"
}

#ifdef HAS_EXT_DATA_CONTROL
#include "ext-data-control-v1-client-protocol.h"
#endif

namespace havel {

static const char *TEXT_MIME = "text/plain;charset=utf-8";
static const char *TEXT_MIME_ALT = "text/plain";

static const struct zwlr_data_control_offer_v1_listener wlrOfferListener_ = {
    .offer = WaylandClipboardBackend::wlrDataOfferOffer,
};

static const struct zwlr_data_control_source_v1_listener wlrSourceListener_ = {
    .send = WaylandClipboardBackend::wlrSourceSend,
    .cancelled = WaylandClipboardBackend::wlrSourceCancelled,
};

#ifdef HAS_EXT_DATA_CONTROL
static const struct ext_data_control_offer_v1_listener extOfferListener_ = {
    .offer = WaylandClipboardBackend::extDataOfferOffer,
};
#endif

static const struct zwlr_data_control_device_v1_listener wlrDeviceListener_ = {
    .data_offer = WaylandClipboardBackend::wlrDeviceDataOffer,
    .selection = WaylandClipboardBackend::wlrDeviceSelection,
    .finished = WaylandClipboardBackend::wlrDeviceFinished,
    .primary_selection = WaylandClipboardBackend::wlrDevicePrimarySelection,
};

#ifdef HAS_EXT_DATA_CONTROL
static const struct ext_data_control_device_v1_listener extDeviceListener_ = {
    .data_offer = WaylandClipboardBackend::extDeviceDataOffer,
    .selection = WaylandClipboardBackend::extDeviceSelection,
    .primary_selection = WaylandClipboardBackend::extDevicePrimarySelection,
    .finished = WaylandClipboardBackend::extDeviceFinished,
};
#endif

WaylandClipboardBackend::WaylandClipboardBackend(WaylandProtocolClient &client)
    : client_(client) {}

WaylandClipboardBackend::~WaylandClipboardBackend() {
    cleanup();
}

bool WaylandClipboardBackend::initialize() {
    if (available_) return true;

    if (!client_.isConnected() && !client_.connect()) {
        error("WaylandClipboardBackend: no Wayland connection");
        return false;
    }

    if (client_.hasExtDataControl()) {
#ifdef HAS_EXT_DATA_CONTROL
        useExt_ = true;
        ext_data_control_manager_v1 *mgr = client_.extDataControlManager();
        extDevice_ = ext_data_control_manager_v1_get_data_device(mgr, client_.seat());
        if (!extDevice_) {
            error("WaylandClipboardBackend: failed to create ext data device");
            useExt_ = false;
        } else {
            ext_data_control_device_v1_add_listener(extDevice_, &extDeviceListener_, this);
        }
#endif
    }

    if (!useExt_ && client_.hasWlrDataControl()) {
        wlrDevice_ = zwlr_data_control_manager_v1_get_data_device(
            client_.wlrDataControlManager(), client_.seat());
        if (!wlrDevice_) {
            error("WaylandClipboardBackend: failed to create wlr data device");
            return false;
        }
        zwlr_data_control_device_v1_add_listener(wlrDevice_, &wlrDeviceListener_, this);
    }

    if (!useExt_ && !wlrDevice_) {
        debug("WaylandClipboardBackend: no data control protocol available");
        return false;
    }

    client_.roundtrip();

    available_ = true;
    info("WaylandClipboardBackend: initialized ({})", useExt_ ? "ext-data-control" : "wlr-data-control");
    return true;
}

void WaylandClipboardBackend::cleanup() {
    if (currentWlrOffer_) {
        zwlr_data_control_offer_v1_destroy(currentWlrOffer_);
        currentWlrOffer_ = nullptr;
    }
    if (currentWlrPrimaryOffer_) {
        zwlr_data_control_offer_v1_destroy(currentWlrPrimaryOffer_);
        currentWlrPrimaryOffer_ = nullptr;
    }
    if (wlrDevice_) {
        zwlr_data_control_device_v1_destroy(wlrDevice_);
        wlrDevice_ = nullptr;
    }
#ifdef HAS_EXT_DATA_CONTROL
    if (currentExtOffer_) {
        ext_data_control_offer_v1_destroy(currentExtOffer_);
        currentExtOffer_ = nullptr;
    }
    if (currentExtPrimaryOffer_) {
        ext_data_control_offer_v1_destroy(currentExtPrimaryOffer_);
        currentExtPrimaryOffer_ = nullptr;
    }
    if (extDevice_) {
        ext_data_control_device_v1_destroy(extDevice_);
        extDevice_ = nullptr;
    }
#endif
    available_ = false;
}

std::string WaylandClipboardBackend::getText() const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (useExt_ && currentExtOffer_) {
        cachedText_ = readFromOfferExt(currentExtOffer_);
        hasText_ = !cachedText_.empty();
        return cachedText_;
    }
    if (!useExt_ && currentWlrOffer_) {
        cachedText_ = readFromOfferWlr(currentWlrOffer_);
        hasText_ = !cachedText_.empty();
        return cachedText_;
    }

    return "";
}

bool WaylandClipboardBackend::setText(const std::string &text) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (useExt_) {
        return writeSelectionExt(text, false);
    }
    return writeSelectionWlr(text, false);
}

bool WaylandClipboardBackend::clear() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (useExt_ && extDevice_) {
#ifdef HAS_EXT_DATA_CONTROL
        ext_data_control_device_v1_set_selection(extDevice_, nullptr);
#endif
        client_.roundtrip();
        cachedText_.clear();
        hasText_ = false;
        return true;
    }
    if (!useExt_ && wlrDevice_) {
        zwlr_data_control_device_v1_set_selection(wlrDevice_, nullptr);
        client_.roundtrip();
        cachedText_.clear();
        hasText_ = false;
        return true;
    }
    return false;
}

bool WaylandClipboardBackend::hasText() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (useExt_) return currentExtOffer_ != nullptr;
    return currentWlrOffer_ != nullptr;
}

std::string WaylandClipboardBackend::getImage() const { return ""; }
bool WaylandClipboardBackend::setImage(const std::string &) { return false; }
bool WaylandClipboardBackend::hasImage() const { return false; }
std::vector<std::string> WaylandClipboardBackend::getFiles() const { return {}; }
bool WaylandClipboardBackend::setFiles(const std::vector<std::string> &) { return false; }
bool WaylandClipboardBackend::hasFiles() const { return false; }

std::string WaylandClipboardBackend::getPrimaryText() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (useExt_ && currentExtPrimaryOffer_) {
        cachedPrimaryText_ = readFromOfferExt(currentExtPrimaryOffer_);
        hasPrimaryText_ = !cachedPrimaryText_.empty();
        return cachedPrimaryText_;
    }
    if (!useExt_ && currentWlrPrimaryOffer_) {
        cachedPrimaryText_ = readFromOfferWlr(currentWlrPrimaryOffer_);
        hasPrimaryText_ = !cachedPrimaryText_.empty();
        return cachedPrimaryText_;
    }
    return "";
}

bool WaylandClipboardBackend::setPrimaryText(const std::string &text) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (useExt_) return writeSelectionExt(text, true);
    return writeSelectionWlr(text, true);
}

bool WaylandClipboardBackend::pollEvents(int timeoutMs) {
    client_.dispatch(timeoutMs);
    return client_.isConnected();
}

std::string WaylandClipboardBackend::readFromOfferWlr(struct zwlr_data_control_offer_v1 *offer) const {
    if (!offer) return "";

    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) < 0) {
        error("WaylandClipboardBackend: pipe2 failed: {}", strerror(errno));
        return "";
    }

    zwlr_data_control_offer_v1_receive(offer, TEXT_MIME, pipefd[1]);
    close(pipefd[1]);

    client_.roundtrip();

    std::string result;
    char buf[4096];
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
        result.append(buf, n);
    }
    close(pipefd[0]);

    while (!result.empty() && (result.back() == '\n' || result.back() == '\0')) {
        result.pop_back();
    }

    return result;
}

std::string WaylandClipboardBackend::readFromOfferExt(struct ext_data_control_offer_v1 *offer) const {
    if (!offer) return "";

    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) < 0) {
        error("WaylandClipboardBackend: pipe2 failed: {}", strerror(errno));
        return "";
    }

#ifdef HAS_EXT_DATA_CONTROL
    ext_data_control_offer_v1_receive(offer, TEXT_MIME, pipefd[1]);
#endif
    close(pipefd[1]);

    client_.roundtrip();

    std::string result;
    char buf[4096];
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
        result.append(buf, n);
    }
    close(pipefd[0]);

    while (!result.empty() && (result.back() == '\n' || result.back() == '\0')) {
        result.pop_back();
    }

    return result;
}

bool WaylandClipboardBackend::writeSelectionWlr(const std::string &text, bool primary) {
    if (!client_.wlrDataControlManager() || !wlrDevice_) return false;

    auto *source = zwlr_data_control_manager_v1_create_data_source(
        client_.wlrDataControlManager());
    if (!source) return false;

    auto *srcState = new ClipboardSourceState{text};
    zwlr_data_control_source_v1_add_listener(source, &wlrSourceListener_, srcState);

    zwlr_data_control_source_v1_offer(source, TEXT_MIME);
    zwlr_data_control_source_v1_offer(source, TEXT_MIME_ALT);

    if (primary) {
        zwlr_data_control_device_v1_set_primary_selection(wlrDevice_, source);
    } else {
        zwlr_data_control_device_v1_set_selection(wlrDevice_, source);
    }

    client_.roundtrip();

    struct pollfd pfd;
    constexpr int timeoutMs = 500;
    auto start = std::chrono::steady_clock::now();

    while (true) {
        client_.dispatch(0);

        int elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        int remaining = timeoutMs - elapsed;
        if (remaining <= 0) break;

        wl_display_flush(client_.display());
        pfd.fd = client_.pollFd();
        pfd.events = POLLIN;
        int ret = poll(&pfd, 1, remaining);
        if (ret <= 0) break;

        if (pfd.revents & POLLIN) {
            wl_display_dispatch(client_.display());
        }
    }

    return true;
}

bool WaylandClipboardBackend::writeSelectionExt(const std::string &text, bool primary) {
#ifdef HAS_EXT_DATA_CONTROL
    if (!client_.extDataControlManager() || !extDevice_) return false;

    auto *source = ext_data_control_manager_v1_create_data_source(
        client_.extDataControlManager());
    if (!source) return false;

    auto *srcState = new ClipboardSourceState{text};
    ext_data_control_source_v1_add_listener(source, &extSourceListener_, srcState);

    ext_data_control_source_v1_offer(source, TEXT_MIME);
    ext_data_control_source_v1_offer(source, TEXT_MIME_ALT);

    if (primary) {
        ext_data_control_device_v1_set_primary_selection(extDevice_, source);
    } else {
        ext_data_control_device_v1_set_selection(extDevice_, source);
    }

    client_.roundtrip();

    struct pollfd pfd;
    constexpr int timeoutMs = 500;
    auto start = std::chrono::steady_clock::now();

    while (true) {
        client_.dispatch(0);
        int elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        int remaining = timeoutMs - elapsed;
        if (remaining <= 0) break;

        wl_display_flush(client_.display());
        pfd.fd = client_.pollFd();
        pfd.events = POLLIN;
        int ret = poll(&pfd, 1, remaining);
        if (ret <= 0) break;
        if (pfd.revents & POLLIN) {
            wl_display_dispatch(client_.display());
        }
    }

    return true;
#else
    (void)text;
    (void)primary;
    return false;
#endif
}

void WaylandClipboardBackend::wlrSourceSend(void *data,
struct zwlr_data_control_source_v1 *,
const char *mime, int32_t fd) {
auto *state = static_cast<ClipboardSourceState *>(data);
if ((mime && strcmp(mime, TEXT_MIME) == 0) || (mime && strcmp(mime, TEXT_MIME_ALT) == 0)) {
write(fd, state->text.c_str(), state->text.size());
}
close(fd);
}

void WaylandClipboardBackend::wlrSourceCancelled(void *data,
                                                   struct zwlr_data_control_source_v1 *source) {
    auto *state = static_cast<ClipboardSourceState *>(data);
    delete state;
    zwlr_data_control_source_v1_destroy(source);
}

#ifdef HAS_EXT_DATA_CONTROL
void WaylandClipboardBackend::extSourceSend(void *data,
struct ext_data_control_source_v1 *,
const char *mime, int32_t fd) {
auto *state = static_cast<ClipboardSourceState *>(data);
if ((mime && strcmp(mime, TEXT_MIME) == 0) || (mime && strcmp(mime, TEXT_MIME_ALT) == 0)) {
        write(fd, state->text.c_str(), state->text.size());
    }
    close(fd);
}

void WaylandClipboardBackend::extSourceCancelled(void *data,
                                                   struct ext_data_control_source_v1 *source) {
    auto *state = static_cast<ClipboardSourceState *>(data);
    delete state;
    ext_data_control_source_v1_destroy(source);
}
#endif

void WaylandClipboardBackend::wlrDataOfferOffer(void *data,
                                                 struct zwlr_data_control_offer_v1 *,
                                                 const char *) {
    (void)data;
}

void WaylandClipboardBackend::extDataOfferOffer(void *data,
                                                 struct ext_data_control_offer_v1 *,
                                                 const char *) {
    (void)data;
}

void WaylandClipboardBackend::wlrDeviceDataOffer(void *data,
                                                   struct zwlr_data_control_device_v1 *,
                                                   struct zwlr_data_control_offer_v1 *offer) {
    auto *self = static_cast<WaylandClipboardBackend *>(data);
    zwlr_data_control_offer_v1_add_listener(offer, &wlrOfferListener_, self);
}

void WaylandClipboardBackend::wlrDeviceSelection(void *data,
                                                   struct zwlr_data_control_device_v1 *,
                                                   struct zwlr_data_control_offer_v1 *offer) {
    auto *self = static_cast<WaylandClipboardBackend *>(data);
    std::lock_guard<std::mutex> lock(self->mutex_);

    if (self->currentWlrOffer_) {
        zwlr_data_control_offer_v1_destroy(self->currentWlrOffer_);
    }
    self->currentWlrOffer_ = offer;
    self->hasText_ = (offer != nullptr);
}

void WaylandClipboardBackend::wlrDevicePrimarySelection(void *data,
                                                          struct zwlr_data_control_device_v1 *,
                                                          struct zwlr_data_control_offer_v1 *offer) {
    auto *self = static_cast<WaylandClipboardBackend *>(data);
    std::lock_guard<std::mutex> lock(self->mutex_);

    if (self->currentWlrPrimaryOffer_) {
        zwlr_data_control_offer_v1_destroy(self->currentWlrPrimaryOffer_);
    }
    self->currentWlrPrimaryOffer_ = offer;
    self->hasPrimaryText_ = (offer != nullptr);
}

void WaylandClipboardBackend::wlrDeviceFinished(void *data,
                                                  struct zwlr_data_control_device_v1 *) {
    auto *self = static_cast<WaylandClipboardBackend *>(data);
    self->available_ = false;
}

#ifdef HAS_EXT_DATA_CONTROL
void WaylandClipboardBackend::extDeviceDataOffer(void *data,
                                                   struct ext_data_control_device_v1 *,
                                                   struct ext_data_control_offer_v1 *offer) {
    auto *self = static_cast<WaylandClipboardBackend *>(data);
    ext_data_control_offer_v1_add_listener(offer, &extOfferListener_, self);
}

void WaylandClipboardBackend::extDeviceSelection(void *data,
                                                   struct ext_data_control_device_v1 *,
                                                   struct ext_data_control_offer_v1 *offer) {
    auto *self = static_cast<WaylandClipboardBackend *>(data);
    std::lock_guard<std::mutex> lock(self->mutex_);

    if (self->currentExtOffer_) {
        ext_data_control_offer_v1_destroy(self->currentExtOffer_);
    }
    self->currentExtOffer_ = offer;
    self->hasText_ = (offer != nullptr);
}

void WaylandClipboardBackend::extDevicePrimarySelection(void *data,
                                                          struct ext_data_control_device_v1 *,
                                                          struct ext_data_control_offer_v1 *offer) {
    auto *self = static_cast<WaylandClipboardBackend *>(data);
    std::lock_guard<std::mutex> lock(self->mutex_);

    if (self->currentExtPrimaryOffer_) {
        ext_data_control_offer_v1_destroy(self->currentExtPrimaryOffer_);
    }
    self->currentExtPrimaryOffer_ = offer;
    self->hasPrimaryText_ = (offer != nullptr);
}

void WaylandClipboardBackend::extDeviceFinished(void *data,
                                                  struct ext_data_control_device_v1 *) {
    auto *self = static_cast<WaylandClipboardBackend *>(data);
    self->available_ = false;
}
#endif

} // namespace havel
