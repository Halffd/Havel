#include "ClipboardManager.hpp"
#include "core/ConfigManager.hpp"
#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QJsonArray>
#include <QTextStream>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QMessageBox>
#include <QHeaderView>
#include <QDateTime>
#include <QStatusBar>
#include <QDebug>
#include <QMenu>
#include <QAction>
#include <QRegularExpression>
#include <QFileInfo>
#include <QDir>
#include <QSettings>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextDocumentFragment>
#include <QScreen>
#include <QCursor>
#include <QPainter>
#include <QBuffer>

namespace havel {

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
    QString basePath = ensureDirectories();
    QString indexFilePath = basePath + "/index.json";
    
    QJsonArray indexArray;
    
    // Save each item and create index entry
    for (int i = 0; i < historyItems.size(); ++i) {
        const ClipboardItem& item = historyItems[i];
        QJsonObject indexEntry;
        
        indexEntry["index"] = i;
        indexEntry["type"] = static_cast<int>(item.type);
        indexEntry["timestamp"] = item.timestamp.toString(Qt::ISODate);
        indexEntry["displayText"] = item.displayText;
        indexEntry["preview"] = item.preview;
        
        // Save content based on type and store file path
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
        
        indexArray.append(indexEntry);
    }
    
    // Save index file
    QFile indexFile(indexFilePath);
    if (indexFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QJsonDocument doc(indexArray);
        indexFile.write(doc.toJson());
        indexFile.close();
        qDebug() << "Saved" << historyItems.size() << "items to" << basePath;
    } else {
        qWarning() << "Failed to save index file:" << indexFilePath;
    }
}

havel::ClipboardManager::ClipboardItem ClipboardManager::loadItemFromFile(const QJsonObject& json) {
    havel::ClipboardManager::ClipboardItem item;
    
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
    
    // Set up the application name and organization for settings
    QCoreApplication::setOrganizationName("Havel");
    QCoreApplication::setApplicationName("ClipboardManager");
    
    // Load settings and history
    loadSettings();
    loadHistory();
    
    // Initialize file type filters
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
    
    // Load saved settings
    loadSettings();
    
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
    close();
}

void ClipboardManager::setupUI() {
    // Set window size and properties
    resize(windowSize);
    
    // Set window background to be solid
    setAttribute(Qt::WA_TranslucentBackground, false);
    setWindowFlags(windowFlags() & ~Qt::FramelessWindowHint);
    
    // Create central widget and layout
    centralWidget = new QWidget(this);
    centralWidget->setAutoFillBackground(true);
    setCentralWidget(centralWidget);
    
    // Set window stylesheet for dark theme
    QString styleSheet = R"(
        QMainWindow, QDialog, QWidget#centralWidget {
            background-color: #1E1E1E;
            color: #E0E0E0;
            border: 1px solid #3F3F46;
            border-radius: 8px;
        }
        
        QListWidget {
            background-color: #252526;
            border: 1px solid #3F3F46;
            border-radius: 6px;
            padding: 4px;
            height: 100%;
            outline: 0;
            margin: 0;
        }
        
        QListWidget::item {
            background-color: #2D2D30;
            color: #E0E0E0;
            padding: 12px 16px;
            border-radius: 6px;
            margin: 4px 2px;
            border: 1px solid transparent;
        }
        
        QListWidget::item:selected {
            background-color: #37373D;
            color: #FFFFFF;
            border: 1px solid #505050;
        }
        
        QListWidget::item:hover {
            background-color: #3E3E42;
            border: 1px solid #5E5E60;
        }
        
        QLineEdit {
            background-color: #3C3C3C;
            color: #E0E0E0;
            border: 1px solid #3F3F46;
            border-radius: 6px;
            padding: 10px 14px;
            selection-background-color: #264F78;
            font-size: 24px;
            margin-bottom: 8px;
        }
        
        QLineEdit:focus {
            border: 1px solid  rgb(10, 79, 124);
            background-color: #3E3E42;
        }
        
        QTextEdit {
            background-color: #252526;
            color: #E0E0E0;
            border: 1px solid #3F3F46;
            border-radius: 6px;
            padding: 10px;
            selection-background-color: #264F78;
            font-size: 26px;
        }
        
        QTextEdit:focus {
            border: 1px solid #007ACC;
        }
        
        QScrollBar:vertical {
            border: none;
            background: #252526;
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
        
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }
        
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
            background: none;
        }
        
        /* Custom selection colors */
        QListWidget::item:selected:active {
            background: #264F78;
        }
        
        /* Search box placeholder text */
        QLineEdit::placeholder {
            color: #858585;
            font-style: italic;
        }
    )";
    
    setStyleSheet(styleSheet);

    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);
    mainLayout->setAlignment(Qt::AlignTop);

    // Create font with custom size
    QFont appFont = QApplication::font();
    appFont.setPointSize(fontSize);
    appFont.setStyleHint(QFont::SansSerif);
    setFont(appFont);
    
    // Set window attributes for better appearance
    setWindowFlags(Qt::WindowStaysOnTopHint | Qt::Window);
    setAttribute(Qt::WA_TranslucentBackground, false);
    
    // Set window opacity for slight transparency
    setWindowOpacity(1.0);
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(30, 30, 30));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, QColor(37, 37, 38));
    darkPalette.setColor(QPalette::AlternateBase, QColor(45, 45, 48));
    darkPalette.setColor(QPalette::ToolTipBase, QColor(255, 255, 255));
    darkPalette.setColor(QPalette::ToolTipText, QColor(255, 255, 255));
    darkPalette.setColor(QPalette::Text, QColor(224, 224, 224));
    darkPalette.setColor(QPalette::Button, QColor(45, 45, 48));
    darkPalette.setColor(QPalette::ButtonText, QColor(255, 255, 255));
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Link, QColor(0, 122, 204));
    darkPalette.setColor(QPalette::Highlight, QColor(38, 79, 120));
    darkPalette.setColor(QPalette::HighlightedText, Qt::white);
    qApp->setPalette(darkPalette);

    // Search box
    searchBox = new QLineEdit(this);
    searchBox->setPlaceholderText("Search clipboard history...");
    searchBox->setFont(appFont);
    searchBox->setClearButtonEnabled(true);
    searchBox->setStyleSheet(QStringLiteral(
        "QLineEdit { "
        "    padding: 10px 14px; "
        "    border-radius: 6px; "
        "    background: #3C3C3C; "
        "    border: 1px solid #3F3F46; "
        "    margin-bottom: 8px;"
        "}"
        "QLineEdit:focus { "
        "    border: 1px solid #007ACC; "
        "    background: #3E3E42;"
        "}"
    ));
    connect(searchBox, &QLineEdit::textChanged,
            this, &ClipboardManager::onSearchTextChanged);
    mainLayout->addWidget(searchBox);

    // Splitter for history and preview
    splitter = new QSplitter(Qt::Horizontal, this);
    
    // Create history list
    historyList = new QListWidget();
    historyList->setObjectName("historyList");
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
void ClipboardManager::processClipboardContent() {
    if (!clipboard) return;

    const QMimeData* mimeData = clipboard->mimeData();
    if (!mimeData) return;

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
        item.type = ContentType::Html;
        item.data = html;
        item.displayText = html.left(100).simplified() + (html.length() > 100 ? "..." : "");
        item.preview = tr("🌐 HTML content");
        
        lastClipboardItem = item;
        addToHistory(item);
        return;
    }
    
    // Check for text content
    if (enabledContentTypes.contains(ContentType::Text) && mimeData->hasText()) {
        QString text = mimeData->text();
        
        // Check if text is markdown
        if (enabledContentTypes.contains(ContentType::Markdown) && 
            (text.contains("```") || 
             text.contains("# ") || 
             text.contains("## ") || 
             text.contains("### ") ||
             text.contains("**") ||
             text.contains("__") ||
             text.contains("[") && text.contains("](") && text.contains(")"))) {
            
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
    if (clipboard) {
        // Use the enhanced content processing from second version
        processClipboardContent();
        
        // Also maintain backward compatibility with simple text
        QString text = clipboard->text();
        if (!text.isEmpty() && text != lastClipboard) {
            lastClipboard = text;
            addToHistory(text);
        }
    }
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
    while (historyItems.size() > MAX_HISTORY_SIZE) {
        removeHistoryFiles(historyItems.size() - 1);
        historyItems.removeLast();
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
    while (fullHistory.size() > MAX_HISTORY_SIZE) {
        fullHistory.removeLast();
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
            
            historyList->addItem(listItem);
        }
    }
    
    if (historyList->count() > 0) {
        historyList->setCurrentRow(lastRow >= 0 && lastRow < historyList->count() ? lastRow : 0);
    }
    
    onItemSelectionChanged();
    statusBar()->showMessage(QString("Showing %1 items").arg(historyList->count()));
}

void ClipboardManager::onItemDoubleClicked(QListWidgetItem* item) {
    if (!item) return;
    
    copySelectedItem();
    close();
}

void ClipboardManager::onItemSelectionChanged() {
    QListWidgetItem* item = historyList->currentItem();
    if (!item) {
        previewPane->clear();
        statusBar()->showMessage("Ready - Double-click to copy, Del to remove");
        return;
    }

    QVariant itemData = item->data(Qt::UserRole);
    if (!itemData.canConvert<ClipboardItem>()) {
        // Fallback to simple text for backward compatibility
        QString text = item->data(Qt::UserRole).toString();
        if (!text.isEmpty()) {
            QString previewText = text;
            if (previewText.length() > PREVIEW_MAX_LENGTH) {
                previewText = previewText.left(PREVIEW_MAX_LENGTH) + "\n\n[... truncated ...]";
            }
            previewPane->setPlainText(previewText);
            statusBar()->showMessage(QString("Selected item: %1 characters").arg(text.length()));
        }
        return;
    }

    ClipboardItem clipboardItem = itemData.value<ClipboardItem>();
    
    // Set a basic style for the preview
    QString style = "<style>"
                   "body { font-family: 'Segoe UI', Arial, sans-serif; line-height: 1.5; color: #e0e0e0; background-color: #252526; margin: 10px; }"
                   "pre { background-color: #1e1e1e; padding: 10px; border-radius: 4px; overflow-x: auto; }"
                   "code { font-family: 'Consolas', 'Monaco', monospace; }"
                   "a { color: #4da6ff; text-decoration: none; }"
                   "a:hover { text-decoration: underline; }"
                   "h1, h2, h3 { color: #9cdcfe; margin: 10px 0; }"
                   "img { max-width: 100%; height: auto; display: block; margin: 10px 0; }"
                   "</style>";
    
    switch (clipboardItem.type) {
        case ContentType::Text:
            previewPane->setPlainText(clipboardItem.data.toString());
            statusBar()->showMessage(QString("Text: %1 characters").arg(clipboardItem.data.toString().length()));
            break;
            
        case ContentType::Markdown: {
            QString html = markdownToHtml(clipboardItem.data.toString());
            previewPane->setHtml(style + html);
            statusBar()->showMessage(QString("Markdown: %1 characters").arg(clipboardItem.data.toString().length()));
            break;
        }
            
        case ContentType::Html: {
            QString html = clipboardItem.data.toString();
            if (!html.contains("<style>")) {
                html = style + html;
            }
            previewPane->setHtml(html);
            statusBar()->showMessage(QString("HTML: %1 characters").arg(html.length()));
            break;
        }
            
        case ContentType::Image: {
            QImage image = qvariant_cast<QImage>(clipboardItem.data);
            QPixmap pixmap = QPixmap::fromImage(image);
            
            // Scale image to fit preview while maintaining aspect ratio
            QSize previewSize = previewPane->viewport()->size();
            pixmap = pixmap.scaled(previewSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            
            QString html = QString("<div style='text-align: center;'>"
                                 "<p>Image: %1x%2 pixels</p>"
                                 "<img src='data:image/png;base64,%3'/>"
                                 "</div>")
                         .arg(image.width())
                         .arg(image.height())
                         .arg([&pixmap]() -> QString {
                             QByteArray byteArray;
                             QBuffer buffer(&byteArray);
                             pixmap.save(&buffer, "PNG");
                             return QString(byteArray.toBase64());
                         }());
            
            previewPane->setHtml(style + html);
            statusBar()->showMessage(QString("Image: %1x%2 pixels").arg(image.width()).arg(image.height()));
            break;
        }
            
        case ContentType::FileList: {
            QList<QUrl> urls = qvariant_cast<QList<QUrl>>(clipboardItem.data);
            QStringList fileList;
            
            fileList << QString("<h3>%1 files:</h3><ul>").arg(urls.size());
            
            for (const QUrl& url : urls) {
                if (url.isLocalFile()) {
                    QFileInfo info(url.toLocalFile());
                    fileList << QString("<li><b>%1</b> (%2, %3)")
                        .arg(info.fileName())
                        .arg(QString::fromLatin1("%1 MB").arg(info.size() / (1024.0 * 1024.0), 0, 'f', 2))
                        .arg(info.lastModified().toString("yyyy-MM-dd hh:mm"));
                } else {
                    fileList << QString("<li><a href='%1'>%1</a>").arg(url.toString());
                }
            }
            
            fileList << "</ul>";
            previewPane->setHtml(style + fileList.join(""));
            statusBar()->showMessage(QString("File list: %1 files").arg(urls.size()));
            break;
        }
            
        default:
            previewPane->setPlainText(tr("Preview not available for this content type."));
            statusBar()->showMessage("Unsupported content type");
            break;
    }
}

void ClipboardManager::copySelectedItem() {
    QListWidgetItem* item = historyList->currentItem();
    if (!item || !clipboard) return;
    
    QVariant itemData = item->data(Qt::UserRole);
    
    if (itemData.canConvert<ClipboardItem>()) {
        // Enhanced content type handling
        ClipboardItem clipboardItem = itemData.value<ClipboardItem>();
        QMimeData* mimeData = new QMimeData();
        
        switch (clipboardItem.type) {
            case ContentType::Text:
                mimeData->setText(clipboardItem.data.toString());
                break;
                
            case ContentType::Html:
                mimeData->setHtml(clipboardItem.data.toString());
                mimeData->setText(clipboardItem.data.toString());
                break;
                
            case ContentType::Image: {
                QImage image = qvariant_cast<QImage>(clipboardItem.data);
                mimeData->setImageData(image);
                break;
            }
                
            case ContentType::FileList: {
                QList<QUrl> urls = qvariant_cast<QList<QUrl>>(clipboardItem.data);
                mimeData->setUrls(urls);
                break;
            }
                
            default:
                // Fallback to text
                mimeData->setText(clipboardItem.data.toString());
                break;
        }
        
        clipboard->setMimeData(mimeData);
    } else {
        // Fallback to simple text
        QString text = itemData.toString();
        clipboard->setText(text);
    }
    
    showTrayMessage("Copied to clipboard!");
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
    if (shown || isVisible()) {
        if (historyList) {
            lastRow = historyList->currentRow();
        }
        close();
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
    shown = !shown;
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
    
    // Copy the item
    historyList->setCurrentItem(item);
    copySelectedItem();
    
    // Move the item to the top of the history
    QVariant itemData = item->data(Qt::UserRole);
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
    
    // Hide the window after pasting
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

void ClipboardManager::showTrayMessage(const QString& message) {
    if (trayIcon) {
        trayIcon->showMessage("Clipboard Manager", message, 
                             QSystemTrayIcon::Information, 2000);
    }
}

void ClipboardManager::closeEvent(QCloseEvent* event) {
    close();
    event->ignore();
}

void ClipboardManager::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        close();
    } else {
        QMainWindow::keyPressEvent(event);
    }
}

void ClipboardManager::onItemClicked(QListWidgetItem* item) {
    if (item) {
        onItemSelectionChanged();
    }
}

} // namespace havel