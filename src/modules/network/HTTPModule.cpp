/*
 * HTTPModule.cpp
 *
 * HTTP client module for Havel language.
 * Provides REST API client functionality (get, post, put, delete, download,
 * upload).
 */
#include "HTTPModule.hpp"
#include "../../havel-lang/runtime/Environment.hpp"
#include "core/net/HttpModule.hpp"

namespace havel::modules {

void registerHTTPModule(Environment &env, std::shared_ptr<IHostAPI> hostAPI) {
  (void)hostAPI; // HTTP doesn't need host context

  // HTTP client for REST API calls
  auto httpMod =
      std::make_shared<std::unordered_map<std::string, HavelValue>>();

  // Helper to convert value to string
  auto valueToString = [](const HavelValue &v) -> std::string {
    if (v.isString())
      return v.asString();
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
    if (v.isBool())
      return v.asBool() ? "true" : "false";
    return "";
  };

  // Helper to create response object
  auto createResponse = [](const HttpResponse &response) -> HavelValue {
    auto obj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    (*obj)["statusCode"] = HavelValue(static_cast<double>(response.statusCode));
    (*obj)["body"] = HavelValue(response.body);
    (*obj)["ok"] = HavelValue(response.ok());
    if (!response.error.empty()) {
      (*obj)["error"] = HavelValue(response.error);
    }
    return HavelValue(obj);
  };

  // =========================================================================
  // http.get(url) - HTTP GET request
  // =========================================================================

  (*httpMod)["get"] = HavelValue(BuiltinFunction(
      [valueToString,
       createResponse](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError("http.get() requires URL");
        }

        std::string url = valueToString(args[0]);
        auto response = getHttp().get(url);
        return createResponse(response);
      }));

  // =========================================================================
  // http.post(url, [data]) - HTTP POST request
  // =========================================================================

  (*httpMod)["post"] = HavelValue(BuiltinFunction(
      [valueToString,
       createResponse](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError("http.post() requires URL");
        }

        std::string url = valueToString(args[0]);
        std::string data = args.size() > 1 ? valueToString(args[1]) : "";
        auto response = getHttp().post(url, data);
        return createResponse(response);
      }));

  // =========================================================================
  // http.put(url, [data]) - HTTP PUT request
  // =========================================================================

  (*httpMod)["put"] = HavelValue(BuiltinFunction(
      [valueToString,
       createResponse](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError("http.put() requires URL");
        }

        std::string url = valueToString(args[0]);
        std::string data = args.size() > 1 ? valueToString(args[1]) : "";
        auto response = getHttp().put(url, data);
        return createResponse(response);
      }));

  // =========================================================================
  // http.delete(url) - HTTP DELETE request
  // =========================================================================

  (*httpMod)["delete"] = HavelValue(BuiltinFunction(
      [valueToString,
       createResponse](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError("http.delete() requires URL");
        }

        std::string url = valueToString(args[0]);
        auto response = getHttp().del(url);
        return createResponse(response);
      }));

  // =========================================================================
  // http.download(url, path) - Download file from URL
  // =========================================================================

  (*httpMod)["download"] = HavelValue(BuiltinFunction(
      [valueToString](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2) {
          return HavelRuntimeError("http.download() requires (url, path)");
        }

        std::string url = valueToString(args[0]);
        std::string path = valueToString(args[1]);
        return HavelValue(getHttp().download(url, path));
      }));

  // =========================================================================
  // http.upload(url, path) - Upload file to URL
  // =========================================================================

  (*httpMod)["upload"] = HavelValue(BuiltinFunction(
      [valueToString,
       createResponse](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.size() < 2) {
          return HavelRuntimeError("http.upload() requires (url, path)");
        }

        std::string url = valueToString(args[0]);
        std::string path = valueToString(args[1]);
        auto response = getHttp().upload(url, path);
        return createResponse(response);
      }));

  // =========================================================================
  // http.setTimeout(ms) - Set HTTP timeout
  // =========================================================================

  (*httpMod)["setTimeout"] = HavelValue(BuiltinFunction(
      [](const std::vector<HavelValue> &args) -> HavelResult {
        if (args.empty()) {
          return HavelRuntimeError("http.setTimeout() requires timeout in ms");
        }

        int timeout = static_cast<int>(args[0].asNumber());
        getHttp().setTimeout(timeout);
        return HavelValue(true);
      }));

  // Register http module
  env.Define("http", HavelValue(httpMod));
}

} // namespace havel::modules
