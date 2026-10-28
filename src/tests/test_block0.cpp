// Opcodes 0x00-0x3F
#include "test_fixture.hpp"

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

TEST_CASE_METHOD(CPUTest, "0x*4:(INC r) increment r (higher pair)") {
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

TEST_CASE_METHOD(CPUTest, "0x*5:(DEC r) decrement r (higher pair)") {
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

TEST_CASE_METHOD(CPUTest, "0x*6:(LD r, u8) loading 8-bit immediate data into r (higher pair)") {
    cpu.PC = 0x00; // Start PC at 0
    cpu.memory[0x01] = 0x24;
    cpu.executeOpcode(0x0006);
    REQUIRE(cpu.BC == 0x2400);
    REQUIRE(cpu.PC == 0x0002);
    cpu.memory[0x03] = 0x25;
    cpu.executeOpcode(0x0016);
    REQUIRE(cpu.DE == 0x2500);
    REQUIRE(cpu.BC != 0x2500);
    cpu.memory[0x05] = 0x26;
    cpu.executeOpcode(0x0026);
    REQUIRE(cpu.HL == 0x2600);
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
    cpu.memory[0xC101] = 0x00; // u16 address = 0xD000 (little endian)
    cpu.memory[0xC102] = 0xD0;
    cpu.executeOpcode(0x0008);
    REQUIRE(cpu.memory[0xD000] == 0xF8);
    REQUIRE(cpu.memory[0xD001] == 0xFF);
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

TEST_CASE_METHOD(CPUTest, "0x*C:(INC r) increment register r (lower pair)") {
    cpu.BC = 0x23FF;
    cpu.executeOpcode(0x000C);
    REQUIRE(cpu.BC == 0x2300);
    REQUIRE(cpu.AF == (zFlag | hFlag)); // set: zero and half-carry flag
    REQUIRE(cpu.PC == 1);
    cpu.AF = nFlag; // Setting AF as negative flag
    cpu.BC = 0x0020; // Setting C register as 0x20
    cpu.executeOpcode(0x000C);
    REQUIRE(cpu.BC == 0x0021);
    REQUIRE(cpu.AF == 0b00000000); // Negative flag is unset
    cpu.DE = 0x0000;
    cpu.executeOpcode(0x1C);
    REQUIRE(cpu.DE == 0x0001);
    REQUIRE(cpu.AF == 0x0000);
    cpu.HL = 0x00A6;
    cpu.executeOpcode(0x2C);
    REQUIRE(cpu.HL == 0x00A7);
    REQUIRE(cpu.AF == 0x0000); 
    cpu.AF = 0xFF00;
    cpu.executeOpcode(0x3C);
    REQUIRE(cpu.AF == (0x0000 | zFlag | hFlag));
    cpu.executeOpcode(0x3C);
    REQUIRE(cpu.AF == 0x0100);
    REQUIRE(cpu.PC == 0x0006);
} 

TEST_CASE_METHOD(CPUTest, "0x*D:(DEC r) decrement register r (lower pair)") {
    cpu.BC = 0x4201;
    cpu.executeOpcode(0x000D);
    REQUIRE(cpu.BC == 0x4200);
    REQUIRE(cpu.AF == (zFlag | nFlag)); // set: zero and negative flag
    REQUIRE(cpu.PC == 1);
    cpu.AF = hFlag; // Setting AF as half carry flag
    cpu.BC = 0x0001; // Setting B register as 0x01
    cpu.executeOpcode(0x000D);
    REQUIRE(cpu.BC == 0x0000);
    REQUIRE(cpu.AF == 0b0000000011000000); // Flags: SET zero, negative | UNSET HC
    cpu.DE = 0x0022;
    cpu.executeOpcode(0x001D);
    REQUIRE(cpu.DE == 0x0021);
    REQUIRE(cpu.AF == nFlag);
    cpu.HL = 0x0076;
    cpu.executeOpcode(0x002D);
    REQUIRE(cpu.HL == 0x0075);  
    cpu.AF = 0x0100;
    cpu.executeOpcode(0x003D);
    REQUIRE(cpu.AF == (0x0000 | zFlag | nFlag));
    cpu.executeOpcode(0x003D);
    REQUIRE(cpu.AF == (0xFF00 | nFlag | hFlag));
    REQUIRE(cpu.PC == 0x0006);
} 

TEST_CASE_METHOD(CPUTest, "0x0E:(LD r, d8) load immediate data into reg r (lower pair)") {
    cpu.PC = 0x05;
    cpu.BC = 0x4200; // Higher register must be left unchanged
    cpu.memory[cpu.PC + 1] = 0x24;
    cpu.executeOpcode(0x000E);
    REQUIRE(cpu.BC == 0x4224);
    REQUIRE(cpu.PC == 7);
    cpu.PC = 0x00; // Restart PC at 0
    cpu.memory[0x01] = 0x24;
    cpu.executeOpcode(0x000E);
    REQUIRE(cpu.BC == 0x4224);
    REQUIRE(cpu.PC == 0x0002);
    cpu.DE = 0x1100;
    cpu.memory[0x03] = 0x25;
    cpu.executeOpcode(0x001E);
    REQUIRE(cpu.DE == 0x1125);
    cpu.HL = 0x3300;
    cpu.memory[0x05] = 0x26;
    cpu.executeOpcode(0x002E);
    REQUIRE(cpu.HL == 0x3326);
    cpu.memory[0x07] = 0x28;
    cpu.executeOpcode(0x003E);
    REQUIRE(cpu.AF == 0x2800);
    REQUIRE(cpu.PC == 0x0008);
} 

TEST_CASE_METHOD(CPUTest, "0x0F:(RRCA) rotating register A right") {
    cpu.AF = 0x3B00 | zFlag | nFlag | hFlag | cFlag; // SetA at 0x95 (0b10010101), all flags set
    cpu.executeOpcode(0x000F);
    REQUIRE(cpu.AF == (0x9D00 | cFlag)); // Register A = 0x2B (0b00101011) | carry flag set w/ rest unset
    REQUIRE(cpu.PC == 1);
}

TEST_CASE_METHOD(CPUTest, "0x1F:(RRCA) rotating register A right") {
    cpu.AF = 0x8100 | zFlag | nFlag | hFlag; // SetA at 0x81 (0b10000001), all flags set
    cpu.executeOpcode(0x001F);
    REQUIRE(cpu.AF == (0x4000 | cFlag)); // Register A = 0x40 (0b01000000) | carry flag set w/ rest unset
    REQUIRE(cpu.PC == 1);
}

TEST_CASE_METHOD(CPUTest, "0x10:(STOP) skips 2 bytes") {
    cpu.executeOpcode(0x0010);
    REQUIRE(cpu.PC == 2);
}

TEST_CASE_METHOD(CPUTest, "0x20/30:(JR ncc, e) jump e steps if condition flag unset") {
    cpu.PC = 0x0000;
    cpu.memory[cpu.PC + 1] = 0x05;
    cpu.AF = zFlag; // Zero flag set, so no jump
    cpu.executeOpcode(0x20);
    REQUIRE(cpu.PC == 2);
    cpu.AF = 0x0000;
    cpu.memory[2 + 1] = 0x05;
    cpu.executeOpcode(0x20);
    REQUIRE(cpu.PC == 9); // 2 + 5 + 2

    cpu.PC = 0x0010;
    cpu.memory[0x11] = 0xFE; // -2
    cpu.AF = 0x0000; // Carry flag unset, so jump
    cpu.executeOpcode(0x30);
    REQUIRE(cpu.PC == 0x0010); // 0x10 - 2 + 2
    cpu.AF = cFlag;
    cpu.executeOpcode(0x30);
    REQUIRE(cpu.PC == 0x0012);
}

TEST_CASE_METHOD(CPUTest, "0x2F:(CPL) complement register A") {
    cpu.AF = 0x3500 | zFlag | cFlag;
    cpu.executeOpcode(0x002F);
    REQUIRE(cpu.AF == (0xCA00 | zFlag | nFlag | hFlag | cFlag)); // zero, carry unchanged | set negative, HC
    REQUIRE(cpu.PC == 1);
}

TEST_CASE_METHOD(CPUTest, "0x3F:(CCF) complement carry flag") {
    cpu.AF = 0x1200 | zFlag | nFlag | hFlag | cFlag;
    cpu.executeOpcode(0x003F);
    REQUIRE(cpu.AF == (0x1200 | zFlag)); // carry flipped | unset negative, HC
    cpu.executeOpcode(0x003F);
    REQUIRE(cpu.AF == (0x1200 | zFlag | cFlag));
    REQUIRE(cpu.PC == 2);
}

TEST_CASE_METHOD(CPUTest, "INC/DEC r & (HL) set half carry on bit 3/4 carry/borrow") {
    cpu.AF = 0x0000;
    cpu.BC = 0x0F00;
    cpu.executeOpcode(0x0004); // INC B: 0x0F -> 0x10
    REQUIRE(cpu.BC == 0x1000);
    REQUIRE(cpu.AF == hFlag);
    cpu.executeOpcode(0x0005); // DEC B: 0x10 -> 0x0F
    REQUIRE(cpu.BC == 0x0F00);
    REQUIRE(cpu.AF == (nFlag | hFlag));
    cpu.HL = 0xC000;
    cpu.memory[0xC000] = 0x2F;
    cpu.executeOpcode(0x0034); // INC (HL): 0x2F -> 0x30
    REQUIRE(cpu.memory[0xC000] == 0x30);
    REQUIRE(cpu.AF == hFlag);
    cpu.executeOpcode(0x0035); // DEC (HL): 0x30 -> 0x2F
    REQUIRE(cpu.memory[0xC000] == 0x2F);
    REQUIRE(cpu.AF == (nFlag | hFlag));
}

TEST_CASE_METHOD(CPUTest, "0x*4/*5/*C/*D:(INC/DEC r & (HL)) carry flag unchanged") {
    cpu.AF = cFlag;
    cpu.BC = 0x1000;
    cpu.executeOpcode(0x0004); // INC B
    REQUIRE(cpu.BC == 0x1100);
    REQUIRE(cpu.AF == cFlag);
    cpu.executeOpcode(0x000D); // DEC C: 0x00 -> 0xFF
    REQUIRE(cpu.BC == 0x11FF);
    REQUIRE(cpu.AF == (nFlag | hFlag | cFlag));
    cpu.HL = 0xC000;
    cpu.memory[0xC000] = 0x05;
    cpu.executeOpcode(0x0034); // INC (HL)
    REQUIRE(cpu.memory[0xC000] == 0x06);
    REQUIRE(cpu.AF == cFlag);
    cpu.executeOpcode(0x0035); // DEC (HL)
    REQUIRE(cpu.memory[0xC000] == 0x05);
    REQUIRE(cpu.AF == (nFlag | cFlag));
}

TEST_CASE_METHOD(CPUTest, "0x*9:(ADD HL, dd) zero flag unchanged") {
    cpu.AF = zFlag;
    cpu.HL = 0x0001;
    cpu.BC = 0x0001;
    cpu.executeOpcode(0x0009);
    REQUIRE(cpu.HL == 0x0002);
    REQUIRE(cpu.AF == zFlag);
}

TEST_CASE_METHOD(CPUTest, "0x1F:(RRA) old carry rotated into bit 7") {
    cpu.AF = 0x0000 | cFlag;
    cpu.executeOpcode(0x001F);
    REQUIRE(cpu.AF == 0x8000); // A = 0x80 | carry unset (bit 0 was 0)
}

TEST_CASE_METHOD(CPUTest, "0x07/0F:(RLCA/RRCA) carry unset when rotated-out bit is 0") {
    cpu.AF = 0x0100 | cFlag;
    cpu.executeOpcode(0x0007);
    REQUIRE(cpu.AF == 0x0200);
    cpu.AF = 0x0200 | cFlag;
    cpu.executeOpcode(0x000F);
    REQUIRE(cpu.AF == 0x0100);
}

TEST_CASE_METHOD(CPUTest, "0x28/38:(JR cc, e) negative offsets") {
    cpu.PC = 0x0020;
    cpu.memory[0x21] = 0xF0; // -16
    cpu.AF = zFlag;
    cpu.executeOpcode(0x28);
    REQUIRE(cpu.PC == 0x0012); // 0x20 - 16 + 2
    cpu.AF = 0x0000;
    cpu.memory[0x13] = 0xF0;
    cpu.executeOpcode(0x28);
    REQUIRE(cpu.PC == 0x0014); // not taken

    cpu.PC = 0x0040;
    cpu.memory[0x41] = 0x80; // -128
    cpu.AF = cFlag;
    cpu.executeOpcode(0x38);
    REQUIRE(cpu.PC == 0xFFC2); // 0x40 - 128 + 2 wraps around
}

TEST_CASE_METHOD(CPUTest, "0x32/2A:(LD (HL-), A / LD A, (HL+)) HL wraps around") {
    cpu.AF = 0x7700;
    cpu.HL = 0x0000;
    cpu.executeOpcode(0x0032);
    REQUIRE(cpu.memory[0x0000] == 0x77);
    REQUIRE(cpu.HL == 0xFFFF);
    cpu.memory[0xFFFF] = 0x99;
    cpu.executeOpcode(0x002A);
    REQUIRE(cpu.AF == 0x9900);
    REQUIRE(cpu.HL == 0x0000);
    REQUIRE(cpu.PC == 2);
}

TEST_CASE_METHOD(CPUTest, "0x37:(SCF) zero flag stays unset") {
    cpu.AF = nFlag | hFlag;
    cpu.executeOpcode(0x0037);
    REQUIRE(cpu.AF == cFlag);
}

TEST_CASE_METHOD(CPUTest, "0x00-0x3F: M-cycles match the opcode table") {
    // Conditional jumps (0x20/28/30/38) are checked separately
    auto [opcode, cycles] = GENERATE(table<std::uint16_t, int>({
        {0x00, 1}, {0x01, 3}, {0x02, 2}, {0x03, 2}, {0x04, 1}, {0x05, 1}, {0x06, 2}, {0x07, 1},
        {0x08, 5}, {0x09, 2}, {0x0A, 2}, {0x0B, 2}, {0x0C, 1}, {0x0D, 1}, {0x0E, 2}, {0x0F, 1},
        {0x10, 1}, {0x11, 3}, {0x12, 2}, {0x13, 2}, {0x14, 1}, {0x15, 1}, {0x16, 2}, {0x17, 1},
        {0x18, 3}, {0x19, 2}, {0x1A, 2}, {0x1B, 2}, {0x1C, 1}, {0x1D, 1}, {0x1E, 2}, {0x1F, 1},
                   {0x21, 3}, {0x22, 2}, {0x23, 2}, {0x24, 1}, {0x25, 1}, {0x26, 2}, {0x27, 1},
                   {0x29, 2}, {0x2A, 2}, {0x2B, 2}, {0x2C, 1}, {0x2D, 1}, {0x2E, 2}, {0x2F, 1},
                   {0x31, 3}, {0x32, 2}, {0x33, 2}, {0x34, 3}, {0x35, 3}, {0x36, 3}, {0x37, 1},
                   {0x39, 2}, {0x3A, 2}, {0x3B, 2}, {0x3C, 1}, {0x3D, 1}, {0x3E, 2}, {0x3F, 1},
    }));
    CAPTURE(opcode);
    cpu.HL = 0xC000; // (HL) opcodes target work RAM
    REQUIRE(cpu.executeOpcode(opcode) == cycles);
}

TEST_CASE_METHOD(CPUTest, "0x20/28/30/38:(JR cc, e) 3 M-cycles if taken, 2 if not") {
    auto [opcode, flags, cycles] = GENERATE(table<std::uint16_t, std::uint16_t, int>({
        {0x20, 0, 3}, {0x20, zFlag, 2},
        {0x28, zFlag, 3}, {0x28, 0, 2},
        {0x30, 0, 3}, {0x30, cFlag, 2},
        {0x38, cFlag, 3}, {0x38, 0, 2},
    }));
    CAPTURE(opcode, flags);
    cpu.AF = flags;
    REQUIRE(cpu.executeOpcode(opcode) == cycles);
}

TEST_CASE_METHOD(CPUTest, "step() fetches the opcode at PC and executes it") {
    cpu.PC = 0x0100;
    cpu.memory[0x0100] = 0x06; // LD B, d8
    cpu.memory[0x0101] = 0x42;
    cpu.memory[0x0102] = 0x04; // INC B
    REQUIRE(cpu.step() == 2);
    REQUIRE(cpu.B() == 0x42);
    REQUIRE(cpu.PC == 0x0102);
    REQUIRE(cpu.step() == 1);
    REQUIRE(cpu.B() == 0x43);
    REQUIRE(cpu.PC == 0x0103);
}
