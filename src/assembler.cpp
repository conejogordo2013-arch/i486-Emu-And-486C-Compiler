#include "c486cc/compiler.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_set>

namespace c486cc {
namespace {
std::string trim_copy(std::string s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}
std::string lower_copy(std::string s) { std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); }); return s; }
std::vector<std::string> split_operands(const std::string& text) {
    std::vector<std::string> out; std::string cur; int bracket = 0;
    for (char c : text) {
        if (c == '[') ++bracket; if (c == ']') --bracket;
        if (c == ',' && bracket == 0) { out.push_back(trim_copy(cur)); cur.clear(); } else cur.push_back(c);
    }
    if (!cur.empty() || !text.empty()) out.push_back(trim_copy(cur));
    return out;
}
bool parse_int_token(const std::string& s, std::int64_t& out) {
    auto t = lower_copy(trim_copy(s));
    if (t.empty()) return false;
    try {
        std::size_t pos = 0; int base = 10;
        if (t.size() > 2 && t[0] == '0' && t[1] == 'x') base = 16;
        out = std::stoll(t, &pos, base);
        return pos == t.size();
    } catch (...) { return false; }
}
int reg32_id(const std::string& r) {
    const auto v = lower_copy(trim_copy(r));
    if (v == "eax") return 0; if (v == "ecx") return 1; if (v == "edx") return 2; if (v == "ebx") return 3;
    if (v == "esp") return 4; if (v == "ebp") return 5; if (v == "esi") return 6; if (v == "edi") return 7;
    return -1;
}
int reg8_id(const std::string& r) {
    const auto v = lower_copy(trim_copy(r));
    if (v == "al") return 0; if (v == "cl") return 1; if (v == "dl") return 2; if (v == "bl") return 3;
    if (v == "ah") return 4; if (v == "ch") return 5; if (v == "dh") return 6; if (v == "bh") return 7;
    return -1;
}
void emit8(std::vector<std::uint8_t>& out, std::uint8_t v) { out.push_back(v); }
void emit32(std::vector<std::uint8_t>& out, std::uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF)); }
struct MemOp { bool valid = false; int base = -1; std::int32_t disp = 0; bool absolute = false; };
MemOp parse_mem(std::string op) {
    op = lower_copy(trim_copy(op));
    if (op.rfind("dword", 0) == 0) op = trim_copy(op.substr(5));
    if (op.size() < 3 || op.front() != '[' || op.back() != ']') return {};
    auto inner = trim_copy(op.substr(1, op.size() - 2));
    std::int64_t imm = 0; if (parse_int_token(inner, imm)) return {true, -1, static_cast<std::int32_t>(imm), true};
    for (int r = 0; r < 8; ++r) {
        const char* names[] = {"eax","ecx","edx","ebx","esp","ebp","esi","edi"};
        std::string name = names[r];
        if (inner == name) return {true, r, 0, false};
        if (inner.rfind(name + "+", 0) == 0 || inner.rfind(name + "-", 0) == 0) {
            const bool neg = inner[name.size()] == '-';
            std::int64_t d = 0; if (parse_int_token(inner.substr(name.size() + 1), d)) return {true, r, static_cast<std::int32_t>(neg ? -d : d), false};
        }
    }
    return {};
}
void emit_modrm_mem(std::vector<std::uint8_t>& out, std::uint8_t reg, const MemOp& m) {
    if (m.absolute) { emit8(out, static_cast<std::uint8_t>((0 << 6) | ((reg & 7) << 3) | 5)); emit32(out, m.disp); return; }
    if (m.base == 4) throw CompileError("assembler MVP: [esp] requires SIB and is not emitted yet");
    const bool disp8 = m.disp >= -128 && m.disp <= 127;
    const bool need_disp = m.disp != 0 || m.base == 5;
    const std::uint8_t mod = !need_disp ? 0 : (disp8 ? 1 : 2);
    emit8(out, static_cast<std::uint8_t>((mod << 6) | ((reg & 7) << 3) | (m.base & 7)));
    if (need_disp) { if (disp8) emit8(out, static_cast<std::uint8_t>(m.disp)); else emit32(out, m.disp); }
}
void emit_modrm_reg(std::vector<std::uint8_t>& out, std::uint8_t reg, std::uint8_t rm) { emit8(out, static_cast<std::uint8_t>(0xC0 | ((reg & 7) << 3) | (rm & 7))); }
std::string strip_size(std::string op) { op = trim_copy(op); auto l = lower_copy(op); if (l.rfind("dword", 0) == 0) return trim_copy(op.substr(5)); if (l.rfind("byte", 0) == 0) return trim_copy(op.substr(4)); return op; }
}

ObjectFile Assembler486::assemble(const std::string& source) const {
    ObjectFile obj; obj.sections.push_back({".text", {}, 0}); auto& code = obj.sections.front().data;
    std::unordered_map<std::string, std::uint32_t> labels; std::unordered_set<std::string> globals;
    struct Pending { std::uint32_t offset; std::string symbol; bool relative; };
    std::vector<Pending> pending;
    auto add_symbol = [&](const std::string& name, std::uint32_t address) { labels[name] = address; };
    std::istringstream in(source); std::string line;
    while (std::getline(in, line)) {
        auto semi = line.find(';'); if (semi != std::string::npos) line = line.substr(0, semi);
        line = trim_copy(line); if (line.empty()) continue;
        if (line.back() == ':') { add_symbol(trim_copy(line.substr(0, line.size() - 1)), static_cast<std::uint32_t>(code.size())); continue; }
        auto space = line.find_first_of(" \t"); auto mnemonic = lower_copy(space == std::string::npos ? line : line.substr(0, space)); auto ops = split_operands(space == std::string::npos ? "" : line.substr(space + 1));
        if (mnemonic == "bits" || mnemonic == "section") continue;
        if (mnemonic == "global") { if (!ops.empty()) globals.insert(ops[0]); continue; }
        if (mnemonic == "db" || mnemonic == "dw" || mnemonic == "dd") { for (auto& op : ops) { std::int64_t v = 0; if (!parse_int_token(op, v)) throw CompileError("data directive expects integer"); if (mnemonic == "db") emit8(code, v); else if (mnemonic == "dw") { emit8(code, v); emit8(code, v >> 8); } else emit32(code, v); } continue; }
        if (mnemonic == "ret") { emit8(code, 0xC3); continue; } if (mnemonic == "hlt") { emit8(code, 0xF4); continue; } if (mnemonic == "nop") { emit8(code, 0x90); continue; } if (mnemonic == "cdq") { emit8(code, 0x99); continue; }
        if (mnemonic == "int" && ops.size() == 1) { std::int64_t v=0; if (!parse_int_token(ops[0], v)) throw CompileError("int expects immediate"); emit8(code, 0xCD); emit8(code, v); continue; }
        if ((mnemonic == "jmp" || mnemonic == "je" || mnemonic == "jne" || mnemonic == "call") && ops.size() == 1) { if (mnemonic == "call") emit8(code, 0xE8); else if (mnemonic == "jmp") emit8(code, 0xE9); else { emit8(code, 0x0F); emit8(code, mnemonic == "je" ? 0x84 : 0x85); } pending.push_back({static_cast<std::uint32_t>(code.size()), ops[0], true}); emit32(code, 0); continue; }
        if (mnemonic == "push" && ops.size() == 1) { auto op=strip_size(ops[0]); int r=reg32_id(op); std::int64_t imm=0; if (r>=0) emit8(code, 0x50+r); else if (parse_int_token(op,imm)) { emit8(code,0x68); emit32(code,imm); } else throw CompileError("unsupported push operand"); continue; }
        if (mnemonic == "pop" && ops.size() == 1) { int r=reg32_id(ops[0]); if (r<0) throw CompileError("pop expects r32"); emit8(code,0x58+r); continue; }
        if (ops.size() == 2 && mnemonic == "mov") {
            auto dst = strip_size(ops[0]), src = strip_size(ops[1]); int rd = reg32_id(dst), rs = reg32_id(src); std::int64_t imm = 0; auto md = parse_mem(dst), ms = parse_mem(src);
            if (rd >= 0 && parse_int_token(src, imm)) { emit8(code, 0xB8 + rd); emit32(code, imm); continue; }
            if (rd >= 0 && rs >= 0) { emit8(code, 0x89); emit_modrm_reg(code, rs, rd); continue; }
            if (rd >= 0 && ms.valid) { emit8(code, 0x8B); emit_modrm_mem(code, rd, ms); continue; }
            if (md.valid && rs >= 0) { emit8(code, 0x89); emit_modrm_mem(code, rs, md); continue; }
            if (md.valid && parse_int_token(src, imm)) { emit8(code, 0xC7); emit_modrm_mem(code, 0, md); emit32(code, imm); continue; }
        }
        if (ops.size() == 2 && (mnemonic == "add" || mnemonic == "sub" || mnemonic == "cmp")) {
            auto dst=strip_size(ops[0]), src=strip_size(ops[1]); int rd=reg32_id(dst), rs=reg32_id(src); auto md=parse_mem(dst), ms=parse_mem(src); std::int64_t imm=0;
            const std::uint8_t ext = mnemonic == "add" ? 0 : mnemonic == "sub" ? 5 : 7;
            if (rd == 0 && parse_int_token(src, imm) && mnemonic != "cmp") { emit8(code, mnemonic == "add" ? 0x05 : 0x2D); emit32(code, imm); continue; }
            if (rd >= 0 && parse_int_token(src, imm)) { emit8(code, 0x81); emit_modrm_reg(code, ext, rd); emit32(code, imm); continue; }
            if (md.valid && parse_int_token(src, imm)) { emit8(code, 0x81); emit_modrm_mem(code, ext, md); emit32(code, imm); continue; }
            if (rd >= 0 && rs >= 0) { emit8(code, mnemonic == "add" ? 0x01 : mnemonic == "sub" ? 0x29 : 0x39); emit_modrm_reg(code, rs, rd); continue; }
            if (rd >= 0 && ms.valid) { emit8(code, mnemonic == "add" ? 0x03 : mnemonic == "sub" ? 0x2B : 0x3B); emit_modrm_mem(code, rd, ms); continue; }
            if (md.valid && rs >= 0) { emit8(code, mnemonic == "add" ? 0x01 : mnemonic == "sub" ? 0x29 : 0x39); emit_modrm_mem(code, rs, md); continue; }
        }
        if (ops.size() == 2 && mnemonic == "imul") { int rd=reg32_id(ops[0]); int rs=reg32_id(strip_size(ops[1])); auto ms=parse_mem(ops[1]); if (rd<0) throw CompileError("imul dst must be r32"); emit8(code,0x0F); emit8(code,0xAF); if (rs>=0) emit_modrm_reg(code, rd, rs); else if (ms.valid) emit_modrm_mem(code, rd, ms); else throw CompileError("unsupported imul source"); continue; }
        if (ops.size() == 1 && mnemonic == "idiv") { auto op=strip_size(ops[0]); int r=reg32_id(op); auto m=parse_mem(op); emit8(code,0xF7); if (r>=0) emit_modrm_reg(code,7,r); else if (m.valid) emit_modrm_mem(code,7,m); else throw CompileError("idiv expects r/m32"); continue; }
        if (ops.size() == 1 && mnemonic.rfind("set",0)==0) { int r=reg8_id(ops[0]); if (r<0) throw CompileError("setcc expects r8"); static const std::unordered_map<std::string,std::uint8_t> cc{{"sete",0x94},{"setz",0x94},{"setne",0x95},{"setnz",0x95},{"setl",0x9C},{"setle",0x9E},{"setg",0x9F},{"setge",0x9D}}; auto it=cc.find(mnemonic); if (it==cc.end()) throw CompileError("unsupported setcc"); emit8(code,0x0F); emit8(code,it->second); emit_modrm_reg(code,0,r); continue; }
        if (ops.size() == 2 && mnemonic == "movzx") { int rd=reg32_id(ops[0]); int rs=reg8_id(ops[1]); if (rd<0 || rs<0) throw CompileError("movzx supports r32,r8"); emit8(code,0x0F); emit8(code,0xB6); emit_modrm_reg(code,rd,rs); continue; }
        throw CompileError("unsupported assembler instruction: " + line);
    }
    for (const auto& [name, addr] : labels) obj.symbols.push_back({name, addr, globals.count(name) != 0});
    for (const auto& p : pending) obj.relocations.push_back({p.offset, p.symbol, p.relative ? -4 : 0, 4, p.relative});
    return obj;
}

std::vector<std::uint8_t> Linker486::link_flat(const std::vector<ObjectFile>& objects, std::uint32_t base) const {
    std::vector<std::uint8_t> image; std::unordered_map<std::string, std::uint32_t> symbols; std::vector<std::pair<AsmRelocation,std::uint32_t>> relocs;
    for (const auto& obj : objects) {
        const auto section_base = static_cast<std::uint32_t>(image.size());
        if (!obj.sections.empty()) image.insert(image.end(), obj.sections.front().data.begin(), obj.sections.front().data.end());
        for (const auto& s : obj.symbols) symbols[s.name] = base + section_base + s.address;
        for (const auto& r : obj.relocations) relocs.push_back({r, section_base});
    }
    for (const auto& item : relocs) {
        const auto& r = item.first; const auto section_base = item.second; auto it = symbols.find(r.symbol); if (it == symbols.end()) throw CompileError("unresolved symbol: " + r.symbol);
        const auto patch = section_base + r.offset; std::int64_t value = it->second + r.addend; if (r.relative) value -= (base + patch);
        for (std::uint8_t i = 0; i < r.size; ++i) image.at(patch + i) = static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF);
    }
    return image;
}

std::vector<std::uint8_t> Assembler486::assemble_flat(const std::string& source, std::uint32_t base) const { return Linker486{}.link_flat({assemble(source)}, base); }
CompileResult Toolchain486::compile_c486_to_asm(const std::string& source) const { return Compiler486CC{}.compile_to_asm(source); }
ObjectFile Toolchain486::assemble(const std::string& assembly) const { return Assembler486{}.assemble(assembly); }
std::vector<std::uint8_t> Toolchain486::link_flat(const std::vector<ObjectFile>& objects, std::uint32_t base) const { return Linker486{}.link_flat(objects, base); }
std::vector<std::uint8_t> Toolchain486::build_c486_flat(const std::string& source, std::uint32_t base) const { auto cr = compile_c486_to_asm(source); auto obj = assemble(cr.assembly); return link_flat({obj}, base); }

} // namespace c486cc
