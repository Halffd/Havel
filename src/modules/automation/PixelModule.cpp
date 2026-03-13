/*
 * PixelModule.cpp
 * 
 * Pixel and image recognition module for Havel language.
 * Host binding - connects language to PixelAutomation.
 */
#include "../../host/HostContext.hpp"
#include "../../havel-lang/runtime/Environment.hpp"
#include "core/automation/PixelAutomation.hpp"

namespace havel::modules {

void registerPixelModule(Environment& env, IHostAPI* hostAPI) {
    if (!hostAPI->GetIO() || !hostAPI->GetBrightnessManager()) {
        return;  // Skip if no pixel automation available
    }
    
    auto& pa = *hostAPI->GetPixelAutomation();
    IO* io = hostAPI->GetIO();  // For mouse position
    
    // Create pixel module object
    auto pixelObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    
    // Helper to convert value to string
    auto valueToString = [](const HavelValue& v) -> std::string {
        if (v.isString()) return v.asString();
        if (v.isNumber()) {
            double val = v.asNumber();
            if (val == std::floor(val) && std::abs(val) < 1e15) {
                return std::to_string(static_cast<long long>(val));
            } else {
                std::ostringstream oss;
                oss.precision(15);
                oss << val;
                std::string s = oss.str();
                if (s.find('.') != std::string::npos) {
                    size_t last = s.find_last_not_of('0');
                    if (last != std::string::npos && s[last] == '.') {
                        s = s.substr(0, last);
                    } else if (last != std::string::npos) {
                        s = s.substr(0, last + 1);
                    }
                }
                return s;
            }
        }
        if (v.isBool()) return v.asBool() ? "true" : "false";
        return "";
    };
    
    // =========================================================================
    // Pixel functions
    // =========================================================================
    
    (*pixelObj)["get"] = HavelValue(BuiltinFunction([&pa, io, &valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        int x, y;
        
        // Get cursor position if no args provided
        if (args.size() < 2) {
            if (io) {
                auto pos = io->GetMousePosition();
                x = pos.first;
                y = pos.second;
            } else {
                return HavelRuntimeError("pixel.get() requires (x, y) or IO system");
            }
        } else {
            x = static_cast<int>(args[0].asNumber());
            y = static_cast<int>(args[1].asNumber());
        }
        
        Color c = pa.getPixel(x, y);
        auto colorObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*colorObj)["r"] = HavelValue(static_cast<double>(c.r));
        (*colorObj)["g"] = HavelValue(static_cast<double>(c.g));
        (*colorObj)["b"] = HavelValue(static_cast<double>(c.b));
        (*colorObj)["a"] = HavelValue(static_cast<double>(c.a));
        (*colorObj)["hex"] = HavelValue(c.toHex());
        return HavelValue(colorObj);
    }));
    
    (*pixelObj)["match"] = HavelValue(BuiltinFunction([&pa, io, &valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 1) {
            return HavelRuntimeError("pixel.match() requires (color) or (x, y, color)");
        }
        
        int x, y;
        std::string color;
        int tolerance = 0;
        
        // If 1 arg: match at cursor position with color
        // If 2 args: match at cursor position with color and tolerance
        // If 3+ args: match at (x, y) with color (and optional tolerance)
        if (args.size() < 3) {
            // Use cursor position
            if (io) {
                auto pos = io->GetMousePosition();
                x = pos.first;
                y = pos.second;
            } else {
                return HavelRuntimeError("pixel.match() requires (x, y) when IO not available");
            }
            color = args[0].isString() ? args[0].asString() : valueToString(args[0]);
            if (args.size() >= 2) {
                tolerance = static_cast<int>(args[1].asNumber());
            }
        } else {
            // Use provided position
            x = static_cast<int>(args[0].asNumber());
            y = static_cast<int>(args[1].asNumber());
            color = args[2].isString() ? args[2].asString() : valueToString(args[2]);
            if (args.size() > 3) {
                tolerance = static_cast<int>(args[3].asNumber());
            }
        }
        
        return HavelValue(pa.pixelMatch(x, y, color, tolerance));
    }));
    
    (*pixelObj)["wait"] = HavelValue(BuiltinFunction([&pa, &valueToString](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 3) {
            return HavelRuntimeError("pixel.wait() requires (x, y, color)");
        }
        int x = static_cast<int>(args[0].asNumber());
        int y = static_cast<int>(args[1].asNumber());
        std::string color = args[2].isString() ? args[2].asString() : valueToString(args[2]);
        int tolerance = args.size() > 3 ? static_cast<int>(args[3].asNumber()) : 0;
        int timeout = args.size() > 4 ? static_cast<int>(args[4].asNumber()) : 5000;

        return HavelValue(pa.waitPixel(x, y, color, tolerance, timeout));
    }));

    (*pixelObj)["region"] = HavelValue(BuiltinFunction([](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.size() < 4) {
            return HavelRuntimeError("pixel.region() requires (x, y, width, height)");
        }
        int x = static_cast<int>(args[0].asNumber());
        int y = static_cast<int>(args[1].asNumber());
        int w = static_cast<int>(args[2].asNumber());
        int h = static_cast<int>(args[3].asNumber());
        
        auto regionObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*regionObj)["x"] = HavelValue(static_cast<double>(x));
        (*regionObj)["y"] = HavelValue(static_cast<double>(y));
        (*regionObj)["w"] = HavelValue(static_cast<double>(w));
        (*regionObj)["h"] = HavelValue(static_cast<double>(h));
        return HavelValue(regionObj);
    }));

    // =========================================================================
    // Image recognition functions
    // =========================================================================
    
    auto imageObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    
    // Helper to extract ScreenRegion from args
    auto extractRegion = [](const std::vector<HavelValue>& args, size_t startIndex) -> ScreenRegion {
        ScreenRegion region(0, 0, 0, 0);
        if (args.size() > startIndex && args[startIndex].is<std::shared_ptr<std::unordered_map<std::string, HavelValue>>>()) {
            auto regionMap = args[startIndex].get<std::shared_ptr<std::unordered_map<std::string, HavelValue>>>();
            if (regionMap) {
                int rx = 0, ry = 0, rw = 0, rh = 0;
                if (regionMap->count("x")) rx = static_cast<int>((*regionMap)["x"].asNumber());
                if (regionMap->count("y")) ry = static_cast<int>((*regionMap)["y"].asNumber());
                if (regionMap->count("w")) rw = static_cast<int>((*regionMap)["w"].asNumber());
                if (regionMap->count("h")) rh = static_cast<int>((*regionMap)["h"].asNumber());
                region = ScreenRegion(rx, ry, rw, rh);
            }
        }
        return region;
    };
    
    // Helper to create match result object
    auto createMatchResult = [](const ImageMatch& match) -> HavelValue {
        if (!match.found) {
            return HavelValue(nullptr);
        }
        auto matchObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
        (*matchObj)["found"] = HavelValue(true);
        (*matchObj)["x"] = HavelValue(static_cast<double>(match.x));
        (*matchObj)["y"] = HavelValue(static_cast<double>(match.y));
        (*matchObj)["w"] = HavelValue(static_cast<double>(match.w));
        (*matchObj)["h"] = HavelValue(static_cast<double>(match.h));
        (*matchObj)["confidence"] = HavelValue(match.confidence);
        (*matchObj)["centerX"] = HavelValue(static_cast<double>(match.centerX()));
        (*matchObj)["centerY"] = HavelValue(static_cast<double>(match.centerY()));
        return HavelValue(matchObj);
    };
    
    (*imageObj)["find"] = HavelValue(BuiltinFunction([&pa, extractRegion, createMatchResult](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("image.find() requires imagePath");
        }
        std::string imagePath = args[0].asString();
        
        ScreenRegion region = extractRegion(args, 1);
        float threshold = args.size() > 2 ? static_cast<float>(args[2].asNumber()) : 0.9f;
        
        ImageMatch match = pa.findImage(imagePath, region, threshold);
        return createMatchResult(match);
    }));
    
    (*imageObj)["wait"] = HavelValue(BuiltinFunction([&pa, extractRegion, createMatchResult](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("image.wait() requires imagePath");
        }
        std::string imagePath = args[0].asString();
        
        ScreenRegion region = extractRegion(args, 1);
        int timeout = args.size() > 2 ? static_cast<int>(args[2].asNumber()) : 5000;
        float threshold = args.size() > 3 ? static_cast<float>(args[3].asNumber()) : 0.9f;
        
        ImageMatch match = pa.waitImage(imagePath, region, timeout, threshold);
        return createMatchResult(match);
    }));
    
    (*imageObj)["exists"] = HavelValue(BuiltinFunction([&pa, extractRegion](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("image.exists() requires imagePath");
        }
        std::string imagePath = args[0].asString();
        
        ScreenRegion region = extractRegion(args, 1);
        float threshold = args.size() > 2 ? static_cast<float>(args[2].asNumber()) : 0.9f;
        
        return HavelValue(pa.existsImage(imagePath, region, threshold));
    }));
    
    (*imageObj)["count"] = HavelValue(BuiltinFunction([&pa, extractRegion](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("image.count() requires imagePath");
        }
        std::string imagePath = args[0].asString();
        
        ScreenRegion region = extractRegion(args, 1);
        float threshold = args.size() > 2 ? static_cast<float>(args[2].asNumber()) : 0.9f;
        
        return HavelValue(static_cast<double>(pa.countImage(imagePath, region, threshold)));
    }));
    
    (*imageObj)["findAll"] = HavelValue(BuiltinFunction([&pa, extractRegion](const std::vector<HavelValue>& args) -> HavelResult {
        if (args.empty()) {
            return HavelRuntimeError("image.findAll() requires imagePath");
        }
        std::string imagePath = args[0].asString();
        
        ScreenRegion region = extractRegion(args, 1);
        float threshold = args.size() > 2 ? static_cast<float>(args[2].asNumber()) : 0.9f;
        
        auto matches = pa.findAllImage(imagePath, region, threshold);
        auto resultArray = std::make_shared<std::vector<HavelValue>>();
        for (const auto& match : matches) {
            auto matchObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
            (*matchObj)["x"] = HavelValue(static_cast<double>(match.x));
            (*matchObj)["y"] = HavelValue(static_cast<double>(match.y));
            (*matchObj)["w"] = HavelValue(static_cast<double>(match.w));
            (*matchObj)["h"] = HavelValue(static_cast<double>(match.h));
            (*matchObj)["confidence"] = HavelValue(match.confidence);
            resultArray->push_back(HavelValue(matchObj));
        }
        return HavelValue(resultArray);
    }));
    
    // Register pixel and image modules
    (*pixelObj)["image"] = HavelValue(imageObj);
    env.Define("pixel", HavelValue(pixelObj));
}

} // namespace havel::modules
