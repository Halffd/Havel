#pragma once
#include "core/window/CompositorBridge.hpp"
#include <memory>
#include <string>

namespace havel {

class CompositorStrategy {
public:
  virtual ~CompositorStrategy() = default;
  virtual CompositorBridge::WindowInfo QueryActiveWindow() = 0;
  virtual CompositorBridge::CompositorType GetType() const = 0;
  virtual std::string GetName() const = 0;
};

class KWinStrategy : public CompositorStrategy {
public:
  CompositorBridge::WindowInfo QueryActiveWindow() override;
  CompositorBridge::CompositorType GetType() const override;
  std::string GetName() const override;
private:
  std::string GetHome() const;
};

class SwayStrategy : public CompositorStrategy {
public:
  CompositorBridge::WindowInfo QueryActiveWindow() override;
  CompositorBridge::CompositorType GetType() const override;
  std::string GetName() const override;
};

class HyprlandStrategy : public CompositorStrategy {
public:
  CompositorBridge::WindowInfo QueryActiveWindow() override;
  CompositorBridge::CompositorType GetType() const override;
  std::string GetName() const override;
};

class RiverStrategy : public CompositorStrategy {
public:
  CompositorBridge::WindowInfo QueryActiveWindow() override;
  CompositorBridge::CompositorType GetType() const override;
  std::string GetName() const override;
};

class WayfireStrategy : public CompositorStrategy {
public:
  CompositorBridge::WindowInfo QueryActiveWindow() override;
  CompositorBridge::CompositorType GetType() const override;
  std::string GetName() const override;
};

std::unique_ptr<CompositorStrategy> CreateCompositorStrategy();

} // namespace havel
