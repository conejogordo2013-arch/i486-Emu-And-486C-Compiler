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
int reg16_id(const std::string& r) {
    const auto v = lower_copy(trim_copy(r));
    if (v == "ax") return 0; if (v == "cx") return 1; if (v == "dx") return 2; if (v == "bx") return 3;
    if (v == "sp") return 4; if (v == "bp") return 5; if (v == "si") return 6; if (v == "di") return 7;
    return -1;
}
int segreg_id(const std::string& r) {
    const auto v = lower_copy(trim_copy(r));
    if (v == "es") return 0; if (v == "cs") return 1; if (v == "ss") return 2; if (v == "ds") return 3; if (v == "fs") return 4; if (v == "gs") return 5;
    return -1;
}
int cr_id(const std::string& r) { const auto v = lower_copy(trim_copy(r)); if (v == "cr0") return 0; if (v == "cr2") return 2; if (v == "cr3") return 3; if (v == "cr4") return 4; return -1; }
int dr_id(const std::string& r) { const auto v = lower_copy(trim_copy(r)); if (v.size() == 3 && v[0] == 'd' && v[1] == 'r' && v[2] >= '0' && v[2] <= '7') return v[2] - '0'; return -1; }
void emit8(std::vector<std::uint8_t>& out, std::uint8_t v) { out.push_back(v); }
void emit16(std::vector<std::uint8_t>& out, std::uint16_t v) { emit8(out, v); emit8(out, v >> 8); }
void emit32(std::vector<std::uint8_t>& out, std::uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF)); }
struct MemOp { bool valid = false; int base = -1; int index = -1; int scale = 1; std::int32_t disp = 0; bool absolute = false; };
MemOp parse_mem(std::string op) {
    op = lower_copy(trim_copy(op));
    if (op.rfind("dword", 0) == 0) op = trim_copy(op.substr(5));
    if (op.rfind("byte", 0) == 0) op = trim_copy(op.substr(4));
    if (op.size() < 3 || op.front() != '[' || op.back() != ']') return {};
    auto inner = trim_copy(op.substr(1, op.size() - 2));
    std::int64_t imm = 0; if (parse_int_token(inner, imm)) return {true, -1, -1, 1, static_cast<std::int32_t>(imm), true};
    for (int r = 0; r < 8; ++r) {
        const char* names[] = {"eax","ecx","edx","ebx","esp","ebp","esi","edi"};
        std::string name = names[r];
        if (inner == name) return {true, r, -1, 1, 0, false};
        if (inner.rfind(name + "+", 0) == 0 || inner.rfind(name + "-", 0) == 0) {
            const bool neg = inner[name.size()] == '-';
            std::int64_t d = 0; if (parse_int_token(inner.substr(name.size() + 1), d)) return {true, r, -1, 1, static_cast<std::int32_t>(neg ? -d : d), false};
        }
    }
    return {};
}
void emit_modrm_mem(std::vector<std::uint8_t>& out, std::uint8_t reg, const MemOp& m) {
    if (m.absolute) { emit8(out, static_cast<std::uint8_t>((0 << 6) | ((reg & 7) << 3) | 5)); emit32(out, m.disp); return; }
    const bool disp8 = m.disp >= -128 && m.disp <= 127;
    const bool need_disp = m.disp != 0 || m.base == 5;
    const std::uint8_t mod = !need_disp ? 0 : (disp8 ? 1 : 2);
    if (m.base == 4) {
        emit8(out, static_cast<std::uint8_t>((mod << 6) | ((reg & 7) << 3) | 4));
        emit8(out, static_cast<std::uint8_t>((0 << 6) | (4 << 3) | 4));
    } else {
        emit8(out, static_cast<std::uint8_t>((mod << 6) | ((reg & 7) << 3) | (m.base & 7)));
    }
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
        if (mnemonic == "align" && ops.size() == 1) { std::int64_t n = 0; if (!parse_int_token(ops[0], n) || n <= 0) throw CompileError("align expects positive integer"); while (code.size() % static_cast<std::size_t>(n)) emit8(code, 0); continue; }
        if (mnemonic == "org" && ops.size() == 1) { std::int64_t n = 0; if (!parse_int_token(ops[0], n) || n < static_cast<std::int64_t>(code.size())) throw CompileError("org expects forward absolute offset"); while (code.size() < static_cast<std::size_t>(n)) emit8(code, 0); continue; }
        if (mnemonic == "db" || mnemonic == "dw" || mnemonic == "dd") { for (auto& op : ops) { auto t = trim_copy(op); if (mnemonic == "db" && t.size() >= 2 && t.front() == '"' && t.back() == '"') { for (std::size_t i = 1; i + 1 < t.size(); ++i) emit8(code, static_cast<std::uint8_t>(t[i])); continue; } std::int64_t v = 0; if (!parse_int_token(t, v)) throw CompileError("data directive expects integer or db string"); if (mnemonic == "db") emit8(code, v); else if (mnemonic == "dw") emit16(code, v); else emit32(code, v); } continue; }
        if (mnemonic == "ret") { if (ops.empty()) emit8(code, 0xC3); else { std::int64_t v=0; if (!parse_int_token(ops[0], v)) throw CompileError("ret imm16 expects integer"); emit8(code,0xC2); emit16(code,v); } continue; }
        static const std::unordered_map<std::string,std::vector<std::uint8_t>> no_ops{{"hlt",{0xF4}},{"nop",{0x90}},{"cdq",{0x99}},{"cwde",{0x98}},{"cli",{0xFA}},{"sti",{0xFB}},{"clc",{0xF8}},{"stc",{0xF9}},{"cmc",{0xF5}},{"cld",{0xFC}},{"std",{0xFD}},{"lahf",{0x9F}},{"sahf",{0x9E}},{"iret",{0xCF}},{"into",{0xCE}},{"cpuid",{0x0F,0xA2}},{"clts",{0x0F,0x06}},{"invd",{0x0F,0x08}},{"wbinvd",{0x0F,0x09}},{"xlat",{0xD7}},{"wait",{0x9B}},{"fwait",{0x9B}},{"movsb",{0xA4}},{"movsw",{0x66,0xA5}},{"movsd",{0xA5}},{"cmpsb",{0xA6}},{"cmpsw",{0x66,0xA7}},{"cmpsd",{0xA7}},{"stosb",{0xAA}},{"stosw",{0x66,0xAB}},{"stosd",{0xAB}},{"lodsb",{0xAC}},{"lodsw",{0x66,0xAD}},{"lodsd",{0xAD}},{"scasb",{0xAE}},{"scasw",{0x66,0xAF}},{"scasd",{0xAF}},
            {"finit",{0xDB,0xE3}},{"fninit",{0xDB,0xE3}},{"fld1",{0xD9,0xE8}},{"fldl2t",{0xD9,0xE9}},{"fldl2e",{0xD9,0xEA}},{"fldpi",{0xD9,0xEB}},{"fldlg2",{0xD9,0xEC}},{"fldln2",{0xD9,0xED}},{"fldz",{0xD9,0xEE}},
            {"fchs",{0xD9,0xE0}},{"fabs",{0xD9,0xE1}},{"ftst",{0xD9,0xE4}},{"fxam",{0xD9,0xE5}},{"f2xm1",{0xD9,0xF0}},{"fyl2x",{0xD9,0xF1}},{"fptan",{0xD9,0xF2}},{"fpatan",{0xD9,0xF3}},{"fyl2xp1",{0xD9,0xF9}},{"fsqrt",{0xD9,0xFA}},{"fsincos",{0xD9,0xFB}},{"fsin",{0xD9,0xFE}},{"fcos",{0xD9,0xFF}},{"fstsw",{0xDF,0xE0}}};
        if (ops.empty()) { auto it = no_ops.find(mnemonic); if (it != no_ops.end()) { for (auto b: it->second) emit8(code,b); continue; } }
        if ((mnemonic == "rep" || mnemonic == "repe" || mnemonic == "repz" || mnemonic == "repne" || mnemonic == "repnz") && ops.size() == 1) { auto inner = lower_copy(ops[0]); auto it = no_ops.find(inner); if (it == no_ops.end()) throw CompileError("REP expects a string instruction mnemonic"); emit8(code, (mnemonic == "repne" || mnemonic == "repnz") ? 0xF2 : 0xF3); for (auto b: it->second) emit8(code,b); continue; }
        if (mnemonic == "int" && ops.size() == 1) { std::int64_t v=0; if (!parse_int_token(ops[0], v)) throw CompileError("int expects immediate"); emit8(code, 0xCD); emit8(code, v); continue; }
        if ((mnemonic == "jmp" || mnemonic == "call" || (mnemonic.size() >= 2 && mnemonic[0] == 'j')) && ops.size() == 1) { static const std::unordered_map<std::string,std::uint8_t> jcc{{"jo",0x80},{"jno",0x81},{"jb",0x82},{"jc",0x82},{"jnae",0x82},{"jae",0x83},{"jnb",0x83},{"jnc",0x83},{"je",0x84},{"jz",0x84},{"jne",0x85},{"jnz",0x85},{"jbe",0x86},{"jna",0x86},{"ja",0x87},{"jnbe",0x87},{"js",0x88},{"jns",0x89},{"jp",0x8A},{"jpe",0x8A},{"jnp",0x8B},{"jpo",0x8B},{"jl",0x8C},{"jnge",0x8C},{"jge",0x8D},{"jnl",0x8D},{"jle",0x8E},{"jng",0x8E},{"jg",0x8F},{"jnle",0x8F}}; if (mnemonic == "call") emit8(code, 0xE8); else if (mnemonic == "jmp") emit8(code, 0xE9); else { auto it = jcc.find(mnemonic); if (it == jcc.end()) throw CompileError("unsupported jcc"); emit8(code, 0x0F); emit8(code, it->second); } pending.push_back({static_cast<std::uint32_t>(code.size()), ops[0], true}); emit32(code, 0); continue; }
        if (mnemonic == "push" && ops.size() == 1) { auto op=strip_size(ops[0]); int r=reg32_id(op); int sr=segreg_id(op); std::int64_t imm=0; if (r>=0) emit8(code, 0x50+r); else if (sr==0) emit8(code,0x06); else if (sr==1) emit8(code,0x0E); else if (sr==2) emit8(code,0x16); else if (sr==3) emit8(code,0x1E); else if (sr==4) { emit8(code,0x0F); emit8(code,0xA0); } else if (sr==5) { emit8(code,0x0F); emit8(code,0xA8); } else if (parse_int_token(op,imm)) { emit8(code,0x68); emit32(code,imm); } else throw CompileError("unsupported push operand"); continue; }
        if (mnemonic == "pop" && ops.size() == 1) { int r=reg32_id(ops[0]); int sr=segreg_id(ops[0]); if (r>=0) emit8(code,0x58+r); else if (sr==0) emit8(code,0x07); else if (sr==2) emit8(code,0x17); else if (sr==3) emit8(code,0x1F); else if (sr==4) { emit8(code,0x0F); emit8(code,0xA1); } else if (sr==5) { emit8(code,0x0F); emit8(code,0xA9); } else throw CompileError("pop expects r32 or poppable segment"); continue; }
        if (ops.size() == 2 && mnemonic == "mov") {
            auto dst = strip_size(ops[0]), src = strip_size(ops[1]); int rd = reg32_id(dst), rs = reg32_id(src); std::int64_t imm = 0; auto md = parse_mem(dst), ms = parse_mem(src);
            const bool byte_dst = lower_copy(ops[0]).rfind("byte", 0) == 0; const bool byte_src = lower_copy(ops[1]).rfind("byte", 0) == 0;
            int r8d = reg8_id(dst), r8s = reg8_id(src); int srd = segreg_id(dst), srs = segreg_id(src); int crd = cr_id(dst), crs = cr_id(src), drd = dr_id(dst), drs = dr_id(src);
            if (srd >= 0 && rs >= 0) { emit8(code, 0x8E); emit_modrm_reg(code, srd, rs); continue; }
            if (rd >= 0 && srs >= 0) { emit8(code, 0x8C); emit_modrm_reg(code, srs, rd); continue; }
            if (rd >= 0 && crs >= 0) { emit8(code,0x0F); emit8(code,0x20); emit_modrm_reg(code,crs,rd); continue; }
            if (crd >= 0 && rs >= 0) { emit8(code,0x0F); emit8(code,0x22); emit_modrm_reg(code,crd,rs); continue; }
            if (rd >= 0 && drs >= 0) { emit8(code,0x0F); emit8(code,0x21); emit_modrm_reg(code,drs,rd); continue; }
            if (drd >= 0 && rs >= 0) { emit8(code,0x0F); emit8(code,0x23); emit_modrm_reg(code,drd,rs); continue; }
            if (r8d >= 0 && parse_int_token(src, imm)) { emit8(code, 0xB0 + r8d); emit8(code, imm); continue; }
            if (r8d >= 0 && r8s >= 0) { emit8(code, 0x88); emit_modrm_reg(code, r8s, r8d); continue; }
            if (r8d >= 0 && ms.valid) { emit8(code, 0x8A); emit_modrm_mem(code, r8d, ms); continue; }
            if (md.valid && r8s >= 0) { emit8(code, 0x88); emit_modrm_mem(code, r8s, md); continue; }
            if ((byte_dst || md.valid) && md.valid && parse_int_token(src, imm) && (byte_dst || byte_src)) { emit8(code, 0xC6); emit_modrm_mem(code, 0, md); emit8(code, imm); continue; }
            if (rd >= 0 && parse_int_token(src, imm)) { emit8(code, 0xB8 + rd); emit32(code, imm); continue; }
            if (rd >= 0 && rs >= 0) { emit8(code, 0x89); emit_modrm_reg(code, rs, rd); continue; }
            if (rd >= 0 && ms.valid) { emit8(code, 0x8B); emit_modrm_mem(code, rd, ms); continue; }
            if (md.valid && rs >= 0) { emit8(code, 0x89); emit_modrm_mem(code, rs, md); continue; }
            if (md.valid && parse_int_token(src, imm)) { emit8(code, 0xC7); emit_modrm_mem(code, 0, md); emit32(code, imm); continue; }
        }
        if (ops.size() == 2 && (mnemonic == "lds" || mnemonic == "les" || mnemonic == "lfs" || mnemonic == "lgs" || mnemonic == "lss")) {
            int rd=reg32_id(ops[0]); auto m=parse_mem(strip_size(ops[1])); if (rd<0 || !m.valid) throw CompileError("far pointer load expects r32, m16:32");
            if (mnemonic == "les" || mnemonic == "lds") emit8(code, mnemonic == "les" ? 0xC4 : 0xC5);
            else { emit8(code,0x0F); emit8(code, mnemonic == "lss" ? 0xB2 : mnemonic == "lfs" ? 0xB4 : 0xB5); }
            emit_modrm_mem(code, rd, m); continue;
        }
        if (ops.size() == 2 && (mnemonic == "add" || mnemonic == "adc" || mnemonic == "sub" || mnemonic == "sbb" || mnemonic == "cmp" || mnemonic == "and" || mnemonic == "or" || mnemonic == "xor")) {
            auto dst=strip_size(ops[0]), src=strip_size(ops[1]); int rd=reg32_id(dst), rs=reg32_id(src); auto md=parse_mem(dst), ms=parse_mem(src); std::int64_t imm=0;
            const std::uint8_t ext = mnemonic == "add" ? 0 : mnemonic == "or" ? 1 : mnemonic == "adc" ? 2 : mnemonic == "sbb" ? 3 : mnemonic == "and" ? 4 : mnemonic == "sub" ? 5 : mnemonic == "xor" ? 6 : 7;
            if (rd == 0 && parse_int_token(src, imm) && mnemonic != "cmp") { emit8(code, mnemonic == "add" ? 0x05 : mnemonic == "or" ? 0x0D : mnemonic == "adc" ? 0x15 : mnemonic == "sbb" ? 0x1D : mnemonic == "and" ? 0x25 : mnemonic == "sub" ? 0x2D : 0x35); emit32(code, imm); continue; }
            if (rd >= 0 && parse_int_token(src, imm)) { emit8(code, 0x81); emit_modrm_reg(code, ext, rd); emit32(code, imm); continue; }
            if (md.valid && parse_int_token(src, imm)) { emit8(code, 0x81); emit_modrm_mem(code, ext, md); emit32(code, imm); continue; }
            if (rd >= 0 && rs >= 0) { emit8(code, mnemonic == "add" ? 0x01 : mnemonic == "or" ? 0x09 : mnemonic == "adc" ? 0x11 : mnemonic == "sbb" ? 0x19 : mnemonic == "and" ? 0x21 : mnemonic == "sub" ? 0x29 : mnemonic == "xor" ? 0x31 : 0x39); emit_modrm_reg(code, rs, rd); continue; }
            if (rd >= 0 && ms.valid) { emit8(code, mnemonic == "add" ? 0x03 : mnemonic == "or" ? 0x0B : mnemonic == "adc" ? 0x13 : mnemonic == "sbb" ? 0x1B : mnemonic == "and" ? 0x23 : mnemonic == "sub" ? 0x2B : mnemonic == "xor" ? 0x33 : 0x3B); emit_modrm_mem(code, rd, ms); continue; }
            if (md.valid && rs >= 0) { emit8(code, mnemonic == "add" ? 0x01 : mnemonic == "or" ? 0x09 : mnemonic == "adc" ? 0x11 : mnemonic == "sbb" ? 0x19 : mnemonic == "and" ? 0x21 : mnemonic == "sub" ? 0x29 : mnemonic == "xor" ? 0x31 : 0x39); emit_modrm_mem(code, rs, md); continue; }
        }
        if (ops.size() == 2 && mnemonic == "imul") { int rd=reg32_id(ops[0]); int rs=reg32_id(strip_size(ops[1])); auto ms=parse_mem(ops[1]); if (rd<0) throw CompileError("imul dst must be r32"); emit8(code,0x0F); emit8(code,0xAF); if (rs>=0) emit_modrm_reg(code, rd, rs); else if (ms.valid) emit_modrm_mem(code, rd, ms); else throw CompileError("unsupported imul source"); continue; }
        if (ops.size() == 1 && mnemonic == "idiv") { auto op=strip_size(ops[0]); int r=reg32_id(op); auto m=parse_mem(op); emit8(code,0xF7); if (r>=0) emit_modrm_reg(code,7,r); else if (m.valid) emit_modrm_mem(code,7,m); else throw CompileError("idiv expects r/m32"); continue; }
        if (ops.size() == 1 && mnemonic.rfind("set",0)==0) { int r=reg8_id(ops[0]); if (r<0) throw CompileError("setcc expects r8"); static const std::unordered_map<std::string,std::uint8_t> cc{{"sete",0x94},{"setz",0x94},{"setne",0x95},{"setnz",0x95},{"setl",0x9C},{"setle",0x9E},{"setg",0x9F},{"setge",0x9D}}; auto it=cc.find(mnemonic); if (it==cc.end()) throw CompileError("unsupported setcc"); emit8(code,0x0F); emit8(code,it->second); emit_modrm_reg(code,0,r); continue; }
        if (ops.size() == 2 && mnemonic == "movzx") { int rd=reg32_id(ops[0]); int rs=reg8_id(ops[1]); if (rd<0 || rs<0) throw CompileError("movzx supports r32,r8"); emit8(code,0x0F); emit8(code,0xB6); emit_modrm_reg(code,rd,rs); continue; }
        if (ops.size() == 2 && mnemonic == "test") { auto dst=strip_size(ops[0]), src=strip_size(ops[1]); int rd=reg32_id(dst), rs=reg32_id(src); auto md=parse_mem(dst); std::int64_t imm=0; if (rd>=0 && rs>=0) { emit8(code,0x85); emit_modrm_reg(code,rs,rd); continue; } if (rd>=0 && parse_int_token(src,imm)) { emit8(code,0xF7); emit_modrm_reg(code,0,rd); emit32(code,imm); continue; } if (md.valid && rs>=0) { emit8(code,0x85); emit_modrm_mem(code,rs,md); continue; } }
        if (ops.size() == 2 && mnemonic == "xchg") { int rd=reg32_id(ops[0]), rs=reg32_id(ops[1]); if (rd>=0 && rs>=0) { emit8(code,0x87); emit_modrm_reg(code,rs,rd); continue; } }
        if (ops.size() == 1 && (mnemonic == "neg" || mnemonic == "not" || mnemonic == "mul" || mnemonic == "div")) { auto op=strip_size(ops[0]); int r=reg32_id(op); auto m=parse_mem(op); const std::uint8_t ext = mnemonic == "not" ? 2 : mnemonic == "neg" ? 3 : mnemonic == "mul" ? 4 : 6; emit8(code,0xF7); if (r>=0) emit_modrm_reg(code,ext,r); else if (m.valid) emit_modrm_mem(code,ext,m); else throw CompileError("group3 expects r/m32"); continue; }
        if (ops.size() == 2 && (mnemonic == "shl" || mnemonic == "sal" || mnemonic == "shr" || mnemonic == "sar" || mnemonic == "rol" || mnemonic == "ror" || mnemonic == "rcl" || mnemonic == "rcr")) { auto dst=strip_size(ops[0]); int r=reg32_id(dst); auto m=parse_mem(dst); std::int64_t imm=0; const std::uint8_t ext = (mnemonic == "rol") ? 0 : (mnemonic == "ror") ? 1 : (mnemonic == "rcl") ? 2 : (mnemonic == "rcr") ? 3 : (mnemonic == "shr") ? 5 : (mnemonic == "sar") ? 7 : 4; if (lower_copy(ops[1]) == "cl") emit8(code,0xD3); else { if (!parse_int_token(ops[1], imm) || imm != 1) throw CompileError("shift supports count 1 or CL"); emit8(code,0xD1); } if (r>=0) emit_modrm_reg(code,ext,r); else if (m.valid) emit_modrm_mem(code,ext,m); else throw CompileError("shift expects r/m32"); continue; }
        if (ops.size() == 1 && mnemonic == "bswap") { int r=reg32_id(ops[0]); if (r<0) throw CompileError("bswap expects r32"); emit8(code,0x0F); emit8(code,0xC8+r); continue; }
        if (ops.size() == 1 && (mnemonic == "lgdt" || mnemonic == "lidt" || mnemonic == "sgdt" || mnemonic == "sidt" || mnemonic == "smsw" || mnemonic == "lmsw" || mnemonic == "invlpg" || mnemonic == "lldt" || mnemonic == "ltr" || mnemonic == "sldt" || mnemonic == "str")) {
            auto op=strip_size(ops[0]); int r=reg32_id(op); auto m=parse_mem(op);
            if (mnemonic == "lldt" || mnemonic == "ltr" || mnemonic == "sldt" || mnemonic == "str") { emit8(code,0x0F); emit8(code,0x00); const std::uint8_t ext = mnemonic == "sldt" ? 0 : mnemonic == "str" ? 1 : mnemonic == "lldt" ? 2 : 3; if (r>=0) emit_modrm_reg(code,ext,r); else if (m.valid) emit_modrm_mem(code,ext,m); else throw CompileError("descriptor selector op expects r/m"); continue; }
            emit8(code,0x0F); emit8(code,0x01); const std::uint8_t ext = mnemonic == "sgdt" ? 0 : mnemonic == "sidt" ? 1 : mnemonic == "lgdt" ? 2 : mnemonic == "lidt" ? 3 : mnemonic == "smsw" ? 4 : mnemonic == "lmsw" ? 6 : 7; if (r>=0) emit_modrm_reg(code,ext,r); else if (m.valid) emit_modrm_mem(code,ext,m); else throw CompileError("system op expects r/m"); continue; }
        if (ops.size() == 1 && (mnemonic == "fld" || mnemonic == "fst" || mnemonic == "fstp" || mnemonic == "fild" || mnemonic == "fist" || mnemonic == "fistp" || mnemonic == "fldcw" || mnemonic == "fstcw" || mnemonic == "fnstcw")) {
            auto op=strip_size(ops[0]); auto m=parse_mem(op); auto low=lower_copy(op);
            if (mnemonic == "fld" && low.rfind("st",0)==0) { int idx = (low.size()>=5 && low[2]=='(') ? low[3]-'0' : 0; emit8(code,0xD9); emit8(code,0xC0 + (idx & 7)); continue; }
            if ((mnemonic == "fst" || mnemonic == "fstp") && low.rfind("st",0)==0) { int idx = (low.size()>=5 && low[2]=='(') ? low[3]-'0' : 0; emit8(code,0xDD); emit8(code,(mnemonic=="fst"?0xD0:0xD8) + (idx & 7)); continue; }
            if (!m.valid) throw CompileError("x87 memory op expects memory operand");
            if (mnemonic == "fld") { emit8(code,0xDD); emit_modrm_mem(code,0,m); continue; }
            if (mnemonic == "fst" || mnemonic == "fstp") { emit8(code,0xDD); emit_modrm_mem(code,mnemonic=="fst"?2:3,m); continue; }
            if (mnemonic == "fild") { emit8(code,0xDB); emit_modrm_mem(code,0,m); continue; }
            if (mnemonic == "fist" || mnemonic == "fistp") { emit8(code,0xDB); emit_modrm_mem(code,mnemonic=="fist"?2:3,m); continue; }
            if (mnemonic == "fldcw") { emit8(code,0xD9); emit_modrm_mem(code,5,m); continue; }
            if (mnemonic == "fstcw" || mnemonic == "fnstcw") { emit8(code,0xD9); emit_modrm_mem(code,7,m); continue; }
        }
        if (ops.size() == 1 && (mnemonic == "fxch" || mnemonic == "ffree" || mnemonic == "fcom")) {
            auto low=lower_copy(strip_size(ops[0])); int idx = (low.rfind("st",0)==0 && low.size()>=5 && low[2]=='(') ? low[3]-'0' : 0;
            emit8(code, mnemonic == "ffree" ? 0xDD : 0xD9); emit8(code, (mnemonic == "fxch" ? 0xC8 : mnemonic == "fcom" ? 0xD0 : 0xC0) + (idx & 7)); continue;
        }
        if (ops.size() == 2 && (mnemonic == "cmpxchg" || mnemonic == "xadd" || mnemonic == "bt" || mnemonic == "bts" || mnemonic == "btr" || mnemonic == "btc")) { auto dst=strip_size(ops[0]), src=strip_size(ops[1]); int rs=reg32_id(src); auto md=parse_mem(dst); int rd=reg32_id(dst); if (rs<0) throw CompileError("i486 op source must be r32"); static const std::unordered_map<std::string,std::uint8_t> opcodes{{"bt",0xA3},{"bts",0xAB},{"btr",0xB3},{"btc",0xBB},{"cmpxchg",0xB1},{"xadd",0xC1}}; emit8(code,0x0F); emit8(code,opcodes.at(mnemonic)); if (rd>=0) emit_modrm_reg(code,rs,rd); else if (md.valid) emit_modrm_mem(code,rs,md); else throw CompileError("i486 op expects r/m32 dst"); continue; }
        if (mnemonic == "out" && ops.size() == 2) { std::int64_t port=0; auto src=lower_copy(strip_size(ops[1])); if (parse_int_token(ops[0], port) && src == "al") { emit8(code,0xE6); emit8(code,port); continue; } if (lower_copy(ops[0]) == "dx" && src == "al") { emit8(code,0xEE); continue; } if (lower_copy(ops[0]) == "dx" && src == "eax") { emit8(code,0xEF); continue; } }
        if (mnemonic == "in" && ops.size() == 2) { auto dst=lower_copy(strip_size(ops[0])); std::int64_t port=0; if (dst == "al" && parse_int_token(ops[1], port)) { emit8(code,0xE4); emit8(code,port); continue; } if (dst == "al" && lower_copy(ops[1]) == "dx") { emit8(code,0xEC); continue; } if (dst == "eax" && lower_copy(ops[1]) == "dx") { emit8(code,0xED); continue; } }
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
