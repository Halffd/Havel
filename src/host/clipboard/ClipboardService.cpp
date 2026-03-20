/*
 * ClipboardService.cpp
 *
 * Pure C++ clipboard service implementation.
 * No VM, no interpreter, no HavelValue - just system logic.
 * 
 * Note: Requires Qt GUI application to be running.
 */
#include "ClipboardService.hpp"
#include <QGuiApplication>
#include <QClipboard>
#include <QTimer>
#include <QMimeData>

namespace havel::host {

// =========================================================================
// Clipboard text operations
// =========================================================================

std::string ClipboardService::getText() {
    if (!QGuiApplication::instance()) {
        return "";
    }
    QClipboard* clipboard = QGuiApplication::clipboard();
    return clipboard ? clipboard->text().toStdString() : "";
}

bool ClipboardService::setText(const std::string& text) {
    if (!QGuiApplication::instance()) {
        return false;
    }
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        return false;
    }
    
    // Use QTimer to make clipboard operation asynchronous and avoid X11 blocking
    QString textToSet = QString::fromStdString(text);
    QTimer::singleShot(0, [clipboard, textToSet]() {
        clipboard->setText(textToSet);
    });
    
    return true;
}

bool ClipboardService::clear() {
    if (!QGuiApplication::instance()) {
        return false;
    }
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        return false;
    }
    
    // Use QTimer to make clipboard operation asynchronous
    QTimer::singleShot(0, [clipboard]() {
        clipboard->clear();
    });
    
    return true;
}

bool ClipboardService::hasText() {
    if (!QGuiApplication::instance()) {
        return false;
    }
    QClipboard* clipboard = QGuiApplication::clipboard();
    return clipboard && !clipboard->text().isEmpty();
}

// =========================================================================
// Clipboard aliases
// =========================================================================

std::string ClipboardService::get() {
    return getText();
}

std::string ClipboardService::in() {
    return getText();
}

std::string ClipboardService::out() {
    return getText();
}

// =========================================================================
// Clipboard send
// =========================================================================

bool ClipboardService::send(const std::string& text, std::function<void(const std::string&)> ioSend) {
    if (!ioSend) {
        return false;
    }
    
    std::string textToSend = text;
    if (textToSend.empty()) {
        textToSend = getText();
    }
    
    if (textToSend.empty()) {
        return false;
    }
    
    // Use QTimer for async send
    QTimer::singleShot(0, [ioSend, textToSend]() {
        ioSend(textToSend);
    });
    
    return true;
}

// =========================================================================
// Clipboard manager operations
// =========================================================================

void ClipboardService::showManager(void* manager) {
    // ClipboardManager is Qt-specific, would need to be called from GUI thread
    // This is a placeholder - actual implementation requires Qt headers
    (void)manager;
}

void ClipboardService::hideManager(void* manager) {
    (void)manager;
}

std::vector<std::string> ClipboardService::getHistory(void* manager) {
    (void)manager;
    return {};
}

void ClipboardService::clearHistory(void* manager) {
    (void)manager;
}

void ClipboardService::pasteHistoryItem(void* manager, int index) {
    (void)manager;
    (void)index;
}

} // namespace havel::host
