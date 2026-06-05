#include "i486/emulator.hpp"
#include "c486cc/compiler.hpp"
#include <cassert>
#include <iostream>

using namespace i486;

void test_mov_add_cmp_jump_stack_hlt() {
    Memory mem;
    IOBus io;
    CPU486 cpu(mem, io);
    cpu.reset();
    std::vector<std::uint8_t> program = {
        0xB8, 0x01, 0x00, 0x00, 0x00,
        0x05, 0x02, 0x00, 0x00, 0x00,
        0x3D, 0x03, 0x00, 0x00, 0x00,
        0x75, 0x01,
        0x50,
        0x5B,
        0xF4,
    };
    mem.load(0x7C00, program);
    for (int i = 0; i < 7; ++i) cpu.step();
    assert(cpu.regs.eax == 3);
    assert(cpu.regs.ebx == 3);
    assert(cpu.flags.ZF);
    assert(cpu.halted);
}

void test_modrm_memory_and_group1() {
    Memory mem;
    IOBus io;
    CPU486 cpu(mem, io);
    cpu.reset();
    std::vector<std::uint8_t> program = {
        0xB8, 0x78, 0x56, 0x34, 0x12,             // mov eax,0x12345678
        0xA3, 0x00, 0x20, 0x00, 0x00,             // mov [0x2000],eax
        0x8B, 0x1D, 0x00, 0x20, 0x00, 0x00,       // mov ebx,[0x2000]
        0x83, 0xC3, 0x01,                         // add ebx,1
        0xF4,
    };
    mem.load(0x7C00, program);
    for (int i = 0; i < 5; ++i) cpu.step();
    assert(cpu.regs.ebx == 0x12345679u);
    assert(mem.read_physical(0x2000, 4) == 0x12345678u);
}

void test_sib_8bit_and_full_jcc_matrix() {
    Memory mem;
    IOBus io;
    CPU486 cpu(mem, io);
    cpu.reset();
    std::vector<std::uint8_t> program = {
        0xB8, 0x04, 0x00, 0x00, 0x00,             // mov eax,4
        0xBB, 0x00, 0x30, 0x00, 0x00,             // mov ebx,0x3000
        0xC6, 0x04, 0x43, 0x7F,                   // mov byte [ebx+eax*2],0x7f
        0x8A, 0x0C, 0x43,                         // mov cl,[ebx+eax*2]
        0x80, 0xF9, 0x7F,                         // cmp cl,0x7f
        0x0F, 0x85, 0x01, 0x00, 0x00, 0x00,       // jne +1 (not taken)
        0x74, 0x01,                               // je +1 (taken)
        0xF4,                                     // skipped
        0xF4,
    };
    mem.load(0x7C00, program);
    for (int i = 0; i < 8; ++i) cpu.step();
    assert(cpu.regs.get8(1) == 0x7F);
    assert(mem.read_physical(0x3008, 1) == 0x7F);
    assert(cpu.halted);
}

void test_memory_modes_faults() {
    Memory mem;
    mem.write(0, 0x100, 0xAA, 1);
    assert(mem.read_physical(0x100, 1) == 0xAA);
    mem.mode = CpuMode::Protected;
    mem.gdt[1] = SegmentDescriptor{0x1000, 0xFF, true, false, true, 0};
    assert(mem.translate(0x08, 0x10, 1) == 0x1010);
    bool got_fault = false;
    try { mem.write(0x08, 0x10, 1, 1); } catch (const MemoryFault&) { got_fault = true; }
    assert(got_fault);
    mem.paging_enabled = true;
    mem.map_page(2, 3, true);
    assert(mem.linear_to_physical(0x2004, 1) == 0x3004);
}

void test_boot_vga_sb16_interrupt() {
    PC486Emulator emu;
    std::vector<std::uint8_t> boot(512, 0);
    const std::vector<std::uint8_t> code = {0xB8, 0x41, 0, 0, 0, 0xE6, 0x22, 0xF4};
    std::copy(code.begin(), code.end(), boot.begin());
    emu.attach_storage(boot);
    emu.boot();
    emu.run(10);
    assert(emu.cpu.halted);
    emu.memory.write_physical(0xB8000, 'A', 1);
    assert(emu.vga->render_text()[0][0] == 'A');
    emu.io.out_port(0x22C, 128);
    emu.io.tick(1);
    assert(!emu.sb16->host_buffer.empty());

    Memory mem;
    IOBus io;
    CPU486 cpu(mem, io);
    cpu.reset();
    mem.load(0x7C00, {0xCD, 0x10, 0xF4});
    mem.write_physical(0x10 * 4, 0x8000, 2);
    mem.write_physical(0x10 * 4 + 2, 0, 2);
    mem.load(0x8000, {0xCF});
    cpu.step();
    assert(cpu.regs.eip == 0x8000);
    cpu.step();
    assert(cpu.regs.eip == 0x7C02);
}

void test_486cc_compiler_pipeline() {
    c486cc::Lexer lexer("int main(){ int x = 1 + 2; return x; }");
    auto tokens = lexer.all();
    assert(tokens.size() > 8);
    c486cc::Compiler486CC compiler;
    auto result = compiler.compile_to_asm("int main(){ int x = 1 + 2; return x; }");
    assert(result.ast.functions.size() == 1);
    assert(result.ir.functions.size() == 1);
    assert(result.assembly.find("global main") != std::string::npos);
    assert(result.assembly.find("mov eax, 3") != std::string::npos);
    assert(result.assembly.find("ret") != std::string::npos);
}

int main() {
    test_mov_add_cmp_jump_stack_hlt();
    test_modrm_memory_and_group1();
    test_memory_modes_faults();
    test_sib_8bit_and_full_jcc_matrix();
    test_boot_vga_sb16_interrupt();
    test_486cc_compiler_pipeline();
    std::cout << "i486 C++ emulator tests OK\n";
    return 0;
}
