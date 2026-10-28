#ifndef TEST_FIXTURE_HPP
#define TEST_FIXTURE_HPP

#include "catch.hpp"
#include "flat_memory.hpp"
#include "../main/CPU.hpp"

struct CPUTest{
    FlatMemory memory; // zeroed 64 KiB of plain RAM
    CPU cpu{memory};
    CPUTest(){
        cpu.initialize();
    }
};

#endif
