#pragma once

#include <QWidget>
#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QTableWidget>
#include <QLabel>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTabWidget>
#include <memory>
#include "../core/io/MapManager.hpp"

namespace havel {

class IO;
class MapManager;

/**
 * MapManagerWindow - GUI for managing input mappings and profiles
 * 
 * Features:
 * - Profile management (create, edit, delete, switch)
 * - Mapping editor (add, edit, delete mappings)
 * - Hotkey rebinding with live capture
 * - Autofire configuration
 * - Macro recording and editing
 * - Condition editor
 * - Profile import/export
 * - Statistics view
 */
class MapManagerWindow : public QWidget {
    Q_OBJECT

public:
    explicit MapManagerWindow(MapManager* mapManager, IO* io, QWidget* parent = nullptr);
    ~MapManagerWindow() override;

private slots:
    // Profile management
    void onNewProfile();
    void onDeleteProfile();
    void onDuplicateProfile();
    void onRenameProfile();
    void onProfileSelected(QListWidgetItem* item);
    void onActivateProfile();
    
    // Mapping management
    void onNewMapping();
    void onEditMapping();
    void onDeleteMapping();
    void onDuplicateMapping();
    void onMappingSelected(int row, int column);
    void onMappingDoubleClicked(int row, int column);
    
    // Import/Export
    void onImportProfile();
    void onExportProfile();
    void onSaveAll();
    void onLoadAll();
    
    // Macro recording
    void onStartMacroRecording();
    void onStopMacroRecording();
    
    // Apply changes
    void onApplyChanges();
    void onRevertChanges();
    
    // Hotkey capture
    void onCaptureHotkey();
    void onCaptureSourceKey();
    void onCaptureTargetKey();

private:
    void setupUI();
    void setupProfilePanel();
    void setupMappingPanel();
    void setupEditorPanel();
    void setupToolbar();
    void setupStatusBar();
    
    void refreshProfileList();
    void refreshMappingTable();
    void loadProfileDetails(const std::string& profileId);
    void loadMappingDetails(const std::string& profileId, const std::string& mappingId);
    void clearMappingDetails();
    
    void showMappingEditor(Mapping* mapping = nullptr);
    void showConditionEditor(Mapping* mapping);
    void showMacroEditor(Mapping* mapping);
    void showStatsDialog();
    
    std::string captureKeyPress();
    
    MapManager* mapManager;
    IO* io;
    
    // Current selection
    std::string currentProfileId;
    std::string currentMappingId;
    
    // UI Components
    QSplitter* mainSplitter;
    
    // Profile panel
    QListWidget* profileList;
    QPushButton* btnNewProfile;
    QPushButton* btnDeleteProfile;
    QPushButton* btnDuplicateProfile;
    QPushButton* btnActivateProfile;
    QLabel* lblActiveProfile;
    
    // Mapping table
    QTableWidget* mappingTable;
    QPushButton* btnNewMapping;
    QPushButton* btnEditMapping;
    QPushButton* btnDeleteMapping;
    QPushButton* btnDuplicateMapping;
    
    // Editor panel (right side)
    QTabWidget* editorTabs;
    
    // Basic settings tab
    QLineEdit* editMappingName;
    QCheckBox* chkEnabled;
    QComboBox* cmbMappingType;
    QComboBox* cmbActionType;
    
    // Source/Target tab
    QLineEdit* editSourceKey;
    QPushButton* btnCaptureSource;
    QListWidget* targetKeysList;
    QPushButton* btnAddTargetKey;
    QPushButton* btnRemoveTargetKey;
    QPushButton* btnCaptureTarget;
    
    // Autofire tab
    QCheckBox* chkAutofire;
    QSpinBox* spinAutofireInterval;
    QCheckBox* chkTurbo;
    QSpinBox* spinTurboInterval;
    
    // Advanced tab
    QCheckBox* chkToggleMode;
    QDoubleSpinBox* spinSensitivity;
    QDoubleSpinBox* spinDeadzone;
    QCheckBox* chkAcceleration;
    
    // Conditions tab
    QListWidget* conditionsList;
    QPushButton* btnAddCondition;
    QPushButton* btnEditCondition;
    QPushButton* btnRemoveCondition;
    
    // Macro tab
    QTableWidget* macroTable;
    QPushButton* btnRecordMacro;
    QPushButton* btnStopMacro;
    QPushButton* btnClearMacro;
    QLabel* lblMacroStatus;
    
    // Toolbar
    QPushButton* btnImport;
    QPushButton* btnExport;
    QPushButton* btnSaveAll;
    QPushButton* btnLoadAll;
    QPushButton* btnStats;
    
    // Status bar
    QLabel* lblStatus;
    
    // State
    bool isCapturingKey = false;
    bool isRecordingMacro = false;
};

/**
 * MappingEditorDialog - Dialog for editing a single mapping
 */
class MappingEditorDialog : public QDialog {
    Q_OBJECT

public:
    explicit MappingEditorDialog(Mapping* mapping, IO* io, QWidget* parent = nullptr);
    
    const Mapping& getMapping() const { return editedMapping; }
    bool wasAccepted() const { return accepted; }

private slots:
    void onAccept();
    void onReject();
    void onCaptureSource();
    void onCaptureTarget();
    void onAddTarget();
    void onRemoveTarget();

private:
    void setupUI();
    std::string captureKeyPress();
    
    Mapping editedMapping;
    IO* io;
    bool accepted = false;
    
    // UI components
    QLineEdit* editName;
    QLineEdit* editSourceKey;
    QPushButton* btnCaptureSource;
    QListWidget* targetList;
    QLineEdit* editNewTarget;
    QPushButton* btnCaptureTarget;
    QPushButton* btnAddTarget;
    QPushButton* btnRemoveTarget;
    QComboBox* cmbActionType;
    QCheckBox* chkAutofire;
    QSpinBox* spinInterval;
};

/**
 * ConditionEditorDialog - Dialog for editing mapping conditions
 */
class ConditionEditorDialog : public QDialog {
    Q_OBJECT

public:
    explicit ConditionEditorDialog(MappingCondition* condition, QWidget* parent = nullptr);
    
    MappingCondition getCondition() const { return editedCondition; }
    bool wasAccepted() const { return accepted; }

private slots:
    void onAccept();
    void onReject();
    void onConditionTypeChanged(int index);

private:
    void setupUI();
    
    MappingCondition editedCondition;
    bool accepted = false;
    
    QComboBox* cmbType;
    QLineEdit* editPattern;
    QLabel* lblPatternHelp;
};

/**
 * HotkeyCapture - Widget for capturing hotkey input
 */
class HotkeyCapture : public QLineEdit {
    Q_OBJECT

public:
    explicit HotkeyCapture(QWidget* parent = nullptr);
    
    void startCapture();
    void stopCapture();
    bool isCapturing() const { return capturing; }
    
    std::string getCapturedKey() const { return capturedKey; }

signals:
    void keyCaptured(const QString& key);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    bool capturing = false;
    std::string capturedKey;
    
    std::string keyEventToString(QKeyEvent* event);
    std::string mouseButtonToString(Qt::MouseButton button);
};

} // namespace havel
