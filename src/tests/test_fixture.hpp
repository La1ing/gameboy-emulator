#ifndef TEST_FIXTURE_HPP
#define TEST_FIXTURE_HPP

#include "catch.hpp"
#include "../main/CPU.hpp"

struct CPUTest{
    CPU cpu;
    CPUTest(){
        cpu.initialize();
    }
};

#endif
