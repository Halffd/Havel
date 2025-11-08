#pragma once

#include "qt.hpp" // Moved to top
#include <QFileSystemModel>
#include <QSplitter>
#include <QListView>
#include <QTreeView>
#include <QDir>
#include <QMainWindow> // Added for QMainWindow definition
#include "types.hpp"

class FileAutomator : public ::QMainWindow {
    Q_OBJECT

public:
    FileAutomator(QWidget* parent = nullptr);

private slots:
    void onSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected);

private:
    void setupUI();

    QFileSystemModel* model;
    QTreeView* tree;
    QListView* list;
};
