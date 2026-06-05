#include "i486/bus.hpp"

namespace i486 {

void IOBus::register_device(const std::shared_ptr<PortDevice>& device) {
    devices_.push_back(device);
    for (auto port : device->ports()) port_map_[port] = device;
}

std::uint32_t IOBus::in_port(std::uint16_t port, std::uint8_t size) {
    auto it = port_map_.find(port);
    if (it == port_map_.end()) return size >= 4 ? 0xFFFFFFFFu : ((1u << (size * 8)) - 1u);
    return it->second->in_port(port, size);
}

void IOBus::out_port(std::uint16_t port, std::uint32_t value, std::uint8_t size) {
    auto it = port_map_.find(port);
    if (it != port_map_.end()) it->second->out_port(port, value, size);
}

void IOBus::raise_irq(std::uint8_t irq) {
    if (std::find(pending_irqs_.begin(), pending_irqs_.end(), irq) == pending_irqs_.end()) {
        pending_irqs_.push_back(irq);
        std::sort(pending_irqs_.begin(), pending_irqs_.end());
    }
}

int IOBus::next_irq() {
    if (pending_irqs_.empty()) return -1;
    const auto irq = pending_irqs_.front();
    pending_irqs_.erase(pending_irqs_.begin());
    return irq;
}

void IOBus::tick(std::uint32_t cycles) {
    for (auto& device : devices_) {
        device->tick(cycles);
        const auto irq = device->pending_irq();
        if (irq >= 0) {
            raise_irq(static_cast<std::uint8_t>(irq));
            device->clear_pending_irq();
        }
    }
}

} // namespace i486
