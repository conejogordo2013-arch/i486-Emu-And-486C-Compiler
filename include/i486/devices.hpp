#pragma once
#include "i486/bus.hpp"
#include "i486/memory.hpp"
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace i486 {

class PIT8254 : public PortDevice {
public:
    std::uint32_t reload = 11932, counter = 11932;
    std::vector<std::uint16_t> ports() const override;
    int irq() const override { return 0; }
    int pending_irq() const override { return pending_irq_; }
    void clear_pending_irq() override { pending_irq_ = -1; }
    std::uint32_t in_port(std::uint16_t port, std::uint8_t size) override;
    void out_port(std::uint16_t port, std::uint32_t value, std::uint8_t size) override;
    void tick(std::uint32_t cycles) override;
private:
    int pending_irq_ = -1;
};

class VGADevice : public PortDevice {
public:
    explicit VGADevice(Memory& memory);
    enum class Mode { Text80x25, Graphics320x200 };
    Mode mode = Mode::Text80x25;
    std::uint32_t text_base = 0xB8000, gfx_base = 0xA0000;
    std::vector<std::uint8_t> framebuffer;
    std::vector<std::uint16_t> ports() const override;
    std::uint32_t in_port(std::uint16_t port, std::uint8_t size) override;
    void out_port(std::uint16_t port, std::uint32_t value, std::uint8_t size) override;
    void tick(std::uint32_t cycles) override;
    void set_text_mode();
    void set_graphics_mode();
    void refresh();
    std::vector<std::string> render_text() const;
    std::vector<std::uint8_t> render_rgb() const;
private:
    Memory& memory_;
};

class SB16Device : public PortDevice {
public:
    std::uint32_t sample_rate = 44100;
    std::uint8_t channels = 2;
    float volume = 1.0f;
    std::deque<std::uint8_t> pcm_fifo;
    std::deque<float> host_buffer;
    std::vector<std::uint16_t> ports() const override;
    int irq() const override { return 5; }
    int pending_irq() const override { return pending_irq_; }
    void clear_pending_irq() override { pending_irq_ = -1; }
    std::uint32_t in_port(std::uint16_t port, std::uint8_t size) override;
    void out_port(std::uint16_t port, std::uint32_t value, std::uint8_t size) override;
    void tick(std::uint32_t cycles) override;
private:
    int pending_irq_ = -1;
};

class KeyboardDevice : public PortDevice {
public:
    std::deque<std::uint8_t> scancodes;
    void press_scancode(std::uint8_t code);
    std::vector<std::uint16_t> ports() const override;
    int irq() const override { return 1; }
    int pending_irq() const override { return pending_irq_; }
    void clear_pending_irq() override { pending_irq_ = -1; }
    std::uint32_t in_port(std::uint16_t port, std::uint8_t size) override;
    void out_port(std::uint16_t port, std::uint32_t value, std::uint8_t size) override;
    void tick(std::uint32_t cycles) override;
private:
    int pending_irq_ = -1;
};

class BlockStorageDevice : public PortDevice {
public:
    explicit BlockStorageDevice(std::vector<std::uint8_t> image);
    std::uint32_t sector_size = 512, selected_lba = 0, data_index = 0;
    std::vector<std::uint8_t> image;
    std::vector<std::uint8_t> read_sector(std::uint32_t lba) const;
    std::vector<std::uint16_t> ports() const override;
    int irq() const override { return 14; }
    int pending_irq() const override { return pending_irq_; }
    void clear_pending_irq() override { pending_irq_ = -1; }
    std::uint32_t in_port(std::uint16_t port, std::uint8_t size) override;
    void out_port(std::uint16_t port, std::uint32_t value, std::uint8_t size) override;
    void tick(std::uint32_t cycles) override;
private:
    int pending_irq_ = -1;
};

} // namespace i486
