#ifndef GAMEBOY_HPP
#define GAMEBOY_HPP

#include "Bus.hpp"
#include "CPU.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

/**
 * A DMG with a cartridge inserted, started in the state the boot ROM leaves it in
 * (Pan Docs: Power Up Sequence), since there is no boot ROM.
 */
class GameBoy {
public:
	/**
	 * \throws std::runtime_error for an invalid or unsupported cartridge.
	 */
	explicit GameBoy(std::vector<std::uint8_t> rom);

	/**
	 * Reads a whole ROM file.
	 * \throws std::runtime_error if the file can't be read.
	 */
	[[nodiscard]] static std::vector<std::uint8_t> readRomFile(const std::filesystem::path &path);

	// Runs one instruction (or interrupt dispatch / halted cycle). \return M-cycles taken.
	int step() { return cpu.step(); }

	[[nodiscard]] const CPU &cpuState() const noexcept { return cpu; }
	[[nodiscard]] const std::string &serialOutput() const noexcept { return bus.serialOutput(); }

private:
	Bus bus;
	CPU cpu{bus};

	void resetToPostBoot(std::uint8_t headerChecksum);
};

#endif
