#include "i486/devices.hpp"
#include <algorithm>
#include <ctime>

namespace i486 {


std::vector<std::uint16_t> PIC8259Device::ports() const { return {0x20, 0x21, 0xA0, 0xA1}; }
void PIC8259Device::request_irq(std::uint8_t irq) { if (irq < 8) master_irr |= static_cast<std::uint8_t>(1u << irq); else slave_irr |= static_cast<std::uint8_t>(1u << (irq - 8)); }
int PIC8259Device::pending_irq() const {
    for (int i = 0; i < 8; ++i) if ((master_irr & (1u << i)) && !(master_mask & (1u << i))) { last_irq_ = i; return i; }
    for (int i = 0; i < 8; ++i) if ((slave_irr & (1u << i)) && !(slave_mask & (1u << i))) { last_irq_ = 8 + i; return 8 + i; }
    return -1;
}
void PIC8259Device::clear_pending_irq() { if (last_irq_ < 0) return; if (last_irq_ < 8) { master_irr &= static_cast<std::uint8_t>(~(1u << last_irq_)); master_isr |= static_cast<std::uint8_t>(1u << last_irq_); } else { slave_irr &= static_cast<std::uint8_t>(~(1u << (last_irq_ - 8))); slave_isr |= static_cast<std::uint8_t>(1u << (last_irq_ - 8)); } last_irq_ = -1; }
std::uint32_t PIC8259Device::in_port(std::uint16_t port, std::uint8_t) { if (port == 0x21) return master_mask; if (port == 0xA1) return slave_mask; if (port == 0x20) return master_irr; if (port == 0xA0) return slave_irr; return 0xFF; }
void PIC8259Device::out_port(std::uint16_t port, std::uint32_t value, std::uint8_t) { if (port == 0x21) master_mask = value & 0xFF; else if (port == 0xA1) slave_mask = value & 0xFF; else if (port == 0x20 && (value & 0x20)) master_isr = 0; else if (port == 0xA0 && (value & 0x20)) slave_isr = 0; else if (port == 0x20 && (value & 0x10)) master_base = 0x20; else if (port == 0xA0 && (value & 0x10)) slave_base = 0x28; }
void PIC8259Device::tick(std::uint32_t) {}

std::vector<std::uint16_t> DMA8237Device::ports() const { std::vector<std::uint16_t> p; for (std::uint16_t v = 0x00; v <= 0x0F; ++v) p.push_back(v); for (std::uint16_t v = 0x80; v <= 0x8F; ++v) p.push_back(v); return p; }
std::uint32_t DMA8237Device::in_port(std::uint16_t port, std::uint8_t) { if (port <= 0x0F) { auto& ch = channels[(port / 2) & 3]; return (port & 1) ? (ch.count & 0xFF) : (ch.address & 0xFF); } if (port >= 0x80 && port <= 0x87) return channels[port & 7].page; return 0xFF; }
void DMA8237Device::out_port(std::uint16_t port, std::uint32_t value, std::uint8_t) { if (port <= 0x0F) { auto& ch = channels[(port / 2) & 3]; if (port & 1) ch.count = static_cast<std::uint16_t>((ch.count & 0xFF00u) | (value & 0xFF)); else ch.address = static_cast<std::uint16_t>((ch.address & 0xFF00u) | (value & 0xFF)); } else if (port >= 0x80 && port <= 0x87) channels[port & 7].page = value & 0xFF; }
void DMA8237Device::tick(std::uint32_t) {}

RTCDevice::RTCDevice() { cmos[0x0A] = 0x26; cmos[0x0B] = 0x02; }
std::vector<std::uint16_t> RTCDevice::ports() const { return {0x70, 0x71}; }
std::uint32_t RTCDevice::in_port(std::uint16_t port, std::uint8_t) { if (port == 0x70) return index; if (port != 0x71) return 0xFF; const auto now = std::time(nullptr); const auto* tm = std::gmtime(&now); if (tm) { cmos[0] = tm->tm_sec; cmos[2] = tm->tm_min; cmos[4] = tm->tm_hour; cmos[7] = tm->tm_mday; cmos[8] = tm->tm_mon + 1; cmos[9] = tm->tm_year % 100; } return cmos[index & 0x7F]; }
void RTCDevice::out_port(std::uint16_t port, std::uint32_t value, std::uint8_t) { if (port == 0x70) index = value & 0x7F; else if (port == 0x71) cmos[index & 0x7F] = value & 0xFF; }
void RTCDevice::tick(std::uint32_t cycles) { accumulated_ += cycles; if (accumulated_ > 1000000) { accumulated_ = 0; pending_irq_ = 8; } }

SerialPortDevice::SerialPortDevice(std::uint16_t b, int irq) : base(b), irq_line(irq) {}
std::vector<std::uint16_t> SerialPortDevice::ports() const { std::vector<std::uint16_t> p; for (std::uint16_t v = 0; v < 8; ++v) p.push_back(base + v); return p; }
void SerialPortDevice::receive(std::uint8_t byte) { rx.push_back(byte); lsr |= 0x01; if (ier & 1) pending_irq_ = irq_line; }
std::uint32_t SerialPortDevice::in_port(std::uint16_t port, std::uint8_t) { const auto off = port - base; if (off == 0) { if (rx.empty()) return 0; auto v = rx.front(); rx.pop_front(); if (rx.empty()) lsr &= ~0x01u; return v; } if (off == 1) return ier; if (off == 3) return lcr; if (off == 4) return mcr; if (off == 5) return lsr; return 0; }
void SerialPortDevice::out_port(std::uint16_t port, std::uint32_t value, std::uint8_t) { const auto off = port - base; if (off == 0) { tx.push_back(value & 0xFF); lsr |= 0x20; if (ier & 2) pending_irq_ = irq_line; } else if (off == 1) ier = value & 0x0F; else if (off == 3) lcr = value & 0xFF; else if (off == 4) mcr = value & 0xFF; }
void SerialPortDevice::tick(std::uint32_t) {}

ParallelPortDevice::ParallelPortDevice(std::uint16_t b) : base(b) {}
std::vector<std::uint16_t> ParallelPortDevice::ports() const { return {base, static_cast<std::uint16_t>(base + 1), static_cast<std::uint16_t>(base + 2)}; }
std::uint32_t ParallelPortDevice::in_port(std::uint16_t port, std::uint8_t) { if (port == base) return data; if (port == base + 1) return status; if (port == base + 2) return control; return 0xFF; }
void ParallelPortDevice::out_port(std::uint16_t port, std::uint32_t value, std::uint8_t) { if (port == base) { data = value & 0xFF; output.push_back(data); } else if (port == base + 2) control = value & 0xFF; }
void ParallelPortDevice::tick(std::uint32_t) {}

std::vector<std::uint16_t> PIT8254::ports() const { return {0x40, 0x41, 0x42, 0x43}; }
std::uint32_t PIT8254::in_port(std::uint16_t, std::uint8_t) { return counter & 0xFF; }
void PIT8254::out_port(std::uint16_t port, std::uint32_t value, std::uint8_t) { if (port == 0x40) { reload = std::max(1u, value & 0xFFFFu); counter = reload; } }
void PIT8254::tick(std::uint32_t cycles) { if (cycles >= counter) { counter = reload; pending_irq_ = 0; } else counter -= cycles; }

VGADevice::VGADevice(Memory& memory) : framebuffer(320 * 200, 0), memory_(memory) {}
std::vector<std::uint16_t> VGADevice::ports() const { std::vector<std::uint16_t> p; for (auto v = 0x3C0; v <= 0x3DA; ++v) p.push_back(v); return p; }
std::uint32_t VGADevice::in_port(std::uint16_t port, std::uint8_t) { return port == 0x3DA ? 0x08 : 0; }
void VGADevice::out_port(std::uint16_t port, std::uint32_t value, std::uint8_t) { if (port == 0x3C0 && value == 0x03) set_text_mode(); else if (port == 0x3C0 && value == 0x13) set_graphics_mode(); }
void VGADevice::tick(std::uint32_t) { refresh(); }
void VGADevice::set_text_mode() { mode = Mode::Text80x25; }
void VGADevice::set_graphics_mode() { mode = Mode::Graphics320x200; }
void VGADevice::refresh() { if (mode == Mode::Graphics320x200) framebuffer = memory_.read_bytes_physical(gfx_base, 320 * 200); }
std::vector<std::string> VGADevice::render_text() const {
    std::vector<std::string> rows;
    for (std::uint32_t y = 0; y < 25; ++y) {
        std::string row;
        for (std::uint32_t x = 0; x < 80; ++x) {
            auto c = static_cast<char>(memory_.read_physical(text_base + (y * 80 + x) * 2, 1));
            row.push_back(c ? c : ' ');
        }
        rows.push_back(row);
    }
    return rows;
}
std::vector<std::uint8_t> VGADevice::render_rgb() const {
    if (mode == Mode::Text80x25) {
        const auto rows = render_text(); std::vector<std::uint8_t> out;
        for (const auto& row : rows) { out.insert(out.end(), row.begin(), row.end()); out.push_back('\n'); }
        return out;
    }
    std::vector<std::uint8_t> rgb; rgb.reserve(framebuffer.size() * 3);
    for (auto px : framebuffer) { rgb.push_back(px); rgb.push_back(px); rgb.push_back(px); }
    return rgb;
}

std::vector<std::uint16_t> SB16Device::ports() const { std::vector<std::uint16_t> p; for (auto v = 0x220; v < 0x230; ++v) p.push_back(v); return p; }
std::uint32_t SB16Device::in_port(std::uint16_t port, std::uint8_t) { return port == 0x22E ? (pcm_fifo.empty() ? 0 : 0x80) : 0; }
void SB16Device::out_port(std::uint16_t port, std::uint32_t value, std::uint8_t) { if (port == 0x22C) pcm_fifo.push_back(value & 0xFF); else if (port == 0x224) volume = (value & 0xFF) / 255.0f; else if (port == 0x225) channels = value == 1 ? 1 : 2; }
void SB16Device::tick(std::uint32_t cycles) { while (!pcm_fifo.empty() && cycles--) { host_buffer.push_back(((static_cast<int>(pcm_fifo.front()) - 128) / 128.0f) * volume); pcm_fifo.pop_front(); } if (pcm_fifo.empty() && !host_buffer.empty()) pending_irq_ = 5; }

void KeyboardDevice::press_scancode(std::uint8_t code) { scancodes.push_back(code); pending_irq_ = 1; }
std::vector<std::uint16_t> KeyboardDevice::ports() const { return {0x60, 0x64}; }
std::uint32_t KeyboardDevice::in_port(std::uint16_t port, std::uint8_t) { if (port == 0x60 && !scancodes.empty()) { auto v = scancodes.front(); scancodes.pop_front(); return v; } return port == 0x64 ? (!scancodes.empty()) : 0; }
void KeyboardDevice::out_port(std::uint16_t, std::uint32_t, std::uint8_t) {}
void KeyboardDevice::tick(std::uint32_t) {}

BlockStorageDevice::BlockStorageDevice(std::vector<std::uint8_t> bytes) : image(std::move(bytes)) {}
std::vector<std::uint8_t> BlockStorageDevice::read_sector(std::uint32_t lba) const { std::vector<std::uint8_t> out(sector_size, 0); const auto start = lba * sector_size; for (std::uint32_t i = 0; i < sector_size && start + i < image.size(); ++i) out[i] = image[start + i]; return out; }
std::vector<std::uint16_t> BlockStorageDevice::ports() const { std::vector<std::uint16_t> p; for (auto v = 0x1F0; v < 0x1F8; ++v) p.push_back(v); return p; }
std::uint32_t BlockStorageDevice::in_port(std::uint16_t port, std::uint8_t) { if (port == 0x1F0) { auto sector = read_sector(selected_lba); auto v = sector[data_index]; data_index = (data_index + 1) % sector_size; return v; } return port == 0x1F7 ? 0x40 : 0; }
void BlockStorageDevice::out_port(std::uint16_t port, std::uint32_t value, std::uint8_t) { if (port == 0x1F3) { selected_lba = value; data_index = 0; } else if (port == 0x1F7 && value == 0x20) pending_irq_ = 14; }
void BlockStorageDevice::tick(std::uint32_t) {}

} // namespace i486
