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
#include <QJsonArray>
#include <QDir>
#include "core/IO.hpp"

namespace havel {

// Global UI Configuration
namespace UIConfig {
    // Font settings
    const int BASE_FONT_SIZE = 11;
    const QString FONT_FAMILY = "Segoe UI";  // Will fall back to system font if not available
    
    // Colors
    namespace Colors {
        const QString BACKGROUND = "#1E1E1E";
        const QString SURFACE = "#252526";
        const QString SURFACE_LIGHT = "#2D2D30";
        const QString SURFACE_LIGHTER = "#3E3E42";
        const QString TEXT_PRIMARY = "#E0E0E0";
        const QString TEXT_SECONDARY = "#A0A0A0";
        const QString PRIMARY = "#007ACC";
        const QString PRIMARY_LIGHT = "#1C97EA";
        const QString BORDER = "#3F3F46";
    }
    
    // Sizing
    const int WINDOW_MIN_WIDTH = 800;
    const int WINDOW_MIN_HEIGHT = 600;
    const int SPLITTER_HANDLE_WIDTH = 8;
    const int PREVIEW_MIN_HEIGHT = 200;
    const int ITEM_HEIGHT = 48;
}

class ClipboardManager : public QMainWindow  {
    Q_OBJECT
public:
    explicit ClipboardManager(IO* io, QWidget* parent = nullptr);
    ~ClipboardManager();
    static QClipboard* clipboard    ;

    void initializeHotkeys();
    Q_INVOKABLE void toggleVisibility();
    Q_INVOKABLE void pasteHistoryItem(int index);

    // Non-copyable
    ClipboardManager(const ClipboardManager&) = delete;
    ClipboardManager& operator=(const ClipboardManager&) = delete;

    QClipboard* getClipboard() const { return clipboard; }

    // Helper function for truncating text
    QString truncateText(const QString &text, int maxLength);

signals:
    void pasteRequested(int index);

private slots:
    void onPasteRequested(int index);

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
    void onCustomContextMenuRequested(const QPoint& pos);
    void editSelectedItem();
    void onItemChanged(QListWidgetItem* item);
    void onClearAll();
    void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason);
    void showContextMenu(const QPoint &pos);
    void copySelectedItem();
    void removeSelectedItem();

private:
    // RAII helper to manage the m_isSettingClipboard flag
    class ClipboardSettingGuard {
    public:
        explicit ClipboardSettingGuard(ClipboardManager* manager) : m_manager(manager) {
            m_manager->m_isSettingClipboard = true;
        }
        ~ClipboardSettingGuard() {
            m_manager->m_isSettingClipboard = false;
        }
    private:
        ClipboardManager* m_manager;
    };

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
        QVariant data;  // Stores the actual content (text, image, etc.)
        QString displayText;
        QDateTime timestamp;
        QString preview;

        // Default constructor
        ClipboardItem() = default;

        // Constructor with data
        ClipboardItem(ContentType t, const QVariant& d, const QString& dt = "",
                     const QDateTime& ts = QDateTime::currentDateTime(),
                     const QString& p = "")
            : type(t), data(d), displayText(dt), timestamp(ts), preview(p) {
            if (displayText.isEmpty() && data.canConvert<QString>()) {
                displayText = data.toString();
            }
        }
    };

    void setupUI();
    void setupShortcuts();
    void setupFonts();
    void setupRenderingSafety();
    void processClipboardContent();
    void addToHistory(const ClipboardItem& item);
    void addToHistory(const QString& text); // Overload for text
    void filterHistory(const QString& filter);
    void showTrayMessage(const QString& message);
    
    bool m_isSettingClipboard = false; // Flag to prevent notification loops
    void onHotkeyPressed();
    void updateHistoryOrder();
    QListWidgetItem* createSafeListItem(const QString& text);
    QString getClipboardText(ContentType type) const;
    QString formatFileList(const QList<QUrl>& urls) const;
    bool isFileTypeAllowed(const QString& fileName) const;
    void loadSettings();
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
    void saveSettings();
    // Member variables
    QList<FileTypeFilter> fileTypeFilters;
    QList<ContentType> enabledContentTypes;
    ClipboardItem lastClipboardItem;
    bool m_isProcessingClipboardChange = false;
    QList<ClipboardItem> historyItems;
    QStringList fullHistory; // For backward compatibility
    QJsonArray historyIndex;
    
    // UI Components
    QWidget* centralWidget = nullptr;
    QLineEdit* searchBox = nullptr;
    QListWidget* historyList = nullptr;
    QTextEdit* previewPane = nullptr;
    QSplitter* splitter = nullptr;
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
    
    // Configuration values
    int maxHistorySize = 1000;
    int previewMaxLength = 1000;
    
    // Getters for configuration values
    int getMaxHistorySize() const { return maxHistorySize; }
    int getPreviewMaxLength() const { return previewMaxLength; }

    // Settings for customization
    int getFontSize() const { return fontSize; }
    void setFontSize(int size) { fontSize = size; }
    int getDisplayedItemsLimit() const { return displayedItemsLimit; }
    void setDisplayedItemsLimit(int limit) { displayedItemsLimit = limit; }
    bool isEnabled() const { return enabled; }
    void setEnabled(bool value) { enabled = value; }

private:
    // Add new member variables for settings
    int displayedItemsLimit = 50;  // Limit of items to display
    bool enabled = true;           // Whether the clipboard manager is enabled
    bool showPreviewPane = false;  // Whether to show the preview pane (now single panel)
};

} // namespace havel