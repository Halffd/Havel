#include "ScriptRunner.hpp"
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QTextEdit>
#include <QProgressBar>
#include <QPushButton>

ScriptRunner::ScriptRunner(QWidget* parent) : QMainWindow(parent) {
    setupUI();
}

void ScriptRunner::setupUI() {
    setWindowTitle("Script Runner Dashboard");
    resize(800, 600);

    QSplitter* mainSplitter = new QSplitter(Qt::Horizontal, this);
    setCentralWidget(mainSplitter);

    // Left side: Script Tree
    QTreeWidget* scriptTree = new QTreeWidget(this);
    scriptTree->setHeaderLabels({"Scripts"});
    mainSplitter->addWidget(scriptTree);

    // Right side: Output and controls
    QWidget* rightPanel = new QWidget(this);
    QVBoxLayout* rightLayout = new QVBoxLayout(rightPanel);
    mainSplitter->addWidget(rightPanel);

    QTextEdit* outputLog = new QTextEdit(this);
    outputLog->setReadOnly(true);
    outputLog->setPlaceholderText("Script output will appear here...");

    QProgressBar* progressBar = new QProgressBar(this);
    QPushButton* runButton = new QPushButton("Run Script", this);

    rightLayout->addWidget(outputLog);
    rightLayout->addWidget(progressBar);
    rightLayout->addWidget(runButton);

    mainSplitter->setSizes({200, 600});
}

void ScriptRunner::runScript() {}
void ScriptRunner::scriptFinished(int, QProcess::ExitStatus) {}
void ScriptRunner::onReadyReadStandardOutput() {}
void ScriptRunner::onReadyReadStandardError() {}
