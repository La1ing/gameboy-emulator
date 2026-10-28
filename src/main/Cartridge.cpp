#include "Cartridge.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>
#include <utility>

namespace {

constexpr std::size_t romBankSize = 0x4000;
constexpr std::size_t ramBankSize = 0x2000;
constexpr std::size_t headerEnd = 0x0150;

// RAM size code at 0x0149 -> number of 8 KiB banks (0x01 is unused)
std::size_t ramBanksFromCode(std::uint8_t code){
	switch (code) {
		case 0x00: case 0x01: return 0;
		case 0x02: return 1;
		case 0x03: return 4;
		case 0x04: return 16;
		case 0x05: return 8;
		default: throw std::runtime_error(std::format("Unknown RAM size code 0x{:02X}", code));
	}
}

} // namespace

CartridgeHeader CartridgeHeader::parse(const std::vector<std::uint8_t> &rom){
	if (rom.size() < headerEnd){
		throw std::runtime_error(std::format("ROM is {} bytes, too small for a cartridge header", rom.size()));
	}
	CartridgeHeader header;
	for (std::size_t addr = 0x0134; addr <= 0x0143 && rom[addr] != 0x00; ++addr){
		header.title += static_cast<char>(rom[addr]);
	}
	header.type = rom[0x0147];

	const std::uint8_t romCode = rom[0x0148];
	if (romCode > 0x08){
		throw std::runtime_error(std::format("Unknown ROM size code 0x{:02X}", romCode));
	}
	header.romBanks = std::size_t{2} << romCode; // 32 KiB << code
	header.ramBanks = ramBanksFromCode(rom[0x0149]);

	std::uint8_t checksum = 0;
	for (std::size_t addr = 0x0134; addr <= 0x014C; ++addr){
		checksum = static_cast<std::uint8_t>(checksum - rom[addr] - 1);
	}
	header.checksumValid = checksum == rom[0x014D];
	return header;
}

std::unique_ptr<Cartridge> Cartridge::load(std::vector<std::uint8_t> rom){
	const CartridgeHeader header = CartridgeHeader::parse(rom);
	// Pad a short dump to the size the header declares (open bus reads as 0xFF)
	rom.resize(std::max(rom.size(), header.romBanks * romBankSize), 0xFF);
	const std::size_t ramSize = header.ramBanks * ramBankSize;

	switch (header.type) {
		case 0x00: case 0x08: case 0x09: // ROM ONLY, ROM+RAM(+BATTERY)
			return std::make_unique<RomOnly>(std::move(rom), ramSize);
		case 0x01: case 0x02: case 0x03: // MBC1(+RAM)(+BATTERY)
			return std::make_unique<Mbc1>(std::move(rom), header.romBanks, ramSize);
		default:
			throw std::runtime_error(std::format("Unsupported cartridge type 0x{:02X}", header.type));
	}
}

RomOnly::RomOnly(std::vector<std::uint8_t> rom, std::size_t ramSize)
	: rom(std::move(rom)), ram(ramSize, 0x00) {
	this->rom.resize(std::max(this->rom.size(), 2 * romBankSize), 0xFF);
}

std::uint8_t RomOnly::read(std::uint16_t addr) const noexcept {
	if (addr < 0x8000){
		return rom[addr];
	}
	if (addr >= 0xA000 && addr < 0xC000 && !ram.empty()){
		return ram[(addr - 0xA000) % ram.size()];
	}
	return 0xFF; // No RAM: open bus
}

void RomOnly::write(std::uint16_t addr, std::uint8_t value) noexcept {
	if (addr >= 0xA000 && addr < 0xC000 && !ram.empty()){
		ram[(addr - 0xA000) % ram.size()] = value;
	} // ROM writes are ignored
}

Mbc1::Mbc1(std::vector<std::uint8_t> rom, std::size_t romBanks, std::size_t ramSize)
	: rom(std::move(rom)), ram(ramSize, 0x00), romBankMask(romBanks - 1) {
	this->rom.resize(std::max(this->rom.size(), romBanks * romBankSize), 0xFF);
}

std::uint8_t Mbc1::read(std::uint16_t addr) const noexcept {
	if (addr < 0x4000){ // Bank 0, or bank2 << 5 in advanced mode
		const std::size_t bank = (advancedMode? bank2 << 5: 0) & romBankMask;
		return rom[bank * romBankSize + addr];
	}
	if (addr < 0x8000){ // Switchable bank; 0 -> 1 is checked on all 5 bits before masking
		const std::size_t low = (romBank == 0)? 1: romBank;
		const std::size_t bank = ((bank2 << 5) | low) & romBankMask;
		return rom[bank * romBankSize + (addr - 0x4000)];
	}
	if (addr >= 0xA000 && addr < 0xC000 && ramEnabled && !ram.empty()){
		return ram[ramIndex(addr)];
	}
	return 0xFF; // RAM disabled or absent: open bus
}

void Mbc1::write(std::uint16_t addr, std::uint8_t value) noexcept {
	if (addr < 0x2000){
		ramEnabled = (value & 0x0F) == 0x0A;
	} else if (addr < 0x4000){
		romBank = value & 0x1F;
	} else if (addr < 0x6000){
		bank2 = value & 0x03;
	} else if (addr < 0x8000){
		advancedMode = value & 0x01;
	} else if (addr >= 0xA000 && addr < 0xC000 && ramEnabled && !ram.empty()){
		ram[ramIndex(addr)] = value;
	}
}

/**
 * RAM bank is bank2 in advanced mode, 0 otherwise; masked to the RAM the cartridge has.
 */
std::size_t Mbc1::ramIndex(std::uint16_t addr) const noexcept {
	const std::size_t bank = advancedMode? bank2: 0;
	return (bank * ramBankSize + (addr - 0xA000)) % ram.size();
}
