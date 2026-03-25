/*
 * ocr_extension.cpp - Native OCR extension using Tesseract
 *
 * Uses C ABI (HavelCAPI.h) for stability.
 * Requests host services via API - does NOT access OS directly.
 */

#include "HavelCAPI.h"

#ifdef HAVE_TESSERACT
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
#endif

#include <unordered_map>
#include <memory>

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

} // anonymous namespace

HAVEL_EXTENSION(ocr) {
#ifdef HAVE_TESSERACT
  
  /* ocr.init(lang) -> handle */
  api->register_function("ocr", "init", [](int argc, HavelValue** argv) {
    const char* lang = "eng";
    if (argc >= 1 && api->get_string(argv[0])) {
      lang = api->get_string(argv[0]);
    }
    
    auto* tessApi = new tesseract::TessBaseAPI();
    if (tessApi->Init(nullptr, lang)) {
      delete tessApi;
      return api->new_int(0);  /* 0 = invalid handle */
    }
    
    int64_t id = g_nextEngineId++;
    g_ocrEngines[id] = std::unique_ptr<tesseract::TessBaseAPI>(tessApi);
    
    return api->new_handle(reinterpret_cast<void*>(id), cleanupEngine);
  });
  
  /* ocr.close(handle) -> bool */
  api->register_function("ocr", "close", [](int argc, HavelValue** argv) {
    if (argc < 1) {
      return api->new_bool(0);
    }
    
    int64_t id = reinterpret_cast<int64_t>(api->get_handle(argv[0]));
    auto it = g_ocrEngines.find(id);
    if (it == g_ocrEngines.end()) {
      return api->new_bool(0);
    }
    
    it->second->End();
    g_ocrEngines.erase(it);
    
    return api->new_bool(1);
  });
  
  /* ocr.recognizeFile(handle, path) -> string */
  api->register_function("ocr", "recognizeFile", [](int argc, HavelValue** argv) {
    if (argc < 2) {
      return api->new_string("");
    }
    
    int64_t id = reinterpret_cast<int64_t>(api->get_handle(argv[0]));
    const char* path = api->get_string(argv[1]);
    
    if (!path) {
      return api->new_string("");
    }
    
    auto engineIt = g_ocrEngines.find(id);
    if (engineIt == g_ocrEngines.end()) {
      return api->new_string("");
    }
    
    auto* tessApi = engineIt->second.get();
    
    /* Load image */
    pix* image = pixRead(path);
    if (!image) {
      return api->new_string("");
    }
    
    tessApi->SetImage(image);
    
    /* Recognize text */
    char* text = tessApi->GetUTF8Text();
    std::string result(text ? text : "");
    
    if (text) {
      delete[] text;
    }
    pixDestroy(&image);
    
    return api->new_string(result.c_str());
  });
  
  /* ocr.confidence(handle, path) -> float */
  api->register_function("ocr", "confidence", [](int argc, HavelValue** argv) {
    if (argc < 2) {
      return api->new_float(-1.0);
    }
    
    int64_t id = reinterpret_cast<int64_t>(api->get_handle(argv[0]));
    const char* path = api->get_string(argv[1]);
    
    if (!path) {
      return api->new_float(-1.0);
    }
    
    auto engineIt = g_ocrEngines.find(id);
    if (engineIt == g_ocrEngines.end()) {
      return api->new_float(-1.0);
    }
    
    auto* tessApi = engineIt->second.get();
    
    /* Load image */
    pix* image = pixRead(path);
    if (!image) {
      return api->new_float(-1.0);
    }
    
    tessApi->SetImage(image);
    
    /* Get confidence */
    int confidence = tessApi->MeanTextConf();
    
    pixDestroy(&image);
    
    return api->new_float(static_cast<double>(confidence));
  });
  
  /* ocr.getAvailableLanguages() -> string (comma-separated) */
  api->register_function("ocr", "getAvailableLanguages", [](int argc, HavelValue** argv) {
    (void)argc;
    (void)argv;
    /* Return common languages */
    return api->new_string("eng,deu,fra,spa,ita,por,nld");
  });
  
#else
  /* Tesseract not available - stub functions */
  
  api->register_function("ocr", "init", [](int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    return api->new_int(0);
  });
  
  api->register_function("ocr", "close", [](int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    return api->new_bool(0);
  });
  
  api->register_function("ocr", "recognizeFile", [](int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    return api->new_string("");
  });
  
  api->register_function("ocr", "confidence", [](int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    return api->new_float(-1.0);
  });
  
  api->register_function("ocr", "getAvailableLanguages", [](int argc, HavelValue** argv) {
    (void)argc; (void)argv;
    return api->new_string("");
  });
#endif
}
