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

namespace havel {

class ClipboardManager : public QMainWindow {
    Q_OBJECT

public:
    explicit ClipboardManager(QWidget* parent = nullptr);
    ~ClipboardManager() = default;

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

private:
    void setupUI();
    void setupShortcuts();
    void addToHistory(const QString& text);
    void showTrayMessage(const QString& message);
    void filterHistory(const QString& filter);
    void copySelectedItem();
    void removeSelectedItem();

    // UI components
    QWidget* centralWidget;
    QLineEdit* searchBox;
    QListWidget* historyList;  // Changed from QListView to QListWidget
    QTextEdit* previewPane;
    QSplitter* splitter;
    
    // System components
    QClipboard* clipboard;
    QSystemTrayIcon* trayIcon;
    
    // Shortcuts
    QShortcut* showShortcut;
    QShortcut* deleteShortcut;
    QShortcut* escapeShortcut;
    
    // State
    QString lastClipboard;
    QStringList fullHistory;
    
    static constexpr int MAX_HISTORY_SIZE = 100;
    static constexpr int PREVIEW_MAX_LENGTH = 1000;
};

} // namespace havel