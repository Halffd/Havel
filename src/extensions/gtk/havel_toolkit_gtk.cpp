#include "c/ToolkitPlugin.h"

#ifdef HAVE_GTK_BACKEND
#include <gtk/gtk.h>
#endif

#include "host/ui/GtkBackend.hpp"

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

HAVEL_TOOLKIT_PLUGIN_IMPL(gtk, "1.0.0", "GTK4 UI toolkit backend")

namespace {
struct GtkToolkitInit {
    GtkToolkitInit() {
        HAVEL_TOOLKIT_SET_UI_BACKEND(create_gtk_ui_backend, destroy_gtk_ui_backend)
    }
} init;
}
