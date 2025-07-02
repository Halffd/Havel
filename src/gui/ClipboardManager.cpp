#include "ClipboardManager.hpp"
#include <QApplication>
#include <QClipboard>
#include <QListWidgetItem>
#include <QShortcut>
#include <QVBoxLayout>

havel::ClipboardManager::ClipboardManager(QWidget *parent) : Window(parent) {
    setupUI();

    clipboard = QApplication::clipboard();
    connect(clipboard, &QClipboard::dataChanged, this, &ClipboardManager::onClipboardChanged);

    trayIcon = new QSystemTrayIcon(this);
    trayIcon->setIcon(QIcon::fromTheme("edit-paste"));
    trayIcon->show();

    auto shortcut = new QShortcut(QKeySequence("Ctrl+Shift+V"), this);
    connect(shortcut, &QShortcut::activated, [this]() {
        show();
        searchBox->setFocus();
    });
}

void havel::ClipboardManager::setupUI() {
    setWindowTitle("Clipboard History");
    setMinimumSize(400, 500);

    auto mainLayout = new QVBoxLayout(this);
    searchBox = new LineEdit(this);
    searchBox->setPlaceholderText("Search clipboard history...");
    mainLayout->addWidget(searchBox);

    auto splitter = new QSplitter(Qt::Vertical, this);
    historyList = new ListView(splitter);
    previewPane = new TextEdit(splitter);
    previewPane->setReadOnly(true);
    splitter->addWidget(historyList);
    splitter->addWidget(previewPane);
    splitter->setSizes({300, 200});
    mainLayout->addWidget(splitter);

    connect(historyList, &ListView::itemDoubleClicked, this, &ClipboardManager::onItemDoubleClicked);
}

void havel::ClipboardManager::onClipboardChanged() {
    auto text = clipboard->text();
    if (!text.isEmpty() && text != lastClipboard) {
        addToHistory(text);
        lastClipboard = text;
    }
}

void havel::ClipboardManager::onItemDoubleClicked(QListWidgetItem* item) {
    clipboard->setText(item->text());
    showTrayMessage("Copied to clipboard!");
}

void havel::ClipboardManager::addToHistory(const QString& text) {
    historyList->insertItem(0, text);
    if (historyList->count() > 100) { // Limit history size
        delete historyList->takeItem(100);
    }
}

void havel::ClipboardManager::showTrayMessage(const QString& message) {
    trayIcon->showMessage("Clipboard Manager", message, QSystemTrayIcon::Information, 2000);
}
