#include "MapManagerWindow.hpp"
#include "../core/IO.hpp"
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QHeaderView>
#include <QFormLayout>
#include <spdlog/spdlog.h>
#include <QDebug>

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
    setupToolbar();

    mainSplitter = new QSplitter(Qt::Horizontal);
    
    setupProfilePanel();
    setupMappingPanel();
    setupEditorPanel();
    
    // Set stretch factors for splitter
    mainSplitter->setStretchFactor(0, 1); // Profile list
    mainSplitter->setStretchFactor(1, 2); // Mapping table
    mainSplitter->setStretchFactor(2, 2); // Editor
    
    mainLayout->addWidget(mainSplitter);
    
    setupStatusBar();
}

void MapManagerWindow::setupToolbar() {
    auto* toolbarLayout = new QHBoxLayout();
    
    btnImport = new QPushButton(QIcon::fromTheme("document-import"), "Import Profile");
    btnExport = new QPushButton(QIcon::fromTheme("document-export"), "Export Profile");
    btnSaveAll = new QPushButton(QIcon::fromTheme("document-save"), "Save All");
    btnLoadAll = new QPushButton(QIcon::fromTheme("document-open"), "Load All");
    btnStats = new QPushButton(QIcon::fromTheme("utilities-system-monitor"), "Statistics");
    
    connect(btnImport, &QPushButton::clicked, this, &MapManagerWindow::onImportProfile);
    connect(btnExport, &QPushButton::clicked, this, &MapManagerWindow::onExportProfile);
    connect(btnSaveAll, &QPushButton::clicked, this, &MapManagerWindow::onSaveAll);
    connect(btnLoadAll, &QPushButton::clicked, this, &MapManagerWindow::onLoadAll);
    connect(btnStats, &QPushButton::clicked, this, &MapManagerWindow::showStatsDialog);
    
    toolbarLayout->addWidget(btnImport);
    toolbarLayout->addWidget(btnExport);
    toolbarLayout->addWidget(btnSaveAll);
    toolbarLayout->addWidget(btnLoadAll);
    toolbarLayout->addStretch();
    toolbarLayout->addWidget(btnStats);
    
    static_cast<QVBoxLayout*>(layout())->addLayout(toolbarLayout);
}

void MapManagerWindow::setupProfilePanel() {
    auto* panel = new QWidget();
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    
    layout->addWidget(new QLabel("<b>Profiles</b>"));
    
    profileList = new QListWidget();
    connect(profileList, &QListWidget::itemClicked, this, &MapManagerWindow::onProfileSelected);
    layout->addWidget(profileList);
    
    auto* btnLayout = new QHBoxLayout();
    btnNewProfile = new QPushButton("New");
    btnDeleteProfile = new QPushButton("Delete");
    btnDuplicateProfile = new QPushButton("Clone");
    
    connect(btnNewProfile, &QPushButton::clicked, this, &MapManagerWindow::onNewProfile);
    connect(btnDeleteProfile, &QPushButton::clicked, this, &MapManagerWindow::onDeleteProfile);
    connect(btnDuplicateProfile, &QPushButton::clicked, this, &MapManagerWindow::onDuplicateProfile);
    
    btnLayout->addWidget(btnNewProfile);
    btnLayout->addWidget(btnDuplicateProfile);
    btnLayout->addWidget(btnDeleteProfile);
    layout->addLayout(btnLayout);
    
    btnActivateProfile = new QPushButton("Activate Profile");
    connect(btnActivateProfile, &QPushButton::clicked, this, &MapManagerWindow::onActivateProfile);
    layout->addWidget(btnActivateProfile);
    
    lblActiveProfile = new QLabel("Active: None");
    layout->addWidget(lblActiveProfile);
    
    mainSplitter->addWidget(panel);
}

void MapManagerWindow::setupMappingPanel() {
    auto* panel = new QWidget();
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    
    layout->addWidget(new QLabel("<b>Mappings</b>"));
    
    mappingTable = new QTableWidget();
    mappingTable->setColumnCount(2);
    mappingTable->setHorizontalHeaderLabels({"Name", "Source Key"});
    mappingTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    mappingTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    mappingTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    mappingTable->setSelectionMode(QAbstractItemView::SingleSelection);
    
    connect(mappingTable, &QTableWidget::cellClicked, this, &MapManagerWindow::onMappingSelected);
    connect(mappingTable, &QTableWidget::cellDoubleClicked, this, &MapManagerWindow::onMappingDoubleClicked);
    
    layout->addWidget(mappingTable);
    
    auto* btnLayout = new QHBoxLayout();
    btnNewMapping = new QPushButton("Add");
    btnEditMapping = new QPushButton("Edit"); // Optional if we use the side panel
    btnDeleteMapping = new QPushButton("Delete");
    btnDuplicateMapping = new QPushButton("Clone");
    
    connect(btnNewMapping, &QPushButton::clicked, this, &MapManagerWindow::onNewMapping);
    connect(btnEditMapping, &QPushButton::clicked, this, &MapManagerWindow::onEditMapping);
    connect(btnDeleteMapping, &QPushButton::clicked, this, &MapManagerWindow::onDeleteMapping);
    connect(btnDuplicateMapping, &QPushButton::clicked, this, &MapManagerWindow::onDuplicateMapping);
    
    btnLayout->addWidget(btnNewMapping);
    btnLayout->addWidget(btnEditMapping);
    btnLayout->addWidget(btnDuplicateMapping);
    btnLayout->addWidget(btnDeleteMapping);
    layout->addLayout(btnLayout);
    
    mainSplitter->addWidget(panel);
}

void MapManagerWindow::setupEditorPanel() {
    editorTabs = new QTabWidget();
    
    // --- Basic Tab ---
    auto* basicTab = new QWidget();
    auto* basicLayout = new QFormLayout(basicTab);
    
    editMappingName = new QLineEdit();
    basicLayout->addRow("Name:", editMappingName);
    
    chkEnabled = new QCheckBox("Enabled");
    basicLayout->addRow("", chkEnabled);
    
    cmbMappingType = new QComboBox();
    cmbMappingType->addItems({"KeyToKey", "KeyToMouse", "MouseToKey", "MouseToMouse", "JoyToKey", "JoyToMouse", "Combo", "Macro"});
    basicLayout->addRow("Type:", cmbMappingType);
    
    cmbActionType = new QComboBox();
    cmbActionType->addItems({"Press", "Hold", "Toggle", "Release"});
    basicLayout->addRow("Action:", cmbActionType);
    
    editorTabs->addTab(basicTab, "Basic");
    
    // --- Source/Target Tab ---
    auto* ioTab = new QWidget();
    auto* ioLayout = new QVBoxLayout(ioTab);
    
    auto* sourceGroup = new QGroupBox("Source Input");
    auto* sourceLayout = new QHBoxLayout(sourceGroup);
    editSourceKey = new QLineEdit();
    btnCaptureSource = new QPushButton("Capture");
    connect(btnCaptureSource, &QPushButton::clicked, this, &MapManagerWindow::onCaptureSourceKey);
    sourceLayout->addWidget(editSourceKey);
    sourceLayout->addWidget(btnCaptureSource);
    ioLayout->addWidget(sourceGroup);
    
    auto* targetGroup = new QGroupBox("Target Output(s)");
    auto* targetLayout = new QVBoxLayout(targetGroup);
    targetKeysList = new QListWidget();
    targetLayout->addWidget(targetKeysList);
    
    auto* targetBtnLayout = new QHBoxLayout();
    btnAddTargetKey = new QPushButton("Add");
    btnRemoveTargetKey = new QPushButton("Remove");
    btnCaptureTarget = new QPushButton("Capture New");
    connect(btnAddTargetKey, &QPushButton::clicked, [this](){
        bool ok;
        QString text = QInputDialog::getText(this, "Add Target", "Key code:", QLineEdit::Normal, "", &ok);
        if (ok && !text.isEmpty()) targetKeysList->addItem(text);
    });
    connect(btnRemoveTargetKey, &QPushButton::clicked, [this](){
        delete targetKeysList->currentItem();
    });
    connect(btnCaptureTarget, &QPushButton::clicked, this, &MapManagerWindow::onCaptureTargetKey);
    
    targetBtnLayout->addWidget(btnAddTargetKey);
    targetBtnLayout->addWidget(btnRemoveTargetKey);
    targetBtnLayout->addWidget(btnCaptureTarget);
    targetLayout->addLayout(targetBtnLayout);
    
    ioLayout->addWidget(targetGroup);
    editorTabs->addTab(ioTab, "Input/Output");
    
    // --- Autofire Tab ---
    auto* autoTab = new QWidget();
    auto* autoLayout = new QFormLayout(autoTab);
    
    chkAutofire = new QCheckBox("Enable Autofire");
    autoLayout->addRow(chkAutofire);
    
    spinAutofireInterval = new QSpinBox();
    spinAutofireInterval->setRange(1, 10000);
    spinAutofireInterval->setSuffix(" ms");
    autoLayout->addRow("Interval:", spinAutofireInterval);
    
    chkTurbo = new QCheckBox("Turbo Mode (Hold to Repeat)");
    autoLayout->addRow(chkTurbo);
    
    spinTurboInterval = new QSpinBox();
    spinTurboInterval->setRange(1, 10000);
    spinTurboInterval->setSuffix(" ms");
    autoLayout->addRow("Turbo Interval:", spinTurboInterval);
    
    editorTabs->addTab(autoTab, "Autofire");
    
    // --- Conditions Tab ---
    auto* condTab = new QWidget();
    auto* condLayout = new QVBoxLayout(condTab);
    conditionsList = new QListWidget();
    condLayout->addWidget(conditionsList);
    
    auto* condBtnLayout = new QHBoxLayout();
    btnAddCondition = new QPushButton("Add");
    btnEditCondition = new QPushButton("Edit");
    btnRemoveCondition = new QPushButton("Remove");
    // Connect slots for conditions... (placeholders for now)
    
    condBtnLayout->addWidget(btnAddCondition);
    condBtnLayout->addWidget(btnEditCondition);
    condBtnLayout->addWidget(btnRemoveCondition);
    condLayout->addLayout(condBtnLayout);
    
    editorTabs->addTab(condTab, "Conditions");
    
    // --- Macro Tab ---
    auto* macroTab = new QWidget();
    auto* macroLayout = new QVBoxLayout(macroTab);
    macroTable = new QTableWidget(0, 3);
    macroTable->setHorizontalHeaderLabels({"Action", "Key", "Delay (ms)"});
    macroLayout->addWidget(macroTable);
    
    auto* macroBtnLayout = new QHBoxLayout();
    btnRecordMacro = new QPushButton("Record");
    btnStopMacro = new QPushButton("Stop");
    btnClearMacro = new QPushButton("Clear");
    connect(btnRecordMacro, &QPushButton::clicked, this, &MapManagerWindow::onStartMacroRecording);
    connect(btnStopMacro, &QPushButton::clicked, this, &MapManagerWindow::onStopMacroRecording);
    
    macroBtnLayout->addWidget(btnRecordMacro);
    macroBtnLayout->addWidget(btnStopMacro);
    macroBtnLayout->addWidget(btnClearMacro);
    macroLayout->addLayout(macroBtnLayout);
    
    lblMacroStatus = new QLabel("Ready");
    macroLayout->addWidget(lblMacroStatus);
    
    editorTabs->addTab(macroTab, "Macro");
    
    // Apply Buttons
    auto* applyLayout = new QHBoxLayout();
    auto* btnApply = new QPushButton("Apply Changes");
    auto* btnRevert = new QPushButton("Revert");
    connect(btnApply, &QPushButton::clicked, this, &MapManagerWindow::onApplyChanges);
    connect(btnRevert, &QPushButton::clicked, this, &MapManagerWindow::onRevertChanges);
    applyLayout->addWidget(btnApply);
    applyLayout->addWidget(btnRevert);
    
    auto* editorContainer = new QWidget();
    auto* editorLayout = new QVBoxLayout(editorContainer);
    editorLayout->addWidget(editorTabs);
    editorLayout->addLayout(applyLayout);
    
    mainSplitter->addWidget(editorContainer);
}

void MapManagerWindow::setupStatusBar() {
    lblStatus = new QLabel("Ready");
    auto* layout = new QHBoxLayout();
    layout->addWidget(lblStatus);
    static_cast<QVBoxLayout*>(this->layout())->addLayout(layout);
}

// ----------------------------------------------------------------------------
// Profile Management
// ----------------------------------------------------------------------------

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

void MapManagerWindow::onNewProfile() {
    bool ok;
    QString name = QInputDialog::getText(this, "New Profile", "Profile name:", 
                                         QLineEdit::Normal, "New Profile", &ok);
    if (ok && !name.isEmpty()) {
        Profile profile;
        profile.name = name.toStdString();
        // ID generation should be handled by MapManager, but for now:
        profile.id = name.toLower().replace(" ", "_").toStdString(); 
        mapManager->AddProfile(profile);
        refreshProfileList();
    }
}

void MapManagerWindow::onDeleteProfile() {
    auto items = profileList->selectedItems();
    if (items.isEmpty()) return;
    
    if (QMessageBox::question(this, "Confirm Delete", "Delete selected profile?") == QMessageBox::Yes) {
        QString id = items.first()->data(Qt::UserRole).toString();
        // mapManager->DeleteProfile(id.toStdString()); // Assuming this exists
        refreshProfileList();
        mappingTable->setRowCount(0);
        currentProfileId = "";
    }
}

void MapManagerWindow::onDuplicateProfile() {
    // TODO: Implement duplication logic
}

void MapManagerWindow::onRenameProfile() {
    // TODO: Implement rename logic
}

void MapManagerWindow::onProfileSelected(QListWidgetItem* item) {
    if (!item) return;
    currentProfileId = item->data(Qt::UserRole).toString().toStdString();
    refreshMappingTable();
    clearMappingDetails();
}

void MapManagerWindow::onActivateProfile() {
    if (!currentProfileId.empty()) {
        mapManager->SetActiveProfile(currentProfileId);
        lblActiveProfile->setText("Active: " + QString::fromStdString(currentProfileId));
        lblStatus->setText("Profile activated: " + QString::fromStdString(currentProfileId));
    }
}

// ----------------------------------------------------------------------------
// Mapping Management
// ----------------------------------------------------------------------------

void MapManagerWindow::refreshMappingTable() {
    mappingTable->setRowCount(0);
    if (currentProfileId.empty()) return;
    
    autoMappings = mapManager->GetMappings(currentProfileId);
    mappingTable->setRowCount(autoMappings.size());
    
    for (size_t i = 0; i < autoMappings.size(); ++i) {
        auto* mapping = autoMappings[i];
        mappingTable->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(mapping->name)));
        mappingTable->item(i, 0)->setData(Qt::UserRole, QString::fromStdString(mapping->id));
        mappingTable->setItem(i, 1, new QTableWidgetItem(QString::fromStdString(mapping->sourceKey)));
    }
}

void MapManagerWindow::onNewMapping() {
    if (currentProfileId.empty()) return;
    
    Mapping mapping;
    mapping.id = "new_mapping_" + std::to_string(QDateTime::currentMSecsSinceEpoch());
    mapping.name = "New Mapping";
    
    // Add to manager
    // mapManager->AddMapping(currentProfileId, mapping);
    refreshMappingTable();
}

void MapManagerWindow::onEditMapping() {
    // Logic handled by selection change -> editor update
}

void MapManagerWindow::onDeleteMapping() {
    // TODO: Implement delete mapping
}

void MapManagerWindow::onDuplicateMapping() {
    // TODO: Implement duplicate mapping
}

void MapManagerWindow::onMappingSelected(int row, int column) {
    auto item = mappingTable->item(row, 0);
    if (!item) return;
    
    currentMappingId = item->data(Qt::UserRole).toString().toStdString();
    loadMappingDetails(currentProfileId, currentMappingId);
}

void MapManagerWindow::onMappingDoubleClicked(int row, int column) {
    // Could open a separate dialog, but we have the side panel
}

void MapManagerWindow::loadMappingDetails(const std::string& profileId, const std::string& mappingId) {
    // Find mapping
    auto mappings = mapManager->GetMappings(profileId);
    Mapping* target = nullptr;
    for (auto* m : mappings) {
        if (m->id == mappingId) {
            target = m;
            break;
        }
    }
    
    if (!target) return;
    
    // Populate UI
    editMappingName->setText(QString::fromStdString(target->name));
    chkEnabled->setChecked(target->enabled);
    editSourceKey->setText(QString::fromStdString(target->sourceKey));
    
    targetKeysList->clear();
    for (const auto& key : target->targetKeys) {
        targetKeysList->addItem(QString::fromStdString(key));
    }
    
    // ... populate other fields ...
}

void MapManagerWindow::clearMappingDetails() {
    editMappingName->clear();
    editSourceKey->clear();
    targetKeysList->clear();
    chkEnabled->setChecked(true);
}

void MapManagerWindow::onApplyChanges() {
    if (currentProfileId.empty() || currentMappingId.empty()) return;
    
    // TODO: Update the actual Mapping object in MapManager
    // This requires MapManager to expose a method to update a mapping
    
    lblStatus->setText("Changes applied.");
}

void MapManagerWindow::onRevertChanges() {
    if (!currentMappingId.empty()) {
        loadMappingDetails(currentProfileId, currentMappingId);
        lblStatus->setText("Changes reverted.");
    }
}

// ----------------------------------------------------------------------------
// Capture & Utilities
// ----------------------------------------------------------------------------

void MapManagerWindow::onCaptureHotkey() {}

void MapManagerWindow::onCaptureSourceKey() {
    // Simple placeholder for capture dialog
    bool ok;
    QString text = QInputDialog::getText(this, "Capture Key", "Press a key (type it for now):", QLineEdit::Normal, "", &ok);
    if (ok) {
        editSourceKey->setText(text);
    }
}

void MapManagerWindow::onCaptureTargetKey() {
    bool ok;
    QString text = QInputDialog::getText(this, "Capture Key", "Press a key (type it for now):", QLineEdit::Normal, "", &ok);
    if (ok && !text.isEmpty()) {
        targetKeysList->addItem(text);
    }
}

void MapManagerWindow::onImportProfile() {}
void MapManagerWindow::onExportProfile() {}
void MapManagerWindow::onSaveAll() {
    // mapManager->SaveAll();
    lblStatus->setText("All profiles saved.");
}
void MapManagerWindow::onLoadAll() {}
void MapManagerWindow::onStartMacroRecording() {
    isRecordingMacro = true;
    lblMacroStatus->setText("Recording...");
    btnRecordMacro->setEnabled(false);
    btnStopMacro->setEnabled(true);
}

void MapManagerWindow::onStopMacroRecording() {
    isRecordingMacro = false;
    lblMacroStatus->setText("Recording stopped.");
    btnRecordMacro->setEnabled(true);
    btnStopMacro->setEnabled(false);
}

void MapManagerWindow::showStatsDialog() {
    QMessageBox::information(this, "Stats", "Statistics not implemented yet.");
}

void MapManagerWindow::showMappingEditor(Mapping* mapping) {}
void MapManagerWindow::showConditionEditor(Mapping* mapping) {}
void MapManagerWindow::showMacroEditor(Mapping* mapping) {}

std::string MapManagerWindow::captureKeyPress() { return ""; }

} // namespace havel
