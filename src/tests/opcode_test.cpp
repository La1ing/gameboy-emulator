
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

TEST_CASE_METHOD(CPUTest, "0x*1:(LD dd, u16) load 2 bytes of immediate data") {
    cpu.PC = 0x00; // Start PC at 0
    // load data for BC
    cpu.memory[0x01] = 0x37;
    cpu.memory[0x02] = 0x13;
    // load data for DE
    cpu.memory[0x04] = 0xBC;
    cpu.memory[0x05] = 0xFC;
    // load data for HL
    cpu.memory[0x07] = 0xCD;
    cpu.memory[0x08] = 0xAB;
    // load data for SP
    cpu.memory[0x0A] = 0x27;
    cpu.memory[0x0B] = 0x72;
    cpu.executeOpcode(0x0001); // BC
    cpu.executeOpcode(0x0011); // DE
    cpu.executeOpcode(0x0021); // HL
    cpu.executeOpcode(0x0031); // SP
    REQUIRE(cpu.BC == 0x1337);
    REQUIRE(cpu.DE == 0xFCBC);
    REQUIRE(cpu.HL == 0xABCD);
    REQUIRE(cpu.SP == 0x7227);
    REQUIRE(cpu.PC == 0x0C);
}

TEST_CASE_METHOD(CPUTest, "0x02:(LD (BC), A) register stored in correct location in memory") {
    // Setting BC to 0x205F and A to 0x3F
    cpu.BC = 0x205F;
    cpu.AF = (0x3F << 8) | cpu.F();
    cpu.executeOpcode(0x0002);
    REQUIRE(cpu.memory[0x205F] == 0x3F);
    REQUIRE(cpu.PC == 0x0001);
}

TEST_CASE_METHOD(CPUTest, "0x*2:(LD (dd), A) register stored in correct location in memory") {
    cpu.DE = 0x207F;
    cpu.HL = 0xFFFF;
    cpu.AF = 0x3F00;
    cpu.executeOpcode(0x0012);
    REQUIRE(cpu.memory[0x207F] == 0x3F);
    cpu.AF = 0x5600;
    cpu.executeOpcode(0x0022);
    REQUIRE(cpu.memory[0xFFFF] == 0x56);
    REQUIRE(cpu.HL == 0x00);
    cpu.AF = 0x4200;
    cpu.HL = 0x4000;
    cpu.executeOpcode(0x0032);
    REQUIRE(cpu.memory[0x4000] == 0x42);
    REQUIRE(cpu.HL == 0x3FFF);
    REQUIRE(cpu.PC == 0x0003);
}

TEST_CASE_METHOD(CPUTest, "0x*3:(LB (dd), A) increment dd") {
    cpu.AF = 0x0000;
    cpu.BC = 0x000F;
    cpu.DE = 0xFFFF;
    cpu.HL = 0x9090;
    cpu.SP = 0xAFAD;
    cpu.executeOpcode(0x03);
    cpu.executeOpcode(0x013);
    cpu.executeOpcode(0x23);
    cpu.executeOpcode(0x33);
    REQUIRE(cpu.BC == 0x0010);
    REQUIRE(cpu.DE == 0x0000);
    REQUIRE(cpu.HL == 0x9091);
    REQUIRE(cpu.SP == 0xAFAE);
    REQUIRE(cpu.AF == 0x0000); // No flags are modified
    REQUIRE(cpu.PC == 0x0004);
}

TEST_CASE_METHOD(CPUTest, "0x03:(INC BC) increment B w/ overflow") {
    cpu.AF = 0x0000;
    cpu.BC = 0xFFFF;
    cpu.executeOpcode(0x0003);
    REQUIRE(cpu.BC == 0x0000);
    REQUIRE(cpu.AF == 0x0000); // No flags are modified
    REQUIRE(cpu.PC == 0x0001);
}

TEST_CASE_METHOD(CPUTest, "0x*4:(INC r) increment r") {
    cpu.AF = nFlag; // Setting AF as negative flag
    cpu.BC = 0x2000; // Setting B register as 0x20
    cpu.executeOpcode(0x0004);
    REQUIRE(cpu.BC == 0x2100);
    REQUIRE(cpu.AF == 0b00000000); // Negative flag is unset
    REQUIRE(cpu.PC == 0x0001);
    cpu.DE = 0x0000;
    cpu.executeOpcode(0x14);
    REQUIRE(cpu.DE == 0x0100);
    REQUIRE(cpu.AF == 0x0000);
    cpu.HL = 0xA600;
    cpu.executeOpcode(0x24);
    REQUIRE(cpu.HL == 0xA700);
    REQUIRE(cpu.AF == 0x0000); 
    cpu.memory[0xA700] = 0xFF;
    cpu.executeOpcode(0x34);
    REQUIRE(cpu.memory[0xA700] == 0x00);
    REQUIRE(cpu.AF == (zFlag | hFlag));
    cpu.executeOpcode(0x34);
    REQUIRE(cpu.memory[0xA700] == 0x01);
    REQUIRE(cpu.AF == 0x0000);
    REQUIRE(cpu.PC == 0x0005);
}

TEST_CASE_METHOD(CPUTest, "0x04:(INC B) increment B w/ overflow") {
    cpu.AF = nFlag; // Setting AF as negative flag
    cpu.BC = 0xFF00; // Setting B register as 0x20
    cpu.executeOpcode(0x0004);
    REQUIRE(cpu.BC == 0x0000);
    REQUIRE(cpu.AF == 0b0000000010100000); // Flags: SET zero, HC | UNSET negative
    REQUIRE(cpu.PC == 0x0001);
}

TEST_CASE_METHOD(CPUTest, "0x*5:(DEC r) decrement r") {
    cpu.AF = hFlag; // Setting AF as half carry flag
    cpu.BC = 0x0100; // Setting B register as 0x01
    cpu.executeOpcode(0x0005);
    REQUIRE(cpu.BC == 0x0000);
    REQUIRE(cpu.AF == 0b0000000011000000); // Flags: SET zero, negative | UNSET HC
    REQUIRE(cpu.PC == 0x0001);
    cpu.DE = 0x2200;
    cpu.executeOpcode(0x0015);
    REQUIRE(cpu.DE == 0x2100);
    REQUIRE(cpu.AF == nFlag);
    cpu.HL = 0x7600;
    cpu.executeOpcode(0x0025);
    REQUIRE(cpu.HL == 0x7500);  
    cpu.memory[0x7500] = 0x01;
    cpu.executeOpcode(0x0035);
    REQUIRE(cpu.memory[0x7500] == 0x00);
    REQUIRE(cpu.AF == (zFlag | nFlag));
    cpu.executeOpcode(0x0035);
    REQUIRE(cpu.memory[0x7500] == 0xFF);
    REQUIRE(cpu.AF == (nFlag | hFlag));
    REQUIRE(cpu.PC == 0x0005);
}

TEST_CASE_METHOD(CPUTest, "0x05:(DEC B) decrement B w/ overflow") {
    cpu.AF = zFlag; // Setting AF as zero flag
    cpu.BC = 0x0000; // Setting B register as 0x01
    cpu.executeOpcode(0x0005);
    REQUIRE(cpu.BC == 0xFF00);
    REQUIRE(cpu.AF == 0b0000000001100000); // Flags: SET negative, HC | UNSET zero
    REQUIRE(cpu.PC == 0x0001);
}

TEST_CASE_METHOD(CPUTest, "0x*6:(LD r, u8) Bloading 8-bit immediate data into r") {
    cpu.PC = 0x00; // Start PC at 0
    cpu.memory[0x01] = 0x24;
    cpu.executeOpcode(0x0006);
    REQUIRE(cpu.BC == 0x2400);
    REQUIRE(cpu.PC == 0x0002);
    cpu.memory[0x03] = 0x25;
    cpu.executeOpcode(0x0016);
    REQUIRE(cpu.BC == 0x2500);
    cpu.memory[0x05] = 0x26;
    cpu.executeOpcode(0x0026);
    REQUIRE(cpu.BC == 0x2600);
    cpu.memory[0x07] = 0x28;
    cpu.HL = 0x0020;
    cpu.executeOpcode(0x0036);
    REQUIRE(cpu.memory[0x020] == 0x28);
    REQUIRE(cpu.PC == 0x0008);
}

TEST_CASE_METHOD(CPUTest, "0x07:(RLCA) rotating register A left") {
    cpu.AF = 0x8500 | zFlag | nFlag | hFlag ; // SetA at 0x85 (0b10000101), carry flag unset in F & other flags set
    cpu.executeOpcode(0x0007);
    REQUIRE(cpu.AF == (0x0B00 | cFlag)); // Register A = 0x0B (0b00001011) | carry flag set w/ rest unset
    REQUIRE(cpu.PC == 1);
}

TEST_CASE_METHOD(CPUTest, "0x17:(RLA) rotating register A left") {
    cpu.AF = 0x1500 | zFlag | nFlag | hFlag | cFlag; // SetA at 0x85 (0b10000101), all flags set
    cpu.executeOpcode(0x0017);
    REQUIRE(cpu.AF == 0x2B00); // Register A = 0x0B (0b00001011) | all flags unset
    REQUIRE(cpu.PC == 1);
}

TEST_CASE_METHOD(CPUTest, "0x37:Set carry flag") {
    cpu.AF = zFlag | nFlag | hFlag; // Set zero, negative and HC flags, unset carry flag
    cpu.executeOpcode(0x0037);
    REQUIRE(cpu.AF == (zFlag | cFlag)); // flags: zero unchanged | unset negative/HC | set carry
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

TEST_CASE_METHOD(CPUTest, "0x18:(JR e) jump e steps") {
    cpu.PC = 0x0000;
    cpu.memory[cpu.PC + 1] = 0b01111111; // 127
    cpu.executeOpcode(0x18);
    REQUIRE(cpu.PC == 129); // Added by 129 (127 + 2)
    cpu.memory[130] = 0b10000000; // -128
    cpu.executeOpcode(0x18);
    REQUIRE(cpu.PC == 3); // Subtracted by 126 (-128 + 2)
}

TEST_CASE_METHOD(CPUTest, "0x28/38:(JR cc, e) jump e steps after condition cc") {
    cpu.PC = 0x0000;
    cpu.memory[cpu.PC + 1] = 0x01; // 1
    cpu.AF = 0x0000; // All flags unset
    cpu.executeOpcode(0x28);
    REQUIRE(cpu.PC == 2); // Jumps by 2 instead of 3 as z flag not set
    cpu.AF = zFlag;
    cpu.memory[2 + 1] = 0x01;
    cpu.executeOpcode(0x28);
    REQUIRE(cpu.PC == 5);

    cpu.PC = 0x0000; // reset PC
    cpu.memory[cpu.PC + 1] = 0x02; // 1
    cpu.AF = 0x0000; // All flags unset
    cpu.executeOpcode(0x38);
    REQUIRE(cpu.PC == 2); // Jumps by 2 instead of 3 as z flag not set
    cpu.AF = cFlag;
    cpu.memory[2 + 1] = 0x02;
    cpu.executeOpcode(0x38);
    REQUIRE(cpu.PC == 6);
}

TEST_CASE_METHOD(CPUTest, "0x*9:(ADD HL, dd), check half carry (@bit 11)") {
    cpu.AF = nFlag | cFlag; // Setting negative and carry flag
    cpu.HL = 0x8A23;
    cpu.BC = 0x0605;
    cpu.DE = 0x0FFF;
    cpu.SP = 0x0001;
    cpu.executeOpcode(0x0009);
    REQUIRE(cpu.HL == 0x9028); // Sum is stored in HL
    REQUIRE(cpu.BC == 0x0605); // BC remains unchanged
    REQUIRE(cpu.AF == hFlag); // Flags: SET HC | UNSET negative, carry
    cpu.executeOpcode(0x0019);
    REQUIRE(cpu.HL == 0xA027);
    REQUIRE(cpu.AF == hFlag); // Flags: set HC, rest unset
    cpu.executeOpcode(0x0039);
    REQUIRE(cpu.HL == 0xA028);
    REQUIRE(cpu.AF == 0x0000); // All flags unset
    REQUIRE(cpu.PC == 0x0003);
}

TEST_CASE_METHOD(CPUTest, "0x29:(ADD HL, HL), check full carry") {
    cpu.AF = nFlag; // Setting negative flag
    cpu.HL = 0x8A23;
    cpu.executeOpcode(0x0029);
    REQUIRE(cpu.HL == 0x1446); // Sum is stored in HL
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

TEST_CASE_METHOD(CPUTest, "0x*A:(LD A, (dd)) loading memory[dd] into A") {
    cpu.DE = 0x0011;
    cpu.memory[cpu.DE] = 0x5C;
    cpu.executeOpcode(0x001A);
    REQUIRE(cpu.AF == 0x5C00); // Register A = 0x2B (0b00101011) | carry flag set w/ rest unset
    REQUIRE(cpu.PC == 1);
    cpu.HL = 0x0003;
    cpu.memory[0x0003] = 0x0A;
    cpu.memory[0x0004] = 0x08;
    cpu.executeOpcode(0x002A);
    REQUIRE(cpu.AF == 0x0A00);
    REQUIRE(cpu.HL == 0x0004);
    cpu.executeOpcode(0x003A);
    REQUIRE(cpu.AF == 0x0800);
    REQUIRE(cpu.HL == 0x0003);
    REQUIRE(cpu.PC == 0x0003);
}

TEST_CASE_METHOD(CPUTest, "0x*B:(DEC dd) decrement pair dd") {
    cpu.BC = 0x235F;
    cpu.DE = 0x1234;
    cpu.HL = 0x4312;
    cpu.SP = 0xFFFF;
    cpu.executeOpcode(0x000B);
    cpu.executeOpcode(0x001B);
    cpu.executeOpcode(0x002B);
    cpu.executeOpcode(0x003B);
    REQUIRE(cpu.BC == 0x235E);
    REQUIRE(cpu.DE == 0x1233);
    REQUIRE(cpu.HL == 0x4311);
    REQUIRE(cpu.SP == 0xFFFE);
    REQUIRE(cpu.AF == 0x0000); // no flags set
    REQUIRE(cpu.PC == 4);
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
