#include "ClipboardManager.hpp"
#include <QApplication>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QMessageBox>
#include <QHeaderView>
#include <QDateTime>
#include <QStatusBar>

namespace havel {

ClipboardManager::ClipboardManager(QWidget* parent) 
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
    , escapeShortcut(nullptr) {
    
    setupUI();
    setupShortcuts();

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

    // Hide by default - show only when needed
    hide();
}

void ClipboardManager::setupUI() {
    setWindowTitle("Clipboard History");
    setMinimumSize(500, 600);
    setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint | Qt::WindowMinimizeButtonHint);

    centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    auto* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setSpacing(8);
    mainLayout->setContentsMargins(8, 8, 8, 8);

    // Search box
    searchBox = new QLineEdit(this);
    searchBox->setPlaceholderText("Search clipboard history... (Ctrl+F)");
    searchBox->setClearButtonEnabled(true);
    connect(searchBox, &QLineEdit::textChanged, 
            this, &ClipboardManager::onSearchTextChanged);
    mainLayout->addWidget(searchBox);

    // Splitter for history list and preview
    splitter = new QSplitter(Qt::Vertical, this);
    
    // History list
    historyList = new QListWidget(this);
    historyList->setAlternatingRowColors(true);
    historyList->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(historyList, &QListWidget::itemDoubleClicked, 
            this, &ClipboardManager::onItemDoubleClicked);
    connect(historyList, &QListWidget::itemSelectionChanged,
            this, &ClipboardManager::onItemSelectionChanged);
    
    // Preview pane
    previewPane = new QTextEdit(this);
    previewPane->setReadOnly(true);
    previewPane->setMaximumHeight(150);
    previewPane->setPlaceholderText("Select an item to preview...");

    splitter->addWidget(historyList);
    splitter->addWidget(previewPane);
    splitter->setSizes({400, 200});
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);

    mainLayout->addWidget(splitter);

    // Status bar
    statusBar()->showMessage("Ready - Double-click to copy, Del to remove");
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
    if (!clipboard) return;

    QString text = clipboard->text();
    if (!text.isEmpty() && text != lastClipboard) {
        addToHistory(text);
        lastClipboard = text;
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
    show();
    raise();
    activateWindow();
    searchBox->setFocus();
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
