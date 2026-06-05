#pragma once
#include <algorithm>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace i486 {

class PortDevice {
public:
    virtual ~PortDevice() = default;
    virtual std::vector<std::uint16_t> ports() const = 0;
    virtual int irq() const { return -1; }
    virtual int pending_irq() const { return -1; }
    virtual void clear_pending_irq() {}
    virtual std::uint32_t in_port(std::uint16_t port, std::uint8_t size) = 0;
    virtual void out_port(std::uint16_t port, std::uint32_t value, std::uint8_t size) = 0;
    virtual void tick(std::uint32_t cycles) = 0;
};

class IOBus {
public:
    void register_device(const std::shared_ptr<PortDevice>& device);
    std::uint32_t in_port(std::uint16_t port, std::uint8_t size = 1);
    void out_port(std::uint16_t port, std::uint32_t value, std::uint8_t size = 1);
    void raise_irq(std::uint8_t irq);
    int next_irq();
    void tick(std::uint32_t cycles);

private:
    std::vector<std::shared_ptr<PortDevice>> devices_;
    std::unordered_map<std::uint16_t, std::shared_ptr<PortDevice>> port_map_;
    std::vector<std::uint8_t> pending_irqs_;
};

} // namespace i486
