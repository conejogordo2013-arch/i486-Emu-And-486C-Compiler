#include "c486cc/compiler.hpp"
#include "i486/emulator.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
std::string read_text(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open input: " + path);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
void write_text(const std::string& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot open output: " + path);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
}
void write_bin(const std::string& path, const std::vector<std::uint8_t>& data) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot open output: " + path);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}
void usage() {
    std::cerr << "usage:\n"
              << "  c486tool cc <in.c486> <out.asm>\n"
              << "  c486tool as <in.asm> <out.bin>\n"
              << "  c486tool build <in.c486> <out.bin>\n"
              << "  c486tool run <in.bin> [cycles]\n";
}
}

int main(int argc, char** argv) {
    try {
        if (argc < 2) { usage(); return 2; }
        const std::string cmd = argv[1];
        c486cc::Toolchain486 toolchain;
        if (cmd == "cc" && argc == 4) {
            write_text(argv[3], toolchain.compile_c486_to_asm(read_text(argv[2])).assembly);
            return 0;
        }
        if (cmd == "as" && argc == 4) {
            write_bin(argv[3], c486cc::Assembler486{}.assemble_flat(read_text(argv[2])));
            return 0;
        }
        if (cmd == "build" && argc == 4) {
            write_bin(argv[3], toolchain.build_c486_flat(read_text(argv[2])));
            return 0;
        }
        if (cmd == "run" && (argc == 3 || argc == 4)) {
            std::ifstream in(argv[2], std::ios::binary);
            if (!in) throw std::runtime_error("cannot open input: " + std::string(argv[2]));
            std::vector<std::uint8_t> image{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
            i486::PC486Emulator emu;
            emu.attach_storage(image);
            emu.boot();
            emu.run(argc == 4 ? static_cast<std::uint64_t>(std::stoull(argv[3])) : 1000000);
            std::cout << "EAX=" << emu.cpu.regs.eax << " EBX=" << emu.cpu.regs.ebx << " ECX=" << emu.cpu.regs.ecx << " EDX=" << emu.cpu.regs.edx << " halted=" << emu.cpu.halted << "\n";
            return 0;
        }
        usage();
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "c486tool: " << e.what() << "\n";
        return 1;
    }
}
