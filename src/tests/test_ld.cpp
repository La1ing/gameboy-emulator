// Opcodes 0x40-0x7F: LD r, r' (incl. (HL) forms) and HALT (0x76)
// Spec: meganesu opcode table (https://meganesu.github.io/generate-gb-opcodes/)
//   LD r, r'     : 1 byte, 1 M-cycle, flags - - - -
//   LD r, (HL)   : 1 byte, 2 M-cycles, flags - - - -
//   LD (HL), r   : 1 byte, 2 M-cycles, flags - - - -
//   HALT         : 1 byte, 1 M-cycle,  flags - - - -
// Register encoding (Pan Docs / gbctr): 0=B 1=C 2=D 3=E 4=H 5=L 6=(HL) 7=A
#include "test_fixture.hpp"

#include <cstdint>
#include <string>

namespace {

constexpr const char *regName[8] = {"B", "C", "D", "E", "H", "L", "(HL)", "A"};

std::uint8_t getReg(const CPU &cpu, int idx) {
    switch (idx) {
        case 0: return cpu.B();
        case 1: return cpu.C();
        case 2: return cpu.D();
        case 3: return cpu.E();
        case 4: return cpu.H();
        case 5: return cpu.L();
        case 7: return cpu.A();
        default: return cpu.memory[cpu.HL];
    }
}

void setHigh(std::uint16_t &pair, std::uint8_t v) { pair = static_cast<std::uint16_t>((pair & 0x00FF) | (v << 8)); }
void setLow(std::uint16_t &pair, std::uint8_t v) { pair = static_cast<std::uint16_t>((pair & 0xFF00) | v); }

void setReg(CPU &cpu, int idx, std::uint8_t v) {
    switch (idx) {
        case 0: setHigh(cpu.BC, v); break;
        case 1: setLow(cpu.BC, v); break;
        case 2: setHigh(cpu.DE, v); break;
        case 3: setLow(cpu.DE, v); break;
        case 4: setHigh(cpu.HL, v); break;
        case 5: setLow(cpu.HL, v); break;
        case 7: setHigh(cpu.AF, v); break;
        default: cpu.memory[cpu.HL] = v; break;
    }
}

std::uint16_t ldOpcode(int dst, int src) {
    return static_cast<std::uint16_t>(0x40 | (dst << 3) | src);
}

// Distinct value in every register; flags given by `flags` (upper nibble of F)
void seedRegisters(CPU &cpu, std::uint8_t flags) {
    cpu.AF = static_cast<std::uint16_t>(0xA100 | flags);
    cpu.BC = 0xB2C3;
    cpu.DE = 0xD4E5;
    cpu.HL = 0xC6F7; // H=0xC6, L=0xF7; also a valid WRAM-ish address
    cpu.SP = 0xDFF0;
    cpu.PC = 0x0200;
}

} // namespace

TEST_CASE_METHOD(CPUTest, "0x40-0x7F:(LD r, r') copies src to dst, nothing else changes") {
    const int dst = GENERATE(0, 1, 2, 3, 4, 5, 7);
    const int src = GENERATE(0, 1, 2, 3, 4, 5, 7);
    const std::uint8_t flags = GENERATE(std::uint8_t{0xF0}, std::uint8_t{0x00});
    const std::uint16_t op = ldOpcode(dst, src);
    CAPTURE(op, regName[dst], regName[src], flags);

    seedRegisters(cpu, flags);
    cpu.memory[cpu.HL] = 0x5A;

    CPU expected = cpu;
    setReg(expected, dst, getReg(cpu, src));
    expected.PC = 0x0201;

    const int cycles = cpu.executeOpcode(op);

    REQUIRE(getReg(cpu, dst) == getReg(expected, dst));
    REQUIRE(cpu.AF == expected.AF); // A (if not dst) and all flags unchanged
    REQUIRE(cpu.F() == flags);
    REQUIRE(cpu.BC == expected.BC);
    REQUIRE(cpu.DE == expected.DE);
    REQUIRE(cpu.HL == expected.HL);
    REQUIRE(cpu.SP == expected.SP);
    REQUIRE(cpu.PC == 0x0201);
    REQUIRE(cpu.memory[0xC6F7] == 0x5A);
    REQUIRE(cycles == 1);
    REQUIRE_FALSE(cpu.halted);
}

TEST_CASE_METHOD(CPUTest, "0x*6/0x*E:(LD r, (HL)) loads byte at HL into r (B,C,D,E,A)") {
    const int dst = GENERATE(0, 1, 2, 3, 7);
    const std::uint8_t flags = GENERATE(std::uint8_t{0xF0}, std::uint8_t{0x00});
    const std::uint16_t op = ldOpcode(dst, 6);
    CAPTURE(op, regName[dst], flags);

    seedRegisters(cpu, flags);
    cpu.memory[0xC6F7] = 0x5A;

    CPU expected = cpu;
    setReg(expected, dst, 0x5A);

    const int cycles = cpu.executeOpcode(op);

    REQUIRE(getReg(cpu, dst) == 0x5A);
    REQUIRE(cpu.AF == expected.AF);
    REQUIRE(cpu.F() == flags);
    REQUIRE(cpu.BC == expected.BC);
    REQUIRE(cpu.DE == expected.DE);
    REQUIRE(cpu.HL == 0xC6F7);
    REQUIRE(cpu.SP == expected.SP);
    REQUIRE(cpu.memory[0xC6F7] == 0x5A);
    REQUIRE(cpu.PC == 0x0201);
    REQUIRE(cycles == 2);
}

TEST_CASE_METHOD(CPUTest, "0x66:(LD H, (HL)) address is read before H changes") {
    seedRegisters(cpu, 0xF0);
    cpu.memory[0xC6F7] = 0x5A;
    cpu.memory[0x5AF7] = 0x11; // would be read if H were changed first

    const int cycles = cpu.executeOpcode(0x0066);

    REQUIRE(cpu.HL == 0x5AF7);
    REQUIRE(cpu.memory[0xC6F7] == 0x5A);
    REQUIRE(cpu.memory[0x5AF7] == 0x11);
    REQUIRE(cpu.AF == 0xA1F0);
    REQUIRE(cpu.BC == 0xB2C3);
    REQUIRE(cpu.DE == 0xD4E5);
    REQUIRE(cpu.PC == 0x0201);
    REQUIRE(cycles == 2);
}

TEST_CASE_METHOD(CPUTest, "0x6E:(LD L, (HL)) address is read before L changes") {
    seedRegisters(cpu, 0xF0);
    cpu.memory[0xC6F7] = 0x5A;
    cpu.memory[0xC65A] = 0x11;

    const int cycles = cpu.executeOpcode(0x006E);

    REQUIRE(cpu.HL == 0xC65A);
    REQUIRE(cpu.memory[0xC6F7] == 0x5A);
    REQUIRE(cpu.memory[0xC65A] == 0x11);
    REQUIRE(cpu.AF == 0xA1F0);
    REQUIRE(cpu.BC == 0xB2C3);
    REQUIRE(cpu.DE == 0xD4E5);
    REQUIRE(cpu.PC == 0x0201);
    REQUIRE(cycles == 2);
}

TEST_CASE_METHOD(CPUTest, "0x70-0x77:(LD (HL), r) stores r at HL (B,C,D,E,A)") {
    const int src = GENERATE(0, 1, 2, 3, 7);
    const std::uint8_t flags = GENERATE(std::uint8_t{0xF0}, std::uint8_t{0x00});
    const std::uint16_t op = ldOpcode(6, src);
    CAPTURE(op, regName[src], flags);

    seedRegisters(cpu, flags);
    cpu.memory[0xC6F7] = 0x5A;
    const std::uint8_t value = getReg(cpu, src);

    const int cycles = cpu.executeOpcode(op);

    REQUIRE(cpu.memory[0xC6F7] == value);
    REQUIRE(cpu.memory[0xC6F6] == 0x00); // neighbours untouched
    REQUIRE(cpu.memory[0xC6F8] == 0x00);
    REQUIRE(cpu.AF == static_cast<std::uint16_t>(0xA100 | flags));
    REQUIRE(cpu.BC == 0xB2C3);
    REQUIRE(cpu.DE == 0xD4E5);
    REQUIRE(cpu.HL == 0xC6F7);
    REQUIRE(cpu.SP == 0xDFF0);
    REQUIRE(cpu.PC == 0x0201);
    REQUIRE(cycles == 2);
}

TEST_CASE_METHOD(CPUTest, "0x74:(LD (HL), H) stores H at HL") {
    seedRegisters(cpu, 0xF0);
    const int cycles = cpu.executeOpcode(0x0074);
    REQUIRE(cpu.memory[0xC6F7] == 0xC6);
    REQUIRE(cpu.HL == 0xC6F7);
    REQUIRE(cpu.AF == 0xA1F0);
    REQUIRE(cpu.PC == 0x0201);
    REQUIRE(cycles == 2);
}

TEST_CASE_METHOD(CPUTest, "0x75:(LD (HL), L) stores L at HL") {
    seedRegisters(cpu, 0xF0);
    const int cycles = cpu.executeOpcode(0x0075);
    REQUIRE(cpu.memory[0xC6F7] == 0xF7);
    REQUIRE(cpu.HL == 0xC6F7);
    REQUIRE(cpu.AF == 0xA1F0);
    REQUIRE(cpu.PC == 0x0201);
    REQUIRE(cycles == 2);
}

TEST_CASE_METHOD(CPUTest, "0x76:(HALT) sets halted, advances PC by 1, 1 cycle, registers unchanged") {
    const std::uint8_t flags = GENERATE(std::uint8_t{0xF0}, std::uint8_t{0x00});
    CAPTURE(flags);
    seedRegisters(cpu, flags);
    cpu.memory[0xC6F7] = 0x5A;
    REQUIRE_FALSE(cpu.halted);

    const int cycles = cpu.executeOpcode(0x0076);

    REQUIRE(cpu.halted);
    REQUIRE(cpu.PC == 0x0201);
    REQUIRE(cycles == 1);
    REQUIRE(cpu.AF == static_cast<std::uint16_t>(0xA100 | flags));
    REQUIRE(cpu.BC == 0xB2C3);
    REQUIRE(cpu.DE == 0xD4E5);
    REQUIRE(cpu.HL == 0xC6F7);
    REQUIRE(cpu.SP == 0xDFF0);
    REQUIRE(cpu.memory[0xC6F7] == 0x5A);
}

TEST_CASE_METHOD(CPUTest, "0x40-0x7F: step() fetches and runs LD / HALT from memory at PC") {
    seedRegisters(cpu, 0xF0);
    cpu.memory[0x0200] = 0x78; // LD A, B
    cpu.memory[0x0201] = 0x4E; // LD C, (HL)
    cpu.memory[0x0202] = 0x72; // LD (HL), D
    cpu.memory[0x0203] = 0x76; // HALT
    cpu.memory[0xC6F7] = 0x5A;

    REQUIRE(cpu.step() == 1);
    REQUIRE(cpu.A() == 0xB2);
    REQUIRE(cpu.PC == 0x0201);

    REQUIRE(cpu.step() == 2);
    REQUIRE(cpu.C() == 0x5A);
    REQUIRE(cpu.PC == 0x0202);

    REQUIRE(cpu.step() == 2);
    REQUIRE(cpu.memory[0xC6F7] == 0xD4);
    REQUIRE(cpu.PC == 0x0203);

    REQUIRE(cpu.step() == 1);
    REQUIRE(cpu.halted);
    REQUIRE(cpu.PC == 0x0204);
    REQUIRE(cpu.F() == 0xF0);
}
