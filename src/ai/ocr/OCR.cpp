#include "OCR.hpp"
#include <iostream>
#include <algorithm>
#include <numeric>

namespace havel {

OCR::OCR(const OCRConfig& cfg) : config(cfg) {
    initializeTesseract();
}

OCR::~OCR() = default;

void OCR::initializeTesseract() {
    tessApi = std::make_unique<tesseract::TessBaseAPI>();
    applyConfiguration();
    
    if (tessApi->Init(config.dataPath.c_str(), config.language.c_str(), tesseract::OEM_LSTM_ONLY)) {
        throw std::runtime_error("Could not initialize tesseract with language: " + config.language);
    }
}

void OCR::applyConfiguration() {
    tessApi->SetPageSegMode(static_cast<tesseract::PageSegMode>(config.psm));
    tessApi->SetVariable("preserve_interword_spaces", config.preserveInterwordSpaces ? "1" : "0");
    
    if (!config.charWhitelist.empty()) {
        tessApi->SetVariable("tessedit_char_whitelist", config.charWhitelist.c_str());
    }
}

OCR::OCRResult OCR::recognize(const cv::Mat& image) {
    if (image.empty()) {
        return OCRResult{};
    }

    // Preprocess the image
    cv::Mat processed = preprocessImage(image);
    
    // Set the image for tesseract
    tessApi->SetImage(processed.data, processed.cols, processed.rows, processed.channels(), processed.step);
    
    // Perform OCR
    char* outText = tessApi->GetUTF8Text();
    if (!outText) {
        return OCRResult{};
    }
    
    OCRResult result;
    result.text = std::string(outText);
    result.confidence = calculateAverageConfidence();
    
    delete[] outText;
    
    // Get bounding boxes for words
    auto wordResults = getWordResults();
    for (const auto& wordResult : wordResults) {
        result.wordBoxes.push_back(wordResult.first);
        result.wordConfidences.push_back(wordResult.second);
    }
    
    return result;
}

OCR::OCRResult OCR::recognize(const std::string& imagePath) {
    cv::Mat image = cv::imread(imagePath);
    return recognize(image);
}

std::string OCR::recognizeText(const cv::Mat& image) {
    OCRResult result = recognize(image);
    return result.text;
}

cv::Mat OCR::preprocessImage(const cv::Mat& input, bool upscale, int scaleFactor) {
    cv::Mat img;
    
    // Convert to grayscale if needed
    if (input.channels() > 1) {
        cv::cvtColor(input, img, cv::COLOR_BGR2GRAY);
    } else {
        img = input.clone();
    }
    
    // Upscale for better OCR accuracy
    if (upscale && scaleFactor > 1) {
        cv::resize(img, img, cv::Size(), scaleFactor, scaleFactor, cv::INTER_CUBIC);
    }
    
    // Denoise
    cv::medianBlur(img, img, 3);
    
    // Adaptive threshold
    cv::adaptiveThreshold(
        img, img, 255,
        cv::ADAPTIVE_THRESH_GAUSSIAN_C,
        cv::THRESH_BINARY,
        31, 5
    );
    
    // Morphological cleanup
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(2, 2));
    cv::morphologyEx(img, img, cv::MORPH_CLOSE, kernel);
    
    return img;
}

cv::Mat OCR::preprocessForUI(const cv::Mat& input) {
    cv::Mat img;
    
    // Convert to grayscale if needed
    if (input.channels() > 1) {
        cv::cvtColor(input, img, cv::COLOR_BGR2GRAY);
    } else {
        img = input.clone();
    }
    
    // UI elements are usually clean, so just upscale and threshold
    cv::resize(img, img, cv::Size(), 2.0, 2.0, cv::INTER_CUBIC);
    
    // Simple threshold for UI text
    cv::threshold(img, img, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    
    return img;
}

cv::Mat OCR::preprocessForText(const cv::Mat& input) {
    cv::Mat img;
    
    // Convert to grayscale if needed
    if (input.channels() > 1) {
        cv::cvtColor(input, img, cv::COLOR_BGR2GRAY);
    } else {
        img = input.clone();
    }
    
    // Upscale for better OCR accuracy
    cv::resize(img, img, cv::Size(), 2.0, 2.0, cv::INTER_CUBIC);
    
    // Denoise
    cv::medianBlur(img, img, 3);
    
    // Adaptive threshold for text
    cv::adaptiveThreshold(
        img, img, 255,
        cv::ADAPTIVE_THRESH_GAUSSIAN_C,
        cv::THRESH_BINARY,
        15, 5
    );
    
    return img;
}

void OCR::setLanguage(const std::string& lang) {
    config.language = lang;
    tessApi->Init(config.dataPath.c_str(), config.language.c_str(), tesseract::OEM_LSTM_ONLY);
}

void OCR::setDataPath(const std::string& path) {
    config.dataPath = path;
    tessApi->Init(config.dataPath.c_str(), config.language.c_str(), tesseract::OEM_LSTM_ONLY);
}

void OCR::setPageSegmentationMode(PageSegmentationMode mode) {
    config.psm = mode;
    tessApi->SetPageSegMode(static_cast<tesseract::PageSegMode>(config.psm));
}

void OCR::setCharWhitelist(const std::string& whitelist) {
    config.charWhitelist = whitelist;
    if (!whitelist.empty()) {
        tessApi->SetVariable("tessedit_char_whitelist", config.charWhitelist.c_str());
    } else {
        tessApi->SetVariable("tessedit_char_whitelist", ""); // Clear whitelist
    }
}

void OCR::setPreserveInterwordSpaces(bool preserve) {
    config.preserveInterwordSpaces = preserve;
    tessApi->SetVariable("preserve_interword_spaces", preserve ? "1" : "0");
}

void OCR::setMinConfidence(int minConf) {
    config.minConfidence = minConf;
}

std::string OCR::getLanguage() const {
    return config.language;
}

std::string OCR::getDataPath() const {
    return config.dataPath;
}

OCR::PageSegmentationMode OCR::getPageSegmentationMode() const {
    return config.psm;
}

std::string OCR::getCharWhitelist() const {
    return config.charWhitelist;
}

bool OCR::getPreserveInterwordSpaces() const {
    return config.preserveInterwordSpaces;
}

int OCR::getMinConfidence() const {
    return config.minConfidence;
}

float OCR::calculateAverageConfidence() {
    tesseract::ResultIterator* ri = tessApi->GetIterator();
    if (!ri) return 0.0f;

    std::vector<float> confidences;
    tesseract::PageIteratorLevel level = tesseract::RIL_WORD;

    do {
        float conf = ri->Confidence(level);
        if (conf >= 0) {  // Confidence values are typically 0-100
            confidences.push_back(conf);
        }
    } while (ri->Next(level));

    if (confidences.empty()) {
        return 0.0f;
    }

    float sum = std::accumulate(confidences.begin(), confidences.end(), 0.0f);
    return sum / static_cast<float>(confidences.size());
}

std::vector<std::pair<cv::Rect, float>> OCR::getWordResults() {
    std::vector<std::pair<cv::Rect, float>> results;
    
    tesseract::ResultIterator* ri = tessApi->GetIterator();
    if (!ri) return results;

    tesseract::PageIteratorLevel level = tesseract::RIL_WORD;

    do {
        float conf = ri->Confidence(level);
        if (conf >= config.minConfidence) {
            int x1, y1, x2, y2;
            if (ri->BoundingBox(level, &x1, &y1, &x2, &y2)) {
                cv::Rect bbox(x1, y1, x2 - x1, y2 - y1);
                results.emplace_back(bbox, conf);
            }
        }
    } while (ri->Next(level));

    return results;
}

OCR::OCRResult OCR::recognizeWithDetails(const cv::Mat& image) {
    if (image.empty()) {
        return OCRResult{};
    }

    // Preprocess the image
    cv::Mat processed = preprocessImage(image);
    
    // Set the image for tesseract
    tessApi->SetImage(processed.data, processed.cols, processed.rows, processed.channels(), processed.step);
    
    // Perform OCR
    char* outText = tessApi->GetUTF8Text();
    if (!outText) {
        return OCRResult{};
    }
    
    OCRResult result;
    result.text = std::string(outText);
    result.confidence = calculateAverageConfidence();
    
    delete[] outText;
    
    // Get detailed word results
    auto wordResults = getWordResults();
    for (const auto& wordResult : wordResults) {
        result.wordBoxes.push_back(wordResult.first);
        result.wordConfidences.push_back(wordResult.second);
    }
    
    return result;
}

} // namespace havel