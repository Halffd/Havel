#include "Clipboard.hpp"
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <cairo/cairo.h>
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

namespace havel {

/**
 * @brief Internal structure to hold clipboard data in various formats
 */
struct ClipboardData {
    std::string text_data; ///< Text representation of the data
    std::vector<uint8_t> binary_data; ///< Binary representation of the data
    Clipboard::Format format; ///< Format type of the data
    std::string mime_type; ///< MIME type for the data

    ClipboardData() : format(Clipboard::Format::TEXT) {}

    const std::string& data() const { return text_data; }
    std::string& data() { return text_data; }
    const std::vector<uint8_t>& get_binary_data() const { return binary_data; }
    bool empty() const { return text_data.empty() && binary_data.empty(); }
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
        throw std::runtime_error("Failed to open X11 display connection");
    }
}

void Clipboard::initializeAtoms() {
    if (!pImpl->display) {
        throw std::runtime_error("Display not initialized");
    }

    pImpl->atoms.clipboard = XInternAtom(pImpl->display, "CLIPBOARD", False);
    pImpl->atoms.primary = XA_PRIMARY;
    pImpl->atoms.secondary = XA_SECONDARY;
    pImpl->atoms.targets = XInternAtom(pImpl->display, "TARGETS", False);
    pImpl->atoms.timestamp = XInternAtom(pImpl->display, "TIMESTAMP", False);
    pImpl->atoms.multiple = XInternAtom(pImpl->display, "MULTIPLE", False);
    pImpl->atoms.utf8String = XInternAtom(pImpl->display, "UTF8_STRING", False);
    pImpl->atoms.plainText = XInternAtom(pImpl->display, "TEXT", False);
    pImpl->atoms.pngImage = XInternAtom(pImpl->display, "image/png", False);
    pImpl->atoms.jpegImage = XInternAtom(pImpl->display, "image/jpeg", False);
    pImpl->atoms.bmpImage = XInternAtom(pImpl->display, "image/bmp", False);
    pImpl->atoms.uriList = XInternAtom(pImpl->display, "text/uri-list", False);
    pImpl->atoms.html = XInternAtom(pImpl->display, "text/html", False);
    pImpl->atoms.rtf = XInternAtom(pImpl->display, "text/rtf", False);
}

void Clipboard::createWindow() {
    if (!pImpl->display) {
        throw std::runtime_error("Display not initialized");
    }

    Window root = DefaultRootWindow(pImpl->display);
    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    attrs.event_mask = PropertyChangeMask | SelectionClearMask | SelectionRequestMask;

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

std::string Clipboard::GetText() const {
    return GetText(Selection::CLIPBOARD);
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

std::string Clipboard::GetText(Selection selection) const {
    if (!pImpl->display) {
        throw std::runtime_error("Clipboard not initialized");
    }

    Atom selAtom = pImpl->getSelectionAtom(selection); // FIXED: Initialize selAtom properly
    if (selAtom == None) {
        return "";
    }

    // If we own this selection, return our local copy
    if (pImpl->ownsSelection[selection]) {
        auto it = pImpl->data.find(selection);
        if (it != pImpl->data.end() && !it->second.empty()) {
            return it->second[0].data();
        }
    }

    // Request selection from external owner
    return requestSelectionData(selAtom, pImpl->atoms.utf8String);
}

void Clipboard::Clear(Selection selection) {
    SetText("", selection);
}

// Image operations
void Clipboard::SetImage(cairo_surface_t* surface) {
    SetImage(surface, Selection::CLIPBOARD);
}

void Clipboard::SetImage(cairo_surface_t* surface, Selection selection) {
    if (!surface) {
        throw std::invalid_argument("Surface cannot be null");
    }

    if (!pImpl->display) {
        throw std::runtime_error("Clipboard not initialized");
    }

    Atom selAtom = pImpl->getSelectionAtom(selection);
    if (selAtom == None) {
        throw std::invalid_argument("Invalid selection type");
    }

    try {
        std::vector<uint8_t> pngData;
        if (!surfaceToPNG(surface, pngData)) {
            throw std::runtime_error("Failed to convert surface to PNG");
        }

        ClipboardData clipData;
        clipData.binary_data = std::move(pngData);
        clipData.format = Format::IMAGE_PNG;
        clipData.mime_type = "image/png";

        auto& dataList = pImpl->data[selection];
        dataList.clear();
        dataList.push_back(std::move(clipData));

        XSetSelectionOwner(pImpl->display, selAtom, pImpl->window, CurrentTime);

        Window owner = XGetSelectionOwner(pImpl->display, selAtom);
        if (owner == pImpl->window) {
            pImpl->ownsSelection[selection] = true;
            NotifyListeners(selection, Format::IMAGE_PNG);
        } else {
            pImpl->ownsSelection[selection] = false;
            throw std::runtime_error("Failed to acquire selection ownership");
        }

        XFlush(pImpl->display);

    } catch (const std::exception& e) {
        pImpl->ownsSelection[selection] = false;
        throw std::runtime_error(std::string("Failed to set image: ") + e.what());
    }
}

std::unique_ptr<cairo_surface_t, void(*)(cairo_surface_t*)> Clipboard::GetImage() const {
    // FIXED: Remove the function call, implement directly
    if (!pImpl->display) {
        return {nullptr, cairo_surface_destroy};
    }

    Selection selection = Selection::CLIPBOARD;
    
    auto it = pImpl->data.find(selection);
    if (it != pImpl->data.end()) {
        for (const auto& data : it->second) {
            if (data.format == Format::IMAGE_PNG && !data.binary_data.empty()) {
                auto surface = pngToSurface(data.binary_data);
                if (surface) {
                    return surface;
                }
            }
        }
    }

    Atom selAtom = pImpl->getSelectionAtom(selection);
    if (selAtom != None) {
        std::string pngData = requestSelectionData(selAtom, pImpl->atoms.pngImage);
        if (!pngData.empty()) {
            std::vector<uint8_t> binaryData(pngData.begin(), pngData.end());
            return pngToSurface(binaryData);
        }
    }

    return {nullptr, cairo_surface_destroy};
}

// File operations
void Clipboard::SetFiles(const std::vector<std::string>& paths) {
    SetFiles(paths, Selection::CLIPBOARD);
}

void Clipboard::SetFiles(const std::vector<std::string>& paths, Selection selection) {
    if (!pImpl->display) {
        throw std::runtime_error("Clipboard not initialized");
    }

    Atom selAtom = pImpl->getSelectionAtom(selection);
    if (selAtom == None) {
        throw std::invalid_argument("Invalid selection type");
    }

    try {
        // Convert file paths to URI list format
        std::ostringstream uriList;
        for (const auto& path : paths) {
            uriList << "file://" << path << "\r\n";
        }

        ClipboardData clipData;
        clipData.text_data = uriList.str();
        clipData.format = Format::URI_LIST;
        clipData.mime_type = "text/uri-list";

        auto& dataList = pImpl->data[selection];
        dataList.clear();
        dataList.push_back(std::move(clipData));

        XSetSelectionOwner(pImpl->display, selAtom, pImpl->window, CurrentTime);

        Window owner = XGetSelectionOwner(pImpl->display, selAtom);
        if (owner == pImpl->window) {
            pImpl->ownsSelection[selection] = true;
            NotifyListeners(selection, Format::URI_LIST);
        } else {
            pImpl->ownsSelection[selection] = false;
            throw std::runtime_error("Failed to acquire selection ownership");
        }

        XFlush(pImpl->display);

    } catch (const std::exception& e) {
        pImpl->ownsSelection[selection] = false;
        throw std::runtime_error(std::string("Failed to set files: ") + e.what());
    }
}

std::vector<std::string> Clipboard::GetFiles() const {
    // FIXED: Remove function call, implement directly
    std::vector<std::string> files;
    Selection selection = Selection::CLIPBOARD;

    if (!pImpl->display) {
        return files;
    }

    auto it = pImpl->data.find(selection);
    if (it != pImpl->data.end()) {
        for (const auto& data : it->second) {
            if (data.format == Format::URI_LIST || data.format == Format::FILE_LIST) {
                parseURIList(data.text_data, files);
                if (!files.empty()) {
                    return files;
                }
            }
        }
    }

    Atom selAtom = pImpl->getSelectionAtom(selection);
    if (selAtom != None) {
        std::string uriData = requestSelectionData(selAtom, pImpl->atoms.uriList);
        if (!uriData.empty()) {
            parseURIList(uriData, files);
        }
    }

    return files; // FIXED: Return files instead of undefined result
}

// HTML operations
void Clipboard::SetHTML(const std::string& html) {
    if (!pImpl->display) {
        throw std::runtime_error("Clipboard not initialized");
    }

    try {
        ClipboardData clipData;
        clipData.text_data = html; // FIXED: Assign string directly
        clipData.format = Format::HTML; // FIXED: Use enum directly
        clipData.mime_type = "text/html"; // FIXED: Assign string directly

        auto& dataList = pImpl->data[Selection::CLIPBOARD];
        dataList.clear();
        dataList.push_back(std::move(clipData));

        XSetSelectionOwner(pImpl->display, pImpl->atoms.html, pImpl->window, CurrentTime);

        Window owner = XGetSelectionOwner(pImpl->display, pImpl->atoms.html);
        if (owner == pImpl->window) {
            pImpl->ownsSelection[Selection::CLIPBOARD] = true;
            NotifyListeners(Selection::CLIPBOARD, Format::HTML);
        } else {
            pImpl->ownsSelection[Selection::CLIPBOARD] = false;
            throw std::runtime_error("Failed to acquire selection ownership");
        }

        XFlush(pImpl->display);

    } catch (const std::exception& e) {
        pImpl->ownsSelection[Selection::CLIPBOARD] = false;
        throw std::runtime_error(std::string("Failed to set HTML: ") + e.what());
    }
}

// HTML/RTF handling
void Clipboard::SetHTML(const std::string& html) {
    if (!pImpl->display) {
        throw std::runtime_error("Clipboard not initialized");
    }
    
    try {
        ClipboardData clipData;
        clipData.text_data = html;
        clipData.format = Format::HTML;
        clipData.mime_type = "text/html";
        
        auto& dataList = pImpl->data[Selection::CLIPBOARD];
        dataList.clear();
        dataList.push_back(std::move(clipData));
        
        // Take ownership
        XSetSelectionOwner(pImpl->display, pImpl->atoms.clipboard, pImpl->window, CurrentTime);
        
        Window owner = XGetSelectionOwner(pImpl->display, pImpl->atoms.clipboard);
        if (owner == pImpl->window) {
            pImpl->ownsSelection[Selection::CLIPBOARD] = true;
            NotifyListeners(Selection::CLIPBOARD, Format::HTML);
        } else {
            pImpl->ownsSelection[Selection::CLIPBOARD] = false;
            throw std::runtime_error("Failed to acquire selection ownership");
        }
        
        XFlush(pImpl->display);
        
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to set HTML: ") + e.what());
    }
}

std::string Clipboard::GetHTML() const {
    if (!pImpl->display) {
        return "";
    }
    
    // Check local data
    auto it = pImpl->data.find(Selection::CLIPBOARD);
    if (it != pImpl->data.end()) {
        for (const auto& data : it->second) {
            if (data.format == Format::HTML) {
                return data.text_data;
            }
        }
    }
    
    // Request from external owner
    return requestSelectionData(pImpl->atoms.clipboard, pImpl->atoms.html);
}

// Custom format handling
void Clipboard::SetData(const std::string& format, const std::vector<uint8_t>& data) {
    if (!pImpl->display) {
        throw std::runtime_error("Clipboard not initialized");
    }
    
    try {
        RegisterFormat(format);
        
        ClipboardData clipData;
        clipData.binary_data = data;
        clipData.format = Format::CUSTOM;
        clipData.mime_type = format;
        
        auto& dataList = pImpl->data[Selection::CLIPBOARD];
        dataList.clear();
        dataList.push_back(std::move(clipData));
        
        // Take ownership using custom format atom
        Atom formatAtom = pImpl->customFormats[format];
        XSetSelectionOwner(pImpl->display, formatAtom, pImpl->window, CurrentTime);
        
        Window owner = XGetSelectionOwner(pImpl->display, formatAtom);
        if (owner == pImpl->window) {
            pImpl->ownsSelection[Selection::CLIPBOARD] = true;
            NotifyListeners(Selection::CLIPBOARD, Format::CUSTOM);
        } else {
            pImpl->ownsSelection[Selection::CLIPBOARD] = false;
            throw std::runtime_error("Failed to acquire selection ownership");
        }
        
        XFlush(pImpl->display);
        
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to set custom data: ") + e.what());
    }
}

std::vector<uint8_t> Clipboard::GetData(const std::string& format) const {
    if (!pImpl->display) {
        return {};
    }
    
    // Check local data
    auto it = pImpl->data.find(Selection::CLIPBOARD);
    if (it != pImpl->data.end()) {
        for (const auto& data : it->second) {
            if (data.format == Format::CUSTOM && data.mime_type == format) {
                return data.binary_data;
            }
        }
    }
    
    // Request from external owner
    auto formatIt = pImpl->customFormats.find(format);
    if (formatIt != pImpl->customFormats.end()) {
        std::string strData = requestSelectionData(pImpl->atoms.clipboard, formatIt->second);
        return std::vector<uint8_t>(strData.begin(), strData.end());
    }
    
    return {};
}

// Event handling
void Clipboard::HandleSelectionRequest(XSelectionRequestEvent* event) {
    if (!event || !pImpl->display) {
        return;
    }
    
    XSelectionEvent response;
    response.type = SelectionNotify;
    response.display = event->display;
    response.requestor = event->requestor;
    response.selection = event->selection;
    response.target = event->target;
    response.property = event->property;
    response.time = event->time;
    
    try {
        // Determine which selection is being requested
        Selection sel = Selection::CLIPBOARD;
        if (event->selection == pImpl->atoms.primary) {
            sel = Selection::PRIMARY;
        } else if (event->selection == pImpl->atoms.secondary) {
            sel = Selection::SECONDARY;
        }
        
        // Check if we own this selection
        if (!pImpl->ownsSelection[sel]) {
            response.property = None;
        } else if (event->target == pImpl->atoms.targets) {
            handleTargetsRequest(event, response);
        } else if (event->target == pImpl->atoms.utf8String || event->target == pImpl->atoms.plainText) {
            handleTextRequest(event, response, sel);
        } else if (event->target == pImpl->atoms.pngImage) {
            handleImageRequest(event, response, sel);
        } else if (event->target == pImpl->atoms.uriList) {
            handleFileListRequest(event, response, sel);
        } else if (event->target == pImpl->atoms.html) {
            handleHTMLRequest(event, response, sel);
        } else {
            // Unknown target
            response.property = None;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error handling selection request: " << e.what() << std::endl;
        response.property = None;
    }
    
    // Send response
    XSendEvent(event->display, event->requestor, False, 0, reinterpret_cast<XEvent*>(&response));
    XFlush(pImpl->display);
}

void Clipboard::HandleSelectionNotify(XSelectionEvent* /* event */) {
    // This is called when we receive data from another application
    // The actual data handling is done in requestSelectionData()
}

void Clipboard::HandleSelectionClear(XSelectionClearEvent* event) {
    if (!event || !pImpl->display) {
        return;
    }
    
    // We've lost ownership of a selection
    Selection sel = Selection::CLIPBOARD;
    if (event->selection == pImpl->atoms.primary) {
        sel = Selection::PRIMARY;
    } else if (event->selection == pImpl->atoms.secondary) {
        sel = Selection::SECONDARY;
    }
    
    pImpl->ownsSelection[sel] = false;
    pImpl->data[sel].clear();
    
    // Notify listeners
    for (const auto& callback : pImpl->changeListeners) {
        try {
            callback(sel, Format::TEXT);  // Assume text for simplicity
        } catch (const std::exception& e) {
            std::cerr << "Error in change callback: " << e.what() << std::endl;
        }
    }
}

// Helper methods
void Clipboard::RegisterFormat(const std::string& format) {
    if (!pImpl->display) {
        throw std::runtime_error("Clipboard not initialized");
    }
    
    if (pImpl->customFormats.find(format) == pImpl->customFormats.end()) {
        Atom atom = XInternAtom(pImpl->display, format.c_str(), False);
        if (atom == None) {
            throw std::runtime_error("Failed to register format: " + format);
        }
        pImpl->customFormats[format] = atom;
    }
}

bool Clipboard::ConvertData(const std::vector<uint8_t>& /* data */,
                          Format /* fromFormat */, Format /* toFormat */,
                          std::vector<uint8_t>& /* result */) {
    // TODO: Implement data format conversion
    // This could include text encoding conversions, image format conversions, etc.
    return false;
}

void Clipboard::AddChangeListener(ChangeCallback callback) {
    if (callback) {
        pImpl->changeListeners.push_back(std::move(callback));
    }
}

void Clipboard::RemoveChangeListener(ChangeCallback /* callback */) {
    // TODO: Implement callback removal
    // This requires storing callbacks in a way that allows comparison/removal
}

// Private helper methods

/**
 * @brief Convert Cairo surface to PNG data
 */
bool Clipboard::surfaceToPNG(cairo_surface_t* surface, std::vector<uint8_t>& pngData) const {
    if (!surface) {
        return false;
    }
    
    // Cairo write function that appends to our vector
    auto writeFunc = [](void* closure, const unsigned char* data, unsigned int length) -> cairo_status_t {
        std::vector<uint8_t>* vec = static_cast<std::vector<uint8_t>*>(closure);
        vec->insert(vec->end(), data, data + length);
        return CAIRO_STATUS_SUCCESS;
    };
    
    cairo_status_t status = cairo_surface_write_to_png_stream(surface, writeFunc, &pngData);
    return status == CAIRO_STATUS_SUCCESS;
}

/**
 * @brief Convert PNG data to Cairo surface
 */
std::unique_ptr<cairo_surface_t, void(*)(cairo_surface_t*)> 
Clipboard::pngToSurface(const std::vector<uint8_t>& pngData) const {
    if (pngData.empty()) {
        return {nullptr, cairo_surface_destroy};
    }
    
    // Create a copy of the data for the read function
    auto dataPtr = std::make_unique<std::vector<uint8_t>>(pngData);
    size_t offset = 0;
    
    // Cairo read function
    auto readFunc = [](void* closure, unsigned char* data, unsigned int length) -> cairo_status_t {
        struct ReadContext {
            std::vector<uint8_t>* dataPtr;
            size_t* offset;
        };
        
        ReadContext* ctx = static_cast<ReadContext*>(closure);
        if (!ctx->dataPtr || *ctx->offset >= ctx->dataPtr->size()) {
            return CAIRO_STATUS_READ_ERROR;
        }
        
        size_t available = ctx->dataPtr->size() - *ctx->offset;
        size_t toCopy = std::min<size_t>(length, available);
        
        std::memcpy(data, ctx->dataPtr->data() + *ctx->offset, toCopy);
        *ctx->offset += toCopy;
        
        return toCopy > 0 ? CAIRO_STATUS_SUCCESS : CAIRO_STATUS_READ_ERROR;
    };
    
    ReadContext ctx{dataPtr.get(), &offset};
    cairo_surface_t* surface = cairo_image_surface_create_from_png_stream(readFunc, &ctx);
    
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return {nullptr, cairo_surface_destroy};
    }
    
    return {surface, cairo_surface_destroy};
}

/**
 * @brief Parse URI list text into file paths
 */
void Clipboard::parseURIList(const std::string& uriText, std::vector<std::string>& files) const {
    std::istringstream iss(uriText);
    std::string line;
    
    while (std::getline(iss, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        // Remove carriage return if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        if (line.empty()) {
            continue;
        }
        
        // Convert file:// URIs to local paths
        if (line.find("file://") == 0) {
            std::string path = line.substr(7);  // Remove "file://"
            
            // Handle localhost prefix
            if (path.find("localhost") == 0) {
                path = path.substr(9);  // Remove "localhost"
            }
            
            // Basic URL decoding for common cases
            urlDecode(path);
            
            if (!path.empty()) {
                files.push_back(path);
            }
        } else {
            // Non-file URI or already a local path
            files.push_back(line);
        }
    }
}

/**
 * @brief Simple URL decoder for file paths
 */
void Clipboard::urlDecode(std::string& str) const {
    std::string result;
    result.reserve(str.length());
    
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '%' && i + 2 < str.length()) {
            // Try to decode hex sequence
            char hex[3] = {str[i + 1], str[i + 2], 0};
            char* end;
            long value = std::strtol(hex, &end, 16);
            
            if (end == hex + 2) {  // Successfully parsed
                result += static_cast<char>(value);
                i += 2;  // Skip the hex digits
            } else {
                result += str[i];  // Keep the % if decoding failed
            }
        } else {
            result += str[i];
        }
    }
    
    str = std::move(result);
}

/**
 * @brief Request selection data from external owner
 */

 std::string Clipboard::requestSelectionData(Atom selection, Atom target) const {
    if (!pImpl->display || !pImpl->window) {
        return "";
    }

    // Create a property to receive the data
    Atom property = XInternAtom(pImpl->display, "HAVEL_CLIPBOARD_PROP", False);
    
    // Request the selection
    XConvertSelection(pImpl->display, selection, target, property, pImpl->window, CurrentTime);
    XFlush(pImpl->display);

    // Wait for SelectionNotify event with timeout
    auto startTime = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::milliseconds(pImpl->SELECTION_TIMEOUT_MS);

    while (std::chrono::steady_clock::now() - startTime < timeout) {
        XEvent event;
        if (XCheckTypedWindowEvent(pImpl->display, pImpl->window, SelectionNotify, &event)) {
            XSelectionEvent* selEvent = &event.xselection;
            
            if (selEvent->property == property) {
                // Read the property data
                Atom actualType;
                int actualFormat;
                unsigned long nitems, bytesAfter;
                unsigned char* data = nullptr;

                int result = XGetWindowProperty(
                    pImpl->display, pImpl->window, property,
                    0, LONG_MAX, False, AnyPropertyType,
                    &actualType, &actualFormat, &nitems, &bytesAfter, &data
                );

                std::string resultData;
                if (result == Success && data) {
                    resultData = std::string(reinterpret_cast<char*>(data), nitems);
                    XFree(data);
                }

                // Clean up the property
                XDeleteProperty(pImpl->display, pImpl->window, property);
                return resultData;
            }
        }

        // Small delay to prevent busy waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return ""; // Timeout
}

std::vector<Atom> Clipboard::getAvailableTargets(Atom selection) const {
    std::vector<Atom> targets;
    
    std::string targetsData = requestSelectionData(selection, pImpl->atoms.targets);
    if (!targetsData.empty() && targetsData.size() % sizeof(Atom) == 0) {
        const Atom* atomData = reinterpret_cast<const Atom*>(targetsData.data());
        size_t atomCount = targetsData.size() / sizeof(Atom);
        
        targets.reserve(atomCount);
        for (size_t i = 0; i < atomCount; ++i) {
            targets.push_back(atomData[i]);
        }
    }

    return targets;
}

Atom Clipboard::formatToAtom(Format format) const {
    switch (format) {
        case Format::TEXT: return pImpl->atoms.utf8String;
        case Format::HTML: return pImpl->atoms.html;
        case Format::RTF: return pImpl->atoms.rtf;
        case Format::IMAGE_PNG: return pImpl->atoms.pngImage;
        case Format::IMAGE_JPEG: return pImpl->atoms.jpegImage;
        case Format::IMAGE_BMP: return pImpl->atoms.bmpImage;
        case Format::URI_LIST:
        case Format::FILE_LIST: return pImpl->atoms.uriList;
        default: return None;
    }
}

Clipboard::Format Clipboard::atomToFormat(Atom atom) const {
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
    
    return Format::UNKNOWN;
}

// Selection request handlers
void Clipboard::handleTargetsRequest(XSelectionRequestEvent* event, XSelectionEvent& response) {
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

void Clipboard::handleFileListRequest(XSelectionRequestEvent* event, XSelectionEvent& response, Selection selection) {
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

void Clipboard::handleHTMLRequest(XSelectionRequestEvent* event, XSelectionEvent& response, Selection selection) {
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

// Image conversion helpers
bool Clipboard::surfaceToPNG(cairo_surface_t* surface, std::vector<uint8_t>& pngData) const {
    if (!surface) return false;

    // Create a temporary file in memory using cairo's PNG writer
    struct PNGWriteData {
        std::vector<uint8_t>* data;
        cairo_status_t status = CAIRO_STATUS_SUCCESS;
    };

    PNGWriteData writeData;
    writeData.data = &pngData;

    auto writeFunc = [](void* closure, const unsigned char* data, unsigned int length) -> cairo_status_t {
        PNGWriteData* wd = static_cast<PNGWriteData*>(closure);
        try {
            wd->data->insert(wd->data->end(), data, data + length);
            return CAIRO_STATUS_SUCCESS;
        } catch (...) {
            return CAIRO_STATUS_WRITE_ERROR;
        }
    };

    cairo_status_t status = cairo_surface_write_to_png_stream(surface, writeFunc, &writeData);
    return status == CAIRO_STATUS_SUCCESS && !pngData.empty();
}

std::unique_ptr<cairo_surface_t, void(*)(cairo_surface_t*)> 
Clipboard::pngToSurface(const std::vector<uint8_t>& pngData) const {
    if (pngData.empty()) {
        return {nullptr, cairo_surface_destroy};
    }

    struct PNGReadData {
        const std::vector<uint8_t>* data;
        size_t offset = 0;
    };

    PNGReadData readData;
    readData.data = &pngData;

    auto readFunc = [](void* closure, unsigned char* data, unsigned int length) -> cairo_status_t {
        PNGReadData* rd = static_cast<PNGReadData*>(closure);
        if (rd->offset + length > rd->data->size()) {
            return CAIRO_STATUS_READ_ERROR;
        }
        
        std::memcpy(data, rd->data->data() + rd->offset, length);
        rd->offset += length;
        return CAIRO_STATUS_SUCCESS;
    };

    cairo_surface_t* surface = cairo_image_surface_create_from_png_stream(readFunc, &readData);
    if (!surface || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        if (surface) {
            cairo_surface_destroy(surface);
        }
        return {nullptr, cairo_surface_destroy};
    }

    return {surface, cairo_surface_destroy};
}

void Clipboard::parseURIList(const std::string& uriData, std::vector<std::string>& files) const {
    if (uriData.empty()) {
        return;
    }

    std::istringstream stream(uriData);
    std::string line;
    
    while (std::getline(stream, line)) {
        // Remove carriage return if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        // Convert file:// URIs to local paths
        if (line.find("file://") == 0) {
            std::string path = line.substr(7); // Remove "file://"
            
            // URL decode the path
            std::string decoded = urlDecode(path);
            if (!decoded.empty()) {
                files.push_back(decoded);
            }
        } else {
            // Non-file URI, keep as-is
            files.push_back(line);
        }
    }
}

std::string Clipboard::urlDecode(const std::string& encoded) const {
    std::string decoded;
    decoded.reserve(encoded.length());
    
    for (size_t i = 0; i < encoded.length(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.length()) {
            // Parse hex digits
            char hex[3] = {encoded[i + 1], encoded[i + 2], 0};
            char* end;
            long value = std::strtol(hex, &end, 16);
            
            if (end == hex + 2) { // Successfully parsed 2 hex digits
                decoded += static_cast<char>(value);
                i += 2; // Skip the hex digits
            } else {
                decoded += encoded[i]; // Invalid encoding, keep original
            }
        } else if (encoded[i] == '+') {
            decoded += ' '; // + represents space in URL encoding
        } else {
            decoded += encoded[i];
        }
    }
    
    return decoded;
}

} // namespace havel