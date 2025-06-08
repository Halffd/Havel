#include "Clipboard.hpp"
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <sstream>
#include <cstring>
#include <memory>
#include <algorithm>
#include <system_error>
#include <unordered_map>
#include <functional>
#include <algorithm>
using namespace havel;

namespace havel {

    struct ClipboardData {
        std::string text_data;
        std::vector<uint8_t> binary_data;
        std::vector<std::string> file_paths;
        havel::Clipboard::Format format;
        std::string mime_type;
        
        ClipboardData() : format(havel::Clipboard::Format::TEXT) {}
        
        std::string& data() { return text_data; }
        const std::string& data() const { return text_data; }
        const std::vector<uint8_t>& get_binary_data() const { return binary_data; }
        
        bool empty() const { 
            return text_data.empty() && binary_data.empty() && file_paths.empty();
        }
    };

struct Clipboard::Impl {
    Display* display{nullptr};
    Window window{0};
    
    // Selection data storage
    std::unordered_map<Selection, std::vector<ClipboardData>> data;
    std::unordered_map<Selection, bool> ownsSelection;
    
    // Atoms for various formats
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
    } atoms;
    
    // Custom formats
    std::unordered_map<std::string, Atom> customFormats;
    
    // Change listeners
    std::vector<ChangeCallback> changeListeners;
};

Clipboard& Clipboard::Instance() {
    static Clipboard instance;
    return instance;
}

Clipboard::Clipboard() : pImpl(std::make_unique<Impl>()) {
    // Open X display
    pImpl->display = XOpenDisplay(nullptr);
    if (!pImpl->display) {
        throw std::runtime_error("Failed to open X display");
    }
    
    // Create an invisible window to own selections
    Window root = DefaultRootWindow(pImpl->display);
    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    attrs.event_mask = PropertyChangeMask;
    
    pImpl->window = XCreateWindow(
        pImpl->display, root,
        0, 0, 1, 1, 0,
        CopyFromParent, InputOutput, CopyFromParent,
        CWOverrideRedirect | CWEventMask,
        &attrs
    );
    
    // Initialize atoms
    pImpl->atoms.targets = XInternAtom(pImpl->display, "TARGETS", False);
    pImpl->atoms.utf8String = XInternAtom(pImpl->display, "UTF8_STRING", False);
    pImpl->atoms.plainText = XInternAtom(pImpl->display, "TEXT", False);
    pImpl->atoms.pngImage = XInternAtom(pImpl->display, "image/png", False);
    pImpl->atoms.jpegImage = XInternAtom(pImpl->display, "image/jpeg", False);
    pImpl->atoms.bmpImage = XInternAtom(pImpl->display, "image/bmp", False);
    pImpl->atoms.uriList = XInternAtom(pImpl->display, "text/uri-list", False);
    pImpl->atoms.html = XInternAtom(pImpl->display, "text/html", False);
    pImpl->atoms.rtf = XInternAtom(pImpl->display, "text/rtf", False);
}

Clipboard::~Clipboard() {
    if (pImpl->display) {
        if (pImpl->window) {
            XDestroyWindow(pImpl->display, pImpl->window);
        }
        XCloseDisplay(pImpl->display);
    }
}

void Clipboard::SetText(const std::string& text) {
    SetText(text, Selection::CLIPBOARD);
}

std::string Clipboard::GetText() const {
    return GetText(Selection::CLIPBOARD);
}

void Clipboard::SetText(const std::string& text, Selection selection) {
    Atom selAtom = None;
    
    // Set the appropriate selection atom
    switch (selection) {
        case Selection::CLIPBOARD: selAtom = XA_CLIPBOARD(pImpl->display); break;
        case Selection::PRIMARY: selAtom = XA_PRIMARY; break;
        case Selection::SECONDARY: selAtom = XA_SECONDARY; break;
    }
    
    if (selAtom == None) {
        return;  // Invalid selection
    }
    
    // Prepare the data
    auto& dataList = pImpl->data[selection];
    dataList.clear();
    dataList.emplace_back();
    dataList.back().text_data = text;
    dataList.back().format = Format::TEXT;
    
    // Take ownership of the selection
    XSetSelectionOwner(pImpl->display, selAtom, pImpl->window, CurrentTime);
    pImpl->ownsSelection[selection] = true;
}

std::string Clipboard::GetText(Selection selection) const {
    Atom selAtom = XA_PRIMARY; // Default to PRIMARY selection
    const std::string& selText = pImpl->data[selection][0].data();
    const bool& ownsSelection = pImpl->ownsSelection[selection];
    
    try {
        // Default implementation: only support direct copy for now
        if (ownsSelection) {
            return selText;
        }
        
        // Request the selection from its owner
        Window owner = XGetSelectionOwner(pImpl->display, selAtom);
        if (owner == None) {
            return "";
        }
        
        // Convert selection to UTF8
        XConvertSelection(pImpl->display, selAtom, pImpl->atoms.utf8String,
                         selAtom, pImpl->window, CurrentTime);
        
        // Wait for SelectionNotify event
        XEvent event;
        do {
            XNextEvent(pImpl->display, &event);
        } while (event.type != SelectionNotify ||
                 event.xselection.selection != selAtom);
        
        if (event.xselection.property == None) {
            return "";
        }
        
        // Read the selection property
        Atom type;
        int format;
        unsigned long nitems, bytes_after;
        unsigned char* data;
        
        XGetWindowProperty(pImpl->display, pImpl->window, selAtom,
                          0, LONG_MAX, False, AnyPropertyType,
                          &type, &format, &nitems, &bytes_after, &data);
        
        std::string result;
        if (data) {
            result = std::string(reinterpret_cast<char*>(data), nitems);
            XFree(data);
        }
        
        return result;
    } catch (const ClipboardAccessException& e) {
        // Actually handle this specific case
        initialize_clipboard();
        return get_clipboard_text(); // retry
    } catch (const std::system_error& e) {
        if (e.code() == std::errc::permission_denied) {
            log_permission_error();
            return ""; // reasonable fallback
        }
        throw; // re-throw what we can't handle
    }
    // Convert selection to UTF8
    XConvertSelection(pImpl->display, selAtom, pImpl->atoms.utf8String,
                     selAtom, pImpl->window, CurrentTime);
    
    // Wait for SelectionNotify event
    XEvent event;
    do {
        XNextEvent(pImpl->display, &event);
    } while (event.type != SelectionNotify ||
             event.xselection.selection != selAtom);
    
    if (event.xselection.property == None) {
        return "";
    }
    
    // Read the selection property
    Atom type;
    int format;
    unsigned long nitems, bytes_after;
    unsigned char* data;
    
    XGetWindowProperty(pImpl->display, pImpl->window, selAtom,
                      0, LONG_MAX, False, AnyPropertyType,
                      &type, &format, &nitems, &bytes_after, &data);
    
    std::string result;
    if (data) {
        result = std::string(reinterpret_cast<char*>(data), nitems);
        XFree(data);
    }
    
    return result;
}

void Clipboard::Clear(Selection selection) {
    SetText("", selection);
}

void Clipboard::HandleSelectionRequest(XSelectionRequestEvent* event) {
    XSelectionEvent response;
    response.type = SelectionNotify;
    response.display = event->display;
    response.requestor = event->requestor;
    response.selection = event->selection;
    response.target = event->target;
    response.property = event->property;
    response.time = event->time;
    
    const std::string* text = nullptr;
    
    if (event->selection == XA_CLIPBOARD(pImpl->display) && pImpl->ownsSelection[Selection::CLIPBOARD]) {
        text = &pImpl->data[Selection::CLIPBOARD][0].data();
    } else if (event->selection == XA_PRIMARY && pImpl->ownsSelection[Selection::PRIMARY]) {
        text = &pImpl->data[Selection::PRIMARY][0].data();
    } else if (event->selection == XA_SECONDARY && pImpl->ownsSelection[Selection::SECONDARY]) {
        text = &pImpl->data[Selection::SECONDARY][0].data();
    }
    
    if (!text) {
        response.property = None;
    } else if (event->target == pImpl->atoms.targets) {
        // Respond with supported targets
        Atom targets[] = {pImpl->atoms.targets, pImpl->atoms.utf8String, pImpl->atoms.plainText, pImpl->atoms.pngImage, pImpl->atoms.jpegImage, pImpl->atoms.bmpImage, pImpl->atoms.uriList, pImpl->atoms.html, pImpl->atoms.rtf};
        XChangeProperty(event->display, event->requestor, event->property,
                       XA_ATOM, 32, PropModeReplace,
                       reinterpret_cast<unsigned char*>(targets),
                       sizeof(targets)/sizeof(Atom));
    } else if (event->target == pImpl->atoms.utf8String ||
               event->target == pImpl->atoms.plainText) {
        // Respond with text data
        XChangeProperty(event->display, event->requestor, event->property,
                       event->target, 8, PropModeReplace,
                       reinterpret_cast<const unsigned char*>(text->c_str()),
                       text->length());
    } else {
        response.property = None;
    }
    
    XSendEvent(event->display, event->requestor, False, 0,
               reinterpret_cast<XEvent*>(&response));
    XFlush(pImpl->display);
}

void Clipboard::HandleSelectionNotify(XSelectionEvent* /*event*/) {
    // Handle incoming selection data if needed
}

// Image handling methods
void Clipboard::SetImage(cairo_surface_t* surface) {
    SetImage(surface, Selection::CLIPBOARD);
}<<

void Clipboard::SetImage(cairo_surface_t* surface, Selection selection) {
    if (!surface) return;
    
    // Convert surface to PNG in memory
    std::vector<uint8_t> pngData;
    
    // Proper C++ capture - no void* nonsense
    auto writeFunc = [&pngData](void*, const unsigned char* data, unsigned int length) -> cairo_status_t {
        pngData.insert(pngData.end(), data, data + length);
        return CAIRO_STATUS_SUCCESS;
    };
    
    cairo_status_t status = cairo_surface_write_to_png_stream(surface, writeFunc, nullptr);
    if (status != CAIRO_STATUS_SUCCESS) {
        throw std::runtime_error("Failed to convert surface to PNG: " + std::string(cairo_status_to_string(status)));
    }
    
    // Rest stays the same...
    auto& dataList = pImpl->data[selection];
    dataList.clear();
    dataList.emplace_back();
    dataList.back().binary_data = std::move(pngData);
    dataList.back().format = Format::IMAGE_PNG;
    
    Atom selAtom = None;
    switch (selection) {
        case Selection::CLIPBOARD: selAtom = XA_CLIPBOARD(pImpl->display); break;
        case Selection::PRIMARY: selAtom = XA_PRIMARY; break;
        case Selection::SECONDARY: selAtom = XA_SECONDARY; break;
    }
    
    if (selAtom != None) {
        XSetSelectionOwner(pImpl->display, selAtom, pImpl->window, CurrentTime);
        pImpl->ownsSelection[selection] = true;
    }
}

std::vector<std::string> Clipboard::GetFiles() const {
    std::vector<std::string> files;
    Selection selection = Selection::CLIPBOARD;
    
    // Check if we have file data
    auto it = pImpl->data.find(selection);
    if (it == pImpl->data.end() || it->second.empty()) {
        return files;
    }
    
    // Look for URI list data
    for (const auto& data : it->second) {
        if (data.format == Format::URI_LIST || data.format == Format::FILE_LIST) {
            // Parse the URI list (one URI per line)
            std::istringstream iss(data.text_data);
            std::string uri;
            while (std::getline(iss, uri)) {
                // Skip empty lines and comments
                if (uri.empty() || uri[0] == '#') continue;
                
                // Remove any trailing carriage return
                if (!uri.empty() && uri.back() == '\r') {
                    uri.pop_back();
                }
                
                // Skip empty lines after CR removal
                if (uri.empty()) continue;
                
                // Basic URI to path conversion (handles file:// URIs)
                if (uri.find("file://") == 0) {
                    // Remove file:// prefix
                    uri = uri.substr(7);
                    // Handle localhost prefix
                    if (uri.find("localhost") == 0) {
                        uri = uri.substr(9);
                    }
                    // URL decode if needed (simplified)
                    // TODO: Implement proper URL decoding
                }
                
                if (!uri.empty()) {
                    files.push_back(uri);
                }
            }
            break; // Found and processed URI list, no need to check further
        }
    }
    
    return files;
}

void havel::Clipboard::SetHTML(const std::string& html) {
    try {
        ClipboardData htmlData;
        htmlData.text_data = html;
        htmlData.format = Format::HTML;
        htmlData.mime_type = "text/html";
        
        pImpl->data[Selection::CLIPBOARD].clear();
        pImpl->data[Selection::CLIPBOARD].push_back(std::move(htmlData));
        
        XSetSelectionOwner(pImpl->display, pImpl->atoms.html,
                         pImpl->window, CurrentTime);
        
        NotifyListeners(Selection::CLIPBOARD, Format::HTML);
    } catch (const std::exception& e) {
        std::cerr << "Error in SetHTML: " << e.what() << std::endl;
        throw;
    }
}

std::string havel::Clipboard::GetHTML() const {
    try {
        auto& dataList = pImpl->data[Selection::CLIPBOARD];
        if (!dataList.empty() && dataList[0].format == Format::HTML) {
            return dataList[0].data();
        }
        return "";
    } catch (const std::exception& e) {
        std::cerr << "Error in GetHTML: " << e.what() << std::endl;
        return "";
    }
}

// Custom format handling
void havel::Clipboard::SetData(const std::string& format, const std::vector<uint8_t>& data) {
    try {
        RegisterFormat(format);
        
        ClipboardData customData;
        customData.binary_data = data;
        customData.format = Format::CUSTOM;
        customData.mime_type = format;
        
        pImpl->data[Selection::CLIPBOARD].clear();
        pImpl->data[Selection::CLIPBOARD].push_back(std::move(customData));
        
        // Set selection owner for custom format
        Atom formatAtom = XInternAtom(pImpl->display, format.c_str(), False);
        XSetSelectionOwner(pImpl->display, formatAtom, pImpl->window, CurrentTime);
        
        NotifyListeners(Selection::CLIPBOARD, Format::CUSTOM);
    } catch (const std::exception& e) {
        std::cerr << "Error in SetData: " << e.what() << std::endl;
        throw;
    }
}

std::vector<uint8_t> havel::Clipboard::GetData(const std::string& format) const {
    try {
        auto& dataList = pImpl->data[Selection::CLIPBOARD];
        for (const auto& data : dataList) {
            if (data.format == Format::CUSTOM && data.mime_type == format) {
                return data.get_binary_data();
            }
        }
        return {};
    } catch (const std::exception& e) {
        std::cerr << "Error in GetData: " << e.what() << std::endl;
        return {};
    }
}

// Helper methods
void havel::Clipboard::RegisterFormat(const std::string& format) {
    if (pImpl->customFormats.find(format) == pImpl->customFormats.end()) {
        Atom atom = XInternAtom(pImpl->display, format.c_str(), False);
        pImpl->customFormats[format] = atom;
    }
}

bool havel::Clipboard::ConvertData(const std::vector<uint8_t>& data,
                                 Format fromFormat, Format toFormat,
                                 std::vector<uint8_t>& result) {
    try {
        // Default implementation: only support direct copy for now
        if (fromFormat == toFormat) {
            result = data;
            return true;
        }
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Error in ConvertData: " << e.what() << std::endl;
        return false;
    }
}

} // namespace havel