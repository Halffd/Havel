#include "c/ToolkitPlugin.h"
#include "host/ui/ImGuiBackend.hpp"

using namespace havel::host;

static void *create_imgui_ui_backend() {
#ifdef HAVE_IMGUI_BACKEND
    return new ImGuiBackend();
#else
    return nullptr;
#endif
}

static void destroy_imgui_ui_backend(void *p) {
    delete castUIBackend(p);
}

HAVEL_TOOLKIT_PLUGIN_IMPL(imgui, "1.0.0", "Dear ImGui UI toolkit backend")

namespace {
struct ImGuiToolkitInit {
    ImGuiToolkitInit() {
        HAVEL_TOOLKIT_SET_UI_BACKEND(create_imgui_ui_backend, destroy_imgui_ui_backend)
    }
} init;
}
