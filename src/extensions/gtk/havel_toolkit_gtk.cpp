#include "c/ToolkitPlugin.h"

#ifdef HAVE_GTK_BACKEND
#include <gtk/gtk.h>
#endif

#include "host/ui/GtkBackend.hpp"
#include "extensions/HavelCAPI.h"

using namespace havel::host;

static void *create_gtk_ui_backend() {
#ifdef HAVE_GTK_BACKEND
    return new GtkBackend();
#else
    return nullptr;
#endif
}

static void destroy_gtk_ui_backend(void *p) {
    delete castUIBackend(p);
}

// Bridge the toolkit ABI's registration hook to the C extension entry in
// gtk_extension.cpp, mirroring havel_toolkit_qt.cpp. UIManager invokes this
// at backend-creation time with the real global HavelAPI table.
extern "C" void havel_extension_init(HavelAPI *api);

void gtk_toolkit_register_functions(void *api) {
    havel_extension_init(static_cast<HavelAPI *>(api));
}

HAVEL_TOOLKIT_PLUGIN_IMPL(gtk, "1.0.0", "GTK4 UI toolkit backend")

namespace {
struct GtkToolkitInit {
    GtkToolkitInit() {
        HAVEL_TOOLKIT_SET_UI_BACKEND(create_gtk_ui_backend, destroy_gtk_ui_backend)
        HAVEL_TOOLKIT_SET_EXT_FUNCTIONS(gtk_toolkit_register_functions)
    }
} init;
}
