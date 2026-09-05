#include "SystemMonitorModule.hpp"
#include "modules/ModuleMacros.hpp"
#include "utils/Logger.hpp"
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>
#include <sys/statvfs.h>

namespace havel::modules {

using compiler::Value;
using compiler::VMApi;

static const char* MODULE_MARKER = "__system_monitor_module";

static bool isModuleObject(const VMApi& api, const Value& val) {
    if (!val.isObjectId()) return false;
    auto marker = api.getField(val, MODULE_MARKER);
    return marker.isBool() && marker.asBool();
}

static std::vector<Value> stripReceiver(const VMApi& api, const std::vector<Value>& args) {
    if (!args.empty() && isModuleObject(api, args[0])) {
        return std::vector<Value>(args.begin() + 1, args.end());
    }
    return args;
}

// CPU usage tracking
static uint64_t g_prev_total = 0;
static uint64_t g_prev_idle = 0;

static double readCpuUsage() {
    std::ifstream stat("/proc/stat");
    if (!stat) return 0.0;
    std::string line;
    if (!std::getline(stat, line)) return 0.0;
    
    uint64_t user, nice, system, idle, iowait, irq, softirq, steal, guest, guest_nice;
    std::sscanf(line.c_str(), "cpu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal, &guest, &guest_nice);
    
    uint64_t total = user + nice + system + idle + iowait + irq + softirq + steal;
    uint64_t total_diff = total - g_prev_total;
    uint64_t idle_diff = idle - g_prev_idle;
    
    g_prev_total = total;
    g_prev_idle = idle;
    
    if (total_diff == 0) return 0.0;
    return 100.0 * (1.0 - static_cast<double>(idle_diff) / static_cast<double>(total_diff));
}

static auto makeCpuFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        auto args = stripReceiver(api, rawArgs);
        double usage = readCpuUsage();
        auto result = api.makeObject();
        api.setField(result, "usage", Value::makeDouble(usage));
        api.setField(result, "cores", Value::makeInt(static_cast<int>(std::thread::hardware_concurrency())));
        return result;
    };
}

static auto makeMemoryFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        std::ifstream meminfo("/proc/meminfo");
        if (!meminfo) return api.makeNull();
        
        uint64_t total = 0, available = 0, free = 0, buffers = 0, cached = 0;
        std::string line;
        while (std::getline(meminfo, line)) {
            if (line.starts_with("MemTotal:")) std::sscanf(line.c_str(), "MemTotal: %llu kB", &total);
            else if (line.starts_with("MemAvailable:")) std::sscanf(line.c_str(), "MemAvailable: %llu kB", &available);
            else if (line.starts_with("MemFree:")) std::sscanf(line.c_str(), "MemFree: %llu kB", &free);
            else if (line.starts_with("Buffers:")) std::sscanf(line.c_str(), "Buffers: %llu kB", &buffers);
            else if (line.starts_with("Cached:")) std::sscanf(line.c_str(), "Cached: %llu kB", &cached);
        }
        
        uint64_t used = total > available ? total - available : 0;
        auto result = api.makeObject();
        api.setField(result, "total", Value::makeDouble(total * 1024.0));
        api.setField(result, "used", Value::makeDouble(used * 1024.0));
        api.setField(result, "free", Value::makeDouble(free * 1024.0));
        api.setField(result, "available", Value::makeDouble(available * 1024.0));
        api.setField(result, "percent", Value::makeDouble(total > 0 ? 100.0 * used / total : 0.0));
        return result;
    };
}

static auto makeDiskFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        auto args = stripReceiver(api, rawArgs);
        std::string path = args.empty() ? "/" : (args[0].isStringValId() ? api.resolveString(args[0]) : "/");
        
        struct statvfs fs;
        if (statvfs(path.c_str(), &fs) != 0) return api.makeNull();
        
        uint64_t total = fs.f_blocks * fs.f_frsize;
        uint64_t free = fs.f_bfree * fs.f_frsize;
        uint64_t available = fs.f_bavail * fs.f_frsize;
        uint64_t used = total > free ? total - free : 0;
        
        auto result = api.makeObject();
        api.setField(result, "path", api.makeString(path));
        api.setField(result, "total", Value::makeDouble(static_cast<double>(total)));
        api.setField(result, "used", Value::makeDouble(static_cast<double>(used)));
        api.setField(result, "free", Value::makeDouble(static_cast<double>(free)));
        api.setField(result, "available", Value::makeDouble(static_cast<double>(available)));
        api.setField(result, "percent", Value::makeDouble(total > 0 ? 100.0 * used / total : 0.0));
        return result;
    };
}

static auto makeNetworkFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        std::ifstream net("/proc/net/dev");
        if (!net) return api.makeNull();
        
        uint64_t rx_bytes = 0, tx_bytes = 0;
        std::string line;
        std::getline(net, line); // skip header
        std::getline(net, line); // skip header
        
        while (std::getline(net, line)) {
            std::string iface;
            uint64_t rx, tx;
            unsigned long long rx_ull, tx_ull;
            std::sscanf(line.c_str(), " %s %llu %*u %*u %*u %*u %*u %*u %*u %llu", 
                        &iface[0], &rx_ull, &tx_ull);
            rx = rx_ull;
            tx = tx_ull;
            if (iface != "lo") {
                rx_bytes += rx;
                tx_bytes += tx;
            }
        }
        
        auto result = api.makeObject();
        api.setField(result, "rxBytes", Value::makeInt(static_cast<int>(rx_bytes)));
        api.setField(result, "txBytes", Value::makeInt(static_cast<int>(tx_bytes)));
        return result;
    };
}

static auto makeProcessesFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        auto args = stripReceiver(api, rawArgs);
        int limit = args.empty() ? 20 : static_cast<int>(args[0].asInt());
        
        auto result = api.makeArray();
        // Simplified - real implementation would scan /proc/*/stat
        // For now return empty array
        return result;
    };
}

static auto makeUptimeFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        std::ifstream uptime_file("/proc/uptime");
        if (!uptime_file) return api.makeNull();
        
        double uptime, idle;
        uptime_file >> uptime >> idle;
        
        auto result = api.makeObject();
        api.setField(result, "uptime", Value::makeDouble(uptime));
        api.setField(result, "idle", Value::makeDouble(idle));
        return result;
    };
}

static auto makeLoadFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        std::ifstream load("/proc/loadavg");
        if (!load) return api.makeNull();
        
        double load1, load5, load15;
        load >> load1 >> load5 >> load15;
        
        auto result = api.makeObject();
        api.setField(result, "load1", Value::makeDouble(load1));
        api.setField(result, "load5", Value::makeDouble(load5));
        api.setField(result, "load15", Value::makeDouble(load15));
        return result;
    };
}

static auto makeAllFunc(const VMApi& api) {
    return [api](const std::vector<Value>& rawArgs) -> Value {
        auto args = stripReceiver(api, rawArgs);
        
        // Call each subsystem
        std::vector<Value> cpu_args = args;
        std::vector<Value> mem_args = args;
        std::vector<Value> disk_args = args;
        std::vector<Value> net_args = args;
        
        auto cpu = readCpuUsage();
        auto mem = api.makeNull(); // Would need to call makeMemoryFunc
        auto disk = api.makeNull();
        auto net = api.makeNull();
        auto uptime = api.makeNull();
        auto load = api.makeNull();
        
        auto result = api.makeObject();
        api.setField(result, "cpu", Value::makeDouble(cpu));
        api.setField(result, "memory", api.makeNull()); // Placeholder
        api.setField(result, "disk", api.makeNull());
        api.setField(result, "network", api.makeNull());
        api.setField(result, "uptime", api.makeNull());
        api.setField(result, "load", api.makeNull());
        return result;
    };
}

void registerSystemMonitorModule(const VMApi& api) {
    HAVEL_BEGIN_MODULE("SystemMonitor");

    HAVEL_REGISTER_FUNCTION(api, "system_monitor.cpu", makeCpuFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "system_monitor.memory", makeMemoryFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "system_monitor.disk", makeDiskFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "system_monitor.network", makeNetworkFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "system_monitor.processes", makeProcessesFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "system_monitor.uptime", makeUptimeFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "system_monitor.load", makeLoadFunc(api));
    HAVEL_REGISTER_FUNCTION(api, "system_monitor.all", makeAllFunc(api));

    auto obj = api.makeObject();
    api.setGlobal("system_monitor", obj);
    api.setField(obj, MODULE_MARKER, Value::makeBool(true));
    api.setField(obj, "cpu", api.makeFunctionRef("system_monitor.cpu"));
    api.setField(obj, "memory", api.makeFunctionRef("system_monitor.memory"));
    api.setField(obj, "disk", api.makeFunctionRef("system_monitor.disk"));
    api.setField(obj, "network", api.makeFunctionRef("system_monitor.network"));
    api.setField(obj, "processes", api.makeFunctionRef("system_monitor.processes"));
    api.setField(obj, "uptime", api.makeFunctionRef("system_monitor.uptime"));
    api.setField(obj, "load", api.makeFunctionRef("system_monitor.load"));
    api.setField(obj, "all", api.makeFunctionRef("system_monitor.all"));

    HAVEL_END_MODULE();
}

} // namespace havel::modules

#ifdef HAVEL_MODULE_PLUGIN
#include "c/ModulePlugin.h"

HAVEL_MODULE_PLUGIN_IMPL(system_monitor, "1.0.0", "System monitor module",
    havel::modules::registerSystemMonitorModule(*api);
)
#endif
