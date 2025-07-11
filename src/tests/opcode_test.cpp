
#define CATCH_CONFIG_MAIN
#include "catch.hpp"
#include "../main/CPU.hpp"

TEST_CASE("0x00 test") {
    CPU cpu;
    cpu.initialize();
    cpu.executeOpcode(0x00);
    REQUIRE(cpu.PC == 0x01);
}