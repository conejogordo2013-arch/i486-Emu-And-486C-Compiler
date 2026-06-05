#include "i486/devices.hpp"
#include <algorithm>

namespace i486 {

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
