#include "MapManagerWindow.hpp"
#include "../core/IO.hpp"
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QHeaderView>
#include <spdlog/spdlog.h>

using namespace spdlog;

namespace havel {

MapManagerWindow::MapManagerWindow(MapManager* mapManager, IO* io, QWidget* parent)
    : QWidget(parent), mapManager(mapManager), io(io) {
    setupUI();
    refreshProfileList();
}

MapManagerWindow::~MapManagerWindow() = default;

void MapManagerWindow::setupUI() {
    setWindowTitle("Map Manager - Input Mapping Configuration");
    resize(1200, 800);
    
    auto* mainLayout = new QVBoxLayout(this);
    mainSplitter = new QSplitter(Qt::Horizontal);
    
    setupProfilePanel();
    setupMappingPanel();
    setupEditorPanel();
    setupToolbar();
    setupStatusBar();
    
    mainLayout->addWidget(mainSplitter);
}

void MapManagerWindow::refreshProfileList() {
    profileList->clear();
    auto ids = mapManager->GetProfileIds();
    for (const auto& id : ids) {
        auto* profile = mapManager->GetProfile(id);
        if (profile) {
            auto* item = new QListWidgetItem(QString::fromStdString(profile->name));
            item->setData(Qt::UserRole, QString::fromStdString(id));
            profileList->addItem(item);
        }
    }
}

void MapManagerWindow::refreshMappingTable() {
    mappingTable->setRowCount(0);
    if (currentProfileId.empty()) return;
    
    auto mappings = mapManager->GetMappings(currentProfileId);
    mappingTable->setRowCount(mappings.size());
    
    for (size_t i = 0; i < mappings.size(); ++i) {
        auto* mapping = mappings[i];
        mappingTable->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(mapping->name)));
        mappingTable->item(i, 0)->setData(Qt::UserRole, QString::fromStdString(mapping->id));
        mappingTable->setItem(i, 1, new QTableWidgetItem(QString::fromStdString(mapping->sourceKey)));
    }
}

// Slot implementations
void MapManagerWindow::onNewProfile() {
    bool ok;
    QString name = QInputDialog::getText(this, "New Profile", "Profile name:", 
                                         QLineEdit::Normal, "New Profile", &ok);
    if (ok && !name.isEmpty()) {
        Profile profile;
        profile.name = name.toStdString();
        mapManager->AddProfile(profile);
        refreshProfileList();
    }
}

void MapManagerWindow::onActivateProfile() {
    if (!currentProfileId.empty()) {
        mapManager->SetActiveProfile(currentProfileId);
    }
}

// Additional implementations...

} // namespace havel
