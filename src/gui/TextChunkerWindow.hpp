#pragma once

#include <QMainWindow>
#include <string>

// Forward declarations for Qt classes to reduce header dependencies
class QLabel;
class QSpinBox;
class QPushButton;
class QScrollArea;
class QClipboard;
class QKeyEvent;

namespace havel::gui {

class TextChunkerWindow : public QMainWindow {
    Q_OBJECT

public:
    TextChunkerWindow(const std::string& inputText, size_t size = 20000, bool tail = false, QWidget* parent = nullptr);
    ~TextChunkerWindow() override;

public slots:
    void goNext();
    void goPrev();
    void loadNewText();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void onChunkSizeChanged();

private:
    void setupUI();
    void recalcChunks();
    std::string getChunk(int pos);
    void updateUI();

    std::string text;
    size_t chunk_size;
    bool tail_mode;
    bool inverted;
    int current_chunk;
    int total_chunks;

    QLabel* chunkLabel;
    QLabel* infoLabel;
    QLabel* helpLabel;
    QSpinBox* chunkSizeSpinBox;
    QScrollArea* scrollArea;
    QClipboard* clipboard;
};

} // namespace havel::gui
