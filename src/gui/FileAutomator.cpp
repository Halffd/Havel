#include "FileAutomator.hpp"
#include <QMainWindow>

FileAutomator::FileAutomator(QWidget* parent) : QMainWindow(parent) {
    setupUI();
}

void FileAutomator::setupUI() {
    setWindowTitle("File Automator");
    resize(800, 600);

    QSplitter* splitter = new QSplitter(this);
    setCentralWidget(splitter);

    model = new QFileSystemModel(this);
    model->setRootPath(QDir::homePath());
    model->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);

    tree = new QTreeView(splitter);
    tree->setModel(model);
    tree->setRootIndex(model->index(QDir::homePath()));

    list = new QListView(splitter);
    list->setModel(model);
    list->setViewMode(QListView::IconMode);
    list->setGridSize(QSize(100, 100));
    list->setIconSize(QSize(96, 96));
    list->setResizeMode(QListView::Adjust);
    list->setUniformItemSizes(true);

    splitter->addWidget(tree);
    splitter->addWidget(list);
    splitter->setSizes({200, 600});

    connect(tree->selectionModel(), &QItemSelectionModel::selectionChanged, this, &FileAutomator::onSelectionChanged);
}

void FileAutomator::onSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected) {
    Q_UNUSED(deselected);
    const QModelIndex index = selected.indexes().first();
    if (model->isDir(index)) {
        list->setRootIndex(index);
    }
}
