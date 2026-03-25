/*
 * image_extension.cpp - Native image processing extension
 *
 * Uses C ABI (HavelCAPI.h) for stability.
 * Requests host services via API - does NOT access OS directly.
 *
 * This maintains the sandbox model:
 * - Extensions request services from HostBridge
 * - HostBridge enforces capability checks
 * - Extensions don't bypass security
 */

#include "HavelCAPI.h"

#include <opencv2/opencv.hpp>
#include <unordered_map>
#include <memory>
#include <cstdio>

namespace {

/* Image handle system - opaque handles for VM */
std::unordered_map<int64_t, std::shared_ptr<cv::Mat>> g_images;
int64_t g_nextImageId = 1;

void cleanupImage(void* ptr) {
  int64_t id = reinterpret_cast<int64_t>(ptr);
  g_images.erase(id);
}

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

} // anonymous namespace

HAVEL_EXTENSION(image) {
  /* 
   * Register functions using C API.
   * No C++ wrappers - pure C ABI for stability.
   */
  
  /* image.load(path) -> handle */
  api->register_function("image", "load", [](int argc, HavelValue** argv) {
    if (argc < 1) {
      return api->new_null();
    }
    
    const char* path = api->get_string(argv[0]);
    if (!path) {
      return api->new_null();
    }
    
    cv::Mat img = cv::imread(path);
    if (img.empty()) {
      return api->new_int(0);  /* 0 = invalid handle */
    }
    
    int64_t id = storeImage(img);
    return api->new_handle(reinterpret_cast<void*>(id), cleanupImage);
  });
  
  /* image.save(handle, path) -> bool */
  api->register_function("image", "save", [](int argc, HavelValue** argv) {
    if (argc < 2) {
      return api->new_bool(0);
    }
    
    int64_t id = reinterpret_cast<int64_t>(api->get_handle(argv[0]));
    const char* path = api->get_string(argv[1]);
    
    if (!path) {
      return api->new_bool(0);
    }
    
    cv::Mat* img = getImage(id);
    if (!img) {
      return api->new_bool(0);
    }
    
    return api->new_bool(cv::imwrite(path, *img) ? 1 : 0);
  });
  
  /* image.width(handle) -> int */
  api->register_function("image", "width", [](int argc, HavelValue** argv) {
    if (argc < 1) {
      return api->new_int(0);
    }
    
    int64_t id = reinterpret_cast<int64_t>(api->get_handle(argv[0]));
    cv::Mat* img = getImage(id);
    if (!img) {
      return api->new_int(0);
    }
    
    return api->new_int(img->cols);
  });
  
  /* image.height(handle) -> int */
  api->register_function("image", "height", [](int argc, HavelValue** argv) {
    if (argc < 1) {
      return api->new_int(0);
    }
    
    int64_t id = reinterpret_cast<int64_t>(api->get_handle(argv[0]));
    cv::Mat* img = getImage(id);
    if (!img) {
      return api->new_int(0);
    }
    
    return api->new_int(img->rows);
  });
  
  /* image.resize(handle, width, height) -> new_handle */
  api->register_function("image", "resize", [](int argc, HavelValue** argv) {
    if (argc < 3) {
      return api->new_int(0);
    }
    
    int64_t id = reinterpret_cast<int64_t>(api->get_handle(argv[0]));
    int64_t width = api->get_int(argv[1]);
    int64_t height = api->get_int(argv[2]);
    
    cv::Mat* img = getImage(id);
    if (!img) {
      return api->new_int(0);
    }
    
    cv::Mat resized;
    cv::resize(*img, resized, cv::Size(width, height));
    
    int64_t newId = storeImage(resized);
    return api->new_handle(reinterpret_cast<void*>(newId), cleanupImage);
  });
  
  /* image.rotate(handle, angle) -> new_handle */
  api->register_function("image", "rotate", [](int argc, HavelValue** argv) {
    if (argc < 2) {
      return api->new_int(0);
    }
    
    int64_t id = reinterpret_cast<int64_t>(api->get_handle(argv[0]));
    double angle = api->get_float(argv[1]);
    
    cv::Mat* img = getImage(id);
    if (!img) {
      return api->new_int(0);
    }
    
    cv::Point2f center(img->cols / 2.0, img->rows / 2.0);
    cv::Mat rot = cv::getRotationMatrix2D(center, angle, 1.0);
    cv::Mat rotated;
    cv::warpAffine(*img, rotated, rot, img->size());
    
    int64_t newId = storeImage(rotated);
    return api->new_handle(reinterpret_cast<void*>(newId), cleanupImage);
  });
  
  /* image.crop(handle, x, y, w, h) -> new_handle */
  api->register_function("image", "crop", [](int argc, HavelValue** argv) {
    if (argc < 5) {
      return api->new_int(0);
    }
    
    int64_t id = reinterpret_cast<int64_t>(api->get_handle(argv[0]));
    int64_t x = api->get_int(argv[1]);
    int64_t y = api->get_int(argv[2]);
    int64_t w = api->get_int(argv[3]);
    int64_t h = api->get_int(argv[4]);
    
    cv::Mat* img = getImage(id);
    if (!img) {
      return api->new_int(0);
    }
    
    cv::Rect roi(x, y, w, h);
    cv::Mat cropped = (*img)(roi);
    
    int64_t newId = storeImage(cropped);
    return api->new_handle(reinterpret_cast<void*>(newId), cleanupImage);
  });
  
  /* image.blur(handle, ksize) -> new_handle */
  api->register_function("image", "blur", [](int argc, HavelValue** argv) {
    if (argc < 2) {
      return api->new_int(0);
    }
    
    int64_t id = reinterpret_cast<int64_t>(api->get_handle(argv[0]));
    int64_t ksize = api->get_int(argv[1]);
    
    cv::Mat* img = getImage(id);
    if (!img) {
      return api->new_int(0);
    }
    
    cv::Mat blurred;
    cv::blur(*img, blurred, cv::Size(ksize, ksize));
    
    int64_t newId = storeImage(blurred);
    return api->new_handle(reinterpret_cast<void*>(newId), cleanupImage);
  });
  
  /* image.grayscale(handle) -> new_handle */
  api->register_function("image", "grayscale", [](int argc, HavelValue** argv) {
    if (argc < 1) {
      return api->new_int(0);
    }
    
    int64_t id = reinterpret_cast<int64_t>(api->get_handle(argv[0]));
    cv::Mat* img = getImage(id);
    if (!img) {
      return api->new_int(0);
    }
    
    cv::Mat gray;
    cv::cvtColor(*img, gray, cv::COLOR_BGR2GRAY);
    
    int64_t newId = storeImage(gray);
    return api->new_handle(reinterpret_cast<void*>(newId), cleanupImage);
  });
  
  /* image.edges(handle) -> new_handle */
  api->register_function("image", "edges", [](int argc, HavelValue** argv) {
    if (argc < 1) {
      return api->new_int(0);
    }
    
    int64_t id = reinterpret_cast<int64_t>(api->get_handle(argv[0]));
    cv::Mat* img = getImage(id);
    if (!img) {
      return api->new_int(0);
    }
    
    cv::Mat edges;
    cv::Canny(*img, edges, 100, 200);
    
    int64_t newId = storeImage(edges);
    return api->new_handle(reinterpret_cast<void*>(newId), cleanupImage);
  });
}
