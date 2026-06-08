#pragma once

#include "host/clipboard/IClipboardBackend.hpp"
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstdint>

struct zwlr_data_control_manager_v1;
struct zwlr_data_control_device_v1;
struct zwlr_data_control_offer_v1;
struct zwlr_data_control_source_v1;
struct ext_data_control_manager_v1;
struct ext_data_control_device_v1;
struct ext_data_control_offer_v1;
struct ext_data_control_source_v1;

namespace havel {

class WaylandProtocolClient;

struct ClipboardSourceState {
    std::string text;
};

class __attribute__((visibility("default"))) WaylandClipboardBackend : public host::IClipboardBackend {
public:
    explicit WaylandClipboardBackend(WaylandProtocolClient &client);
    ~WaylandClipboardBackend() override;

    bool initialize();
    void cleanup();
    bool isAvailable() const { return available_; }

    std::string getText() const override;
    bool setText(const std::string &text) override;
    bool clear() override;
    bool hasText() const override;

    std::string getImage() const override;
    bool setImage(const std::string &base64Png) override;
    bool hasImage() const override;

    std::vector<std::string> getFiles() const override;
    bool setFiles(const std::vector<std::string> &paths) override;
    bool hasFiles() const override;

    std::string getPrimaryText() const;
    bool setPrimaryText(const std::string &text);

    bool pollEvents(int timeoutMs = 100);

    static void wlrSourceSend(void *data, struct zwlr_data_control_source_v1 *source,
                               const char *mime, int32_t fd);
    static void wlrSourceCancelled(void *data, struct zwlr_data_control_source_v1 *source);

    static void wlrDataOfferOffer(void *data, struct zwlr_data_control_offer_v1 *offer,
                                   const char *mime);
    static void extDataOfferOffer(void *data, struct ext_data_control_offer_v1 *offer,
                                   const char *mime);

    static void wlrDeviceDataOffer(void *data, struct zwlr_data_control_device_v1 *device,
                                    struct zwlr_data_control_offer_v1 *offer);
    static void wlrDeviceSelection(void *data, struct zwlr_data_control_device_v1 *device,
                                    struct zwlr_data_control_offer_v1 *offer);
    static void wlrDevicePrimarySelection(void *data, struct zwlr_data_control_device_v1 *device,
                                            struct zwlr_data_control_offer_v1 *offer);
    static void wlrDeviceFinished(void *data, struct zwlr_data_control_device_v1 *device);

    static void extDeviceDataOffer(void *data, struct ext_data_control_device_v1 *device,
                                    struct ext_data_control_offer_v1 *offer);
    static void extDeviceSelection(void *data, struct ext_data_control_device_v1 *device,
                                    struct ext_data_control_offer_v1 *offer);
    static void extDevicePrimarySelection(void *data, struct ext_data_control_device_v1 *device,
                                            struct ext_data_control_offer_v1 *offer);
    static void extDeviceFinished(void *data, struct ext_data_control_device_v1 *device);

#ifdef HAS_EXT_DATA_CONTROL
    static void extSourceSend(void *data, struct ext_data_control_source_v1 *source,
                               const char *mime, int32_t fd);
    static void extSourceCancelled(void *data, struct ext_data_control_source_v1 *source);
#endif

private:
    std::string readFromOfferWlr(struct zwlr_data_control_offer_v1 *offer) const;
    std::string readFromOfferExt(struct ext_data_control_offer_v1 *offer) const;
    bool writeSelectionWlr(const std::string &text, bool primary);
    bool writeSelectionExt(const std::string &text, bool primary);

    WaylandProtocolClient &client_;
    bool available_ = false;
    bool useExt_ = false;

    struct zwlr_data_control_device_v1 *wlrDevice_ = nullptr;
    struct ext_data_control_device_v1 *extDevice_ = nullptr;

    mutable std::mutex mutex_;
    mutable struct zwlr_data_control_offer_v1 *currentWlrOffer_ = nullptr;
    mutable struct ext_data_control_offer_v1 *currentExtOffer_ = nullptr;
    mutable struct zwlr_data_control_offer_v1 *currentWlrPrimaryOffer_ = nullptr;
    mutable struct ext_data_control_offer_v1 *currentExtPrimaryOffer_ = nullptr;

    mutable std::vector<std::string> currentOfferMimes_;
    mutable std::vector<std::string> currentPrimaryOfferMimes_;
    mutable std::string cachedText_;
    mutable std::string cachedPrimaryText_;
    mutable bool hasText_ = false;
    mutable bool hasPrimaryText_ = false;
    mutable bool hasImage_ = false;
};

} // namespace havel
