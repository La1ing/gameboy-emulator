#ifndef BUS_HPP
#define BUS_HPP

#include "Cartridge.hpp"
#include "MemoryBus.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

/**
 * DMG memory map (Pan Docs: Memory Map).
 *   0000-7FFF  cartridge ROM          8000-9FFF  VRAM
 *   A000-BFFF  cartridge RAM          C000-DFFF  WRAM
 *   E000-FDFF  echo of C000-DDFF      FE00-FE9F  OAM
 *   FEA0-FEFF  not usable             FF00-FF7F  I/O registers
 *   FF80-FFFE  HRAM                   FFFF       IE
 */
class Bus : public MemoryBus {
public:
	Bus() = default; // No cartridge inserted
	explicit Bus(std::unique_ptr<Cartridge> cartridge) noexcept : cartridge(std::move(cartridge)) {}

	[[nodiscard]] std::uint8_t read(std::uint16_t addr) const noexcept override;
	void write(std::uint16_t addr, std::uint8_t value) noexcept override;

	// Every byte sent over the serial port (test ROMs print their results this way)
	[[nodiscard]] const std::string &serialOutput() const noexcept { return serialData; }

	static constexpr std::uint16_t serialTransferData = 0xFF01; // SB
	static constexpr std::uint16_t serialControl = 0xFF02; // SC: bit 7 start, bit 0 internal clock

private:
	std::string serialData;
	std::unique_ptr<Cartridge> cartridge;
	std::array<std::uint8_t, 0x2000> vram{};
	std::array<std::uint8_t, 0x2000> wram{};
	std::array<std::uint8_t, 0xA0> oam{};
	std::array<std::uint8_t, 0x80> io{}; // Placeholder until timer / PPU / joypad own their registers
	std::array<std::uint8_t, 0x7F> hram{};
	std::uint8_t ie{};

	void transferSerial() noexcept;
};

#endif
