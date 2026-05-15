#include "OCR.hpp"

#ifdef HAVE_OPENCV
#ifdef COUNT
#undef COUNT
#endif
#ifdef EPS
#undef EPS
#endif
#include <opencv2/opencv.hpp>
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
#endif

#include <iostream>
#include <algorithm>
#include <numeric>

namespace havel {

struct OCR::Impl {
#ifdef HAVE_TESSERACT
    std::unique_ptr<tesseract::TessBaseAPI> tessApi;
#endif
};

#ifdef HAVE_OPENCV

OCR::OCR(const OCRConfig& cfg) : config(cfg), impl_(std::make_unique<Impl>()) {
#ifdef HAVE_TESSERACT
    impl_->tessApi = std::make_unique<tesseract::TessBaseAPI>();
    impl_->tessApi->SetPageSegMode(static_cast<tesseract::PageSegMode>(config.psm));
    impl_->tessApi->SetVariable("preserve_interword_spaces", config.preserveInterwordSpaces ? "1" : "0");
    if (!config.charWhitelist.empty()) {
        impl_->tessApi->SetVariable("tessedit_char_whitelist", config.charWhitelist.c_str());
    }
    if (impl_->tessApi->Init(config.dataPath.c_str(), config.language.c_str(), tesseract::OEM_LSTM_ONLY)) {
        throw std::runtime_error("Could not initialize tesseract with language: " + config.language);
    }
#endif
}

OCR::~OCR() = default;

OCR::OCRResult OCR::recognize(const cv::Mat& image) {
#ifdef HAVE_TESSERACT
    if (image.empty() || !impl_->tessApi) return OCRResult{};

    cv::Mat processed = preprocessImage(image);
    impl_->tessApi->SetImage(processed.data, processed.cols, processed.rows, processed.channels(), processed.step);

    char* outText = impl_->tessApi->GetUTF8Text();
    if (!outText) return OCRResult{};

    OCRResult result;
    result.text = std::string(outText);
    delete[] outText;

    tesseract::ResultIterator* ri = impl_->tessApi->GetIterator();
    if (ri) {
        std::vector<float> confidences;
        tesseract::PageIteratorLevel level = tesseract::RIL_WORD;
        do {
            float conf = ri->Confidence(level);
            if (conf >= 0) confidences.push_back(conf);
            if (conf >= config.minConfidence) {
                int x1, y1, x2, y2;
                if (ri->BoundingBox(level, &x1, &y1, &x2, &y2)) {
                    result.wordBoxes.emplace_back(x1, y1, x2 - x1, y2 - y1);
                    result.wordConfidences.push_back(conf);
                }
            }
        } while (ri->Next(level));

        if (!confidences.empty()) {
            result.confidence = std::accumulate(confidences.begin(), confidences.end(), 0.0f) / confidences.size();
        }
    }

    return result;
#else
    (void)image;
    return OCRResult{};
#endif
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
    if (input.channels() > 1) {
        cv::cvtColor(input, img, cv::COLOR_BGR2GRAY);
    } else {
        img = input.clone();
    }
    if (upscale && scaleFactor > 1) {
        cv::resize(img, img, cv::Size(), scaleFactor, scaleFactor, cv::INTER_CUBIC);
    }
    cv::medianBlur(img, img, 3);
    cv::adaptiveThreshold(img, img, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY, 31, 5);
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(2, 2));
    cv::morphologyEx(img, img, cv::MORPH_CLOSE, kernel);
    return img;
}

cv::Mat OCR::preprocessForUI(const cv::Mat& input) {
    cv::Mat img;
    if (input.channels() > 1) {
        cv::cvtColor(input, img, cv::COLOR_BGR2GRAY);
    } else {
        img = input.clone();
    }
    cv::resize(img, img, cv::Size(), 2.0, 2.0, cv::INTER_CUBIC);
    cv::threshold(img, img, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    return img;
}

cv::Mat OCR::preprocessForText(const cv::Mat& input) {
    cv::Mat img;
    if (input.channels() > 1) {
        cv::cvtColor(input, img, cv::COLOR_BGR2GRAY);
    } else {
        img = input.clone();
    }
    cv::resize(img, img, cv::Size(), 2.0, 2.0, cv::INTER_CUBIC);
    cv::medianBlur(img, img, 3);
    cv::adaptiveThreshold(img, img, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY, 15, 5);
    return img;
}

OCR::OCRResult OCR::recognizeWithDetails(const cv::Mat& image) {
    return recognize(image);
}

#endif // HAVE_OPENCV

void OCR::setLanguage(const std::string& lang) {
    config.language = lang;
#ifdef HAVE_OPENCV
#ifdef HAVE_TESSERACT
    if (impl_ && impl_->tessApi) {
        impl_->tessApi->Init(config.dataPath.c_str(), config.language.c_str(), tesseract::OEM_LSTM_ONLY);
    }
#endif
#endif
}

void OCR::setDataPath(const std::string& path) {
    config.dataPath = path;
#ifdef HAVE_OPENCV
#ifdef HAVE_TESSERACT
    if (impl_ && impl_->tessApi) {
        impl_->tessApi->Init(config.dataPath.c_str(), config.language.c_str(), tesseract::OEM_LSTM_ONLY);
    }
#endif
#endif
}

void OCR::setPageSegmentationMode(PageSegmentationMode mode) {
    config.psm = mode;
#ifdef HAVE_OPENCV
#ifdef HAVE_TESSERACT
    if (impl_ && impl_->tessApi) {
        impl_->tessApi->SetPageSegMode(static_cast<tesseract::PageSegMode>(config.psm));
    }
#endif
#endif
}

void OCR::setCharWhitelist(const std::string& whitelist) {
    config.charWhitelist = whitelist;
#ifdef HAVE_OPENCV
#ifdef HAVE_TESSERACT
    if (impl_ && impl_->tessApi) {
        impl_->tessApi->SetVariable("tessedit_char_whitelist", whitelist.c_str());
    }
#endif
#endif
}

void OCR::setPreserveInterwordSpaces(bool preserve) {
    config.preserveInterwordSpaces = preserve;
#ifdef HAVE_OPENCV
#ifdef HAVE_TESSERACT
    if (impl_ && impl_->tessApi) {
        impl_->tessApi->SetVariable("preserve_interword_spaces", preserve ? "1" : "0");
    }
#endif
#endif
}

void OCR::setMinConfidence(int minConf) {
    config.minConfidence = minConf;
}

std::string OCR::getLanguage() const { return config.language; }
std::string OCR::getDataPath() const { return config.dataPath; }
OCR::PageSegmentationMode OCR::getPageSegmentationMode() const { return config.psm; }
std::string OCR::getCharWhitelist() const { return config.charWhitelist; }
bool OCR::getPreserveInterwordSpaces() const { return config.preserveInterwordSpaces; }
int OCR::getMinConfidence() const { return config.minConfidence; }

} // namespace havel
