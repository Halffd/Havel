#pragma once

#include <QProcess>
#include "types.hpp"

class ScriptRunner : public havel::QWindow {
    Q_OBJECT

public:
    ScriptRunner(QWidget* parent = nullptr);

private slots:
    void runScript();
    void scriptFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onReadyReadStandardOutput();
    void onReadyReadStandardError();

private:
    void setupUI();
    QWidget* createScriptTab(const QString& language);

    havel::TabWidget* tabWidget;
    havel::TextEdit* pythonScriptEdit;
    havel::TextEdit* luaScriptEdit;
    havel::TextEdit* havelScriptEdit;
    havel::TextEdit* outputLog;
    havel::Button* runButton;
    QProcess* process;
};
