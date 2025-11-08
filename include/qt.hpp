#pragma once

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

// Include Qt headers here or after this header
// Qt headers will now work without conflicts

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

namespace havel {
    using App = QApplication;
}
#ifndef emit
#define emit
#endif