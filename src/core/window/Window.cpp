#include "Window.hpp"
#include "utils/Logger.hpp"
#include "types.hpp"
#include <sstream>

namespace havel {

Window::Window(cstr title, wID id) : m_title(title), m_id(id) {
  if (id != 0 && m_title.empty()) {
    m_title = Title(id);
  }
  if (id != 0 && m_class.empty()) {
    m_class = Class(id);
  }
  if (id != 0 && m_pid == 0) {
    m_pid = PID(id);
  }
}

Window::Window(wID id) : m_id(id) {
  m_title = Title(id);
  m_class = Class(id);
  m_pid = PID(id);
}

Rect Window::Pos() const { return Pos(m_id); }

Rect Window::Pos(wID win) {
  if (!win) return {};
  return WindowManager::get().getBackend().getWindowPosition(win);
}

wID Window::Find2(cstr identifier, cstr type) {
  wID win = 0;
  if (type == "title") {
    win = WindowManager::FindByTitle(identifier);
  } else if (type == "class") {
    win = WindowManager::FindByClass(identifier);
  } else if (type == "pid") {
    pID pid = std::stoi(identifier);
    win = WindowManager::GetwIDByPID(pid);
  }
  return win;
}

wID Window::Find(cstr identifier) {
  if (identifier.find("title=") == 0) {
    return WindowManager::FindByTitle(identifier.substr(6));
  } else if (identifier.find("class=") == 0) {
    return WindowManager::FindByClass(identifier.substr(6));
  } else if (identifier.find("pid=") == 0) {
    try {
      pID pid = std::stoi(identifier.substr(4));
      return WindowManager::GetwIDByPID(pid);
    } catch (const std::exception &) {
      return 0;
    }
  }
  return WindowManager::FindByTitle(identifier);
}

std::string Window::Title(wID win) {
  return WindowManager::GetWindowTitle(win);
}

std::string Window::Class(wID win) {
  return WindowManager::GetWindowClass(win);
}

pID Window::PID(wID win) {
  return WindowManager::GetWindowPID(win);
}

bool Window::Active() { return Active(m_id); }

bool Window::Active(wID win) {
  return WindowManager::GetActiveWindow() == win;
}

bool Window::Exists() { return Exists(m_id); }

bool Window::Exists(wID win) {
  return WindowManager::get().getBackend().isWindowExists(win);
}

void Window::Activate() {
  WindowManager::get().getBackend().focusWindow(m_id);
}

void Window::Close() {
  WindowManager::get().getBackend().closeWindow(m_id);
}

void Window::Min() {
  WindowManager::get().getBackend().minimizeWindow(m_id);
}

void Window::Max() {
  WindowManager::get().getBackend().maximizeWindow(m_id);
}

void Window::Hide() {
  WindowManager::get().getBackend().hideWindow(m_id);
}

void Window::Show() {
  WindowManager::get().getBackend().showWindow(m_id);
}

void Window::Minimize() {
  WindowManager::get().getBackend().minimizeWindow(m_id);
}

void Window::Transparency(int alpha) {
  Transparency(m_id, alpha);
}

void Window::Transparency(wID win, int alpha) {
  float opacity = static_cast<float>(alpha) / 255.0f;
  WindowManager::get().getBackend().setWindowOpacity(win, opacity);
}

void Window::AlwaysOnTop(bool top) {
  AlwaysOnTop(m_id, top);
}

void Window::AlwaysOnTop(wID win, bool top) {
  WindowManager::get().getBackend().setWindowAlwaysOnTop(win, top);
}

void Window::ToggleFullscreen() {
  WindowManager::get().getBackend().toggleWindowFullscreen(m_id);
}

bool Window::Move(int x, int y, bool centerOnScreen) {
  return WindowManager::Move(m_id, x, y, centerOnScreen);
}

bool Window::Resize(int width, int height, bool fullscreen) {
  return WindowManager::Resize(m_id, width, height, fullscreen);
}

bool Window::MoveResize(int x, int y, int width, int height) {
  return WindowManager::MoveResize(m_id, x, y, width, height);
}

bool Window::Center() {
  return WindowManager::Center(m_id);
}

bool Window::MoveToCorner(const std::string& corner) {
  return WindowManager::MoveToCorner(m_id, corner);
}

bool Window::MoveToMonitor(int monitorIndex) {
  return WindowManager::MoveToMonitor(m_id, monitorIndex);
}

void Window::Snap(int position) {
  WindowManager::SnapWindow(m_id, position);
}

} // namespace havel
