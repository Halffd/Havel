#include "ClipboardManager.hpp"
#include <QApplication>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QMessageBox>
#include <QHeaderView>
#include <QDateTime>
#include <QStatusBar>
#include <QDebug>
#include <QMenu>
#include <QAction>

namespace havel {

ClipboardManager::~ClipboardManager() {
    // Clean up resources
    if (trayIcon) {
        trayIcon->hide();
    }
    
    // Clean up shortcuts
    delete showShortcut;
    delete deleteShortcut;
    delete escapeShortcut;
}

ClipboardManager::ClipboardManager(IO* io, QWidget* parent) 
    : QMainWindow(parent)
    , centralWidget(nullptr)
    , searchBox(nullptr)
    , historyList(nullptr)
    , previewPane(nullptr)
    , splitter(nullptr)
    , clipboard(nullptr)
    , trayIcon(nullptr)
    , showShortcut(nullptr)
    , deleteShortcut(nullptr)
    , escapeShortcut(nullptr)
    , io(io) {
    
    // Set window properties
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    
    // Setup UI with custom font size and window size
    setupUI();
    
    // Initialize clipboard
    clipboard = QApplication::clipboard();
    if (clipboard) {
        connect(clipboard, &QClipboard::dataChanged, 
                this, &ClipboardManager::onClipboardChanged);
    }

    // Setup system tray icon
    trayIcon = new QSystemTrayIcon(this);
    QIcon icon = QIcon::fromTheme("edit-paste");
    if (icon.isNull()) {
        // Fallback icon
        QPixmap pixmap(16, 16);
        pixmap.fill(QColor(100, 150, 200));
        icon = QIcon(pixmap);
    }
    trayIcon->setIcon(icon);
    trayIcon->setToolTip("Clipboard Manager");
    trayIcon->show();

    // Setup hotkey for showing/hiding the window
    if (io) {
        initializeHotkeys();
    }

    // Hide by default - show only when needed
    hide();
}

void ClipboardManager::setupUI() {
    // Set window size
    resize(windowSize);
    
    // Create central widget and layout
    centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(5, 5, 5, 5);
    mainLayout->setSpacing(5);

    // Create font with custom size
    QFont appFont = QApplication::font();
    appFont.setPointSize(fontSize);
    setFont(appFont);

    // Search box
    searchBox = new QLineEdit(this);
    searchBox->setPlaceholderText("Search clipboard history...");
    searchBox->setFont(appFont);
    connect(searchBox, &QLineEdit::textChanged,
            this, &ClipboardManager::onSearchTextChanged);
    mainLayout->addWidget(searchBox);

    // Splitter for history and preview
    splitter = new QSplitter(Qt::Horizontal, this);
    
    // History list
    historyList = new QListWidget(this);
    historyList->setFont(appFont);
    historyList->setAlternatingRowColors(true);
    historyList->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(historyList, &QListWidget::itemDoubleClicked,
            this, &ClipboardManager::onItemDoubleClicked);
    connect(historyList, &QListWidget::itemSelectionChanged,
            this, &ClipboardManager::onItemSelectionChanged);
    connect(historyList, &QListWidget::itemClicked,
            this, &ClipboardManager::onItemClicked);
    splitter->addWidget(historyList);

    // Preview pane
    previewPane = new QTextEdit(this);
    previewPane->setFont(appFont);
    previewPane->setReadOnly(true);
    previewPane->setMinimumWidth(200);
    splitter->addWidget(previewPane);
    
    // Set initial splitter sizes
    QList<int> sizes;
    sizes << width() * 0.6 << width() * 0.4;
    splitter->setSizes(sizes);
    
    mainLayout->addWidget(splitter);

    // Status bar with smaller font
    QStatusBar* statusBar = new QStatusBar(this);
    QFont statusFont = appFont;
    statusFont.setPointSize(fontSize * 0.7);
    statusBar->setFont(statusFont);
    setStatusBar(statusBar);
    statusBar->showMessage("Alt+V to toggle | Double-click to copy | Del to remove");
}

void ClipboardManager::setupShortcuts() {
    // Global shortcut to show clipboard manager
    showShortcut = new QShortcut(QKeySequence("Ctrl+Shift+V"), this);
    connect(showShortcut, &QShortcut::activated, this, &ClipboardManager::showAndFocus);

    // Delete selected item
    deleteShortcut = new QShortcut(QKeySequence::Delete, this);
    connect(deleteShortcut, &QShortcut::activated, this, &ClipboardManager::removeSelectedItem);

    // Escape to hide
    escapeShortcut = new QShortcut(QKeySequence::Cancel, this);
    connect(escapeShortcut, &QShortcut::activated, this, &QWidget::hide);

    // Enter/Return to copy
    auto* enterShortcut = new QShortcut(QKeySequence::InsertParagraphSeparator, this);
    connect(enterShortcut, &QShortcut::activated, this, &ClipboardManager::copySelectedItem);

    // Ctrl+F to focus search
    auto* searchShortcut = new QShortcut(QKeySequence::Find, this);
    connect(searchShortcut, &QShortcut::activated, [this]() {
        searchBox->setFocus();
        searchBox->selectAll();
    });
}

void ClipboardManager::onClipboardChanged() {
    if (clipboard) {
        QString text = clipboard->text();
        if (!text.isEmpty() && text != lastClipboard) {
            lastClipboard = text;
            addToHistory(text);
        }
    }
}

void ClipboardManager::onItemDoubleClicked(QListWidgetItem* item) {
    if (!item || !clipboard) return;

    QString text = item->data(Qt::UserRole).toString();
    clipboard->setText(text);
    showTrayMessage("Copied to clipboard!");
    
    // Optional: hide after copying
    hide();
}

void ClipboardManager::onSearchTextChanged(const QString& text) {
    filterHistory(text);
}

void ClipboardManager::onItemClicked(QListWidgetItem* item) {
    if (item) {
        QString text = item->data(Qt::UserRole).toString();
        previewPane->setPlainText(text);
    }
}

void ClipboardManager::onItemSelectionChanged() {
    auto* currentItem = historyList->currentItem();
    if (currentItem) {
        QString fullText = currentItem->data(Qt::UserRole).toString();
        
        // Truncate for preview if too long
        QString previewText = fullText;
        if (previewText.length() > PREVIEW_MAX_LENGTH) {
            previewText = previewText.left(PREVIEW_MAX_LENGTH) + "\n\n[... truncated ...]";
        }
        previewPane->setPlainText(previewText);
        
        // Update status
        statusBar()->showMessage(QString("Selected item: %1 characters").arg(fullText.length()));
    } else {
        previewPane->clear();
        statusBar()->showMessage("Ready - Double-click to copy, Del to remove");
    }
}

void ClipboardManager::addToHistory(const QString& text) {
    // Remove if already exists (move to top)
    fullHistory.removeAll(text);
    fullHistory.prepend(text);

    // Limit size
    while (fullHistory.size() > MAX_HISTORY_SIZE) {
        fullHistory.removeLast();
    }

    // Update display
    filterHistory(searchBox->text());
}

void ClipboardManager::filterHistory(const QString& filter) {
    historyList->clear();

    for (const QString& text : fullHistory) {
        if (filter.isEmpty() || text.contains(filter, Qt::CaseInsensitive)) {
            auto* item = new QListWidgetItem();
            
            // Display text (truncated for list view)
            QString displayText = text;
            displayText.replace('\n', ' ').replace('\r', ' ');
            if (displayText.length() > 80) {
                displayText = displayText.left(77) + "...";
            }
            
            item->setText(displayText);
            item->setData(Qt::UserRole, text);  // Store full text
            item->setToolTip(text.left(500));   // Tooltip with more text
            
            historyList->addItem(item);
        }
    }
    
    statusBar()->showMessage(QString("Showing %1 items").arg(historyList->count()));
}

void ClipboardManager::showTrayMessage(const QString& message) {
    if (trayIcon) {
        trayIcon->showMessage("Clipboard Manager", message, 
                             QSystemTrayIcon::Information, 2000);
    }
}

void ClipboardManager::showAndFocus() {
    if (isVisible()) {
        hide();
    } else {
        show();
        raise();
        activateWindow();
        searchBox->setFocus();
    }
}

void ClipboardManager::toggleVisibility() {
    showAndFocus();
}

void ClipboardManager::initializeHotkeys() {
    if (!io) {
        qWarning() << "IO system not available for hotkey registration";
        return;
    }
    
    // Register Alt+V hotkey
    bool success = io->Hotkey("!v", [this]() {
        qDebug() << "Alt+V pressed - toggling clipboard manager";
        QMetaObject::invokeMethod(this, "toggleVisibility", Qt::QueuedConnection);
    });
    
    if (!success) {
        qWarning() << "Failed to register Alt+V hotkey";
        // Fallback to Qt's shortcut system if IO hotkey registration fails
        if (!showShortcut) {
            showShortcut = new QShortcut(QKeySequence("Alt+V"), this);
            connect(showShortcut, &QShortcut::activated, this, &ClipboardManager::toggleVisibility);
            qInfo() << "Using Qt shortcut system for Alt+V hotkey";
        }
    } else {
        qInfo() << "Successfully registered Alt+V hotkey with IO system";
    }
    
    // Register number key hotkeys (^+1 through ^+9)
    for (int i = 1; i <= 9; i++) {
        QString hotkey = QString("^+%1").arg(i);
        bool hotkeySuccess = io->Hotkey(hotkey.toStdString(), [this, i]() {
            qDebug() << "Pasting history item at index" << (i - 1);
            QMetaObject::invokeMethod(this, "pasteHistoryItem", 
                                   Qt::QueuedConnection,
                                   Q_ARG(int, i - 1));
        });
        
        if (!hotkeySuccess) {
            qWarning() << "Failed to register hotkey for" << hotkey;
            // Fallback to Qt's shortcut system if IO hotkey registration fails
            QShortcut* shortcut = new QShortcut(QKeySequence(QString("Ctrl+%1").arg(i)), this);
            connect(shortcut, &QShortcut::activated, this, [this, i] {
                pasteHistoryItem(i - 1);
            });
            qInfo() << "Using Qt shortcut system for" << hotkey;
        } else {
            qInfo() << "Successfully registered hotkey for" << hotkey;
        }
    }
    
    // Register ^+0 to clear clipboard
    bool clearSuccess = io->Hotkey("^+0", [this]() {
        qDebug() << "Clearing clipboard";
        QMetaObject::invokeMethod(this, "onClearAll", Qt::QueuedConnection);
    });
    
    if (!clearSuccess) {
        qWarning() << "Failed to register ^+0 hotkey";
        // Fallback to Qt's shortcut system if IO hotkey registration fails
        QShortcut* clearShortcut = new QShortcut(QKeySequence("Ctrl+0"), this);
        connect(clearShortcut, &QShortcut::activated, this, &ClipboardManager::onClearAll);
        qInfo() << "Using Qt shortcut system for Ctrl+0";
    } else {
        qInfo() << "Successfully registered ^+0 hotkey";
    }
}

void ClipboardManager::onHotkeyPressed() {
    toggleVisibility();
}

void ClipboardManager::pasteHistoryItem(int index) {
    if (index < 0 || index >= historyList->count()) {
        qWarning() << "Invalid history index:" << index;
        return;
    }
    
    // Get the item from the history list
    QListWidgetItem* item = historyList->item(index);
    if (!item) {
        qWarning() << "Failed to get history item at index:" << index;
        return;
    }
    
    // Get the full text from the item's data
    QString text = item->data(Qt::UserRole).toString();
    if (text.isEmpty()) {
        qWarning() << "Empty text in history item at index:" << index;
        return;
    }
    
    // Set the clipboard content
    clipboard->setText(text);
    
    // Move the item to the top of the history
    QString displayText = text.left(100);
    if (text.length() > 100) {
        displayText += "...";
    }
    
    // Remove the item and reinsert it at the top
    int row = historyList->row(item);
    QListWidgetItem* newItem = historyList->takeItem(row);
    historyList->insertItem(0, newItem);
    historyList->setCurrentItem(newItem);
    
    // Update the full history list
    fullHistory.removeAt(row);
    fullHistory.prepend(text);
    
    // Hide the window after pasting
    hide();
}

void ClipboardManager::onClearAll() {
    if (historyList) {
        historyList->clear();
        fullHistory.clear();
        previewPane->clear();
        statusBar()->showMessage("Clipboard history cleared");
    }
}

void ClipboardManager::onTrayIconActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger) {
        toggleVisibility();
    }
}

void ClipboardManager::showContextMenu(const QPoint& pos) {
    QMenu menu(this);
    
    QAction* copyAction = menu.addAction("Copy");
    QAction* deleteAction = menu.addAction("Delete");
    menu.addSeparator();
    QAction* clearAllAction = menu.addAction("Clear All");
    menu.addSeparator();
    QAction* quitAction = menu.addAction("Quit");
    
    connect(copyAction, &QAction::triggered, this, &ClipboardManager::copySelectedItem);
    connect(deleteAction, &QAction::triggered, this, &ClipboardManager::removeSelectedItem);
    connect(clearAllAction, &QAction::triggered, this, &ClipboardManager::onClearAll);
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);
    
    menu.exec(historyList->mapToGlobal(pos));
}

void ClipboardManager::copySelectedItem() {
    auto* currentItem = historyList->currentItem();
    if (currentItem && clipboard) {
        QString text = currentItem->data(Qt::UserRole).toString();
        clipboard->setText(text);
        showTrayMessage("Copied to clipboard!");
        hide();
    }
}

void ClipboardManager::removeSelectedItem() {
    auto* currentItem = historyList->currentItem();
    if (!currentItem) return;

    QString text = currentItem->data(Qt::UserRole).toString();
    fullHistory.removeAll(text);
    
    int currentRow = historyList->row(currentItem);
    delete historyList->takeItem(currentRow);
    
    statusBar()->showMessage("Item removed");
}

void ClipboardManager::closeEvent(QCloseEvent* event) {
    // Hide instead of closing
    hide();
    event->ignore();
}

void ClipboardManager::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        hide();
    } else {
        QMainWindow::keyPressEvent(event);
    }
}

} // namespace havel
