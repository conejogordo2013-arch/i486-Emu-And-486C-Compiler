#include "i486/memory.hpp"
#include <algorithm>
#include <sstream>

namespace i486 {

namespace {
constexpr std::uint32_t page_number(std::uint32_t address) { return address / kPageSize; }
constexpr std::uint32_t page_offset(std::uint32_t address) { return address % kPageSize; }

std::uint32_t effective_limit(std::uint32_t raw_limit, bool granularity4k) {
    return granularity4k ? ((raw_limit << 12) | 0xFFFu) : raw_limit;
}
} // namespace

SegmentDescriptor SegmentDescriptor::from_raw(std::uint64_t raw) {
    SegmentDescriptor d;
    const auto raw_limit = static_cast<std::uint32_t>((raw & 0xFFFFu) | ((raw >> 32) & 0x000F0000u));
    d.base = static_cast<std::uint32_t>(((raw >> 16) & 0xFFFFFFu) | ((raw >> 32) & 0xFF000000u));
    d.granularity4k = (raw & (1ull << 55)) != 0;
    d.limit = effective_limit(raw_limit, d.granularity4k);
    d.present = (raw & (1ull << 47)) != 0;
    d.dpl = static_cast<std::uint8_t>((raw >> 45) & 0x3u);
    d.accessed = (raw & (1ull << 40)) != 0;
    d.writable = (raw & (1ull << 41)) != 0;
    d.expand_down = (raw & (1ull << 42)) != 0;
    d.executable = (raw & (1ull << 43)) != 0;
    d.conforming = d.executable && ((raw & (1ull << 42)) != 0);
    d.default32 = (raw & (1ull << 54)) != 0;
    return d;
}

std::uint64_t SegmentDescriptor::pack() const {
    const auto raw_limit = granularity4k ? (limit >> 12) : limit;
    std::uint64_t raw = 0;
    raw |= raw_limit & 0xFFFFu;
    raw |= static_cast<std::uint64_t>(base & 0xFFFFFFu) << 16;
    raw |= static_cast<std::uint64_t>(accessed) << 40;
    raw |= static_cast<std::uint64_t>(writable) << 41;
    raw |= static_cast<std::uint64_t>(executable ? conforming : expand_down) << 42;
    raw |= static_cast<std::uint64_t>(executable) << 43;
    raw |= 1ull << 44; // code/data descriptor, not a system gate
    raw |= static_cast<std::uint64_t>(dpl & 0x3u) << 45;
    raw |= static_cast<std::uint64_t>(present) << 47;
    raw |= static_cast<std::uint64_t>((raw_limit >> 16) & 0xFu) << 48;
    raw |= static_cast<std::uint64_t>(default32) << 54;
    raw |= static_cast<std::uint64_t>(granularity4k) << 55;
    raw |= static_cast<std::uint64_t>((base >> 24) & 0xFFu) << 56;
    return raw;
}

void SegmentDescriptor::check(std::uint32_t offset, std::uint32_t size, bool write, bool execute) const {
    if (!present) throw MemoryFault("segment not present");
    if (size == 0) throw MemoryFault("zero-sized segment access");
    const auto last = offset + size - 1;
    if (last < offset) throw MemoryFault("segmentation wraparound fault");
    if (expand_down && !executable) {
        if (last <= limit) throw MemoryFault("expand-down segment limit fault");
    } else if (offset > limit || last > limit) {
        throw MemoryFault("segmentation limit fault");
    }
    if (write && !writable) throw MemoryFault("write to read-only segment");
    if (execute && !executable) throw MemoryFault("execute from non-code segment");
}

Memory::Memory(std::uint32_t ram_size) : ram_(ram_size, 0) {
    gdt[0] = SegmentDescriptor{0, 0xFFFFFFFFu, true, true, true, false, false, true, true, true, 0};
}

void Memory::check_physical(std::uint32_t physical, std::uint32_t size) const {
    if (size == 0 || physical > ram_.size() || size > ram_.size() - physical) {
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
        if (size == 0 || offset + size - 1 > 0xFFFFu) throw MemoryFault("real-mode segment limit exceeded");
        return (static_cast<std::uint32_t>(selector) << 4) + (offset & 0xFFFFu);
    }
    const auto index = selector >> 3;
    const auto use_ldt = (selector & 0x4u) != 0;
    const auto& table = use_ldt ? ldt : gdt;
    const auto it = table.find(index);
    if (it == table.end()) throw MemoryFault(use_ldt ? "missing LDT descriptor" : "missing GDT descriptor");
    if (cpl > it->second.dpl && !execute) throw MemoryFault("descriptor privilege fault");
    it->second.check(offset, size, write, execute);
    return it->second.base + offset;
}

std::uint32_t Memory::linear_to_physical(std::uint32_t linear, std::uint32_t size, bool write, bool user) const {
    if (!paging_enabled) {
        check_physical(linear, size);
        return linear;
    }
    if (size == 0) throw MemoryFault("zero-sized paging access");
    const auto first = page_number(linear);
    const auto last = page_number(linear + size - 1);
    if (first != last) throw MemoryFault("cross-page access must be split");
    const auto tlb_it = tlb.find(first);
    if (tlb_it != tlb.end()) {
        const auto& entry = tlb_it->second;
        if (write && !entry.writable) throw PageFault(linear, 0x3u | (user ? 0x4u : 0), "TLB write to read-only page");
        if (user && !entry.user) throw PageFault(linear, 0x5u, "TLB user access to supervisor page");
        const auto physical = entry.physical_frame * kPageSize + page_offset(linear);
        check_physical(physical, size);
        return physical;
    }
    auto it = page_table.find(first);
    if (it == page_table.end() || !it->second.present) throw PageFault(linear, user ? 0x4u : 0, "page not present");
    if (write && !it->second.writable) throw PageFault(linear, 0x3u | (user ? 0x4u : 0), "write to read-only page");
    if (user && !it->second.user) throw PageFault(linear, 0x5u, "user access to supervisor page");
    it->second.accessed = true;
    if (write) it->second.dirty = true;
    tlb[first] = TlbEntry{it->second.frame, it->second.writable, it->second.user, false};
    const auto physical = it->second.frame * kPageSize + page_offset(linear);
    check_physical(physical, size);
    return physical;
}

std::uint32_t Memory::translate(std::uint16_t selector, std::uint32_t offset, std::uint32_t size, bool write, bool execute) const {
    return linear_to_physical(logical_to_linear(selector, offset, size, write, execute), size, write, cpl == 3);
}

std::uint64_t Memory::read(std::uint16_t selector, std::uint32_t offset, std::uint8_t size) const {
    const auto linear = logical_to_linear(selector, offset, size, false, false);
    std::uint64_t value = 0;
    std::uint32_t consumed = 0;
    while (consumed < size) {
        const auto chunk = std::min<std::uint32_t>(size - consumed, kPageSize - page_offset(linear + consumed));
        const auto physical = linear_to_physical(linear + consumed, chunk, false, cpl == 3);
        for (std::uint32_t i = 0; i < chunk; ++i) {
            value |= static_cast<std::uint64_t>(ram_[physical + i]) << ((consumed + i) * 8);
        }
        consumed += chunk;
    }
    return value;
}

void Memory::write(std::uint16_t selector, std::uint32_t offset, std::uint64_t value, std::uint8_t size) {
    const auto linear = logical_to_linear(selector, offset, size, true, false);
    std::uint32_t consumed = 0;
    while (consumed < size) {
        const auto chunk = std::min<std::uint32_t>(size - consumed, kPageSize - page_offset(linear + consumed));
        const auto physical = linear_to_physical(linear + consumed, chunk, true, cpl == 3);
        for (std::uint32_t i = 0; i < chunk; ++i) {
            ram_[physical + i] = static_cast<std::uint8_t>((value >> ((consumed + i) * 8)) & 0xFF);
        }
        consumed += chunk;
    }
}

void Memory::map_page(std::uint32_t linear_page, std::uint32_t physical_frame, bool writable, bool user, bool present) {
    page_table[linear_page] = PageMapping{physical_frame, present, writable, user};
    tlb.erase(linear_page);
}

void Memory::flush_tlb() { tlb.clear(); }
void Memory::invalidate_tlb(std::uint32_t linear_address) { tlb.erase(page_number(linear_address)); }

} // namespace i486
