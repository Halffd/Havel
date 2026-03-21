/*
 * NetworkService.hpp - HTTP client service (stub, header-only)
 * Full implementation requires libcurl
 */
#pragma once

#include <string>
#include <map>
#include <memory>
#include <unordered_map>

#include "../../havel-lang/runtime/Environment.hpp"

namespace havel::host {

using havel::HavelValue;
using havel::HavelRuntimeError;
// BuiltinFunction is already available from Environment.hpp

struct HttpResponse {
    int statusCode = 0;
    std::string body;
    std::map<std::string, std::string> headers;
    bool success = false;
    std::string error;
};

class NetworkService {
    int m_defaultTimeout = 30000;
    std::string m_userAgent = "Havel/1.0";
    
public:
    HttpResponse get(const std::string& url, const std::map<std::string, std::string>& = {}, int = 30000) {
        return errorResponse("NetworkService not implemented (requires libcurl)");
    }
    HttpResponse post(const std::string&, const std::string&, const std::map<std::string, std::string>& = {}, int = 30000) {
        return errorResponse("NetworkService not implemented (requires libcurl)");
    }
    HttpResponse put(const std::string&, const std::string&, const std::map<std::string, std::string>& = {}, int = 30000) {
        return errorResponse("NetworkService not implemented (requires libcurl)");
    }
    HttpResponse del(const std::string&, const std::map<std::string, std::string>& = {}, int = 30000) {
        return errorResponse("NetworkService not implemented (requires libcurl)");
    }
    bool download(const std::string&, const std::string&) { return false; }
    HttpResponse upload(const std::string&, const std::string&) { return errorResponse("NetworkService not implemented"); }
    void setDefaultTimeout(int ms) { m_defaultTimeout = ms; }
    int getDefaultTimeout() const { return m_defaultTimeout; }
    void setUserAgent(const std::string& ua) { m_userAgent = ua; }
    std::string getUserAgent() const { return m_userAgent; }
    
private:
    HttpResponse errorResponse(const std::string& msg) {
        HttpResponse r; r.statusCode = 501; r.error = msg; return r;
    }
};

// Module registration (co-located)
class Environment;
class IHostAPI;
struct HavelValue;
struct HavelRuntimeError;
template<typename T> struct BuiltinFunction;

inline void registerNetworkModule(havel::Environment& env, std::shared_ptr<havel::IHostAPI>) {
    auto netService = std::make_shared<NetworkService>();
    auto netObj = std::make_shared<std::unordered_map<std::string, HavelValue>>();
    
    (*netObj)["get"] = HavelValue(::havel::BuiltinFunction(
        [netService](const std::vector<HavelValue>& args) -> HavelResult {
            if (args.empty()) return HavelRuntimeError("get requires URL");
            auto resp = netService->get(args[0].asString());
            return HavelValue(resp.success ? resp.body : resp.error);
        }));
    
    (*netObj)["post"] = HavelValue(::havel::BuiltinFunction(
        [netService](const std::vector<HavelValue>& args) -> HavelResult {
            if (args.size() < 2) return HavelRuntimeError("post requires (url, body)");
            auto resp = netService->post(args[0].asString(), args[1].asString());
            return HavelValue(resp.success ? resp.body : resp.error);
        }));
    
    env.Define("network", HavelValue(netObj));
}

} // namespace havel::host
