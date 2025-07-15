
#define CATCH_CONFIG_MAIN
#include "catch.hpp"
#include "../main/CPU.hpp"

struct CPUTest{
    CPU cpu;
    CPUTest(){
        cpu.initialize();
    }
};

TEST_CASE_METHOD(CPUTest, "0x00:(NOP) correctly increment PC") {
    cpu.executeOpcode(0x0000);
    REQUIRE(cpu.PC == 0x0001);
}

TEST_CASE_METHOD(CPUTest, "0x01:(LB BC, u16) load 2 bytes of immediate data") {
    cpu.PC = 0x00; // Start PC at 0
    cpu.memory[0x01] = 0x37;
    cpu.memory[0x02] = 0x13;
    cpu.executeOpcode(0x0001);
    REQUIRE(cpu.BC == 0x1337);
    REQUIRE(cpu.PC == 0x03);
}

TEST_CASE_METHOD(CPUTest, "0x02:(LD BC, A) register stored in correct location in memory") {
    // Setting BC to 0x205F and A to 0x3F
    cpu.BC = 0x205F;
    cpu.AF = (0x3F << 8) | cpu.F();
    cpu.executeOpcode(0x0002);
    REQUIRE(cpu.memory[0x205F] == 0x3F);
    REQUIRE(cpu.PC == 0x0001);
}

TEST_CASE_METHOD(CPUTest, "0x03:(LB (BC), A) increment BC NO overflow") {
    cpu.AF = 0x0000;
    cpu.BC = 0x000F;
    cpu.executeOpcode(0x03);
    REQUIRE(cpu.BC == 0x0010);
    REQUIRE(cpu.AF == 0x0000); // No flags are modified
    REQUIRE(cpu.PC == 0x0001);
}

TEST_CASE_METHOD(CPUTest, "0x03:(INC BC) increment B w/ overflow") {
    cpu.AF = 0x0000;
    cpu.BC = 0xFFFF;
    cpu.executeOpcode(0x0003);
    REQUIRE(cpu.BC == 0x0000);
    REQUIRE(cpu.AF == 0x0000); // No flags are modified
    REQUIRE(cpu.PC == 0x0001);
}

TEST_CASE_METHOD(CPUTest, "0x04:(INC B) increment B NO overflow") {
    cpu.AF = nFlag; // Setting AF as negative flag
    cpu.BC = 0x2000; // Setting B register as 0x20
    cpu.executeOpcode(0x0004);
    REQUIRE(cpu.BC == 0x2100);
    REQUIRE(cpu.AF == 0b00000000); // Negative flag is unset
    REQUIRE(cpu.PC == 0x0001);
}

TEST_CASE_METHOD(CPUTest, "0x04:(INC B) increment B w/ overflow") {
    cpu.AF = nFlag; // Setting AF as negative flag
    cpu.BC = 0xFF00; // Setting B register as 0x20
    cpu.executeOpcode(0x0004);
    REQUIRE(cpu.BC == 0x0000);
    REQUIRE(cpu.AF == 0b0000000010100000); // Flags: SET zero, HC | UNSET negative
    REQUIRE(cpu.PC == 0x0001);
}

TEST_CASE_METHOD(CPUTest, "0x05:(DEC B) decrement B NO overflow (zero flag)") {
    cpu.AF = hFlag; // Setting AF as half carry flag
    cpu.BC = 0x0100; // Setting B register as 0x01
    cpu.executeOpcode(0x0005);
    REQUIRE(cpu.BC == 0x0000);
    REQUIRE(cpu.AF == 0b0000000011000000); // Flags: SET zero, negative | UNSET HC
    REQUIRE(cpu.PC == 0x0001);
}

TEST_CASE_METHOD(CPUTest, "0x05:(DEC B) decrement B w/ overflow") {
    cpu.AF = zFlag; // Setting AF as zero flag
    cpu.BC = 0x0000; // Setting B register as 0x01
    cpu.executeOpcode(0x0005);
    REQUIRE(cpu.BC == 0xFF00);
    REQUIRE(cpu.AF == 0b0000000001100000); // Flags: SET negative, HC | UNSET zero
    REQUIRE(cpu.PC == 0x0001);
}

TEST_CASE_METHOD(CPUTest, "0x06:(LD B, u8) Bloading 8-bit immediate data into B") {
    cpu.PC = 0x00; // Start PC at 0
    cpu.memory[0x01] = 0x24;
    cpu.executeOpcode(0x0006);
    REQUIRE(cpu.BC == 0x2400);
    REQUIRE(cpu.PC == 0x0002);
}

TEST_CASE_METHOD(CPUTest, "0x07:(RLCA) rotating register A left") {
    cpu.AF = 0x8500 | zFlag | nFlag | hFlag ; // SetA at 0x85 (0b10000101), carry flag unset in F & other flags set
    cpu.executeOpcode(0x0007);
    REQUIRE(cpu.AF == (0x0B00 | cFlag)); // Register A = 0x0B (0b00001011) | carry flag set w/ rest unset
    REQUIRE(cpu.PC == 1);
}

TEST_CASE_METHOD(CPUTest, "0x08:(LD (u16), SP) store SP into memory in u16 address") {
    cpu.SP = 0xFFF8;
    cpu.PC = 0xC100; // starting program counter from C100
    cpu.executeOpcode(0x0008);
    REQUIRE(cpu.memory[0xC100] == 0xF8);
    REQUIRE(cpu.memory[0xC101] == 0xFF);
    REQUIRE(cpu.PC == 0xC103);
}

TEST_CASE_METHOD(CPUTest, "0x09:(ADD HL, BC), check half carry (@bit 11)") {
    cpu.AF = nFlag | cFlag; // Setting negative and carry flag
    cpu.HL = 0x8A23;
    cpu.BC = 0x0605;
    cpu.executeOpcode(0x0009);
    REQUIRE(cpu.HL == 0x9028); // Sum is stored in HL
    REQUIRE(cpu.BC == 0x0605); // BC remains unchanged
    REQUIRE(cpu.AF == hFlag); // Flags: SET HC | UNSET negative, carry
    REQUIRE(cpu.PC == 0x0001);
}

TEST_CASE_METHOD(CPUTest, "0x09:(ADD HL, BC), check full carry") {
    cpu.AF = nFlag; // Setting negative flag
    cpu.HL = 0x8A23;
    cpu.BC = 0x8A23;
    cpu.executeOpcode(0x0009);
    REQUIRE(cpu.HL == 0x1446); // Sum is stored in HL
    REQUIRE(cpu.BC == 0x8A23); // BC remains unchanged
    REQUIRE(cpu.AF == (hFlag | cFlag)); // Flags: SET HC, carrt | UNSET negative
    REQUIRE(cpu.PC == 0x0001);
}

TEST_CASE_METHOD(CPUTest, "0x0A:(LD A, (BC)) loading memory[BC] into A") {
    cpu.BC = 0x0011;
    cpu.memory[cpu.BC] = 0x5C;
    cpu.executeOpcode(0x000A);
    REQUIRE(cpu.AF == 0x5C00); // Register A = 0x2B (0b00101011) | carry flag set w/ rest unset
    REQUIRE(cpu.PC == 1);
} 

TEST_CASE_METHOD(CPUTest, "0x0B:(DEC BC) decrement pair BC") {
    cpu.BC = 0x235F;
    cpu.executeOpcode(0x000B);
    REQUIRE(cpu.BC == 0x235E);
    REQUIRE(cpu.AF == 0x0000); // no flags set
    REQUIRE(cpu.PC == 1);
} 

TEST_CASE_METHOD(CPUTest, "0x0C:(INC C) increment register C") {
    cpu.BC = 0x23FF;
    cpu.executeOpcode(0x000C);
    REQUIRE(cpu.BC == 0x2300);
    REQUIRE(cpu.AF == (zFlag | hFlag)); // set: zero and half-carry flag
    REQUIRE(cpu.PC == 1);
} 

TEST_CASE_METHOD(CPUTest, "0x0D:(DEC C) decrement register C") {
    cpu.BC = 0x4201;
    cpu.executeOpcode(0x000D);
    REQUIRE(cpu.BC == 0x4200);
    REQUIRE(cpu.AF == (zFlag | nFlag)); // set: zero and negative flag
    REQUIRE(cpu.PC == 1);
} 

TEST_CASE_METHOD(CPUTest, "0x0E:(LD C, d8) load immediate data into reg C") {
    cpu.PC = 0x05;
    cpu.BC = 0x0000;
    cpu.memory[cpu.PC + 1] = 0x24;
    cpu.executeOpcode(0x000E);
    REQUIRE(cpu.BC == 0x0024);
    REQUIRE(cpu.PC == 7);
} 

TEST_CASE_METHOD(CPUTest, "0x0F:(RRCA) rotating register A right") {
    cpu.AF = 0x3B00 | zFlag | nFlag | hFlag | cFlag; // SetA at 0x95 (0b10010101), all flags set
    cpu.executeOpcode(0x000F);
    REQUIRE(cpu.AF == (0x9D00 | cFlag)); // Register A = 0x2B (0b00101011) | carry flag set w/ rest unset
    REQUIRE(cpu.PC == 1);
}
