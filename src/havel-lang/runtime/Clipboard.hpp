#pragma once
#include <string>
#include <memory>
#include <vector>
#include <functional>

// Use system X11 headers for type definitions
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

// Forward declarations for Cairo
typedef struct _cairo_surface cairo_surface_t;

namespace havel {

/**
 * @brief Cross-platform clipboard manager with support for multiple data formats
 */
class Clipboard final {
public:
    /**
     * @brief Supported clipboard data formats
     */
    enum class Format {
        UNKNOWN = 0,
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
    
    /**
     * @brief X11 selection types
     */
    enum class Selection {
        CLIPBOARD = 0,  // Ctrl+C/V clipboard
        PRIMARY,        // Mouse selection clipboard
        SECONDARY       // Rarely used
    };
    
    /**
     * @brief Callback function type for clipboard change notifications
     */
    using ChangeCallback = std::function<void(Selection selection, Format format)>;

    // Singleton access
    static Clipboard& Instance();

    // Disable copy/move - singleton pattern
    Clipboard(const Clipboard&) = delete;
    Clipboard& operator=(const Clipboard&) = delete;
    Clipboard(Clipboard&&) = delete;
    Clipboard& operator=(Clipboard&&) = delete;

    ~Clipboard();

    // Basic text operations (use CLIPBOARD selection by default)
    void SetText(const std::string& text);
    std::string GetText() const;

    // Text operations with explicit selection
    void SetText(const std::string& text, Selection selection);
    std::string GetText(Selection selection) const;

    // Image operations
    void SetImage(cairo_surface_t* surface);
    void SetImage(cairo_surface_t* surface, Selection selection);
    std::unique_ptr<cairo_surface_t, void(*)(cairo_surface_t*)> GetImage() const;

    // File operations
    void SetFiles(const std::vector<std::string>& paths, Selection selection = Selection::CLIPBOARD);
    std::vector<std::string> GetFiles(Selection selection = Selection::CLIPBOARD) const;

    // Rich text operations
    void SetHTML(const std::string& html);
    std::string GetHTML() const;

    // Custom data operations
    void SetData(const std::string& format, const std::vector<uint8_t>& data);
    std::vector<uint8_t> GetData(const std::string& format) const;

    // Utility operations
    void Clear(Selection selection = Selection::CLIPBOARD);
    bool HasFormat(Format format) const;
    bool HasFormat(Format format, Selection selection) const;
    std::vector<Format> GetAvailableFormats() const;
    std::vector<Format> GetAvailableFormats(Selection selection) const;

    // Change notification
    void AddChangeListener(const ChangeCallback& callback);
    void RemoveChangeListener(const ChangeCallback& callback);

    // Custom format registration
    void RegisterFormat(const std::string& mimeType);

    // X11 event handling (call these from your event loop)
    void HandleSelectionRequest(XSelectionRequestEvent* event);
    void HandleSelectionNotify(XSelectionEvent* event);
    void HandleSelectionClear(XSelectionClearEvent* event);

private:
    // Private implementation
    struct Impl;
    std::unique_ptr<Impl> pImpl;

    // Private constructor for singleton
    Clipboard();

    // Initialization methods
    void initializeX11();
    void initializeAtoms();
    void createWindow();
    void cleanup();

    // Helper methods
    void NotifyListeners(Selection selection, Format format) const;
    std::string requestSelectionData(unsigned long selection, unsigned long target) const;
    std::vector<unsigned long> getAvailableTargets(unsigned long selection) const;
    unsigned long formatToAtom(Format format) const;
    Format atomToFormat(unsigned long atom) const;

    // Selection request handlers
    void handleTargetsRequest(XSelectionRequestEvent* event, XSelectionEvent& response);
    void handleTextRequest(XSelectionRequestEvent* event, XSelectionEvent& response, Selection selection);
    void handleImageRequest(XSelectionRequestEvent* event, XSelectionEvent& response, Selection selection);
    void handleFileListRequest(XSelectionRequestEvent* event, XSelectionEvent& response, Selection selection);
    void handleHTMLRequest(XSelectionRequestEvent* event, XSelectionEvent& response, Selection selection);

    // Image conversion helpers
    bool surfaceToPNG(cairo_surface_t* surface, std::vector<uint8_t>& pngData) const;
    std::unique_ptr<cairo_surface_t, void(*)(cairo_surface_t*)> pngToSurface(const std::vector<uint8_t>& pngData) const;

    // URI/file parsing helpers
    void parseURIList(const std::string& uriData, std::vector<std::string>& files) const;
    std::string urlDecode(const std::string& encoded) const;

    // X11 forward declarations for Atom type
    using Atom = unsigned long;
};

} // namespace havel