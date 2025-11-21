#include "ClipboardManager.hpp"
#include "core/ConfigManager.hpp"
#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QJsonArray>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QMessageBox>
#include <QHeaderView>
#include <QDateTime>
#include <QClipboard>
#include <QDebug>
#include <QDateTime>
#include <QFileInfo>
#include <QPainter>
#include <QTextDocumentFragment>
#include <QRegularExpression>
#include <QFileInfo>
#include <QStatusBar>
#include <QDir>
#include <QSettings>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextDocumentFragment>
#include <QScreen>
#include <QCursor>
#include <QPainter>
#include <QBuffer>
#include <QFontDatabase>
#include <QStyleFactory>
#include <QPainterPath>
#include <QTimer>
#include <QMenu>

namespace havel {
    QClipboard* ClipboardManager::clipboard = nullptr;
// File system operations
QString ClipboardManager::getHistoryBasePath() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/clipboard_history";
}

QString ClipboardManager::ensureDirectories() const {
    QString basePath = getHistoryBasePath();
    QDir baseDir(basePath);
    
    if (!baseDir.exists()) {
        baseDir.mkpath(".");
    }
    
    QDir textsDir(basePath + "/texts");
    if (!textsDir.exists()) {
        textsDir.mkpath(".");
    }
    
    QDir imagesDir(basePath + "/images");
    if (!imagesDir.exists()) {
        imagesDir.mkpath(".");
    }
    
    QDir filesDir(basePath + "/files");
    if (!filesDir.exists()) {
        filesDir.mkpath(".");
    }
    
    return basePath;
}

QString ClipboardManager::saveTextToFile(const QString& text, int index) {
    QString basePath = ensureDirectories();
    QString filePath = basePath + "/texts/" + QString::number(index) + ".txt";
    
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << text;
        file.close();
        return filePath;
    }
    
    return QString();
}

QString ClipboardManager::saveImageToFile(const QImage& image, int index) {
    QString basePath = ensureDirectories();
    QString filePath = basePath + "/images/" + QString::number(index) + ".png";
    
    if (image.save(filePath, "PNG")) {
        return filePath;
    }
    
    return QString();
}

QString ClipboardManager::saveFileListToFile(const QList<QUrl>& urls, int index) {
    QString basePath = ensureDirectories();
    QString filePath = basePath + "/files/" + QString::number(index) + ".txt";
    
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&file);
        for (const QUrl& url : urls) {
            stream << url.toString() << "\n";
        }
        file.close();
        return filePath;
    }
    
    return QString();
}

void ClipboardManager::removeHistoryFiles(int index) {
    if (index < 0) return;
    
    QString basePath = getHistoryBasePath();
    QDir baseDir(basePath);
    
    // Remove text file
    QString textPath = basePath + "/texts/" + QString::number(index) + ".txt";
    if (QFile::exists(textPath)) {
        QFile::remove(textPath);
    }
    
    // Remove image file
    QString imagePath = basePath + "/images/" + QString::number(index) + ".png";
    if (QFile::exists(imagePath)) {
        QFile::remove(imagePath);
    }
    
    // Remove file list
    QString fileListPath = basePath + "/files/" + QString::number(index) + ".txt";
    if (QFile::exists(fileListPath)) {
        QFile::remove(fileListPath);
    }
    
    // Clean up empty directories
    QDir(basePath + "/texts").removeRecursively();
    QDir(basePath + "/images").removeRecursively();
    QDir(basePath + "/files").removeRecursively();
    
    // Recreate directories if they were removed
    ensureDirectories();
}

void ClipboardManager::saveHistory() {
    if (historyItems.isEmpty()) {
        return;  // Nothing to save
    }
    
    QString basePath = ensureDirectories();
    QString indexFilePath = basePath + "/index.json";
    
    // Check if the history has been modified since last save
    static QDateTime lastSaveTime = QDateTime::currentDateTime();
    bool hasChanges = false;
    
    // Check if we have any items that were added or modified since last save
    for (const auto& item : historyItems) {
        if (item.timestamp >= lastSaveTime) {
            hasChanges = true;
            break;
        }
    }
    
    if (!hasChanges) {
        qDebug() << "No changes detected, skipping save";
        return;
    }
    
    QJsonArray indexArray;
    
    // Save only the most recent items to prevent history from growing too large
    int maxItems = getMaxHistorySize() > 0 ? getMaxHistorySize() : historyItems.size();
    int startIdx = qMax(0, historyItems.size() - maxItems);
    
    // Create a temporary file first
    QString tempPath = indexFilePath + ".tmp";
    QFile tempFile(tempPath);
    
    if (!tempFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Failed to create temporary file:" << tempPath;
        return;
    }
    
    // Save to temporary file first
    for (int i = startIdx; i < historyItems.size(); ++i) {
        const auto& item = historyItems[i];
        QJsonObject indexEntry;
        
        indexEntry["timestamp"] = item.timestamp.toString(Qt::ISODate);
        indexEntry["type"] = static_cast<int>(item.type);
        indexEntry["displayText"] = item.displayText;
        indexEntry["preview"] = item.preview;
        
        // Save content to file and store the path
        switch (item.type) {
            case ContentType::Text:
            case ContentType::Markdown:
            case ContentType::Html: {
                QString filePath = saveTextToFile(item.data.toString(), i);
                if (!filePath.isEmpty()) {
                    indexEntry["filePath"] = filePath;
                    indexEntry["contentType"] = "text";
                }
                break;
            }
            case ContentType::Image: {
                QImage image = qvariant_cast<QImage>(item.data);
                QString filePath = saveImageToFile(image, i);
                if (!filePath.isEmpty()) {
                    indexEntry["filePath"] = filePath;
                    indexEntry["contentType"] = "image";
                }
                break;
            }
            case ContentType::FileList: {
                QList<QUrl> urls = qvariant_cast<QList<QUrl>>(item.data);
                QString filePath = saveFileListToFile(urls, i);
                if (!filePath.isEmpty()) {
                    indexEntry["filePath"] = filePath;
                    indexEntry["contentType"] = "filelist";
                }
                break;
            }
            default:
                break;
        }
        
        if (!indexEntry.isEmpty()) {
            indexArray.append(indexEntry);
        }
    }
    
    // Write to temporary file
    QJsonDocument doc(indexArray);
    tempFile.write(doc.toJson());
    tempFile.close();
    
    // Atomically replace the old file with the new one
    QFile::remove(indexFilePath);
    if (!tempFile.rename(indexFilePath)) {
        qWarning() << "Failed to replace index file:" << indexFilePath;
        QFile::remove(tempPath);  // Clean up temp file
        return;
    }
    
    lastSaveTime = QDateTime::currentDateTime();
    qDebug() << "Saved" << indexArray.size() << "items to" << basePath;
}


ClipboardManager::ClipboardItem ClipboardManager::loadItemFromFile(const QJsonObject& json) {
    
ClipboardManager::ClipboardItem item;
    
    item.type = static_cast<ContentType>(json["type"].toInt());
    item.timestamp = QDateTime::fromString(json["timestamp"].toString(), Qt::ISODate);
    item.displayText = json["displayText"].toString();
    item.preview = json["preview"].toString();
    
    QString filePath = json["filePath"].toString();
    QString contentType = json["contentType"].toString();
    
    if (!filePath.isEmpty() && QFile::exists(filePath)) {
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly)) {
            if (contentType == "text") {
                QTextStream stream(&file);
                QString content = stream.readAll();
                item.data = content;
                file.close();
            } else if (contentType == "image") {
                QImage image;
                if (image.load(filePath)) {
                    item.data = image;
                }
            } else if (contentType == "filelist") {
                QTextStream stream(&file);
                QList<QUrl> urls;
                while (!stream.atEnd()) {
                    QString line = stream.readLine();
                    if (!line.isEmpty()) {
                        urls.append(QUrl(line));
                    }
                }
                item.data = QVariant::fromValue(urls);
                file.close();
            }
        }
    }
    
    return item;
}

void ClipboardManager::loadHistory() {
    QString basePath = ensureDirectories();
    QString indexFilePath = basePath + "/index.json";
    
    if (!QFile::exists(indexFilePath)) {
        qDebug() << "No history index found at:" << indexFilePath;
        return;
    }
    
    QFile indexFile(indexFilePath);
    if (!indexFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Failed to open index file:" << indexFilePath;
        return;
    }
    
    QByteArray data = indexFile.readAll();
    indexFile.close();
    
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError) {
        qWarning() << "Failed to parse history index:" << error.errorString();
        return;
    }
    
    if (!doc.isArray()) {
        qWarning() << "Invalid index file format: root is not an array";
        return;
    }
    
    QJsonArray indexArray = doc.array();
    QList<ClipboardItem> loadedItems;
    QStringList loadedTextHistory;
    
    // Load items from disk
    for (const QJsonValue& value : indexArray) {
        if (!value.isObject()) {
            qWarning() << "Skipping invalid history item (not an object)";
            continue;
        }
        
        QJsonObject json = value.toObject();
        ClipboardItem item = loadItemFromFile(json);
        
        if (!item.data.isNull()) {
            loadedItems.prepend(item); // Prepend to reverse order (newest first)
            
            // Also populate simple text history
            if (item.type == ContentType::Text || item.type == ContentType::Markdown) {
                loadedTextHistory.prepend(item.data.toString());
            }
        }
    }
    
    // Update the model data
    historyItems = loadedItems;
    fullHistory = loadedTextHistory;
    
    // Update UI if visible
    if (isVisible() && historyList) {
        filterHistory(searchBox->text());
    }
    
    qInfo() << "Loaded" << historyItems.size() << "history items from" << basePath;
}

ClipboardManager::~ClipboardManager() {
    // Save history before closing
    saveHistory();
    
    // Clean up resources
    if (trayIcon) {
        trayIcon->hide();
    }
    
    delete showShortcut;
    delete deleteShortcut;
    delete escapeShortcut;
}

ClipboardManager::ClipboardManager(IO* io, QWidget* parent) 
    : QMainWindow(parent)
    , io(io) {
    
    // Enable style sheet propagation in widget styles
    QApplication::setAttribute(Qt::AA_UseStyleSheetPropagationInWidgetStyles, true);
    
    // Set a style that works well with the current theme
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    
    // Set up the application name and organization for settings
    QCoreApplication::setOrganizationName("havel");
    QCoreApplication::setApplicationName("ClipboardManager");
    
    // Initialize file type filters first
    fileTypeFilters = {
        {"Images", {"*.png", "*.jpg", "*.jpeg", "*.gif", "*.bmp", "*.svg"}},
        {"Documents", {"*.pdf", "*.doc", "*.docx", "*.odt", "*.txt", "*.md"}},
        {"Archives", {"*.zip", "*.rar", "*.7z", "*.tar", "*.gz"}},
        {"Code", {"*.cpp", "*.h", "*.hpp", "*.c", "*.py", "*.js", "*.html", "*.css"}}
    };
    
    // Enable all content types by default
    enabledContentTypes = {
        ContentType::Text,
        ContentType::Markdown,
        ContentType::Html,
        ContentType::Image,
        ContentType::FileList
    };
    
    // Initialize clipboard first
    clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        qWarning() << "Failed to get clipboard instance";
    } else {
        qDebug() << "Clipboard instance obtained";
    }
    
    // Connect clipboard signal
    if (clipboard) {
        bool connected = connect(clipboard, &QClipboard::dataChanged, 
                               this, &ClipboardManager::onClipboardChanged,
                               Qt::QueuedConnection);
        qDebug() << "Clipboard signal connection" << (connected ? "succeeded" : "failed");
    }
    
    // Load saved settings and history
    loadSettings();
    loadHistory();
    
    // Set window properties
    //setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    
    // Setup UI with custom font size and window size
    setupUI();

    // Setup system tray icon
    trayIcon = new QSystemTrayIcon(this);
    QIcon icon = QIcon::fromTheme("edit-paste");
    if (icon.isNull()) {
        // Create a better fallback icon
        QPixmap pixmap(32, 32);
        pixmap.fill(Qt::transparent);
        
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(QColor(100, 150, 200));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(2, 2, 28, 28);
        
        // Add a simple "P" for paste
        painter.setPen(Qt::white);
        painter.setFont(QFont("Arial", 16, QFont::Bold));
        painter.drawText(pixmap.rect(), Qt::AlignCenter, "P");
        
        icon = QIcon(pixmap);
    }
    trayIcon->setIcon(icon);
    trayIcon->setToolTip("Clipboard Manager");
    trayIcon->show();
    connect(trayIcon, &QSystemTrayIcon::activated,
            this, &ClipboardManager::onTrayIconActivated);

    // Setup hotkey for showing/hiding the window
    if (io) {
        initializeHotkeys();
    }
    setupShortcuts();
    
    windowSize = QSize(Configs::Get().Get<int>("ClipboardManager.Width", 900), 
                      Configs::Get().Get<int>("ClipboardManager.Height", 1000));
    // Hide by default - show only when needed
    hide();
}

void ClipboardManager::setupFonts() {
    // Get system default font
    QFont appFont = QApplication::font();
    
    // Set a safe default font family
    QStringList preferredFonts = {
        "Segoe UI", "Arial", "Noto Sans", "DejaVu Sans", "Liberation Sans",
        "Helvetica", "Verdana", "Tahoma", "Ubuntu", "Roboto"
    };
    
    // Try to find and set a preferred font
    for (const QString& fontName : preferredFonts) {
        if (QFontDatabase::hasFamily(fontName)) {
            appFont.setFamily(fontName);
            break;
        }
    }
    
    // Set reasonable font size and style
    appFont.setPointSize(10);
    appFont.setStyleHint(QFont::SansSerif);
    appFont.setStyleStrategy(QFont::PreferAntialias);
    
    // Apply to application
    QApplication::setFont(appFont);
    setFont(appFont);
    
    // Set up status bar font
    QFont statusFont = appFont;
    statusFont.setPointSize(appFont.pointSize() * 0.8);
    statusBar()->setFont(statusFont);
}

QListWidgetItem* ClipboardManager::createSafeListItem(const QString& text) {
    QListWidgetItem* item = new QListWidgetItem(text);
    
    // Set safe font
    QFont itemFont = font();
    itemFont.setPointSize(font().pointSize());
    item->setFont(itemFont);
    
    // Set reasonable size hints
    QFontMetrics fm(itemFont);
    int height = qMax(fm.height() * 1.5, 24.0); // Minimum 24px height
    item->setSizeHint(QSize(item->sizeHint().width(), static_cast<int>(height)));
    
    // Enable necessary flags
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
    
    // Set text color that works with both light and dark themes
    item->setForeground(Qt::white);
    
    return item;
}

void ClipboardManager::setupUI() {
    using namespace UIConfig;
    
    // Set up fonts
    setupFonts();
    
    // Set window properties
    setWindowTitle("Clipboard Manager");
    setMinimumSize(WINDOW_MIN_WIDTH, WINDOW_MIN_HEIGHT);
    
    // Set window flags for better behavior
    setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint | Qt::CustomizeWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, false);
    
    // Create central widget and main layout
    centralWidget = new QWidget(this);
    centralWidget->setAutoFillBackground(true);
    setCentralWidget(centralWidget);
    
    // Set initial window size based on screen size
    QScreen* screen = QGuiApplication::primaryScreen();
    if (screen) {
        QRect screenGeometry = screen->availableGeometry();
        int width = qMin(WINDOW_MIN_WIDTH, static_cast<int>(screenGeometry.width() * 0.8));
        int height = qMin(WINDOW_MIN_HEIGHT, static_cast<int>(screenGeometry.height() * 0.7));
        resize(width, height);
    } else {
        resize(WINDOW_MIN_WIDTH, WINDOW_MIN_HEIGHT);
    }
    // Set window stylesheet using theme variables
    QString styleSheet = QString(R"(
        QMainWindow, QDialog, QWidget#centralWidget {
            background-color: %1;
            color: %2;
            border: 1px solid %3;
            border-radius: 8px;
        }
        
        QListWidget {
            background-color: %4;
            border: 1px solid %3;
            border-radius: 6px;
            padding: 6px;
            outline: 0;
            margin: 0;
            show-decoration-selected: 1;
            font-size: %9px;
        }
        
        QListWidget::item {
            background-color: %5;
            color: %2;
            padding: 12px 16px;
            border-radius: 6px;
            margin: 4px 2px;
            border: 1px solid transparent;
            min-height: %10px;
        }
        
        QListWidget::item:selected {
            background-color: %6;
            color: white;
            border: 1px solid %7;
        }
        
        QListWidget::item:hover {
            background-color: %6;
            border: 1px solid %7;
        }
        
        QLineEdit {
            background-color: %5;
            color: %2;
            border: 1px solid %3;
            border-radius: 6px;
            padding: 12px 16px;
            selection-background-color: %7;
            font-size: %8px;
            margin-bottom: 8px;
            min-height: %11px;
        }
        
        QLineEdit:focus {
            border: 1px solid %7;
            background-color: %6;
        }
        
        QTextEdit {
            background-color: %4;
            color: %2;
            border: 1px solid %3;
            border-radius: 6px;
            padding: 16px;
            selection-background-color: %7;
            font-size: %9px;
            line-height: 1.5;
        }
        
        QTextEdit:focus {
            border: 1px solid %7;
        }
        
        QScrollBar:vertical {
            border: none;
            background: %4;
            width: 12px;
            margin: 2px;
            border-radius: 6px;
        }
        
        QScrollBar::handle:vertical {
            background: #4E4E50;
            min-height: 30px;
            border-radius: 6px;
            margin: 2px;
        }
        
        QScrollBar::handle:vertical:hover {
            background: #5E5E60;
        }
        
        QScrollBar::add-line:vertical, 
        QScrollBar::sub-line:vertical {
            height: 0px;
        }
        
        QScrollBar::add-page:vertical, 
        QScrollBar::sub-page:vertical {
            background: none;
        }
        
        /* Custom selection colors */
        QListWidget::item:selected:active {
            background: %7;
        }
        
        /* Search box placeholder text */
        QLineEdit::placeholder {
            color: %2;
            opacity: 0.6;
            font-style: italic;
        }
        
        /* Splitter styling */
        QSplitter::handle:horizontal {
            width: %12px;
            background: %3;
        }
        
        QSplitter::handle:horizontal:hover {
            background: %7;
        }
    )").arg(
        // Argument order must match the %1, %2, etc. in the template
        Colors::BACKGROUND,    // %1
        Colors::TEXT_PRIMARY,  // %2
        Colors::BORDER,        // %3
        Colors::SURFACE,       // %4
        Colors::SURFACE_LIGHT, // %5
        Colors::SURFACE_LIGHTER, // %6
        Colors::PRIMARY,       // %7
        QString::number(BASE_FONT_SIZE + 2), // %8 (search font size)
        QString::number(BASE_FONT_SIZE),     // %9 (base font size)
        QString::number(ITEM_HEIGHT),        // %10 (item height)
        QString::number(ITEM_HEIGHT + 8),    // %11 (search box height)
        QString::number(SPLITTER_HANDLE_WIDTH) // %12 (splitter handle width)
    );
    
    setStyleSheet(styleSheet);

    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);
    mainLayout->setAlignment(Qt::AlignTop);

    using namespace UIConfig;
    
    // Set up application font
    QFont appFont(FONT_FAMILY, BASE_FONT_SIZE);
    appFont.setStyleHint(QFont::SansSerif);
    qApp->setFont(appFont);
    setFont(appFont);
    
    // Set window attributes
    setWindowFlags(Qt::WindowStaysOnTopHint | Qt::Window);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setWindowOpacity(1.0);
    
    // Set application palette
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(Colors::BACKGROUND));
    darkPalette.setColor(QPalette::WindowText, QColor(Colors::TEXT_PRIMARY));
    darkPalette.setColor(QPalette::Base, QColor(Colors::SURFACE));
    darkPalette.setColor(QPalette::AlternateBase, QColor(Colors::SURFACE_LIGHT));
    darkPalette.setColor(QPalette::ToolTipBase, QColor(Colors::TEXT_PRIMARY));
    darkPalette.setColor(QPalette::ToolTipText, QColor(Colors::TEXT_PRIMARY));
    darkPalette.setColor(QPalette::Text, QColor(Colors::TEXT_PRIMARY));
    darkPalette.setColor(QPalette::Button, QColor(Colors::SURFACE_LIGHT));
    darkPalette.setColor(QPalette::ButtonText, QColor(Colors::TEXT_PRIMARY));
    darkPalette.setColor(QPalette::BrightText, QColor(Colors::PRIMARY_LIGHT));
    darkPalette.setColor(QPalette::Link, QColor(Colors::PRIMARY));
    darkPalette.setColor(QPalette::Highlight, QColor(Colors::PRIMARY));
    darkPalette.setColor(QPalette::HighlightedText, Qt::white);
    qApp->setPalette(darkPalette);

    // Search box with improved styling
    searchBox = new QLineEdit(this);
    searchBox->setPlaceholderText("Search clipboard history...");
    searchBox->setFont(appFont);
    searchBox->setClearButtonEnabled(true);
    searchBox->setMinimumHeight(ITEM_HEIGHT + 8);
    searchBox->setStyleSheet(QString(
        "QLineEdit { "
        "    padding: 12px 16px; "
        "    border-radius: 6px; "
        "    background: %1; "
        "    color: %2; "
        "    border: 1px solid %3; "
        "    margin-bottom: 8px;"
        "    font-size: %4px;"
        "    min-height: %5px;"
        "}"
        "QLineEdit:focus { "
        "    border: 1px solid %6; "
        "    background: %7;"
        "}"
        "QLineEdit::placeholder { "
        "    color: %8; "
        "    opacity: 0.6; "
        "    font-style: italic;"
        "}"
    ).arg(
        Colors::SURFACE_LIGHT,  // Background
        Colors::TEXT_PRIMARY,   // Text color
        Colors::BORDER,         // Border color
        QString::number(BASE_FONT_SIZE + 1),  // Font size
        QString::number(ITEM_HEIGHT + 8),     // Min height
        Colors::PRIMARY,         // Focus border
        Colors::SURFACE_LIGHTER, // Focus background
        Colors::TEXT_SECONDARY   // Placeholder color
    ));
    
    connect(searchBox, &QLineEdit::textChanged,
            this, &ClipboardManager::onSearchTextChanged);
    mainLayout->addWidget(searchBox);

    // Create splitter for resizable panes
    splitter = new QSplitter(Qt::Vertical, this);
    splitter->setHandleWidth(SPLITTER_HANDLE_WIDTH);
    splitter->setChildrenCollapsible(false);
    splitter->setOpaqueResize(true);
    
    // Create history list with improved settings
    historyList = new QListWidget();
    historyList->setObjectName("historyList");
    historyList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    historyList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    historyList->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    historyList->setSelectionMode(QAbstractItemView::SingleSelection);
    historyList->setTextElideMode(Qt::ElideRight);
    historyList->setSpacing(4);
    historyList->setFrameShape(QFrame::NoFrame);
    historyList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    historyList->setStyleSheet(QString(
        "QListWidget { "
        "    background: %1; "
        "    border: 1px solid %2; "
        "    border-radius: 6px; "
        "    padding: 6px; "
        "    outline: none; "
        "    font-size: %3px;"
        "}"
        "QListWidget::item { "
        "    background: %4; "
        "    color: %5; "
        "    padding: 12px 16px; "
        "    margin: 2px 0; "
        "    border-radius: 4px; "
        "    min-height: %6px;"
        "}"
        "QListWidget::item:selected { "
        "    background: %7; "
        "    border: 1px solid %8;"
        "}"
        "QListWidget::item:hover { "
        "    background: %9; "
        "    border: 1px solid %10;"
        "}"
    ).arg(
        Colors::SURFACE,        // Background
        Colors::BORDER,         // Border
        QString::number(BASE_FONT_SIZE),  // Font size
        Colors::SURFACE_LIGHT,  // Item background
        Colors::TEXT_PRIMARY,   // Text color
        QString::number(ITEM_HEIGHT),  // Item height
        Colors::PRIMARY,        // Selected background
        Colors::PRIMARY_LIGHT,  // Selected border
        Colors::SURFACE_LIGHTER, // Hover background
        Colors::BORDER          // Hover border
    ));
    
    // Connect signals
    connect(historyList, &QListWidget::itemDoubleClicked,
            this, &ClipboardManager::onItemDoubleClicked);
    connect(historyList->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &ClipboardManager::onItemSelectionChanged);
    historyList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    historyList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    historyList->setSelectionBehavior(QAbstractItemView::SelectRows);
    historyList->setSelectionMode(QAbstractItemView::SingleSelection);
    historyList->setSpacing(4);
    historyList->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    historyList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    historyList->setFocusPolicy(Qt::StrongFocus);
    historyList->setFrameShape(QFrame::NoFrame);
    
    // Ensure items are aligned to the top
    QVBoxLayout* listLayout = new QVBoxLayout();
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(0);
    listLayout->addWidget(historyList);
    
    QWidget* listContainer = new QWidget();
    listContainer->setLayout(listLayout);
    listContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    historyList->viewport()->setAcceptDrops(true);
    historyList->setDropIndicatorShown(true);
    historyList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(historyList, &QListWidget::itemDoubleClicked,
            this, &ClipboardManager::onItemDoubleClicked);
    connect(historyList, &QListWidget::itemSelectionChanged,
            this, &ClipboardManager::onItemSelectionChanged);
    connect(historyList, &QListWidget::itemClicked,
            this, &ClipboardManager::onItemClicked);
    connect(historyList, &QListWidget::customContextMenuRequested,
            this, &ClipboardManager::showContextMenu);
    // Connect item delegate signals
    connect(historyList->itemDelegate(), &QAbstractItemDelegate::commitData,
            this, [this](QWidget* editor) {
                QListWidgetItem* item = historyList->currentItem();
                if (item) onItemChanged(item);
            });
    splitter->addWidget(listContainer);

    // Preview pane
    previewPane = new QTextEdit(this);
    previewPane->setReadOnly(true);
    previewPane->setMaximumHeight(180);
    previewPane->setFrameStyle(QFrame::NoFrame);
    previewPane->setStyleSheet(QStringLiteral(
        "QTextEdit { "
        "    background-color: #252526; "
        "    border-radius: 6px; "
        "    padding: 12px;"
        "}"
    ));
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
    connect(escapeShortcut, &QShortcut::activated, this, &QWidget::close);

    // Enter/Return to copy
    auto* enterShortcut = new QShortcut(QKeySequence::InsertParagraphSeparator, this);
    connect(enterShortcut, &QShortcut::activated, this, &ClipboardManager::copySelectedItem);

    // Ctrl+F to focus search
    auto* searchShortcut = new QShortcut(QKeySequence::Find, this);
    connect(searchShortcut, &QShortcut::activated, [this]() {
        searchBox->setFocus();
        searchBox->selectAll();
    });

    // Enhanced navigation shortcuts from first version
    // Navigation shortcuts
    QShortcut* upShortcut = new QShortcut(QKeySequence("Up"), this);
    connect(upShortcut, &QShortcut::activated, this, [this]() {
        if (historyList->currentRow() > 0) {
            historyList->setCurrentRow(historyList->currentRow() - 1);
        }
    });

    QShortcut* downShortcut = new QShortcut(QKeySequence("Down"), this);
    connect(downShortcut, &QShortcut::activated, this, [this]() {
        if (historyList->currentRow() < historyList->count() - 1) {
            historyList->setCurrentRow(historyList->currentRow() + 1);
        }
    });

    // Ctrl+Up/Down to move items up/down
    QShortcut* moveUpShortcut = new QShortcut(QKeySequence("Ctrl+Up"), this);
    connect(moveUpShortcut, &QShortcut::activated, this, [this]() {
        int currentRow = historyList->currentRow();
        if (currentRow > 0) {
            QListWidgetItem* currentItem = historyList->takeItem(currentRow);
            historyList->insertItem(currentRow - 1, currentItem);
            historyList->setCurrentRow(currentRow - 1);
            updateHistoryOrder();
        }
    });

    QShortcut* moveDownShortcut = new QShortcut(QKeySequence("Ctrl+Down"), this);
    connect(moveDownShortcut, &QShortcut::activated, this, [this]() {
        int currentRow = historyList->currentRow();
        if (currentRow < historyList->count() - 1 && currentRow >= 0) {
            QListWidgetItem* currentItem = historyList->takeItem(currentRow);
            historyList->insertItem(currentRow + 1, currentItem);
            historyList->setCurrentRow(currentRow + 1);
            updateHistoryOrder();
        }
    });

    // Multi-selection shortcuts
    QShortcut* multiSelectDownShortcut = new QShortcut(QKeySequence("Shift+Down"), this);
    connect(multiSelectDownShortcut, &QShortcut::activated, this, [this]() {
        int nextRow = historyList->currentRow() + 1;
        if (nextRow < historyList->count()) {
            QListWidgetItem* item = historyList->item(nextRow);
            item->setSelected(!item->isSelected());
            historyList->setCurrentRow(nextRow);
        }
    });

    QShortcut* multiSelectUpShortcut = new QShortcut(QKeySequence("Shift+Up"), this);
    connect(multiSelectUpShortcut, &QShortcut::activated, this, [this]() {
        int prevRow = historyList->currentRow() - 1;
        if (prevRow >= 0) {
            QListWidgetItem* item = historyList->item(prevRow);
            item->setSelected(!item->isSelected());
            historyList->setCurrentRow(prevRow);
        }
    });

    // Shift+Enter to paste all selected items in order
    std::function<void(bool)> pasteSelected = [this](bool reverse) {
        QList<QListWidgetItem*> selected = historyList->selectedItems();
        QString mergedText;
        if (reverse) {
            std::reverse(selected.begin(), selected.end());
        }
        for (QListWidgetItem* item : selected) {
            QString text = item->data(Qt::UserRole).toString();
            mergedText += text + "\n";
        }
        if (clipboard) {
            clipboard->setText(mergedText);
        }
    };

    QShortcut* shiftEnterShortcut = new QShortcut(QKeySequence("Shift+Return"), this);
    connect(shiftEnterShortcut, &QShortcut::activated, this, [pasteSelected]() {
        pasteSelected(true);
    });

    // Connect the drop event to update history order
    connect(historyList->model(), &QAbstractItemModel::rowsMoved, 
            this, &ClipboardManager::updateHistoryOrder);

    //last row listener
    connect(historyList, &QListWidget::itemSelectionChanged, this, [this]() {
        //if multi selection is enabled, get topmost selected item
        if (historyList->selectionMode() == QAbstractItemView::ExtendedSelection && !historyList->selectedItems().isEmpty()) {
            QListWidgetItem* item = historyList->selectedItems().first();
            lastRow = historyList->row(item);
            return;
        }
        lastRow = historyList->currentRow();
    });
}

void ClipboardManager::initializeHotkeys() {
    if (!io) {
        qWarning() << "IO system not available for hotkey registration";
        return;
    }
        
    // Register Alt+V hotkey
    bool success = io->Hotkey("!c", [this]() {
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
            pasteHistoryItem(i - 1);
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

// Content type processing methods from second version
bool ClipboardManager::isFileTypeAllowed(const QString& fileName) const {
    if (enabledContentTypes.isEmpty()) return true;
    
    QFileInfo fileInfo(fileName);
    QString suffix = fileInfo.suffix().toLower();
    
    for (const auto& filter : fileTypeFilters) {
        if (filter.extensions.contains("*." + suffix, Qt::CaseInsensitive)) {
            return enabledContentTypes.contains(ContentType::FileList);
        }
    }
    
    return false;
}

void ClipboardManager::loadSettings() {
    QSettings settings("Havel", "ClipboardManager");
    
    // Load history size (default to 1000 if not set, 0 or negative means unlimited)
    maxHistorySize = settings.value("maxHistorySize", 1000).toInt();
    
    // Load preview max length (default to 1000 if not set)
    previewMaxLength = settings.value("previewMaxLength", 1000).toInt();
    
    // Load enabled content types
    QVariant enabledTypes = settings.value("enabledContentTypes");
    if (enabledTypes.isValid()) {
        enabledContentTypes.clear();
        for (const QVariant& type : enabledTypes.toList()) {
            enabledContentTypes.append(static_cast<ContentType>(type.toInt()));
        }
    }
}

void ClipboardManager::saveSettings() {
    QSettings settings("Havel", "ClipboardManager");
    
    // Save history size
    settings.setValue("maxHistorySize", maxHistorySize);
    
    // Save preview max length
    settings.setValue("previewMaxLength", previewMaxLength);
    
    // Save enabled content types
    QVariantList types;
    for (const auto& type : enabledContentTypes) {
        types.append(static_cast<int>(type));
    }
    settings.setValue("enabledContentTypes", types);
}

QString ClipboardManager::markdownToHtml(const QString& markdown) {
    // Simple markdown to HTML conversion
    QString html = markdown;
    
    // Headers
    html.replace(QRegularExpression("^#\\s+(.*)$", QRegularExpression::MultilineOption), "<h1>\\1</h1>");
    html.replace(QRegularExpression("^##\\s+(.*)$", QRegularExpression::MultilineOption), "<h2>\\1</h2>");
    html.replace(QRegularExpression("^###\\s+(.*)$", QRegularExpression::MultilineOption), "<h3>\\1</h3>");
    
    // Bold and italic
    html.replace(QRegularExpression("\\\\*{2}(.*?)\\*{2}"), "<b>\\1</b>");
    html.replace(QRegularExpression("__(.*?)__"), "<b>\\1</b>");
    html.replace(QRegularExpression("\\*([^*]+?)\\*"), "<i>\\1</i>");
    html.replace(QRegularExpression("_(.*?)_"), "<i>\\1</i>");
    
    // Links
    html.replace(QRegularExpression("\\[(.*?)\\]\\((.*?)\\)"), "<a href=\"\\2\">\\1</a>");
    
    // Lists
    html.replace(QRegularExpression("^\\s*[-*+]\\s+(.*)$", QRegularExpression::MultilineOption), "<li>\\1</li>");
    html.replace(QRegularExpression("(<li>.*</li>)", 
                 QRegularExpression::DotMatchesEverythingOption | QRegularExpression::InvertedGreedinessOption), 
                 "<ul>\\1</ul>");
    
    // Code blocks
    html.replace(QRegularExpression("```([^`]*)```"), "<pre><code>\\1</code></pre>");
    html.replace(QRegularExpression("`([^`]*)`"), "<code>\\1</code>");
    
    // Paragraphs
    html.replace("\n\n", "</p><p>");
    
    // Preserve line breaks
    html.replace("\n", "<br>");
    
    return "<html><body>" + html + "</body></html>";
}
void ClipboardManager::onCustomContextMenuRequested(const QPoint &pos) {
    QMenu contextMenu(this);
    
    QAction *copyAction = new QAction("Copy", this);
    connect(copyAction, &QAction::triggered, this, &ClipboardManager::copySelectedItem);
    
    QAction *deleteAction = new QAction("Delete", this);
    connect(deleteAction, &QAction::triggered, this, &ClipboardManager::removeSelectedItem);
    
    QAction *clearAllAction = new QAction("Clear All", this);
    connect(clearAllAction, &QAction::triggered, this, &ClipboardManager::onClearAll);
    
    contextMenu.addAction(copyAction);
    contextMenu.addAction(deleteAction);
    contextMenu.addAction(clearAllAction);
    
    contextMenu.exec(mapToGlobal(pos));
}
void ClipboardManager::processClipboardContent() {
    if (!clipboard) return;

    const QMimeData* mimeData = clipboard->mimeData();
    if (!mimeData) return;

    qDebug() << "=== Processing clipboard content ===";
    qDebug() << "Has text:" << mimeData->hasText() << "Has HTML:" << mimeData->hasHtml() 
             << "Has image:" << mimeData->hasImage() << "Has URLs:" << mimeData->hasUrls();

    ClipboardItem item;
    item.timestamp = QDateTime::currentDateTime();

    // Check for image data
    if (enabledContentTypes.contains(ContentType::Image) && mimeData->hasImage()) {
        QImage image = qvariant_cast<QImage>(mimeData->imageData());
        if (!image.isNull()) {
            item.type = ContentType::Image;
            item.data = image;
            item.displayText = tr("Image: %1x%2").arg(image.width()).arg(image.height());
            item.preview = tr("🖼️ [%1x%2]").arg(image.width()).arg(image.height());
            
            lastClipboardItem = item;
            addToHistory(item);
            return;
        }
    }
    
    // Check for URLs (files/links)
    if (enabledContentTypes.contains(ContentType::FileList) && mimeData->hasUrls()) {
        QList<QUrl> urls = mimeData->urls();
        
        // Filter URLs based on file type
        QList<QUrl> filteredUrls;
        for (const QUrl& url : urls) {
            if (url.isLocalFile()) {
                if (isFileTypeAllowed(url.toLocalFile())) {
                    filteredUrls.append(url);
                }
            } else {
                // Allow all remote URLs
                filteredUrls.append(url);
            }
        }
        
        if (!filteredUrls.isEmpty()) {
            item.type = ContentType::FileList;
            item.data = QVariant::fromValue(filteredUrls);
            item.displayText = formatFileList(filteredUrls);
            item.preview = tr("📁 %1 files").arg(filteredUrls.size());
            
            lastClipboardItem = item;
            addToHistory(item);
            return;
        }
    }
    
    // Check for HTML content
    if (enabledContentTypes.contains(ContentType::Html) && mimeData->hasHtml()) {
        QString html = mimeData->html();
        qDebug() << "Processing HTML content, length:" << html.length();
        
        // Extract plain text from HTML for pasting
        QTextDocument doc;
        doc.setHtml(html);
        QString plainText = doc.toPlainText().simplified();
        
        // Store both HTML and plain text
        item.type = ContentType::Html;
        item.data = html;  // Store original HTML
        // Store plain text for pasting
        item.data = plainText;
        
        // Create a preview with HTML formatting
        QTextDocument previewDoc;
        previewDoc.setHtml(html);
        QString previewText = previewDoc.toPlainText().left(100);
        if (previewText.length() == 100) {
            previewText += "...";
        }
        
        item.displayText = previewText;
        item.preview = tr("🌐 ") + previewText.left(50) + (previewText.length() > 50 ? "..." : "");
        
        // Only add if different from last item to prevent duplicates
        if (lastClipboardItem.type != ContentType::Html || 
            lastClipboardItem.data.toString() != plainText) {
            qDebug() << "Adding new HTML content to history";
            lastClipboardItem = item;
            addToHistory(item);
        } else {
            qDebug() << "Skipping duplicate HTML content";
        }
        return;
    }
    
    // Check for text content
    if (enabledContentTypes.contains(ContentType::Text) && mimeData->hasText()) {
        QString text = mimeData->text().trimmed();
        qDebug() << "Processing text content, length:" << text.length();
        
        if (text.isEmpty()) {
            qDebug() << "Text is empty, skipping";
            return;
        }
        
        // Clean up the text - remove any HTML tags
        if (text.contains(QLatin1String("<")) && text.contains(QLatin1String(">"))) {
            QTextDocument doc;
            doc.setHtml(text);
            text = doc.toPlainText().trimmed();
            qDebug() << "Cleaned HTML from text, new length:" << text.length();
        }
        
        // Skip if this is the same as the last text we processed
        if (lastClipboardItem.type == ContentType::Text && 
            lastClipboardItem.data.toString() == text) {
            qDebug() << "Skipping duplicate text content";
            return;
        }
        
        // Check if text is markdown
        if (enabledContentTypes.contains(ContentType::Markdown) && 
            (text.startsWith("#") || // Headers
             text.contains("```") || // Code blocks
             text.contains("**") ||  // Bold
             text.contains("__") ||  // Bold/underline
             text.contains("* ") ||  // List items
             text.contains("- ") ||  // List items
             (text.contains("[") && text.contains("](") && text.contains(")")))) { // Links
            
            item.type = ContentType::Markdown;
            item.data = text;
            item.displayText = text.left(100).simplified() + (text.length() > 100 ? "..." : "");
            item.preview = tr("📝 Markdown");
        } else {
            item.type = ContentType::Text;
            item.data = text;
            item.displayText = text.left(500).simplified() + (text.length() > 500 ? "..." : "");
            item.preview = text.left(100).simplified() + (text.length() > 100 ? "..." : "");
        }
        
        lastClipboardItem = item;
        addToHistory(item);
        return;
    }

    // If we get here, no supported content type was found
    item.type = ContentType::Unknown;
    item.displayText = tr("[Unsupported format]");
    item.preview = item.displayText;
}

QString ClipboardManager::formatFileList(const QList<QUrl>& urls) const {
    QStringList files;
    for (const QUrl& url : urls) {
        if (url.isLocalFile()) {
            QFileInfo info(url.toLocalFile());
            files << info.fileName();
        } else {
            files << url.toString();
        }
    }
    return files.join(", ");
}

// Core functionality from first version
void ClipboardManager::onClipboardChanged() {
    qDebug() << "Clipboard changed signal received";
    
    // Skip if we're the ones who changed the clipboard
    if (m_isSettingClipboard) {
        qDebug() << "Skipping - we set the clipboard";
        return;
    }

    // Skip if no clipboard
    if (!clipboard) {
        qWarning() << "No clipboard available";
        return;
    }
    
    // Skip if already processing
    if (m_isProcessingClipboardChange) {
        qDebug() << "Already processing clipboard change";
        return;
    }
    m_isProcessingClipboardChange = true;

    try {
        qDebug() << "Processing clipboard content...";
        processClipboardContent();
        qDebug() << "Finished processing clipboard content";
    } catch (const std::exception& e) {
        qWarning() << "Error processing clipboard content:" << e.what();
    } catch (...) {
        qWarning() << "Unknown error processing clipboard content";
    }
    
    m_isProcessingClipboardChange = false;
}

void ClipboardManager::addToHistory(const ClipboardItem& item) {
    // Create a copy of the item to modify
    ClipboardItem newItem = item;
    
    // Set timestamp if not already set
    if (!newItem.timestamp.isValid()) {
        newItem.timestamp = QDateTime::currentDateTime();
    }
    
    // Check for duplicates
    for (int i = 0; i < historyItems.size(); ++i) {
        if (historyItems[i].data == newItem.data) {
            // Remove existing item and its files
            removeHistoryFiles(i);
            historyItems.removeAt(i);
            break;
        }
    }
    
    // Add to beginning of list
    historyItems.prepend(newItem);
    
    // Limit size and remove oldest items if needed
    int maxSize = getMaxHistorySize();
    if (maxSize > 0) {  // If maxSize <= 0, unlimited history
        while (historyItems.size() > maxSize) {
            removeHistoryFiles(historyItems.size() - 1);
            historyItems.removeLast();
        }
    }
    
    // Save the updated history to disk
    saveHistory();
    
    // Update the UI
    filterHistory(searchBox->text());
}

void ClipboardManager::addToHistory(const QString& text) {
    // Create a ClipboardItem for text
    ClipboardItem item;
    item.type = ContentType::Text;
    item.data = text;
    item.timestamp = QDateTime::currentDateTime();
    item.displayText = text.left(100).simplified() + (text.length() > 100 ? "..." : "");
    item.preview = text.left(50).simplified() + (text.length() > 50 ? "..." : "");
    
    addToHistory(item);
    
    // Also maintain the simple string list for backward compatibility
    fullHistory.removeAll(text);
    fullHistory.prepend(text);
    
    // Only limit history size if maxHistorySize is greater than 0
    if (maxHistorySize > 0) {
        while (fullHistory.size() > maxHistorySize) {
            fullHistory.removeLast();
        }
    }
    
    // Save the updated history to disk
    saveHistory();
}

void ClipboardManager::onSearchTextChanged(const QString& text) {
    filterHistory(text);
}

void ClipboardManager::filterHistory(const QString& filter) {
    historyList->clear();

    for (const ClipboardItem& item : historyItems) {
        if (filter.isEmpty() || item.displayText.contains(filter, Qt::CaseInsensitive) ||
            item.preview.contains(filter, Qt::CaseInsensitive)) {
            auto* listItem = new QListWidgetItem();
            
            QString displayText = item.displayText;
            if (displayText.length() > 80) {
                displayText = displayText.left(77) + "...";
            }
            
            // Add icon based on content type
            QString iconText;
            switch (item.type) {
                case ContentType::Image: iconText = "🖼️ "; break;
                case ContentType::FileList: iconText = "📁 "; break;
                case ContentType::Html: iconText = "🌐 "; break;
                case ContentType::Markdown: iconText = "📝 "; break;
                default: iconText = "📋 ";
            }
            
            listItem->setText(iconText + displayText);
            listItem->setData(Qt::UserRole, QVariant::fromValue(item));
            listItem->setToolTip(item.preview);
            listItem->setFlags(listItem->flags() | Qt::ItemIsEditable | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            listItem->setSizeHint(QSize(listItem->sizeHint().width(), 50));
            historyList->insertItem(0, listItem);
        }
    }
    
    if (historyList->count() > 0) {
        historyList->setCurrentRow(lastRow >= 0 && lastRow < historyList->count() ? lastRow : 0);
    }
    
    onItemSelectionChanged();
    statusBar()->showMessage(QString("Showing %1 items").arg(historyList->count()));
}

void ClipboardManager::onItemSelectionChanged() {
    QListWidgetItem* current = historyList->currentItem();
    if (!current) {
        statusBar()->showMessage("No item selected");
        return;
    }

    int index = historyList->row(current);
    if (index < 0 || index >= historyItems.size()) {
        statusBar()->showMessage("Invalid item selected");
        return;
    }

    const ClipboardItem& clipboardItem = historyItems[index];
    
    // Update preview based on content type
    if (!previewPane) {
        return;
    }

    switch (clipboardItem.type) {
        case ContentType::Text:
        case ContentType::Html:
        case ContentType::Markdown: {
            QString text = clipboardItem.data.toString();
            if (text.length() > 200) {
                text = text.left(200) + "...";
            }
            previewPane->setPlainText(text);
            statusBar()->showMessage(QString("Selected: %1 characters").arg(clipboardItem.data.toString().length()));
            break;
        }
            
        case ContentType::Image: {
            QImage image = qvariant_cast<QImage>(clipboardItem.data);
            if (!image.isNull()) {
                // Create a scaled version for preview
                QPixmap pixmap = QPixmap::fromImage(image.scaled(
                    previewPane->size(), 
                    Qt::KeepAspectRatio, 
                    Qt::SmoothTransformation
                ));
                previewPane->clear();
                previewPane->document()->addResource(
                    QTextDocument::ImageResource,
                    QUrl("data:image"),
                    QVariant(pixmap)
                );
                previewPane->setHtml(
                    QString("<img src=\"data:image\" /><br>%1x%2 pixels")
                        .arg(image.width())
                        .arg(image.height())
                );
                statusBar()->showMessage(QString("Selected: Image %1x%2").arg(image.width()).arg(image.height()));
            }
            break;
        }
            
        case ContentType::FileList: {
            QList<QUrl> urls = qvariant_cast<QList<QUrl>>(clipboardItem.data);
            QStringList fileNames;
            for (const QUrl& url : urls) {
                QFileInfo fileInfo(url.toLocalFile());
                fileNames << fileInfo.fileName();
            }
            previewPane->setPlainText(fileNames.join("\n"));
            statusBar()->showMessage(QString("Selected: %1 files").arg(urls.size()));
            break;
        }
                
            default:{
                previewPane->clear();
                statusBar()->showMessage("Ready - Double-click to copy, Del to remove");
                break;
            }
        }
    }

void ClipboardManager::removeSelectedItem() {
    QListWidgetItem* item = historyList->currentItem();
    if (!item) return;

    QVariant itemData = item->data(Qt::UserRole);
    
    if (itemData.canConvert<ClipboardItem>()) {
        // Remove from enhanced history
        ClipboardItem clipboardItem = itemData.value<ClipboardItem>();
        for (int i = 0; i < historyItems.size(); ++i) {
            if (historyItems[i].data == clipboardItem.data) {
                historyItems.removeAt(i);
                break;
            }
        }
    } else {
        // Remove from simple history
        QString text = itemData.toString();
        fullHistory.removeAll(text);
    }
    
    int currentRow = historyList->row(item);
    delete historyList->takeItem(currentRow);
    
    statusBar()->showMessage("Item removed");
}

void ClipboardManager::copySelectedItem() {
    QListWidgetItem* item = historyList->currentItem();
    if (!item) return;
    
    QVariant itemData = item->data(Qt::UserRole);
    if (!itemData.canConvert<ClipboardItem>()) return;
    
    const ClipboardItem& clipboardItem = itemData.value<ClipboardItem>();
    
    if (!clipboard) {
        clipboard = QApplication::clipboard();
        if (!clipboard) return;
    }
    
    // Use the guard to prevent notification loops
    ClipboardSettingGuard guard(this);
    
    try {
        switch (clipboardItem.type) {
            case ContentType::Text:
            case ContentType::Markdown:
            case ContentType::Html:
                clipboard->setText(clipboardItem.data.toString());
                break;
                
            case ContentType::Image: {
                QImage image = qvariant_cast<QImage>(clipboardItem.data);
                if (!image.isNull()) {
                    clipboard->setImage(image);
                }
                break;
            }
                
            case ContentType::FileList: {
                QList<QUrl> urls = qvariant_cast<QList<QUrl>>(clipboardItem.data);
                if (!urls.isEmpty()) {
                    QMimeData* mimeData = new QMimeData();
                    mimeData->setUrls(urls);
                    clipboard->setMimeData(mimeData);
                }
                break;
            }
                
            default:
                break;
        }
        
        statusBar()->showMessage("Copied to clipboard", 2000);
    } catch (...) {
        qWarning() << "Error setting clipboard content";
        throw;
    }
}

void ClipboardManager::onClearAll() {
    // Remove all history files
    for (int i = 0; i < historyItems.size(); ++i) {
        removeHistoryFiles(i);
    }
    
    // Clear in-memory data
    historyItems.clear();
    fullHistory.clear();
    historyIndex = QJsonArray();
    
    // Clear UI
    historyList->clear();
    previewPane->clear();
    lastClipboard.clear();
    
    // Remove the index file
    QString indexFilePath = getHistoryBasePath() + "/index.json";
    QFile::remove(indexFilePath);
    
    qInfo() << "Cleared all clipboard history";
}

void ClipboardManager::showAndFocus() {
    if (isVisible()) {
        if (historyList) {
            lastRow = historyList->currentRow();
        }
        QMainWindow::close();
    } else {
        // multimonitor support
        QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
        if (!screen) {
            screen = QGuiApplication::primaryScreen();
        }

        if (!screen) return; // No screen available

        QPoint cursorPos = QCursor::pos();
        QRect screenGeometry = screen->availableGeometry();
        
        // Calculate position to ensure window stays on screen
        int x = cursorPos.x();
        int y = cursorPos.y();
        
        // Adjust position if window would extend beyond screen bounds
        if (x + width() > screenGeometry.right()) {
            x = screenGeometry.right() - width();
        }
        if (y + height() > screenGeometry.bottom()) {
            y = screenGeometry.bottom() - height();
        }
        
        // Ensure window doesn't go above/left of screen
        x = std::max(x, screenGeometry.left());
        y = std::max(y, screenGeometry.top());

        setGeometry(x, y, width(), height());
        show();
        raise();
        activateWindow();
        
        if (searchBox) {
            searchBox->setFocus();
            searchBox->selectAll();
        }
        if (historyList && lastRow >= 0 && lastRow < historyList->count()) {
            historyList->setCurrentRow(lastRow);
        }
    }
}

void ClipboardManager::toggleVisibility() {
    showAndFocus();
}

void ClipboardManager::pasteHistoryItem(int index) {
    if (index < 0 || index >= historyList->count()) {
        qWarning() << "Invalid history index:" << index;
        return;
    }
    
    QListWidgetItem* item = historyList->item(index);
    if (!item) {
        qWarning() << "Failed to get history item at index:" << index;
        return;
    }
    
    // Get the item data before making any changes
    QVariant itemData = item->data(Qt::UserRole);
    
    // Move the item to the top of the history first
    if (itemData.canConvert<ClipboardItem>()) {
        ClipboardItem clipboardItem = itemData.value<ClipboardItem>();
        
        // Remove and reinsert at top
        for (int i = 0; i < historyItems.size(); ++i) {
            if (historyItems[i].data == clipboardItem.data) {
                historyItems.removeAt(i);
                break;
            }
        }
        historyItems.prepend(clipboardItem);
    } else {
        // Simple text handling
        QString text = itemData.toString();
        fullHistory.removeAll(text);
        fullHistory.prepend(text);
    }
    
    // Update UI
    QListWidgetItem* newItem = historyList->takeItem(index);
    historyList->insertItem(0, newItem);
    historyList->setCurrentItem(newItem);
    
    // Use the guard to prevent notification loops
    ClipboardSettingGuard guard(this);
    
    try {
        // Now copy the item to clipboard
        if (itemData.canConvert<ClipboardItem>()) {
            const ClipboardItem& clipboardItem = itemData.value<ClipboardItem>();
            
            if (!clipboard) {
                clipboard = QApplication::clipboard();
                if (!clipboard) return;
            }
            
            switch (clipboardItem.type) {
                case ContentType::Text:
                case ContentType::Markdown:
                case ContentType::Html:
                    clipboard->setText(clipboardItem.data.toString());
                    break;
                    
                case ContentType::Image: {
                    QImage image = qvariant_cast<QImage>(clipboardItem.data);
                    if (!image.isNull()) {
                        clipboard->setImage(image);
                    }
                    break;
                }
                    
                case ContentType::FileList: {
                    QList<QUrl> urls = qvariant_cast<QList<QUrl>>(clipboardItem.data);
                    if (!urls.isEmpty()) {
                        QMimeData* mimeData = new QMimeData();
                        mimeData->setUrls(urls);
                        clipboard->setMimeData(mimeData);
                    }
                    break;
                }
                    
                default:
                    break;
            }
        } else {
            // Simple text handling
            if (clipboard) {
                clipboard->setText(itemData.toString());
            }
        }
        
        statusBar()->showMessage("Pasted to clipboard", 2000);
    } catch (...) {
        qWarning() << "Error pasting item to clipboard";
    }
    
    close();
}

void ClipboardManager::updateHistoryOrder() {
    // Update the internal history to match the current order in the list widget
    QList<ClipboardItem> newHistory;
    QStringList newFullHistory;
    
    for (int i = 0; i < historyList->count(); ++i) {
        QListWidgetItem* item = historyList->item(i);
        QVariant itemData = item->data(Qt::UserRole);
        
        if (itemData.canConvert<ClipboardItem>()) {
            newHistory.append(itemData.value<ClipboardItem>());
        } else {
            QString text = itemData.toString();
            newFullHistory.append(text);
            
            // Create a simple ClipboardItem for consistency
            ClipboardItem clipboardItem;
            clipboardItem.type = ContentType::Text;
            clipboardItem.data = text;
            clipboardItem.displayText = text.left(100).simplified() + (text.length() > 100 ? "..." : "");
            newHistory.append(clipboardItem);
        }
    }
    
    historyItems = newHistory;
    fullHistory = newFullHistory;
    
    qDebug() << "Clipboard history order updated";
}

void ClipboardManager::onHotkeyPressed() {
    toggleVisibility();
}

void ClipboardManager::onTrayIconActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger) {
        toggleVisibility();
    }
}

void ClipboardManager::showContextMenu(const QPoint& pos) {
    QListWidgetItem* item = historyList->itemAt(pos);
    QMenu menu(this);
    
    if (item) {
        menu.addAction("Edit", this, &ClipboardManager::editSelectedItem);
        menu.addAction("Copy", this, &ClipboardManager::copySelectedItem);
        menu.addAction("Remove", this, &ClipboardManager::removeSelectedItem);
        menu.addSeparator();
    }
    menu.addAction("Clear All", this, &ClipboardManager::onClearAll);
    menu.exec(historyList->mapToGlobal(pos));
}

void ClipboardManager::showTrayMessage(const QString& message) {
    if (trayIcon) {
        trayIcon->showMessage("Clipboard Manager", message, 
                             QSystemTrayIcon::Information, 2000);
    }
}

void ClipboardManager::closeEvent(QCloseEvent* event) {
    // Save any unsaved data
    saveHistory();
    
    // Accept the close event - let the window actually close
    event->accept();
}

void ClipboardManager::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        hide();
    } else {
        QMainWindow::keyPressEvent(event);
    }
}

void ClipboardManager::onItemClicked(QListWidgetItem* item) {
    if (item) {
        onItemSelectionChanged();
    }
}

void ClipboardManager::onItemDoubleClicked(QListWidgetItem* item) {
    if (!item) return;
    copySelectedItem();
    hide();
}

void ClipboardManager::editSelectedItem() {
    QListWidgetItem* item = historyList->currentItem();
    if (!item) return;
    
    int index = historyList->row(item);
    if (index < 0 || index >= historyItems.size()) return;
    
    historyList->editItem(item);
}

void ClipboardManager::onItemChanged(QListWidgetItem* item) {
    if (!item) return;
    
    int index = historyList->row(item);
    if (index < 0 || index >= historyItems.size()) return;
    
    // Update the display text of the corresponding history item
    historyItems[index].displayText = item->text();
    
    // If the item contains text content, update the data as well
    if (historyItems[index].data.canConvert<QString>()) {
        historyItems[index].data = item->text();
    }
    
    // Save the updated history
    saveHistory();
}

} // namespace havel