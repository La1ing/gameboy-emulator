#ifndef CPU_HPP
#define CPU_HPP

#include <array>
#include <cstdint>

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

	/**
	 * Fetches the opcode at PC (and the byte after a 0xCB prefix) and executes it.
	 * \return M-cycles taken.
	 */
	int step();

	/**
	 * Executes `opcode`; its operands are read from memory after PC.
	 * CB-prefixed opcodes are passed as 0xCBxx.
	 * \return M-cycles taken.
	 */
	int executeOpcode(std::uint16_t opcode);

private:
	// Opcode blocks, selected by bits 7-6 of the opcode (x = 1st octal digit)
	int execBlock0(std::uint8_t opcode);
	int execBlock1(std::uint8_t opcode);
	int execBlock2(std::uint8_t opcode);
	int execBlock3(std::uint8_t opcode);
	int execCB(std::uint8_t opcode);
	int unimplemented(std::uint16_t opcode);

	// Operand helpers
	[[nodiscard]] std::uint16_t &pairReg(std::uint8_t p) noexcept;
	[[nodiscard]] std::uint8_t getR8(std::uint8_t idx) const noexcept;
	void setR8(std::uint8_t idx, std::uint8_t value) noexcept;
	[[nodiscard]] std::uint16_t read16(std::uint16_t addr) const noexcept;
	void push16(std::uint16_t value) noexcept;
	[[nodiscard]] std::uint16_t pop16() noexcept;
	[[nodiscard]] bool cond(std::uint8_t cc) const noexcept;
	void setFlag(std::uint16_t flag, bool set) noexcept;

	// Operations
	void loadReg(std::uint8_t high, std::uint8_t low, std::uint16_t &reg);
	void storeReg(std::uint8_t value, std::uint16_t loc);
	void incReg(int amount, std::uint16_t &reg, Mode mode);
	void incMem(int amount, std::uint16_t loc);
	void rotate(std::uint16_t &regPair, bool useCarry, Direction d, Mode mode);
	void addPairs(std::uint16_t &target, std::uint16_t value);
};

#endif
