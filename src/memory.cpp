#include "i486/memory.hpp"
#include <sstream>

namespace i486 {

void SegmentDescriptor::check(std::uint32_t offset, std::uint32_t size, bool write, bool execute) const {
    if (!present) throw MemoryFault("segment not present");
    if (size == 0 || offset > limit || offset + size - 1 > limit) throw MemoryFault("segmentation limit fault");
    if (write && !writable) throw MemoryFault("write to read-only segment");
    if (execute && !executable) throw MemoryFault("execute from non-code segment");
}

Memory::Memory(std::uint32_t ram_size) : ram_(ram_size, 0) {
    gdt[0] = SegmentDescriptor{0, 0xFFFFFFFFu, true, true, true, 0};
}

void Memory::check_physical(std::uint32_t physical, std::uint32_t size) const {
    if (size == 0 || physical > ram_.size() || physical + size > ram_.size()) {
        std::ostringstream out;
        out << "invalid physical access at 0x" << std::hex << physical << "+" << std::dec << size;
        throw MemoryFault(out.str());
    }
}

void Memory::load(std::uint32_t physical, const std::vector<std::uint8_t>& data) {
    check_physical(physical, static_cast<std::uint32_t>(data.size()));
    std::copy(data.begin(), data.end(), ram_.begin() + physical);
}

std::vector<std::uint8_t> Memory::read_bytes_physical(std::uint32_t physical, std::uint32_t size) const {
    check_physical(physical, size);
    return {ram_.begin() + physical, ram_.begin() + physical + size};
}

std::uint64_t Memory::read_physical(std::uint32_t physical, std::uint8_t size) const {
    check_physical(physical, size);
    std::uint64_t value = 0;
    for (std::uint8_t i = 0; i < size; ++i) value |= static_cast<std::uint64_t>(ram_[physical + i]) << (i * 8);
    return value;
}

void Memory::write_physical(std::uint32_t physical, std::uint64_t value, std::uint8_t size) {
    check_physical(physical, size);
    for (std::uint8_t i = 0; i < size; ++i) ram_[physical + i] = static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF);
}

std::uint32_t Memory::logical_to_linear(std::uint16_t selector, std::uint32_t offset, std::uint32_t size, bool write, bool execute) const {
    if (mode == CpuMode::Real) {
        if (offset + size - 1 > 0xFFFFu) throw MemoryFault("real-mode segment limit exceeded");
        return (static_cast<std::uint32_t>(selector) << 4) + (offset & 0xFFFFu);
    }
    const auto index = selector >> 3;
    const auto it = gdt.find(index);
    if (it == gdt.end()) throw MemoryFault("missing GDT descriptor");
    it->second.check(offset, size, write, execute);
    return it->second.base + offset;
}

std::uint32_t Memory::linear_to_physical(std::uint32_t linear, std::uint32_t size, bool write, bool user) const {
    if (!paging_enabled) {
        check_physical(linear, size);
        return linear;
    }
    const auto first = linear / kPageSize;
    const auto last = (linear + size - 1) / kPageSize;
    if (first != last) throw MemoryFault("cross-page access must be split");
    const auto it = page_table.find(first);
    if (it == page_table.end() || !it->second.present) throw MemoryFault("page not present");
    if (write && !it->second.writable) throw MemoryFault("write to read-only page");
    if (user && !it->second.user) throw MemoryFault("user access to supervisor page");
    const auto physical = it->second.frame * kPageSize + (linear % kPageSize);
    check_physical(physical, size);
    return physical;
}

std::uint32_t Memory::translate(std::uint16_t selector, std::uint32_t offset, std::uint32_t size, bool write, bool execute) const {
    return linear_to_physical(logical_to_linear(selector, offset, size, write, execute), size, write);
}

std::uint64_t Memory::read(std::uint16_t selector, std::uint32_t offset, std::uint8_t size) const {
    return read_physical(translate(selector, offset, size), size);
}

void Memory::write(std::uint16_t selector, std::uint32_t offset, std::uint64_t value, std::uint8_t size) {
    write_physical(translate(selector, offset, size, true), value, size);
}

void Memory::map_page(std::uint32_t linear_page, std::uint32_t physical_frame, bool writable, bool user, bool present) {
    page_table[linear_page] = PageMapping{physical_frame, present, writable, user};
}

} // namespace i486
