#pragma once

#include <QMainWindow>
#include <QClipboard>
#include <QSystemTrayIcon>
#include <QListWidget>
#include <QLineEdit>
#include <QTextEdit>
#include <QSplitter>
#include <QVBoxLayout>
#include <QShortcut>
#include <QIcon>
#include <QSettings>
#include <QAction>
#include <QSize>
#include "core/IO.hpp"

namespace havel {

class ClipboardManager : public QMainWindow {
    Q_OBJECT

public:
    explicit ClipboardManager(IO* io, QWidget* parent = nullptr);
    ~ClipboardManager();

    void initializeHotkeys();
    void toggleVisibility();

    // Non-copyable
    ClipboardManager(const ClipboardManager&) = delete;
    ClipboardManager& operator=(const ClipboardManager&) = delete;

    QClipboard* getClipboard() const { return clipboard; }

public slots:
    void showAndFocus();

protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void onClipboardChanged();
    void onItemDoubleClicked(QListWidgetItem* item);
    void onSearchTextChanged(const QString& text);
    void onItemSelectionChanged();
    void onItemClicked(QListWidgetItem *item);
    void onClearAll();
    void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason);
    void showContextMenu(const QPoint &pos);

private:
    void setupUI();
    void setupShortcuts();
    void addToHistory(const QString& text);
    void filterHistory(const QString& filter);
    void showTrayMessage(const QString& message);
    void copySelectedItem();
    void removeSelectedItem();
    void pasteHistoryItem(int index);
    void onHotkeyPressed();

    // UI Components
    QWidget* centralWidget;
    QLineEdit* searchBox;
    QListWidget* historyList;
    QTextEdit* previewPane;
    QSplitter* splitter;
    QClipboard* clipboard;
    QSystemTrayIcon* trayIcon;
    
    // Shortcuts
    QShortcut* showShortcut;
    QShortcut* deleteShortcut;
    QShortcut* escapeShortcut;
    
    // Configuration
    IO* io;
    int fontSize = 14;
    QSize windowSize = QSize(700, 800);
    
    // State
    QString lastClipboard;
    QStringList fullHistory;
    
    static constexpr int MAX_HISTORY_SIZE = 100;
    static constexpr int PREVIEW_MAX_LENGTH = 1000;
};

} // namespace havel