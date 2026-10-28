#ifndef CPU_HPP
#define CPU_HPP

#include <array>
#include <cstdint>
#include <format>
#include <iostream>

/*
FLAGS REGISTER FOR LOWER 8 BITS OF AF.
\param 7:zFlag 	zero flag
\param 6:nFlag 	subtraction flag (BCD)
\param 5:hFlag 	half carry flag (BCD)
\param 4:cFlag 	carry flag
*/
inline constexpr std::uint16_t zFlag = 1 << 7;
inline constexpr std::uint16_t nFlag = 1 << 6;
inline constexpr std::uint16_t hFlag = 1 << 5;
inline constexpr std::uint16_t cFlag = 1 << 4;

/**
 * Mode determines which register is chosen.
 * \param High Higher register.
 * \param Low lower register.
 * \param Pair both registers.
 */
enum class Mode {High, Low, Pair};

enum class Direction {Left, Right};

// Higher / lower byte of a register pair
[[nodiscard]] constexpr std::uint8_t highByte(std::uint16_t pair) noexcept { return static_cast<std::uint8_t>(pair >> 8); }
[[nodiscard]] constexpr std::uint8_t lowByte(std::uint16_t pair) noexcept { return static_cast<std::uint8_t>(pair & 0x00FF); }

class CPU {
public:
	// Registers
	std::uint16_t AF{}; // Accumulator & flags
	std::uint16_t BC{}; // BC
	std::uint16_t DE{}; // DE
	std::uint16_t HL{}; // HL
	std::uint16_t SP{}; // Stack pointer
	std::uint16_t PC{}; // Program counter

	// Getters for individual registers
	[[nodiscard]] constexpr std::uint8_t A() const noexcept { return highByte(AF); }
	[[nodiscard]] constexpr std::uint8_t F() const noexcept { return lowByte(AF); }
	[[nodiscard]] constexpr std::uint8_t B() const noexcept { return highByte(BC); }
	[[nodiscard]] constexpr std::uint8_t C() const noexcept { return lowByte(BC); }
	[[nodiscard]] constexpr std::uint8_t D() const noexcept { return highByte(DE); }
	[[nodiscard]] constexpr std::uint8_t E() const noexcept { return lowByte(DE); }
	[[nodiscard]] constexpr std::uint8_t H() const noexcept { return highByte(HL); } // Higher register of HL (HL+)
	[[nodiscard]] constexpr std::uint8_t L() const noexcept { return lowByte(HL); } // Lower register of HL (HL-)

	// Memory
	std::array<std::uint8_t, 0x10000> memory{}; // 16 bit address bus

	// Reads memory; addresses wrap around at 16 bits (e.g. PC + 1 when PC = 0xFFFF)
	[[nodiscard]] std::uint8_t readByte(std::uint16_t addr) const noexcept { return memory[addr]; }

	void initialize();

	void executeOpcode(std::uint16_t opcode);
	void loadReg(std::uint8_t high, std::uint8_t low, std::uint16_t &reg);
	void storeReg(std::uint8_t value, std::uint16_t loc);
	void incReg(int amount, std::uint16_t &reg, Mode mode);
	void incMem(int amount, std::uint16_t loc);
	void rotate(std::uint16_t &regPair, bool useCarry, Direction d, Mode mode);
	void addPairs(std::uint16_t &target, std::uint16_t value);

private:
	[[nodiscard]] std::uint16_t &pairReg(std::uint8_t p) noexcept;
	void setFlag(std::uint16_t flag, bool set) noexcept;
};

inline void CPU::initialize(){
	AF = 0;
	BC = 0;
	DE = 0;
	HL = 0;
	SP = 0;
	PC = 0x00; // Stating point of ROM
}

inline void CPU::executeOpcode(std::uint16_t opcode){
	const auto p = static_cast<std::uint8_t>((opcode & 0b00110000) >> 4); // bits 5-4 of opcode, selects register pair

	switch(opcode & 0xFF00){
		case 0x0000: // 8-bit opcodes
			if (opcode <= 0x3F){ // First 4 rows of opcodes
				std::uint16_t &ddReg = pairReg(p); // Register pair target
				switch(opcode & 0x000F){
					case 0x00: {
						const auto e = static_cast<std::int8_t>(readByte(PC + 1));
						switch (p) {
							case 0b00: PC++; break; // NOP
							case 0b01: PC += 2; break; // STOP
							case 0b10: PC += ((AF & zFlag)? 0: e) + 2; break; // JR NZ, e
							case 0b11: PC += ((AF & cFlag)? 0: e) + 2; break; // JR NC, e
						}
						break;
					}
					case 0x01: loadReg(readByte(PC + 2), readByte(PC + 1), ddReg); PC += 3; break;
					case 0x02:
						switch (opcode){
							case 0x22: storeReg(A(), HL); incReg(1, HL, Mode::Pair); break;
							case 0x32: storeReg(A(), HL); incReg(-1, HL, Mode::Pair); break;
							default: storeReg(A(), ddReg); break;
						}
						PC++;
						break;
					case 0x03: incReg(1, ddReg, Mode::Pair); PC++; break;
					case 0x04:
						if (opcode == 0x34){
							incMem(1, HL);
						} else {
							incReg(1, ddReg, Mode::High);
						}
						PC++;
						break;
					case 0x05:
						if (opcode == 0x35) {
							incMem(-1, HL);
						} else {
							incReg(-1, ddReg, Mode::High);
						}
						PC++;
						break;
					case 0x06:
						if (opcode == 0x36) {
							storeReg(readByte(PC + 1), HL);
						} else {
							loadReg(readByte(PC + 1), lowByte(ddReg), ddReg);
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
								const auto addr = static_cast<std::uint16_t>((readByte(PC + 2) << 8) | readByte(PC + 1));
								storeReg(lowByte(SP), addr);
								storeReg(highByte(SP), addr + 1);
								PC += 3;
								break;
							}
							case 0b01: PC += e + 2; break;
							case 0b10: PC += ((AF & zFlag)? e: 0) + 2; break;
							case 0b11: PC += ((AF & cFlag)? e: 0) + 2; break;
						}
						break;
					}
					case 0x09: addPairs(HL, ddReg); PC++; break;
					case 0x0A:
						switch (opcode){
							case 0x2A: loadReg(readByte(HL), F(), AF); incReg(1, HL, Mode::Pair); break;
							case 0x3A: loadReg(readByte(HL), F(), AF); incReg(-1, HL, Mode::Pair); break;
							default: loadReg(readByte(ddReg), F(), AF); break;
						}
						PC++;
						break;
					case 0x0B: incReg(-1, ddReg, Mode::Pair); PC++; break;
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
			}
			break;

		case 0xCB00: // 16-bit opcodes

			break;

		default: std::cerr << std::format("Unknown opcode: 0x{:X}\n", opcode); break;
	}
}

/**
 * Returns the register pair selected by bits 5-4 of the opcode.
 */
inline std::uint16_t &CPU::pairReg(std::uint8_t p) noexcept {
	switch (p) {
		case 0b00: return BC;
		case 0b01: return DE;
		case 0b10: return HL;
		default: return SP;
	}
}

/**
 * Sets (or unsets) the given flag/s in F.
 */
inline void CPU::setFlag(std::uint16_t flag, bool set) noexcept {
	if (set){
		AF |= flag;
	} else {
		AF &= ~flag;
	}
}

/**
 * Loads data into register.
 */
inline void CPU::loadReg(std::uint8_t high, std::uint8_t low, std::uint16_t &reg){
	reg = static_cast<std::uint16_t>((high << 8) | low);
}

/**
 * Stores register into memory.
 */
inline void CPU::storeReg(std::uint8_t value, std::uint16_t loc){
	memory[loc] = value;
}

/**
 * Increments registers.
 * \param mode determines if higher/lower/both register/s are incremented.
 */
inline void CPU::incReg(int amount, std::uint16_t &reg, Mode mode){
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
inline void CPU::incMem(int amount, std::uint16_t loc){
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
inline void CPU::rotate(std::uint16_t &regPair, bool useCarry, Direction d, Mode mode){
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
inline void CPU::addPairs(std::uint16_t &target, std::uint16_t value){
	setFlag(nFlag, false); // Unset negative flag
	constexpr std::uint16_t halfMask = 0x0FFF; // Mask for first 12 bits of registers
	setFlag(hFlag, (target & halfMask) + (value & halfMask) > halfMask); // Checking for half Carry-over (bit 11 carry)
	setFlag(cFlag, target + value > 0xFFFF); // Checking for carry-over
	target = static_cast<std::uint16_t>(target + value);
}

#endif
