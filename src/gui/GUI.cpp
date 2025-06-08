#include "GUI.hpp"
#include "Widget.hpp"
#include "Layout.hpp"
#include <gtkmm/cssprovider.h>
#include <gtkmm/main.h>
#include <gtkmm/window.h>
#include <gdk/gdk.h>
#include <stdexcept>
#include <iostream>

namespace havel {

// GUI Implementation class
class GUI::Impl {
public:
    Impl() = default;
    ~Impl() = default;
    
    // GTK-specific members
    Glib::RefPtr<Gtk::CssProvider> cssProvider;
    Glib::RefPtr<Gtk::Settings> settings;
    
    // Window management
    std::unique_ptr<Gtk::Window> mainWindow;
    std::unique_ptr<Gtk::Box> mainBox;
    std::unique_ptr<Gtk::MenuBar> menuBar;
    std::unique_ptr<Gtk::Notebook> notebook;
    
    // Initialize GTK components
    void InitializeGTK() {
        // Initialize GTK if not already initialized
        if (!Gtk::Main::instance()) {
            int argc = 0;
            char** argv = nullptr;
            Gtk::Main kit(argc, argv);
        }
        
        // Create main window
        mainWindow = std::make_unique<Gtk::Window>();
        mainWindow->set_title("Havel Window Manager");
        mainWindow->set_default_size(1024, 768);
        
        // Create main box
        mainBox = std::make_unique<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 0);
        mainWindow->add(*mainBox);
        
        // Create menu bar
        menuBar = std::make_unique<Gtk::MenuBar>();
        
        // Create file menu
        auto fileMenuItem = std::make_unique<Gtk::MenuItem>("_File", true);
        auto fileMenu = std::make_unique<Gtk::Menu>();
        
        // Create quit item
        auto quitItem = std::make_unique<Gtk::MenuItem>("_Quit", true);
        quitItem->signal_activate().connect([]() {
            Gtk::Main::quit();
        });
        
        fileMenu->append(*quitItem);
        fileMenuItem->set_submenu(*fileMenu);
        menuBar->append(*fileMenuItem);
        
        // Create notebook
        notebook = std::make_unique<Gtk::Notebook>();
        
        // Pack everything
        mainBox->pack_start(*menuBar, Gtk::PACK_SHRINK);
        mainBox->pack_start(*notebook);
        
        // Show all widgets
        mainWindow->show_all();
    }
    
    // Cleanup GTK resources
    void CleanupGTK() {
        if (mainWindow) {
            mainWindow->hide();
            mainWindow.reset();
        }
        notebook.reset();
        menuBar.reset();
        mainBox.reset();
    }
};

// GUI constructor
GUI::GUI(std::shared_ptr<WindowManager> wm, Mode mode)
    : windowManager(std::move(wm))
    , currentMode(mode)
    , pImpl(std::make_unique<Impl>()) {
    
    // Initialize the backend based on the specified mode
    InitializeBackend();
}

// GUI destructor
GUI::~GUI() {
    try {
        Stop();
        CleanupBackend();
    } catch (const std::exception& e) {
        std::cerr << "Error in GUI destructor: " << e.what() << std::endl;
    }
}

// Initialize the appropriate backend
void GUI::InitializeBackend() {
    if (pImpl) {
        pImpl->InitializeGTK();
    }
}

// Cleanup backend resources
void GUI::CleanupBackend() {
    if (pImpl) {
        pImpl->CleanupGTK();
    }
}
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

void GUI::SetTheme(const Theme& theme) {
    this->theme = theme;
    
    // Apply theme to all widgets
    for (const auto& widget : widgets) {
        if (widget) {
            widget->SetTheme(theme);
        }
    }
    
    // Queue a redraw
    QueueRedraw();
}

void GUI::SetupSignals() {
    window->signal_hide().connect(
        sigc::mem_fun(*this, &GUI::OnWindowClosed));
}

void GUI::OnWindowClosed() {
    Stop();
}

} // namespace havel