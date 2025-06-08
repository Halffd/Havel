#include "Clipboard.hpp"

// Standard C++ includes
#include <stdexcept>
#include <vector>
#include <sstream>
#include <algorithm>
#include <unordered_map>
#include <functional>
#include <cstring>
#include <memory>
#include <chrono>
#include <thread>
#include <iostream>
#include <fstream>
#include <cstdint>
#include <cctype>
#include <iomanip>

// X11 headers must be included in the correct order
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/X.h>
#include <X11/keysym.h>
#include <X11/XF86keysym.h>

// Cairo for image handling
#include <cairo/cairo.h>

// For file operations
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

// For mmap
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

// Standard library includes
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <sstream>
#include <fstream>

// Helper function to convert selection enum to X11 atom
namespace {
    Atom GetSelectionAtom(havel::Clipboard::Selection selection, Display* display) {
        switch (selection) {
            case havel::Clipboard::Selection::PRIMARY:
                return XA_PRIMARY;
            case havel::Clipboard::Selection::SECONDARY: {
                static Atom secondary = XInternAtom(display, "SECONDARY", False);
                return secondary;
            }
            case havel::Clipboard::Selection::CLIPBOARD: {
                static Atom clipboard = XInternAtom(display, "CLIPBOARD", False);
                return clipboard;
            }
            default:
                return None;
        }
    }

    // Helper function to convert X11 atom to selection enum
    havel::Clipboard::Selection GetSelectionFromAtom(Atom atom, Display* display) {
        if (atom == XA_PRIMARY) {
            return havel::Clipboard::Selection::PRIMARY;
        }
        
        static Atom secondary = XInternAtom(display, "SECONDARY", False);
        if (atom == secondary) {
            return havel::Clipboard::Selection::SECONDARY;
        }
        
        static Atom clipboard = XInternAtom(display, "CLIPBOARD", False);
        if (atom == clipboard) {
            return havel::Clipboard::Selection::CLIPBOARD;
        }
        
        return havel::Clipboard::Selection::CLIPBOARD; // Default to CLIPBOARD
    }
    
    // Helper function to get format name as string
    std::string GetFormatName(havel::Clipboard::Format format) {
        switch (format) {
            case havel::Clipboard::Format::TEXT: return "TEXT";
            case havel::Clipboard::Format::HTML: return "HTML";
            case havel::Clipboard::Format::RTF: return "RTF";
            case havel::Clipboard::Format::IMAGE_PNG: return "IMAGE_PNG";
            case havel::Clipboard::Format::IMAGE_JPEG: return "IMAGE_JPEG";
            case havel::Clipboard::Format::IMAGE_BMP: return "IMAGE_BMP";
            case havel::Clipboard::Format::URI_LIST: return "URI_LIST";
            case havel::Clipboard::Format::FILE_LIST: return "FILE_LIST";
            case havel::Clipboard::Format::CUSTOM: return "CUSTOM";
            default: return "UNKNOWN";
        }
    }
    
    // Helper function to convert string to format
    havel::Clipboard::Format GetFormatFromName(const std::string& name) {
        static const std::unordered_map<std::string, havel::Clipboard::Format> formatMap = {
            {"TEXT", havel::Clipboard::Format::TEXT},
            {"HTML", havel::Clipboard::Format::HTML},
            {"RTF", havel::Clipboard::Format::RTF},
            {"IMAGE_PNG", havel::Clipboard::Format::IMAGE_PNG},
            {"IMAGE_JPEG", havel::Clipboard::Format::IMAGE_JPEG},
            {"IMAGE_BMP", havel::Clipboard::Format::IMAGE_BMP},
            {"URI_LIST", havel::Clipboard::Format::URI_LIST},
            {"FILE_LIST", havel::Clipboard::Format::FILE_LIST},
            {"CUSTOM", havel::Clipboard::Format::CUSTOM}
        };
        
        auto it = formatMap.find(name);
        return (it != formatMap.end()) ? it->second : havel::Clipboard::Format::UNKNOWN;
    }
    
    // Helper function to convert Cairo surface to PNG data
    bool SurfaceToPNG(cairo_surface_t* surface, std::vector<uint8_t>& pngData) {
        if (!surface || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
            return false;
        }
        
        // Create a memory write context
        cairo_surface_t* pngSurface = nullptr;
        cairo_status_t status;
        
        // Use a lambda function as the write function
        auto writeFunc = [](void* closure, const unsigned char* data, unsigned int length) -> cairo_status_t {
            std::vector<uint8_t>* pngData = static_cast<std::vector<uint8_t>*>(closure);
            pngData->insert(pngData->end(), data, data + length);
            return CAIRO_STATUS_SUCCESS;
        };
        
        // Create a surface that writes to our vector
        pngSurface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 
                                              cairo_image_surface_get_width(surface),
                                              cairo_image_surface_get_height(surface));
        
        if (cairo_surface_status(pngSurface) != CAIRO_STATUS_SUCCESS) {
            if (pngSurface) cairo_surface_destroy(pngSurface);
            return false;
        }
        
        // Copy the source surface to our new surface
        cairo_t* cr = cairo_create(pngSurface);
        cairo_set_source_surface(cr, surface, 0, 0);
        cairo_paint(cr);
        
        // Write PNG data to our vector
        status = cairo_surface_write_to_png_stream(pngSurface, writeFunc, &pngData);
        
        // Cleanup
        cairo_destroy(cr);
        cairo_surface_destroy(pngSurface);
        
        return status == CAIRO_STATUS_SUCCESS;
    }
    
    // Helper function to create a Cairo surface from PNG data
    cairo_surface_t* CreateSurfaceFromPNG(const std::vector<uint8_t>& pngData) {
        if (pngData.empty()) {
            return nullptr;
        }
        
        // Create a memory read context
        struct ReadContext {
            const uint8_t* data;
            size_t size;
            size_t offset;
        };
        
        ReadContext ctx{ pngData.data(), pngData.size(), 0 };
        
        // Read function for cairo
        auto readFunc = [](void* closure, unsigned char* data, unsigned int length) -> cairo_status_t {
            ReadContext* ctx = static_cast<ReadContext*>(closure);
            if (ctx->offset + length > ctx->size) {
                return CAIRO_STATUS_READ_ERROR;
            }
            
            std::memcpy(data, ctx->data + ctx->offset, length);
            ctx->offset += length;
            return CAIRO_STATUS_SUCCESS;
        };
        
        // Create the surface from PNG data
        cairo_surface_t* surface = cairo_image_surface_create_from_png_stream(readFunc, &ctx);
        if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
            if (surface) cairo_surface_destroy(surface);
            return nullptr;
        }
        
        return surface;
    }
    
    // Helper function to get MIME type from format
    const char* GetMimeType(havel::Clipboard::Format format) {
        switch (format) {
            case havel::Clipboard::Format::TEXT: return "text/plain";
            case havel::Clipboard::Format::HTML: return "text/html";
            case havel::Clipboard::Format::RTF: return "text/rtf";
            case havel::Clipboard::Format::IMAGE_PNG: return "image/png";
            case havel::Clipboard::Format::IMAGE_JPEG: return "image/jpeg";
            case havel::Clipboard::Format::IMAGE_BMP: return "image/bmp";
            case havel::Clipboard::Format::URI_LIST: return "text/uri-list";
            case havel::Clipboard::Format::FILE_LIST: return "text/uri-list";
            default: return "application/octet-stream";
        }
    }
    
    // Helper function to get the current timestamp
    Time GetCurrentTime(Display* /*display*/) {
        return CurrentTime; // X11's CurrentTime is 0L
    }
    
    // Helper function to wait for a selection notify event
    bool WaitForNotify(Display* display, Window window, Atom selection, Atom target, Time /*time*/) {
        XEvent event;
        XSync(display, False);
        
        // Wait for the selection notify event with a timeout
        for (int i = 0; i < 50; ++i) { // 50 * 20ms = 1 second timeout
            if (XCheckTypedWindowEvent(display, window, SelectionNotify, &event)) {
                XSelectionEvent* sev = reinterpret_cast<XSelectionEvent*>(&event.xselection);
                if (sev->selection == selection && sev->target == target) {
                    return true;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return false;
    }
    
    // Helper function to read property from window
    std::vector<unsigned char> GetWindowProperty(Display* display, Window window, 
                                                Atom property, Atom* type_ret = nullptr, 
                                                int* format_ret = nullptr) {
        Atom actual_type;
        int actual_format;
        unsigned long nitems, bytes_after;
        unsigned char* data = nullptr;
        
        // First get the size of the property
        if (XGetWindowProperty(display, window, property, 0, 0, False, 
                              AnyPropertyType, &actual_type, &actual_format, 
                              &nitems, &bytes_after, &data) != Success) {
            return {};
        }
        
        if (data) {
            XFree(data);
            data = nullptr;
        }
        
        if (nitems == 0 || bytes_after == 0) {
            return {};
        }
        
        // Now get the actual data
        if (XGetWindowProperty(display, window, property, 0, nitems + (bytes_after / 4), 
                              False, AnyPropertyType, &actual_type, &actual_format, 
                              &nitems, &bytes_after, &data) != Success) {
            return {};
        }
        
        std::vector<unsigned char> result(data, data + (nitems * (actual_format / 8)));
        
        if (type_ret) *type_ret = actual_type;
        if (format_ret) *format_ret = actual_format;
        
        XFree(data);
        return result;
    }
    
    // Helper function to convert string to atom
    Atom GetAtom(Display* display, const std::string& name, bool onlyIfExists = false) {
        return XInternAtom(display, name.c_str(), onlyIfExists ? True : False);
    }
    
    // Helper function to get atom name
    std::string GetAtomName(Display* display, Atom atom) {
        if (atom == None) return "None";
        
        char* name = XGetAtomName(display, atom);
        if (!name) return "";
        
        std::string result(name);
        XFree(name);
        return result;
    }
    
    // Helper function to check if a window is valid
    bool IsWindowValid(Display* display, Window window) {
        if (window == None) return false;
        
        XWindowAttributes attrs;
        return XGetWindowAttributes(display, window, &attrs) != 0;
    }
    
    // Helper function to get the current timestamp
    Time GetTimestamp(Display* display) {
        XEvent event;
        Window root = DefaultRootWindow(display);
        Window win;
        int x, y;
        unsigned int mask;
        
        // Get the current pointer position to generate a timestamp
        if (XQueryPointer(display, root, &root, &win, &x, &y, &x, &y, &mask)) {
            return event.xbutton.time;
        }
        
        // Fallback to current server time
        XSelectInput(display, root, PropertyChangeMask);
        XChangeProperty(display, root, XA_WM_NAME, XA_STRING, 8, PropModeAppend, nullptr, 0);
        XFlush(display);
        
        XNextEvent(display, &event);
        return event.xproperty.time;
    }
    
    // Helper function to convert a vector of strings to a single string with newlines
    std::string JoinStrings(const std::vector<std::string>& strings, const std::string& delimiter = "\n") {
        if (strings.empty()) return "";
        
        std::string result;
        for (size_t i = 0; i < strings.size(); ++i) {
            if (i > 0) result += delimiter;
            result += strings[i];
        }
        return result;
    }
    
    // Helper function to split a string by delimiter
    std::vector<std::string> SplitString(const std::string& str, char delimiter) {
        std::vector<std::string> result;
        std::stringstream ss(str);
        std::string item;
        
        while (std::getline(ss, item, delimiter)) {
            if (!item.empty()) {
                result.push_back(item);
            }
        }
        
        return result;
    }
    
    // Helper function to trim whitespace from string
    std::string TrimString(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\n\r\f\v");
        if (first == std::string::npos) return "";
        
        size_t last = str.find_last_not_of(" \t\n\r\f\v");
        return str.substr(first, (last - first + 1));
    }
    
    // Helper function to check if a string starts with a prefix
    bool StartsWith(const std::string& str, const std::string& prefix) {
        return str.size() >= prefix.size() && 
               str.compare(0, prefix.size(), prefix) == 0;
    }
    
    // Helper function to check if a string ends with a suffix
    bool EndsWith(const std::string& str, const std::string& suffix) {
        return str.size() >= suffix.size() && 
               str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
    }
    
    // Helper function to convert string to lowercase
    std::string ToLower(const std::string& str) {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(),
                      [](unsigned char c) { return std::tolower(c); });
        return result;
    }
    
    // Helper function to convert string to uppercase
    std::string ToUpper(const std::string& str) {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(),
                     [](unsigned char c) { return std::toupper(c); });
        return result;
    }
    
    // Helper function to URL encode a string
    std::string URLEncode(const std::string& value) {
        std::ostringstream escaped;
        escaped.fill('0');
        escaped << std::hex;

        for (std::string::const_iterator i = value.begin(), n = value.end(); i != n; ++i) {
            std::string::value_type c = (*i);

            // Keep alphanumeric and other accepted characters intact
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/' || c == '\\') {
                escaped << c;
                continue;
            }

            // Any other characters are percent-encoded
            escaped << '%' << std::setw(2) << int((unsigned char)c);
        }

        return escaped.str();
    }

    // Helper function to URL decode a string
    std::string URLDecode(const std::string& encoded) {
        std::string result;
        result.reserve(encoded.size());
        
        for (size_t i = 0; i < encoded.size(); ++i) {
            if (encoded[i] == '%' && i + 2 < encoded.size()) {
                // Try to decode percent-encoded sequence
                char hex[3] = {encoded[i+1], encoded[i+2], '\0'};
                char* end = nullptr;
                long value = std::strtol(hex, &end, 16);
                if (*end == '\0') {
                    result += static_cast<char>(value);
                    i += 2; // Skip the next two characters
                    continue;
                }
            } else if (encoded[i] == '+') {
                // Handle + as space
                result += ' ';
                continue;
            }
            result += encoded[i];
        }
        
        return result;
    }
}
// Standard C++ includes
#include <stdexcept>
#include <vector>
#include <sstream>
#include <algorithm>
#include <unordered_map>
#include <functional>
#include <cstring>
#include <memory>
#include <chrono>
#include <thread>
#include <iostream>
#include <fstream>
#include <cstdint>

// For file operations
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

// For mmap
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

namespace havel {

/**
 * @brief Internal structure to hold clipboard data in various formats
 */
struct ClipboardData {
    std::string text_data; ///< Text representation of the data
    std::vector<uint8_t> binary_data; ///< Binary representation of the data
    std::vector<std::string> file_paths; ///< File paths for file lists
    havel::Clipboard::Format format; ///< Format type of the data
    std::string mime_type; ///< MIME type for the data

    ClipboardData() : format(havel::Clipboard::Format::TEXT) {}

    const std::string& data() const { return text_data; }
    std::string& data() { return text_data; }
    const std::vector<uint8_t>& get_binary_data() const { return binary_data; }
    bool empty() const { 
        return text_data.empty() && binary_data.empty() && file_paths.empty(); 
    }
};

/**
 * @brief Private implementation details for Clipboard class
 */
struct Clipboard::Impl {
    Display* display{nullptr}; 
    Window window{0}; 

    // Selection data storage
    std::unordered_map<Selection, std::vector<ClipboardData>> data;
    std::unordered_map<Selection, bool> ownsSelection;

    // X11 Atoms for various data formats
    struct {
        Atom targets{0};
        Atom text{0};  // Added missing text atom
        Atom utf8String{0};
        Atom plainText{0};
        Atom pngImage{0};
        Atom jpegImage{0};
        Atom bmpImage{0};
        Atom uriList{0};
        Atom html{0};
        Atom rtf{0};
        Atom clipboard{0};
        Atom primary{0};
        Atom secondary{0};
        Atom timestamp{0};
        Atom multiple{0};
    } atoms;

    std::unordered_map<std::string, Atom> customFormats;
    std::vector<ChangeCallback> changeListeners;
    static constexpr int SELECTION_TIMEOUT_MS = 1000;

    Atom getSelectionAtom(Selection selection) const {
        switch (selection) {
            case Selection::CLIPBOARD: return atoms.clipboard;
            case Selection::PRIMARY: return atoms.primary;
            case Selection::SECONDARY: return atoms.secondary;
            default: return None;
        }
    }
};

// Static instance management
Clipboard& Clipboard::Instance() {
    static Clipboard instance;
    return instance;
}

Clipboard::Clipboard() : pImpl(std::make_unique<Impl>()) {
    try {
        initializeX11();
        initializeAtoms();
        createWindow();
    } catch (const std::exception& e) {
        cleanup();
        throw;
    }
}

Clipboard::~Clipboard() {
    cleanup();
}

void Clipboard::initializeX11() {
    pImpl->display = XOpenDisplay(nullptr);
    if (!pImpl->display) {
        throw std::runtime_error("Failed to open X11 display");
    }

    // Create a hidden window for clipboard handling
    int screen = DefaultScreen(pImpl->display);
    Window root = DefaultRootWindow(pImpl->display);
    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    attrs.event_mask = PropertyChangeMask | StructureNotifyMask | ExposureMask | 
                      SelectionClear | SelectionRequest | SelectionNotify;

    pImpl->window = XCreateWindow(
        pImpl->display, root,
        -100, -100, 1, 1, 0,  // x, y, width, height, border_width
        CopyFromParent,        // depth
        CopyFromParent,        // class
        CopyFromParent,        // visual
        CWOverrideRedirect | CWEventMask, &attrs);

    if (!pImpl->window) {
        XCloseDisplay(pImpl->display);
        pImpl->display = nullptr;
        throw std::runtime_error("Failed to create X11 window");
    }

    // Initialize X11 atoms
    pImpl->atoms.primary = XA_PRIMARY;
    pImpl->atoms.secondary = XInternAtom(pImpl->display, "SECONDARY", False);
    pImpl->atoms.clipboard = XInternAtom(pImpl->display, "CLIPBOARD", False);
    pImpl->atoms.utf8String = XInternAtom(pImpl->display, "UTF8_STRING", False);
    pImpl->atoms.targets = XInternAtom(pImpl->display, "TARGETS", False);
    pImpl->atoms.text = XA_STRING;
    pImpl->atoms.html = XInternAtom(pImpl->display, "text/html", False);
    pImpl->atoms.uriList = XInternAtom(pImpl->display, "text/uri-list", False);
    
    // Register for selection events
    XSelectInput(pImpl->display, pImpl->window, PropertyChangeMask | StructureNotifyMask | 
                ExposureMask | SelectionClear | SelectionRequest | SelectionNotify);
    XFlush(pImpl->display);
}

void Clipboard::initializeAtoms() {
    if (!pImpl->display) {
        throw std::runtime_error("Display not initialized");
    }

    pImpl->atoms.pngImage = XInternAtom(pImpl->display, "image/png", False);
    pImpl->atoms.jpegImage = XInternAtom(pImpl->display, "image/jpeg", False);
    pImpl->atoms.bmpImage = XInternAtom(pImpl->display, "image/bmp", False);
    pImpl->atoms.rtf = XInternAtom(pImpl->display, "text/rtf", False);
    pImpl->atoms.timestamp = XInternAtom(pImpl->display, "TIMESTAMP", False);
    pImpl->atoms.multiple = XInternAtom(pImpl->display, "MULTIPLE", False);
}

void Clipboard::createWindow() {
    if (!pImpl->display) {
        throw std::runtime_error("Display not initialized");
    }

    Window root = DefaultRootWindow(pImpl->display);
    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    attrs.event_mask = PropertyChangeMask | StructureNotifyMask | ExposureMask;

    pImpl->window = XCreateWindow(
        pImpl->display, root,
        -1, -1, 1, 1, 0,
        CopyFromParent, InputOutput, CopyFromParent,
        CWOverrideRedirect | CWEventMask,
        &attrs
    );

    if (!pImpl->window) {
        throw std::runtime_error("Failed to create clipboard window");
    }
}

void Clipboard::cleanup() {
    if (pImpl && pImpl->display) {
        if (pImpl->window) {
            XDestroyWindow(pImpl->display, pImpl->window);
            pImpl->window = 0;
        }
        XCloseDisplay(pImpl->display);
        pImpl->display = nullptr;
    }
}

// Text operations
void Clipboard::SetText(const std::string& text) {
    SetText(text, Selection::CLIPBOARD);
}

// Text operations
std::string Clipboard::GetText() const {
    return GetText(Selection::CLIPBOARD);
}

std::string Clipboard::GetText(Selection selection) const {
    if (!pImpl->display) {
        return "";
    }

    auto it = pImpl->data.find(selection);
    if (it != pImpl->data.end() && !it->second.empty()) {
        return it->second[0].text_data;
    }
    
    // Request text from X11 selection
    Atom selectionAtom = pImpl->getSelectionAtom(selection);
    return requestSelectionData(selectionAtom, pImpl->atoms.utf8String);
}

// File operations
std::vector<std::string> Clipboard::GetFiles(Selection selection) const {
    if (!pImpl || !pImpl->display) return {};
    
    auto it = pImpl->data.find(selection);
    if (it != pImpl->data.end()) {
        for (const auto& data : it->second) {
            if (data.format == Format::URI_LIST || data.format == Format::FILE_LIST) {
                if (!data.file_paths.empty()) {
                    return data.file_paths;
                }
                
                // Fall back to parsing the text data
                if (!data.text_data.empty()) {
                    std::vector<std::string> files;
                    parseURIList(data.text_data, files);
                    return files;
                }
            }
        }
    }
    
    // Try to get files from X11 selection
    std::string uriList = requestSelectionData(pImpl->getSelectionAtom(selection), pImpl->atoms.uriList);
    if (!uriList.empty()) {
        std::vector<std::string> files;
        parseURIList(uriList, files);
        return files;
    }
    
    return {};
}

void Clipboard::parseURIList(const std::string& uriData, std::vector<std::string>& files) const {
    files.clear();
    
    std::istringstream stream(uriData);
    std::string line;
    while (std::getline(stream, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') continue;
        
        // Remove any trailing whitespace and CR characters
        line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
        line = line.substr(0, line.find_last_not_of(" \t\n\r\f\v") + 1);
        
        // Skip empty lines after trimming
        if (line.empty()) continue;
        
        // Check if the line is a valid URI (starts with file://)
        if (line.rfind("file://", 0) == 0) {
            std::string path = line.substr(7); // Remove file:// prefix
            // Handle localhost prefix if present
            if (path.rfind("localhost", 0) == 0) {
                path = path.substr(9);
            }
            // URL decode the path
            files.push_back(URLDecode(path));
        } else {
            // Assume it's a plain file path
            files.push_back(line);
        }
    }
}

void Clipboard::SetFiles(const std::vector<std::string>& paths, Selection selection) {
    if (!pImpl || !pImpl->display) return;
    
    // Convert paths to URI list format
    std::string uriList;
    std::vector<std::string> validPaths;
    
    for (const auto& path : paths) {
        if (path.empty()) continue;
        
        validPaths.push_back(path);
        
        // Check if path is already a file URI
        if (path.find("file://") == 0) {
            uriList += path + "\r\n";
        } else {
            // Convert local path to properly encoded file URI
            std::string encodedPath = path;
            // Only encode the path part, not the protocol
            size_t protoPos = path.find("://");
            if (protoPos != std::string::npos) {
                // If there's already a protocol, only encode the part after ://
                std::string proto = path.substr(0, protoPos + 3);
                std::string pathPart = path.substr(protoPos + 3);
                encodedPath = proto + URLEncode(pathPart);
            } else {
                // No protocol, encode the whole path
                encodedPath = "file://" + URLEncode(path);
            }
            uriList += encodedPath + "\r\n";
        }
    }
    
    if (uriList.empty()) return;
    
    // Remove trailing newline
    if (uriList.size() >= 2) {
        uriList = uriList.substr(0, uriList.length() - 2);
    }
    
    // Store the URI list
    ClipboardData data;
    data.format = Format::URI_LIST;
    data.text_data = uriList;
    data.file_paths = validPaths; // Store original paths for easy access
    
    pImpl->data[selection] = {std::move(data)};
    
    // Take ownership of the selection
    Atom selectionAtom = pImpl->getSelectionAtom(selection);
    XSetSelectionOwner(pImpl->display, selectionAtom, pImpl->window, CurrentTime);
    pImpl->ownsSelection[selection] = true;
    
    NotifyListeners(selection, Format::URI_LIST);
}

void Clipboard::SetText(const std::string& text, Selection selection) {
    if (!pImpl->display) {
        throw std::runtime_error("Clipboard not initialized");
    }

    Atom selAtom = pImpl->getSelectionAtom(selection);
    if (selAtom == None) {
        throw std::invalid_argument("Invalid selection type");
    }

    try {
        ClipboardData clipData;
        clipData.text_data = text;
        clipData.format = Format::TEXT;
        clipData.mime_type = "text/plain";

        auto& dataList = pImpl->data[selection];
        dataList.clear();
        dataList.push_back(std::move(clipData));

        XSetSelectionOwner(pImpl->display, selAtom, pImpl->window, CurrentTime);

        Window owner = XGetSelectionOwner(pImpl->display, selAtom);
        if (owner == pImpl->window) {
            pImpl->ownsSelection[selection] = true;
            NotifyListeners(selection, Format::TEXT);
        } else {
            pImpl->ownsSelection[selection] = false;
            throw std::runtime_error("Failed to acquire selection ownership");
        }

        XFlush(pImpl->display);

    } catch (const std::exception& e) {
        pImpl->ownsSelection[selection] = false;
        throw std::runtime_error(std::string("Failed to set text: ") + e.what());
    }
}

// Event handling
void Clipboard::HandleSelectionRequest(XSelectionRequestEvent* event) {
    if (!pImpl->display) return;

    XSelectionEvent response;
    response.type = SelectionNotify;
    response.display = event->display;
    response.requestor = event->requestor;
    response.selection = event->selection;
    response.target = event->target;
    response.property = None;
    response.time = event->time;

    try {
        Selection selection = GetSelectionFromAtom(event->selection, pImpl->display);
        auto& dataList = pImpl->data[selection];

        if (event->target == pImpl->atoms.targets) {
            // Handle TARGETS request
            std::vector<Atom> targets = {
                pImpl->atoms.targets,
                pImpl->atoms.utf8String,
                pImpl->atoms.text
            };
            
            XChangeProperty(pImpl->display, event->requestor, event->property, 
                          XA_ATOM, 32, PropModeReplace, 
                          reinterpret_cast<const unsigned char*>(targets.data()), 
                          targets.size());
            response.property = event->property;
        } 
        else if (!dataList.empty()) {
            // Handle data requests
            const auto& data = dataList[0];
            
            if ((event->target == pImpl->atoms.text || 
                 event->target == XA_STRING || 
                 event->target == pImpl->atoms.utf8String) && 
                !data.text_data.empty()) {
                // Handle text request
                const char* text = data.text_data.c_str();
                XChangeProperty(pImpl->display, event->requestor, event->property,
                              event->target, 8, PropModeReplace,
                              reinterpret_cast<const unsigned char*>(text), 
                              data.text_data.length());
                response.property = event->property;
            }
            else if (event->target == pImpl->atoms.uriList && 
                    data.format == Format::FILE_LIST) {
                // Handle URI list request
                std::string uriList;
                for (const auto& path : data.file_paths) {
                    uriList += "file://" + path + "\r\n";
                }
                XChangeProperty(pImpl->display, event->requestor, event->property,
                              pImpl->atoms.utf8String, 8, PropModeReplace,
                              reinterpret_cast<const unsigned char*>(uriList.c_str()),
                              uriList.length());
                response.property = event->property;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error handling selection request: " << e.what() << std::endl;
    }

    XSendEvent(pImpl->display, event->requestor, False, 0, 
              reinterpret_cast<XEvent*>(&response));
    XFlush(pImpl->display);
}

void Clipboard::HandleSelectionNotify(XSelectionEvent* event) {
    if (!pImpl->display || !event) return;
    // Handle selection notify event if needed
}

void Clipboard::HandleSelectionClear(XSelectionClearEvent* event) {
    if (!pImpl->display || !event) return;
    
    try {
        Selection sel = GetSelectionFromAtom(event->selection, pImpl->display);
        pImpl->ownsSelection[sel] = false;
        pImpl->data[sel].clear();
        NotifyListeners(sel, Format::UNKNOWN);
    if (fromFormat == toFormat) {
        result = data;
        return true;
    }
    
    // Add format conversion logic here
    // For now, we only support direct copy
    return false;
}

Clipboard::Format Clipboard::atomToFormat(Atom atom) const {
    if (!pImpl) return Format::TEXT;
    if (atom == None) return Format::TEXT;
    
    // Check standard formats
    if (atom == pImpl->atoms.utf8String || atom == pImpl->atoms.plainText) {
        return Format::TEXT;
    } else if (atom == pImpl->atoms.html) {
        return Format::HTML;
    } else if (atom == pImpl->atoms.rtf) {
        return Format::RTF;
    } else if (atom == pImpl->atoms.pngImage) {
        return Format::IMAGE_PNG;
    } else if (atom == pImpl->atoms.jpegImage) {
        return Format::IMAGE_JPEG;
    } else if (atom == pImpl->atoms.bmpImage) {
        return Format::IMAGE_BMP;
    } else if (atom == pImpl->atoms.uriList) {
        return Format::URI_LIST;
    }
    
    // Check custom formats
    for (const auto& [name, customAtom] : pImpl->customFormats) {
        if (customAtom == atom) {
            return Format::CUSTOM;
        }
    }
    
    return Format::UNKNOWN;
}

void Clipboard::HandlePropertyNotify(XPropertyEvent* event) {
    if (!pImpl || !pImpl->display || !event) return;
    
    // Handle property change
    XEvent event_return;
    if (XCheckTypedEvent(pImpl->display, PropertyNotify, &event_return)) {
        // Handle property change event
    }
}

void Clipboard::NotifyListeners(Selection selection, Format format) const {
    if (!pImpl) return;
    
    for (const auto& listener : pImpl->changeListeners) {
        try {
            if (listener) {
                listener(selection, format);
            }
        } catch (const std::exception& e) {
            std::cerr << "Error in clipboard listener: " << e.what() << std::endl;
        }
    }
}

void Clipboard::RegisterFormat(const std::string& format) {
    if (!pImpl || !pImpl->display) return;
    
    if (pImpl->customFormats.find(format) == pImpl->customFormats.end()) {
        Atom atom = XInternAtom(pImpl->display, format.c_str(), False);
        if (atom != None) {
            pImpl->customFormats[format] = atom;
        }
    }
}

void Clipboard::AddChangeListener(const ChangeCallback& callback) {
    if (!pImpl) return;
    
    if (callback) {
        pImpl->changeListeners.push_back(callback);
    }
}

void Clipboard::RemoveChangeListener(const ChangeCallback& callback) {
    if (!pImpl) return;
    
    auto& listeners = pImpl->changeListeners;
    if (callback) {
        listeners.erase(
            std::remove_if(listeners.begin(), listeners.end(),
                [&](const ChangeCallback& cb) {
                    return cb.target_type() == callback.target_type();
                }),
            listeners.end()
        );
    }
}

Atom Clipboard::formatToAtom(Format format) const {
    if (!pImpl) return None;
    
    switch (format) {
        case Format::TEXT: return pImpl->atoms.utf8String;
        case Format::HTML: return pImpl->atoms.html;
        case Format::RTF: return pImpl->atoms.rtf;
        case Format::IMAGE_PNG: return pImpl->atoms.pngImage;
        case Format::IMAGE_JPEG: return pImpl->atoms.jpegImage;
        case Format::IMAGE_BMP: return pImpl->atoms.bmpImage;
        case Format::URI_LIST:
        case Format::FILE_LIST: return pImpl->atoms.uriList;
        case Format::CUSTOM: return None; // Must be handled by custom format lookup
        default: return None;
    }
}

// Selection request handlers
void Clipboard::handleTargetsRequest(XSelectionRequestEvent* event, XSelectionEvent& response) {
    if (!pImpl || !pImpl->display) return;
    
    std::vector<Atom> targets;
    
    // Always support TARGETS
    targets.push_back(pImpl->atoms.targets);
    
    // Add supported formats based on what we have
    Selection sel = (event->selection == pImpl->atoms.primary) ? Selection::PRIMARY :
                   (event->selection == pImpl->atoms.secondary) ? Selection::SECONDARY :
                   Selection::CLIPBOARD;
    
    auto it = pImpl->data.find(sel);
    if (it != pImpl->data.end()) {
        for (const auto& data : it->second) {
            Atom atom = formatToAtom(data.format);
            if (atom != None && std::find(targets.begin(), targets.end(), atom) == targets.end()) {
                targets.push_back(atom);
            }
        }
    }

    // Set the property with our supported targets
    XChangeProperty(
        event->display, event->requestor, event->property,
        XA_ATOM, 32, PropModeReplace,
        reinterpret_cast<unsigned char*>(targets.data()),
        targets.size()
    );
}

void Clipboard::handleTextRequest(XSelectionRequestEvent* event, XSelectionEvent& response, Selection selection) {
    if (!pImpl || !pImpl->display) return;
    
    auto it = pImpl->data.find(selection);
    if (it != pImpl->data.end() && !it->second.empty()) {
        const std::string& text = it->second[0].text_data;
        
        XChangeProperty(
            event->display, event->requestor, event->property,
            event->target, 8, PropModeReplace,
            reinterpret_cast<const unsigned char*>(text.c_str()),
            text.length()
        );
    } else {
        response.property = None;
    }
}

void Clipboard::handleImageRequest(XSelectionRequestEvent* event, XSelectionEvent& response, Selection selection) {
    if (!pImpl || !pImpl->display) return;
    
    auto it = pImpl->data.find(selection);
    if (it != pImpl->data.end()) {
        for (const auto& data : it->second) {
            if (data.format == Format::IMAGE_PNG && !data.binary_data.empty()) {
                XChangeProperty(
                    event->display, event->requestor, event->property,
                    event->target, 8, PropModeReplace,
                    data.binary_data.data(),
                    data.binary_data.size()
                );
                return;
            }
        }
    }
    response.property = None;
}

// Handle file list selection request
void Clipboard::handleFileListRequest(XSelectionRequestEvent* event, XSelectionEvent& response, Selection selection) {
    if (!pImpl || !pImpl->display) return;
    
    auto it = pImpl->data.find(selection);
    if (it != pImpl->data.end()) {
        for (const auto& data : it->second) {
            if ((data.format == Format::URI_LIST || data.format == Format::FILE_LIST) && 
                !data.text_data.empty()) {
                XChangeProperty(
                    event->display, event->requestor, event->property,
                    event->target, 8, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(data.text_data.c_str()),
                    data.text_data.length()
                );
                return;
            }
        }
    }
    response.property = None;
}

// Handle HTML selection request
void Clipboard::handleHTMLRequest(XSelectionRequestEvent* event, XSelectionEvent& response, Selection selection) {
    if (!pImpl || !pImpl->display) return;
    
    auto it = pImpl->data.find(selection);
    if (it != pImpl->data.end()) {
        for (const auto& data : it->second) {
            if (data.format == Format::HTML && !data.text_data.empty()) {
                XChangeProperty(
                    event->display, event->requestor, event->property,
                    event->target, 8, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(data.text_data.c_str()),
                    data.text_data.length()
                );
                return;
            }
        }
    }
    response.property = None;
}

// Image operations
void Clipboard::SetImage(cairo_surface_t* surface) {
    SetImage(surface, Selection::CLIPBOARD);
}

void Clipboard::SetImage(cairo_surface_t* surface, Selection selection) {
    if (!pImpl || !pImpl->display || !surface) return;
    
    std::vector<uint8_t> pngData;
    if (SurfaceToPNG(surface, pngData)) {
        ClipboardData data;
        data.format = Format::IMAGE_PNG;
        data.binary_data = std::move(pngData);
        pImpl->data[selection] = {std::move(data)};
        
        // Take ownership of the selection
        Atom selectionAtom = pImpl->getSelectionAtom(selection);
        XSetSelectionOwner(pImpl->display, selectionAtom, pImpl->window, CurrentTime);
        pImpl->ownsSelection[selection] = true;
        
        NotifyListeners(selection, Format::IMAGE_PNG);
    }
}

std::unique_ptr<cairo_surface_t, void(*)(cairo_surface_t*)> Clipboard::GetImage() const {
    return GetImage(Selection::CLIPBOARD);
}

std::unique_ptr<cairo_surface_t, void(*)(cairo_surface_t*)> Clipboard::GetImage(Selection selection) const {
    if (!pImpl || !pImpl->display) return {nullptr, nullptr};
    
    auto it = pImpl->data.find(selection);
    if (it != pImpl->data.end()) {
        for (const auto& data : it->second) {
            if (data.format == Format::IMAGE_PNG && !data.binary_data.empty()) {
                cairo_surface_t* surface = CreateSurfaceFromPNG(data.binary_data);
                if (surface) {
                    return {surface, cairo_surface_destroy};
                }
            }
        }
    }
    
    return {nullptr, nullptr};
}

// Rich text operations
void Clipboard::SetHTML(const std::string& html) {
    SetHTML(html, Selection::CLIPBOARD);
}

void Clipboard::SetHTML(const std::string& html, Selection selection) {
    if (!pImpl || !pImpl->display) return;
    
    ClipboardData data;
    data.format = Format::HTML;
    data.text_data = html;
    pImpl->data[selection] = {std::move(data)};
    
    // Take ownership of the selection
    Atom selectionAtom = pImpl->getSelectionAtom(selection);
    XSetSelectionOwner(pImpl->display, selectionAtom, pImpl->window, CurrentTime);
    pImpl->ownsSelection[selection] = true;
    
    NotifyListeners(selection, Format::HTML);
}

std::string Clipboard::GetHTML() const {
    return GetHTML(Selection::CLIPBOARD);
}

std::string Clipboard::GetHTML(Selection selection) const {
    if (!pImpl || !pImpl->display) return "";
    
    auto it = pImpl->data.find(selection);
    if (it != pImpl->data.end()) {
        for (const auto& data : it->second) {
            if (data.format == Format::HTML) {
                return data.text_data;
            }
        }
    }
    
    return "";
}

// Custom data operations
void Clipboard::SetData(const std::string& format, const std::vector<uint8_t>& data) {
    SetData(format, data, Selection::CLIPBOARD);
}

void Clipboard::SetData(const std::string& format, const std::vector<uint8_t>& data, Selection selection) {
    if (!pImpl || !pImpl->display || format.empty()) return;
    
    // Register the custom format if not already registered
    if (pImpl->customFormats.find(format) == pImpl->customFormats.end()) {
        RegisterFormat(format);
    }
    
    ClipboardData clipboardData;
    clipboardData.format = Format::CUSTOM;
    clipboardData.mime_type = format;
    clipboardData.binary_data = data;
    
    // Replace any existing data for this format
    auto& selectionData = pImpl->data[selection];
    selectionData.erase(
        std::remove_if(selectionData.begin(), selectionData.end(),
            [&](const ClipboardData& d) {
                return d.format == Format::CUSTOM && d.mime_type == format;
            }),
        selectionData.end()
    );
    
    selectionData.push_back(std::move(clipboardData));
    
    // Take ownership of the selection
    Atom selectionAtom = pImpl->getSelectionAtom(selection);
    XSetSelectionOwner(pImpl->display, selectionAtom, pImpl->window, CurrentTime);
    pImpl->ownsSelection[selection] = true;
    
    NotifyListeners(selection, Format::CUSTOM);
}

std::vector<uint8_t> Clipboard::GetData(const std::string& format) const {
    return GetData(format, Selection::CLIPBOARD);
}

std::vector<uint8_t> Clipboard::GetData(const std::string& format, Selection selection) const {
    if (!pImpl || !pImpl->display || format.empty()) return {};
    
    auto it = pImpl->data.find(selection);
    if (it != pImpl->data.end()) {
        for (const auto& data : it->second) {
            if (data.format == Format::CUSTOM && data.mime_type == format) {
                return data.binary_data;
            }
        }
    }
    
    return {};
}
} // namespace havel