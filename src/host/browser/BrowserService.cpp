/*
 * BrowserService.cpp
 *
 * Browser automation service implementation.
 * Delegates to core/browser/BrowserModule for CDP/Marionette communication.
 */
#include "BrowserService.hpp"
#include "core/browser/BrowserModule.hpp"

namespace havel::host {

BrowserService::BrowserService()
    : m_browser(std::make_shared<havel::BrowserModule>()) {
}

BrowserService::~BrowserService() {
}

bool BrowserService::connect(const std::string& browserUrl) {
  return m_browser->connect(browserUrl);
}

bool BrowserService::connectFirefox(int port) {
  return m_browser->connectFirefox(port);
}

void BrowserService::disconnect() {
  m_browser->disconnect();
}

bool BrowserService::isConnected() const {
  return m_browser->isConnected();
}

BrowserType BrowserService::getBrowserType() const {
  auto coreType = m_browser->getBrowserType();
  return static_cast<BrowserType>(static_cast<int>(coreType));
}

bool BrowserService::open(const std::string& url) {
  return m_browser->open(url);
}

bool BrowserService::newTab(const std::string& url) {
  return m_browser->newTab(url);
}

bool BrowserService::gotoUrl(const std::string& url) {
  return m_browser->gotoUrl(url);
}

bool BrowserService::back() {
  return m_browser->back();
}

bool BrowserService::forward() {
  return m_browser->forward();
}

bool BrowserService::reload(bool ignoreCache) {
  return m_browser->reload(ignoreCache);
}

std::vector<BrowserTab> BrowserService::listTabs() {
  auto tabs = m_browser->listTabs();
  std::vector<BrowserTab> result;
  result.reserve(tabs.size());
  for (auto& t : tabs) {
    BrowserTab out;
    out.id = t.id;
    out.title = t.title;
    out.url = t.url;
    out.type = t.type;
    result.push_back(std::move(out));
  }
  return result;
}

bool BrowserService::activate(int tabId) {
  return m_browser->activate(tabId);
}

bool BrowserService::closeTab(int tabId) {
  return m_browser->closeTab(tabId);
}

bool BrowserService::closeAll() {
  return m_browser->closeAll();
}

int BrowserService::getCurrentTabId() const {
  return m_browser->getCurrentTabId();
}

BrowserTab BrowserService::getActiveTab() const {
  auto t = m_browser->getActiveTab();
  BrowserTab out;
  out.id = t.id;
  out.title = t.title;
  out.url = t.url;
  out.type = t.type;
  return out;
}

std::string BrowserService::getActiveTabTitle() const {
  return m_browser->getActiveTabTitle();
}

std::string BrowserService::getCurrentUrl() const {
  return m_browser->getCurrentUrl();
}

std::string BrowserService::getTitle() const {
  return m_browser->getTitle();
}

std::vector<BrowserWindow> BrowserService::listWindows() {
  auto wins = m_browser->listWindows();
  std::vector<BrowserWindow> result;
  result.reserve(wins.size());
  for (auto& w : wins) {
    BrowserWindow out;
    out.id = w.id;
    out.x = w.x;
    out.y = w.y;
    out.width = w.width;
    out.height = w.height;
    out.maximized = w.maximized;
    out.minimized = w.minimized;
    out.fullscreen = w.fullscreen;
    result.push_back(std::move(out));
  }
  return result;
}

bool BrowserService::setWindowSize(int windowId, int width, int height) {
  return m_browser->setWindowSize(windowId, width, height);
}

bool BrowserService::setWindowPosition(int windowId, int x, int y) {
  return m_browser->setWindowPosition(windowId, x, y);
}

bool BrowserService::maximizeWindow(int windowId) {
  return m_browser->maximizeWindow(windowId);
}

bool BrowserService::minimizeWindow(int windowId) {
  return m_browser->minimizeWindow(windowId);
}

bool BrowserService::click(const std::string& selector) {
  return m_browser->click(selector);
}

bool BrowserService::type(const std::string& selector, const std::string& text) {
  return m_browser->type(selector, text);
}

bool BrowserService::focus(const std::string& selector) {
  return m_browser->focus(selector);
}

bool BrowserService::setZoom(double level) {
  return m_browser->setZoom(level);
}

double BrowserService::getZoom() const {
  return m_browser->getZoom();
}

bool BrowserService::resetZoom() {
  return m_browser->resetZoom();
}

std::string BrowserService::eval(const std::string& js) {
  return m_browser->eval(js);
}

bool BrowserService::screenshot(const std::string& path) {
  return m_browser->screenshot(path);
}

} // namespace havel::host
