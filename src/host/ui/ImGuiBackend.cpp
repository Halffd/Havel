/*
 * ImGuiBackend.cpp - Dear ImGui implementation of UIBackend
 */
#include "ImGuiBackend.hpp"

#ifdef HAVE_IMGUI_BACKEND

#include "modules/ui/UIElement.hpp"
#include "utils/Logger.hpp"
#include <iostream>
#include <cstring>

// ImGui and GLFW headers
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

namespace havel::host {

ImGuiBackend::ImGuiBackend() = default;

ImGuiBackend::~ImGuiBackend() {
    if (initialized_) {
        shutdown();
    }
}

bool ImGuiBackend::isAvailable() const {
    // Check if GLFW is available
    if (!glfwInit()) {
        return false;
    }
    glfwTerminate();
    return true;
}

bool ImGuiBackend::initialize() {
    if (initialized_) {
        return true;
    }
    
    // Initialize GLFW
    if (!initGLFW()) {
        havel::error("Failed to initialize GLFW");
        return false;
    }
    
    // Initialize ImGui
    if (!initImGui()) {
        havel::error("Failed to initialize ImGui");
        shutdownGLFW();
        return false;
    }
    
    initialized_ = true;
    running_ = true;
    
    havel::info("ImGui backend initialized (ImGui {})", IMGUI_VERSION);
    
    return true;
}

void ImGuiBackend::shutdown() {
    if (!initialized_) {
        return;
    }
    
    running_ = false;
    
    shutdownImGui();
    shutdownGLFW();
    
    initialized_ = false;
    havel::info("ImGui backend shutdown complete");
}

bool ImGuiBackend::initGLFW() {
    if (!glfwInit()) {
        return false;
    }
    
    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    
    // Create window
    window_ = glfwCreateWindow(windowWidth_, windowHeight_, windowTitle_.c_str(), nullptr, nullptr);
    if (window_ == nullptr) {
        return false;
    }
    
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1); // Enable vsync
    
    return true;
}

bool ImGuiBackend::initImGui() {
    // Create ImGui context
    IMGUI_CHECKVERSION();
    context_ = ImGui::CreateContext();
    if (!context_) {
        return false;
    }
    
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    
    // Setup style
    ImGui::StyleColorsDark();
    
    // Setup Platform/Renderer bindings
    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 130");
    
    return true;
}

void ImGuiBackend::shutdownGLFW() {
    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    glfwTerminate();
}

void ImGuiBackend::shutdownImGui() {
    if (context_) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        context_ = nullptr;
    }
}

// Element creation
std::shared_ptr<ui::UIElement> ImGuiBackend::window(const std::string &title) {
    auto element = std::make_shared<ui::UIElement>();
    element->id = generateId("window");
    element->type = ui::UIElement::Type::Window;
    element->props["title"] = title;
    
    elements_[element->id] = element;
    elementOpen_[element->id] = true;
    windowStack_.push_back(element->id);
    
    return element;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::panel(const std::string &side) {
    auto element = std::make_shared<ui::UIElement>();
    element->id = generateId("panel");
    element->type = ui::UIElement::Type::Panel;
    element->props["side"] = side;
    
    elements_[element->id] = element;
    elementOpen_[element->id] = true;
    
    return element;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::modal(const std::string &title) {
    auto element = std::make_shared<ui::UIElement>();
    element->id = generateId("modal");
    element->type = ui::UIElement::Type::Modal;
    element->props["title"] = title;
    
    elements_[element->id] = element;
    elementOpen_[element->id] = true;
    
    return element;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::text(const std::string &content) {
    auto element = std::make_shared<ui::UIElement>();
    element->id = generateId("text");
    element->type = ui::UIElement::Type::Text;
    element->props["content"] = content;
    
    elements_[element->id] = element;
    
    return element;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::label(const std::string &content) {
    auto element = std::make_shared<ui::UIElement>();
    element->id = generateId("label");
    element->type = ui::UIElement::Type::Label;
    element->props["content"] = content;
    
    elements_[element->id] = element;
    
    return element;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::image(const std::string &path) {
    auto element = std::make_shared<ui::UIElement>();
    element->id = generateId("image");
    element->type = ui::UIElement::Type::Image;
    element->props["path"] = path;
    
    elements_[element->id] = element;
    
    return element;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::icon(const std::string &name) {
    auto element = std::make_shared<ui::UIElement>();
    element->id = generateId("icon");
    element->type = ui::UIElement::Type::Icon;
    element->props["name"] = name;
    
    elements_[element->id] = element;
    
    return element;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::divider() {
    auto element = std::make_shared<ui::UIElement>();
    element->id = generateId("divider");
    element->type = ui::UIElement::Type::Divider;
    
    elements_[element->id] = element;
    
    return element;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::spacer(int size) {
    auto element = std::make_shared<ui::UIElement>();
    element->id = generateId("spacer");
    element->type = ui::UIElement::Type::Spacer;
    element->props["size"] = size;
    
    elements_[element->id] = element;
    
    return element;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::progress(int value, int max) {
    auto element = std::make_shared<ui::UIElement>();
    element->id = generateId("progress");
    element->type = ui::UIElement::Type::Progress;
    element->props["value"] = value;
    element->props["max"] = max;
    
    elements_[element->id] = element;
    floatValues_[element->id] = static_cast<float>(value) / max;
    
    return element;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::spinner() {
    auto element = std::make_shared<ui::UIElement>();
    element->id = generateId("spinner");
    element->type = ui::UIElement::Type::Spinner;
    
    elements_[element->id] = element;
    
    return element;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::btn(const std::string &label) {
    auto element = std::make_shared<ui::UIElement>();
    element->id = generateId("button");
    element->type = ui::UIElement::Type::Button;
    element->props["label"] = label;
    
    elements_[element->id] = element;
    
    return element;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::input(const std::string &placeholder) {
    auto element = std::make_shared<ui::UIElement>();
    element->id = generateId("input");
    element->type = ui::UIElement::Type::Input;
    element->props["placeholder"] = placeholder;
    
    elements_[element->id] = element;
    textValues_[element->id] = "";
    
    return element;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::textarea(const std::string &placeholder) {
    auto element = std::make_shared<ui::UIElement>();
    element->id = generateId("textarea");
    element->type = ui::UIElement::Type::TextArea;
    element->props["placeholder"] = placeholder;
    
    elements_[element->id] = element;
    textValues_[element->id] = "";
    
    return element;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::checkbox(const std::string &label, bool checked) {
    auto element = std::make_shared<ui::UIElement>();
    element->id = generateId("checkbox");
    element->type = ui::UIElement::Type::Checkbox;
    element->props["label"] = label;
    element->props["checked"] = checked;
    
    elements_[element->id] = element;
    boolValues_[element->id] = checked;
    
    return element;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::toggle(const std::string &label, bool value) {
    auto element = std::make_shared<ui::UIElement>();
    element->id = generateId("toggle");
    element->type = ui::UIElement::Type::Toggle;
    element->props["label"] = label;
    element->props["value"] = value;
    
    elements_[element->id] = element;
    boolValues_[element->id] = value;
    
    return element;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::slider(int min, int max, int value) {
    auto element = std::make_shared<ui::UIElement>();
    element->id = generateId("slider");
    element->type = ui::UIElement::Type::Slider;
    element->props["min"] = min;
    element->props["max"] = max;
    element->props["value"] = value;
    
    elements_[element->id] = element;
    intValues_[element->id] = value;
    
    return element;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::dropdown(const std::vector<std::string> &options) {
    auto element = std::make_shared<ui::UIElement>();
    element->id = generateId("dropdown");
    element->type = ui::UIElement::Type::Dropdown;
    element->props["options"] = options;
    
    elements_[element->id] = element;
    comboSelections_[element->id] = 0;
    
    return element;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::row() {
    auto element = std::make_shared<ui::UIElement>();
    element->id = generateId("row");
    element->type = ui::UIElement::Type::Row;
    
    elements_[element->id] = element;
    
    return element;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::col() {
    auto element = std::make_shared<ui::UIElement>();
    element->id = generateId("col");
    element->type = ui::UIElement::Type::Column;
    
    elements_[element->id] = element;
    
    return element;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::grid(int cols) {
    auto element = std::make_shared<ui::UIElement>();
    element->id = generateId("grid");
    element->type = ui::UIElement::Type::Grid;
    element->props["columns"] = cols;
    
    elements_[element->id] = element;
    
    return element;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::table(int rows, int cols) {
    auto element = std::make_shared<ui::UIElement>();
    element->id = generateId("table");
    element->type = ui::UIElement::Type::Table;
    element->props["rows"] = rows;
    element->props["cols"] = cols;
    
    elements_[element->id] = element;
    
    return element;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::flex(const std::string &direction) {
    auto element = std::make_shared<ui::UIElement>();
    element->id = generateId("flex");
    element->type = ui::UIElement::Type::Flex;
    element->props["direction"] = direction;
    
    elements_[element->id] = element;
    
    return element;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::scroll() {
    auto element = std::make_shared<ui::UIElement>();
    element->id = generateId("scroll");
    element->type = ui::UIElement::Type::Scroll;
    
    elements_[element->id] = element;
    
    return element;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::canvas(int width, int height) {
    auto element = std::make_shared<ui::UIElement>();
    element->id = generateId("canvas");
    element->type = ui::UIElement::Type::Canvas;
    element->props["width"] = width;
    element->props["height"] = height;
    
    elements_[element->id] = element;
    
    return element;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::menu(const std::string &title) {
    auto element = std::make_shared<ui::UIElement>();
    element->id = generateId("menu");
    element->type = ui::UIElement::Type::Menu;
    element->props["title"] = title;
    
    elements_[element->id] = element;
    
    return element;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::menuItem(const std::string &label, const std::string &shortcut) {
    auto element = std::make_shared<ui::UIElement>();
    element->id = generateId("menuitem");
    element->type = ui::UIElement::Type::MenuItem;
    element->props["label"] = label;
    element->props["shortcut"] = shortcut;
    
    elements_[element->id] = element;
    
    return element;
}

std::shared_ptr<ui::UIElement> ImGuiBackend::menuSeparator() {
    auto element = std::make_shared<ui::UIElement>();
    element->id = generateId("menuseparator");
    element->type = ui::UIElement::Type::MenuSeparator;
    
    elements_[element->id] = element;
    
    return element;
}

void ImGuiBackend::realize(std::shared_ptr<ui::UIElement> element) {
    // In ImGui, realization is done during render
    (void)element;
}

void ImGuiBackend::show(std::shared_ptr<ui::UIElement> window) {
    if (!window) return;
    elementOpen_[window->id] = true;
}

void ImGuiBackend::hide(std::shared_ptr<ui::UIElement> window) {
    if (!window) return;
    elementOpen_[window->id] = false;
}

void ImGuiBackend::close(std::shared_ptr<ui::UIElement> window) {
    if (!window) return;
    elementOpen_[window->id] = false;
    elements_.erase(window->id);
    
    // Remove from window stack
    auto it = std::find(windowStack_.begin(), windowStack_.end(), window->id);
    if (it != windowStack_.end()) {
        windowStack_.erase(it);
    }
    
    // Check if all windows closed
    if (windowStack_.empty() && onAllWindowsClosedCallback_) {
        onAllWindowsClosedCallback_();
    }
}

void ImGuiBackend::alert(const std::string &message) {
    // Queue alert for next frame
    // In ImGui, we'd use a popup modal
    havel::info("Alert: {}", message);
}

bool ImGuiBackend::confirm(const std::string &message) {
    // In ImGui, this would be a blocking modal
    havel::info("Confirm: {} (returning true)", message);
    return true;
}

std::string ImGuiBackend::filePicker(const std::string &title) {
    // Use native file picker or ImGui file dialog
    (void)title;
    return "";
}

std::string ImGuiBackend::dirPicker(const std::string &title) {
    (void)title;
    return "";
}

void ImGuiBackend::notify(const std::string &message, const std::string &type) {
    // ImGui doesn't have native notifications
    // Could use a toast system or external library
    havel::info("Notification [{}]: {}", type, message);
}

void ImGuiBackend::pumpEvents(int timeoutMs) {
    (void)timeoutMs;
    
    if (!window_ || !running_) return;
    
    // Check if window should close
    if (glfwWindowShouldClose(window_)) {
        running_ = false;
        if (onAllWindowsClosedCallback_) {
            onAllWindowsClosedCallback_();
        }
        return;
    }
    
    // Poll events
    glfwPollEvents();
    
    // Start ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    
    // Render all windows
    for (const auto& [id, element] : elements_) {
        if (!elementOpen_[id]) continue;
        
        switch (element->type) {
            case ui::UIElement::Type::Window:
                drawWindow(element);
                break;
            case ui::UIElement::Type::Button:
                drawButton(element);
                break;
            case ui::UIElement::Type::Input:
                drawInput(element);
                break;
            case ui::UIElement::Type::TextArea:
                drawTextArea(element);
                break;
            case ui::UIElement::Type::Checkbox:
                drawCheckbox(element);
                break;
            case ui::UIElement::Type::Toggle:
                drawToggle(element);
                break;
            case ui::UIElement::Type::Slider:
                drawSlider(element);
                break;
            case ui::UIElement::Type::Dropdown:
                drawDropdown(element);
                break;
            case ui::UIElement::Type::Label:
                drawLabel(element);
                break;
            case ui::UIElement::Type::Text:
                drawText(element);
                break;
            case ui::UIElement::Type::Progress:
                drawProgress(element);
                break;
            case ui::UIElement::Type::Image:
                drawImage(element);
                break;
            case ui::UIElement::Type::Icon:
                drawIcon(element);
                break;
            case ui::UIElement::Type::Row:
                drawRow(element);
                break;
            case ui::UIElement::Type::Column:
                drawCol(element);
                break;
            case ui::UIElement::Type::Grid:
                drawGrid(element);
                break;
            case ui::UIElement::Type::Scroll:
                drawScroll(element);
                break;
            case ui::UIElement::Type::Canvas:
                drawCanvas(element);
                break;
            case ui::UIElement::Type::Menu:
                drawMenu(element);
                break;
            case ui::UIElement::Type::MenuItem:
                drawMenuItem(element);
                break;
            case ui::UIElement::Type::Divider:
                drawDivider();
                break;
            case ui::UIElement::Type::Spacer:
                drawSpacer(std::get<int>(element->props["size"]));
                break;
            case ui::UIElement::Type::Spinner:
                drawSpinner();
                break;
            default:
                break;
        }
    }
    
    // Rendering
    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(window_, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    
    // Update and Render additional Platform Windows
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        GLFWwindow* backup_current_context = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backup_current_context);
    }
    
    glfwSwapBuffers(window_);
}

bool ImGuiBackend::hasActiveWindows() const {
    return !windowStack_.empty();
}

void ImGuiBackend::onAllWindowsClosed(std::function<void()> callback) {
    onAllWindowsClosedCallback_ = callback;
}

std::string ImGuiBackend::getValue(std::shared_ptr<ui::UIElement> element) {
    if (!element) return "";
    
    auto it = textValues_.find(element->id);
    if (it != textValues_.end()) {
        return it->second;
    }
    
    auto intIt = intValues_.find(element->id);
    if (intIt != intValues_.end()) {
        return std::to_string(intIt->second);
    }
    
    auto boolIt = boolValues_.find(element->id);
    if (boolIt != boolValues_.end()) {
        return boolIt->second ? "true" : "false";
    }
    
    auto floatIt = floatValues_.find(element->id);
    if (floatIt != floatValues_.end()) {
        return std::to_string(floatIt->second);
    }
    
    return "";
}

void ImGuiBackend::setValue(std::shared_ptr<ui::UIElement> element, const std::string &value) {
    if (!element) return;
    
    elementValues_[element->id] = value;
    textValues_[element->id] = value;
}

void ImGuiBackend::trayIcon(const std::string &iconPath, const std::string &tooltip) {
    trayIconPath_ = iconPath;
    trayTooltip_ = tooltip;
    trayVisible_ = true;
}

void ImGuiBackend::trayMenu(std::shared_ptr<ui::UIElement> menu) {
    (void)menu;
}

void ImGuiBackend::trayNotify(const std::string &title, const std::string &message, const std::string &iconType) {
    (void)title;
    (void)iconType;
    havel::info("Tray notification: {}", message);
}

void ImGuiBackend::trayShow() {
    trayVisible_ = true;
}

void ImGuiBackend::trayHide() {
    trayVisible_ = false;
}

bool ImGuiBackend::trayIsVisible() const {
    return trayVisible_;
}

void ImGuiBackend::applyStyle(std::shared_ptr<ui::UIElement> element, const std::string &key, const ui::PropValue &value) {
    (void)element;
    (void)key;
    (void)value;
    // ImGui uses global styles, not per-element
}

void ImGuiBackend::setWindowTitle(const std::string &title) {
    windowTitle_ = title;
    if (window_) {
        glfwSetWindowTitle(window_, title.c_str());
    }
}

void ImGuiBackend::setWindowSize(int width, int height) {
    windowWidth_ = width;
    windowHeight_ = height;
    if (window_) {
        glfwSetWindowSize(window_, width, height);
    }
}

void ImGuiBackend::setFrameRate(int fps) {
    targetFps_ = fps;
}

std::string ImGuiBackend::generateId(const std::string &prefix) {
    return prefix + "_" + std::to_string(++idCounter_);
}

// Drawing helpers
void ImGuiBackend::drawWindow(std::shared_ptr<ui::UIElement> element) {
    auto title = std::get<std::string>(element->props["title"]);
    bool *open = &elementOpen_[element->id];
    
    if (ImGui::Begin(title.c_str(), open)) {
        // Window content would be drawn here
    }
    ImGui::End();
}

void ImGuiBackend::drawButton(std::shared_ptr<ui::UIElement> element) {
    auto label = std::get<std::string>(element->props["label"]);
    if (ImGui::Button(label.c_str())) {
        // Trigger click handler
    }
}

void ImGuiBackend::drawInput(std::shared_ptr<ui::UIElement> element) {
    auto placeholder = std::get<std::string>(element->props["placeholder"]);
    char buf[256] = "";
    strncpy(buf, textValues_[element->id].c_str(), sizeof(buf) - 1);
    
    if (ImGui::InputText(placeholder.c_str(), buf, sizeof(buf))) {
        textValues_[element->id] = buf;
    }
}

void ImGuiBackend::drawTextArea(std::shared_ptr<ui::UIElement> element) {
    auto placeholder = std::get<std::string>(element->props["placeholder"]);
    static char buf[1024] = "";
    strncpy(buf, textValues_[element->id].c_str(), sizeof(buf) - 1);
    
    if (ImGui::InputTextMultiline(placeholder.c_str(), buf, sizeof(buf))) {
        textValues_[element->id] = buf;
    }
}

void ImGuiBackend::drawCheckbox(std::shared_ptr<ui::UIElement> element) {
    auto label = std::get<std::string>(element->props["label"]);
    bool checked = boolValues_[element->id];
    
    if (ImGui::Checkbox(label.c_str(), &checked)) {
        boolValues_[element->id] = checked;
    }
}

void ImGuiBackend::drawToggle(std::shared_ptr<ui::UIElement> element) {
    auto label = std::get<std::string>(element->props["label"]);
    bool value = boolValues_[element->id];
    
    if (ImGui::Checkbox(label.c_str(), &value)) {
        boolValues_[element->id] = value;
    }
}

void ImGuiBackend::drawSlider(std::shared_ptr<ui::UIElement> element) {
    auto label = element->id; // Use id as label
    int value = intValues_[element->id];
    int min = std::get<int>(element->props["min"]);
    int max = std::get<int>(element->props["max"]);
    
    if (ImGui::SliderInt(label.c_str(), &value, min, max)) {
        intValues_[element->id] = value;
    }
}

void ImGuiBackend::drawDropdown(std::shared_ptr<ui::UIElement> element) {
    auto options = std::get<std::vector<std::string>>(element->props["options"]);
    int selection = comboSelections_[element->id];
    
    if (ImGui::BeginCombo(element->id.c_str(), options[selection].c_str())) {
        for (int i = 0; i < static_cast<int>(options.size()); i++) {
            bool isSelected = (selection == i);
            if (ImGui::Selectable(options[i].c_str(), isSelected)) {
                comboSelections_[element->id] = i;
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
}

void ImGuiBackend::drawLabel(std::shared_ptr<ui::UIElement> element) {
    auto content = std::get<std::string>(element->props["content"]);
    ImGui::Text("%s", content.c_str());
}

void ImGuiBackend::drawText(std::shared_ptr<ui::UIElement> element) {
    auto content = std::get<std::string>(element->props["content"]);
    ImGui::TextWrapped("%s", content.c_str());
}

void ImGuiBackend::drawProgress(std::shared_ptr<ui::UIElement> element) {
    float fraction = floatValues_[element->id];
    ImGui::ProgressBar(fraction);
}

void ImGuiBackend::drawImage(std::shared_ptr<ui::UIElement> element) {
    auto path = std::get<std::string>(element->props["path"]);
    // Would load and display texture
    ImGui::Text("[Image: %s]", path.c_str());
}

void ImGuiBackend::drawIcon(std::shared_ptr<ui::UIElement> element) {
    auto name = std::get<std::string>(element->props["name"]);
    ImGui::Text("[%s]", name.c_str());
}

void ImGuiBackend::drawRow(std::shared_ptr<ui::UIElement> element) {
    (void)element;
    // Use SameLine for horizontal layout
}

void ImGuiBackend::drawCol(std::shared_ptr<ui::UIElement> element) {
    (void)element;
    // Default vertical layout
}

void ImGuiBackend::drawGrid(std::shared_ptr<ui::UIElement> element) {
    (void)element;
    // Use columns API
}

void ImGuiBackend::drawScroll(std::shared_ptr<ui::UIElement> element) {
    (void)element;
    // Use BeginChild with scrolling flags
}

void ImGuiBackend::drawCanvas(std::shared_ptr<ui::UIElement> element) {
    int width = std::get<int>(element->props["width"]);
    int height = std::get<int>(element->props["height"]);
    
    ImVec2 size(width, height);
    ImGui::InvisibleButton(element->id.c_str(), size);
    
    // Draw canvas background
    ImVec2 p0 = ImGui::GetItemRectMin();
    ImVec2 p1 = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(p0, p1, IM_COL32(50, 50, 50, 255));
}

void ImGuiBackend::drawMenu(std::shared_ptr<ui::UIElement> element) {
    auto title = std::get<std::string>(element->props["title"]);
    if (ImGui::BeginMenu(title.c_str())) {
        ImGui::EndMenu();
    }
}

void ImGuiBackend::drawMenuItem(std::shared_ptr<ui::UIElement> element) {
    auto label = std::get<std::string>(element->props["label"]);
    auto shortcut = std::get<std::string>(element->props["shortcut"]);
    ImGui::MenuItem(label.c_str(), shortcut.c_str());
}

void ImGuiBackend::drawDivider() {
    ImGui::Separator();
}

void ImGuiBackend::drawSpacer(int size) {
    ImGui::Dummy(ImVec2(static_cast<float>(size), static_cast<float>(size)));
}

void ImGuiBackend::drawSpinner() {
    // ImGui doesn't have a built-in spinner, but we can use a rotating indicator
    ImGui::Text("Loading...");
}

} // namespace havel::host

#endif // HAVE_IMGUI_BACKEND
