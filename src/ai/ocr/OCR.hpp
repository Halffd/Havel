#pragma once

#include <string>
#include <memory>
#include <opencv2/opencv.hpp>
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>

namespace havel {

class OCR {
public:
    enum class PageSegmentationMode {
        OSD_ONLY = 0,
        AUTO_OSD = 1,
        AUTO_ONLY = 2,
        SINGLE_COLUMN = 3,
        SINGLE_BLOCK_VERT_TEXT = 4,
        SINGLE_BLOCK = 5,
        SINGLE_LINE = 6,
        SINGLE_WORD = 7,
        CIRCLE_WORD = 8,
        SINGLE_CHAR = 9,
        SPARSE_TEXT = 10,
        SPARSE_TEXT_OSD = 11,
        RAW_LINE = 12,
        RAW_BOX = 13,
        UNLV = 14,
        AUTO = 15
    };

    struct OCRConfig {
        std::string language;
        std::string dataPath;
        PageSegmentationMode psm;
        std::string charWhitelist;
        bool preserveInterwordSpaces;
        int minConfidence;

        OCRConfig() : language("eng"), dataPath("/usr/share/tessdata"),
                      psm(PageSegmentationMode::SINGLE_BLOCK),
                      preserveInterwordSpaces(true), minConfidence(0) {}
    };

    struct OCRResult {
        std::string text;
        float confidence = 0.0f;
        cv::Rect boundingBox;
        std::vector<cv::Rect> wordBoxes;
        std::vector<float> wordConfidences;
    };

    explicit OCR(const OCRConfig& config = OCRConfig{});
    ~OCR();

    // Main OCR functions
    OCRResult recognize(const cv::Mat& image);
    OCRResult recognize(const std::string& imagePath);
    std::string recognizeText(const cv::Mat& image);
    
    // Preprocessing utilities
    static cv::Mat preprocessImage(const cv::Mat& input, bool upscale = true, int scaleFactor = 2);
    static cv::Mat preprocessForUI(const cv::Mat& input);
    static cv::Mat preprocessForText(const cv::Mat& input);
    
    // Configuration
    void setLanguage(const std::string& lang);
    void setDataPath(const std::string& path);
    void setPageSegmentationMode(PageSegmentationMode mode);
    void setCharWhitelist(const std::string& whitelist);
    void setPreserveInterwordSpaces(bool preserve);
    void setMinConfidence(int minConf);
    
    // Getters
    std::string getLanguage() const;
    std::string getDataPath() const;
    PageSegmentationMode getPageSegmentationMode() const;
    std::string getCharWhitelist() const;
    bool getPreserveInterwordSpaces() const;
    int getMinConfidence() const;

    // Advanced recognition with detailed results
    OCRResult recognizeWithDetails(const cv::Mat& image);

private:
    std::unique_ptr<tesseract::TessBaseAPI> tessApi;
    OCRConfig config;

    void initializeTesseract();
    void applyConfiguration();
    float calculateAverageConfidence();
    std::vector<std::pair<cv::Rect, float>> getWordResults();
};

} // namespace havel