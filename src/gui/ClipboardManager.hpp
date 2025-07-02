#pragma once

#include "types.hpp"
#include <QClipboard>
#include <QSystemTrayIcon>

namespace havel {

class ClipboardManager : public Window {
    Q_OBJECT

public:
    explicit ClipboardManager(QWidget *parent = nullptr);

private slots:
    void onClipboardChanged();
    void onItemDoubleClicked(QListWidgetItem* item);

private:
    void setupUI();
    void addToHistory(const QString& text);
    void showTrayMessage(const QString& message);

    ListView* historyList;
    TextEdit* previewPane;
    LineEdit* searchBox;
    QClipboard* clipboard;
    QSystemTrayIcon* trayIcon;
    QString lastClipboard;
};

} // namespace havel
