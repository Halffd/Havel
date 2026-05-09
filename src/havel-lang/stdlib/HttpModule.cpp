#include "HttpModule.hpp"

#include <curl/curl.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

using havel::compiler::Value;
using havel::compiler::VMApi;

namespace havel::stdlib {

static size_t curlWriteCb(void *ptr, size_t size, size_t nmemb, std::string *s) {
    s->append((char *)ptr, size * nmemb);
    return size * nmemb;
}

static size_t curlFileWriteCb(void *ptr, size_t size, size_t nmemb, FILE *fp) {
    return fwrite(ptr, size, nmemb, fp);
}

static size_t curlHeaderCb(void *buffer, size_t size, size_t nitems, void *userdata) {
    auto *hdrs = static_cast<std::unordered_map<std::string, std::string> *>(userdata);
    std::string line((char *)buffer, size * nitems);
    if (line.find("HTTP/") == 0) return size * nitems;
    auto colon = line.find(':');
    if (colon == std::string::npos) return size * nitems;
    std::string key = line.substr(0, colon);
    std::string val = line.substr(colon + 1);
    auto trim = [](std::string &s) {
        s.erase(0, s.find_first_not_of(" \t"));
        s.erase(s.find_last_not_of(" \t\r\n") + 1);
    };
    trim(key);
    trim(val);
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);
    (*hdrs)[key] = val;
    return size * nitems;
}

static bool curlInitialized = false;
static void ensureCurl() {
    if (!curlInitialized) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curlInitialized = true;
    }
}

struct HttpRequestOpts {
    int timeout = 30000;
    bool followRedirects = true;
    bool verifySsl = true;
    std::unordered_map<std::string, std::string> headers;
};

static HttpRequestOpts parseOpts(const Value &opts, const VMApi &api) {
    HttpRequestOpts o;
    if (!opts.isObjectId()) return o;

    auto timeoutVal = api.getField(opts, "timeout");
    if (timeoutVal.isInt()) o.timeout = static_cast<int>(timeoutVal.asInt());
    else if (timeoutVal.isDouble()) o.timeout = static_cast<int>(timeoutVal.asDouble());

    auto followVal = api.getField(opts, "follow_redirects");
    if (followVal.isBool()) o.followRedirects = followVal.asBool();

    auto sslVal = api.getField(opts, "verify_ssl");
    if (sslVal.isBool()) o.verifySsl = sslVal.asBool();

    auto headersVal = api.getField(opts, "headers");
    if (headersVal.isObjectId()) {
        auto keys = api.getObjectKeys(headersVal);
        for (auto &k : keys) {
            auto v = api.getField(headersVal, k);
            if (v.isStringId()) {
                o.headers[k] = api.toString(v);
            }
        }
    }
    return o;
}

static Value makeResponseObj(int status, const std::string &body,
                              const std::unordered_map<std::string, std::string> &headers,
                              const std::string &error, const VMApi &api) {
    auto obj = api.makeObject();

    api.setField(obj, "status", Value::makeInt(status));
    api.setField(obj, "ok", Value::makeBool(status >= 200 && status < 300));
    api.setField(obj, "body", api.makeString(body));
    api.setField(obj, "error", error.empty() ? Value::makeNull() : api.makeString(error));

    auto hdrsObj = api.makeObject();
    for (auto &[k, v] : headers) {
        api.setField(hdrsObj, k, api.makeString(v));
    }
    api.setField(obj, "headers", hdrsObj);

    return obj;
}

static Value doRequest(const std::string &method, const std::string &url,
                        const std::string &data, const HttpRequestOpts &opts, const VMApi &api) {
    ensureCurl();
    CURL *curl = curl_easy_init();
    if (!curl) {
        return makeResponseObj(0, "", {}, "failed to initialize curl", api);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, opts.timeout);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, opts.timeout / 3);

    if (opts.followRedirects) {
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    }

    if (!opts.verifySsl) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
    } else if (method == "PUT") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    } else if (method == "DELETE") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    } else if (method == "PATCH") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
    } else if (method == "HEAD") {
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    }

    struct curl_slist *headerList = nullptr;

    bool hasContentType = false;
    for (auto &[k, v] : opts.headers) {
        std::string h = k + ": " + v;
        headerList = curl_slist_append(headerList, h.c_str());
        if (k == "content-type" || k == "Content-Type") hasContentType = true;
    }
    if (!hasContentType && !data.empty() && (method == "POST" || method == "PUT" || method == "PATCH")) {
        headerList = curl_slist_append(headerList, "Content-Type: application/json");
    }

    if (headerList) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    }

    if (!data.empty() && (method == "POST" || method == "PUT" || method == "PATCH")) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, data.size());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
    }

    std::string responseBody;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);

    std::unordered_map<std::string, std::string> responseHeaders;
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curlHeaderCb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &responseHeaders);

    CURLcode res = curl_easy_perform(curl);

    int statusCode = 0;
    std::string error;

    if (res != CURLE_OK) {
        error = std::string("curl error: ") + curl_easy_strerror(res);
    } else {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        statusCode = static_cast<int>(code);
    }

    curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);

    return makeResponseObj(statusCode, responseBody, responseHeaders, error, api);
}

void registerHttpModule(const VMApi &api) {
    api.registerFunction("http.get", [api](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("http.get requires a url");
        std::string url = api.toString(args[0]);
        HttpRequestOpts opts;
        if (args.size() > 1) opts = parseOpts(args[1], api);
        return doRequest("GET", url, "", opts, api);
    });

    api.registerFunction("http.post", [api](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("http.post requires a url");
        std::string url = api.toString(args[0]);
        std::string data;
        if (args.size() > 1 && args[1].isStringId()) data = api.toString(args[1]);
        HttpRequestOpts opts;
        if (args.size() > 2) opts = parseOpts(args[2], api);
        return doRequest("POST", url, data, opts, api);
    });

    api.registerFunction("http.put", [api](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("http.put requires a url");
        std::string url = api.toString(args[0]);
        std::string data;
        if (args.size() > 1 && args[1].isStringId()) data = api.toString(args[1]);
        HttpRequestOpts opts;
        if (args.size() > 2) opts = parseOpts(args[2], api);
        return doRequest("PUT", url, data, opts, api);
    });

    api.registerFunction("http.del", [api](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("http.del requires a url");
        std::string url = api.toString(args[0]);
        HttpRequestOpts opts;
        if (args.size() > 1) opts = parseOpts(args[1], api);
        return doRequest("DELETE", url, "", opts, api);
    });

    api.registerFunction("http.patch", [api](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("http.patch requires a url");
        std::string url = api.toString(args[0]);
        std::string data;
        if (args.size() > 1 && args[1].isStringId()) data = api.toString(args[1]);
        HttpRequestOpts opts;
        if (args.size() > 2) opts = parseOpts(args[2], api);
        return doRequest("PATCH", url, data, opts, api);
    });

    api.registerFunction("http.head", [api](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("http.head requires a url");
        std::string url = api.toString(args[0]);
        HttpRequestOpts opts;
        if (args.size() > 1) opts = parseOpts(args[1], api);
        return doRequest("HEAD", url, "", opts, api);
    });

    api.registerFunction("http.download", [api](const std::vector<Value> &args) {
        if (args.size() < 2) throw std::runtime_error("http.download requires url and path");
        std::string url = api.toString(args[0]);
        std::string path = api.toString(args[1]);
        int timeout = 60000;
        if (args.size() > 2) {
            auto &t = args[2];
            if (t.isInt()) timeout = static_cast<int>(t.asInt());
            else if (t.isDouble()) timeout = static_cast<int>(t.asDouble());
        }

        ensureCurl();
        CURL *curl = curl_easy_init();
        if (!curl) return Value::makeBool(false);

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);

        FILE *fp = fopen(path.c_str(), "wb");
        if (!fp) {
            curl_easy_cleanup(curl);
            return Value::makeBool(false);
        }

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlFileWriteCb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

        CURLcode res = curl_easy_perform(curl);
        fclose(fp);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK) {
            std::remove(path.c_str());
            return Value::makeBool(false);
        }
        return Value::makeBool(true);
    });

    api.registerFunction("http.upload", [api](const std::vector<Value> &args) {
        if (args.size() < 2) throw std::runtime_error("http.upload requires url and file path");
        std::string url = api.toString(args[0]);
        std::string filePath = api.toString(args[1]);
        HttpRequestOpts opts;
        if (args.size() > 2) opts = parseOpts(args[2], api);
        if (opts.timeout < 60000) opts.timeout = 90000;

        std::ifstream fileCheck(filePath, std::ios::binary);
        if (!fileCheck) {
            return makeResponseObj(0, "", {}, "file not found: " + filePath, api);
        }
        fileCheck.close();

        ensureCurl();
        CURL *curl = curl_easy_init();
        if (!curl) {
            return makeResponseObj(0, "", {}, "failed to initialize curl", api);
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, opts.timeout);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, opts.timeout / 3);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

        if (!opts.verifySsl) {
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        }

        struct curl_slist *headerList = nullptr;
        for (auto &[k, v] : opts.headers) {
            std::string h = k + ": " + v;
            headerList = curl_slist_append(headerList, h.c_str());
        }
        if (headerList) {
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
        }

        std::ifstream file(filePath, std::ios::binary);
        file.seekg(0, std::ios::end);
        auto fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)fileSize);
        curl_easy_setopt(curl, CURLOPT_READDATA, &file);

        std::string responseBody;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);

        std::unordered_map<std::string, std::string> responseHeaders;
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curlHeaderCb);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &responseHeaders);

        CURLcode res = curl_easy_perform(curl);
        file.close();

        int statusCode = 0;
        std::string error;
        if (res != CURLE_OK) {
            error = std::string("curl error: ") + curl_easy_strerror(res);
        } else {
            long code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
            statusCode = static_cast<int>(code);
        }

        curl_slist_free_all(headerList);
        curl_easy_cleanup(curl);

        return makeResponseObj(statusCode, responseBody, responseHeaders, error, api);
    });

    api.registerFunction("http.urlEncode", [api](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("http.urlEncode requires a string");
        std::string str = api.toString(args[0]);
        std::ostringstream oss;
        for (char c : str) {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
                oss << c;
            } else if (c == ' ') {
                oss << '+';
            } else {
                oss << '%' << std::uppercase << std::hex << static_cast<int>(static_cast<unsigned char>(c));
            }
        }
        return api.makeString(oss.str());
    });

    api.registerFunction("http.urlDecode", [api](const std::vector<Value> &args) {
        if (args.empty()) throw std::runtime_error("http.urlDecode requires a string");
        std::string str = api.toString(args[0]);
        std::ostringstream oss;
        for (size_t i = 0; i < str.size(); ++i) {
            if (str[i] == '%' && i + 2 < str.size()) {
                char hex[3] = {str[i+1], str[i+2], 0};
                char *end;
                long val = std::strtol(hex, &end, 16);
                if (end == hex + 2) {
                    oss << static_cast<char>(val);
                    i += 2;
                    continue;
                }
            } else if (str[i] == '+') {
                oss << ' ';
                continue;
            }
            oss << str[i];
        }
        return api.makeString(oss.str());
    });

    api.registerFunction("http.isOnline", [](const std::vector<Value> &) {
        ensureCurl();
        CURL *curl = curl_easy_init();
        if (!curl) return Value::makeBool(false);

        curl_easy_setopt(curl, CURLOPT_URL, "https://www.google.com");
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 5000L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 3000L);
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](void *, size_t s, size_t n, void *) -> size_t { return s * n; });

        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        return Value::makeBool(res == CURLE_OK);
    });

    auto httpObj = api.makeObject();
    api.setField(httpObj, "get", api.makeFunctionRef("http.get"));
    api.setField(httpObj, "post", api.makeFunctionRef("http.post"));
    api.setField(httpObj, "put", api.makeFunctionRef("http.put"));
    api.setField(httpObj, "del", api.makeFunctionRef("http.del"));
    api.setField(httpObj, "patch", api.makeFunctionRef("http.patch"));
    api.setField(httpObj, "head", api.makeFunctionRef("http.head"));
    api.setField(httpObj, "download", api.makeFunctionRef("http.download"));
    api.setField(httpObj, "upload", api.makeFunctionRef("http.upload"));
    api.setField(httpObj, "urlEncode", api.makeFunctionRef("http.urlEncode"));
    api.setField(httpObj, "urlDecode", api.makeFunctionRef("http.urlDecode"));
    api.setField(httpObj, "isOnline", api.makeFunctionRef("http.isOnline"));
    api.setGlobal("http", httpObj);
}

} // namespace havel::stdlib
