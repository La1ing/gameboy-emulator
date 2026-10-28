#ifndef MEMORY_BUS_HPP
#define MEMORY_BUS_HPP

#include <cstdint>

/**
 * 16-bit address bus the CPU reads and writes through.
 * Implemented by the real memory map (Bus) and by plain RAM in the tests (FlatMemory).
 */
class MemoryBus {
public:
	virtual ~MemoryBus() = default;

	[[nodiscard]] virtual std::uint8_t read(std::uint16_t addr) const noexcept = 0;
	virtual void write(std::uint16_t addr, std::uint8_t value) noexcept = 0;

protected:
	MemoryBus() = default;
	MemoryBus(const MemoryBus &) = default;
	MemoryBus &operator=(const MemoryBus &) = default;
};

#endif
