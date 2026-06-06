#include "i486/cpu.hpp"
#include <cmath>
#include <cstdlib>

namespace i486 {
namespace {
constexpr std::uint32_t mask_for(std::uint8_t bits) { return bits == 32 ? 0xFFFFFFFFu : ((1u << bits) - 1u); }
bool parity8(std::uint8_t value) { value ^= value >> 4; value &= 0xF; return ((0x6996 >> value) & 1) == 0; }

std::uint32_t sign_extend(std::uint32_t value, std::uint8_t bits) {
    const auto sign = 1u << (bits - 1);
    return (value ^ sign) - sign;
}

std::uint32_t rol(std::uint32_t value, std::uint8_t count, std::uint8_t bits) {
    const auto mask = mask_for(bits);
    count %= bits;
    value &= mask;
    return count == 0 ? value : ((value << count) | (value >> (bits - count))) & mask;
}

std::uint32_t ror(std::uint32_t value, std::uint8_t count, std::uint8_t bits) {
    const auto mask = mask_for(bits);
    count %= bits;
    value &= mask;
    return count == 0 ? value : ((value >> count) | (value << (bits - count))) & mask;
}
}

std::uint32_t EFlags::pack() const {
    std::uint32_t v = 0x2;
    if (CF) v |= 1u << 0; if (PF) v |= 1u << 2; if (AF) v |= 1u << 4; if (ZF) v |= 1u << 6; if (SF) v |= 1u << 7;
    if (TF) v |= 1u << 8; if (IF) v |= 1u << 9; if (DF) v |= 1u << 10; if (OF) v |= 1u << 11;
    v |= (IOPL & 3u) << 12; if (NT) v |= 1u << 14; if (RF) v |= 1u << 16; if (VM) v |= 1u << 17; if (AC) v |= 1u << 18;
    return v;
}
void EFlags::unpack(std::uint32_t v) {
    CF = v & (1u << 0); PF = v & (1u << 2); AF = v & (1u << 4); ZF = v & (1u << 6); SF = v & (1u << 7);
    TF = v & (1u << 8); IF = v & (1u << 9); DF = v & (1u << 10); OF = v & (1u << 11); IOPL = (v >> 12) & 3;
    NT = v & (1u << 14); RF = v & (1u << 16); VM = v & (1u << 17); AC = v & (1u << 18);
}

std::uint32_t& Registers::reg32(std::uint8_t i) {
    switch (i & 7) { case 0: return eax; case 1: return ecx; case 2: return edx; case 3: return ebx; case 4: return esp; case 5: return ebp; case 6: return esi; default: return edi; }
}
const std::uint32_t& Registers::reg32(std::uint8_t i) const { return const_cast<Registers*>(this)->reg32(i); }
std::uint8_t Registers::get8(std::uint8_t i) const {
    const auto r = i & 3;
    const auto value = reg32(r);
    return i < 4 ? static_cast<std::uint8_t>(value) : static_cast<std::uint8_t>(value >> 8);
}
void Registers::set8(std::uint8_t i, std::uint8_t value) {
    auto& r = reg32(i & 3);
    if (i < 4) r = (r & 0xFFFFFF00u) | value;
    else r = (r & 0xFFFF00FFu) | (static_cast<std::uint32_t>(value) << 8);
}
std::uint16_t Registers::get16(std::uint8_t i) const { return static_cast<std::uint16_t>(reg32(i)); }
void Registers::set16(std::uint8_t i, std::uint16_t value) { auto& r = reg32(i); r = (r & 0xFFFF0000u) | value; }

void FPUx87::fld(double value) { if (stack_.size() >= 8) throw CpuFault("x87 stack overflow"); stack_.insert(stack_.begin(), value); }
double FPUx87::fst(bool pop) { if (stack_.empty()) throw CpuFault("x87 stack underflow"); const auto v = stack_.front(); if (pop) stack_.erase(stack_.begin()); return v; }
void FPUx87::fadd() { if (stack_.size() < 2) throw CpuFault("x87 stack underflow"); stack_[1] += stack_[0]; stack_.erase(stack_.begin()); }
void FPUx87::fmul() { if (stack_.size() < 2) throw CpuFault("x87 stack underflow"); stack_[1] *= stack_[0]; stack_.erase(stack_.begin()); }
void FPUx87::fdiv() { if (stack_.size() < 2) throw CpuFault("x87 stack underflow"); stack_[1] /= stack_[0]; stack_.erase(stack_.begin()); }
double FPUx87::st(std::size_t index) const { if (index >= stack_.size()) throw CpuFault("x87 stack underflow"); return stack_[index]; }

CPU486::CPU486(Memory& memory, IOBus& io) : memory_(memory), io_(io) { reset(); }
void CPU486::reset(bool dev_boot) { regs = Registers{}; flags = EFlags{}; halted = false; memory_.mode = CpuMode::Real; regs.cs = dev_boot ? 0 : 0xF000; regs.eip = dev_boot ? 0x7C00 : 0xFFF0; }

std::uint8_t CPU486::fetch8() { auto v = static_cast<std::uint8_t>(memory_.read(regs.cs, regs.eip, 1)); regs.eip++; return v; }
std::uint16_t CPU486::fetch16() { auto v = static_cast<std::uint16_t>(memory_.read(regs.cs, regs.eip, 2)); regs.eip += 2; return v; }
std::uint32_t CPU486::fetch32() { auto v = static_cast<std::uint32_t>(memory_.read(regs.cs, regs.eip, 4)); regs.eip += 4; return v; }
std::uint32_t CPU486::fetch_imm() { return operand32_ ? fetch32() : fetch16(); }
std::uint16_t CPU486::data_segment() const { return has_segment_override_ ? segment_override_ : regs.ds; }

ModRM CPU486::fetch_modrm() {
    ModRM m;
    m.raw = fetch8();
    m.mod = m.raw >> 6;
    m.reg = (m.raw >> 3) & 7;
    m.rm = m.raw & 7;
    m.has_memory = m.mod != 3;
    if (m.has_memory) {
        if (address32_ && m.rm == 4) {
            m.has_sib = true;
            m.sib = fetch8();
            m.scale = m.sib >> 6;
            m.index = (m.sib >> 3) & 7;
            m.base = m.sib & 7;
        }
        if (m.mod == 1) {
            auto d = fetch8();
            m.disp = d & 0x80 ? 0xFFFFFF00u | d : d;
        } else if (m.mod == 2 || (m.mod == 0 && m.rm == 5) || (m.has_sib && m.mod == 0 && m.base == 5)) {
            m.disp = fetch32();
        }
    }
    return m;
}
std::uint32_t CPU486::effective_address(const ModRM& m) const {
    if (m.has_sib) {
        std::uint32_t base_value = (m.mod == 0 && m.base == 5) ? 0 : regs.reg32(m.base);
        std::uint32_t index_value = (m.index == 4) ? 0 : (regs.reg32(m.index) << m.scale);
        return base_value + index_value + m.disp;
    }
    if (m.mod == 0 && m.rm == 5) return m.disp;
    return regs.reg32(m.rm) + m.disp;
}
std::uint32_t CPU486::read_rm32(const ModRM& m) { return m.mod == 3 ? regs.reg32(m.rm) : static_cast<std::uint32_t>(memory_.read(data_segment(), effective_address(m), 4)); }
void CPU486::write_rm32(const ModRM& m, std::uint32_t v) { if (m.mod == 3) regs.reg32(m.rm) = v; else memory_.write(data_segment(), effective_address(m), v, 4); }
std::uint16_t CPU486::read_rm16(const ModRM& m) { return m.mod == 3 ? regs.get16(m.rm) : static_cast<std::uint16_t>(memory_.read(data_segment(), effective_address(m), 2)); }
void CPU486::write_rm16(const ModRM& m, std::uint16_t v) { if (m.mod == 3) regs.set16(m.rm, v); else memory_.write(data_segment(), effective_address(m), v, 2); }
std::uint8_t CPU486::read_rm8(const ModRM& m) { return m.mod == 3 ? regs.get8(m.rm) : static_cast<std::uint8_t>(memory_.read(data_segment(), effective_address(m), 1)); }
void CPU486::write_rm8(const ModRM& m, std::uint8_t v) { if (m.mod == 3) regs.set8(m.rm, v); else memory_.write(data_segment(), effective_address(m), v, 1); }
void CPU486::push32(std::uint32_t v) { regs.esp -= 4; memory_.write(regs.ss, regs.esp, v, 4); }
std::uint32_t CPU486::pop32() { auto v = static_cast<std::uint32_t>(memory_.read(regs.ss, regs.esp, 4)); regs.esp += 4; return v; }
void CPU486::rel_jump(std::uint32_t d, std::uint8_t bits) { if (d & (1u << (bits - 1))) d |= ~mask_for(bits); regs.eip += d; }

void CPU486::update_logic_flags(std::uint32_t r, std::uint8_t bits) { r &= mask_for(bits); flags.ZF = r == 0; flags.SF = r & (1u << (bits - 1)); flags.PF = parity8(r); flags.CF = false; flags.OF = false; }
std::uint32_t CPU486::alu(const std::string& op, std::uint32_t a, std::uint32_t b, std::uint8_t bits) {
    const auto mask = mask_for(bits); std::uint64_t raw = 0;
    if (op == "add") raw = static_cast<std::uint64_t>(a) + b; else if (op == "sub" || op == "cmp") raw = static_cast<std::uint64_t>(a) - b;
    else if (op == "and") raw = a & b; else if (op == "or") raw = a | b; else if (op == "xor") raw = a ^ b; else throw CpuFault("bad alu op");
    const auto r = static_cast<std::uint32_t>(raw) & mask; flags.ZF = r == 0; flags.SF = r & (1u << (bits - 1)); flags.PF = parity8(r); flags.AF = ((a ^ b ^ r) & 0x10) != 0;
    if (op == "add") { flags.CF = raw > mask; flags.OF = (~(a ^ b) & (a ^ r) & (1u << (bits - 1))) != 0; }
    else if (op == "sub" || op == "cmp") { flags.CF = (a & mask) < (b & mask); flags.OF = ((a ^ b) & (a ^ r) & (1u << (bits - 1))) != 0; }
    else { flags.CF = flags.OF = false; }
    return r;
}

std::uint32_t CPU486::step() {
    if (halted) { cycles++; return 1; }
    service_pending_irq(); operand32_ = true; address32_ = true; has_segment_override_ = false;
    for (;;) {
        const auto opcode = fetch8();
        if (opcode == 0x66) { operand32_ = !operand32_; continue; }
        if (opcode == 0x67) { address32_ = !address32_; continue; }
        if (opcode == 0x26 || opcode == 0x2E || opcode == 0x36 || opcode == 0x3E || opcode == 0x64 || opcode == 0x65) {
            segment_override_ = opcode == 0x26 ? regs.es : opcode == 0x2E ? regs.cs : opcode == 0x36 ? regs.ss : opcode == 0x3E ? regs.ds : opcode == 0x64 ? regs.fs : regs.gs;
            has_segment_override_ = true; continue;
        }
        execute_opcode(opcode); break;
    }
    cycles++; return 1;
}

void CPU486::execute_opcode(std::uint8_t op) {
    if (op >= 0xB8 && op <= 0xBF) { regs.reg32(op - 0xB8) = fetch_imm(); return; }
    if (op >= 0x50 && op <= 0x57) { push32(regs.reg32(op - 0x50)); return; }
    if (op >= 0x58 && op <= 0x5F) { regs.reg32(op - 0x58) = pop32(); return; }
    if (op >= 0x40 && op <= 0x47) { auto& r = regs.reg32(op - 0x40); r = alu("add", r, 1, 32); return; }
    if (op >= 0x48 && op <= 0x4F) { auto& r = regs.reg32(op - 0x48); r = alu("sub", r, 1, 32); return; }
    switch (op) {
    case 0x00: { auto m = fetch_modrm(); write_rm8(m, alu("add", read_rm8(m), regs.get8(m.reg), 8)); break; }
    case 0x01: { auto m = fetch_modrm(); write_rm32(m, alu("add", read_rm32(m), regs.reg32(m.reg), 32)); break; }
    case 0x02: { auto m = fetch_modrm(); regs.set8(m.reg, alu("add", regs.get8(m.reg), read_rm8(m), 8)); break; }
    case 0x03: { auto m = fetch_modrm(); regs.reg32(m.reg) = alu("add", regs.reg32(m.reg), read_rm32(m), 32); break; }
    case 0x04: regs.set8(0, alu("add", regs.get8(0), fetch8(), 8)); break;
    case 0x05: regs.eax = alu("add", regs.eax, fetch_imm(), 32); break;
    case 0x08: { auto m = fetch_modrm(); write_rm8(m, alu("or", read_rm8(m), regs.get8(m.reg), 8)); break; }
    case 0x09: { auto m = fetch_modrm(); write_rm32(m, alu("or", read_rm32(m), regs.reg32(m.reg), 32)); break; }
    case 0x0A: { auto m = fetch_modrm(); regs.set8(m.reg, alu("or", regs.get8(m.reg), read_rm8(m), 8)); break; }
    case 0x0B: { auto m = fetch_modrm(); regs.reg32(m.reg) = alu("or", regs.reg32(m.reg), read_rm32(m), 32); break; }
    case 0x0C: regs.set8(0, alu("or", regs.get8(0), fetch8(), 8)); break;
    case 0x0D: regs.eax = alu("or", regs.eax, fetch_imm(), 32); break;
    case 0x20: { auto m = fetch_modrm(); write_rm8(m, alu("and", read_rm8(m), regs.get8(m.reg), 8)); break; }
    case 0x21: { auto m = fetch_modrm(); write_rm32(m, alu("and", read_rm32(m), regs.reg32(m.reg), 32)); break; }
    case 0x22: { auto m = fetch_modrm(); regs.set8(m.reg, alu("and", regs.get8(m.reg), read_rm8(m), 8)); break; }
    case 0x23: { auto m = fetch_modrm(); regs.reg32(m.reg) = alu("and", regs.reg32(m.reg), read_rm32(m), 32); break; }
    case 0x24: regs.set8(0, alu("and", regs.get8(0), fetch8(), 8)); break;
    case 0x25: regs.eax = alu("and", regs.eax, fetch_imm(), 32); break;
    case 0x28: { auto m = fetch_modrm(); write_rm8(m, alu("sub", read_rm8(m), regs.get8(m.reg), 8)); break; }
    case 0x29: { auto m = fetch_modrm(); write_rm32(m, alu("sub", read_rm32(m), regs.reg32(m.reg), 32)); break; }
    case 0x2A: { auto m = fetch_modrm(); regs.set8(m.reg, alu("sub", regs.get8(m.reg), read_rm8(m), 8)); break; }
    case 0x2B: { auto m = fetch_modrm(); regs.reg32(m.reg) = alu("sub", regs.reg32(m.reg), read_rm32(m), 32); break; }
    case 0x2C: regs.set8(0, alu("sub", regs.get8(0), fetch8(), 8)); break;
    case 0x2D: regs.eax = alu("sub", regs.eax, fetch_imm(), 32); break;
    case 0x30: { auto m = fetch_modrm(); write_rm8(m, alu("xor", read_rm8(m), regs.get8(m.reg), 8)); break; }
    case 0x31: { auto m = fetch_modrm(); write_rm32(m, alu("xor", read_rm32(m), regs.reg32(m.reg), 32)); break; }
    case 0x32: { auto m = fetch_modrm(); regs.set8(m.reg, alu("xor", regs.get8(m.reg), read_rm8(m), 8)); break; }
    case 0x33: { auto m = fetch_modrm(); regs.reg32(m.reg) = alu("xor", regs.reg32(m.reg), read_rm32(m), 32); break; }
    case 0x34: regs.set8(0, alu("xor", regs.get8(0), fetch8(), 8)); break;
    case 0x35: regs.eax = alu("xor", regs.eax, fetch_imm(), 32); break;
    case 0x38: { auto m = fetch_modrm(); alu("cmp", read_rm8(m), regs.get8(m.reg), 8); break; }
    case 0x39: { auto m = fetch_modrm(); alu("cmp", read_rm32(m), regs.reg32(m.reg), 32); break; }
    case 0x3A: { auto m = fetch_modrm(); alu("cmp", regs.get8(m.reg), read_rm8(m), 8); break; }
    case 0x3B: { auto m = fetch_modrm(); alu("cmp", regs.reg32(m.reg), read_rm32(m), 32); break; }
    case 0x3C: alu("cmp", regs.get8(0), fetch8(), 8); break;
    case 0x3D: alu("cmp", regs.eax, fetch_imm(), 32); break;
    case 0x84: { auto m = fetch_modrm(); update_logic_flags(read_rm8(m) & regs.get8(m.reg), 8); flags.CF = flags.OF = false; break; }
    case 0x85: { auto m = fetch_modrm(); update_logic_flags(read_rm32(m) & regs.reg32(m.reg), 32); flags.CF = flags.OF = false; break; }
    case 0x86: { auto m = fetch_modrm(); auto lhs = read_rm8(m); auto rhs = regs.get8(m.reg); write_rm8(m, rhs); regs.set8(m.reg, lhs); break; }
    case 0x87: { auto m = fetch_modrm(); auto lhs = read_rm32(m); auto rhs = regs.reg32(m.reg); write_rm32(m, rhs); regs.reg32(m.reg) = lhs; break; }
    case 0x68: push32(fetch_imm()); break;
    case 0x6A: push32(static_cast<std::uint32_t>(static_cast<std::int8_t>(fetch8()))); break;
    case 0x88: { auto m = fetch_modrm(); write_rm8(m, regs.get8(m.reg)); break; }
    case 0x89: { auto m = fetch_modrm(); write_rm32(m, regs.reg32(m.reg)); break; }
    case 0x8A: { auto m = fetch_modrm(); regs.set8(m.reg, read_rm8(m)); break; }
    case 0x8B: { auto m = fetch_modrm(); regs.reg32(m.reg) = read_rm32(m); break; }
    case 0x8D: { auto m = fetch_modrm(); if (!m.has_memory) throw CpuFault("LEA requires memory addressing"); regs.reg32(m.reg) = effective_address(m); break; }
    case 0x90: break;
    case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: { auto& r = regs.reg32(op - 0x90); std::swap(regs.eax, r); break; }
    case 0x99: regs.edx = (regs.eax & 0x80000000u) ? 0xFFFFFFFFu : 0; break;
    case 0x9C: push32(flags.pack()); break; case 0x9D: flags.unpack(pop32()); break;
    case 0xA8: update_logic_flags(regs.get8(0) & fetch8(), 8); flags.CF = flags.OF = false; break;
    case 0xA9: update_logic_flags(regs.eax & fetch_imm(), 32); flags.CF = flags.OF = false; break;
    case 0xA0: regs.set8(0, memory_.read(data_segment(), fetch32(), 1)); break;
    case 0xA1: regs.eax = static_cast<std::uint32_t>(memory_.read(data_segment(), fetch32(), 4)); break;
    case 0xA2: memory_.write(data_segment(), fetch32(), regs.get8(0), 1); break;
    case 0xA3: memory_.write(data_segment(), fetch32(), regs.eax, 4); break;
    case 0xC3: regs.eip = pop32(); break;
    case 0xC9: regs.esp = regs.ebp; regs.ebp = pop32(); break;
    case 0xC6: { auto m = fetch_modrm(); if (m.reg != 0) throw CpuFault("bad C6 group"); write_rm8(m, fetch8()); break; }
    case 0xC7: { auto m = fetch_modrm(); if (m.reg != 0) throw CpuFault("bad C7 group"); write_rm32(m, fetch_imm()); break; }
    case 0xD0: case 0xD1: case 0xD2: case 0xD3: execute_group2(op); break;
    case 0xCD: interrupt(fetch8()); break; case 0xCF: iret(); break;
    case 0xE8: { auto ret = regs.eip + 4; auto d = fetch32(); push32(ret); rel_jump(d, 32); break; }
    case 0xE9: rel_jump(fetch32(), 32); break; case 0xEB: rel_jump(fetch8(), 8); break;
    case 0xE4: regs.set8(0, io_.in_port(fetch8(), 1)); break; case 0xE5: regs.eax = io_.in_port(fetch8(), 4); break;
    case 0xE6: io_.out_port(fetch8(), regs.get8(0), 1); break; case 0xE7: io_.out_port(fetch8(), regs.eax, 4); break;
    case 0xEC: regs.set8(0, io_.in_port(static_cast<std::uint16_t>(regs.edx), 1)); break; case 0xED: regs.eax = io_.in_port(static_cast<std::uint16_t>(regs.edx), 4); break;
    case 0xEE: io_.out_port(static_cast<std::uint16_t>(regs.edx), regs.get8(0), 1); break; case 0xEF: io_.out_port(static_cast<std::uint16_t>(regs.edx), regs.eax, 4); break;
    case 0xF4: halted = true; break; case 0xFA: flags.IF = false; break; case 0xFB: flags.IF = true; break;
    case 0xF6: case 0xF7: execute_group3(op); break;
    case 0x0F: execute_extended(); break; case 0x80: case 0x81: case 0x83: execute_group1(op); break;
    case 0xD9: case 0xDE: execute_fpu(op); break;
    default:
        if (op >= 0x70 && op <= 0x7F) { const auto d = fetch8(); if (condition(op & 0x0F)) rel_jump(d, 8); break; }
        throw CpuFault("unimplemented opcode 0x" + std::to_string(op));
    }
}

void CPU486::execute_group1(std::uint8_t op) {
    auto m = fetch_modrm(); std::uint32_t imm = op == 0x83 ? static_cast<std::int8_t>(fetch8()) : (op == 0x80 ? fetch8() : fetch_imm());
    const char* names[] = {"add", "or", "bad", "bad", "and", "sub", "xor", "cmp"};
    const auto name = std::string(names[m.reg]); if (name == "bad") throw CpuFault("bad group1 op");
    if (op == 0x80) { auto result = alu(name, read_rm8(m), imm, 8); if (name != "cmp") write_rm8(m, result); }
    else { auto result = alu(name, read_rm32(m), imm, 32); if (name != "cmp") write_rm32(m, result); }
}

void CPU486::execute_group2(std::uint8_t op) {
    auto m = fetch_modrm();
    const auto bits = (op == 0xD0 || op == 0xD2) ? 8 : 32;
    auto count = (op == 0xD0 || op == 0xD1) ? 1 : (regs.get8(1) & 0x1F);
    if (count >= bits) count %= bits;
    if (count == 0) return;
    const auto mask = mask_for(bits);
    const auto value = bits == 8 ? read_rm8(m) : read_rm32(m);
    std::uint32_t result = value & mask;
    switch (m.reg) {
    case 0: result = rol(value, count, bits); flags.CF = result & 1u; if (count == 1) flags.OF = ((result >> (bits - 1)) ^ result) & 1u; break;
    case 1: result = ror(value, count, bits); flags.CF = (result >> (bits - 1)) & 1u; if (count == 1) flags.OF = ((result >> (bits - 1)) ^ (result >> (bits - 2))) & 1u; break;
    case 4: { const bool cf = (value >> (bits - count)) & 1u; result = (value << count) & mask; update_logic_flags(result, bits); flags.CF = cf; if (count == 1) flags.OF = ((result >> (bits - 1)) ^ flags.CF) & 1u; break; }
    case 5: { const bool cf = (value >> (count - 1)) & 1u; const bool of = (value >> (bits - 1)) & 1u; result = (value & mask) >> count; update_logic_flags(result, bits); flags.CF = cf; if (count == 1) flags.OF = of; break; }
    case 7: { const bool cf = (value >> (count - 1)) & 1u; result = static_cast<std::uint32_t>(static_cast<std::int32_t>(sign_extend(value & mask, bits)) >> count) & mask; update_logic_flags(result, bits); flags.CF = cf; if (count == 1) flags.OF = false; break; }
    default: throw CpuFault("unsupported group2 rotate through carry");
    }
    if (bits == 8) write_rm8(m, static_cast<std::uint8_t>(result)); else write_rm32(m, result);
}

void CPU486::execute_group3(std::uint8_t op) {
    auto m = fetch_modrm();
    if (op == 0xF6) {
        auto value = read_rm8(m);
        switch (m.reg) {
        case 0: update_logic_flags(value & fetch8(), 8); flags.CF = flags.OF = false; return;
        case 2: write_rm8(m, ~value); return;
        case 3: write_rm8(m, static_cast<std::uint8_t>(alu("sub", 0, value, 8))); return;
        default: throw CpuFault("unsupported 8-bit group3 op");
        }
    }
    auto value = read_rm32(m);
    switch (m.reg) {
    case 0: update_logic_flags(value & fetch_imm(), 32); flags.CF = flags.OF = false; return;
    case 2: write_rm32(m, ~value); return;
    case 3: write_rm32(m, alu("sub", 0, value, 32)); return;
    case 4: { const auto product = static_cast<std::uint64_t>(regs.eax) * value; regs.eax = static_cast<std::uint32_t>(product); regs.edx = static_cast<std::uint32_t>(product >> 32); flags.CF = flags.OF = regs.edx != 0; return; }
    case 5: { const auto product = static_cast<std::int64_t>(static_cast<std::int32_t>(regs.eax)) * static_cast<std::int64_t>(static_cast<std::int32_t>(value)); regs.eax = static_cast<std::uint32_t>(product); regs.edx = static_cast<std::uint32_t>(product >> 32); flags.CF = flags.OF = product != static_cast<std::int64_t>(static_cast<std::int32_t>(regs.eax)); return; }
    case 6: if (value == 0) throw CpuFault("division by zero"); regs.edx = regs.eax % value; regs.eax /= value; return;
    case 7: { if (value == 0) throw CpuFault("division by zero"); const auto dividend = (static_cast<std::uint64_t>(regs.edx) << 32) | regs.eax; const auto q = static_cast<std::int64_t>(dividend) / static_cast<std::int32_t>(value); const auto r = static_cast<std::int64_t>(dividend) % static_cast<std::int32_t>(value); regs.eax = static_cast<std::uint32_t>(q); regs.edx = static_cast<std::uint32_t>(r); return; }
    default: throw CpuFault("bad group3 op");
    }
}

void CPU486::execute_extended() {
    const auto op = fetch8();
    if (op >= 0x80 && op <= 0x8F) {
        auto d = fetch32();
        if (condition(op & 0x0F)) rel_jump(d, 32);
        return;
    }
    if (op >= 0x90 && op <= 0x9F) { auto m = fetch_modrm(); write_rm8(m, condition(op & 0x0F) ? 1 : 0); return; }
    if (op == 0xAF) { auto m = fetch_modrm(); regs.reg32(m.reg) = static_cast<std::uint32_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(regs.reg32(m.reg))) * static_cast<std::int32_t>(read_rm32(m))); update_logic_flags(regs.reg32(m.reg), 32); return; }
    if (op == 0xB6) { auto m = fetch_modrm(); regs.reg32(m.reg) = read_rm8(m); return; }
    if (op == 0xBE) { auto m = fetch_modrm(); regs.reg32(m.reg) = sign_extend(read_rm8(m), 8); return; }
    if (op == 0xB7) { auto m = fetch_modrm(); regs.reg32(m.reg) = read_rm16(m); return; }
    if (op == 0xBF) { auto m = fetch_modrm(); regs.reg32(m.reg) = sign_extend(read_rm16(m), 16); return; }
    if (op >= 0xC8 && op <= 0xCF) { auto& r = regs.reg32(op - 0xC8); r = ((r & 0x000000FFu) << 24) | ((r & 0x0000FF00u) << 8) | ((r & 0x00FF0000u) >> 8) | ((r & 0xFF000000u) >> 24); return; }
    if (op == 0x20) { auto m = fetch_modrm(); regs.reg32(m.rm) = m.reg == 0 ? regs.cr0 : m.reg == 2 ? regs.cr2 : m.reg == 3 ? regs.cr3 : regs.cr4; return; }
    if (op == 0x22) { auto m = fetch_modrm(); auto value = regs.reg32(m.rm); if (m.reg == 0) { regs.cr0 = value; memory_.mode = (regs.cr0 & 1) ? CpuMode::Protected : CpuMode::Real; memory_.paging_enabled = regs.cr0 & 0x80000000u; } else if (m.reg == 2) regs.cr2 = value; else if (m.reg == 3) regs.cr3 = value; else regs.cr4 = value; return; }
    throw CpuFault("unimplemented 0F opcode");
}

void CPU486::execute_fpu(std::uint8_t op) {
    const auto sub = fetch8();
    if (op == 0xD9 && sub == 0xE8) fpu.fld(1.0); else if (op == 0xD9 && sub == 0xEE) fpu.fld(0.0);
    else if (op == 0xDE && sub == 0xC1) fpu.fadd(); else if (op == 0xDE && sub == 0xC9) fpu.fmul(); else if (op == 0xDE && sub == 0xF9) fpu.fdiv();
    else throw CpuFault("unimplemented x87 opcode");
}

bool CPU486::condition(std::uint8_t jcc) const {
    switch (jcc & 0x0F) {
    case 0x0: return flags.OF;
    case 0x1: return !flags.OF;
    case 0x2: return flags.CF;
    case 0x3: return !flags.CF;
    case 0x4: return flags.ZF;
    case 0x5: return !flags.ZF;
    case 0x6: return flags.CF || flags.ZF;
    case 0x7: return !flags.CF && !flags.ZF;
    case 0x8: return flags.SF;
    case 0x9: return !flags.SF;
    case 0xA: return flags.PF;
    case 0xB: return !flags.PF;
    case 0xC: return flags.SF != flags.OF;
    case 0xD: return flags.SF == flags.OF;
    case 0xE: return flags.ZF || (flags.SF != flags.OF);
    case 0xF: return !flags.ZF && (flags.SF == flags.OF);
    default: return false;
    }
}

void CPU486::interrupt(std::uint8_t vector) {
    push32(flags.pack()); push32(regs.cs); push32(regs.eip); flags.IF = false;
    if (memory_.mode == CpuMode::Real) { regs.eip = static_cast<std::uint16_t>(memory_.read_physical(vector * 4, 2)); regs.cs = static_cast<std::uint16_t>(memory_.read_physical(vector * 4 + 2, 2)); }
    else { regs.cs = idt[vector].first; regs.eip = idt[vector].second; }
}
void CPU486::iret() { regs.eip = pop32(); regs.cs = static_cast<std::uint16_t>(pop32()); flags.unpack(pop32()); }
void CPU486::service_pending_irq() { if (!flags.IF) return; const auto irq = io_.next_irq(); if (irq >= 0) interrupt(static_cast<std::uint8_t>(0x20 + irq)); }

} // namespace i486
