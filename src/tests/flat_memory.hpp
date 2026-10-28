#ifndef FLAT_MEMORY_HPP
#define FLAT_MEMORY_HPP

#include "../main/MemoryBus.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

// 64 KiB of plain RAM: every address is readable and writable. Test-only stand-in for the real Bus.
class FlatMemory : public MemoryBus {
public:
    [[nodiscard]] std::uint8_t read(std::uint16_t addr) const noexcept override { return data[addr]; }
    void write(std::uint16_t addr, std::uint8_t value) noexcept override { data[addr] = value; }

    std::uint8_t &operator[](std::size_t addr) { return data[addr]; }
    const std::uint8_t &operator[](std::size_t addr) const { return data[addr]; }

private:
    std::array<std::uint8_t, 0x10000> data{};
};

#endif
