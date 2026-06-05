#pragma once

#include <stdint.h>

#define HAVEL_TOOLKIT_ABI_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HavelToolkitABI {
int abi_version;
const char *name;
const char *version;
const char *description;

void *(*create_ui_backend)(void);
void (*destroy_ui_backend)(void *backend);

void *(*create_screenshot_backend)(void);
void (*destroy_screenshot_backend)(void *backend);

void *(*create_alttab_backend)(void);
void (*destroy_alttab_backend)(void *backend);

void *(*create_clipboard_backend)(void);
void (*destroy_clipboard_backend)(void *backend);

void *(*create_clipboard_manager_backend)(void);
void (*destroy_clipboard_manager_backend)(void *backend);

void (*register_extension_functions)(void *api);

void *reserved[4];
} HavelToolkitABI;

typedef const HavelToolkitABI *(*havel_toolkit_info_fn)(void);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

#include "host/ui/UIBackend.hpp"
#include "host/screenshot/IScreenshotBackend.hpp"
#include "host/window/IAltTabBackend.hpp"
#include "host/clipboard/IClipboardBackend.hpp"
#include "host/clipboard/IClipboardManagerBackend.hpp"

namespace havel::host {

inline UIBackend *castUIBackend(void *p) { return static_cast<UIBackend *>(p); }
inline IScreenshotBackend *castScreenshotBackend(void *p) { return static_cast<IScreenshotBackend *>(p); }
inline IAltTabBackend *castAltTabBackend(void *p) { return static_cast<IAltTabBackend *>(p); }
inline IClipboardBackend *castClipboardBackend(void *p) { return static_cast<IClipboardBackend *>(p); }
inline IClipboardManagerBackend *castClipboardManagerBackend(void *p) { return static_cast<IClipboardManagerBackend *>(p); }

}

#define HAVEL_TOOLKIT_PLUGIN_IMPL(name, version_str, description_str) \
extern "C" const HavelToolkitABI *havel_toolkit_info(void); \
static HavelToolkitABI havel_toolkit_abi_##name = { \
HAVEL_TOOLKIT_ABI_VERSION, \
#name, \
version_str, \
description_str, \
nullptr, nullptr, \
nullptr, nullptr, \
nullptr, nullptr, \
nullptr, nullptr, \
nullptr, nullptr, \
nullptr, \
{nullptr, nullptr, nullptr, nullptr} \
}; \
static HavelToolkitABI *havel_toolkit_abi_ptr = &havel_toolkit_abi_##name; \
extern "C" const HavelToolkitABI *havel_toolkit_info(void) { \
return &havel_toolkit_abi_##name; \
}

#define HAVEL_TOOLKIT_SET_UI_BACKEND(factory, destroyer) \
havel_toolkit_abi_ptr->create_ui_backend = (void *(*)(void))(factory); \
havel_toolkit_abi_ptr->destroy_ui_backend = (void (*)(void *))(destroyer);

#define HAVEL_TOOLKIT_SET_SCREENSHOT_BACKEND(factory, destroyer) \
havel_toolkit_abi_ptr->create_screenshot_backend = (void *(*)(void))(factory); \
havel_toolkit_abi_ptr->destroy_screenshot_backend = (void (*)(void *))(destroyer);

#define HAVEL_TOOLKIT_SET_ALTTAB_BACKEND(factory, destroyer) \
havel_toolkit_abi_ptr->create_alttab_backend = (void *(*)(void))(factory); \
havel_toolkit_abi_ptr->destroy_alttab_backend = (void (*)(void *))(destroyer);

#define HAVEL_TOOLKIT_SET_CLIPBOARD_BACKEND(factory, destroyer) \
havel_toolkit_abi_ptr->create_clipboard_backend = (void *(*)(void))(factory); \
havel_toolkit_abi_ptr->destroy_clipboard_backend = (void (*)(void *))(destroyer);

#define HAVEL_TOOLKIT_SET_CLIPBOARD_MGR_BACKEND(factory, destroyer) \
havel_toolkit_abi_ptr->create_clipboard_manager_backend = (void *(*)(void))(factory); \
havel_toolkit_abi_ptr->destroy_clipboard_manager_backend = (void (*)(void *))(destroyer);

#define HAVEL_TOOLKIT_SET_EXT_FUNCTIONS(fn) \
havel_toolkit_abi_ptr->register_extension_functions = (void (*)(void *))(fn);

#endif
