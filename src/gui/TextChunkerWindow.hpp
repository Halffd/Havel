// TextChunkerWindow.hpp
#pragma once

#include <QMainWindow>
#include <string>

class QLabel;
class QSpinBox;
class QScrollArea;
class QClipboard;

namespace havel::gui {

class TextChunkerWindow : public QMainWindow {
    Q_OBJECT

public:
    TextChunkerWindow(const std::string& inputText, size_t size = 20000, bool tail = false, QWidget* parent = nullptr);
    ~TextChunkerWindow();

    static TextChunkerWindow* instance;

    // Public methods for global hotkey control
    void nextChunk();
    void prevChunk();
    void invertMode();
    void recopyChunk();
    void increaseLimit();
    void decreaseLimit();
    void loadNewText();
    void toggleVisibility();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void onChunkSizeChanged();

private:
    void recalcChunks();
    std::string getChunk(int pos);
    void updateUI();
    void goNext();
    void goPrev();
    void setupUI();

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