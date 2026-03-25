/*
 * ocr_extension.cpp - Native OCR extension using Tesseract
 *
 * Uses C ABI (HavelCAPI.h) for stability.
 * All functions are static C functions - no captures in lambdas.
 */

#include "HavelCAPI.h"

#ifdef HAVE_TESSERACT
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
#endif

#include <unordered_map>
#include <memory>
#include <cstdio>

namespace {

#ifdef HAVE_TESSERACT
/* OCR engine handle system */
std::unordered_map<int64_t, std::unique_ptr<tesseract::TessBaseAPI>> g_ocrEngines;
int64_t g_nextEngineId = 1;

void cleanupEngine(void* ptr) {
    int64_t id = reinterpret_cast<int64_t>(ptr);
    auto it = g_ocrEngines.find(id);
    if (it != g_ocrEngines.end()) {
        it->second->End();
        g_ocrEngines.erase(it);
    }
}
#endif

/* Static C functions - no captures */

#ifdef HAVE_TESSERACT
static HavelValue* ocr_init(int argc, HavelValue** argv) {
    const char* lang = "eng";
    if (argc >= 1 && havel_get_type(argv[0]) == HAVEL_STRING) {
        lang = havel_get_string(argv[0]);
    }
    
    auto* tessApi = new tesseract::TessBaseAPI();
    if (tessApi->Init(nullptr, lang)) {
        delete tessApi;
        return havel_new_int(0);  /* 0 = invalid handle */
    }
    
    int64_t id = g_nextEngineId++;
    g_ocrEngines[id] = std::unique_ptr<tesseract::TessBaseAPI>(tessApi);
    
    return havel_new_handle(reinterpret_cast<void*>(id), cleanupEngine);
}

static HavelValue* ocr_close(int argc, HavelValue** argv) {
    if (argc < 1) return havel_new_bool(0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    auto it = g_ocrEngines.find(id);
    if (it == g_ocrEngines.end()) {
        return havel_new_bool(0);
    }
    
    it->second->End();
    g_ocrEngines.erase(it);
    
    return havel_new_bool(1);
}

static HavelValue* ocr_recognizeFile(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_string("");
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    const char* path = havel_get_string(argv[1]);
    
    if (!path) return havel_new_string("");
    
    auto engineIt = g_ocrEngines.find(id);
    if (engineIt == g_ocrEngines.end()) {
        return havel_new_string("");
    }
    
    auto* tessApi = engineIt->second.get();
    
    /* Load image */
    Pix* image = pixRead(path);
    if (!image) {
        return havel_new_string("");
    }
    
    tessApi->SetImage(image);
    
    /* Recognize text */
    char* text = tessApi->GetUTF8Text();
    HavelValue* result = havel_new_string(text ? text : "");
    
    if (text) delete[] text;
    pixDestroy(&image);
    
    return result;
}

static HavelValue* ocr_confidence(int argc, HavelValue** argv) {
    if (argc < 2) return havel_new_float(-1.0);
    
    int64_t id = reinterpret_cast<int64_t>(havel_get_handle(argv[0]));
    const char* path = havel_get_string(argv[1]);
    
    if (!path) return havel_new_float(-1.0);
    
    auto engineIt = g_ocrEngines.find(id);
    if (engineIt == g_ocrEngines.end()) {
        return havel_new_float(-1.0);
    }
    
    auto* tessApi = engineIt->second.get();
    
    /* Load image */
    Pix* image = pixRead(path);
    if (!image) {
        return havel_new_float(-1.0);
    }
    
    tessApi->SetImage(image);
    
    /* Get confidence */
    int confidence = tessApi->MeanTextConf();
    
    pixDestroy(&image);
    
    return havel_new_float(static_cast<double>(confidence));
}

static HavelValue* ocr_getAvailableLanguages(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    /* Return common languages */
    return havel_new_string("eng,deu,fra,spa,ita,por,nld");
}

#else
/* Tesseract not available - stub functions */

static HavelValue* ocr_init(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    return havel_new_int(0);
}

static HavelValue* ocr_close(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    return havel_new_bool(0);
}

static HavelValue* ocr_recognizeFile(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    return havel_new_string("");
}

static HavelValue* ocr_confidence(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    return havel_new_float(-1.0);
}

static HavelValue* ocr_getAvailableLanguages(int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    return havel_new_string("");
}
#endif

} /* anonymous namespace */

extern "C" void havel_extension_init(HavelAPI* api) {
    /* Register all OCR functions */
    api->register_function("ocr", "init", ocr_init);
    api->register_function("ocr", "close", ocr_close);
    api->register_function("ocr", "recognizeFile", ocr_recognizeFile);
    api->register_function("ocr", "confidence", ocr_confidence);
    api->register_function("ocr", "getAvailableLanguages", ocr_getAvailableLanguages);
}
