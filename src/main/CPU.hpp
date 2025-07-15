#ifndef CPU_HPP
#define CPU_HPP

#include <iostream>
#include <fstream>
#include <cstdlib>

/*
FLAGS REGISTER FOR LOWER 8 BITS OF AF.
\param 7:zFlag 	zero flag
\param 6:nFlag 	subtraction flag (BCD)
\param 5:hFlag 	half carry flag (BCD)
\param 4:cFlag 	carry flag
*/
#define zFlag	0b0000000010000000
#define nFlag	0b0000000001000000
#define hFlag	0b0000000000100000
#define cFlag	0b0000000000010000

/**
 * MODE determines which register is chosen.
 * \param HIGH Higher register.
 * \param LOW lower register.
 * \param PAIR both registers.
 */
enum MODE {HIGH, LOW, PAIR};

enum DIRECTION {LEFT, RIGHT};

class CPU {
public:
	// Registers
	unsigned short AF; // Accumulator & flags
	unsigned short BC; // BC
	unsigned short DE; // DE
	unsigned short HL; // HL
	unsigned short SP; // Stack pointer
	unsigned short PC; // Program counter

	// Getters for individual registers
	inline unsigned char A() const { return (AF & 0xFF00) >> 8;}
	inline unsigned char F() const { return ( AF & 0x00FF);}
	inline unsigned char B() const { return ( (BC & 0xFF00) >> 8);}
	inline unsigned char C() const { return ( BC & 0x00FF);}
	inline unsigned char D() const { return ( (DE & 0xFF00) >> 8);}
	inline unsigned char E() const { return ( DE & 0x00FF);}
	inline unsigned char H() const { return ( (HL & 0xFF00) >> 8);}
	inline unsigned char L() const { return ( HL & 0x00FF);}

	// Memory
	unsigned char memory[465535]; // 16 bit address bus

	unsigned short opcode;

	void initialize();

	void executeOpcode(short opcode);
	void loadReg(unsigned char high, unsigned char low, unsigned short &reg);
	void storeReg(unsigned char reg, unsigned short loc);
	void incReg(int amount, unsigned short &reg, MODE mode);
	void rotate(unsigned short &regPair, bool carry, DIRECTION d, MODE mode);
	void addPairs(unsigned short &storeReg, unsigned short &reg);
};

void CPU::initialize(){
	AF = 0;
	BC = 0;
	DE = 0;
	HL = 0;
	SP = 0;
	PC = 0x00; // Stating point of ROM
}

void CPU::executeOpcode(short input){
	opcode = input;

	unsigned char x = (opcode & 0b11000000) >> 6; // 1st octal digit
	unsigned char y = (opcode & 0b00111000) >> 3; // 2nd octal digit
	unsigned char z = opcode & 0b00000111; // 3rd octal digit
	unsigned char p = (opcode & 0b00110000) >> 4; // y rightshifted by 1 (bits 5-4 of y)
	unsigned char q = (opcode & 0b00001000) >> 3; // y mod 2 (bit 3 of y)

	switch(opcode & 0xFF00){
		case 0x0000: // 8-bit opcodes
			if (opcode <= 0x3F){ // First 4 rows of opcodes
				unsigned short *ddReg; // Register pair targets
				switch (p) {
					case 0b00: ddReg = &BC; break;
					case 0b01: ddReg = &DE; break;
					case 0b10: ddReg = &HL; break;
					case 0b11: ddReg = &SP; break;
				}
				switch(opcode & 0x000F){
					case 0x00: PC++; break;
					case 0x01: loadReg(memory[PC+2], memory[PC+1], *ddReg); PC+= 3; break;
					case 0x02: storeReg(A(), BC); PC++; break;
					case 0x03: incReg(1, *ddReg, PAIR); PC++; break;
					case 0x04: incReg(1, BC, HIGH); PC++; break;
					case 0x05: incReg(-1, BC, HIGH); PC++; break;
					case 0x06: loadReg(memory[PC+1], C(), BC);PC+=2; break;
					case 0x07: rotate(AF, true, LEFT, HIGH); PC++; break;
					case 0x08: {
						unsigned short nn = (memory[PC + 1] << 8)| memory[PC];
						storeReg((SP & 0x00FF), PC);
						storeReg(((SP & 0xFF00) >> 8), PC + 1);
						PC += 3; 
						break;
					}
					case 0x09: addPairs(HL, *ddReg); PC++; break;
					case 0x0A: loadReg(memory[BC], (AF & 0xFF), AF); PC++; break;
					case 0x0B: incReg(-1, BC, PAIR); PC++; break;
					case 0x0C: incReg(1, BC, LOW); PC++; break;
					case 0x0D: incReg(-1, BC, LOW); PC++; break;
					case 0x0E: loadReg(B(), memory[PC+1], BC); PC+=2; break;
					case 0x0F: rotate(AF, true, RIGHT, HIGH); PC++; break;
					default: printf("Unknown opcode: 0x%X\n", opcode); break;
				}
			}
			break;

		case 0xCB00: // 16-bit opcodes

			break;

		default: printf("Unknown opcode: 0x%X\n", opcode); break;	
	}
}

/**
 * Loads data into register.
 */
void CPU::loadReg(unsigned char high, unsigned char low, unsigned short &reg){
	reg = (high << 8) | low;
}

/**
 * Stores register into memory.
 */
void CPU::storeReg(unsigned char reg, unsigned short loc){
	memory[loc] = reg;
}

/**
 * Increments registers.
 * \param mode determines if higher/lower/both register/s are incremented.
 */
void CPU::incReg(int amount, unsigned short &reg, MODE mode){
	if (mode == PAIR){
		reg += amount;
	} else {
		int shift = (mode == HIGH)? 8:0; // Shift of 8 if on the higher bit; 0 otherwise
		unsigned char halfReg = (reg >> shift) & 0x00FF; // Shifting register pair then masking the lower register to get the register
		if (amount > 0){
			if (halfReg == 0xFF) {
				AF |= (zFlag | hFlag); // set zero / half carry
			} else {
				AF &= ~(zFlag | hFlag); // unset otherwise
			}
			AF &= ~nFlag; // Unset nFlag
		} else {
			AF &= ~ (zFlag | hFlag); // unset both flags temporarily
			if (halfReg == 1){
				AF |= zFlag;
			} else  if (halfReg == 0){
				AF |= hFlag;
			}
			AF |= nFlag; // set nFlag
		}
		halfReg += amount;
		unsigned short mask = (mode == HIGH)? 0x00FF:0xFF00; // Mask for other register
		reg = (halfReg << shift) | (reg & mask);
	}
}

/**
 * Rotate register `LEFT` or `RIGHT`.
 * 
 * \param reg Target register pair.
 * \param useCarry True if uses carry for rotation, false if only last bit is copied to carry.
 * \param d Rotate `LEFT` or `RIGHT`.
 * \param mode Register that is rotated.
 */
void CPU::rotate(unsigned short &regPair, bool useCarry, DIRECTION d, MODE mode){
	if (mode == PAIR){
		// TODO: rotate pair
	} else {
		int shift = (mode == HIGH)? 8:0; // Shift of 8 if on the higher bit; 0 otherwise
		unsigned char reg = (regPair >> shift) & 0x00FF; // Shifting register pair then masking the lower register to get the register
		unsigned char newReg;
		if(d == LEFT){ // LEFT rotation
			newReg = reg << 1;
			newReg = (newReg & 0b11111110) | (reg & 0b10000000) >> 7; // first bit is set to last of original
			if ((reg & 0b10000000) >> 7){
				AF |= cFlag;
			} else {
				AF &= ~cFlag;
			}

		} else { // RIGHT rotation
			newReg = reg >> 1;
			newReg = (reg & 0x01) << 7 | (newReg & 0b01111111);
			if (reg & 0x01){
				AF |= cFlag;
			} else {
				AF &= ~cFlag;
			}
		}
		AF &= ~(zFlag | nFlag | hFlag);
		regPair = (newReg << shift) | regPair & ((shift==8)?0x00FF:0xFF00); //other (unchanged) register pair is opposite mask of rotated register
	}
}

/**
 * Adds registers `storeReg` and  `reg` and stores result in `storeReg`.
 */
void CPU::addPairs(unsigned short &storeReg, unsigned short &reg){
	AF &= ~nFlag; // Unset negative flag
	unsigned short halfMask = 0b111111111111; // Mask for first 11 bits of registers
	if ((storeReg & halfMask) + (reg & halfMask) > halfMask){ // Checking for half Carry-over (bit 11 carry)
		AF |= hFlag;
	} else {
		AF &= ~hFlag;
	}
	if (storeReg + reg > 0xFFFF){ // Checking for carry-over
		AF |= cFlag;
	} else {
		AF &= ~cFlag;
	}
	storeReg = storeReg + reg;
}

#endif