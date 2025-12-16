#include "MapManagerWindow.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QListWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QMessageBox>

namespace havel {

MappingEditorDialog::MappingEditorDialog(Mapping* mapping, IO* io, QWidget* parent)
    : QDialog(parent), io(io) {
    setupUI();
    
    if (mapping) {
        editedMapping = *mapping;
    }
}

void MappingEditorDialog::setupUI() {
    setWindowTitle("Edit Mapping");
    resize(600, 400);

    auto* mainLayout = new QVBoxLayout(this);

    auto* formLayout = new QFormLayout();
    
    editName = new QLineEdit();
    formLayout->addRow("Name:", editName);
    
    editSourceKey = new QLineEdit();
    btnCaptureSource = new QPushButton("Capture");
    auto* sourceLayout = new QHBoxLayout();
    sourceLayout->addWidget(editSourceKey);
    sourceLayout->addWidget(btnCaptureSource);
    formLayout->addRow("Source Key:", sourceLayout);

    targetList = new QListWidget();
    auto* targetGroupBox = new QGroupBox("Target Keys");
    auto* targetGroupLayout = new QVBoxLayout();
    targetGroupLayout->addWidget(targetList);
    targetGroupBox->setLayout(targetGroupLayout);
    
    editNewTarget = new QLineEdit();
    btnCaptureTarget = new QPushButton("Capture");
    btnAddTarget = new QPushButton("Add");
    btnRemoveTarget = new QPushButton("Remove");
    auto* newTargetLayout = new QHBoxLayout();
    newTargetLayout->addWidget(editNewTarget);
    newTargetLayout->addWidget(btnCaptureTarget);
    newTargetLayout->addWidget(btnAddTarget);
    newTargetLayout->addWidget(btnRemoveTarget);
    targetGroupLayout->addLayout(newTargetLayout);

    cmbActionType = new QComboBox();
    cmbActionType->addItems({"Send Keys", "Execute Command", "Toggle Mapping", "Run Script"});
    formLayout->addRow("Action Type:", cmbActionType);

    chkAutofire = new QCheckBox();
    spinInterval = new QSpinBox();
    spinInterval->setRange(10, 1000);
    spinInterval->setValue(100);
    auto* autofireLayout = new QHBoxLayout();
    autofireLayout->addWidget(chkAutofire);
    autofireLayout->addWidget(new QLabel("Interval (ms):"));
    autofireLayout->addWidget(spinInterval);
    formLayout->addRow("Autofire:", autofireLayout);

    mainLayout->addLayout(formLayout);
    
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    mainLayout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &MappingEditorDialog::onAccept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &MappingEditorDialog::onReject);
    connect(btnCaptureSource, &QPushButton::clicked, this, &MappingEditorDialog::onCaptureSource);
    connect(btnCaptureTarget, &QPushButton::clicked, this, &MappingEditorDialog::onCaptureTarget);
    connect(btnAddTarget, &QPushButton::clicked, this, &MappingEditorDialog::onAddTarget);
    connect(btnRemoveTarget, &QPushButton::clicked, this, &MappingEditorDialog::onRemoveTarget);
}

void MappingEditorDialog::onAccept() {}
void MappingEditorDialog::onReject() {}
void MappingEditorDialog::onCaptureSource() {}
void MappingEditorDialog::onCaptureTarget() {}
void MappingEditorDialog::onAddTarget() {}
void MappingEditorDialog::onRemoveTarget() {}

std::string MappingEditorDialog::captureKeyPress() {
    return ""; // Placeholder
}

} // namespace havel