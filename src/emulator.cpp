#include "i486/emulator.hpp"

namespace i486 {

PC486Emulator::PC486Emulator() : memory(), io(), cpu(memory, io) {
    pit = std::make_shared<PIT8254>(); vga = std::make_shared<VGADevice>(memory); sb16 = std::make_shared<SB16Device>(); keyboard = std::make_shared<KeyboardDevice>();
    io.register_device(pit); io.register_device(vga); io.register_device(sb16); io.register_device(keyboard);
}
void PC486Emulator::attach_storage(const std::vector<std::uint8_t>& image) { storage = std::make_shared<BlockStorageDevice>(image); io.register_device(storage); }
void PC486Emulator::boot() {
    memory.mode = CpuMode::Real; cpu.reset(true);
    for (std::uint32_t v = 0; v < 256; ++v) { memory.write_physical(v * 4, 0, 2); memory.write_physical(v * 4 + 2, 0, 2); }
    memory.write_physical(0x20 * 4, 0x7E00, 2); memory.write_physical(0x20 * 4 + 2, 0, 2); memory.load(0x7E00, {0xCF});
    if (storage) { memory.load(0x7C00, storage->read_sector(0)); cpu.regs.cs = 0; cpu.regs.eip = 0x7C00; }
}
std::uint32_t PC486Emulator::run_instruction() { const auto cycles = cpu.step(); io.tick(cycles); ticks += cycles; return cycles; }
void PC486Emulator::run(std::uint32_t max_instructions) { for (std::uint32_t i = 0; i < max_instructions && !cpu.halted; ++i) run_instruction(); }

} // namespace i486
