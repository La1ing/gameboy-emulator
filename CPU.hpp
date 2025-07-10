#ifndef CPU_HPP
#define CPU_HPP

#include <iostream>
#include <fstream>
#include <cstdlib>

/*
FLAGS REGISTER FOR LOWER 8 BITS OF AF
7: zFlag | zero flag
6: nFlag | subtraction flag (BCD)
5: hFlag | half carry flag (BCD)
4: cFlag | carry flag
*/
#define zFlag	AF & 0b000000001000000
#define nFlag	AF & 0b000000000100000
#define hFlag	AF & 0b000000000010000
#define nFlag	AF & 0b000000000001000


/*
Individual register masks
*/
#define A	(unsigned char) ((AF & 0xFF00) >> 8)
#define F	(unsigned char) AF & 0x00FF
#define B	(unsigned char) ((BC & 0xFF00) >> 8)
#define C	(unsigned char) BC & 0x00FF
#define D	(unsigned char) ((DE & 0xFF00) >> 8)
#define E	(unsigned char) DE & 0x00FF
#define H	(unsigned char) ((HL & 0xFF00) >> 8)
#define L	(unsigned char) HL & 0x00FF

/**
 * MODE determines which register is chosen
 * \param HIGH Higher register
 * \param LOW lower regier
 * \param PAIR both registers
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

	// Memory
	unsigned char memory[465535]; // 16 bit address bus

	unsigned short opcode;

	void initialize();

	void executeOpcode(short opcode);
	void loadReg(unsigned char high, unsigned char low, unsigned short &reg);
	void storeReg(unsigned char reg, unsigned short loc);
	void incReg(int amount, unsigned short &reg, MODE mode);
	void rotate(unsigned short &reg, bool carry, DIRECTION d);
};

void CPU::initialize(){
	AF = 0;
	BC = 0;
	DE = 0;
	HL = 0;
	SP = 0;
	PC = 0x00; // Stating point of ROM
}

void CPU::executeOpcode(short opcode){
	char x = opcode & 0b11000000; // 1st octal digit
	char y = opcode & 0b00111000; // 2nd octal digit
	char z = opcode & 0b00000111; // 3rd octal digit
	char p = opcode & 0b00110000; // y rightshifted by 1 (bits 5-4 of y)
	char q = opcode & 0b00001000; // y mod 2 (bit 3 of y)

	switch(opcode & 0xFF00){
		case 0x0000: // 8-bit opcodes
			switch(opcode & 0x00FF){
				case 0x00: PC++; break;
				case 0x01: loadReg(memory[PC+2], memory[PC+1], BC); PC+= 3; break;
				case 0x02: storeReg(A, BC); PC++; break;
				case 0x03: incReg(1, BC, PAIR); PC++; break;
				case 0x04: incReg(1, BC, HIGH); PC++; break;
				case 0x05: incReg(-1, BC, HIGH); PC++; break;
				case 0x06: loadReg(memory[PC+1], (BC&0x00FF), BC);PC+=2; break;
				case 0x07: PC+=1; break;

				default: printf("Unknown opcode: 0x%X\n", opcode); break;
			}
			break;

		case 0xCB00: // 16-bit opcodes

			break;

		default: printf("Unknown opcode: 0x%X\n", opcode); break;	
	}
}

/**
 * Loads data into register
 */
void CPU::loadReg(unsigned char low, unsigned char high, unsigned short &reg){
	reg = (high << 8) | low;
}

/**
 * Stores register into memory
 */
void CPU::storeReg(unsigned char reg, unsigned short loc){
	memory[loc] = reg;
}

/**
 * Increments registers
 * \param mode determines if higher/lower/both register/s are incremented
 */
void CPU::incReg(int amount, unsigned short &reg, MODE mode){
	if (mode == PAIR){
		reg += amount;
	} else {
		unsigned short mask = (mode == HIGH)? 0xFF00:0x00FF;
		unsigned char halfReg = reg & mask;
		if (amount > 0){
			AF |= (halfReg == 0xFF)? zFlag: 0;
			AF &= ~nFlag; // Unset nFlag
			AF |= (halfReg == 0xFF)? hFlag: 0;
		} else {
			AF |= (halfReg == 1)? zFlag: 0;
			AF |= nFlag; // set nFlag
			AF |= (halfReg == 0)? hFlag: 0;
		}
		halfReg += amount;
	}
}

void rotate(unsigned short &reg, bool carry, DIRECTION d){
	// CURRENTLY ONLY IMPLEMENTATION OF RLCA
	unsigned char regA = AF &

}

#endif