#include "GUI.hpp"
#include <gtkmm/cssprovider.h>
#include <stdexcept>

namespace havel {

class GUI::Impl {
public:
    Impl() = default;
    
    // GTK-specific members
    Glib::RefPtr<Gtk::CssProvider> cssProvider;
    Glib::RefPtr<Gtk::Settings> settings;
};

MainWindow::MainWindow()
    : mainBox(Gtk::ORIENTATION_VERTICAL)
    , notebook() {
    // Setup window
    set_title("Window Manager");
    set_default_size(800, 600);
    
    // Add main box
    add(mainBox);
    
    // Setup menu bar
    auto fileMenu = Gtk::manage(new Gtk::MenuItem("_File", true));
    auto fileSubMenu = Gtk::manage(new Gtk::Menu());
    auto quitItem = Gtk::manage(new Gtk::MenuItem("_Quit", true));
    
    fileSubMenu->append(*quitItem);
    fileMenu->set_submenu(*fileSubMenu);
    menuBar.append(*fileMenu);
    
    mainBox.pack_start(menuBar, Gtk::PACK_SHRINK);
    mainBox.pack_start(notebook);
    
    // Connect signals
    quitItem->signal_activate().connect(
        sigc::mem_fun(*this, &MainWindow::on_button_clicked));
    
    show_all_children();
}

void MainWindow::on_button_clicked() {
    hide();
}

bool MainWindow::on_key_press_event(GdkEventKey* event) {
    // Handle keyboard shortcuts
    if ((event->state & GDK_CONTROL_MASK) && event->keyval == GDK_KEY_q) {
        hide();
        return true;
    }
    return Gtk::Window::on_key_press_event(event);
}

bool MainWindow::on_delete_event(GdkEventAny* event) {
    hide();
    return true;
}

GUI::GUI(std::shared_ptr<WindowManager> wm, Mode mode)
    : windowManager(std::move(wm))
    , currentMode(mode) {
    if (!windowManager) {
        throw std::runtime_error("Invalid window manager");
    }
    
    InitializeBackend();
}

GUI::~GUI() noexcept {
    Stop();
}

void GUI::InitializeBackend() {
    switch (currentMode) {
        case Mode::X11:
            backend = std::make_unique<X11Backend>(theme);
            break;
        case Mode::GTK:
            backend = std::make_unique<GTKBackend>(theme);
            break;
        default:
            throw std::runtime_error("Invalid GUI mode");
    }
    
    backend->Initialize();
}

void GUI::CleanupBackend() noexcept {
    if (backend) {
        backend->Cleanup();
        backend.reset();
    }
}

void GUI::SwitchMode(Mode mode) {
    if (mode == currentMode) return;
    
    Stop();
    CleanupBackend();
    currentMode = mode;
    InitializeBackend();
    Start();
}

void GUI::Start() {
    if (running.exchange(true)) return;
    
    try {
        stopRequested.store(false);
        eventThread = std::make_unique<std::thread>(&GUI::ProcessEvents, this);
        backend->Show();
    } catch (...) {
        running.store(false);
        throw;
    }
}

void GUI::Stop() noexcept {
    if (!running.exchange(false)) return;
    
    try {
        stopRequested.store(true);
        eventQueue.Stop();
        
        if (eventThread && eventThread->joinable()) {
            eventThread->join();
        }
        
        backend->Hide();
        CleanupBackend();
        
    } catch (...) {
        // Log error but don't throw from destructor
    }
}

void GUI::ProcessEvents() {
    while (!stopRequested.load()) {
        try {
            backend->ProcessEvents();
            
            GUIEvent event;
            while (eventQueue.Pop(event)) {
                if (eventCallback) {
                    eventCallback(event);
                }
            }
        } catch (const std::exception& e) {
            // Log error and continue
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

// Delegate all window operations to backend
void GUI::ShowWindow() { backend->Show(); }
void GUI::HideWindow() { backend->Hide(); }
void GUI::SetWindowPosition(int x, int y) { backend->SetPosition(x, y); }
void GUI::SetWindowSize(int width, int height) { backend->SetSize(width, height); }
void GUI::SetTitle(const std::string& title) { backend->SetTitle(title); }
void GUI::SetOpacity(float opacity) { backend->SetOpacity(opacity); }
void GUI::QueueRedraw() { backend->QueueRedraw(); }

void GUI::SetTheme(const Glib::RefPtr<Gtk::Settings>& settings) {
    pImpl->settings = settings;
    RefreshTheme();
}

void GUI::RefreshTheme() {
    // Apply theme settings
    if (pImpl->settings) {
        pImpl->settings->property_gtk_theme_name() = "Adwaita";
        pImpl->settings->property_gtk_application_prefer_dark_theme() = true;
    }
}

void GUI::SetupSignals() {
    window->signal_hide().connect(
        sigc::mem_fun(*this, &GUI::OnWindowClosed));
}

void GUI::OnWindowClosed() {
    Stop();
}

} // namespace havel 

} // namespace havel 