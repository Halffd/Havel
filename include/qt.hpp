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
    using Button = QPushButton;
    using Label = QLabel;
    using Layout = QVBoxLayout;
    using HLayout = QHBoxLayout;
    using QWindow = QMainWindow;
    using Widget = QWidget;
    using MessageBox = QMessageBox;
    using App = QApplication;
    using Menu = QMenu;
    using Action = QAction;
    using Timer = QTimer;
    using Thread = QThread;
    using Dialog = QDialog;
    using LineEdit = QLineEdit;
    using TextEdit = QTextEdit;
    using CheckBox = QCheckBox;
    using RadioButton = QRadioButton;
    using ComboBox = QComboBox;
    using Slider = QSlider;
    using ProgressBar = QProgressBar;
    using TreeWidget = QTreeWidget;
    using TableWidget = QTableWidget;
    using ListView = QListView;
    using TabWidget = QTabWidget;
    using ScrollArea = QScrollArea;
    using Splitter = QSplitter;
    using StatusBar = QStatusBar;
    using ToolBar = QToolBar;
    using MenuBar = QMenuBar;
    using TreeView = QTreeView;
    using FileSystemModel = QFileSystemModel;
}
// Restore X11 macros after Qt usage (if needed)
#ifdef X11_None
#define None X11_None
#endif

#ifdef X11_Bool
#define Bool X11_Bool
#endif

#ifdef X11_Status
#define Status X11_Status
#endif

#ifdef X11_True
#define True X11_True
#endif

#ifdef X11_False
#define False X11_False
#endif