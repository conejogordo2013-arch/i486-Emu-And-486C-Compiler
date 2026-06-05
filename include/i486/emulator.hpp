#pragma once
#include "i486/cpu.hpp"
#include "i486/devices.hpp"
#include <memory>

namespace i486 {

class PC486Emulator {
public:
    Memory memory;
    IOBus io;
    CPU486 cpu;
    std::shared_ptr<PIT8254> pit;
    std::shared_ptr<VGADevice> vga;
    std::shared_ptr<SB16Device> sb16;
    std::shared_ptr<KeyboardDevice> keyboard;
    std::shared_ptr<BlockStorageDevice> storage;
    std::uint64_t ticks = 0;

    PC486Emulator();
    void attach_storage(const std::vector<std::uint8_t>& image);
    void boot();
    std::uint32_t run_instruction();
    void run(std::uint32_t max_instructions);
};

} // namespace i486
