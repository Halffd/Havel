/*
 * GtkBackend.cpp - GTK4 implementation of UIBackend
 *
 * Widgets are created eagerly; the FFI shim passes id-only placeholder
 * elements back in, so all operations resolve elements via the local
 * registry (elements_/widgets_) keyed by ElementId.
 */
#include "GtkBackend.hpp"

#ifdef HAVE_GTK_BACKEND

#include "modules/ui/UIElement.hpp"

extern "C" {
#include <glib.h>
#include <gtk/gtk.h>
}

#include <atomic>
#include <cstring>

namespace havel::host {

namespace {
// Nested main-loop pump so async GTK4 dialogs can be presented from the
// synchronous UIBackend API. The loop quits from the dialog callback and
// control returns to the caller with the result filled in.
struct DialogWait {
  GMainLoop *loop = g_main_loop_new(nullptr, false);
  bool done = false;
  int choice = -1;              // GtkAlertDialog button index
  GFile *file = nullptr;        // GtkFileDialog result
  ~DialogWait() { g_main_loop_unref(loop); }
};

GtkWindow *activeWindow(GtkApplication *app) {
  return app ? gtk_application_get_active_window(app) : nullptr;
}
} // namespace

std::shared_ptr<ui::UIElement> GtkBackend::registerElem(const char *type,
                                                        GtkWidget *widget) {
  auto el = std::make_shared<ui::UIElement>(type);
  el->id = nextId_++;
  el->nativeHandle = widget;
  el->realized = true;
  elements_[el->id] = el;
  if (widget) widgets_[el->id] = widget;
  return el;
}

GtkWidget* GtkBackend::getWidget(ui::ElementId id) const {
  auto it = widgets_.find(id);
  return it != widgets_.end() ? it->second : nullptr;
}

void GtkBackend::destroyWidget(ui::ElementId id) {
  auto it = widgets_.find(id);
  if (it != widgets_.end()) {
    if (it->second && GTK_IS_WIDGET(it->second)) gtk_widget_unparent(it->second);
    widgets_.erase(it);
  }
  auto mi = menus_.find(id);
  if (mi != menus_.end()) {
    if (mi->second) g_object_unref(mi->second);
    menus_.erase(mi);
  }
  toggleSwitches_.erase(id);
  gridCells_.erase(id);
  elements_.erase(id);
}

GtkBackend::GtkBackend() = default;

GtkBackend::~GtkBackend() {
  if (initialized_) shutdown();
}

bool GtkBackend::isAvailable() const {
  return gtk_init_check() && gtk_get_major_version() >= 4;
}

bool GtkBackend::initialize() {
  if (initialized_) return true;
  if (!gtk_init_check()) return false;

  app_ = gtk_application_new(appId_.c_str(), G_APPLICATION_DEFAULT_FLAGS);
  if (!app_) return false;

  setupSignalHandlers();
  // Notification delivery requires the GApplication to be registered.
  g_application_register(G_APPLICATION(app_), nullptr, nullptr);
  initialized_ = true;
  return true;
}

void GtkBackend::shutdown() {
  if (!initialized_) return;
  for (auto &pair : widgets_) {
    if (pair.second && GTK_IS_WIDGET(pair.second)) gtk_widget_unparent(pair.second);
  }
  widgets_.clear();
  elements_.clear();
  for (auto &m : menus_) if (m.second) g_object_unref(m.second);
  menus_.clear();
  toggleSwitches_.clear();
  if (loop_ && g_main_loop_is_running(loop_)) g_main_loop_quit(loop_);
  if (app_) { g_object_unref(app_); app_ = nullptr; }
  initialized_ = false;
}

// ---------------------------------------------------------------------------
// Element creation
// ---------------------------------------------------------------------------
std::shared_ptr<ui::UIElement> GtkBackend::window(const std::string &title) {
  GtkWidget *win = createWindowInternal(title, false);
  if (!win) return nullptr;
  auto el = registerElem(ui::ElementType::WINDOW, win);
  el->props["title"] = title;
  return el;
}

std::shared_ptr<ui::UIElement> GtkBackend::panel(const std::string &side) {
  bool vertical = (side == "left" || side == "right");
  auto el = registerElem(ui::ElementType::PANEL, createBoxInternal(vertical));
  el->props["side"] = side;
  return el;
}

std::shared_ptr<ui::UIElement> GtkBackend::modal(const std::string &title) {
  GtkWidget *win = createWindowInternal(title, true);
  if (!win) return nullptr;
  gtk_window_set_modal(GTK_WINDOW(win), true);
  auto el = registerElem(ui::ElementType::MODAL, win);
  el->props["title"] = title;
  return el;
}

std::shared_ptr<ui::UIElement> GtkBackend::text(const std::string &content) {
  GtkWidget *w = gtk_label_new(content.c_str());
  gtk_label_set_wrap(GTK_LABEL(w), true);
  auto el = registerElem(ui::ElementType::TEXT, w);
  el->props["text"] = content;
  return el;
}

std::shared_ptr<ui::UIElement> GtkBackend::label(const std::string &content) {
  GtkWidget *w = gtk_label_new(content.c_str());
  gtk_widget_add_css_class(w, "label");
  auto el = registerElem(ui::ElementType::LABEL, w);
  el->props["text"] = content;
  return el;
}

std::shared_ptr<ui::UIElement> GtkBackend::image(const std::string &path) {
  GtkWidget *w = gtk_picture_new_for_filename(path.c_str());
  if (!w) w = gtk_picture_new();
  auto el = registerElem(ui::ElementType::IMAGE, w);
  el->props["path"] = path;
  return el;
}

std::shared_ptr<ui::UIElement> GtkBackend::icon(const std::string &name) {
  GtkWidget *w = gtk_image_new_from_icon_name(name.c_str());
  auto el = registerElem(ui::ElementType::ICON, w);
  el->props["name"] = name;
  return el;
}

std::shared_ptr<ui::UIElement> GtkBackend::divider() {
  return registerElem(ui::ElementType::DIVIDER,
                      gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
}

std::shared_ptr<ui::UIElement> GtkBackend::spacer(int size) {
  GtkWidget *w = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_size_request(w, size, size);
  auto el = registerElem(ui::ElementType::SPACER, w);
  el->props["size"] = static_cast<int64_t>(size);
  return el;
}

std::shared_ptr<ui::UIElement> GtkBackend::progress(int value, int max) {
  GtkWidget *w = gtk_progress_bar_new();
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(w),
                                max > 0 ? double(value) / max : 0.0);
  auto el = registerElem(ui::ElementType::PROGRESS, w);
  el->props["value"] = static_cast<int64_t>(value);
  el->props["max"] = static_cast<int64_t>(max);
  return el;
}

std::shared_ptr<ui::UIElement> GtkBackend::spinner() {
  GtkWidget *w = gtk_spinner_new();
  gtk_spinner_start(GTK_SPINNER(w));
  return registerElem(ui::ElementType::SPINNER, w);
}

std::shared_ptr<ui::UIElement> GtkBackend::btn(const std::string &label) {
  GtkWidget *w = gtk_button_new_with_label(label.c_str());
  auto el = registerElem(ui::ElementType::BUTTON, w);
  el->props["label"] = label;
  return el;
}

std::shared_ptr<ui::UIElement> GtkBackend::input(const std::string &placeholder) {
  GtkWidget *w = gtk_entry_new();
  gtk_editable_set_text(GTK_EDITABLE(w), "");
  gtk_entry_set_placeholder_text(GTK_ENTRY(w), placeholder.c_str());
  auto el = registerElem(ui::ElementType::INPUT, w);
  el->props["placeholder"] = placeholder;
  return el;
}

std::shared_ptr<ui::UIElement> GtkBackend::textarea(const std::string &placeholder) {
  GtkWidget *w = gtk_text_view_new();
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(w), GTK_WRAP_WORD);
  GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w));
  gtk_text_buffer_set_text(buf, placeholder.c_str(), -1);
  auto el = registerElem(ui::ElementType::TEXTAREA, w);
  el->props["placeholder"] = placeholder;
  return el;
}

std::shared_ptr<ui::UIElement> GtkBackend::checkbox(const std::string &label, bool checked) {
  GtkWidget *w = gtk_check_button_new_with_label(label.c_str());
  gtk_check_button_set_active(GTK_CHECK_BUTTON(w), checked);
  auto el = registerElem(ui::ElementType::CHECKBOX, w);
  el->props["label"] = label;
  el->props["checked"] = checked;
  return el;
}

std::shared_ptr<ui::UIElement> GtkBackend::toggle(const std::string &label, bool value) {
  GtkWidget *sw = gtk_switch_new();
  gtk_switch_set_active(GTK_SWITCH(sw), value);

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *lbl = gtk_label_new(label.c_str());
  gtk_box_append(GTK_BOX(box), lbl);
  gtk_box_append(GTK_BOX(box), sw);

  auto el = registerElem(ui::ElementType::TOGGLE, box);
  el->props["label"] = label;
  el->props["value"] = value;
  toggleSwitches_[el->id] = sw;
  return el;
}

std::shared_ptr<ui::UIElement> GtkBackend::slider(int min, int max, int value) {
  GtkAdjustment *adj = gtk_adjustment_new(value, min, max, 1, 10, 0);
  GtkWidget *w = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adj);
  gtk_range_set_fill_level(GTK_RANGE(w), value);
  auto el = registerElem(ui::ElementType::SLIDER, w);
  el->props["min"] = static_cast<int64_t>(min);
  el->props["max"] = static_cast<int64_t>(max);
  el->props["value"] = static_cast<int64_t>(value);
  return el;
}

std::shared_ptr<ui::UIElement> GtkBackend::dropdown(const std::vector<std::string> &options) {
  GtkStringList *listModel = gtk_string_list_new(nullptr);
  std::string csv;
  for (const auto &opt : options) {
    gtk_string_list_append(listModel, opt.c_str());
    if (!csv.empty()) csv += ',';
    csv += opt;
  }
  GtkWidget *w = gtk_drop_down_new(G_LIST_MODEL(listModel), nullptr);
  g_object_unref(listModel);
  auto el = registerElem(ui::ElementType::DROPDOWN, w);
  el->props["options"] = csv;   // PropValue cannot hold vectors; store CSV
  return el;
}

std::shared_ptr<ui::UIElement> GtkBackend::row() {
  GtkWidget *w = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_add_css_class(w, "row");
  return registerElem(ui::ElementType::ROW, w);
}

std::shared_ptr<ui::UIElement> GtkBackend::col() {
  GtkWidget *w = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_add_css_class(w, "col");
  return registerElem(ui::ElementType::COL, w);
}

std::shared_ptr<ui::UIElement> GtkBackend::grid(int cols) {
  GtkWidget *w = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(w), 6);
  gtk_grid_set_row_spacing(GTK_GRID(w), 6);
  auto el = registerElem(ui::ElementType::GRID, w);
  el->props["columns"] = static_cast<int64_t>(cols);
  return el;
}

std::shared_ptr<ui::UIElement> GtkBackend::table(int rows, int cols) {
  // GTK4 has no table widget with cell geometry; use a grid as the container.
  GtkWidget *w = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(w), true);
  auto el = registerElem(ui::ElementType::TABLE, w);
  el->props["rows"] = static_cast<int64_t>(rows);
  el->props["cols"] = static_cast<int64_t>(cols);
  return el;
}

std::shared_ptr<ui::UIElement> GtkBackend::flex(const std::string &direction) {
  bool horizontal = direction != "col" && direction != "column";
  auto el = registerElem(ui::ElementType::FLEX, createBoxInternal(horizontal));
  el->props["direction"] = direction;
  return el;
}

std::shared_ptr<ui::UIElement> GtkBackend::scroll() {
  GtkWidget *w = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(w),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  return registerElem(ui::ElementType::SCROLL, w);
}

std::shared_ptr<ui::UIElement> GtkBackend::canvas(int width, int height) {
  GtkWidget *w = gtk_drawing_area_new();
  gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(w), width);
  gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(w), height);
  auto el = registerElem(ui::ElementType::CANVAS, w);
  el->props["width"] = static_cast<int64_t>(width);
  el->props["height"] = static_cast<int64_t>(height);

  struct CanvasCtx {
    GtkBackend *self;
    ui::ElementId id;
  };
  auto *ctx = new CanvasCtx{this, el->id};
  gtk_drawing_area_set_draw_func(
      GTK_DRAWING_AREA(w),
      +[](GtkDrawingArea *, cairo_t *cr, int, int, gpointer data) {
        auto *cc = static_cast<CanvasCtx *>(data);
        auto it = cc->self->elements_.find(cc->id);
        if (it == cc->self->elements_.end()) return;
        replayCanvas(cr, *it->second);
      },
      ctx, +[](gpointer data) { delete static_cast<CanvasCtx *>(data); });
  return el;
}

void GtkBackend::replayCanvas(cairo_t *cr, ui::UIElement &el) {
  double strokeR = 0, strokeG = 0, strokeB = 0, strokeA = 1;
  double fillR = 0, fillG = 0, fillB = 0, fillA = 0;   // default: transparent
  double penWidth = el.canvasStrokeWidth;

  auto parseColor = [](const std::string &c, double &r, double &g, double &b,
                       double &a) {
    a = 1.0;
    if (c == "transparent") { a = 0.0; return; }
    if (c == "black") { r = g = b = 0; return; }
    if (c == "white") { r = g = b = 1; return; }
    if (c == "red") { r = 1; g = b = 0; return; }
    if (c == "green") { r = 0; g = 1; b = 0; return; }
    if (c == "blue") { r = 0; g = 0; b = 1; return; }
    if (c.size() == 7 && c[0] == '#') {
      r = std::strtol(c.substr(1, 2).c_str(), nullptr, 16) / 255.0;
      g = std::strtol(c.substr(3, 2).c_str(), nullptr, 16) / 255.0;
      b = std::strtol(c.substr(5, 2).c_str(), nullptr, 16) / 255.0;
    }
  };
  parseColor(el.canvasStrokeColor, strokeR, strokeG, strokeB, strokeA);
  parseColor(el.canvasFillColor, fillR, fillG, fillB, fillA);

  // The canvas script api sends a pen as r/g/b, the builder api as a color
  // name. Accept both.
  auto applyStroke = [&](const ui::CanvasCmd &cmd) {
    auto color = cmd.params.find("color");
    if (color != cmd.params.end() &&
        std::holds_alternative<std::string>(color->second)) {
      parseColor(std::get<std::string>(color->second), strokeR, strokeG, strokeB,
                 strokeA);
      return;
    }
    auto r = cmd.params.find("r"), g = cmd.params.find("g"),
         b = cmd.params.find("b");
    if (r != cmd.params.end() && g != cmd.params.end() &&
        b != cmd.params.end()) {
      strokeR = std::get<int64_t>(r->second) / 255.0;
      strokeG = std::get<int64_t>(g->second) / 255.0;
      strokeB = std::get<int64_t>(b->second) / 255.0;
      strokeA = 1.0;
    }
  };
  auto applyFill = [&](const ui::CanvasCmd &cmd) {
    auto color = cmd.params.find("color");
    if (color != cmd.params.end() &&
        std::holds_alternative<std::string>(color->second)) {
      parseColor(std::get<std::string>(color->second), fillR, fillG, fillB, fillA);
      return;
    }
    auto r = cmd.params.find("r"), g = cmd.params.find("g"),
         b = cmd.params.find("b");
    if (r != cmd.params.end() && g != cmd.params.end() &&
        b != cmd.params.end()) {
      fillR = std::get<int64_t>(r->second) / 255.0;
      fillG = std::get<int64_t>(g->second) / 255.0;
      fillB = std::get<int64_t>(b->second) / 255.0;
      fillA = 1.0;
    }
  };

  for (const auto &cmd : el.canvasCommands) {
    const std::string &t = cmd.type;
    auto p = [&](const std::string &k, int dflt) { return cmd.getParam(k, dflt); };
    if (t == ui::CanvasCmdType::CLEAR) {
      cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
      cairo_paint(cr);
      cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    } else if (t == ui::CanvasCmdType::SET_COLOR ||
               t == ui::CanvasCmdType::SET_STROKE) {
      applyStroke(cmd);
    } else if (t == ui::CanvasCmdType::SET_FILL) {
      applyFill(cmd);
    } else if (t == ui::CanvasCmdType::SET_LINE_WIDTH) {
      penWidth = p("width", 1);
      cairo_set_line_width(cr, penWidth);
    } else if (t == ui::CanvasCmdType::MOVE_TO) {
      cairo_move_to(cr, p("x", 0), p("y", 0));
    } else if (t == ui::CanvasCmdType::LINE_TO) {
      cairo_line_to(cr, p("x", 0), p("y", 0));
    } else if (t == ui::CanvasCmdType::LINE) {
      cairo_move_to(cr, p("x1", 0), p("y1", 0));
      cairo_line_to(cr, p("x2", 0), p("y2", 0));
      cairo_set_source_rgba(cr, strokeR, strokeG, strokeB, strokeA);
      cairo_stroke(cr);
    } else if (t == ui::CanvasCmdType::RECT) {
      cairo_rectangle(cr, p("x", 0), p("y", 0), p("w", 0), p("h", 0));
      cairo_set_source_rgba(cr, strokeR, strokeG, strokeB, strokeA);
      cairo_stroke(cr);
    } else if (t == ui::CanvasCmdType::FILL_RECT) {
      cairo_rectangle(cr, p("x", 0), p("y", 0), p("w", 0), p("h", 0));
      cairo_set_source_rgba(cr, fillR, fillG, fillB, fillA);
      cairo_fill(cr);
    } else if (t == ui::CanvasCmdType::CIRCLE) {
      cairo_arc(cr, p("cx", 0), p("cy", 0), p("r", 0), 0, 2 * G_PI);
      cairo_set_source_rgba(cr, strokeR, strokeG, strokeB, strokeA);
      cairo_stroke(cr);
    } else if (t == ui::CanvasCmdType::FILL_CIRCLE) {
      cairo_arc(cr, p("cx", 0), p("cy", 0), p("r", 0), 0, 2 * G_PI);
      cairo_set_source_rgba(cr, fillR, fillG, fillB, fillA);
      cairo_fill(cr);
    } else if (t == ui::CanvasCmdType::TEXT) {
      auto it = cmd.params.find("text");
      std::string text =
          (it != cmd.params.end() && std::holds_alternative<std::string>(it->second))
              ? std::get<std::string>(it->second) : "";
      cairo_move_to(cr, p("x", 0), p("y", 0));
      cairo_set_source_rgba(cr, strokeR, strokeG, strokeB, strokeA);
      cairo_show_text(cr, text.c_str());
    } else if (t == ui::CanvasCmdType::IMAGE) {
      auto it = cmd.params.find("path");
      if (it != cmd.params.end() && std::holds_alternative<std::string>(it->second)) {
        cairo_surface_t *img = cairo_image_surface_create_from_png(
            std::get<std::string>(it->second).c_str());
        if (cairo_surface_status(img) == CAIRO_STATUS_SUCCESS) {
          cairo_set_source_surface(cr, img, p("x", 0), p("y", 0));
          cairo_paint(cr);
        }
        cairo_surface_destroy(img);
      }
    }
  }
}

std::shared_ptr<ui::UIElement> GtkBackend::menu(const std::string &title) {
  auto el = registerElem(ui::ElementType::MENU, nullptr);
  el->props["title"] = title;
  auto *model = g_menu_new();
  menus_[el->id] = model;
  return el;
}

std::shared_ptr<ui::UIElement> GtkBackend::menuItem(const std::string &label, const std::string &shortcut) {
  auto el = registerElem(ui::ElementType::MENU_ITEM, nullptr);
  el->props["label"] = label;
  el->props["shortcut"] = shortcut;
  return el;
}

std::shared_ptr<ui::UIElement> GtkBackend::menuSeparator() {
  return registerElem(ui::ElementType::MENU_SEP, nullptr);
}

// ---------------------------------------------------------------------------
// Realization / show / hide / close
// ---------------------------------------------------------------------------
void GtkBackend::realize(std::shared_ptr<ui::UIElement> element) {
  auto it = element ? elements_.find(element->id) : elements_.end();
  if (it != elements_.end()) it->second->realized = true;
}

std::shared_ptr<ui::UIElement> GtkBackend::resolve(ui::ElementId id) {
  auto it = elements_.find(id);
  return it != elements_.end() ? it->second : nullptr;
}

void GtkBackend::addChild(std::shared_ptr<ui::UIElement> parent,
                          std::shared_ptr<ui::UIElement> child) {
  if (!parent || !child) return;
  // Record the tree edge on the real element, then link the native widgets.
  if (!child->parent.expired()) return;   // already parented
  parent->add(child);
  GtkWidget *pw = getWidget(parent->id);
  GtkWidget *cw = getWidget(child->id);
  if (pw && cw) {
    attachChildWidget(parent->id, pw, cw);
    g_debug("gtk: attached %s into %s", g_type_name(G_OBJECT_TYPE(cw)),
            g_type_name(G_OBJECT_TYPE(pw)));
  } else {
    g_debug("gtk: no native widget for child %llu or parent %llu",
            (unsigned long long)child->id, (unsigned long long)parent->id);
  }
}

void GtkBackend::attachChildWidget(ui::ElementId parentId, GtkWidget *parent,
                                   GtkWidget *child) {
  if (!parent || !child) return;
  if (GTK_IS_WINDOW(parent)) {
    // Windows hold a single vertical box; children go inside it.
    GtkWidget *box = gtk_window_get_child(GTK_WINDOW(parent));
    if (box && GTK_IS_BOX(box)) gtk_box_append(GTK_BOX(box), child);
    return;
  }
  if (GTK_IS_BOX(parent)) {
    gtk_box_append(GTK_BOX(parent), child);
    return;
  }
  if (GTK_IS_GRID(parent)) {
    auto it = elements_.find(parentId);
    int cols = it != elements_.end() ? it->second->getProp("columns", 1) : 1;
    if (cols < 1) cols = 1;
    int slot = gridCells_[parentId]++;
    gtk_grid_attach(GTK_GRID(parent), child, slot % cols, slot / cols, 1, 1);
    return;
  }
  if (GTK_IS_SCROLLED_WINDOW(parent)) {
    GtkWidget *inner = gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(parent));
    if (inner && GTK_IS_BOX(inner)) gtk_box_append(GTK_BOX(inner), child);
    return;
  }
  g_warning("gtk: cannot attach child to widget of type %s",
            g_type_name(G_OBJECT_TYPE(parent)));
}

void GtkBackend::show(std::shared_ptr<ui::UIElement> window) {
  if (!window) return;
  GtkWidget *w = getWidget(window->id);
  if (!w) return;
  // The shim writes title/size onto the live element; apply them now.
  auto el = elements_.find(window->id);
  if (el != elements_.end()) {
    auto title = el->second->getProp("title", std::string());
    if (!title.empty() && GTK_IS_WINDOW(w)) {
      gtk_window_set_title(GTK_WINDOW(w), title.c_str());
    }
    int width = el->second->getProp("width", 0);
    int height = el->second->getProp("height", 0);
    if (GTK_IS_WINDOW(w) && (width > 0 || height > 0)) {
      gtk_window_set_default_size(GTK_WINDOW(w), width > 0 ? width : 800,
                                  height > 0 ? height : 600);
    }
    el->second->visible = true;
  }
  if (GTK_IS_WINDOW(w)) {
    gtk_window_present(GTK_WINDOW(w));
  } else {
    gtk_widget_set_visible(w, true);
  }
}

void GtkBackend::hide(std::shared_ptr<ui::UIElement> window) {
  if (!window) return;
  GtkWidget *w = getWidget(window->id);
  if (w) gtk_widget_set_visible(w, false);
  auto it = elements_.find(window->id);
  if (it != elements_.end()) it->second->visible = false;
}

void GtkBackend::close(std::shared_ptr<ui::UIElement> window) {
  if (!window) return;
  GtkWidget *w = getWidget(window->id);
  if (w && GTK_IS_WINDOW(w)) gtk_window_close(GTK_WINDOW(w));
  destroyWidget(window->id);
}

// ---------------------------------------------------------------------------
// Dialogs (GTK4 async APIs behind the synchronous interface)
// ---------------------------------------------------------------------------
void GtkBackend::alert(const std::string &message) {
  if (!app_) return;
  GtkAlertDialog *d = gtk_alert_dialog_new("%s", message.c_str());
  DialogWait wait;
  gtk_alert_dialog_choose(
      d, activeWindow(app_), nullptr,
      +[](GObject *obj, GAsyncResult *res, gpointer data) {
        auto *w = static_cast<DialogWait *>(data);
        int r = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(obj), res, nullptr);
        (void)r;
        w->done = true;
        g_main_loop_quit(w->loop);
      },
      &wait);
  if (!wait.done) g_main_loop_run(wait.loop);
  g_object_unref(d);
}

bool GtkBackend::confirm(const std::string &message) {
  if (!app_) return false;
  GtkAlertDialog *d = gtk_alert_dialog_new("%s", message.c_str());
  const char *buttons[] = {"_No", "_Yes", nullptr};
  gtk_alert_dialog_set_buttons(d, buttons);
  gtk_alert_dialog_set_default_button(d, 0);
  gtk_alert_dialog_set_cancel_button(d, 0);
  DialogWait wait;
  gtk_alert_dialog_choose(
      d, activeWindow(app_), nullptr,
      +[](GObject *obj, GAsyncResult *res, gpointer data) {
        auto *w = static_cast<DialogWait *>(data);
        int b = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(obj), res, nullptr);
        w->choice = b;
        w->done = true;
        g_main_loop_quit(w->loop);
      },
      &wait);
  if (!wait.done) g_main_loop_run(wait.loop);
  g_object_unref(d);
  return wait.choice == 1;
}

std::string GtkBackend::filePicker(const std::string &title) {
  if (!app_) return "";
  GtkFileDialog *d = gtk_file_dialog_new();
  gtk_file_dialog_set_title(d, title.c_str());
  DialogWait wait;
  gtk_file_dialog_open(
      d, activeWindow(app_), nullptr,
      +[](GObject *obj, GAsyncResult *res, gpointer data) {
        auto *w = static_cast<DialogWait *>(data);
        w->file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(obj), res, nullptr);
        w->done = true;
        g_main_loop_quit(w->loop);
      },
      &wait);
  if (!wait.done) g_main_loop_run(wait.loop);
  std::string result;
  if (wait.file) {
    char *path = g_file_get_path(wait.file);
    if (path) { result = path; g_free(path); }
    g_object_unref(wait.file);
  }
  g_object_unref(d);
  return result;
}

std::string GtkBackend::dirPicker(const std::string &title) {
  if (!app_) return "";
  GtkFileDialog *d = gtk_file_dialog_new();
  gtk_file_dialog_set_title(d, title.c_str());
  DialogWait wait;
  gtk_file_dialog_select_folder(
      d, activeWindow(app_), nullptr,
      +[](GObject *obj, GAsyncResult *res, gpointer data) {
        auto *w = static_cast<DialogWait *>(data);
        w->file = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(obj), res, nullptr);
        w->done = true;
        g_main_loop_quit(w->loop);
      },
      &wait);
  if (!wait.done) g_main_loop_run(wait.loop);
  std::string result;
  if (wait.file) {
    char *path = g_file_get_path(wait.file);
    if (path) { result = path; g_free(path); }
    g_object_unref(wait.file);
  }
  g_object_unref(d);
  return result;
}

void GtkBackend::notify(const std::string &message, const std::string &type) {
  if (!app_) return;
  GNotification *n = g_notification_new("Havel");
  g_notification_set_body(n, message.c_str());
  if (type == "error") g_notification_set_priority(n, G_NOTIFICATION_PRIORITY_URGENT);
  else if (type == "warning") g_notification_set_priority(n, G_NOTIFICATION_PRIORITY_HIGH);
  g_application_send_notification(G_APPLICATION(app_), nullptr, n);
  g_object_unref(n);
}

// ---------------------------------------------------------------------------
// Event loop
// ---------------------------------------------------------------------------
static gboolean onIdleTimer(gpointer data) {
  auto *cb = static_cast<std::function<void()> *>(data);
  if (cb && *cb) (*cb)();
  return G_SOURCE_CONTINUE;
}

void GtkBackend::pumpEvents(int timeoutMs) {
  if (timeoutMs > 0) {
    gint64 deadline = g_get_monotonic_time() + timeoutMs * 1000;
    while (g_main_context_pending(nullptr)) g_main_context_iteration(nullptr, false);
    gint64 remain = deadline - g_get_monotonic_time();
    if (remain > 0) g_usleep(static_cast<gulong>(remain));
  } else {
    while (g_main_context_pending(nullptr)) g_main_context_iteration(nullptr, false);
  }
}

int GtkBackend::runEventLoop() {
  if (!initialized_) return 1;
  loop_ = g_main_loop_new(nullptr, false);
  if (!loop_) return 1;
  if (idleCallback_) idleSourceId_ = g_timeout_add(50, onIdleTimer, &idleCallback_);
  g_main_loop_run(loop_);
  g_main_loop_unref(loop_);
  loop_ = nullptr;
  if (idleSourceId_ > 0) { g_source_remove(idleSourceId_); idleSourceId_ = 0; }
  return 0;
}

void GtkBackend::quitEventLoop(int exitCode) {
  (void)exitCode;
  if (loop_ && g_main_loop_is_running(loop_)) g_main_loop_quit(loop_);
}

void GtkBackend::setApplicationMetadata(const ApplicationMetadata &meta) {
  appMeta_ = meta;
  if (!meta.applicationName.empty()) {
    g_set_application_name(meta.applicationName.c_str());
  }
  if (!meta.organizationName.empty() && app_) {
    std::string id = meta.organizationName;
    auto pos = id.find(' ');
    while (pos != std::string::npos) { id.replace(pos, 1, "-"); pos = id.find(' '); }
    g_object_set(app_, "application-id", (id + "." + meta.applicationName).c_str(), nullptr);
  }
}

void GtkBackend::resetPerRunState() {
  onAllWindowsClosedCallback_ = nullptr;
}

void GtkBackend::setIdleCallback(std::function<void()> cb) {
  idleCallback_ = std::move(cb);
}

bool GtkBackend::hasActiveWindows() const {
  if (!app_) return false;
  return gtk_application_get_windows(app_) != nullptr;
}

void GtkBackend::onAllWindowsClosed(std::function<void()> callback) {
  onAllWindowsClosedCallback_ = std::move(callback);
}

// ---------------------------------------------------------------------------
// Values
// ---------------------------------------------------------------------------
std::string GtkBackend::getValue(std::shared_ptr<ui::UIElement> element) {
  if (!element) return "";
  auto sw = toggleSwitches_.find(element->id);
  if (sw != toggleSwitches_.end()) {
    return gtk_switch_get_active(GTK_SWITCH(sw->second)) ? "true" : "false";
  }
  GtkWidget *w = getWidget(element->id);
  if (!w) return "";
  if (GTK_IS_ENTRY(w)) return gtk_editable_get_text(GTK_EDITABLE(w));
  if (GTK_IS_TEXT_VIEW(w)) {
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buf, &start, &end);
    char *text = gtk_text_buffer_get_text(buf, &start, &end, false);
    std::string out = text ? text : "";
    g_free(text);
    return out;
  }
  if (GTK_IS_CHECK_BUTTON(w))
    return gtk_check_button_get_active(GTK_CHECK_BUTTON(w)) ? "true" : "false";
  if (GTK_IS_SCALE(w)) {
    double v = gtk_range_get_value(GTK_RANGE(w));
    return std::to_string(static_cast<long long>(v));
  }
  if (GTK_IS_DROP_DOWN(w)) {
    guint sel = gtk_drop_down_get_selected(GTK_DROP_DOWN(w));
    return sel == GTK_INVALID_LIST_POSITION ? "" : std::to_string(sel);
  }
  return "";
}

void GtkBackend::setValue(std::shared_ptr<ui::UIElement> element, const std::string &value) {
  if (!element) return;
  auto sw = toggleSwitches_.find(element->id);
  if (sw != toggleSwitches_.end()) {
    gtk_switch_set_active(GTK_SWITCH(sw->second), value == "true" || value == "1");
    return;
  }
  GtkWidget *w = getWidget(element->id);
  if (!w) return;
  if (GTK_IS_ENTRY(w)) {
    gtk_editable_set_text(GTK_EDITABLE(w), value.c_str());
  } else if (GTK_IS_TEXT_VIEW(w)) {
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w));
    gtk_text_buffer_set_text(buf, value.c_str(), -1);
  } else if (GTK_IS_LABEL(w)) {
    gtk_label_set_text(GTK_LABEL(w), value.c_str());
  } else if (GTK_IS_PROGRESS_BAR(w)) {
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(w), g_strtod(value.c_str(), nullptr));
  } else if (GTK_IS_SCALE(w)) {
    gtk_range_set_value(GTK_RANGE(w), g_strtod(value.c_str(), nullptr));
  } else if (GTK_IS_SPIN_BUTTON(w)) {
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(w), g_strtod(value.c_str(), nullptr));
  } else if (GTK_IS_CHECK_BUTTON(w)) {
    gtk_check_button_set_active(GTK_CHECK_BUTTON(w), value == "true" || value == "1");
  } else if (GTK_IS_DROP_DOWN(w)) {
    char *end = nullptr;
    long idx = std::strtol(value.c_str(), &end, 10);
    if (end && *end == '\0') gtk_drop_down_set_selected(GTK_DROP_DOWN(w), static_cast<guint>(idx));
  }
}

// ---------------------------------------------------------------------------
// System tray: GTK4 has no built-in tray (needs libayatana-appindicator,
// which is not a build dependency). Store the requested state and report
// honestly via trayIsVisible().
// ---------------------------------------------------------------------------
void GtkBackend::trayIcon(const std::string &iconPath, const std::string &tooltip) {
  (void)iconPath; (void)tooltip;
  trayVisible_ = false;  // not shown: no tray support in GTK4
}

void GtkBackend::trayMenu(std::shared_ptr<ui::UIElement> menu) { (void)menu; }

void GtkBackend::trayNotify(const std::string &title, const std::string &message,
                            const std::string &iconType) {
  // Real desktop notification is observable behaviour; tray icon itself is not.
  (void)title;
  notify(message, iconType);
}

void GtkBackend::trayShow() { trayVisible_ = false; }
void GtkBackend::trayHide() { trayVisible_ = false; }
bool GtkBackend::trayIsVisible() const { return false; }

// ---------------------------------------------------------------------------
// Styling
// ---------------------------------------------------------------------------
void GtkBackend::applyStyle(std::shared_ptr<ui::UIElement> element,
                            const std::string &key, const ui::PropValue &value) {
  if (!element) return;
  GtkWidget *w = getWidget(element->id);
  if (!w) return;
  std::visit(
      [&](auto &&arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::string>) {
          if (key == "css-class") gtk_widget_add_css_class(w, arg.c_str());
        } else if constexpr (std::is_same_v<T, int64_t>) {
          if (key == "width") gtk_widget_set_size_request(w, static_cast<int>(arg), -1);
          else if (key == "height") gtk_widget_set_size_request(w, -1, static_cast<int>(arg));
        }
      },
      value);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
GtkWidget* GtkBackend::createWindowInternal(const std::string &title, bool modal) {
  if (!app_) return nullptr;
  GtkWidget *win = gtk_application_window_new(app_);
  gtk_window_set_title(GTK_WINDOW(win), title.c_str());
  gtk_window_set_default_size(GTK_WINDOW(win), 800, 600);
  if (modal) gtk_window_set_modal(GTK_WINDOW(win), true);
  // A window holds exactly one child; keep an inner box so scripts can add
  // any number of children to it.
  gtk_window_set_child(GTK_WINDOW(win), createBoxInternal(false));
  g_signal_connect(win, "destroy", G_CALLBACK(onWindowClosed), this);
  return win;
}

GtkWidget* GtkBackend::createBoxInternal(bool horizontal) {
  return gtk_box_new(horizontal ? GTK_ORIENTATION_HORIZONTAL
                                : GTK_ORIENTATION_VERTICAL, 6);
}

void GtkBackend::setupSignalHandlers() {
  g_signal_connect(app_, "activate", G_CALLBACK(+[](GtkApplication *, gpointer) {}), this);
}

void GtkBackend::onWindowClosed(GtkWindow *, void *userData) {
  auto *backend = static_cast<GtkBackend *>(userData);
  if (!backend->hasActiveWindows() && backend->onAllWindowsClosedCallback_)
    backend->onAllWindowsClosedCallback_();
}

// ---------------------------------------------------------------------------
// Canvas
// ---------------------------------------------------------------------------
void GtkBackend::canvasFlush(std::shared_ptr<ui::UIElement> canvas) {
  if (!canvas) return;
  GtkWidget *w = getWidget(canvas->id);
  if (w && GTK_IS_WIDGET(w)) {
    g_debug("gtk: canvas %llu redraw with %zu commands",
            (unsigned long long)canvas->id, canvas->canvasCommands.size());
    gtk_widget_queue_draw(w);
  }
}

void GtkBackend::canvasClear(std::shared_ptr<ui::UIElement> canvas) {
  if (!canvas) return;
  auto it = elements_.find(canvas->id);
  if (it != elements_.end()) {
    it->second->canvasCommands.clear();
    it->second->canvasCommands.push_back({ui::CanvasCmdType::CLEAR, {}});
  }
  canvasFlush(std::move(canvas));
}

void GtkBackend::canvasDrawLine(std::shared_ptr<ui::UIElement> canvas, int x1,
                                int y1, int x2, int y2) {
  if (!canvas) return;
  auto it = elements_.find(canvas->id);
  if (it == elements_.end()) return;
  it->second->canvasCommands.push_back(
      {ui::CanvasCmdType::LINE,
       {{"x1", static_cast<int64_t>(x1)},
        {"y1", static_cast<int64_t>(y1)},
        {"x2", static_cast<int64_t>(x2)},
        {"y2", static_cast<int64_t>(y2)}}});
  canvasFlush(std::move(canvas));
}

void GtkBackend::canvasDrawRect(std::shared_ptr<ui::UIElement> canvas, int x,
                                int y, int w, int h) {
  if (!canvas) return;
  auto it = elements_.find(canvas->id);
  if (it == elements_.end()) return;
  it->second->canvasCommands.push_back(
      {ui::CanvasCmdType::RECT,
       {{"x", static_cast<int64_t>(x)},
        {"y", static_cast<int64_t>(y)},
        {"w", static_cast<int64_t>(w)},
        {"h", static_cast<int64_t>(h)}}});
  canvasFlush(std::move(canvas));
}

void GtkBackend::canvasDrawCircle(std::shared_ptr<ui::UIElement> canvas, int cx,
                                  int cy, int r) {
  if (!canvas) return;
  auto it = elements_.find(canvas->id);
  if (it == elements_.end()) return;
  it->second->canvasCommands.push_back(
      {ui::CanvasCmdType::CIRCLE,
       {{"cx", static_cast<int64_t>(cx)},
        {"cy", static_cast<int64_t>(cy)},
        {"r", static_cast<int64_t>(r)}}});
  canvasFlush(std::move(canvas));
}

void GtkBackend::canvasSetPen(std::shared_ptr<ui::UIElement> canvas, int r, int g,
                              int b, int width) {
  if (!canvas) return;
  auto it = elements_.find(canvas->id);
  if (it == elements_.end()) return;
  it->second->canvasStrokeWidth = width;
  it->second->canvasCommands.push_back(
      {ui::CanvasCmdType::SET_STROKE,
       {{"r", static_cast<int64_t>(r)},
        {"g", static_cast<int64_t>(g)},
        {"b", static_cast<int64_t>(b)},
        {"width", static_cast<int64_t>(width)}}});
}

void GtkBackend::canvasFill(std::shared_ptr<ui::UIElement> canvas, int x, int y) {
  if (!canvas) return;
  auto it = elements_.find(canvas->id);
  if (it == elements_.end()) return;
  it->second->canvasCommands.push_back(
      {ui::CanvasCmdType::FILL_RECT,
       {{"x", static_cast<int64_t>(x)}, {"y", static_cast<int64_t>(y)}}});
  canvasFlush(std::move(canvas));
}

void GtkBackend::canvasBeginStroke(std::shared_ptr<ui::UIElement> canvas) {
  if (!canvas) return;
  auto it = elements_.find(canvas->id);
  if (it == elements_.end()) return;
  it->second->canvasCommands.push_back({ui::CanvasCmdType::SET_STROKE, {}});
}

void GtkBackend::canvasEndStroke(std::shared_ptr<ui::UIElement> canvas) {
  if (!canvas) return;
  auto it = elements_.find(canvas->id);
  if (it == elements_.end()) return;
  it->second->canvasCommands.push_back({ui::CanvasCmdType::SET_FILL, {}});
  canvasFlush(std::move(canvas));
}

} // namespace havel::host

#endif // HAVE_GTK_BACKEND
