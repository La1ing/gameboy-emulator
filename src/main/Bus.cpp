#include "Bus.hpp"

/**
 * Serial transfer with the internal clock and no link partner (Pan Docs: Serial Data Transfer):
 * the byte in SB is sent (kept in serialOutput), SB receives 0xFF, SC bit 7 clears and the serial
 * interrupt is requested. Happens at once; the real transfer takes 8 bits at 8192 Hz.
 */
void Bus::transferSerial() noexcept {
	serialData += static_cast<char>(io[serialTransferData - 0xFF00]);
	io[serialTransferData - 0xFF00] = 0xFF;
	io[serialControl - 0xFF00] &= 0x7F;
	io[0x0F] |= 0x08; // IF: serial interrupt
}

std::uint8_t Bus::read(std::uint16_t addr) const noexcept {
	if (addr < 0x8000 || (addr >= 0xA000 && addr < 0xC000)){ // Cartridge ROM / RAM
		return cartridge? cartridge->read(addr): 0xFF; // Empty slot reads as open bus
	}
	if (addr < 0xA000) return vram[addr - 0x8000];
	if (addr < 0xE000) return wram[addr - 0xC000];
	if (addr < 0xFE00) return wram[addr - 0xE000]; // Echo RAM
	if (addr < 0xFEA0) return oam[addr - 0xFE00];
	if (addr < 0xFF00) return 0x00; // Not usable: reads 0x00 on DMG (outside OAM blocking)
	if (addr < 0xFF80) return io[addr - 0xFF00];
	if (addr < 0xFFFF) return hram[addr - 0xFF80];
	return ie;
}

void Bus::write(std::uint16_t addr, std::uint8_t value) noexcept {
	if (addr < 0x8000 || (addr >= 0xA000 && addr < 0xC000)){ // Cartridge MBC registers / RAM
		if (cartridge){
			cartridge->write(addr, value);
		}
	} else if (addr < 0xA000){
		vram[addr - 0x8000] = value;
	} else if (addr < 0xE000){
		wram[addr - 0xC000] = value;
	} else if (addr < 0xFE00){
		wram[addr - 0xE000] = value; // Echo RAM
	} else if (addr < 0xFEA0){
		oam[addr - 0xFE00] = value;
	} else if (addr < 0xFF00){
		// Not usable: writes ignored
	} else if (addr < 0xFF80){
		io[addr - 0xFF00] = value;
		if (addr == serialControl && (value & 0x81) == 0x81){
			transferSerial();
		}
	} else if (addr < 0xFFFF){
		hram[addr - 0xFF80] = value;
	} else {
		ie = value;
	}
}
