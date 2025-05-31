#include "Clipboard.hpp"
#include <X11/Xatom.h>
#include <stdexcept>
#include <vector>
#include <sstream>
#include <algorithm>
#include <unordered_map>
#include <functional>

namespace havel {

struct ClipboardData {
    std::vector<uint8_t> data;
    Format format;
    std::string mimeType;
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
    Atom selAtom;
    std::string& selText = pImpl->data[selection][0].data;
    bool& ownsSelection = pImpl->ownsSelection[selection];
    
    selText = text;
    
    // Take ownership of selection
    XSetSelectionOwner(pImpl->display, selAtom, pImpl->window, CurrentTime);
    
    if (XGetSelectionOwner(pImpl->display, selAtom) == pImpl->window) {
        ownsSelection = true;
    } else {
        throw std::runtime_error("Failed to take ownership of selection");
    }
    
    XFlush(pImpl->display);
}

std::string Clipboard::GetText(Selection selection) const {
    Atom selAtom;
    const std::string& selText = pImpl->data[selection][0].data;
    const bool& ownsSelection = pImpl->ownsSelection[selection];
    
    // If we own the selection, return our copy
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
        text = &pImpl->data[Selection::CLIPBOARD][0].data;
    } else if (event->selection == XA_PRIMARY && pImpl->ownsSelection[Selection::PRIMARY]) {
        text = &pImpl->data[Selection::PRIMARY][0].data;
    } else if (event->selection == XA_SECONDARY && pImpl->ownsSelection[Selection::SECONDARY]) {
        text = &pImpl->data[Selection::SECONDARY][0].data;
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

void Clipboard::HandleSelectionNotify(XSelectionEvent* event) {
    // Handle incoming selection data if needed
}

// Image handling methods
void Clipboard::SetImage(cairo_surface_t* surface) {
    SetImage(surface, Selection::CLIPBOARD);
}

void Clipboard::SetImage(cairo_surface_t* surface, Selection selection) {
    if (!surface) return;
    
    // Convert surface to PNG data
    std::vector<uint8_t> pngData;
    cairo_surface_write_to_png_stream(surface, 
        [](void* closure, const unsigned char* data, unsigned int length) -> cairo_status_t {
            auto* pngData = static_cast<std::vector<uint8_t>*>(closure);
            pngData->insert(pngData->end(), data, data + length);
            return CAIRO_STATUS_SUCCESS;
        }, &pngData);
    
    // Store the PNG data
    ClipboardData imageData{
        std::move(pngData),
        Format::IMAGE_PNG,
        "image/png"
    };
    
    pImpl->data[selection].clear();
    pImpl->data[selection].push_back(std::move(imageData));
    
    // Take ownership of selection
    XSetSelectionOwner(pImpl->display, pImpl->atoms.pngImage,
                      pImpl->window, CurrentTime);
    
    NotifyListeners(selection, Format::IMAGE_PNG);
}

std::unique_ptr<cairo_surface_t, void(*)(cairo_surface_t*)> Clipboard::GetImage() const {
    return GetImage(Selection::CLIPBOARD);
}

std::unique_ptr<cairo_surface_t, void(*)(cairo_surface_t*)> 
Clipboard::GetImage(Selection selection) const {
    // Try PNG first
    if (HasFormat(Format::IMAGE_PNG)) {
        auto pngData = GetData("image/png");
        if (!pngData.empty()) {
            return std::unique_ptr<cairo_surface_t, void(*)(cairo_surface_t*)>(
                cairo_image_surface_create_from_png_stream(
                    [](void* closure, unsigned char* data, unsigned int length) -> cairo_status_t {
                        auto* src = static_cast<const std::vector<uint8_t>*>(closure);
                        static size_t offset = 0;
                        
                        if (offset >= src->size()) return CAIRO_STATUS_READ_ERROR;
                        
                        size_t remaining = std::min(static_cast<size_t>(length),
                                                  src->size() - offset);
                        std::copy_n(src->begin() + offset, remaining, data);
                        offset += remaining;
                        
                        return CAIRO_STATUS_SUCCESS;
                    },
                    &pngData
                ),
                cairo_surface_destroy
            );
        }
    }
    
    // Try JPEG
    if (HasFormat(Format::IMAGE_JPEG)) {
        // Implementation for JPEG...
    }
    
    // Try BMP
    if (HasFormat(Format::IMAGE_BMP)) {
        // Implementation for BMP...
    }
    
    return std::unique_ptr<cairo_surface_t, void(*)(cairo_surface_t*)>(
        nullptr, cairo_surface_destroy);
}

// File handling methods
void Clipboard::SetFiles(const std::vector<std::string>& paths) {
    SetFiles(paths, Selection::CLIPBOARD);
}

void Clipboard::SetFiles(const std::vector<std::string>& paths, Selection selection) {
    std::stringstream ss;
    for (const auto& path : paths) {
        ss << "file://" << path << "\n";
    }
    
    ClipboardData fileData{
        std::vector<uint8_t>(ss.str().begin(), ss.str().end()),
        Format::URI_LIST,
        "text/uri-list"
    };
    
    pImpl->data[selection].clear();
    pImpl->data[selection].push_back(std::move(fileData));
    
    XSetSelectionOwner(pImpl->display, pImpl->atoms.uriList,
                      pImpl->window, CurrentTime);
    
    NotifyListeners(selection, Format::URI_LIST);
}

std::vector<std::string> Clipboard::GetFiles() const {
    return GetFiles(Selection::CLIPBOARD);
}

std::vector<std::string> Clipboard::GetFiles(Selection selection) const {
    std::vector<std::string> result;
    
    auto uriList = GetData("text/uri-list");
    if (uriList.empty()) return result;
    
    std::string uriStr(uriList.begin(), uriList.end());
    std::stringstream ss(uriStr);
    std::string line;
    
    while (std::getline(ss, line)) {
        if (line.empty() || line[0] == '#') continue;
        if (line.substr(0, 7) == "file://") {
            result.push_back(line.substr(7));
        }
    }
    
    return result;
}

// HTML/RTF handling
void Clipboard::SetHTML(const std::string& html) {
    ClipboardData htmlData{
        std::vector<uint8_t>(html.begin(), html.end()),
        Format::HTML,
        "text/html"
    };
    
    pImpl->data[Selection::CLIPBOARD].clear();
    pImpl->data[Selection::CLIPBOARD].push_back(std::move(htmlData));
    
    XSetSelectionOwner(pImpl->display, pImpl->atoms.html,
                      pImpl->window, CurrentTime);
    
    NotifyListeners(Selection::CLIPBOARD, Format::HTML);
}

std::string Clipboard::GetHTML() const {
    auto htmlData = GetData("text/html");
    return std::string(htmlData.begin(), htmlData.end());
}

// Custom format handling
void Clipboard::SetData(const std::string& format, const std::vector<uint8_t>& data) {
    RegisterFormat(format);
    
    ClipboardData customData{
        data,
        Format::CUSTOM,
        format
    };
    
    pImpl->data[Selection::CLIPBOARD].clear();
    pImpl->data[Selection::CLIPBOARD].push_back(std::move(customData));
    
    XSetSelectionOwner(pImpl->display, pImpl->customFormats[format],
                      pImpl->window, CurrentTime);
    
    NotifyListeners(Selection::CLIPBOARD, Format::CUSTOM);
}

std::vector<uint8_t> Clipboard::GetData(const std::string& format) const {
    // Implementation for getting custom format data...
    return std::vector<uint8_t>();
}

// Helper methods
void Clipboard::RegisterFormat(const std::string& format) {
    if (pImpl->customFormats.find(format) == pImpl->customFormats.end()) {
        Atom atom = XInternAtom(pImpl->display, format.c_str(), False);
        pImpl->customFormats[format] = atom;
    }
}

bool Clipboard::ConvertData(const std::vector<uint8_t>& data,
                          Format fromFormat, Format toFormat,
                          std::vector<uint8_t>& result) {
    // Implementation for data format conversion...
    return false;
}

} // namespace havel 