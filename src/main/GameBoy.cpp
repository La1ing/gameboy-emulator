#include "GameBoy.hpp"

#include "Cartridge.hpp"

#include <format>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace {

// Header checksum byte, read before the ROM is handed to the cartridge
std::uint8_t headerChecksumOf(const std::vector<std::uint8_t> &rom){
	return rom.size() > 0x014D? rom[0x014D]: 0x00;
}

} // namespace

GameBoy::GameBoy(std::vector<std::uint8_t> rom)
	: bus(Cartridge::load(rom)) {
	resetToPostBoot(headerChecksumOf(rom));
}

std::vector<std::uint8_t> GameBoy::readRomFile(const std::filesystem::path &path){
	std::ifstream file(path, std::ios::binary);
	if (!file){
		throw std::runtime_error(std::format("Can't open ROM file '{}'", path.string()));
	}
	return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

/**
 * DMG / MGB register values after the boot ROM (Pan Docs: Power Up Sequence).
 * H and C are set unless the header checksum is 0x00. OBP0 / OBP1 are left uninitialized by the boot ROM.
 */
void GameBoy::resetToPostBoot(std::uint8_t headerChecksum){
	cpu.initialize();
	cpu.AF = static_cast<std::uint16_t>(0x0100 | zFlag | ((headerChecksum != 0)? (hFlag | cFlag): 0));
	cpu.BC = 0x0013;
	cpu.DE = 0x00D8;
	cpu.HL = 0x014D;
	cpu.SP = 0xFFFE;
	cpu.PC = 0x0100;

	constexpr std::pair<std::uint16_t, std::uint8_t> ioRegisters[] = {
		{0xFF00, 0xCF}, // P1 / JOYP
		{0xFF01, 0x00}, // SB
		{0xFF02, 0x7E}, // SC
		{0xFF04, 0xAB}, // DIV
		{0xFF05, 0x00}, // TIMA
		{0xFF06, 0x00}, // TMA
		{0xFF07, 0xF8}, // TAC
		{0xFF0F, 0xE1}, // IF
		{0xFF10, 0x80}, {0xFF11, 0xBF}, {0xFF12, 0xF3}, {0xFF13, 0xFF}, {0xFF14, 0xBF}, // NR10-NR14
		{0xFF16, 0x3F}, {0xFF17, 0x00}, {0xFF18, 0xFF}, {0xFF19, 0xBF}, // NR21-NR24
		{0xFF1A, 0x7F}, {0xFF1B, 0xFF}, {0xFF1C, 0x9F}, {0xFF1D, 0xFF}, {0xFF1E, 0xBF}, // NR30-NR34
		{0xFF20, 0xFF}, {0xFF21, 0x00}, {0xFF22, 0x00}, {0xFF23, 0xBF}, // NR41-NR44
		{0xFF24, 0x77}, {0xFF25, 0xF3}, {0xFF26, 0xF1}, // NR50-NR52
		{0xFF40, 0x91}, // LCDC
		{0xFF41, 0x85}, // STAT
		{0xFF42, 0x00}, // SCY
		{0xFF43, 0x00}, // SCX
		{0xFF44, 0x00}, // LY
		{0xFF45, 0x00}, // LYC
		{0xFF46, 0xFF}, // DMA
		{0xFF47, 0xFC}, // BGP
		{0xFF4A, 0x00}, // WY
		{0xFF4B, 0x00}, // WX
		{0xFFFF, 0x00}, // IE
	};
	for (const auto &[addr, value] : ioRegisters){
		bus.write(addr, value);
	}
}
