#pragma once

#include <QProcess>
#include <QMainWindow>
#include <QTabWidget>
#include <QTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QProgressBar>

#undef Window // Undefine X11 macro
#undef None // Undefine X11 macro

class ScriptRunner : public QMainWindow {
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

    QTabWidget* tabWidget;
    QTextEdit* pythonScriptEdit;
    QTextEdit* luaScriptEdit;
    QTextEdit* havelScriptEdit;
    QTextEdit* outputLog;
    QPushButton* runButton;
    QProcess* process;
};
