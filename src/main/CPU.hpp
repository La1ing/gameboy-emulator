#ifndef CPU_HPP
#define CPU_HPP

#include <iostream>
#include <fstream>
#include <cstdlib>

/*
FLAGS REGISTER FOR LOWER 8 BITS OF AF
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
	void rotate(unsigned short &reg, bool carry, DIRECTION d);
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
				case 0x02: storeReg(A(), BC); PC++; break;
				case 0x03: incReg(1, BC, PAIR); PC++; break;
				case 0x04: incReg(1, BC, HIGH); PC++; break;
				case 0x05: incReg(-1, BC, HIGH); PC++; break;
				case 0x06: loadReg(memory[PC+1], (BC&0x00FF), BC);PC+=2; break;
				case 0x07: rotate(AF, true, LEFT); PC+=1; break;
				case 0x08: {
					unsigned short nn = (memory[PC + 1] << 8)| memory[PC];
					storeReg((SP & 0x00FF), nn);
					storeReg(((SP & 0xFF00) >> 8), (nn + 1));
					PC += 3; 
					break;
				}
				case 0x09: addPairs(HL, BC); PC++; break;
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
void CPU::loadReg(unsigned char high, unsigned char low, unsigned short &reg){
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
		int shift = (mode == HIGH)? 8:0; // Shift of 8 if on the higher bit; 0 otherwise
		unsigned char halfReg = (reg >> shift) & 0x00FF; // Shifting register pair then masking the lower register to get the register
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
		unsigned short mask = (mode == HIGH)? 0x00FF:0xFF00; // Mask for other register
		reg = (halfReg << shift) | (reg & mask);
		if (mode == HIGH) {
			reg = (halfReg << 8) | (reg & 0x00FF);
		} else {
			reg = (reg & 0x00FF) | halfReg;
		}
	}
}

void CPU::rotate(unsigned short &reg, bool carry, DIRECTION d){
	// CURRENTLY ONLY IMPLEMENTATION OF RLCA
	unsigned char regA =A();
	unsigned char newRegA = regA << 1;
	newRegA |=  (regA & 0b10000000) >> 7;
	if ((regA & 0b10000000) >> 7 == 1){
		AF |= cFlag;
	} else {
		AF &= ~cFlag;
	}
	AF &= ~(zFlag | nFlag | hFlag);
	AF = (newRegA << 8) | F(); 
}

void CPU::addPairs(unsigned short &storeReg, unsigned short &reg){
	AF &= ~nFlag; // Unset negative flag
	if ((storeReg & 0x0B) + (reg & 0x0B) > 0x0B){
		AF |= hFlag;
	} else {
		AF &= ~hFlag;
	}
	if ((storeReg & 0x0F) + (reg & 0x0F) > 0x0F){
		AF |= cFlag;
	} else {
		AF &= ~cFlag;
	}
	storeReg = storeReg + reg;
}

#endif