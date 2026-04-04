#pragma once
#include <string>
#include <vector>
#include <QObject>
#include <QMainWindow>
#include <QClipboard>
#include <QListWidget>
#include <QLineEdit>
#include <QSystemTrayIcon>
#include <QShortcut>
#include <QDateTime>
#include <QJsonObject>
#include <QVariant>

namespace havel {

class IO;

class ClipboardManager : public QMainWindow {
    Q_OBJECT
public:
    enum class ContentType { Unknown, Text, Markdown, Html, Image, FileList };
    
    struct ClipboardItem {
        ContentType type = ContentType::Unknown;
        QDateTime timestamp;
        QString displayText;
        QString preview;
        QVariant data;
    };

    explicit ClipboardManager(IO* io = nullptr, QWidget* parent = nullptr);
    ~ClipboardManager();

    static ClipboardManager& getInstance() {
        static ClipboardManager instance;
        return instance;
    }

    void enable();
    void disable();
    bool isEnabled() const;
    
    void showAndFocus();
    void hide();
    
    // Legacy support for scripts
    void addToHistory(const std::string& content);
    void clearHistory();

private slots:
    void onClipboardChanged();
    void onSearchTextChanged(const QString& text);
    void onItemDoubleClicked(QListWidgetItem* item);
    void onItemSelectionChanged();
    void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason);
    void onPasteRequested(int index);
    void onItemClicked(QListWidgetItem* item);
    void showContextMenu(const QPoint& pos);
    void removeSelectedItem();
    void copySelectedItems();
    void onHotkeyPressed();
    void onClearAll();
    void onCustomContextMenuRequested(const QPoint &pos);

signals:
    void pasteRequested(int index);

private:
    void setupUI();
    void setupFonts();
    void setupShortcuts();
    void loadSettings();
    void saveSettings();
    void loadHistory();
    void saveHistory();
    QString getHistoryBasePath() const;
    QString ensureDirectories() const;
    QString saveTextToFile(const QString& text, int index);
    QString saveImageToFile(const QImage& image, int index);
    QString saveFileListToFile(const QList<QUrl>& urls, int index);
    void removeHistoryFiles(int index);
    ClipboardItem loadItemFromFile(const QJsonObject& json);
    void filterHistory(const QString& text);
    void initializeHotkeys();
    int getMaxHistorySize() const;
    QListWidgetItem* createSafeListItem(const QString& text);
    void onItemChanged(QListWidgetItem* item);
    void toggleVisibility();
    void pasteHistoryItem(int index);
    void updateHistoryOrder();
    void processClipboardContent();
    bool isFileTypeAllowed(const QString& fileName) const;
    QString formatFileList(const QList<QUrl>& urls) const;
    QString markdownToHtml(const QString& markdown);
    QString truncateText(const QString &text, int maxLength);
    void animateItemDelete(QListWidgetItem* item);
    void showWithFade();
    void hideWithFade();

    IO* io = nullptr;
    static QClipboard* clipboard;
    QLineEdit* searchBox = nullptr;
    QListWidget* historyList = nullptr;
    QSystemTrayIcon* trayIcon = nullptr;
    QList<ClipboardItem> historyItems;
    QStringList fullHistory;
    QShortcut* showShortcut = nullptr;
    QShortcut* deleteShortcut = nullptr;
    QShortcut* escapeShortcut = nullptr;
    QTextEdit* previewPane = nullptr;
    QSize windowSize;
    int fontSize = 10;
    bool tray = true;
    int lastRow = -1;
    int maxHistorySize = 1000;
    int previewMaxLength = 1000;
    int displayedItemsLimit = 50;
    bool enabled = true;
    bool m_isSettingClipboard = false;
    bool m_isProcessingClipboardChange = false;
    QList<ClipboardItem> enabledContentTypes;
    ClipboardItem lastClipboardItem;
    QString lastClipboard;
    QJsonArray historyIndex;
    
    struct FileTypeFilter {
        QString name;
        QStringList extensions;
    };
    std::vector<FileTypeFilter> fileTypeFilters;

    struct UIConfig {
        static constexpr int WINDOW_MIN_WIDTH = 400;
        static constexpr int WINDOW_MIN_HEIGHT = 500;
        static constexpr int ITEM_HEIGHT = 40;
        static constexpr int BASE_FONT_SIZE = 10;
        static constexpr int SPLITTER_HANDLE_WIDTH = 1;
        static const char* FONT_FAMILY;
    };

    struct Colors {
        static const char* BACKGROUND;
        static const char* TEXT_PRIMARY;
        static const char* TEXT_SECONDARY;
        static const char* BORDER;
        static const char* SURFACE;
        static const char* SURFACE_LIGHT;
        static const char* SURFACE_LIGHTER;
        static const char* PRIMARY;
        static const char* PRIMARY_LIGHT;
    };

    std::map<std::string, std::vector<std::string>> fileTypeFilters;
    std::vector<ContentType> enabledContentTypes;
};

} // namespace havel
