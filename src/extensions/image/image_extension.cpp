/*
 * image_extension.cpp - Native image processing extension
 *
 * Provides image operations via OpenCV:
 * - load, save
 * - resize, crop, rotate
 * - filters (blur, sharpen, edge detection)
 * - color operations
 */

#include "extensions/ExtensionAPI.hpp"

#include <opencv2/opencv.hpp>
#include <unordered_map>
#include <memory>

namespace {

// Simple image handle system
std::unordered_map<int64_t, std::shared_ptr<cv::Mat>> g_images;
int64_t g_nextImageId = 1;

int64_t storeImage(const cv::Mat& img) {
  int64_t id = g_nextImageId++;
  g_images[id] = std::make_shared<cv::Mat>(img.clone());
  return id;
}

cv::Mat* getImage(int64_t id) {
  auto it = g_images.find(id);
  if (it == g_images.end()) {
    return nullptr;
  }
  return it->second.get();
}

void removeImage(int64_t id) {
  g_images.erase(id);
}

} // anonymous namespace

HAVEL_EXTENSION(image) {
  // ==========================================================================
  // Image loading/saving
  // ==========================================================================
  
  api.addFunction("image", "load", [](const std::vector<havel::ExtensionValue>& args) {
    if (args.empty()) {
      return havel::ExtensionValue(nullptr);
    }
    
    const std::string* path = std::get_if<std::string>(&args[0]);
    if (!path) {
      return havel::ExtensionValue(nullptr);
    }
    
    cv::Mat img = cv::imread(*path);
    if (img.empty()) {
      return havel::ExtensionValue(static_cast<int64_t>(0));
    }
    
    return havel::ExtensionValue(storeImage(img));
  });
  
  api.addFunction("image", "save", [](const std::vector<havel::ExtensionValue>& args) {
    if (args.size() < 2) {
      return havel::ExtensionValue(false);
    }
    
    const int64_t* id = std::get_if<int64_t>(&args[0]);
    const std::string* path = std::get_if<std::string>(&args[1]);
    
    if (!id || !path) {
      return havel::ExtensionValue(false);
    }
    
    cv::Mat* img = getImage(*id);
    if (!img) {
      return havel::ExtensionValue(false);
    }
    
    return havel::ExtensionValue(cv::imwrite(*path, *img));
  });
  
  // ==========================================================================
  // Image properties
  // ==========================================================================
  
  api.addFunction("image", "width", [](const std::vector<havel::ExtensionValue>& args) {
    if (args.empty()) {
      return havel::ExtensionValue(static_cast<int64_t>(0));
    }
    
    const int64_t* id = std::get_if<int64_t>(&args[0]);
    if (!id) {
      return havel::ExtensionValue(static_cast<int64_t>(0));
    }
    
    cv::Mat* img = getImage(*id);
    if (!img) {
      return havel::ExtensionValue(static_cast<int64_t>(0));
    }
    
    return havel::ExtensionValue(static_cast<int64_t>(img->cols));
  });
  
  api.addFunction("image", "height", [](const std::vector<havel::ExtensionValue>& args) {
    if (args.empty()) {
      return havel::ExtensionValue(static_cast<int64_t>(0));
    }
    
    const int64_t* id = std::get_if<int64_t>(&args[0]);
    if (!id) {
      return havel::ExtensionValue(static_cast<int64_t>(0));
    }
    
    cv::Mat* img = getImage(*id);
    if (!img) {
      return havel::ExtensionValue(static_cast<int64_t>(0));
    }
    
    return havel::ExtensionValue(static_cast<int64_t>(img->rows));
  });
  
  // ==========================================================================
  // Image transformations
  // ==========================================================================
  
  api.addFunction("image", "resize", [](const std::vector<havel::ExtensionValue>& args) {
    if (args.size() < 3) {
      return havel::ExtensionValue(static_cast<int64_t>(0));
    }
    
    const int64_t* id = std::get_if<int64_t>(&args[0]);
    const int64_t* width = std::get_if<int64_t>(&args[1]);
    const int64_t* height = std::get_if<int64_t>(&args[2]);
    
    if (!id || !width || !height) {
      return havel::ExtensionValue(static_cast<int64_t>(0));
    }
    
    cv::Mat* img = getImage(*id);
    if (!img) {
      return havel::ExtensionValue(static_cast<int64_t>(0));
    }
    
    cv::Mat resized;
    cv::resize(*img, resized, cv::Size(*width, *height));
    
    return havel::ExtensionValue(storeImage(resized));
  });
  
  api.addFunction("image", "rotate", [](const std::vector<havel::ExtensionValue>& args) {
    if (args.size() < 2) {
      return havel::ExtensionValue(static_cast<int64_t>(0));
    }
    
    const int64_t* id = std::get_if<int64_t>(&args[0]);
    const double* angle = std::get_if<double>(&args[1]);
    if (!angle) {
      const int64_t* angleInt = std::get_if<int64_t>(&args[1]);
      if (angleInt) {
        angle = new double(static_cast<double>(*angleInt));
      }
    }
    
    if (!id || !angle) {
      return havel::ExtensionValue(static_cast<int64_t>(0));
    }
    
    cv::Mat* img = getImage(*id);
    if (!img) {
      return havel::ExtensionValue(static_cast<int64_t>(0));
    }
    
    cv::Point2f center(img->cols / 2.0, img->rows / 2.0);
    cv::Mat rot = cv::getRotationMatrix2D(center, *angle, 1.0);
    cv::Mat rotated;
    cv::warpAffine(*img, rotated, rot, img->size());
    
    return havel::ExtensionValue(storeImage(rotated));
  });
  
  api.addFunction("image", "crop", [](const std::vector<havel::ExtensionValue>& args) {
    if (args.size() < 5) {
      return havel::ExtensionValue(static_cast<int64_t>(0));
    }
    
    const int64_t* id = std::get_if<int64_t>(&args[0]);
    const int64_t* x = std::get_if<int64_t>(&args[1]);
    const int64_t* y = std::get_if<int64_t>(&args[2]);
    const int64_t* w = std::get_if<int64_t>(&args[3]);
    const int64_t* h = std::get_if<int64_t>(&args[4]);
    
    if (!id || !x || !y || !w || !h) {
      return havel::ExtensionValue(static_cast<int64_t>(0));
    }
    
    cv::Mat* img = getImage(*id);
    if (!img) {
      return havel::ExtensionValue(static_cast<int64_t>(0));
    }
    
    cv::Rect roi(*x, *y, *w, *h);
    cv::Mat cropped = (*img)(roi);
    
    return havel::ExtensionValue(storeImage(cropped));
  });
  
  // ==========================================================================
  // Filters
  // ==========================================================================
  
  api.addFunction("image", "blur", [](const std::vector<havel::ExtensionValue>& args) {
    if (args.size() < 2) {
      return havel::ExtensionValue(static_cast<int64_t>(0));
    }
    
    const int64_t* id = std::get_if<int64_t>(&args[0]);
    const int64_t* ksize = std::get_if<int64_t>(&args[1]);
    
    if (!id || !ksize) {
      return havel::ExtensionValue(static_cast<int64_t>(0));
    }
    
    cv::Mat* img = getImage(*id);
    if (!img) {
      return havel::ExtensionValue(static_cast<int64_t>(0));
    }
    
    cv::Mat blurred;
    cv::blur(*img, blurred, cv::Size(*ksize, *ksize));
    
    return havel::ExtensionValue(storeImage(blurred));
  });
  
  api.addFunction("image", "grayscale", [](const std::vector<havel::ExtensionValue>& args) {
    if (args.empty()) {
      return havel::ExtensionValue(static_cast<int64_t>(0));
    }
    
    const int64_t* id = std::get_if<int64_t>(&args[0]);
    if (!id) {
      return havel::ExtensionValue(static_cast<int64_t>(0));
    }
    
    cv::Mat* img = getImage(*id);
    if (!img) {
      return havel::ExtensionValue(static_cast<int64_t>(0));
    }
    
    cv::Mat gray;
    cv::cvtColor(*img, gray, cv::COLOR_BGR2GRAY);
    
    return havel::ExtensionValue(storeImage(gray));
  });
  
  api.addFunction("image", "edges", [](const std::vector<havel::ExtensionValue>& args) {
    if (args.empty()) {
      return havel::ExtensionValue(static_cast<int64_t>(0));
    }
    
    const int64_t* id = std::get_if<int64_t>(&args[0]);
    if (!id) {
      return havel::ExtensionValue(static_cast<int64_t>(0));
    }
    
    cv::Mat* img = getImage(*id);
    if (!img) {
      return havel::ExtensionValue(static_cast<int64_t>(0));
    }
    
    cv::Mat edges;
    cv::Canny(*img, edges, 100, 200);
    
    return havel::ExtensionValue(storeImage(edges));
  });
  
  // ==========================================================================
  // Cleanup
  // ==========================================================================
  
  api.addFunction("image", "free", [](const std::vector<havel::ExtensionValue>& args) {
    if (args.empty()) {
      return havel::ExtensionValue(false);
    }
    
    const int64_t* id = std::get_if<int64_t>(&args[0]);
    if (!id) {
      return havel::ExtensionValue(false);
    }
    
    removeImage(*id);
    return havel::ExtensionValue(true);
  });
}
