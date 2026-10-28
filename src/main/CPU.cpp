#include "CPU.hpp"

#include <format>
#include <iostream>

void CPU::initialize(){
	AF = 0;
	BC = 0;
	DE = 0;
	HL = 0;
	SP = 0;
	PC = 0x00; // Stating point of ROM
}

int CPU::step(){
	const std::uint8_t opcode = readByte(PC);
	if (opcode == 0xCB){
		return executeOpcode(static_cast<std::uint16_t>(0xCB00 | readByte(PC + 1)));
	}
	return executeOpcode(opcode);
}

int CPU::executeOpcode(std::uint16_t opcode){
	if (highByte(opcode) == 0xCB){ // CB-prefixed opcodes
		return execCB(lowByte(opcode));
	}
	if (opcode > 0xFF){
		return unimplemented(opcode);
	}
	const auto op = static_cast<std::uint8_t>(opcode);
	switch (op >> 6){
		case 0: return execBlock0(op);
		case 1: return execBlock1(op);
		case 2: return execBlock2(op);
		default: return execBlock3(op);
	}
}

/**
 * 0x00-0x3F, decoded by the lower nibble.
 */
int CPU::execBlock0(std::uint8_t opcode){
	const auto y = static_cast<std::uint8_t>((opcode >> 3) & 0b111); // 2nd octal digit
	const auto p = static_cast<std::uint8_t>(y >> 1); // bits 5-4 of opcode, selects register pair
	std::uint16_t &ddReg = pairReg(p); // Register pair target
	int cycles = 1;

	switch(opcode & 0x0F){
		case 0x00: {
			const auto e = static_cast<std::int8_t>(readByte(PC + 1));
			switch (p) {
				case 0b00: PC++; break; // NOP
				case 0b01: PC += 2; break; // STOP
				default: { // JR NZ, e / JR NC, e
					const bool taken = cond(static_cast<std::uint8_t>(y - 4));
					PC += (taken? e: 0) + 2;
					cycles = taken? 3: 2;
					break;
				}
			}
			break;
		}
		case 0x01: loadReg(readByte(PC + 2), readByte(PC + 1), ddReg); PC += 3; cycles = 3; break;
		case 0x02:
			switch (opcode){
				case 0x22: storeReg(A(), HL); incReg(1, HL, Mode::Pair); break;
				case 0x32: storeReg(A(), HL); incReg(-1, HL, Mode::Pair); break;
				default: storeReg(A(), ddReg); break;
			}
			PC++;
			cycles = 2;
			break;
		case 0x03: incReg(1, ddReg, Mode::Pair); PC++; cycles = 2; break;
		case 0x04:
			if (opcode == 0x34){
				incMem(1, HL);
				cycles = 3;
			} else {
				incReg(1, ddReg, Mode::High);
			}
			PC++;
			break;
		case 0x05:
			if (opcode == 0x35) {
				incMem(-1, HL);
				cycles = 3;
			} else {
				incReg(-1, ddReg, Mode::High);
			}
			PC++;
			break;
		case 0x06:
			if (opcode == 0x36) {
				storeReg(readByte(PC + 1), HL);
				cycles = 3;
			} else {
				loadReg(readByte(PC + 1), lowByte(ddReg), ddReg);
				cycles = 2;
			}
			PC += 2;
			break;
		case 0x07:
			switch (p){
				case 0b00: rotate(AF, false, Direction::Left, Mode::High); break;
				case 0b01: rotate(AF, true, Direction::Left, Mode::High); break;
				case 0b10: break; // DO WHEN FINISHED IMPLEMENTING ADD/SUB LOGIC
				case 0b11: setFlag(cFlag, true); setFlag(nFlag | hFlag, false); break; // Set carry flag | unset negative & HC flags
			}
			PC++;
			break;
		case 0x08: {
			const auto e = static_cast<std::int8_t>(readByte(PC + 1));
			switch (p) {
				case 0b00: { // LD (u16), SP
					const std::uint16_t addr = read16(PC + 1);
					storeReg(lowByte(SP), addr);
					storeReg(highByte(SP), addr + 1);
					PC += 3;
					cycles = 5;
					break;
				}
				case 0b01: PC += e + 2; cycles = 3; break; // JR e
				default: { // JR Z, e / JR C, e
					const bool taken = cond(static_cast<std::uint8_t>(y - 4));
					PC += (taken? e: 0) + 2;
					cycles = taken? 3: 2;
					break;
				}
			}
			break;
		}
		case 0x09: addPairs(HL, ddReg); PC++; cycles = 2; break;
		case 0x0A:
			switch (opcode){
				case 0x2A: loadReg(readByte(HL), F(), AF); incReg(1, HL, Mode::Pair); break;
				case 0x3A: loadReg(readByte(HL), F(), AF); incReg(-1, HL, Mode::Pair); break;
				default: loadReg(readByte(ddReg), F(), AF); break;
			}
			PC++;
			cycles = 2;
			break;
		case 0x0B: incReg(-1, ddReg, Mode::Pair); PC++; cycles = 2; break;
		case 0x0C:
			if (p == 0b11){
				incReg(1, AF, Mode::High);
			} else {
				incReg(1, ddReg, Mode::Low);
			}
			PC++;
			break;
		case 0x0D:
			if (p == 0b11){
				incReg(-1, AF, Mode::High);
			} else {
				incReg(-1, ddReg, Mode::Low);
			}
			PC++;
			break;
		case 0x0E:
			if (p == 0b11){
				loadReg(readByte(PC + 1), F(), AF);
			} else {
				loadReg(highByte(ddReg), readByte(PC + 1), ddReg);
			}
			PC += 2;
			cycles = 2;
			break;
		case 0x0F:
			switch (p){
				case 0b00: rotate(AF, false, Direction::Right, Mode::High); break;
				case 0b01: rotate(AF, true, Direction::Right, Mode::High); break;
				case 0b10: loadReg(static_cast<std::uint8_t>(~A()), F(), AF); setFlag(nFlag | hFlag, true); break; // CPL: set negative & HC flags
				case 0b11: AF ^= cFlag; setFlag(nFlag | hFlag, false); break; // CCF: flip carry | unset negative & HC flags
			}
			PC++;
			break;
	}
	return cycles;
}

/**
 * 0x40-0x7F: LD r, r' and HALT.
 */
int CPU::execBlock1(std::uint8_t opcode){
	return unimplemented(opcode);
}

/**
 * 0x80-0xBF: 8-bit ALU on A and r.
 */
int CPU::execBlock2(std::uint8_t opcode){
	return unimplemented(opcode);
}

/**
 * 0xC0-0xFF: control flow, stack, high-page loads and immediate ALU.
 */
int CPU::execBlock3(std::uint8_t opcode){
	return unimplemented(opcode);
}

/**
 * CB-prefixed opcodes: rotates/shifts, BIT, RES and SET.
 */
int CPU::execCB(std::uint8_t opcode){
	return unimplemented(static_cast<std::uint16_t>(0xCB00 | opcode));
}

int CPU::unimplemented(std::uint16_t opcode){
	std::cerr << std::format("Unimplemented opcode: 0x{:X}\n", opcode);
	return 0;
}

/**
 * Returns the register pair selected by bits 5-4 of the opcode.
 */
std::uint16_t &CPU::pairReg(std::uint8_t p) noexcept {
	switch (p) {
		case 0b00: return BC;
		case 0b01: return DE;
		case 0b10: return HL;
		default: return SP;
	}
}

/**
 * Reads 8-bit register by its opcode index: B, C, D, E, H, L, (HL), A.
 */
std::uint8_t CPU::getR8(std::uint8_t idx) const noexcept {
	switch (idx) {
		case 0: return B();
		case 1: return C();
		case 2: return D();
		case 3: return E();
		case 4: return H();
		case 5: return L();
		case 6: return readByte(HL);
		default: return A();
	}
}

/**
 * Writes 8-bit register by its opcode index: B, C, D, E, H, L, (HL), A.
 */
void CPU::setR8(std::uint8_t idx, std::uint8_t value) noexcept {
	switch (idx) {
		case 0: loadReg(value, C(), BC); break;
		case 1: loadReg(B(), value, BC); break;
		case 2: loadReg(value, E(), DE); break;
		case 3: loadReg(D(), value, DE); break;
		case 4: loadReg(value, L(), HL); break;
		case 5: loadReg(H(), value, HL); break;
		case 6: storeReg(value, HL); break;
		default: loadReg(value, F(), AF); break;
	}
}

/**
 * Reads little endian 16-bit value at `addr`.
 */
std::uint16_t CPU::read16(std::uint16_t addr) const noexcept {
	return static_cast<std::uint16_t>((readByte(addr + 1) << 8) | readByte(addr));
}

/**
 * Pushes `value` onto the stack (higher byte at SP-1, lower byte at SP-2).
 */
void CPU::push16(std::uint16_t value) noexcept {
	storeReg(highByte(value), --SP);
	storeReg(lowByte(value), --SP);
}

/**
 * Pops 16-bit value from the stack.
 */
std::uint16_t CPU::pop16() noexcept {
	const std::uint16_t value = read16(SP);
	SP += 2;
	return value;
}

/**
 * Condition by its opcode index: NZ, Z, NC, C.
 */
bool CPU::cond(std::uint8_t cc) const noexcept {
	switch (cc) {
		case 0: return !(AF & zFlag);
		case 1: return AF & zFlag;
		case 2: return !(AF & cFlag);
		default: return AF & cFlag;
	}
}

/**
 * Sets (or unsets) the given flag/s in F.
 */
void CPU::setFlag(std::uint16_t flag, bool set) noexcept {
	if (set){
		AF |= flag;
	} else {
		AF &= ~flag;
	}
}

/**
 * Loads data into register.
 */
void CPU::loadReg(std::uint8_t high, std::uint8_t low, std::uint16_t &reg){
	reg = static_cast<std::uint16_t>((high << 8) | low);
}

/**
 * Stores register into memory.
 */
void CPU::storeReg(std::uint8_t value, std::uint16_t loc){
	memory[loc] = value;
}

/**
 * Increments registers.
 * \param mode determines if higher/lower/both register/s are incremented.
 */
void CPU::incReg(int amount, std::uint16_t &reg, Mode mode){
	if (mode == Mode::Pair){
		reg = static_cast<std::uint16_t>(reg + amount);
	} else {
		const int shift = (mode == Mode::High)? 8:0; // Shift of 8 if on the higher bit; 0 otherwise
		const auto halfReg = static_cast<std::uint8_t>(reg >> shift); // Shifting register pair then masking the lower register to get the register
		if (amount > 0){
			setFlag(zFlag, halfReg == 0xFF);
			setFlag(hFlag, (halfReg & 0x0F) == 0x0F); // carry from bit 3
			setFlag(nFlag, false);
		} else {
			setFlag(zFlag, halfReg == 1);
			setFlag(hFlag, (halfReg & 0x0F) == 0x00); // borrow from bit 4
			setFlag(nFlag, true);
		}
		const auto result = static_cast<std::uint8_t>(halfReg + amount);
		const std::uint16_t mask = (mode == Mode::High)? 0x00FF:0xFF00; // Mask for other register
		reg = static_cast<std::uint16_t>((result << shift) | (reg & mask));
	}
}


/**
 * Increments memory at location of register `loc`.
 */
void CPU::incMem(int amount, std::uint16_t loc){
	std::uint8_t &targetMem = memory[loc];
	if (amount > 0){
		setFlag(zFlag, targetMem == 0xFF);
		setFlag(hFlag, (targetMem & 0x0F) == 0x0F); // carry from bit 3
		setFlag(nFlag, false);
	} else {
		setFlag(zFlag, targetMem == 1);
		setFlag(hFlag, (targetMem & 0x0F) == 0x00); // borrow from bit 4
		setFlag(nFlag, true);
	}
	targetMem = static_cast<std::uint8_t>(targetMem + amount);
}

/**
 * Rotate register `Left` or `Right`.
 *
 * \param regPair Target register pair.
 * \param useCarry True if uses carry for rotation, false if only last bit is copied to carry.
 * \param d Rotate `Left` or `Right`.
 * \param mode Register that is rotated.
 */
void CPU::rotate(std::uint16_t &regPair, bool useCarry, Direction d, Mode mode){
	if (mode == Mode::Pair){
		// TODO: rotate pair
	} else {
		const int shift = (mode == Mode::High)? 8:0; // Shift of 8 if on the higher bit; 0 otherwise
		const auto reg = static_cast<std::uint8_t>(regPair >> shift); // Shifting register pair then masking the lower register to get the register
		const int carryIn = (AF & cFlag)? 1: 0;
		std::uint8_t newReg;
		if (d == Direction::Left){
			newReg = static_cast<std::uint8_t>((reg << 1) | (useCarry? carryIn: reg >> 7)); // first bit is carry or last bit of original
			setFlag(cFlag, reg & 0x80);
		} else {
			newReg = static_cast<std::uint8_t>((reg >> 1) | ((useCarry? carryIn: reg & 0x01) << 7)); // last bit is carry or first bit of original
			setFlag(cFlag, reg & 0x01);
		}
		setFlag(zFlag | nFlag | hFlag, false);
		const std::uint16_t mask = (mode == Mode::High)? 0x00FF:0xFF00; // other (unchanged) register is opposite mask of rotated register
		regPair = static_cast<std::uint16_t>((newReg << shift) | (regPair & mask));
	}
}

/**
 * Adds `value` to register pair `target` and stores result in `target`.
 */
void CPU::addPairs(std::uint16_t &target, std::uint16_t value){
	setFlag(nFlag, false); // Unset negative flag
	constexpr std::uint16_t halfMask = 0x0FFF; // Mask for first 12 bits of registers
	setFlag(hFlag, (target & halfMask) + (value & halfMask) > halfMask); // Checking for half Carry-over (bit 11 carry)
	setFlag(cFlag, target + value > 0xFFFF); // Checking for carry-over
	target = static_cast<std::uint16_t>(target + value);
}
