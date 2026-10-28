#ifndef CARTRIDGE_HPP
#define CARTRIDGE_HPP

#include <cstdint>

/**
 * Game cartridge: ROM at 0x0000-0x7FFF and external RAM at 0xA000-0xBFFF.
 * Writes to the ROM range go to the cartridge's memory bank controller (if any).
 */
class Cartridge {
public:
	virtual ~Cartridge() = default;

	[[nodiscard]] virtual std::uint8_t read(std::uint16_t addr) const noexcept = 0;
	virtual void write(std::uint16_t addr, std::uint8_t value) noexcept = 0;

protected:
	Cartridge() = default;
	Cartridge(const Cartridge &) = default;
	Cartridge &operator=(const Cartridge &) = default;
};

#endif
