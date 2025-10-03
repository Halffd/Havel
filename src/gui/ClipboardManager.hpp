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
#include <QMimeData>
#include <QDateTime>
#include <QImage>
#include <QFileInfo>
#include <QUrl>
#include <QDir>
#include <QJsonArray>
#include "core/IO.hpp"

namespace havel {

class ClipboardManager : public QMainWindow  {
    Q_OBJECT
public:
    explicit ClipboardManager(IO* io, QWidget* parent = nullptr);
    ~ClipboardManager();

    void initializeHotkeys();
    Q_INVOKABLE void toggleVisibility();
    Q_INVOKABLE void pasteHistoryItem(int index);

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
    void copySelectedItem();
    void removeSelectedItem();

    // Clipboard content types
    enum class ContentType {
        Text,
        Markdown,
        Html,
        Image,
        FileList,
        Color,
        Code,
        Unknown
    };
    
    // File type filters
    struct FileTypeFilter {
        QString name;
        QStringList extensions;
    };

    struct ClipboardItem {
        ContentType type;
        QString displayText;
        QVariant data;
        QDateTime timestamp;
        QString preview;
    };

private:
    void setupUI();
    void setupShortcuts();
    void processClipboardContent();
    void addToHistory(const ClipboardItem& item);
    void addToHistory(const QString& text); // Overload for text
    void filterHistory(const QString& filter);
    void showTrayMessage(const QString& message);
    void onHotkeyPressed();
    void updateHistoryOrder();
    QString getClipboardText(ContentType type) const;
    QString formatFileList(const QList<QUrl>& urls) const;
    QString getMimeTypeIcon(ContentType type) const;
    bool isFileTypeAllowed(const QString& fileName) const;
    void loadSettings();
    void saveSettings();
    static QString markdownToHtml(const QString& markdown);
    
    // Persistence methods
    void loadHistory();
    void saveHistory();
    QString getHistoryBasePath() const;
    QString ensureDirectories() const;
    QString saveTextToFile(const QString& text, int index);
    QString saveImageToFile(const QImage& image, int index);
    QString saveFileListToFile(const QList<QUrl>& urls, int index);
    ClipboardItem loadItemFromFile(const QJsonObject& json);
    void removeHistoryFiles(int index);
    
    // Member variables
    QList<FileTypeFilter> fileTypeFilters;
    QList<ContentType> enabledContentTypes;
    ClipboardItem lastClipboardItem;
    QList<ClipboardItem> historyItems;
    QStringList fullHistory; // For backward compatibility
    QJsonArray historyIndex;
    
    // UI Components
    QWidget* centralWidget = nullptr;
    QLineEdit* searchBox = nullptr;
    QListWidget* historyList = nullptr;
    QTextEdit* previewPane = nullptr;
    QSplitter* splitter = nullptr;
    QClipboard* clipboard = nullptr;
    QSystemTrayIcon* trayIcon = nullptr;
    
    // Shortcuts
    QShortcut* showShortcut = nullptr;
    QShortcut* deleteShortcut = nullptr;
    QShortcut* escapeShortcut = nullptr;
    
    // Configuration
    IO* io = nullptr;
    bool shown = false;
    int fontSize = 28;
    int lastRow = 1;
    QSize windowSize = QSize(700, 800);
    
    // State
    QString lastClipboard;
    
    static constexpr int MAX_HISTORY_SIZE = 100;
    static constexpr int PREVIEW_MAX_LENGTH = 1000;
};

} // namespace havel