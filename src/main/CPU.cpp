#include "CPU.hpp"

#include <bit>
#include <format>
#include <iostream>
#include <utility>

void CPU::initialize(){
	AF = 0;
	BC = 0;
	DE = 0;
	HL = 0;
	SP = 0;
	PC = 0x00; // Stating point of ROM
	halted = false;
	locked = false;
	ime = false;
	imePending = false;
	haltBug = false;
}

int CPU::step(){
	if (locked){
		return 1;
	}
	const std::uint8_t pending = pendingInterrupts();
	if (halted){
		if (!pending){
			return 1; // Idle for one M-cycle
		}
		halted = false; // Wake up; the interrupt is only serviced if IME is set
	}
	const bool bugged = std::exchange(haltBug, false);
	if (ime && pending){
		if (bugged){
			PC--; // Handler returns to the HALT (EI right before a bugged HALT)
		}
		return serviceInterrupt(pending);
	}

	const bool enableIme = imePending; // EI from the previous instruction takes effect after this one
	const std::uint8_t opcode = readByte(PC);
	if (bugged){
		PC--; // HALT bug: PC is not incremented after the fetch, so the byte after HALT is read twice
	}
	const int cycles = (opcode == 0xCB)
		? executeOpcode(static_cast<std::uint16_t>(0xCB00 | readByte(PC + 1)))
		: executeOpcode(opcode);
	if (enableIme && imePending){ // a DI in between cancels it
		ime = true;
		imePending = false;
	}
	return cycles;
}

void CPU::requestInterrupt(Interrupt interrupt) noexcept {
	storeReg(static_cast<std::uint8_t>(readByte(ifAddress) | (1 << static_cast<int>(interrupt))), ifAddress);
}

/**
 * Interrupts that are both requested (IF) and enabled (IE).
 */
std::uint8_t CPU::pendingInterrupts() const noexcept {
	return readByte(ifAddress) & readByte(ieAddress) & 0x1F;
}

/**
 * Calls the handler of the highest priority pending interrupt (lowest bit): clears its IF bit and IME,
 * pushes PC and jumps to 0x40 + 8 * bit. Takes 5 M-cycles (2 wait, 2 push, 1 jump).
 */
int CPU::serviceInterrupt(std::uint8_t pending){
	const int bit = std::countr_zero(pending);
	storeReg(static_cast<std::uint8_t>(readByte(ifAddress) & ~(1 << bit)), ifAddress);
	ime = false;
	push16(PC);
	PC = static_cast<std::uint16_t>(0x40 + 8 * bit);
	return 5;
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
				case 0b00: setR8(7, shiftRotate(0, A())); setFlag(zFlag, false); break; // RLCA: RLC A, but Z always unset
				case 0b01: setR8(7, shiftRotate(2, A())); setFlag(zFlag, false); break; // RLA: RL A, but Z always unset
				case 0b10: daa(); break;
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
				case 0b00: setR8(7, shiftRotate(1, A())); setFlag(zFlag, false); break; // RRCA: RRC A, but Z always unset
				case 0b01: setR8(7, shiftRotate(3, A())); setFlag(zFlag, false); break; // RRA: RR A, but Z always unset
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
	PC++;
	if (opcode == 0x76){ // HALT (would be LD (HL), (HL))
		if (!ime && pendingInterrupts()){
			haltBug = true; // HALT exits at once, and the byte after it is read twice
		} else {
			halted = true;
		}
		return 1;
	}
	const auto y = static_cast<std::uint8_t>((opcode >> 3) & 0b111); // destination
	const auto z = static_cast<std::uint8_t>(opcode & 0b111); // source
	setR8(y, getR8(z));
	return (y == 6 || z == 6)? 2: 1; // (HL) access takes an extra M-cycle
}

/**
 * 0x80-0xBF: 8-bit ALU on A and r.
 */
int CPU::execBlock2(std::uint8_t opcode){
	const auto y = static_cast<std::uint8_t>((opcode >> 3) & 0b111); // operation
	const auto z = static_cast<std::uint8_t>(opcode & 0b111); // operand
	alu(y, getR8(z));
	PC++;
	return (z == 6)? 2: 1; // (HL) access takes an extra M-cycle
}

/**
 * 0xC0-0xFF: control flow, stack, high-page loads and immediate ALU.
 */
int CPU::execBlock3(std::uint8_t opcode){
	const auto y = static_cast<std::uint8_t>((opcode >> 3) & 0b111);
	const auto z = static_cast<std::uint8_t>(opcode & 0b111);
	const auto p = static_cast<std::uint8_t>(y >> 1); // register pair select
	const bool q = y & 1;

	switch (z){
		case 0:
			switch (y){
				case 4: storeReg(A(), static_cast<std::uint16_t>(0xFF00 | readByte(PC + 1))); PC += 2; return 3; // LDH (a8), A
				case 5: SP = spPlusE(static_cast<std::int8_t>(readByte(PC + 1))); PC += 2; return 4; // ADD SP, e
				case 6: loadReg(readByte(static_cast<std::uint16_t>(0xFF00 | readByte(PC + 1))), F(), AF); PC += 2; return 3; // LDH A, (a8)
				case 7: HL = spPlusE(static_cast<std::int8_t>(readByte(PC + 1))); PC += 2; return 3; // LD HL, SP+e
				default: // RET cc
					if (cond(y)){
						PC = pop16();
						return 5;
					}
					PC++;
					return 2;
			}
		case 1:
			if (!q){ // POP rr
				const std::uint16_t value = pop16();
				if (p == 0b11){
					AF = value & 0xFFF0; // lower nibble of F is always 0
				} else {
					pairReg(p) = value;
				}
				PC++;
				return 3;
			}
			switch (p){
				case 0b00: PC = pop16(); return 4; // RET
				case 0b01: PC = pop16(); ime = true; imePending = false; return 4; // RETI: IME set immediately
				case 0b10: PC = HL; return 1; // JP HL
				default: SP = HL; PC++; return 2; // LD SP, HL
			}
		case 2:
			switch (y){
				case 4: storeReg(A(), static_cast<std::uint16_t>(0xFF00 | C())); PC++; return 2; // LD (C), A
				case 5: storeReg(A(), read16(PC + 1)); PC += 3; return 4; // LD (a16), A
				case 6: loadReg(readByte(static_cast<std::uint16_t>(0xFF00 | C())), F(), AF); PC++; return 2; // LD A, (C)
				case 7: loadReg(readByte(read16(PC + 1)), F(), AF); PC += 3; return 4; // LD A, (a16)
				default: // JP cc, a16
					if (cond(y)){
						PC = read16(PC + 1);
						return 4;
					}
					PC += 3;
					return 3;
			}
		case 3:
			switch (y){
				case 0: PC = read16(PC + 1); return 4; // JP a16
				case 1: return unimplemented(opcode); // 0xCB prefix: CB opcodes are passed as 0xCBxx
				case 6: ime = false; imePending = false; PC++; return 1; // DI
				case 7: imePending = true; PC++; return 1; // EI: IME is set after the next instruction
				default: return lock(); // 0xD3, 0xDB, 0xE3, 0xEB
			}
		case 4:
			if (y < 4){ // CALL cc, a16
				if (cond(y)){
					call(read16(PC + 1));
					return 6;
				}
				PC += 3;
				return 3;
			}
			return lock(); // 0xE4, 0xEC, 0xF4, 0xFC
		case 5:
			if (!q){ // PUSH rr
				push16((p == 0b11)? AF: pairReg(p));
				PC++;
				return 4;
			}
			if (p == 0b00){ // CALL a16
				call(read16(PC + 1));
				return 6;
			}
			return lock(); // 0xDD, 0xED, 0xFD
		case 6: // ADD/ADC/SUB/SBC/AND/XOR/OR/CP A, d8
			alu(y, readByte(PC + 1));
			PC += 2;
			return 2;
		default: // RST y*8
			push16(static_cast<std::uint16_t>(PC + 1));
			PC = static_cast<std::uint16_t>(y * 8);
			return 4;
	}
}

/**
 * Calls `target`: pushes the address of the next instruction (CALL is 3 bytes) and jumps.
 */
void CPU::call(std::uint16_t target) noexcept {
	push16(static_cast<std::uint16_t>(PC + 3));
	PC = target;
}

/**
 * Illegal opcode: hard-locks the CPU until power off.
 */
int CPU::lock() noexcept {
	locked = true;
	return 1;
}

/**
 * SP + signed `e` for ADD SP, e and LD HL, SP+e. H and C come from the unsigned add of the lower byte.
 */
std::uint16_t CPU::spPlusE(std::int8_t e) noexcept {
	const auto offset = static_cast<std::uint8_t>(e);
	setFlag(zFlag | nFlag, false);
	setFlag(hFlag, (SP & 0x0F) + (offset & 0x0F) > 0x0F); // carry from bit 3
	setFlag(cFlag, (SP & 0xFF) + offset > 0xFF); // carry from bit 7
	return static_cast<std::uint16_t>(SP + e);
}

/**
 * CB-prefixed opcodes: rotates/shifts, BIT, RES and SET.
 */
int CPU::execCB(std::uint8_t opcode){
	const auto x = static_cast<std::uint8_t>(opcode >> 6); // operation group
	const auto y = static_cast<std::uint8_t>((opcode >> 3) & 0b111); // rotate/shift op or bit number
	const auto z = static_cast<std::uint8_t>(opcode & 0b111); // operand
	const std::uint8_t value = getR8(z);
	const auto bit = static_cast<std::uint8_t>(1 << y);
	PC += 2;

	switch (x) {
		case 0: setR8(z, shiftRotate(y, value)); break;
		case 1: // BIT y, r: Z set if bit is 0 | unset N | set H
			setFlag(zFlag, !(value & bit));
			setFlag(nFlag, false);
			setFlag(hFlag, true);
			return (z == 6)? 3: 2; // (HL) is only read
		case 2: setR8(z, static_cast<std::uint8_t>(value & ~bit)); break; // RES y, r
		default: setR8(z, static_cast<std::uint8_t>(value | bit)); break; // SET y, r
	}
	return (z == 6)? 4: 2; // (HL) is read and written
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
 * 8-bit ALU operation on A by its opcode index: ADD, ADC, SUB, SBC, AND, XOR, OR, CP.
 */
void CPU::alu(std::uint8_t op, std::uint8_t value) noexcept {
	const std::uint8_t a = A();
	const int carry = (op == 1 || op == 3) && (AF & cFlag)? 1: 0; // carry-in for ADC / SBC
	int result;
	switch (op) {
		case 0: case 1: // ADD / ADC
			result = a + value + carry;
			setFlag(hFlag, (a & 0x0F) + (value & 0x0F) + carry > 0x0F);
			setFlag(cFlag, result > 0xFF);
			setFlag(nFlag, false);
			break;
		case 2: case 3: case 7: // SUB / SBC / CP
			result = a - value - carry;
			setFlag(hFlag, (a & 0x0F) < (value & 0x0F) + carry);
			setFlag(cFlag, result < 0);
			setFlag(nFlag, true);
			break;
		case 4: // AND
			result = a & value;
			setFlag(nFlag | cFlag, false);
			setFlag(hFlag, true);
			break;
		case 5: // XOR
			result = a ^ value;
			setFlag(nFlag | hFlag | cFlag, false);
			break;
		default: // OR
			result = a | value;
			setFlag(nFlag | hFlag | cFlag, false);
			break;
	}
	setFlag(zFlag, (result & 0xFF) == 0);
	if (op != 7){ // CP only sets flags
		loadReg(static_cast<std::uint8_t>(result), F(), AF);
	}
}

/**
 * Decimal adjusts A after a BCD addition / subtraction (uses N, H and C from the previous op).
 */
void CPU::daa() noexcept {
	int a = A();
	bool carry = AF & cFlag;
	if (!(AF & nFlag)){ // after addition
		if (carry || a > 0x99){
			a += 0x60;
			carry = true;
		}
		if ((AF & hFlag) || (a & 0x0F) > 0x09){
			a += 0x06;
		}
	} else { // after subtraction
		if (carry){
			a -= 0x60;
		}
		if (AF & hFlag){
			a -= 0x06;
		}
	}
	setFlag(zFlag, (a & 0xFF) == 0);
	setFlag(hFlag, false);
	setFlag(cFlag, carry);
	loadReg(static_cast<std::uint8_t>(a), F(), AF);
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
 * Rotate / shift by its CB opcode index: RLC, RRC, RL, RR, SLA, SRA, SWAP, SRL.
 * Sets Z from the result, unsets N and H, and C from the bit shifted out (unset for SWAP).
 * \return Rotated / shifted value.
 */
std::uint8_t CPU::shiftRotate(std::uint8_t op, std::uint8_t value) noexcept {
	const int carryIn = (AF & cFlag)? 1: 0;
	int result;
	bool carryOut;
	switch (op) {
		case 0: carryOut = value & 0x80; result = (value << 1) | (value >> 7); break; // RLC: bit 7 to bit 0 and carry
		case 1: carryOut = value & 0x01; result = (value >> 1) | (value << 7); break; // RRC: bit 0 to bit 7 and carry
		case 2: carryOut = value & 0x80; result = (value << 1) | carryIn; break; // RL: through carry
		case 3: carryOut = value & 0x01; result = (value >> 1) | (carryIn << 7); break; // RR: through carry
		case 4: carryOut = value & 0x80; result = value << 1; break; // SLA
		case 5: carryOut = value & 0x01; result = (value >> 1) | (value & 0x80); break; // SRA: bit 7 kept
		case 6: carryOut = false; result = (value << 4) | (value >> 4); break; // SWAP nibbles
		default: carryOut = value & 0x01; result = value >> 1; break; // SRL
	}
	const auto newValue = static_cast<std::uint8_t>(result);
	setFlag(zFlag, newValue == 0);
	setFlag(nFlag | hFlag, false);
	setFlag(cFlag, carryOut);
	return newValue;
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
