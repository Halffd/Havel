#include "TextChunkerWindow.hpp"
#include <QApplication>
#include <QClipboard>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QSpinBox>
#include <QPushButton>
#include <QStatusBar>
#include <QKeyEvent>
#include <QString>
#include <QFont>
#include <QScreen>
#include <QTimer>
#include <algorithm>
#include <iostream>

namespace havel::gui {

TextChunkerWindow::TextChunkerWindow(const std::string& inputText, size_t size, bool tail, QWidget* parent)
    : QMainWindow(parent), text(inputText), chunk_size(size), tail_mode(tail), inverted(false), current_chunk(1) {

    recalcChunks();
    if (tail_mode) current_chunk = total_chunks;

    setupUI();
    updateUI();
    
    setAttribute(Qt::WA_DeleteOnClose);
    statusBar()->showMessage("Local hotkeys active. Global hotkeys are managed by Havel.", 5000);
}

TextChunkerWindow::~TextChunkerWindow() {
    // Destructor body
}

void TextChunkerWindow::recalcChunks() {
    total_chunks = (text.length() + chunk_size - 1) / chunk_size;
    if (total_chunks == 0) total_chunks = 1;
    if (current_chunk > total_chunks) current_chunk = total_chunks;
    if (current_chunk < 1) current_chunk = 1;
}

std::string TextChunkerWindow::getChunk(int pos) {
    if (pos < 1 || pos > total_chunks) return "";
    size_t start_pos, end_pos;
    if (tail_mode ^ inverted) {
        end_pos = text.length() - (total_chunks - pos) * chunk_size;
        start_pos = (end_pos > chunk_size) ? end_pos - chunk_size : 0;
    } else {
        start_pos = (pos - 1) * chunk_size;
        end_pos = std::min(start_pos + chunk_size, text.length());
    }
    return text.substr(start_pos, end_pos - start_pos);
}

void TextChunkerWindow::updateUI() {
    std::string chunk = getChunk(current_chunk);
    chunkLabel->setText(QString::fromStdString(chunk));

    QString info = QString("Chunk %1/%2 | %3 total chars | %4 chars per chunk")
                       .arg(current_chunk)
                       .arg(total_chunks)
                       .arg(text.length())
                       .arg(chunk_size);
    
    QString modes = "";
    if (tail_mode) modes += "TAIL ";
    if (inverted) modes += "INVERTED ";
    if (!modes.isEmpty()) info += " | " + modes.trimmed();
    
    infoLabel->setText(info);

    statusBar()->showMessage(QString("Copied %1 characters to clipboard").arg(chunk.length()));
    clipboard->setText(QString::fromStdString(chunk));

    if (!isActiveWindow()) {
        activateWindow();
        raise();
        setWindowOpacity(0.9);
        QTimer::singleShot(100, this, [this]() {
            setWindowOpacity(1.0);
        });
    }
}

void TextChunkerWindow::goNext() {
    if (tail_mode ^ inverted)
        current_chunk = std::max(1, current_chunk - 1);
    else
        current_chunk = std::min(total_chunks, current_chunk + 1);
    updateUI();
}

void TextChunkerWindow::goPrev() {
    if (tail_mode ^ inverted)
        current_chunk = std::min(total_chunks, current_chunk + 1);
    else
        current_chunk = std::max(1, current_chunk - 1);
    updateUI();
}

void TextChunkerWindow::loadNewText() {
    std::string newText = clipboard->text().toStdString();
    if (newText.empty()) {
        statusBar()->showMessage("No text in clipboard!", 3000);
        return;
    }
    
    text = newText;
    current_chunk = 1;
    if (tail_mode) {
        recalcChunks();
        current_chunk = total_chunks;
    } else {
        recalcChunks();
    }
    
    statusBar()->showMessage("Loaded new text from clipboard!", 2000);
    updateUI();
}

void TextChunkerWindow::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_N: case Qt::Key_Right: case Qt::Key_Space: case Qt::Key_Return: case Qt::Key_Enter:
        goNext();
        break;
    case Qt::Key_P: case Qt::Key_Left: case Qt::Key_Backspace:
        goPrev();
        break;
    case Qt::Key_R: case Qt::Key_C:
        clipboard->setText(QString::fromStdString(getChunk(current_chunk)));
        statusBar()->showMessage("Recopied to clipboard", 2000);
        break;
    case Qt::Key_I:
        inverted = !inverted;
        current_chunk = total_chunks - current_chunk + 1;
        updateUI();
        break;
    case Qt::Key_F: case Qt::Key_Home:
        current_chunk = (tail_mode ^ inverted) ? total_chunks : 1;
        updateUI();
        break;
    case Qt::Key_L: case Qt::Key_End:
        current_chunk = (tail_mode ^ inverted) ? 1 : total_chunks;
        updateUI();
        break;
    case Qt::Key_V:
        loadNewText();
        break;
    case Qt::Key_Q: case Qt::Key_Escape:
        this->close(); // Use close() instead of QApplication::quit()
        break;
    }
}

void TextChunkerWindow::onChunkSizeChanged() {
    chunk_size = chunkSizeSpinBox->value();
    recalcChunks();
    updateUI();
}

void TextChunkerWindow::setupUI() {
    // This is mostly the same as the user's code, just with some adjustments.
    QWidget* central = new QWidget(this);
    QVBoxLayout* mainLayout = new QVBoxLayout(central);
    mainLayout->setSpacing(15);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    QHBoxLayout* controlsLayout = new QHBoxLayout();
    QLabel* chunkSizeLabel = new QLabel("Chunk Size:");
    QFont controlFont = chunkSizeLabel->font();
    controlFont.setPointSize(14);
    controlFont.setBold(true);
    chunkSizeLabel->setFont(controlFont);
    controlsLayout->addWidget(chunkSizeLabel);
    
    chunkSizeSpinBox = new QSpinBox(this);
    chunkSizeSpinBox->setRange(100, 100000);
    chunkSizeSpinBox->setValue(chunk_size);
    chunkSizeSpinBox->setSingleStep(1000);
    QFont spinBoxFont = chunkSizeSpinBox->font();
    spinBoxFont.setPointSize(14);
    chunkSizeSpinBox->setFont(spinBoxFont);
    connect(chunkSizeSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &TextChunkerWindow::onChunkSizeChanged);
    controlsLayout->addWidget(chunkSizeSpinBox);
    
    controlsLayout->addStretch();
    mainLayout->addLayout(controlsLayout);

    scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    chunkLabel = new QLabel();
    chunkLabel->setWordWrap(true);
    chunkLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    chunkLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    chunkLabel->setMargin(15);
    
    QFont chunkFont("Consolas", 12);
    if (!chunkFont.exactMatch()) chunkFont.setFamily("Monaco");
    if (!chunkFont.exactMatch()) chunkFont.setFamily("Courier New");
    chunkLabel->setFont(chunkFont);

    scrollArea->setWidget(chunkLabel);
    mainLayout->addWidget(scrollArea, 1);

    infoLabel = new QLabel(this);
    infoLabel->setAlignment(Qt::AlignCenter);
    infoLabel->setWordWrap(true);
    QFont infoFont = infoLabel->font();
    infoFont.setPointSize(16);
    infoFont.setBold(true);
    infoLabel->setFont(infoFont);
    mainLayout->addWidget(infoLabel);

    helpLabel = new QLabel("⌨️  Local: N/Space/Enter/→=Next  P/Backspace/←=Prev  R/C=Recopy  V=New Text  Q/Esc=Quit", this);
    helpLabel->setAlignment(Qt::AlignCenter);
    helpLabel->setWordWrap(true);
    QFont helpFont = helpLabel->font();
    helpFont.setPointSize(13);
    helpFont.setBold(true);
    helpLabel->setFont(helpFont);
    mainLayout->addWidget(helpLabel);

    setCentralWidget(central);

    resize(1200, 850);
    setMinimumSize(800, 600);
    setWindowTitle("Text Chunker");

    setStyleSheet(R"(
        QMainWindow { background-color: #1e1e1e; color: #ffffff; }
        QLabel { color: #ffffff; background-color: transparent; }
        QLabel#chunkLabel { background-color: #2d2d2d; border: 2px solid #404040; border-radius: 8px; padding: 15px; selection-background-color: #0078d4; }
        QScrollArea { background-color: #2d2d2d; border: 2px solid #404040; border-radius: 8px; }
        QScrollBar:vertical { background: #404040; width: 12px; border-radius: 6px; }
        QScrollBar::handle:vertical { background: #606060; border-radius: 6px; min-height: 20px; }
        QScrollBar::handle:vertical:hover { background: #707070; }
        QSpinBox { background-color: #2d2d2d; border: 2px solid #404040; border-radius: 6px; padding: 8px; color: #ffffff; font-size: 14px; min-width: 120px; }
        QSpinBox:focus { border-color: #0078d4; }
        QStatusBar { background-color: #2d2d2d; color: #ffffff; border-top: 1px solid #404040; font-size: 12px; }
    )");

    chunkLabel->setObjectName("chunkLabel");
    statusBar()->setSizeGripEnabled(true);
    statusBar()->showMessage("Ready");

    clipboard = QApplication::clipboard();
}

} // namespace havel::gui
