#include "MapManagerWindow.hpp"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFocusEvent>
#include <QApplication>
#include <QDebug>

namespace havel {

HotkeyCapture::HotkeyCapture(QWidget* parent) : QLineEdit(parent) {
    setPlaceholderText("Click here and press a key combination...");
    setReadOnly(true);
}

void HotkeyCapture::mousePressEvent(QMouseEvent* event) {
    // Handle mouse press
    QLineEdit::mousePressEvent(event);
    startCapture();
}

void HotkeyCapture::wheelEvent(QWheelEvent* event) {
    // Handle wheel
    if (capturing) {
        // For wheel events, we can handle them differently
        // For now, just emit the signal
        QString wheelStr = (event->angleDelta().y() > 0) ? "WheelUp" : "WheelDown";
        emit keyCaptured(wheelStr);
        stopCapture();
    } else {
        QLineEdit::wheelEvent(event);
    }
}

void HotkeyCapture::keyPressEvent(QKeyEvent* event) {
    // Handle key press
    if (capturing) {
        QString keyStr = QString::fromStdString(keyEventToString(event));
        emit keyCaptured(keyStr);
        stopCapture();
    } else {
        QLineEdit::keyPressEvent(event);
    }
}

void HotkeyCapture::focusInEvent(QFocusEvent* event) {
    // Handle focus in
    QLineEdit::focusInEvent(event);
    startCapture();
}

void HotkeyCapture::focusOutEvent(QFocusEvent* event) {
    // Handle focus out
    QLineEdit::focusOutEvent(event);
    if (capturing) {
        stopCapture();
    }
}

void HotkeyCapture::startCapture() {
    capturing = true;
    setText("Capturing...");
    setStyleSheet("QLineEdit { background-color: #ffeb3b; }");
}

void HotkeyCapture::stopCapture() {
    capturing = false;
    setStyleSheet("");
    if (capturedKey.empty()) {
        setPlaceholderText("Click here and press a key combination...");
    }
}

std::string HotkeyCapture::keyEventToString(QKeyEvent* event) {
    QString keyText;
    
    // Check modifiers
    Qt::KeyboardModifiers modifiers = event->modifiers();
    if (modifiers & Qt::ControlModifier) {
        keyText += "Ctrl+";
    }
    if (modifiers & Qt::ShiftModifier) {
        keyText += "Shift+";
    }
    if (modifiers & Qt::AltModifier) {
        keyText += "Alt+";
    }
    if (modifiers & Qt::MetaModifier) {
        keyText += "Meta+";
    }

    // Get the key name
    int key = event->key();
    QString keyName = QKeySequence(key).toString();
    
    if (keyName.isEmpty()) {
        // Fallback for special keys
        switch (key) {
            case Qt::Key_Escape: keyName = "Escape"; break;
            case Qt::Key_Tab: keyName = "Tab"; break;
            case Qt::Key_Backtab: keyName = "Tab"; break;  // Backtab is just Shift+Tab
            case Qt::Key_Backspace: keyName = "Backspace"; break;
            case Qt::Key_Return: keyName = "Enter"; break;
            case Qt::Key_Enter: keyName = "Enter"; break;
            case Qt::Key_Insert: keyName = "Insert"; break;
            case Qt::Key_Delete: keyName = "Delete"; break;
            case Qt::Key_Pause: keyName = "Pause"; break;
            case Qt::Key_Print: keyName = "PrintScreen"; break;
            case Qt::Key_SysReq: keyName = "SysReq"; break;
            case Qt::Key_Clear: keyName = "Clear"; break;
            case Qt::Key_Home: keyName = "Home"; break;
            case Qt::Key_End: keyName = "End"; break;
            case Qt::Key_Left: keyName = "Left"; break;
            case Qt::Key_Right: keyName = "Right"; break;
            case Qt::Key_Up: keyName = "Up"; break;
            case Qt::Key_Down: keyName = "Down"; break;
            case Qt::Key_PageUp: keyName = "PageUp"; break;
            case Qt::Key_PageDown: keyName = "PageDown"; break;
            case Qt::Key_F1: keyName = "F1"; break;
            case Qt::Key_F2: keyName = "F2"; break;
            case Qt::Key_F3: keyName = "F3"; break;
            case Qt::Key_F4: keyName = "F4"; break;
            case Qt::Key_F5: keyName = "F5"; break;
            case Qt::Key_F6: keyName = "F6"; break;
            case Qt::Key_F7: keyName = "F7"; break;
            case Qt::Key_F8: keyName = "F8"; break;
            case Qt::Key_F9: keyName = "F9"; break;
            case Qt::Key_F10: keyName = "F10"; break;
            case Qt::Key_F11: keyName = "F11"; break;
            case Qt::Key_F12: keyName = "F12"; break;
            case Qt::Key_F13: keyName = "F13"; break;
            case Qt::Key_F14: keyName = "F14"; break;
            case Qt::Key_F15: keyName = "F15"; break;
            case Qt::Key_F16: keyName = "F16"; break;
            case Qt::Key_F17: keyName = "F17"; break;
            case Qt::Key_F18: keyName = "F18"; break;
            case Qt::Key_F19: keyName = "F19"; break;
            case Qt::Key_F20: keyName = "F20"; break;
            case Qt::Key_F21: keyName = "F21"; break;
            case Qt::Key_F22: keyName = "F22"; break;
            case Qt::Key_F23: keyName = "F23"; break;
            case Qt::Key_F24: keyName = "F24"; break;
            case Qt::Key_Space: keyName = "Space"; break;
            case Qt::Key_Asterisk: keyName = "*"; break;
            case Qt::Key_Plus: keyName = "+"; break;
            case Qt::Key_Comma: keyName = ","; break;
            case Qt::Key_Minus: keyName = "-"; break;
            case Qt::Key_Period: keyName = "."; break;
            case Qt::Key_Slash: keyName = "/"; break;
            case Qt::Key_Colon: keyName = ":"; break;
            case Qt::Key_Semicolon: keyName = ";"; break;
            case Qt::Key_Less: keyName = "<"; break;
            case Qt::Key_Equal: keyName = "="; break;
            case Qt::Key_Greater: keyName = ">"; break;
            case Qt::Key_Question: keyName = "?"; break;
            case Qt::Key_At: keyName = "@"; break;
            case Qt::Key_BracketLeft: keyName = "["; break;
            case Qt::Key_Backslash: keyName = "\\"; break;
            case Qt::Key_BracketRight: keyName = "]"; break;
            case Qt::Key_AsciiCircum: keyName = "^"; break;
            case Qt::Key_Underscore: keyName = "_"; break;
            case Qt::Key_QuoteLeft: keyName = "`"; break;
            case Qt::Key_QuoteDbl: keyName = "\""; break;
            case Qt::Key_BraceLeft: keyName = "{"; break;
            case Qt::Key_Bar: keyName = "|"; break;
            case Qt::Key_BraceRight: keyName = "}"; break;
            case Qt::Key_AsciiTilde: keyName = "~"; break;
            case Qt::Key_Exclam: keyName = "!"; break;
            case Qt::Key_NumberSign: keyName = "#"; break;
            case Qt::Key_Dollar: keyName = "$"; break;
            case Qt::Key_Percent: keyName = "%"; break;
            case Qt::Key_Ampersand: keyName = "&"; break;
            default:
                keyName = event->text();
                if (keyName.isEmpty()) {
                    keyName = QString("Key_%1").arg(key);
                }
                break;
        }
    }
    
    return keyText.append(keyName).toStdString();
}

std::string HotkeyCapture::mouseButtonToString(Qt::MouseButton button) {
    switch (button) {
        case Qt::LeftButton: return "LButton";
        case Qt::RightButton: return "RButton";
        case Qt::MiddleButton: return "MButton";
        case Qt::XButton1: return "XButton1";
        case Qt::XButton2: return "XButton2";
        default: return "MouseButton";
    }
}

} // namespace havel