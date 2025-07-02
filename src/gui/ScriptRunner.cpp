#include "ScriptRunner.hpp"

ScriptRunner::ScriptRunner(QWidget* parent) : havel::Window(parent) {
    setupUI();
}

void ScriptRunner::setupUI() {
    setWindowTitle("Script Runner Dashboard");
    resize(800, 600);

    havel::Splitter* mainSplitter = new havel::Splitter(Qt::Horizontal, this);
    setCentralWidget(mainSplitter);

    // Left side: Script Tree
    havel::TreeWidget* scriptTree = new havel::TreeWidget(this);
    scriptTree->setHeaderLabels({"Scripts"});
    mainSplitter->addWidget(scriptTree);

    // Right side: Output and controls
    havel::Widget* rightPanel = new havel::Widget(this);
    havel::Layout* rightLayout = new havel::Layout(rightPanel);
    mainSplitter->addWidget(rightPanel);

    havel::TextEdit* outputLog = new havel::TextEdit(this);
    outputLog->setReadOnly(true);
    outputLog->setPlaceholderText("Script output will appear here...");

    havel::ProgressBar* progressBar = new havel::ProgressBar(this);
    havel::Button* runButton = new havel::Button("Run Script", this);

    rightLayout->addWidget(outputLog);
    rightLayout->addWidget(progressBar);
    rightLayout->addWidget(runButton);

    mainSplitter->setSizes({200, 600});
}
