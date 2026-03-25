/*
 * ocr_extension.cpp - Native OCR extension using Tesseract
 *
 * Provides optical character recognition:
 * - recognize text from image
 * - multiple languages
 * - confidence scores
 */

#include "extensions/ExtensionAPI.hpp"

#ifdef HAVE_TESSERACT
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
#endif

#include <unordered_map>

namespace {

#ifdef HAVE_TESSERACT
std::unordered_map<int64_t, std::unique_ptr<tesseract::TessBaseAPI>> g_ocrEngines;
int64_t g_nextEngineId = 1;
#endif

} // anonymous namespace

HAVEL_EXTENSION(ocr) {
#ifdef HAVE_TESSERACT
  
  // ==========================================================================
  // Engine management
  // ==========================================================================
  
  api.addFunction("ocr", "init", [](const std::vector<havel::ExtensionValue>& args) {
    std::string lang = "eng";
    if (!args.empty()) {
      if (const std::string* s = std::get_if<std::string>(&args[0])) {
        lang = *s;
      }
    }
    
    auto* api = new tesseract::TessBaseAPI();
    if (api->Init(nullptr, lang.c_str())) {
      delete api;
      return havel::ExtensionValue(static_cast<int64_t>(0));
    }
    
    int64_t id = g_nextEngineId++;
    g_ocrEngines[id] = std::unique_ptr<tesseract::TessBaseAPI>(api);
    
    return havel::ExtensionValue(id);
  });
  
  api.addFunction("ocr", "close", [](const std::vector<havel::ExtensionValue>& args) {
    if (args.empty()) {
      return havel::ExtensionValue(false);
    }
    
    int64_t* id = std::get_if<int64_t>(&args[0]);
    if (!id) {
      return havel::ExtensionValue(false);
    }
    
    auto it = g_ocrEngines.find(*id);
    if (it == g_ocrEngines.end()) {
      return havel::ExtensionValue(false);
    }
    
    it->second->End();
    g_ocrEngines.erase(it);
    
    return havel::ExtensionValue(true);
  });
  
  // ==========================================================================
  // OCR operations
  // ==========================================================================
  
  api.addFunction("ocr", "recognize", [](const std::vector<havel::ExtensionValue>& args) {
    if (args.size() < 2) {
      return havel::ExtensionValue(std::string(""));
    }
    
    const int64_t* engineId = std::get_if<int64_t>(&args[0]);
    const int64_t* imageId = std::get_if<int64_t>(&args[1]);
    
    if (!engineId || !imageId) {
      return havel::ExtensionValue(std::string(""));
    }
    
    auto engineIt = g_ocrEngines.find(*engineId);
    if (engineIt == g_ocrEngines.end()) {
      return havel::ExtensionValue(std::string(""));
    }
    
    // Get image from image extension (would need cross-extension communication)
    // For now, this is a placeholder - full implementation needs image sharing
    
    auto* api = engineIt->second.get();
    
    // Placeholder - in real implementation would get image and process
    // For now return empty string
    return havel::ExtensionValue(std::string(""));
  });
  
  api.addFunction("ocr", "recognizeFile", [](const std::vector<havel::ExtensionValue>& args) {
    if (args.size() < 2) {
      return havel::ExtensionValue(std::string(""));
    }
    
    const int64_t* engineId = std::get_if<int64_t>(&args[0]);
    const std::string* path = std::get_if<std::string>(&args[1]);
    
    if (!engineId || !path) {
      return havel::ExtensionValue(std::string(""));
    }
    
    auto engineIt = g_ocrEngines.find(*engineId);
    if (engineIt == g_ocrEngines.end()) {
      return havel::ExtensionValue(std::string(""));
    }
    
    auto* api = engineIt->second.get();
    
    // Load image
    pix* image = pixRead(path->c_str());
    if (!image) {
      return havel::ExtensionValue(std::string(""));
    }
    
    api->SetImage(image);
    
    // Recognize text
    char* text = api->GetUTF8Text();
    std::string result(text ? text : "");
    
    if (text) {
      delete[] text;
    }
    pixDestroy(&image);
    
    return havel::ExtensionValue(result);
  });
  
  api.addFunction("ocr", "confidence", [](const std::vector<havel::ExtensionValue>& args) {
    if (args.size() < 2) {
      return havel::ExtensionValue(-1.0);
    }
    
    const int64_t* engineId = std::get_if<int64_t>(&args[0]);
    const std::string* path = std::get_if<std::string>(&args[1]);
    
    if (!engineId || !path) {
      return havel::ExtensionValue(-1.0);
    }
    
    auto engineIt = g_ocrEngines.find(*engineId);
    if (engineIt == g_ocrEngines.end()) {
      return havel::ExtensionValue(-1.0);
    }
    
    auto* api = engineIt->second.get();
    
    // Load image
    pix* image = pixRead(path->c_str());
    if (!image) {
      return havel::ExtensionValue(-1.0);
    }
    
    api->SetImage(image);
    
    // Get confidence
    int confidence = api->MeanTextConf();
    
    pixDestroy(&image);
    
    return havel::ExtensionValue(static_cast<double>(confidence));
  });
  
  // ==========================================================================
  // Configuration
  // ==========================================================================
  
  api.addFunction("ocr", "setVariable", [](const std::vector<havel::ExtensionValue>& args) {
    if (args.size() < 3) {
      return havel::ExtensionValue(false);
    }
    
    const int64_t* engineId = std::get_if<int64_t>(&args[0]);
    const std::string* name = std::get_if<std::string>(&args[1]);
    const std::string* value = std::get_if<std::string>(&args[2]);
    
    if (!engineId || !name || !value) {
      return havel::ExtensionValue(false);
    }
    
    auto engineIt = g_ocrEngines.find(*engineId);
    if (engineIt == g_ocrEngines.end()) {
      return havel::ExtensionValue(false);
    }
    
    auto* api = engineIt->second.get();
    return havel::ExtensionValue(api->SetVariable(name->c_str(), value->c_str()));
  });
  
  api.addFunction("ocr", "getAvailableLanguages", [](const std::vector<havel::ExtensionValue>& args) {
    (void)args;
    // Return common languages as a simple string list
    // Full implementation would query tesseract
    return havel::ExtensionValue(std::string("eng,deu,fra,spa,ita,por,nld"));
  });
  
#else
  // Tesseract not available - stub functions
  
  api.addFunction("ocr", "init", [](const std::vector<havel::ExtensionValue>& args) {
    (void)args;
    return havel::ExtensionValue(static_cast<int64_t>(0));
  });
  
  api.addFunction("ocr", "close", [](const std::vector<havel::ExtensionValue>& args) {
    (void)args;
    return havel::ExtensionValue(false);
  });
  
  api.addFunction("ocr", "recognize", [](const std::vector<havel::ExtensionValue>& args) {
    (void)args;
    return havel::ExtensionValue(std::string(""));
  });
  
  api.addFunction("ocr", "recognizeFile", [](const std::vector<havel::ExtensionValue>& args) {
    (void)args;
    return havel::ExtensionValue(std::string(""));
  });
  
  api.addFunction("ocr", "confidence", [](const std::vector<havel::ExtensionValue>& args) {
    (void)args;
    return havel::ExtensionValue(-1.0);
  });
  
  api.addFunction("ocr", "setVariable", [](const std::vector<havel::ExtensionValue>& args) {
    (void)args;
    return havel::ExtensionValue(false);
  });
  
  api.addFunction("ocr", "getAvailableLanguages", [](const std::vector<havel::ExtensionValue>& args) {
    (void)args;
    return havel::ExtensionValue(std::string(""));
  });
#endif
}
