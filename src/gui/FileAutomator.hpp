#pragma once

#include <QFileSystemModel>
#include <QSplitter>
#include <QListView>
#include <QTreeView>
#include <QDir>
#include "qt.hpp"
#include "types.hpp"

class FileAutomator : public havel::QWindow {
    Q_OBJECT

public:
    FileAutomator(QWidget* parent = nullptr);

private slots:
    void onSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected);

private:
    void setupUI();

    QFileSystemModel* model;
    havel::TreeView* tree;
    havel::ListView* list;
};
