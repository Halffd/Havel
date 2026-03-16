#pragma once

#include "x11.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QScroller>
#include <QScrollerProperties>
#include <QStyle>
#include <QStyleOption>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace havel {

struct AltTabWindowInfo {
  Window window;
  std::string title;
  std::string className;
  QPixmap thumbnail;
  int width;
  int height;
  bool isActive;
  bool isMinimized;
  bool isMaximized;

  AltTabWindowInfo()
      : window(x11::XNone), width(0), height(0), isActive(false),
        isMinimized(false), isMaximized(false) {}
};

class AltTabWindow : public QWidget {
  Q_OBJECT

public:
  explicit AltTabWindow(QWidget *parent = nullptr);
  ~AltTabWindow();

  void showAltTab();
  void hideAltTab();
  void refreshWindows();
  void nextWindow();
  void prevWindow();
  void selectCurrentWindow();
  void setThumbnailSize(int width, int height);

public slots:
  void onWindowActivated();

protected:
  void paintEvent(QPaintEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void keyReleaseEvent(QKeyEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
  void setupUI();
  void setupX11();
  std::vector<AltTabWindowInfo> getWindows();
  AltTabWindowInfo getWindowInfo(Window win);
  QPixmap captureWindowThumbnail(Window win);
  void updateSelection();
  void activateWindow(Window win);
  void updateWindowList();
  void centerWindow();
  void updateThumbnail(Window win, int index);

  // X11 related members
  Display *display;
  Window rootWindow;
  Atom netClientListAtom;
  Atom netActiveWindowAtom;
  Atom netWMNameAtom;
  Atom wmNameAtom;
  Atom netWMStateAtom;
  Atom netWMStateHiddenAtom;
  Atom netWMStateDemandsAttentionAtom;

  // UI elements
  std::vector<AltTabWindowInfo> windows;
  int currentIndex;
  int thumbnailWidth;
  int thumbnailHeight;

  // Thumbnail cache
  std::unordered_map<Window, QPixmap> thumbnailCache;

  // UI elements
  QVBoxLayout *mainLayout;
  QHBoxLayout *thumbnailsLayout;
  QWidget *thumbnailsContainer;
  QScrollArea *scrollArea;
  QLabel *titleLabel;

  // Animation and timing
  QTimer *refreshTimer;
  bool isVisibleFlag;
};

} // namespace havel