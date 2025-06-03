#pragma once
#include <string>
#include <memory>
#include <vector>
#include <X11/Xlib.h>
#include <cairo/cairo.h>
#include <functional>
#define XA_CLIPBOARD(display) XInternAtom(display, "CLIPBOARD", False)
namespace havel {
class Clipboard {
public:
    // Singleton access
    static Clipboard& Instance();

    // Data format types
    enum class Format {
        TEXT,
        HTML,
        RTF,
        IMAGE_PNG,
        IMAGE_JPEG,
        IMAGE_BMP,
        URI_LIST,
        FILE_LIST,
        CUSTOM
    };
    
    // Selection types
    enum class Selection {
        CLIPBOARD,  // Ctrl+C/V clipboard
        PRIMARY,    // Mouse selection clipboard
        SECONDARY   // Rarely used
    };
    
    // Text operations
    void SetText(const std::string& text);
    std::string GetText() const;
    
    // Image operations
    void SetImage(cairo_surface_t* surface);
    std::unique_ptr<cairo_surface_t, void(*)(cairo_surface_t*)> GetImage() const;
    
    // File operations
    void SetFiles(const std::vector<std::string>& paths);
    std::vector<std::string> GetFiles() const;
    
    // HTML operations
    void SetHTML(const std::string& html);
    std::string GetHTML() const;
    
    // Custom format operations
    void SetData(const std::string& format, const std::vector<uint8_t>& data);
    std::vector<uint8_t> GetData(const std::string& format) const;
    
    // Multi-format operations
    void Clear(Selection selection = Selection::CLIPBOARD);
    bool HasFormat(Format format) const;
    std::vector<Format> GetAvailableFormats() const;
    
    // Advanced operations
    void SetText(const std::string& text, Selection selection);
    std::string GetText(Selection selection) const;
    void SetImage(cairo_surface_t* surface, Selection selection);
    void SetFiles(const std::vector<std::string>& paths, Selection selection);
    
    // Event handling
    void HandleSelectionRequest(XSelectionRequestEvent* event);
    void HandleSelectionNotify(XSelectionEvent* event);
    
    // Clipboard monitoring
    using ChangeCallback = std::function<void(Selection, Format)>;
    void AddChangeListener(ChangeCallback callback);
    void RemoveChangeListener(ChangeCallback callback);
    
private:
    Clipboard();
    ~Clipboard();
    Clipboard(const Clipboard&) = delete;
    Clipboard& operator=(const Clipboard&) = delete;
    
    struct Impl;
    std::unique_ptr<Impl> pImpl;
    
    // Helper methods
    void NotifyListeners(Selection selection, Format format);
    Atom GetFormatAtom(Format format) const;
    Format GetFormatFromAtom(Atom atom) const;
    void RegisterFormat(const std::string& format);
    bool ConvertData(const std::vector<uint8_t>& data, Format fromFormat, Format toFormat, std::vector<uint8_t>& result);
};

} // namespace havel 