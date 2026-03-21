/*
 * Clipboard.cpp
 *
 * Core clipboard implementation - minimal overhead.
 */
#include "Clipboard.hpp"
#include <QClipboard>
#include <QGuiApplication>

namespace havel::host {

Clipboard::Clipboard() {
    // Cache QClipboard pointer for fast access
    if (QGuiApplication::instance()) {
        clipboard_ = QGuiApplication::clipboard();
    }
}

Clipboard::~Clipboard() {
}

std::string Clipboard::getText() const {
    auto* cb = static_cast<QClipboard*>(clipboard_);
    if (!cb) {
        return "";
    }
    return cb->text().toStdString();
}

bool Clipboard::setText(const std::string& text) {
    auto* cb = static_cast<QClipboard*>(clipboard_);
    if (!cb) {
        return false;
    }
    cb->setText(QString::fromStdString(text));
    return true;
}

bool Clipboard::clear() {
    auto* cb = static_cast<QClipboard*>(clipboard_);
    if (!cb) {
        return false;
    }
    cb->clear();
    return true;
}

bool Clipboard::hasText() const {
    auto* cb = static_cast<QClipboard*>(clipboard_);
    if (!cb) {
        return false;
    }
    return !cb->text().isEmpty();
}

} // namespace havel::host
