#include "i486/emulator.hpp"
#include "c486cc/compiler.hpp"
#include <cassert>
#include <cstring>
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

void test_i486_integer_extensions() {
    Memory mem;
    IOBus io;
    CPU486 cpu(mem, io);
    cpu.reset();
    std::vector<std::uint8_t> program = {
        0xB8, 0xFE, 0xFF, 0xFF, 0xFF,             // mov eax,-2
        0xBB, 0x03, 0x00, 0x00, 0x00,             // mov ebx,3
        0x0F, 0xAF, 0xC3,                         // imul eax,ebx
        0x0F, 0xBE, 0xC8,                         // movsx ecx,al
        0x0F, 0xB6, 0xD0,                         // movzx edx,al
        0x0F, 0xC8,                               // bswap eax
        0xF7, 0xD8,                               // neg eax
        0x85, 0xC0,                               // test eax,eax
        0x0F, 0x95, 0xC3,                         // setne bl
        0xF4,
    };
    mem.load(0x7C00, program);
    for (int i = 0; i < 10; ++i) cpu.step();
    assert(cpu.regs.ecx == 0xFFFFFFFAu);
    assert(cpu.regs.edx == 0xFAu);
    assert(cpu.regs.get8(3) == 1);
    assert(!cpu.flags.ZF);
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


void test_i486_system_ops_paging_and_fpu() {
    Memory paged;
    paged.paging_enabled = true;
    paged.map_page(0, 2, true);
    paged.map_page(1, 3, true);
    paged.write(0, 0x0FFE, 0xAABBCCDDu, 4);
    assert(paged.read_physical(0x2FFE, 2) == 0xCCDDu);
    assert(paged.read_physical(0x3000, 2) == 0xAABBu);
    assert(paged.page_table[0].accessed && paged.page_table[0].dirty);
    assert(paged.page_table[1].accessed && paged.page_table[1].dirty);
    paged.invalidate_tlb(0x0FFE);

    Memory mem;
    IOBus io;
    CPU486 cpu(mem, io);
    cpu.reset();
    const double nine = 9.0;
    std::uint64_t nine_raw = 0;
    std::memcpy(&nine_raw, &nine, sizeof(nine_raw));
    mem.write_physical(0x3000, nine_raw, 8);
    std::vector<std::uint8_t> program = {
        0x0F, 0xA2,                               // cpuid: deterministic i486-compatible vendor leaf
        0xC7, 0x05, 0x00, 0x20, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, // mov dword [0x2000],5
        0xBB, 0x07, 0x00, 0x00, 0x00,             // mov ebx,7
        0x0F, 0xC1, 0x1D, 0x00, 0x20, 0x00, 0x00, // xadd [0x2000],ebx
        0xB8, 0x0C, 0x00, 0x00, 0x00,             // mov eax,12
        0xBA, 0xAA, 0x00, 0x00, 0x00,             // mov edx,0xaa
        0x0F, 0xB1, 0x15, 0x00, 0x20, 0x00, 0x00, // cmpxchg [0x2000],edx
        0xB9, 0x01, 0x00, 0x00, 0x00,             // mov ecx,1
        0x0F, 0xA3, 0x0D, 0x00, 0x20, 0x00, 0x00, // bt [0x2000],ecx
        0x0F, 0xAB, 0x0D, 0x00, 0x20, 0x00, 0x00, // bts [0x2000],ecx
        0xDD, 0x05, 0x00, 0x30, 0x00, 0x00,       // fld qword [0x3000]
        0xD9, 0xFA,                               // fsqrt
        0xDD, 0x1D, 0x08, 0x30, 0x00, 0x00,       // fstp qword [0x3008]
        0xF4,
    };
    mem.load(0x7C00, program);
    for (int i = 0; i < 20 && !cpu.halted; ++i) cpu.step();
    assert(cpu.halted);
    assert(cpu.flags.ID);
    assert(cpu.regs.ebx == 5);
    assert(mem.read_physical(0x2000, 4) == 0xAAu);
    assert(cpu.flags.CF);
    const auto sqrt_raw = mem.read_physical(0x3008, 8);
    double sqrt_value = 0.0;
    std::memcpy(&sqrt_value, &sqrt_raw, sizeof(sqrt_value));
    assert(sqrt_value == 3.0);
    assert(cpu.fpu.depth() == 0);
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

    auto advanced = compiler.compile_to_asm("int add(int a,int b){ int c = (a + b) % 5; if (c >= 3) return c; return c != 0; }");
    assert(advanced.assembly.find("[ebp+8]") != std::string::npos);
    assert(advanced.assembly.find("mov ecx, 5") != std::string::npos);
    assert(advanced.assembly.find("setge al") != std::string::npos);
    assert(advanced.assembly.find("setne al") != std::string::npos);
}


void test_486cc_for_inline_asm_bitwise_and_devices() {
    c486cc::Compiler486CC compiler;
    auto result = compiler.compile_to_asm(R"c486(
        int main(){
            int x = 0;
            for (int i = 0; i < 4; i = i + 1) { x = x + i; }
            asm { nop; out 0x80, al; }
            return (x & 7) | 8;
        }
    )c486");
    assert(result.assembly.find(".Lfor") != std::string::npos);
    assert(result.assembly.find("out 0x80,al") != std::string::npos || result.assembly.find("out 0x80, al") != std::string::npos);
    assert(result.assembly.find("and eax") != std::string::npos);
    assert(result.assembly.find("or eax") != std::string::npos);

    c486cc::Assembler486 assembler;
    const auto image = assembler.assemble_flat(R"asm(
        bits 32
        global _start
    _start:
        mov eax, 0x12
        mov byte [esp], 0x7f
        cpuid
        bswap eax
        out 0x80, al
        hlt
    )asm");
    Memory mem;
    IOBus io;
    CPU486 cpu(mem, io);
    cpu.reset();
    mem.load(0x7C00, image);
    for (int i = 0; i < 16 && !cpu.halted; ++i) cpu.step();
    assert(cpu.halted);
    assert(mem.read_physical(0x7C00, 1) == 0x7F);

    PC486Emulator emu;
    emu.serial->receive('Z');
    assert(emu.serial->in_port(0x3F8, 1) == 'Z');
    emu.serial->out_port(0x3F8, 'A', 1);
    assert(!emu.serial->tx.empty() && emu.serial->tx.back() == 'A');
    emu.parallel->out_port(0x378, 'P', 1);
    assert(!emu.parallel->output.empty() && emu.parallel->output.back() == 'P');
    emu.rtc->out_port(0x70, 0x0B, 1);
    assert((emu.rtc->in_port(0x71, 1) & 0x02) != 0);
    emu.dma->out_port(0x80, 0x12, 1);
    assert(emu.dma->channels[0].page == 0x12);
    emu.pic->request_irq(4);
    assert(emu.pic->pending_irq() == 4);
}

void test_assembler_and_linker_pipeline() {
    c486cc::Assembler486 assembler;
    const auto image = assembler.assemble_flat(R"asm(
        bits 32
        global _start
    _start:
        mov eax, 1
        add eax, 2
        cmp eax, 3
        jne failed
        mov ebx, 0x12345678
        hlt
    failed:
        mov ebx, 0
        hlt
    )asm");
    Memory mem;
    IOBus io;
    CPU486 cpu(mem, io);
    cpu.reset();
    mem.load(0x7C00, image);
    for (int i = 0; i < 16 && !cpu.halted; ++i) cpu.step();
    assert(cpu.regs.eax == 3);
    assert(cpu.regs.ebx == 0x12345678u);
    assert(cpu.halted);

    c486cc::Toolchain486 toolchain;
    const auto compiled = toolchain.compile_c486_to_asm("int main(){ int x = 7 % 4; return x == 3; }");
    const auto obj = toolchain.assemble(compiled.assembly);
    const auto bin = toolchain.link_flat({obj});
    assert(!bin.empty());
}

int main() {
    test_mov_add_cmp_jump_stack_hlt();
    test_modrm_memory_and_group1();
    test_memory_modes_faults();
    test_sib_8bit_and_full_jcc_matrix();
    test_i486_integer_extensions();
    test_i486_system_ops_paging_and_fpu();
    test_boot_vga_sb16_interrupt();
    test_486cc_compiler_pipeline();
    test_486cc_for_inline_asm_bitwise_and_devices();
    test_assembler_and_linker_pipeline();
    std::cout << "i486 C++ emulator tests OK\n";
    return 0;
}
