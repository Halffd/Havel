#pragma once
#ifdef HAVE_QT_EXTENSION
#include "qt.hpp"

// #include <QMainWindow>
#include "extensions/gui/automation_suite/AutomationSuite.hpp"
#include "core/config/ConfigManager.hpp"

namespace havel {
    class AutomationSuite;
// Settings window class
class SettingsWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit SettingsWindow(AutomationSuite* suite, QWidget* parent = nullptr);
    
protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void setupUI();
    
    AutomationSuite* automationSuite;
};

} // namespace havel

#endif // HAVE_QT_EXTENSION