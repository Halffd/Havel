#pragma once

#ifdef HAVE_QT_EXTENSION

// Save X11 macros before Qt headers
#ifdef None
#define X11_None None
#undef None
#endif

#ifdef Bool
#define X11_Bool Bool
#undef Bool
#endif

#ifdef Status
#define X11_Status Status
#undef Status
#endif

#ifdef True
#define X11_True True
#undef True
#endif

#ifdef False
#define X11_False False
#undef False
#endif

// Include Qt headers
#include <QApplication>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QWidget>
#include <QMessageBox>
#include <QMenu>
#include <QAction>
#include <QTimer>
#include <QThread>
#include <QDialog>
#include <QLineEdit>
#include <QTextEdit>
#include <QCheckBox>
#include <QRadioButton>
#include <QComboBox>
#include <QSlider>
#include <QProgressBar>
#include <QTreeWidget>
#include <QTableWidget>
#include <QListView>
#include <QListWidget>
#include <QTabWidget>
#include <QScrollArea>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QMenuBar>
#include <QTreeView>
#include <QFileSystemModel>
#include <QStyle>
#include <QClipboard>
#include <QSystemTrayIcon>
#include <QShortcut>
#include <QSize>
#include <QMimeData>
#include <QDateTime>
#include <QImage>
#include <QFileInfo>
#include <QUrl>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDir>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QGraphicsOpacityEffect>
#include <QEasingCurve>
#include <QPoint>
#include <QVariant>
#include <QStringList>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QListWidgetItem>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QProcess>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QGroupBox>

namespace havel {
    using App = QApplication;
}
#ifndef emit
#define emit
#endif

#endif // HAVE_QT_EXTENSION
