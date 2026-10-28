#ifndef CARTRIDGE_HPP
#define CARTRIDGE_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/**
 * Cartridge header fields (Pan Docs: The Cartridge Header).
 */
struct CartridgeHeader {
	std::string title; // 0134-0143, upper-case ASCII padded with 0x00
	std::uint8_t type{}; // 0147, which MBC and extra hardware
	std::size_t romBanks{}; // 0148, number of 16 KiB ROM banks
	std::size_t ramBanks{}; // 0149, number of 8 KiB RAM banks
	bool checksumValid{}; // 014D matches the checksum of 0134-014C (the boot ROM locks up otherwise)

	/**
	 * Parses the header of `rom`.
	 * \throws std::runtime_error if `rom` is too small to hold a header or has an unknown ROM / RAM size code.
	 */
	[[nodiscard]] static CartridgeHeader parse(const std::vector<std::uint8_t> &rom);
};

/**
 * Game cartridge: ROM at 0x0000-0x7FFF and external RAM at 0xA000-0xBFFF.
 * Writes to the ROM range go to the cartridge's memory bank controller (if any).
 */
class Cartridge {
public:
	virtual ~Cartridge() = default;

	[[nodiscard]] virtual std::uint8_t read(std::uint16_t addr) const noexcept = 0;
	virtual void write(std::uint16_t addr, std::uint8_t value) noexcept = 0;

	/**
	 * Creates the cartridge for `rom`, picking the MBC from header byte 0x0147.
	 * \throws std::runtime_error for an invalid header or an unsupported cartridge type.
	 */
	[[nodiscard]] static std::unique_ptr<Cartridge> load(std::vector<std::uint8_t> rom);

protected:
	Cartridge() = default;
	Cartridge(const Cartridge &) = default;
	Cartridge &operator=(const Cartridge &) = default;
};

/**
 * No MBC: 32 KiB ROM, optionally 8 KiB RAM (types 0x00, 0x08, 0x09).
 */
class RomOnly : public Cartridge {
public:
	// `ramSize` in bytes (0 for no RAM)
	RomOnly(std::vector<std::uint8_t> rom, std::size_t ramSize);

	[[nodiscard]] std::uint8_t read(std::uint16_t addr) const noexcept override;
	void write(std::uint16_t addr, std::uint8_t value) noexcept override;

private:
	std::vector<std::uint8_t> rom;
	std::vector<std::uint8_t> ram;
};

/**
 * MBC1 (types 0x01-0x03, Pan Docs: MBC1). Up to 2 MiB ROM and 32 KiB RAM.
 *   0000-1FFF  RAM enable (0x0A in the lower nibble)
 *   2000-3FFF  ROM bank, 5 bits (0 selects 1)
 *   4000-5FFF  RAM bank / upper ROM bank bits, 2 bits
 *   6000-7FFF  banking mode (1 also banks 0000-3FFF and A000-BFFF with the 2-bit register)
 */
class Mbc1 : public Cartridge {
public:
	// `romBanks` in 16 KiB banks (a power of two), `ramSize` in bytes (0 for no RAM)
	Mbc1(std::vector<std::uint8_t> rom, std::size_t romBanks, std::size_t ramSize);

	[[nodiscard]] std::uint8_t read(std::uint16_t addr) const noexcept override;
	void write(std::uint16_t addr, std::uint8_t value) noexcept override;

private:
	std::vector<std::uint8_t> rom;
	std::vector<std::uint8_t> ram;
	std::size_t romBankMask; // ROM bank numbers are masked to the banks the cartridge has

	bool ramEnabled{};
	std::uint8_t romBank{}; // 5-bit register
	std::uint8_t bank2{}; // 2-bit register: RAM bank or ROM bank bits 5-6
	bool advancedMode{};

	[[nodiscard]] std::size_t ramIndex(std::uint16_t addr) const noexcept;
};

#endif
