#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace i486 {

constexpr std::uint32_t kRamSize = 64u * 1024u * 1024u;
constexpr std::uint32_t kPageSize = 4096u;

enum class CpuMode { Real, Protected };

class MemoryFault : public std::runtime_error {
public:
    explicit MemoryFault(const std::string& message) : std::runtime_error(message) {}
};

struct SegmentDescriptor {
    std::uint32_t base = 0;
    std::uint32_t limit = 0xFFFF;
    bool present = true;
    bool writable = true;
    bool executable = false;
    std::uint8_t dpl = 0;

    void check(std::uint32_t offset, std::uint32_t size, bool write, bool execute) const;
};

struct PageMapping {
    std::uint32_t frame = 0;
    bool present = true;
    bool writable = true;
    bool user = false;
};

class Memory {
public:
    explicit Memory(std::uint32_t ram_size = kRamSize);

    CpuMode mode = CpuMode::Real;
    bool paging_enabled = false;
    std::unordered_map<std::uint32_t, SegmentDescriptor> gdt;
    std::unordered_map<std::uint32_t, PageMapping> page_table;

    std::uint32_t size() const { return static_cast<std::uint32_t>(ram_.size()); }
    std::uint8_t* raw() { return ram_.data(); }
    const std::uint8_t* raw() const { return ram_.data(); }

    void load(std::uint32_t physical, const std::vector<std::uint8_t>& data);
    std::vector<std::uint8_t> read_bytes_physical(std::uint32_t physical, std::uint32_t size) const;

    std::uint64_t read_physical(std::uint32_t physical, std::uint8_t size) const;
    void write_physical(std::uint32_t physical, std::uint64_t value, std::uint8_t size);

    std::uint32_t logical_to_linear(std::uint16_t selector, std::uint32_t offset, std::uint32_t size,
                                    bool write = false, bool execute = false) const;
    std::uint32_t linear_to_physical(std::uint32_t linear, std::uint32_t size, bool write = false,
                                     bool user = false) const;
    std::uint32_t translate(std::uint16_t selector, std::uint32_t offset, std::uint32_t size,
                            bool write = false, bool execute = false) const;

    std::uint64_t read(std::uint16_t selector, std::uint32_t offset, std::uint8_t size) const;
    void write(std::uint16_t selector, std::uint32_t offset, std::uint64_t value, std::uint8_t size);
    void map_page(std::uint32_t linear_page, std::uint32_t physical_frame, bool writable = true,
                  bool user = false, bool present = true);

private:
    std::vector<std::uint8_t> ram_;
    void check_physical(std::uint32_t physical, std::uint32_t size) const;
};

} // namespace i486
