#pragma once

#include "types.hpp"
#include <QFileSystemModel>

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
