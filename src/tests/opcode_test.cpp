
#define CATCH_CONFIG_MAIN
#include "catch.hpp"
#include "../main/CPU.hpp"

struct CPUTest{
    CPU cpu;
    CPUTest(){
        cpu.initialize();
    }
};

TEST_CASE_METHOD(CPUTest, "0x00: correctly increment PC") {
    cpu.executeOpcode(0x0000);
    REQUIRE(cpu.PC == 0x0001);
}

TEST_CASE_METHOD(CPUTest, "0x01: load 2 bytes of immediate data") {
    cpu.PC = 0x00; // start PC at 0
    cpu.memory[0x01] = 0x37;
    cpu.memory[0x02] = 0x13;
    cpu.executeOpcode(0x0001);
    REQUIRE(cpu.BC == 0x1337);
    REQUIRE(cpu.PC == 0x03);
}

TEST_CASE_METHOD(CPUTest, "0x02: register stored in correct location in memory") {
    // setting BC to 0x205F and A to 0x3F
    cpu.BC = 0x205F;
    cpu.AF = (0x3F << 8) | cpu.F();
    cpu.executeOpcode(0x0002);
    REQUIRE(cpu.memory[0x205F] == 0x3F);
    REQUIRE(cpu.PC == 0x0001);
}

TEST_CASE_METHOD(CPUTest, "0x03: increment BC NO overflow") {
    cpu.AF = 0x0000;
    cpu.BC = 0x000F;
    cpu.executeOpcode(0x03);
    REQUIRE(cpu.BC == 0x0010);
    REQUIRE(cpu.AF == 0x0000); // no flags are modified
    REQUIRE(cpu.PC == 0x0001);
}

TEST_CASE_METHOD(CPUTest, "0x03: increment B w/ overflow") {
    cpu.AF = 0x0000;
    cpu.BC = 0xFFFF;
    cpu.executeOpcode(0x0003);
    REQUIRE(cpu.BC == 0x0000);
    REQUIRE(cpu.AF == 0x0000); // no flags are modified
    REQUIRE(cpu.PC == 0x0001);
}

TEST_CASE_METHOD(CPUTest, "0x04: increment B NO overflow") {
    cpu.AF = 0b0000000001000000; // Setting AF as negative flag
    cpu.BC = 0x2000; // Setting B register as 0x20
    cpu.executeOpcode(0x0004);
    REQUIRE(cpu.BC == 0x2100);
    REQUIRE(cpu.AF == 0b00000000); // negative flag is unset
    REQUIRE(cpu.PC == 0x0001);
}

TEST_CASE_METHOD(CPUTest, "0x04: increment B w/ overflow") {
    cpu.AF = 0b0000000001000000; // Setting AF as negative flag
    cpu.BC = 0xFF00; // Setting B register as 0x20
    cpu.executeOpcode(0x0004);
    REQUIRE(cpu.BC == 0x0000);
    REQUIRE(cpu.AF == 0b0000000010100000); // Flag: SET zero, HC | UNSET negative
    REQUIRE(cpu.PC == 0x0001);
}

TEST_CASE_METHOD(CPUTest, "0x05: decrement B NO overflow (zero flag)") {
    // cpu.AF = 0b0000000000000000; // Setting AF as negative flag
    cpu.BC = 0x0100; // Setting B register as 0x01
    cpu.executeOpcode(0x0005);
    REQUIRE(cpu.BC == 0x0000);
    REQUIRE(cpu.AF == 0b0000000011000000); // Flag: SET zero, negative | UNSET HC
    REQUIRE(cpu.PC == 0x0001);
}

TEST_CASE_METHOD(CPUTest, "0x05: decrement B w/ overflow") {
    cpu.AF = 0b0000000010000000; // Setting AF as zero flag
    cpu.BC = 0x0000; // Setting B register as 0x01
    cpu.executeOpcode(0x0005);
    REQUIRE(cpu.BC == 0xFF00);
    REQUIRE(cpu.AF == 0b0000000001100000); // Flag: SET negative, HC | UNSET zero
    REQUIRE(cpu.PC == 0x0001);
}