/*
 * ServiceRegistry.hpp
 *
 * Central service registry for host services.
 * 
 * This prevents HostBridge from becoming a "god object" that knows about every service.
 * Instead, HostBridge depends on ONE thing: the ServiceRegistry.
 * 
 * Usage:
 *   // Register a service
 *   ServiceRegistry::instance().registerService<IOService>(ioService);
 *   
 *   // Get a service
 *   auto io = ServiceRegistry::instance().get<IOService>();
 *   if (io) io->sendKeys("hello");
 */
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>
#include <typeindex>
#include <stdexcept>

namespace havel::host {

/**
 * ServiceRegistry - Central registry for all host services
 * 
 * Singleton pattern for service discovery and lifecycle management.
 * Services are registered by type and retrieved by type.
 * 
 * Thread-safe for concurrent access.
 */
class ServiceRegistry {
public:
    /**
     * Get singleton instance
     */
    static ServiceRegistry& instance() {
        static ServiceRegistry registry;
        return registry;
    }
    
    /**
     * Register a service by type
     * 
     * @tparam T Service type
     * @param service Service instance (shared_ptr for lifetime management)
     * @throws std::runtime_error if service already registered
     */
    template<typename T>
    void registerService(std::shared_ptr<T> service) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto key = std::type_index(typeid(T));
        if (services_.count(key)) {
            throw std::runtime_error("Service already registered: " + std::string(typeid(T).name()));
        }
        services_[key] = std::static_pointer_cast<void>(service);
    }
    
    /**
     * Get a service by type
     * 
     * @tparam T Service type
     * @return Service instance, or nullptr if not registered
     */
    template<typename T>
    std::shared_ptr<T> get() {
        std::lock_guard<std::mutex> lock(mutex_);
        auto key = std::type_index(typeid(T));
        auto it = services_.find(key);
        if (it == services_.end()) {
            return nullptr;
        }
        return std::static_pointer_cast<T>(it->second);
    }
    
    /**
     * Check if a service is registered
     * 
     * @tparam T Service type
     * @return true if service is registered
     */
    template<typename T>
    bool has() {
        std::lock_guard<std::mutex> lock(mutex_);
        auto key = std::type_index(typeid(T));
        return services_.count(key) > 0;
    }
    
    /**
     * Unregister a service
     * 
     * @tparam T Service type
     */
    template<typename T>
    void unregisterService() {
        std::lock_guard<std::mutex> lock(mutex_);
        auto key = std::type_index(typeid(T));
        services_.erase(key);
    }
    
    /**
     * Clear all registered services
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        services_.clear();
    }
    
    /**
     * Get count of registered services (for debugging)
     */
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return services_.size();
    }
    
    // Non-copyable, non-movable
    ServiceRegistry(const ServiceRegistry&) = delete;
    ServiceRegistry& operator=(const ServiceRegistry&) = delete;
    ServiceRegistry(ServiceRegistry&&) = delete;
    ServiceRegistry& operator=(ServiceRegistry&&) = delete;

private:
    ServiceRegistry() = default;
    ~ServiceRegistry() = default;
    
    mutable std::mutex mutex_;
    std::unordered_map<std::type_index, std::shared_ptr<void>> services_;
};

} // namespace havel::host
