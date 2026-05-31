#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <typeindex>
#include <stdexcept>
#include <functional>
#include <algorithm>

namespace havel::host {

struct ServiceInfo {
	std::string name;
	std::string group;
	bool registered = false;
};

using ServiceFilter = std::unordered_set<std::string>;

class ServiceRegistry {
public:
	static ServiceRegistry& instance() {
		static ServiceRegistry registry;
		return registry;
	}

	template<typename T>
	void registerService(std::shared_ptr<T> service) {
		std::lock_guard<std::mutex> lock(mutex_);
		auto key = std::type_index(typeid(T));
		if (services_.count(key)) {
			throw std::runtime_error("Service already registered: " + std::string(typeid(T).name()));
		}
		services_[key] = std::static_pointer_cast<void>(service);
		auto nameIt = typeToName_.find(key);
		if (nameIt != typeToName_.end()) {
			for (auto& info : serviceCatalog_) {
				if (info.name == nameIt->second) {
					info.registered = true;
					break;
				}
			}
		}
	}

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

	template<typename T>
	bool has() {
		std::lock_guard<std::mutex> lock(mutex_);
		auto key = std::type_index(typeid(T));
		return services_.count(key) > 0;
	}

	template<typename T>
	void unregisterService() {
		std::lock_guard<std::mutex> lock(mutex_);
		auto key = std::type_index(typeid(T));
		services_.erase(key);
		auto nameIt = typeToName_.find(key);
		if (nameIt != typeToName_.end()) {
			for (auto& info : serviceCatalog_) {
				if (info.name == nameIt->second) {
					info.registered = false;
					break;
				}
			}
		}
	}

	void clear() {
		std::lock_guard<std::mutex> lock(mutex_);
		services_.clear();
		for (auto& info : serviceCatalog_) {
			info.registered = false;
		}
	}

	size_t size() const {
		std::lock_guard<std::mutex> lock(mutex_);
		return services_.size();
	}

	template<typename T>
	void declareService(const std::string& name, const std::string& group = "") {
		std::lock_guard<std::mutex> lock(mutex_);
		auto key = std::type_index(typeid(T));
		typeToName_[key] = name;
		for (auto& info : serviceCatalog_) {
			if (info.name == name) return;
		}
		serviceCatalog_.push_back({name, group, false});
	}

	std::vector<ServiceInfo> listServices() const {
		std::lock_guard<std::mutex> lock(mutex_);
		return serviceCatalog_;
	}

	std::vector<ServiceInfo> listRegistered() const {
		std::lock_guard<std::mutex> lock(mutex_);
		std::vector<ServiceInfo> result;
		for (auto& info : serviceCatalog_) {
			if (info.registered) result.push_back(info);
		}
		return result;
	}

	std::vector<ServiceInfo> listAvailable() const {
		std::lock_guard<std::mutex> lock(mutex_);
		std::vector<ServiceInfo> result;
		for (auto& info : serviceCatalog_) {
			if (!info.registered) result.push_back(info);
		}
		return result;
	}

	bool shouldRegister(const std::string& name,
						const ServiceFilter& include,
						const ServiceFilter& exclude) const {
		if (!exclude.empty() && exclude.count(name)) return false;
		if (!include.empty()) return include.count(name) > 0;
		return true;
	}

	ServiceRegistry(const ServiceRegistry&) = delete;
	ServiceRegistry& operator=(const ServiceRegistry&) = delete;
	ServiceRegistry(ServiceRegistry&&) = delete;
	ServiceRegistry& operator=(ServiceRegistry&&) = delete;

private:
	ServiceRegistry() = default;

public:
	~ServiceRegistry() = default;

private:
	mutable std::mutex mutex_;
	std::unordered_map<std::type_index, std::shared_ptr<void>> services_;
	std::unordered_map<std::type_index, std::string> typeToName_;
	std::vector<ServiceInfo> serviceCatalog_;
};

} // namespace havel::host
