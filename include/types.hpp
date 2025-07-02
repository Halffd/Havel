#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>

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


// Type aliases
using str = std::string;
using cstr = const std::string&;
using wID = unsigned long;
using pID = unsigned long;

// Group type definition
using group = std::unordered_map<std::string, std::vector<std::string>>;

// Process method enum
enum class ProcessMethod {
    Invalid = -1,
    WaitForTerminate = 0,
    ForkProcess = 1,
    ContinueExecution = 2,
    WaitUntilStarts = 3,
    CreateNewWindow = 4,
    AsyncProcessCreate = 5,
    SystemCall = 6,
    SameWindow = 7,
    Shell = 8
};

namespace havel {
    using Button = QPushButton;
    using Label = QLabel;
    using Layout = QVBoxLayout;
    using HLayout = QHBoxLayout;
    using Window = QMainWindow;
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

// Helper function to convert string to lowercase
inline std::string ToLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), 
                  [](unsigned char c) { return std::tolower(c); });
    return result;
}

// Common typedefs for key handling
#ifndef Key_typedef
#define Key_typedef
typedef unsigned long Key;
#endif

// Helper struct for Windows EnumWindows
#ifdef _WIN32
struct EnumWindowsData {
    std::string targetProcessName;
    wID id = 0;
    
    explicit EnumWindowsData(const std::string& name) : targetProcessName(name) {}
};
#endif 