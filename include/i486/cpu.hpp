#pragma once
#include "i486/bus.hpp"
#include "i486/memory.hpp"
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace i486 {

class CpuFault : public std::runtime_error {
public:
    explicit CpuFault(const std::string& message) : std::runtime_error(message) {}
};

struct EFlags {
    bool CF = false, PF = false, AF = false, ZF = false, SF = false;
    bool TF = false, IF = true, DF = false, OF = false;
    std::uint8_t IOPL = 0;
    bool NT = false, RF = false, VM = false, AC = false, ID = false;
    std::uint32_t pack() const;
    void unpack(std::uint32_t value);
};

struct Registers {
    std::uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0, esi = 0, edi = 0, esp = 0x7C00, ebp = 0, eip = 0;
    std::uint16_t cs = 0, ds = 0, es = 0, fs = 0, gs = 0, ss = 0;
    std::uint32_t cr0 = 0, cr2 = 0, cr3 = 0, cr4 = 0;
    std::uint32_t& reg32(std::uint8_t index);
    const std::uint32_t& reg32(std::uint8_t index) const;
    std::uint8_t get8(std::uint8_t index) const;
    void set8(std::uint8_t index, std::uint8_t value);
    std::uint16_t get16(std::uint8_t index) const;
    void set16(std::uint8_t index, std::uint16_t value);
};

class FPUx87 {
public:
    std::uint16_t control = 0x037F;
    std::uint16_t status = 0;
    std::uint16_t tag = 0xFFFF;
    void fld(double value);
    double fst(bool pop = false);
    void fadd(std::size_t index = 1, bool pop = true);
    void fsub(std::size_t index = 1, bool pop = true);
    void fmul(std::size_t index = 1, bool pop = true);
    void fdiv(std::size_t index = 1, bool pop = true);
    void fsqrt();
    int fcom(std::size_t index = 1);
    std::size_t depth() const { return stack_.size(); }
    double st(std::size_t index) const;
private:
    void refresh_status();
    std::vector<double> stack_;
};

struct DescriptorTableCache {
    DescriptorTableRegister gdtr{};
    DescriptorTableRegister idtr{};
    DescriptorTableRegister ldtr{};
    std::uint16_t tr = 0;
};

struct TimingModel486 {
    std::uint32_t base_cycles = 1;
    std::uint32_t memory_wait_states = 0;
    std::uint64_t prefetch_flushes = 0;
    std::uint64_t pipeline_stalls = 0;
    std::uint64_t cache_hits = 0;
    std::uint64_t cache_misses = 0;
};

struct ModRM {
    std::uint8_t raw = 0, mod = 0, reg = 0, rm = 0;
    std::uint8_t sib = 0, scale = 0, index = 0, base = 0;
    std::uint32_t disp = 0;
    bool has_memory = false;
    bool has_sib = false;
};

class CPU486 {
public:
    CPU486(Memory& memory, IOBus& io);
    Registers regs;
    EFlags flags;
    FPUx87 fpu;
    bool halted = false;
    std::uint64_t cycles = 0;
    std::array<std::pair<std::uint16_t, std::uint32_t>, 256> idt{};
    DescriptorTableCache tables{};
    TimingModel486 timing{};

    void reset(bool dev_boot = true);
    std::uint32_t step();
    void interrupt(std::uint8_t vector);
    void iret();
    void service_pending_irq();

private:
    Memory& memory_;
    IOBus& io_;
    bool operand32_ = true;
    bool address32_ = true;
    std::uint16_t segment_override_ = 0;
    bool has_segment_override_ = false;

    std::uint8_t fetch8();
    std::uint16_t fetch16();
    std::uint32_t fetch32();
    std::uint32_t fetch_imm();
    ModRM fetch_modrm();
    std::uint32_t effective_address(const ModRM& modrm) const;
    std::uint16_t data_segment() const;

    std::uint32_t read_rm32(const ModRM& modrm);
    void write_rm32(const ModRM& modrm, std::uint32_t value);
    std::uint16_t read_rm16(const ModRM& modrm);
    void write_rm16(const ModRM& modrm, std::uint16_t value);
    std::uint8_t read_rm8(const ModRM& modrm);
    void write_rm8(const ModRM& modrm, std::uint8_t value);

    void push16(std::uint16_t value);
    std::uint16_t pop16();
    void push32(std::uint32_t value);
    std::uint32_t pop32();
    void push(std::uint32_t value);
    std::uint32_t pop();
    void rel_jump(std::uint32_t displacement, std::uint8_t bits);
    std::uint32_t alu(const std::string& op, std::uint32_t a, std::uint32_t b, std::uint8_t bits);
    void update_logic_flags(std::uint32_t result, std::uint8_t bits);
    void execute_opcode(std::uint8_t opcode);
    void execute_extended();
    void execute_group1(std::uint8_t opcode);
    void execute_group2(std::uint8_t opcode);
    void execute_group3(std::uint8_t opcode);
    void execute_fpu(std::uint8_t opcode);
    void load_table_register(DescriptorTableRegister& reg, const ModRM& modrm);
    void store_table_register(const DescriptorTableRegister& reg, const ModRM& modrm);
    void raise_fault(std::uint8_t vector, std::uint32_t error_code = 0);
    bool condition(std::uint8_t jcc) const;
};

} // namespace i486
