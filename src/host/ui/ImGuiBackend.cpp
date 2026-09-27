/*
 * ImGuiBackend.cpp - Dear ImGui implementation of UIBackend
 *
 * Dear ImGui is immediate mode: no widgets are created, the element tree is
 * walked and re-submitted every frame. The FFI shim only carries element ids,
 * so elements are registered by ElementId and every operation resolves through
 * that registry.
 */
#include "ImGuiBackend.hpp"

#ifdef HAVE_IMGUI_BACKEND

#include "modules/ui/UIElement.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

extern "C" {
#include <glib.h>
}

// ImVec2 operator* is only available with the math operators enabled
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

namespace havel::host {

namespace {
// GL 3.0 core, the oldest version the bundled ImGui backend supports.
constexpr const char *kGlslVersion = "#version 130";
constexpr int kToastSeconds = 4;
// ImGui writes straight into the buffer, so every editable string is sized.
constexpr size_t kTextBufferSize = 4096;
} // namespace

ImGuiBackend::ImGuiBackend() = default;

ImGuiBackend::~ImGuiBackend() {
  if (initialized_) shutdown();
}

bool ImGuiBackend::isAvailable() const {
  // Never tear down a live GLFW session here: the probe window would destroy
  // the very context the backend is about to use.
  if (window_ || context_) return true;
  if (!glfwInit()) {
    reportGlfwError("isAvailable: glfwInit");
    return false;
  }
  glfwTerminate();
  return true;
}

bool ImGuiBackend::initialize() {
  if (initialized_) return true;
  if (!initGLFW()) return false;
  if (!initImGui()) {
    g_warning("imgui: context init failed");
    shutdownImGui();
    shutdownGLFW();
    return false;
  }
  initialized_ = true;
  running_ = true;
  g_message("imgui: backend initialized (ImGui %s, renderer %s)", IMGUI_VERSION,
            reinterpret_cast<const char *>(glGetString(GL_RENDERER)));
  return true;
}

void ImGuiBackend::shutdown() {
  if (!initialized_) return;
  running_ = false;
  shutdownImGui();
  shutdownGLFW();
  elements_.clear();
  elementValues_.clear();
  elementOpen_.clear();
  windowStack_.clear();
  creationOrder_.clear();
  intValues_.clear();
  floatValues_.clear();
  boolValues_.clear();
  textValues_.clear();
  comboSelections_.clear();
  dropdownOptions_.clear();
  toasts_.clear();
  pendingDialog_.reset();
  initialized_ = false;
  g_message("imgui: backend shutdown complete after %llu frames",
            static_cast<unsigned long long>(frameCount_));
  frameCount_ = 0;
}

bool ImGuiBackend::initGLFW() {
  if (!glfwInit()) {
    reportGlfwError("initGLFW: glfwInit");
    return false;
  }

  // A core profile is only legal from OpenGL 3.2 onwards, and the bundled
  // renderer needs 3.3 for the GLSL 130 shaders it compiles.
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  window_ = glfwCreateWindow(windowWidth_, windowHeight_, windowTitle_.c_str(),
                             nullptr, nullptr);
  if (!window_) {
    reportGlfwError("initGLFW: glfwCreateWindow");
    return false;
  }

  glfwMakeContextCurrent(window_);
  glfwSwapInterval(1);
  return true;
}

bool ImGuiBackend::initImGui() {
  IMGUI_CHECKVERSION();
  context_ = ImGui::CreateContext();
  if (!context_) return false;

  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  // Multi-viewport rendering needs a platform callback this backend does not
  // install, so keep every window on the main GLFW window.
  io.IniFilename = nullptr;   // no imgui.ini written next to the binary
  io.LogFilename = nullptr;

  ImGui::StyleColorsDark();
  if (!ImGui_ImplGlfw_InitForOpenGL(window_, true)) {
    g_warning("imgui: ImGui_ImplGlfw_InitForOpenGL failed");
    return false;
  }
  if (!ImGui_ImplOpenGL3_Init(kGlslVersion)) {
    g_warning("imgui: ImGui_ImplOpenGL3_Init failed");
    return false;
  }
  return true;
}

void ImGuiBackend::reportGlfwError(const char *where) const {
  const char *description = nullptr;
  const int code = glfwGetError(&description);
  g_warning("imgui: %s failed: GLFW error 0x%05x (%s)", where, code,
            description ? description : "no description");
}

void ImGuiBackend::shutdownGLFW() {
  if (window_) {
    glfwDestroyWindow(window_);
    window_ = nullptr;
  }
  glfwTerminate();
}

void ImGuiBackend::shutdownImGui() {
  if (!context_) return;
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  context_ = nullptr;
}

// ---------------------------------------------------------------------------
// Element registry
// ---------------------------------------------------------------------------
std::shared_ptr<ui::UIElement> ImGuiBackend::createElem(const char *type) {
  auto el = std::make_shared<ui::UIElement>(type);
  el->id = nextId_++;
  el->realized = true;
  elements_[el->id] = el;
  creationOrder_.push_back(el->id);
  return el;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::resolve(ui::ElementId id) {
  auto it = elements_.find(id);
  return it != elements_.end() ? it->second : nullptr;
}

std::string ImGuiBackend::imguiId(ui::ElementId id, const char *suffix) {
  std::string label = "##havel";
  label += std::to_string(id);
  if (suffix && suffix[0]) {
    label += '_';
    label += suffix;
  }
  return label;
}

void ImGuiBackend::dropElement(ui::ElementId id) {
  elements_.erase(id);
  elementValues_.erase(id);
  elementOpen_.erase(id);
  intValues_.erase(id);
  floatValues_.erase(id);
  boolValues_.erase(id);
  textValues_.erase(id);
  comboSelections_.erase(id);
  dropdownOptions_.erase(id);
  auto eraseFrom = [id](std::vector<ui::ElementId> &vec) {
    vec.erase(std::remove(vec.begin(), vec.end(), id), vec.end());
  };
  eraseFrom(windowStack_);
  eraseFrom(creationOrder_);
}

// ---------------------------------------------------------------------------
// Element creation
// ---------------------------------------------------------------------------
std::shared_ptr<ui::UIElement> ImGuiBackend::window(const std::string &title) {
  auto el = createElem(ui::ElementType::WINDOW);
  el->props["title"] = title;
  el->props["width"] = static_cast<int64_t>(windowWidth_);
  el->props["height"] = static_cast<int64_t>(windowHeight_);
  elementOpen_[el->id] = true;
  windowStack_.push_back(el->id);
  return el;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::panel(const std::string &side) {
  auto el = createElem(ui::ElementType::PANEL);
  el->props["side"] = side;
  return el;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::modal(const std::string &title) {
  auto el = createElem(ui::ElementType::MODAL);
  el->props["title"] = title;
  return el;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::text(const std::string &content) {
  auto el = createElem(ui::ElementType::TEXT);
  el->props["text"] = content;
  return el;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::label(const std::string &content) {
  auto el = createElem(ui::ElementType::LABEL);
  el->props["text"] = content;
  return el;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::image(const std::string &path) {
  auto el = createElem(ui::ElementType::IMAGE);
  el->props["path"] = path;
  return el;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::icon(const std::string &name) {
  auto el = createElem(ui::ElementType::ICON);
  el->props["name"] = name;
  return el;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::divider() {
  return createElem(ui::ElementType::DIVIDER);
}

std::shared_ptr<ui::UIElement> ImGuiBackend::spacer(int size) {
  auto el = createElem(ui::ElementType::SPACER);
  el->props["size"] = static_cast<int64_t>(size);
  return el;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::progress(int value, int max) {
  auto el = createElem(ui::ElementType::PROGRESS);
  el->props["value"] = static_cast<int64_t>(value);
  el->props["max"] = static_cast<int64_t>(max);
  floatValues_[el->id] = max > 0 ? static_cast<float>(value) / max : 0.0f;
  return el;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::spinner() {
  return createElem(ui::ElementType::SPINNER);
}

std::shared_ptr<ui::UIElement> ImGuiBackend::btn(const std::string &label) {
  auto el = createElem(ui::ElementType::BUTTON);
  el->props["label"] = label;
  return el;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::input(const std::string &placeholder) {
  auto el = createElem(ui::ElementType::INPUT);
  el->props["placeholder"] = placeholder;
  textValues_[el->id] = "";
  return el;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::textarea(const std::string &placeholder) {
  auto el = createElem(ui::ElementType::TEXTAREA);
  el->props["placeholder"] = placeholder;
  el->props["height"] = static_cast<int64_t>(96);
  textValues_[el->id] = "";
  return el;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::checkbox(const std::string &label,
                                                     bool checked) {
  auto el = createElem(ui::ElementType::CHECKBOX);
  el->props["label"] = label;
  boolValues_[el->id] = checked;
  return el;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::toggle(const std::string &label,
                                                    bool value) {
  auto el = createElem(ui::ElementType::TOGGLE);
  el->props["label"] = label;
  boolValues_[el->id] = value;
  return el;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::slider(int min, int max, int value) {
  auto el = createElem(ui::ElementType::SLIDER);
  el->props["min"] = static_cast<int64_t>(min);
  el->props["max"] = static_cast<int64_t>(max);
  el->props["value"] = static_cast<int64_t>(value);
  intValues_[el->id] = value;
  return el;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::dropdown(
    const std::vector<std::string> &options) {
  auto el = createElem(ui::ElementType::DROPDOWN);
  el->props["options"] = options.size();   // PropValue holds no vectors
  dropdownOptions_[el->id] = options;
  comboSelections_[el->id] = 0;
  return el;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::row() {
  return createElem(ui::ElementType::ROW);
}

std::shared_ptr<ui::UIElement> ImGuiBackend::col() {
  return createElem(ui::ElementType::COL);
}

std::shared_ptr<ui::UIElement> ImGuiBackend::grid(int cols) {
  auto el = createElem(ui::ElementType::GRID);
  el->props["columns"] = static_cast<int64_t>(cols);
  return el;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::table(int rows, int cols) {
  auto el = createElem(ui::ElementType::TABLE);
  el->props["rows"] = static_cast<int64_t>(rows);
  el->props["cols"] = static_cast<int64_t>(cols);
  return el;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::flex(const std::string &direction) {
  auto el = createElem(ui::ElementType::FLEX);
  el->props["direction"] = direction;
  return el;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::scroll() {
  auto el = createElem(ui::ElementType::SCROLL);
  el->props["height"] = static_cast<int64_t>(240);
  return el;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::canvas(int width, int height) {
  auto el = createElem(ui::ElementType::CANVAS);
  el->props["width"] = static_cast<int64_t>(width);
  el->props["height"] = static_cast<int64_t>(height);
  return el;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::menu(const std::string &title) {
  auto el = createElem(ui::ElementType::MENU);
  el->props["title"] = title;
  return el;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::menuItem(const std::string &label,
                                                      const std::string &shortcut) {
  auto el = createElem(ui::ElementType::MENU_ITEM);
  el->props["label"] = label;
  el->props["shortcut"] = shortcut;
  return el;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::menuSeparator() {
  return createElem(ui::ElementType::MENU_SEP);
}

// ---------------------------------------------------------------------------
// Parenting and visibility
// ---------------------------------------------------------------------------
void ImGuiBackend::realize(std::shared_ptr<ui::UIElement> element) {
  auto el = resolve(element ? element->id : 0);
  if (el) el->realized = true;
}

void ImGuiBackend::addChild(std::shared_ptr<ui::UIElement> parent,
                            std::shared_ptr<ui::UIElement> child) {
  if (!parent || !child) return;
  if (!child->parent.expired()) return;   // already parented
  auto pel = resolve(parent->id);
  auto cel = resolve(child->id);
  if (!pel || !cel) {
    g_warning("imgui: cannot parent unknown element %llu",
              (unsigned long long)child->id);
    return;
  }
  pel->add(cel);
}

void ImGuiBackend::show(std::shared_ptr<ui::UIElement> window) {
  auto el = resolve(window ? window->id : 0);
  if (!el) return;
  elementOpen_[el->id] = true;
  el->visible = true;
  if (el->type == ui::ElementType::WINDOW &&
      std::find(windowStack_.begin(), windowStack_.end(), el->id) ==
          windowStack_.end()) {
    windowStack_.push_back(el->id);
  }
}

void ImGuiBackend::hide(std::shared_ptr<ui::UIElement> window) {
  auto el = resolve(window ? window->id : 0);
  if (!el) return;
  elementOpen_[el->id] = false;
  el->visible = false;
}

void ImGuiBackend::close(std::shared_ptr<ui::UIElement> window) {
  auto el = resolve(window ? window->id : 0);
  if (!el) return;
  auto children = el->children;
  for (auto &child : children) close(child);
  const bool wasOpen = elementOpen_.count(el->id) && elementOpen_[el->id];
  g_message("imgui: closing element %llu after %llu frames",
            static_cast<unsigned long long>(el->id),
            static_cast<unsigned long long>(frameCount_));
  dropElement(el->id);
  if (wasOpen && windowStack_.empty() && onAllWindowsClosedCallback_) {
    onAllWindowsClosedCallback_();
  }
}

// ---------------------------------------------------------------------------
// Dialogs
// ---------------------------------------------------------------------------
void ImGuiBackend::alert(const std::string &message) {
  auto dialog = std::make_unique<PendingDialog>();
  dialog->kind = PendingDialog::Kind::Alert;
  dialog->message = message;
  pendingDialog_ = std::move(dialog);
  pumpUntilAnswered([this] { return pendingDialog_ && pendingDialog_->done; }, 0);
}

bool ImGuiBackend::confirm(const std::string &message) {
  auto dialog = std::make_unique<PendingDialog>();
  dialog->kind = PendingDialog::Kind::Confirm;
  dialog->message = message;
  pendingDialog_ = std::move(dialog);
  pumpUntilAnswered([this] { return pendingDialog_ && pendingDialog_->done; }, 0);
  bool answer = pendingDialog_ && pendingDialog_->answer;
  pendingDialog_.reset();
  return answer;
}

std::string ImGuiBackend::filePicker(const std::string &title) {
  if (!initialized_) return "";
  auto dialog = std::make_unique<PendingDialog>();
  dialog->kind = PendingDialog::Kind::File;
  dialog->title = title;
  dialog->dir = std::filesystem::current_path();
  fileDialogBuffer_ = dialog->dir.string();
  pendingDialog_ = std::move(dialog);
  pumpUntilAnswered([this] { return pendingDialog_ && pendingDialog_->done; }, 0);
  std::string path = pendingDialog_ ? pendingDialog_->path : std::string();
  pendingDialog_.reset();
  return path;
}

std::string ImGuiBackend::dirPicker(const std::string &title) {
  if (!initialized_) return "";
  auto dialog = std::make_unique<PendingDialog>();
  dialog->kind = PendingDialog::Kind::Dir;
  dialog->title = title;
  dialog->dir = std::filesystem::current_path();
  fileDialogBuffer_ = dialog->dir.string();
  pendingDialog_ = std::move(dialog);
  pumpUntilAnswered([this] { return pendingDialog_ && pendingDialog_->done; }, 0);
  std::string path = pendingDialog_ ? pendingDialog_->path : std::string();
  pendingDialog_.reset();
  return path;
}

void ImGuiBackend::pumpUntilAnswered(const std::function<bool()> &done,
                                     int timeoutMs) {
  if (!window_) return;
  const double deadline =
      timeoutMs > 0 ? now_ + timeoutMs / 1000.0 : 0.0;   // 0 means wait forever
  while (running_ && !(done && done())) {
    if (deadline > 0.0 && now_ >= deadline) break;
    renderFrame();
  }
}

void ImGuiBackend::drawPendingDialog() {
  if (!pendingDialog_) return;
  switch (pendingDialog_->kind) {
  case PendingDialog::Kind::Alert:
  case PendingDialog::Kind::Confirm: {
    const bool confirm = pendingDialog_->kind == PendingDialog::Kind::Confirm;
    ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f, ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiCond_Always);
    bool open = true;
    if (ImGui::Begin("##havel_dialog", &open,
                     ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoCollapse)) {
      ImGui::TextUnformatted(pendingDialog_->message.c_str());
      ImGui::Separator();
      if (confirm) {
        if (ImGui::Button("Yes")) {
          pendingDialog_->answer = true;
          pendingDialog_->done = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("No")) {
          pendingDialog_->answer = false;
          pendingDialog_->done = true;
        }
      } else {
        if (ImGui::Button("OK")) pendingDialog_->done = true;
      }
      ImGui::End();
    } else {
      // Closed with the window button: treat as dismissal.
      if (confirm) pendingDialog_->answer = false;
      pendingDialog_->done = true;
    }
    return;
  }
  case PendingDialog::Kind::File:
  case PendingDialog::Kind::Dir:
    drawFileBrowser(pendingDialog_->kind == PendingDialog::Kind::Dir);
    return;
  }
}

bool ImGuiBackend::drawFileBrowser(bool directoriesOnly) {
  bool accepted = false;
  const bool directories = directoriesOnly;
  ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f, ImGuiCond_Always,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_Always);
  if (!ImGui::Begin("##havel_file_browser", nullptr,
                    ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::End();
    return accepted;
  }

  std::error_code ec;
  auto dir = pendingDialog_->dir;
  auto parent = dir.parent_path();
  if (!parent.empty() && parent != dir &&
      ImGui::Button("..")) {
    pendingDialog_->dir = parent;
    ImGui::End();
    return accepted;
  }

  if (fileDialogBuffer_.empty()) fileDialogBuffer_ = dir.string();
  fileDialogBuffer_.resize(kTextBufferSize, '\0');
  ImGui::SetNextItemWidth(-1.0f);
  if (ImGui::InputText("##havel_path", fileDialogBuffer_.data(),
                       kTextBufferSize)) {
    std::filesystem::path typed(fileDialogBuffer_.c_str());
    if (std::filesystem::is_directory(typed, ec)) pendingDialog_->dir = typed;
  }

  if (ImGui::BeginChild("##havel_entries", ImVec2(0, -60), true)) {
    std::vector<std::filesystem::path> entries;
    for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
      if (ec) break;
      if (entry.is_directory() || !directories) entries.push_back(entry.path());
    }
    std::sort(entries.begin(), entries.end());
    for (const auto &entry : entries) {
      std::string name = entry.filename().string();
      if (std::filesystem::is_directory(entry, ec)) name = "[ " + name + " ]";
      if (ImGui::Selectable(name.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
        if (std::filesystem::is_directory(entry, ec)) {
          pendingDialog_->dir = entry;
          fileDialogBuffer_ = entry.string();
        } else {
          pendingDialog_->path = entry.string();
          accepted = true;
        }
      }
    }
    if (ec) ImGui::TextDisabled("cannot read directory");
  }
  ImGui::EndChild();

  ImGui::Separator();
  if (directories) {
    // Selecting a directory means selecting the one currently shown.
    if (ImGui::Button("Select this directory")) {
      pendingDialog_->path = pendingDialog_->dir.string();
      accepted = true;
    }
  } else if (ImGui::Button("Select")) {
    std::filesystem::path typed(fileDialogBuffer_.c_str());
    std::error_code exists;
    if (std::filesystem::exists(typed, exists)) {
      pendingDialog_->path = typed.string();
      accepted = true;
    }
  }
  ImGui::SameLine();
  bool cancelled = ImGui::Button("Cancel");
  ImGui::End();

  if (ImGui::IsKeyPressed(ImGuiKey_Escape)) cancelled = true;
  if (cancelled) {
    pendingDialog_->path.clear();
    pendingDialog_->done = true;
    return false;
  }
  if (accepted) {
    pendingDialog_->done = true;
    return true;
  }
  return false;
}

void ImGuiBackend::notify(const std::string &message, const std::string &type) {
  // No desktop notification channel here: show it in-app for a few seconds.
  toasts_.push_back({message, type, now_ + kToastSeconds});
  while (toasts_.size() > 4) toasts_.pop_front();
}

// ---------------------------------------------------------------------------
// Event loop
// ---------------------------------------------------------------------------
void ImGuiBackend::pumpEvents(int timeoutMs) {
  if (!window_ || !running_) return;
  if (timeoutMs > 0) {
    glfwWaitEventsTimeout(timeoutMs / 1000.0);
  } else {
    glfwPollEvents();
  }
  renderFrame();
}

void ImGuiBackend::renderFrame() {
  if (!window_) return;
  now_ = ImGui::GetTime();

  glfwPollEvents();
  if (glfwWindowShouldClose(window_)) {
    running_ = false;
    if (onAllWindowsClosedCallback_) onAllWindowsClosedCallback_();
    return;
  }

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  // ImGui needs a window context for every widget, so elements that were never
  // parented into a window are submitted inside one shared host window.
  std::vector<ui::ElementId> looseRoots;
  for (ui::ElementId id : creationOrder_) {
    auto it = elements_.find(id);
    if (it == elements_.end()) continue;
    auto &el = it->second;
    if (!el->parent.expired()) continue;   // drawn by its parent
    if (el->type == ui::ElementType::WINDOW || el->type == ui::ElementType::MODAL) {
      drawElement(el);
      continue;
    }
    looseRoots.push_back(id);
  }
  if (!looseRoots.empty()) {
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
    ImGui::Begin("##havel_root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoNav);
    for (ui::ElementId id : looseRoots) {
      auto it = elements_.find(id);
      if (it != elements_.end()) drawElement(it->second);
    }
    ImGui::End();
  }

  drawPendingDialog();
  drawToasts();

  ImGui::Render();
  int fbWidth = 0, fbHeight = 0;
  glfwGetFramebufferSize(window_, &fbWidth, &fbHeight);
  glViewport(0, 0, fbWidth, fbHeight);
  glClearColor(0.07f, 0.07f, 0.08f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  glfwSwapBuffers(window_);
  ++frameCount_;
}

int ImGuiBackend::runEventLoop() {
  if (!window_ || !initialized_) return 1;
  running_ = true;
  exitCode_ = 0;
  while (running_ && !glfwWindowShouldClose(window_)) {
    renderFrame();
    if (idleCallback_) idleCallback_();
  }
  running_ = false;
  return exitCode_;
}

void ImGuiBackend::quitEventLoop(int exitCode) {
  exitCode_ = exitCode;
  running_ = false;
}

void ImGuiBackend::setApplicationMetadata(const ApplicationMetadata &meta) {
  appMeta_ = meta;
  if (!meta.applicationName.empty()) {
    windowTitle_ = meta.applicationName;
    if (window_) glfwSetWindowTitle(window_, windowTitle_.c_str());
  }
}

void ImGuiBackend::resetPerRunState() {
  elements_.clear();
  elementValues_.clear();
  elementOpen_.clear();
  windowStack_.clear();
  creationOrder_.clear();
  intValues_.clear();
  floatValues_.clear();
  boolValues_.clear();
  textValues_.clear();
  comboSelections_.clear();
  dropdownOptions_.clear();
  toasts_.clear();
  onAllWindowsClosedCallback_ = nullptr;
}

void ImGuiBackend::setIdleCallback(std::function<void()> cb) {
  idleCallback_ = std::move(cb);
}

bool ImGuiBackend::hasActiveWindows() const {
  for (ui::ElementId id : windowStack_) {
    auto it = elementOpen_.find(id);
    if (it != elementOpen_.end() && it->second) return true;
  }
  return false;
}

void ImGuiBackend::onAllWindowsClosed(std::function<void()> callback) {
  onAllWindowsClosedCallback_ = std::move(callback);
}

// ---------------------------------------------------------------------------
// Values
// ---------------------------------------------------------------------------
std::string ImGuiBackend::getValue(std::shared_ptr<ui::UIElement> element) {
  auto el = resolve(element ? element->id : 0);
  if (!el) return "";
  if (el->type == ui::ElementType::DROPDOWN) {
    auto opts = dropdownOptions_.find(el->id);
    auto sel = comboSelections_.find(el->id);
    if (opts != dropdownOptions_.end() && sel != comboSelections_.end() &&
        sel->second >= 0 &&
        sel->second < static_cast<int>(opts->second.size())) {
      return opts->second[sel->second];
    }
    return "";
  }
  auto text = textValues_.find(el->id);
  if (text != textValues_.end()) return text->second;
  auto integer = intValues_.find(el->id);
  if (integer != intValues_.end()) return std::to_string(integer->second);
  auto flag = boolValues_.find(el->id);
  if (flag != boolValues_.end()) return flag->second ? "true" : "false";
  auto real = floatValues_.find(el->id);
  if (real != floatValues_.end()) return std::to_string(real->second);
  return "";
}

void ImGuiBackend::setValue(std::shared_ptr<ui::UIElement> element,
                            const std::string &value) {
  auto el = resolve(element ? element->id : 0);
  if (!el) return;
  elementValues_[el->id] = value;
  if (el->type == ui::ElementType::INPUT ||
      el->type == ui::ElementType::TEXTAREA ||
      el->type == ui::ElementType::LABEL ||
      el->type == ui::ElementType::TEXT) {
    textValues_[el->id] = value;
    return;
  }
  if (el->type == ui::ElementType::CHECKBOX ||
      el->type == ui::ElementType::TOGGLE) {
    boolValues_[el->id] = value == "true" || value == "1";
    return;
  }
  if (el->type == ui::ElementType::DROPDOWN) {
    auto opts = dropdownOptions_.find(el->id);
    if (opts != dropdownOptions_.end()) {
      for (size_t i = 0; i < opts->second.size(); ++i) {
        if (opts->second[i] == value) {
          comboSelections_[el->id] = static_cast<int>(i);
          return;
        }
      }
    }
    return;
  }
  if (el->type == ui::ElementType::PROGRESS) {
    floatValues_[el->id] = std::strtof(value.c_str(), nullptr);
    return;
  }
  // Numeric widgets (slider, spacer) and everything else.
  intValues_[el->id] = static_cast<int>(std::strtol(value.c_str(), nullptr, 10));
}

// ---------------------------------------------------------------------------
// System tray: Dear ImGui draws inside its own window and has no tray channel,
// so report the state honestly instead of pretending an icon exists.
// ---------------------------------------------------------------------------
void ImGuiBackend::trayIcon(const std::string &iconPath,
                            const std::string &tooltip) {
  trayIconPath_ = iconPath;
  trayTooltip_ = tooltip;
  trayVisible_ = false;
  g_warning("imgui: no tray support, tray icon ignored");
}

void ImGuiBackend::trayMenu(std::shared_ptr<ui::UIElement> menu) {
  (void)menu;
}

void ImGuiBackend::trayNotify(const std::string &title, const std::string &message,
                              const std::string &iconType) {
  (void)title;
  notify(message, iconType);
}

void ImGuiBackend::trayShow() { trayVisible_ = false; }
void ImGuiBackend::trayHide() { trayVisible_ = false; }
bool ImGuiBackend::trayIsVisible() const { return false; }

// ---------------------------------------------------------------------------
// Styling
// ---------------------------------------------------------------------------
void ImGuiBackend::applyStyle(std::shared_ptr<ui::UIElement> element,
                              const std::string &key,
                              const ui::PropValue &value) {
  auto el = resolve(element ? element->id : 0);
  if (!el) return;
  el->props[key] = value;
}

// ---------------------------------------------------------------------------
// Canvas
// ---------------------------------------------------------------------------
void ImGuiBackend::canvasFlush(std::shared_ptr<ui::UIElement> canvas) {
  // Immediate mode: the command list is replayed on the next frame, so
  // nothing has to be invalidated.
  (void)canvas;
}

void ImGuiBackend::canvasClear(std::shared_ptr<ui::UIElement> canvas) {
  auto el = resolve(canvas ? canvas->id : 0);
  if (!el) return;
  el->canvasCommands.clear();
  el->canvasCommands.push_back({ui::CanvasCmdType::CLEAR, {}});
}

void ImGuiBackend::canvasDrawLine(std::shared_ptr<ui::UIElement> canvas, int x1,
                                  int y1, int x2, int y2) {
  auto el = resolve(canvas ? canvas->id : 0);
  if (!el) return;
  el->canvasCommands.push_back(
      {ui::CanvasCmdType::LINE,
       {{"x1", static_cast<int64_t>(x1)},
        {"y1", static_cast<int64_t>(y1)},
        {"x2", static_cast<int64_t>(x2)},
        {"y2", static_cast<int64_t>(y2)}}});
}

void ImGuiBackend::canvasDrawRect(std::shared_ptr<ui::UIElement> canvas, int x,
                                  int y, int w, int h) {
  auto el = resolve(canvas ? canvas->id : 0);
  if (!el) return;
  el->canvasCommands.push_back(
      {ui::CanvasCmdType::RECT,
       {{"x", static_cast<int64_t>(x)},
        {"y", static_cast<int64_t>(y)},
        {"w", static_cast<int64_t>(w)},
        {"h", static_cast<int64_t>(h)}}});
}

void ImGuiBackend::canvasDrawCircle(std::shared_ptr<ui::UIElement> canvas, int cx,
                                    int cy, int r) {
  auto el = resolve(canvas ? canvas->id : 0);
  if (!el) return;
  el->canvasCommands.push_back(
      {ui::CanvasCmdType::CIRCLE,
       {{"cx", static_cast<int64_t>(cx)},
        {"cy", static_cast<int64_t>(cy)},
        {"r", static_cast<int64_t>(r)}}});
}

void ImGuiBackend::canvasSetPen(std::shared_ptr<ui::UIElement> canvas, int r,
                                int g, int b, int width) {
  auto el = resolve(canvas ? canvas->id : 0);
  if (!el) return;
  el->canvasStrokeWidth = width;
  el->canvasCommands.push_back(
      {ui::CanvasCmdType::SET_STROKE,
       {{"r", static_cast<int64_t>(r)},
        {"g", static_cast<int64_t>(g)},
        {"b", static_cast<int64_t>(b)},
        {"width", static_cast<int64_t>(width)}}});
}

void ImGuiBackend::canvasFill(std::shared_ptr<ui::UIElement> canvas, int x,
                              int y) {
  auto el = resolve(canvas ? canvas->id : 0);
  if (!el) return;
  el->canvasCommands.push_back(
      {ui::CanvasCmdType::FILL_RECT,
       {{"x", static_cast<int64_t>(x)}, {"y", static_cast<int64_t>(y)}}});
}

// ---------------------------------------------------------------------------
// Immediate-mode rendering
// ---------------------------------------------------------------------------
void ImGuiBackend::drawElement(const std::shared_ptr<ui::UIElement> &element) {
  if (!element) return;
  const std::string &type = element->type;
  auto open = elementOpen_.find(element->id);
  const bool isWindow = type == ui::ElementType::WINDOW ||
                        type == ui::ElementType::MODAL ||
                        type == ui::ElementType::PANEL;
  if (isWindow && (open == elementOpen_.end() || !open->second)) return;

  if (type == ui::ElementType::WINDOW || type == ui::ElementType::MODAL) {
    std::string title = element->getProp("title", std::string());
    bool windowOpen = elementOpen_[element->id];
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings;
    if (type == ui::ElementType::MODAL) {
      ImGui::SetNextWindowPos(ImGui::GetIO().DisplaySize * 0.5f,
                              ImGuiCond_Always, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiCond_Always);
      flags |= ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse;
    }
    if (ImGui::Begin((title + imguiId(element->id)).c_str(), &windowOpen, flags)) {
      drawMenuBarFor(element);
      drawChildren(element);
    }
    ImGui::End();
    if (!windowOpen) {
      close(element);
      return;
    }
    elementOpen_[element->id] = true;
    return;
  }

  if (type == ui::ElementType::PANEL) {
    // A panel is a docked side area: a resizable child filling the host window.
    const std::string side = element->getProp("side", std::string("left"));
    ImGui::BeginChild((side + imguiId(element->id, "panel")).c_str(), ImVec2(0, 0),
                      true);
    drawChildren(element);
    ImGui::End();
    return;
  }

  if (type == ui::ElementType::ROW || type == ui::ElementType::FLEX) {
    std::string direction = element->getProp("direction", std::string("row"));
    const bool horizontal = type == ui::ElementType::ROW ||
                            (direction != "col" && direction != "column");
    ImGui::BeginGroup();
    size_t index = 0;
    for (auto &child : element->children) {
      if (horizontal && index > 0) ImGui::SameLine();
      drawElement(child);
      ++index;
    }
    ImGui::EndGroup();
    return;
  }

  if (type == ui::ElementType::COL) {
    ImGui::BeginGroup();
    drawChildren(element);
    ImGui::EndGroup();
    return;
  }

  if (type == ui::ElementType::GRID || type == ui::ElementType::TABLE) {
    int columns = type == ui::ElementType::GRID
                      ? element->getProp("columns", 1)
                      : element->getProp("cols", 1);
    if (columns < 1) columns = 1;
    const bool visible = ImGui::BeginTable(
        imguiId(element->id, "table").c_str(), columns,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp);
    if (visible) {
      for (size_t i = 0; i < element->children.size(); ++i) {
        if (i % static_cast<size_t>(columns) == 0) ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(
            static_cast<int>(i % static_cast<size_t>(columns)));
        drawElement(element->children[i]);
      }
    }
    ImGui::EndTable();
    return;
  }

  if (type == ui::ElementType::SCROLL) {
    const int height = element->getProp("height", 240);
    ImGui::BeginChild(imguiId(element->id, "scroll").c_str(),
                      ImVec2(0, static_cast<float>(height)), true,
                      ImGuiWindowFlags_HorizontalScrollbar);
    drawChildren(element);
    ImGui::EndChild();
    return;
  }

  if (type == ui::ElementType::CANVAS) {
    const int width = element->getProp("width", 400);
    const int height = element->getProp("height", 300);
    ImGui::InvisibleButton(imguiId(element->id, "canvas").c_str(),
                           ImVec2(static_cast<float>(width),
                                  static_cast<float>(height)));
    replayCanvas(element, ImGui::GetItemRectMin());
    return;
  }

  if (type == ui::ElementType::TEXT) {
    ImGui::TextWrapped("%s", element->getProp("text", std::string()).c_str());
    return;
  }
  if (type == ui::ElementType::LABEL) {
    ImGui::TextUnformatted(element->getProp("text", std::string()).c_str());
    return;
  }
  if (type == ui::ElementType::BUTTON) {
    if (ImGui::Button(element->getProp("label", std::string()).c_str(),
                      ImVec2(0, 0))) {
      auto cb = element->events.find(ui::EventType::CLICK);
      if (cb != element->events.end() &&
          std::holds_alternative<ui::UIEventCallback>(cb->second)) {
        std::get<ui::UIEventCallback>(cb->second)();
      }
    }
    return;
  }
  if (type == ui::ElementType::INPUT) {
    std::string &value = textValues_[element->id];
    value.resize(kTextBufferSize, '\0');
    if (ImGui::InputTextWithHint(
            imguiId(element->id, "input").c_str(),
            element->getProp("placeholder", std::string()).c_str(),
            value.data(), kTextBufferSize)) {
      auto cb = element->events.find(ui::EventType::CHANGE);
      if (cb != element->events.end() &&
          std::holds_alternative<ui::UIEventCallback>(cb->second)) {
        std::get<ui::UIEventCallback>(cb->second)();
      }
    }
    return;
  }
  if (type == ui::ElementType::TEXTAREA) {
    const int height = element->getProp("height", 96);
    std::string &value = textValues_[element->id];
    value.resize(kTextBufferSize, '\0');
    // This ImGui version has no multiline hint overload, so the placeholder is
    // only used when the field is empty and unfocused is not possible here.
    if (ImGui::InputTextMultiline(imguiId(element->id, "textarea").c_str(),
                                  value.data(), kTextBufferSize,
                                  ImVec2(0, static_cast<float>(height)))) {
      auto cb = element->events.find(ui::EventType::CHANGE);
      if (cb != element->events.end() &&
          std::holds_alternative<ui::UIEventCallback>(cb->second)) {
        std::get<ui::UIEventCallback>(cb->second)();
      }
    }
    return;
  }
  if (type == ui::ElementType::CHECKBOX || type == ui::ElementType::TOGGLE) {
    bool checked = boolValues_[element->id];
    if (ImGui::Checkbox(element->getProp("label", std::string()).c_str(),
                        &checked)) {
      boolValues_[element->id] = checked;
      auto cb = element->events.find(ui::EventType::CHANGE);
      if (cb != element->events.end() &&
          std::holds_alternative<ui::UIEventCallback>(cb->second)) {
        std::get<ui::UIEventCallback>(cb->second)();
      }
    }
    return;
  }
  if (type == ui::ElementType::SLIDER) {
    int value = intValues_[element->id];
    const int min = element->getProp("min", 0);
    const int max = element->getProp("max", 100);
    if (ImGui::SliderInt(imguiId(element->id, "slider").c_str(), &value, min,
                         max)) {
      intValues_[element->id] = value;
    }
    return;
  }
  if (type == ui::ElementType::DROPDOWN) {
    auto opts = dropdownOptions_.find(element->id);
    if (opts == dropdownOptions_.end()) return;
    int selection = comboSelections_[element->id];
    if (selection < 0 || selection >= static_cast<int>(opts->second.size())) {
      selection = 0;
    }
    std::string preview = opts->second.empty() ? "" : opts->second[selection];
    if (ImGui::BeginCombo(imguiId(element->id, "dropdown").c_str(),
                          preview.c_str())) {
      for (size_t i = 0; i < opts->second.size(); ++i) {
        bool selected = selection == static_cast<int>(i);
        if (ImGui::Selectable(opts->second[i].c_str(), selected)) {
          comboSelections_[element->id] = static_cast<int>(i);
        }
        if (selected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
    return;
  }
  if (type == ui::ElementType::PROGRESS) {
    float fraction = floatValues_[element->id];
    char overlay[16];
    std::snprintf(overlay, sizeof(overlay), "%d%%",
                  static_cast<int>(fraction * 100.0f));
    ImGui::ProgressBar(fraction, ImVec2(-1, 0), overlay);
    return;
  }
  if (type == ui::ElementType::SPACER) {
    const int size = element->getProp("size", 8);
    ImGui::Dummy(ImVec2(static_cast<float>(size), static_cast<float>(size)));
    return;
  }
  if (type == ui::ElementType::DIVIDER) {
    ImGui::Separator();
    return;
  }
  if (type == ui::ElementType::SPINNER) {
    ImGui::TextDisabled("loading...");
    return;
  }
  if (type == ui::ElementType::IMAGE) {
    ImGui::TextDisabled("[image: %s]",
                        element->getProp("path", std::string()).c_str());
    return;
  }
  if (type == ui::ElementType::ICON) {
    ImGui::TextDisabled("[%s]",
                        element->getProp("name", std::string()).c_str());
    return;
  }
  if (type == ui::ElementType::MENU || type == ui::ElementType::MENU_ITEM ||
      type == ui::ElementType::MENU_SEP) {
    // Menus are submitted by drawMenuBarFor from their parent window.
    return;
  }
  // Unknown type: surface it instead of dropping it silently.
  ImGui::TextDisabled("[%s]", type.c_str());
}

void ImGuiBackend::drawChildren(const std::shared_ptr<ui::UIElement> &parent) {
  if (!parent) return;
  for (auto &child : parent->children) drawElement(child);
}

void ImGuiBackend::drawMenuBarFor(const std::shared_ptr<ui::UIElement> &window) {
  if (!window) return;
  bool hasMenu = false;
  for (auto &child : window->children) {
    if (child && child->type == ui::ElementType::MENU) hasMenu = true;
  }
  if (!hasMenu) return;

  if (ImGui::BeginMenuBar()) {
    for (auto &child : window->children) {
      if (!child || child->type != ui::ElementType::MENU) continue;
      const std::string title = child->getProp("title", std::string());
      if (ImGui::BeginMenu(title.c_str())) drawMenuContents(child);
      ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
  }
}

void ImGuiBackend::drawMenuContents(const std::shared_ptr<ui::UIElement> &menu) {
  if (!menu) return;
  for (auto &child : menu->children) {
    if (!child) continue;
    if (child->type == ui::ElementType::MENU_ITEM) {
      const std::string label = child->getProp("label", std::string());
      const std::string shortcut = child->getProp("shortcut", std::string());
      if (ImGui::MenuItem(label.c_str(), shortcut.empty() ? nullptr
                                                          : shortcut.c_str())) {
        auto cb = child->events.find(ui::EventType::CLICK);
        if (cb != child->events.end() &&
            std::holds_alternative<ui::UIEventCallback>(cb->second)) {
          std::get<ui::UIEventCallback>(cb->second)();
        }
      }
    } else if (child->type == ui::ElementType::MENU_SEP) {
      ImGui::Separator();
    }
  }
}

void ImGuiBackend::replayCanvas(const std::shared_ptr<ui::UIElement> &element,
                                ImVec2 origin) {
  if (!element) return;
  ImDrawList *drawList = ImGui::GetWindowDrawList();
  ImVec2 max = ImGui::GetItemRectMax();

  auto toImu = [&](const std::string &color, ImU32 fallback) {
    if (color == "black") return IM_COL32(0, 0, 0, 255);
    if (color == "white") return IM_COL32(255, 255, 255, 255);
    if (color == "red") return IM_COL32(255, 0, 0, 255);
    if (color == "green") return IM_COL32(0, 255, 0, 255);
    if (color == "blue") return IM_COL32(0, 0, 255, 255);
    if (color.size() == 7 && color[0] == '#') {
      int r = static_cast<int>(std::strtol(color.substr(1, 2).c_str(), nullptr, 16));
      int g = static_cast<int>(std::strtol(color.substr(3, 2).c_str(), nullptr, 16));
      int b = static_cast<int>(std::strtol(color.substr(5, 2).c_str(), nullptr, 16));
      return IM_COL32(r, g, b, 255);
    }
    return fallback;
  };

  ImU32 stroke = toImu(element->canvasStrokeColor, IM_COL32(255, 255, 255, 255));
  ImU32 fill = toImu(element->canvasFillColor, IM_COL32(0, 0, 0, 0));
  float thickness = static_cast<float>(element->canvasStrokeWidth);

  auto point = [&](float x, float y) { return ImVec2(origin.x + x, origin.y + y); };
  auto p = [&](const ui::CanvasCmd &cmd, const char *key, int dflt) {
    return cmd.getParam(key, dflt);
  };

  for (const auto &cmd : element->canvasCommands) {
    const std::string &type = cmd.type;
    auto colorOf = [&](const char *key) {
      auto it = cmd.params.find(key);
      if (it != cmd.params.end() &&
          std::holds_alternative<std::string>(it->second)) {
        return toImu(std::get<std::string>(it->second), stroke);
      }
      auto r = cmd.params.find("r"), g = cmd.params.find("g"),
           b = cmd.params.find("b");
      if (r != cmd.params.end() && g != cmd.params.end() &&
          b != cmd.params.end()) {
        return IM_COL32(static_cast<int>(std::get<int64_t>(r->second)),
                        static_cast<int>(std::get<int64_t>(g->second)),
                        static_cast<int>(std::get<int64_t>(b->second)), 255);
      }
      return ImU32(0);
    };

    if (type == ui::CanvasCmdType::CLEAR) {
      drawList->AddRectFilled(origin, max, IM_COL32(32, 32, 36, 255));
    } else if (type == ui::CanvasCmdType::SET_COLOR ||
               type == ui::CanvasCmdType::SET_STROKE) {
      stroke = colorOf("color");
    } else if (type == ui::CanvasCmdType::SET_FILL) {
      fill = colorOf("color");
    } else if (type == ui::CanvasCmdType::SET_LINE_WIDTH) {
      thickness = static_cast<float>(p(cmd, "width", 1));
    } else if (type == ui::CanvasCmdType::MOVE_TO) {
      // ImGui has no path object: moves are applied to the next line/polyline.
      continue;
    } else if (type == ui::CanvasCmdType::LINE) {
      drawList->AddLine(point(static_cast<float>(p(cmd, "x1", 0)),
                             static_cast<float>(p(cmd, "y1", 0))),
                        point(static_cast<float>(p(cmd, "x2", 0)),
                              static_cast<float>(p(cmd, "y2", 0))),
                        stroke, thickness);
    } else if (type == ui::CanvasCmdType::LINE_TO) {
      drawList->AddLine(origin, point(static_cast<float>(p(cmd, "x", 0)),
                                      static_cast<float>(p(cmd, "y", 0))),
                        stroke, thickness);
    } else if (type == ui::CanvasCmdType::RECT) {
      const float x = static_cast<float>(p(cmd, "x", 0));
      const float y = static_cast<float>(p(cmd, "y", 0));
      drawList->AddRect(point(x, y),
                        point(x + static_cast<float>(p(cmd, "w", 0)),
                              y + static_cast<float>(p(cmd, "h", 0))),
                        stroke, thickness);
    } else if (type == ui::CanvasCmdType::FILL_RECT) {
      const float x = static_cast<float>(p(cmd, "x", 0));
      const float y = static_cast<float>(p(cmd, "y", 0));
      const float w = static_cast<float>(p(cmd, "w", 0));
      const float h = static_cast<float>(p(cmd, "h", 0));
      drawList->AddRectFilled(point(x, y), point(x + w, y + h), fill);
    } else if (type == ui::CanvasCmdType::CIRCLE) {
      drawList->AddCircle(point(static_cast<float>(p(cmd, "cx", 0)),
                                static_cast<float>(p(cmd, "cy", 0))),
                          static_cast<float>(p(cmd, "r", 0)), stroke, 0,
                          thickness);
    } else if (type == ui::CanvasCmdType::FILL_CIRCLE) {
      drawList->AddCircleFilled(point(static_cast<float>(p(cmd, "cx", 0)),
                                     static_cast<float>(p(cmd, "cy", 0))),
                               static_cast<float>(p(cmd, "r", 0)), fill);
    } else if (type == ui::CanvasCmdType::ARC) {
      const float cx = static_cast<float>(p(cmd, "cx", 0));
      const float cy = static_cast<float>(p(cmd, "cy", 0));
      auto param = cmd.params.find("span");
      float span = 6.2831853f;
      if (param != cmd.params.end() &&
          std::holds_alternative<double>(param->second)) {
        span = static_cast<float>(std::get<double>(param->second));
      }
      drawList->PathArcTo(point(cx, cy), static_cast<float>(p(cmd, "r", 0)),
                          0.0f, span, 24);
      drawList->PathStroke(stroke, 0, thickness);
    } else if (type == ui::CanvasCmdType::TEXT) {
      auto it = cmd.params.find("text");
      std::string text =
          it != cmd.params.end() && std::holds_alternative<std::string>(it->second)
              ? std::get<std::string>(it->second)
              : "";
      drawList->AddText(point(static_cast<float>(p(cmd, "x", 0)),
                             static_cast<float>(p(cmd, "y", 0))),
                        IM_COL32(255, 255, 255, 255), text.c_str());
    }
  }
}

void ImGuiBackend::drawToasts() {
  while (!toasts_.empty() && toasts_.front().expiresAt <= now_) {
    toasts_.pop_front();
  }
  if (toasts_.empty()) return;

  const ImVec2 display = ImGui::GetIO().DisplaySize;
  float offset = 16.0f;
  int toastIndex = 0;
  for (const auto &toast : toasts_) {
    ImVec2 size(320, 0);
    ImGui::SetNextWindowPos(ImVec2(display.x - size.x - 16.0f,
                                    display.y - offset - 64.0f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.9f);
    if (ImGui::Begin(
            imguiId(static_cast<ui::ElementId>(toastIndex), "toast").c_str(),
                     nullptr,
                     ImGuiWindowFlags_NoDecoration |
                         ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoInputs |
                         ImGuiWindowFlags_NoNav)) {
      ImVec4 tint(1, 1, 1, 1);
      if (toast.type == "error") tint = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
      else if (toast.type == "warning") tint = ImVec4(1.0f, 0.8f, 0.3f, 1.0f);
      else if (toast.type == "success") tint = ImVec4(0.5f, 1.0f, 0.5f, 1.0f);
      ImGui::TextColored(tint, "%s", toast.message.c_str());
    }
    ImGui::End();
    offset += 48.0f;
    ++toastIndex;
  }
}

// ---------------------------------------------------------------------------
// Window controls
// ---------------------------------------------------------------------------
void ImGuiBackend::setWindowTitle(const std::string &title) {
  windowTitle_ = title;
  if (window_) glfwSetWindowTitle(window_, title.c_str());
}

void ImGuiBackend::setWindowSize(int width, int height) {
  windowWidth_ = width;
  windowHeight_ = height;
  if (window_) glfwSetWindowSize(window_, width, height);
}

void ImGuiBackend::setFrameRate(int fps) {
  targetFps_ = fps;
  fpsLimit_ = fps > 0 ? 1.0 / fps : 0.0;
}

void ImGuiBackend::processInputs() {
  // Immediate mode: every input is read while the frame is being submitted.
}

} // namespace havel::host

#endif // HAVE_IMGUI_BACKEND
