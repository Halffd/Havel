#include "MapManagerWindow.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QGroupBox>

namespace havel {

ConditionEditorDialog::ConditionEditorDialog(MappingCondition* condition, QWidget* parent)
    : QDialog(parent) {
    setupUI();
    
    if (condition) {
        editedCondition = *condition;
    }
}

void ConditionEditorDialog::setupUI() {
    setWindowTitle("Edit Condition");
    resize(500, 300);

    auto* mainLayout = new QVBoxLayout(this);

    auto* formLayout = new QFormLayout();
    
    cmbType = new QComboBox();
    cmbType->addItems({"Window Title", "Window Class", "Process Name", "Process PID", "Active Application"});
    formLayout->addRow("Condition Type:", cmbType);

    editPattern = new QLineEdit();
    lblPatternHelp = new QLabel("Enter pattern to match (supports regex)");
    lblPatternHelp->setWordWrap(true);
    auto* patternWidget = new QWidget();
    auto* patternLayout = new QVBoxLayout();
    patternLayout->addWidget(editPattern);
    patternLayout->addWidget(lblPatternHelp);
    patternWidget->setLayout(patternLayout);
    formLayout->addRow("Pattern:", patternWidget);

    mainLayout->addLayout(formLayout);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    mainLayout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &ConditionEditorDialog::onAccept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &ConditionEditorDialog::onReject);
    connect(cmbType, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            this, &ConditionEditorDialog::onConditionTypeChanged);
}

void ConditionEditorDialog::onAccept() {}
void ConditionEditorDialog::onReject() {}
void ConditionEditorDialog::onConditionTypeChanged(int) {}

} // namespace havel