// Opcodes 0xC0-0xFF: jumps, calls, returns, RST, PUSH/POP, high/absolute loads, SP arithmetic,
// DI/EI/RETI and the illegal (hard-lock) opcodes. The immediate ALU forms are in test_alu.cpp.
// Spec: meganesu opcode table (https://meganesu.github.io/generate-gb-opcodes/), bytes / M-cycles:
//   RET cc       : 1 byte, 5 taken / 2 not taken    RET, RETI    : 1 byte, 4
//   JP cc, a16   : 3 bytes, 4 taken / 3 not taken   JP a16       : 3 bytes, 4      JP HL : 1 byte, 1
//   CALL cc, a16 : 3 bytes, 6 taken / 3 not taken   CALL a16     : 3 bytes, 6
//   RST n        : 1 byte, 4                        PUSH rr      : 1 byte, 4       POP rr : 1 byte, 3
//   LD (a8),A / LD A,(a8)   : 2 bytes, 3            LD (C),A / LD A,(C)     : 1 byte, 2
//   LD (a16),A / LD A,(a16) : 3 bytes, 4            LD SP,HL                : 1 byte, 2
//   ADD SP, s8   : 2 bytes, 4, flags 0 0 H CY       LD HL, SP+s8 : 2 bytes, 3, flags 0 0 H CY
//   DI, EI       : 1 byte, 1
//   All others above: flags - - - -
// Exact semantics from rgbds gbz80(7) (linked from Pan Docs' instruction set page):
//   PUSH: DEC SP; LD [SP],HIGH; DEC SP; LD [SP],LOW  -> low byte at SP, high byte at SP+1
//   POP AF: Z/N/H/C set from bits 7-4 of the popped low byte (the meganesu table shows "- - - -"
//           for POP AF; gbz80(7) is used here). F's lower nibble is always 0.
//   ADD SP,e8 / LD HL,SP+e8: Z=0, N=0, H = overflow from bit 3, C = overflow from bit 7
//           (i.e. of the unsigned add of SP's low byte and e8 as an unsigned byte).
//   EI: "The flag is only set after the instruction following EI."
//   RETI: like EI then RET, "IME is set right after this instruction".
// Pan Docs CPU instruction set: $D3, $DB, $DD, $E3, $E4, $EB, $EC, $ED, $F4, $FC, $FD are invalid
//   and hard-lock the CPU until power off. LDH addresses are $FF00 + a8 / $FF00 + C.
#include "test_fixture.hpp"

#include <cstdint>
#include <vector>

namespace {

constexpr std::uint8_t Z = 0x80, N = 0x40, H = 0x20, C = 0x10;

constexpr const char *ccName[4] = {"NZ", "Z", "NC", "C"};
constexpr const char *pairName[4] = {"BC", "DE", "HL", "AF"};

void setHigh(std::uint16_t &pair, std::uint8_t v) { pair = static_cast<std::uint16_t>((pair & 0x00FF) | (v << 8)); }
void setLow(std::uint16_t &pair, std::uint8_t v) { pair = static_cast<std::uint16_t>((pair & 0xFF00) | v); }
void setA(CPU &cpu, std::uint8_t v) { setHigh(cpu.AF, v); }
void setC(CPU &cpu, std::uint8_t v) { setLow(cpu.BC, v); }

// Distinct value in every register; F given by `flags`
void seedRegisters(CPU &cpu, std::uint8_t flags) {
    cpu.AF = static_cast<std::uint16_t>(0xA100 | flags);
    cpu.BC = 0xB2C3;
    cpu.DE = 0xD4E5;
    cpu.HL = 0xC6F7;
    cpu.SP = 0xDFF0;
    cpu.PC = 0x0200;
}

void write16(CPU &cpu, std::uint16_t addr, std::uint16_t v) {
    cpu.memory[addr] = lowByte(v);
    cpu.memory[static_cast<std::uint16_t>(addr + 1)] = highByte(v);
}

std::uint16_t &pairRef(CPU &cpu, int p) {
    switch (p) {
        case 0: return cpu.BC;
        case 1: return cpu.DE;
        case 2: return cpu.HL;
        default: return cpu.AF;
    }
}

bool condTaken(int cc, std::uint8_t f) {
    switch (cc) {
        case 0: return (f & Z) == 0;
        case 1: return (f & Z) != 0;
        case 2: return (f & C) == 0;
        default: return (f & C) != 0;
    }
}

// Flag patterns that cover both branches of every condition, plus N/H noise
const std::vector<std::uint8_t> kFlagSets = {0x00, Z, C, Z | C, N | H, Z | N | H | C};

// All registers other than PC/SP must be unchanged
void requireRegsUnchanged(const CPU &cpu, const CPU &expected) {
    REQUIRE(cpu.AF == expected.AF);
    REQUIRE(cpu.BC == expected.BC);
    REQUIRE(cpu.DE == expected.DE);
    REQUIRE(cpu.HL == expected.HL);
}

} // namespace

// ---------------------------------------------------------------------------
// Jumps
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(CPUTest, "0xC3:(JP a16) jumps to little-endian a16, 4 cycles, flags unchanged") {
    const std::uint8_t flags = GENERATE(std::uint8_t{0x00}, std::uint8_t{0xF0});
    const std::uint16_t target = GENERATE(std::uint16_t{0x1234}, std::uint16_t{0x0000}, std::uint16_t{0xFFFF},
                                          std::uint16_t{0x0200});
    CAPTURE(flags, target);
    seedRegisters(cpu, flags);
    write16(cpu, 0x0201, target);
    CPU expected = cpu;

    const int cycles = cpu.executeOpcode(0xC3);

    REQUIRE(cpu.PC == target);
    REQUIRE(cpu.SP == expected.SP);
    requireRegsUnchanged(cpu, expected);
    REQUIRE(cycles == 4);
}

TEST_CASE_METHOD(CPUTest, "0xC2-0xDA:(JP cc, a16) taken -> target, 4 cycles; not taken -> PC +3, 3 cycles") {
    const int cc = GENERATE(0, 1, 2, 3);
    const std::uint8_t flags = GENERATE(from_range(kFlagSets));
    const std::uint16_t op = static_cast<std::uint16_t>(0xC2 | (cc << 3));
    const bool taken = condTaken(cc, flags);
    CAPTURE(op, ccName[cc], flags, taken);

    seedRegisters(cpu, flags);
    write16(cpu, 0x0201, 0x4321);
    CPU expected = cpu;

    const int cycles = cpu.executeOpcode(op);

    REQUIRE(cpu.PC == (taken ? 0x4321 : 0x0203));
    REQUIRE(cycles == (taken ? 4 : 3));
    REQUIRE(cpu.SP == expected.SP);
    requireRegsUnchanged(cpu, expected);
}

TEST_CASE_METHOD(CPUTest, "0xE9:(JP HL) PC = HL, 1 cycle, flags unchanged") {
    const std::uint8_t flags = GENERATE(std::uint8_t{0x00}, std::uint8_t{0xF0});
    const std::uint16_t hl = GENERATE(std::uint16_t{0xC6F7}, std::uint16_t{0x0000}, std::uint16_t{0xFFFF});
    CAPTURE(flags, hl);
    seedRegisters(cpu, flags);
    cpu.HL = hl;
    write16(cpu, hl, 0x9999); // memory at HL must not be used as a target
    write16(cpu, 0x0201, 0x8888);
    CPU expected = cpu;

    const int cycles = cpu.executeOpcode(0xE9);

    REQUIRE(cpu.PC == hl);
    REQUIRE(cpu.SP == expected.SP);
    requireRegsUnchanged(cpu, expected);
    REQUIRE(cycles == 1);
}

// ---------------------------------------------------------------------------
// Calls and returns
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(CPUTest, "0xCD:(CALL a16) pushes PC+3 (low at SP, high at SP+1), SP -2, jumps, 6 cycles") {
    const std::uint8_t flags = GENERATE(std::uint8_t{0x00}, std::uint8_t{0xF0});
    CAPTURE(flags);
    seedRegisters(cpu, flags);
    cpu.PC = 0x12FE; // return address 0x1301: distinct bytes
    write16(cpu, 0x12FF, 0x5678);
    CPU expected = cpu;

    const int cycles = cpu.executeOpcode(0xCD);

    REQUIRE(cpu.PC == 0x5678);
    REQUIRE(cpu.SP == 0xDFEE);
    REQUIRE(cpu.memory[0xDFEE] == 0x01);
    REQUIRE(cpu.memory[0xDFEF] == 0x13);
    REQUIRE(cpu.memory[0xDFF0] == expected.memory[0xDFF0]);
    requireRegsUnchanged(cpu, expected);
    REQUIRE(cycles == 6);
}

TEST_CASE_METHOD(CPUTest, "0xCD:(CALL a16) SP wraps below 0x0000") {
    seedRegisters(cpu, 0);
    cpu.SP = 0x0001;
    write16(cpu, 0x0201, 0x3000);

    const int cycles = cpu.executeOpcode(0xCD);

    REQUIRE(cpu.SP == 0xFFFF);
    REQUIRE(cpu.memory[0xFFFF] == 0x03); // low byte of 0x0203
    REQUIRE(cpu.memory[0x0000] == 0x02); // high byte
    REQUIRE(cpu.PC == 0x3000);
    REQUIRE(cycles == 6);
}

TEST_CASE_METHOD(CPUTest, "0xC4-0xDC:(CALL cc, a16) taken pushes/jumps, 6 cycles; not taken PC +3, 3 cycles") {
    const int cc = GENERATE(0, 1, 2, 3);
    const std::uint8_t flags = GENERATE(from_range(kFlagSets));
    const std::uint16_t op = static_cast<std::uint16_t>(0xC4 | (cc << 3));
    const bool taken = condTaken(cc, flags);
    CAPTURE(op, ccName[cc], flags, taken);

    seedRegisters(cpu, flags);
    write16(cpu, 0x0201, 0x4321);
    cpu.memory[0xDFEE] = 0xEE;
    cpu.memory[0xDFEF] = 0xEF;
    CPU expected = cpu;

    const int cycles = cpu.executeOpcode(op);

    if (taken) {
        REQUIRE(cpu.PC == 0x4321);
        REQUIRE(cpu.SP == 0xDFEE);
        REQUIRE(cpu.memory[0xDFEE] == 0x03);
        REQUIRE(cpu.memory[0xDFEF] == 0x02);
        REQUIRE(cycles == 6);
    } else {
        REQUIRE(cpu.PC == 0x0203);
        REQUIRE(cpu.SP == 0xDFF0);
        REQUIRE(cpu.memory[0xDFEE] == 0xEE);
        REQUIRE(cpu.memory[0xDFEF] == 0xEF);
        REQUIRE(cycles == 3);
    }
    requireRegsUnchanged(cpu, expected);
}

TEST_CASE_METHOD(CPUTest, "0xC9:(RET) pops PC (low from SP, high from SP+1), SP +2, 4 cycles") {
    const std::uint8_t flags = GENERATE(std::uint8_t{0x00}, std::uint8_t{0xF0});
    CAPTURE(flags);
    seedRegisters(cpu, flags);
    cpu.SP = 0xDFEE;
    cpu.memory[0xDFEE] = 0x34;
    cpu.memory[0xDFEF] = 0x12;
    CPU expected = cpu;

    const int cycles = cpu.executeOpcode(0xC9);

    REQUIRE(cpu.PC == 0x1234);
    REQUIRE(cpu.SP == 0xDFF0);
    REQUIRE(cpu.ime == expected.ime); // plain RET does not touch IME
    requireRegsUnchanged(cpu, expected);
    REQUIRE(cycles == 4);
}

TEST_CASE_METHOD(CPUTest, "0xC9:(RET) SP wraps past 0xFFFF") {
    seedRegisters(cpu, 0);
    cpu.SP = 0xFFFF;
    cpu.memory[0xFFFF] = 0x78;
    cpu.memory[0x0000] = 0x56;

    const int cycles = cpu.executeOpcode(0xC9);

    REQUIRE(cpu.PC == 0x5678);
    REQUIRE(cpu.SP == 0x0001);
    REQUIRE(cycles == 4);
}

TEST_CASE_METHOD(CPUTest, "0xC0-0xD8:(RET cc) taken pops PC, 5 cycles; not taken PC +1, 2 cycles") {
    const int cc = GENERATE(0, 1, 2, 3);
    const std::uint8_t flags = GENERATE(from_range(kFlagSets));
    const std::uint16_t op = static_cast<std::uint16_t>(0xC0 | (cc << 3));
    const bool taken = condTaken(cc, flags);
    CAPTURE(op, ccName[cc], flags, taken);

    seedRegisters(cpu, flags);
    cpu.SP = 0xDFEE;
    cpu.memory[0xDFEE] = 0x34;
    cpu.memory[0xDFEF] = 0x12;
    CPU expected = cpu;

    const int cycles = cpu.executeOpcode(op);

    REQUIRE(cpu.PC == (taken ? 0x1234 : 0x0201));
    REQUIRE(cpu.SP == (taken ? 0xDFF0 : 0xDFEE));
    REQUIRE(cycles == (taken ? 5 : 2));
    requireRegsUnchanged(cpu, expected);
}

TEST_CASE_METHOD(CPUTest, "0xD9:(RETI) pops PC, SP +2, sets IME immediately, 4 cycles") {
    const std::uint8_t flags = GENERATE(std::uint8_t{0x00}, std::uint8_t{0xF0});
    CAPTURE(flags);
    seedRegisters(cpu, flags);
    cpu.ime = false;
    cpu.SP = 0xDFEE;
    cpu.memory[0xDFEE] = 0x34;
    cpu.memory[0xDFEF] = 0x12;
    CPU expected = cpu;

    const int cycles = cpu.executeOpcode(0xD9);

    REQUIRE(cpu.PC == 0x1234);
    REQUIRE(cpu.SP == 0xDFF0);
    REQUIRE(cpu.ime);
    requireRegsUnchanged(cpu, expected);
    REQUIRE(cycles == 4);
}

TEST_CASE_METHOD(CPUTest, "0xCD/0xC9:(CALL then RET) round-trip returns to the instruction after CALL") {
    seedRegisters(cpu, 0xF0);
    cpu.memory[0x0200] = 0xCD;
    write16(cpu, 0x0201, 0x4000);
    cpu.memory[0x4000] = 0xC9;

    REQUIRE(cpu.step() == 6);
    REQUIRE(cpu.PC == 0x4000);
    REQUIRE(cpu.step() == 4);
    REQUIRE(cpu.PC == 0x0203);
    REQUIRE(cpu.SP == 0xDFF0);
    REQUIRE(cpu.F() == 0xF0);
}

// ---------------------------------------------------------------------------
// RST
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(CPUTest, "0xC7-0xFF:(RST n) pushes PC+1, jumps to n*8, 4 cycles, flags unchanged") {
    const int n = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    const std::uint8_t flags = GENERATE(std::uint8_t{0x00}, std::uint8_t{0xF0});
    const std::uint16_t op = static_cast<std::uint16_t>(0xC7 | (n << 3));
    CAPTURE(op, n, flags);

    seedRegisters(cpu, flags);
    cpu.PC = 0x12FF; // return address 0x1300
    CPU expected = cpu;

    const int cycles = cpu.executeOpcode(op);

    REQUIRE(cpu.PC == n * 8);
    REQUIRE(cpu.SP == 0xDFEE);
    REQUIRE(cpu.memory[0xDFEE] == 0x00);
    REQUIRE(cpu.memory[0xDFEF] == 0x13);
    requireRegsUnchanged(cpu, expected);
    REQUIRE(cycles == 4);
}

// ---------------------------------------------------------------------------
// PUSH / POP
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(CPUTest, "0xC5-0xF5:(PUSH rr) high at SP+1, low at SP, SP -2, 4 cycles, registers unchanged") {
    const int p = GENERATE(0, 1, 2, 3);
    const std::uint8_t flags = GENERATE(std::uint8_t{0x00}, std::uint8_t{0xF0}, std::uint8_t{0x50});
    const std::uint16_t op = static_cast<std::uint16_t>(0xC5 | (p << 4));
    CAPTURE(op, pairName[p], flags);

    seedRegisters(cpu, flags);
    const std::uint16_t value = pairRef(cpu, p);
    CPU expected = cpu;

    const int cycles = cpu.executeOpcode(op);

    REQUIRE(cpu.SP == 0xDFEE);
    REQUIRE(cpu.memory[0xDFEE] == lowByte(value));
    REQUIRE(cpu.memory[0xDFEF] == highByte(value));
    REQUIRE(cpu.memory[0xDFF0] == expected.memory[0xDFF0]);
    REQUIRE(cpu.PC == 0x0201);
    requireRegsUnchanged(cpu, expected);
    REQUIRE(cycles == 4);
}

TEST_CASE_METHOD(CPUTest, "0xC5:(PUSH BC) SP wraps below 0x0000") {
    seedRegisters(cpu, 0);
    cpu.SP = 0x0001;

    const int cycles = cpu.executeOpcode(0xC5);

    REQUIRE(cpu.SP == 0xFFFF);
    REQUIRE(cpu.memory[0xFFFF] == 0xC3);
    REQUIRE(cpu.memory[0x0000] == 0xB2);
    REQUIRE(cycles == 4);
}

TEST_CASE_METHOD(CPUTest, "0xC1-0xE1:(POP BC/DE/HL) low from SP, high from SP+1, SP +2, 3 cycles, flags unchanged") {
    const int p = GENERATE(0, 1, 2);
    const std::uint8_t flags = GENERATE(std::uint8_t{0x00}, std::uint8_t{0xF0});
    const std::uint16_t op = static_cast<std::uint16_t>(0xC1 | (p << 4));
    CAPTURE(op, pairName[p], flags);

    seedRegisters(cpu, flags);
    cpu.SP = 0xDFEE;
    cpu.memory[0xDFEE] = 0x5A;
    cpu.memory[0xDFEF] = 0x3C;
    CPU expected = cpu;
    pairRef(expected, p) = 0x3C5A;

    const int cycles = cpu.executeOpcode(op);

    REQUIRE(cpu.SP == 0xDFF0);
    REQUIRE(cpu.PC == 0x0201);
    requireRegsUnchanged(cpu, expected);
    REQUIRE(cycles == 3);
}

TEST_CASE_METHOD(CPUTest, "0xF1:(POP AF) A from SP+1, F from SP with lower nibble forced to 0") {
    // gbz80(7): Z/N/H/C set from bits 7-4 of the popped low byte
    const auto [lo, fExpected] = GENERATE(table<std::uint8_t, std::uint8_t>({
        {0xFF, 0xF0},
        {0x0F, 0x00},
        {0x5A, 0x50},
        {0xA5, 0xA0},
        {0x80, 0x80},
        {0x10, 0x10},
        {0x00, 0x00},
    }));
    const std::uint8_t initialF = GENERATE(std::uint8_t{0x00}, std::uint8_t{0xF0});
    CAPTURE(lo, initialF);

    seedRegisters(cpu, initialF);
    cpu.SP = 0xDFEE;
    cpu.memory[0xDFEE] = lo;
    cpu.memory[0xDFEF] = 0x7E;
    CPU expected = cpu;

    const int cycles = cpu.executeOpcode(0xF1);

    REQUIRE(cpu.A() == 0x7E);
    REQUIRE(cpu.F() == fExpected);
    REQUIRE(cpu.SP == 0xDFF0);
    REQUIRE(cpu.BC == expected.BC);
    REQUIRE(cpu.DE == expected.DE);
    REQUIRE(cpu.HL == expected.HL);
    REQUIRE(cpu.PC == 0x0201);
    REQUIRE(cycles == 3);
}

TEST_CASE_METHOD(CPUTest, "0xC5-0xF1:(PUSH rr then POP rr) round-trip restores the pair") {
    const int p = GENERATE(0, 1, 2, 3);
    CAPTURE(pairName[p]);
    seedRegisters(cpu, 0xB0);
    const std::uint16_t value = pairRef(cpu, p);

    cpu.memory[0x0200] = static_cast<std::uint8_t>(0xC5 | (p << 4)); // PUSH
    REQUIRE(cpu.step() == 4);
    pairRef(cpu, p) = (p == 3) ? 0x0000 : 0x1111; // clobber
    cpu.memory[cpu.PC] = static_cast<std::uint8_t>(0xC1 | (p << 4)); // POP
    REQUIRE(cpu.step() == 3);

    REQUIRE(pairRef(cpu, p) == value);
    REQUIRE(cpu.SP == 0xDFF0);
    REQUIRE(cpu.PC == 0x0202);
}

TEST_CASE_METHOD(CPUTest, "0xF5/0xC1:(PUSH AF, POP BC) stack holds A at SP+1 and F at SP") {
    seedRegisters(cpu, 0xA0);
    cpu.memory[0x0200] = 0xF5;
    cpu.memory[0x0201] = 0xC1;

    REQUIRE(cpu.step() == 4);
    REQUIRE(cpu.memory[0xDFEE] == 0xA0);
    REQUIRE(cpu.memory[0xDFEF] == 0xA1);
    REQUIRE(cpu.step() == 3);
    REQUIRE(cpu.BC == 0xA1A0);
    REQUIRE(cpu.SP == 0xDFF0);
}

// ---------------------------------------------------------------------------
// High-page and absolute loads, LD SP,HL
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(CPUTest, "0xE0:(LDH (a8), A) writes A to 0xFF00+a8, PC +2, 3 cycles, flags unchanged") {
    const std::uint8_t a8 = GENERATE(std::uint8_t{0x80}, std::uint8_t{0x00}, std::uint8_t{0xFF}, std::uint8_t{0x42});
    const std::uint8_t flags = GENERATE(std::uint8_t{0x00}, std::uint8_t{0xF0});
    CAPTURE(a8, flags);
    seedRegisters(cpu, flags);
    setA(cpu, 0x5C);
    cpu.memory[0x0201] = a8;
    const std::uint16_t addr = static_cast<std::uint16_t>(0xFF00 + a8);
    CPU expected = cpu;

    const int cycles = cpu.executeOpcode(0xE0);

    REQUIRE(cpu.memory[addr] == 0x5C);
    REQUIRE(cpu.memory[a8] == expected.memory[a8]);
    REQUIRE(cpu.PC == 0x0202);
    REQUIRE(cpu.SP == expected.SP);
    requireRegsUnchanged(cpu, expected);
    REQUIRE(cycles == 3);
}

TEST_CASE_METHOD(CPUTest, "0xF0:(LDH A, (a8)) reads A from 0xFF00+a8, PC +2, 3 cycles, flags unchanged") {
    const std::uint8_t a8 = GENERATE(std::uint8_t{0x80}, std::uint8_t{0x00}, std::uint8_t{0xFF}, std::uint8_t{0x42});
    const std::uint8_t flags = GENERATE(std::uint8_t{0x00}, std::uint8_t{0xF0});
    CAPTURE(a8, flags);
    seedRegisters(cpu, flags);
    cpu.memory[0x0201] = a8;
    cpu.memory[static_cast<std::uint16_t>(0xFF00 + a8)] = 0x6D;
    cpu.memory[a8] = 0x11; // low-page alias, must not be read
    CPU expected = cpu;
    setA(expected, 0x6D);

    const int cycles = cpu.executeOpcode(0xF0);

    REQUIRE(cpu.A() == 0x6D);
    REQUIRE(cpu.PC == 0x0202);
    REQUIRE(cpu.SP == expected.SP);
    requireRegsUnchanged(cpu, expected);
    REQUIRE(cycles == 3);
}

TEST_CASE_METHOD(CPUTest, "0xE2:(LD (C), A) writes A to 0xFF00+C, PC +1, 2 cycles, flags unchanged") {
    const std::uint8_t c = GENERATE(std::uint8_t{0x80}, std::uint8_t{0x00}, std::uint8_t{0xFF}, std::uint8_t{0x42});
    const std::uint8_t flags = GENERATE(std::uint8_t{0x00}, std::uint8_t{0xF0});
    CAPTURE(c, flags);
    seedRegisters(cpu, flags);
    setA(cpu, 0x5C);
    setC(cpu, c);
    cpu.memory[0x0201] = 0x33; // must not be used as an operand
    CPU expected = cpu;

    const int cycles = cpu.executeOpcode(0xE2);

    REQUIRE(cpu.memory[static_cast<std::uint16_t>(0xFF00 + c)] == 0x5C);
    REQUIRE(cpu.memory[static_cast<std::uint16_t>(0xFF33)] == expected.memory[0xFF33]);
    REQUIRE(cpu.PC == 0x0201);
    REQUIRE(cpu.SP == expected.SP);
    requireRegsUnchanged(cpu, expected);
    REQUIRE(cycles == 2);
}

TEST_CASE_METHOD(CPUTest, "0xF2:(LD A, (C)) reads A from 0xFF00+C, PC +1, 2 cycles, flags unchanged") {
    const std::uint8_t c = GENERATE(std::uint8_t{0x80}, std::uint8_t{0x00}, std::uint8_t{0xFF}, std::uint8_t{0x42});
    const std::uint8_t flags = GENERATE(std::uint8_t{0x00}, std::uint8_t{0xF0});
    CAPTURE(c, flags);
    seedRegisters(cpu, flags);
    setC(cpu, c);
    cpu.memory[static_cast<std::uint16_t>(0xFF00 + c)] = 0x6D;
    cpu.memory[c] = 0x11;
    CPU expected = cpu;
    setA(expected, 0x6D);

    const int cycles = cpu.executeOpcode(0xF2);

    REQUIRE(cpu.A() == 0x6D);
    REQUIRE(cpu.PC == 0x0201);
    REQUIRE(cpu.SP == expected.SP);
    requireRegsUnchanged(cpu, expected);
    REQUIRE(cycles == 2);
}

TEST_CASE_METHOD(CPUTest, "0xEA:(LD (a16), A) writes A to little-endian a16, PC +3, 4 cycles, flags unchanged") {
    const std::uint16_t addr = GENERATE(std::uint16_t{0xC123}, std::uint16_t{0x0000}, std::uint16_t{0xFFFF},
                                        std::uint16_t{0x1234});
    const std::uint8_t flags = GENERATE(std::uint8_t{0x00}, std::uint8_t{0xF0});
    CAPTURE(addr, flags);
    seedRegisters(cpu, flags);
    setA(cpu, 0x5C);
    write16(cpu, 0x0201, addr);
    CPU expected = cpu;

    const int cycles = cpu.executeOpcode(0xEA);

    REQUIRE(cpu.memory[addr] == 0x5C);
    if (addr != 0x3412) { REQUIRE(cpu.memory[0x3412] == expected.memory[0x3412]); } // byte-swapped address
    REQUIRE(cpu.PC == 0x0203);
    REQUIRE(cpu.SP == expected.SP);
    requireRegsUnchanged(cpu, expected);
    REQUIRE(cycles == 4);
}

TEST_CASE_METHOD(CPUTest, "0xFA:(LD A, (a16)) reads A from little-endian a16, PC +3, 4 cycles, flags unchanged") {
    const std::uint16_t addr = GENERATE(std::uint16_t{0xC123}, std::uint16_t{0x0000}, std::uint16_t{0xFFFF},
                                        std::uint16_t{0x1234});
    const std::uint8_t flags = GENERATE(std::uint8_t{0x00}, std::uint8_t{0xF0});
    CAPTURE(addr, flags);
    seedRegisters(cpu, flags);
    write16(cpu, 0x0201, addr);
    cpu.memory[addr] = 0x6D;
    if (addr != 0x3412) cpu.memory[0x3412] = 0x11; // byte-swapped address, must not be read
    CPU expected = cpu;
    setA(expected, 0x6D);

    const int cycles = cpu.executeOpcode(0xFA);

    REQUIRE(cpu.A() == 0x6D);
    REQUIRE(cpu.PC == 0x0203);
    REQUIRE(cpu.SP == expected.SP);
    requireRegsUnchanged(cpu, expected);
    REQUIRE(cycles == 4);
}

TEST_CASE_METHOD(CPUTest, "0xF9:(LD SP, HL) SP = HL, PC +1, 2 cycles, flags unchanged") {
    const std::uint16_t hl = GENERATE(std::uint16_t{0xC6F7}, std::uint16_t{0x0000}, std::uint16_t{0xFFFF});
    const std::uint8_t flags = GENERATE(std::uint8_t{0x00}, std::uint8_t{0xF0});
    CAPTURE(hl, flags);
    seedRegisters(cpu, flags);
    cpu.HL = hl;
    CPU expected = cpu;

    const int cycles = cpu.executeOpcode(0xF9);

    REQUIRE(cpu.SP == hl);
    REQUIRE(cpu.PC == 0x0201);
    requireRegsUnchanged(cpu, expected);
    REQUIRE(cycles == 2);
}

// ---------------------------------------------------------------------------
// SP arithmetic: ADD SP,e8 (0xE8) and LD HL,SP+e8 (0xF8)
// ---------------------------------------------------------------------------

namespace {

struct SpCase {
    std::uint16_t sp;
    std::uint8_t e; // raw operand byte (two's complement)
    std::uint16_t result;
    std::uint8_t flags;
};

// H = ((SP & 0xF) + (e & 0xF)) > 0xF, C = ((SP & 0xFF) + e) > 0xFF; Z = N = 0 (gbz80(7))
const std::vector<SpCase> kSpCases = {
    {0x0000, 0x01, 0x0001, 0},
    {0x000F, 0x01, 0x0010, H},
    {0x00FF, 0x01, 0x0100, H | C},
    {0x00F0, 0x10, 0x0100, C},
    {0x00FF, 0xFF, 0x00FE, H | C},   // e = -1: still carries out of bits 3 and 7
    {0x0000, 0xFF, 0xFFFF, 0},       // e = -1, no carries
    {0x1000, 0xFF, 0x0FFF, 0},
    {0xDFF0, 0xF0, 0xDFE0, C},       // e = -16
    {0xDFF8, 0x08, 0xE000, H | C},
    {0x0001, 0x80, 0xFF81, 0},       // e = -128
    {0xFFFF, 0x01, 0x0000, H | C},   // result 0, Z still 0
    {0x0005, 0x7F, 0x0084, H},       // e = +127
    {0x0008, 0xF8, 0x0000, H | C},   // e = -8, result 0
    {0xC000, 0x00, 0xC000, 0},
    {0x12F8, 0x80, 0x1278, C},       // e = -128 with carry from bit 7, no H
};

} // namespace

TEST_CASE_METHOD(CPUTest, "0xE8:(ADD SP, e8) signed add, Z=0 N=0, H/C from low byte, PC +2, 4 cycles") {
    const SpCase tc = GENERATE(from_range(kSpCases));
    const std::uint8_t initialF = GENERATE(std::uint8_t{0x00}, std::uint8_t{0xF0});
    CAPTURE(tc.sp, tc.e, initialF);

    seedRegisters(cpu, initialF);
    cpu.SP = tc.sp;
    cpu.memory[0x0201] = tc.e;
    CPU expected = cpu;

    const int cycles = cpu.executeOpcode(0xE8);

    REQUIRE(cpu.SP == tc.result);
    REQUIRE(cpu.F() == tc.flags);
    REQUIRE(cpu.A() == expected.A());
    REQUIRE(cpu.BC == expected.BC);
    REQUIRE(cpu.DE == expected.DE);
    REQUIRE(cpu.HL == expected.HL);
    REQUIRE(cpu.PC == 0x0202);
    REQUIRE(cycles == 4);
}

TEST_CASE_METHOD(CPUTest, "0xF8:(LD HL, SP+e8) HL = SP + e, SP unchanged, Z=0 N=0, H/C from low byte, PC +2, 3 cycles") {
    const SpCase tc = GENERATE(from_range(kSpCases));
    const std::uint8_t initialF = GENERATE(std::uint8_t{0x00}, std::uint8_t{0xF0});
    CAPTURE(tc.sp, tc.e, initialF);

    seedRegisters(cpu, initialF);
    cpu.SP = tc.sp;
    cpu.memory[0x0201] = tc.e;
    CPU expected = cpu;

    const int cycles = cpu.executeOpcode(0xF8);

    REQUIRE(cpu.HL == tc.result);
    REQUIRE(cpu.SP == tc.sp);
    REQUIRE(cpu.F() == tc.flags);
    REQUIRE(cpu.A() == expected.A());
    REQUIRE(cpu.BC == expected.BC);
    REQUIRE(cpu.DE == expected.DE);
    REQUIRE(cpu.PC == 0x0202);
    REQUIRE(cycles == 3);
}

// ---------------------------------------------------------------------------
// DI / EI / RETI and IME
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(CPUTest, "initialize() resets ime, halted and locked") {
    cpu.ime = true;
    cpu.halted = true;
    cpu.locked = true;
    cpu.initialize();
    REQUIRE_FALSE(cpu.ime);
    REQUIRE_FALSE(cpu.halted);
    REQUIRE_FALSE(cpu.locked);
}

TEST_CASE_METHOD(CPUTest, "0xF3:(DI) clears IME, PC +1, 1 cycle, flags unchanged") {
    const std::uint8_t flags = GENERATE(std::uint8_t{0x00}, std::uint8_t{0xF0});
    const bool imeBefore = GENERATE(true, false);
    CAPTURE(flags, imeBefore);
    seedRegisters(cpu, flags);
    cpu.ime = imeBefore;
    CPU expected = cpu;

    const int cycles = cpu.executeOpcode(0xF3);

    REQUIRE_FALSE(cpu.ime);
    REQUIRE(cpu.PC == 0x0201);
    REQUIRE(cpu.SP == expected.SP);
    requireRegsUnchanged(cpu, expected);
    REQUIRE(cycles == 1);
}

TEST_CASE_METHOD(CPUTest, "0xFB:(EI) PC +1, 1 cycle, flags unchanged") {
    const std::uint8_t flags = GENERATE(std::uint8_t{0x00}, std::uint8_t{0xF0});
    CAPTURE(flags);
    seedRegisters(cpu, flags);
    CPU expected = cpu;

    const int cycles = cpu.executeOpcode(0xFB);

    REQUIRE(cpu.PC == 0x0201);
    REQUIRE(cpu.SP == expected.SP);
    requireRegsUnchanged(cpu, expected);
    REQUIRE(cycles == 1);
}

TEST_CASE_METHOD(CPUTest, "0xFB:(EI) IME is set only after the following instruction (step)") {
    seedRegisters(cpu, 0);
    cpu.memory[0x0200] = 0xFB; // EI
    cpu.memory[0x0201] = 0x00; // NOP
    cpu.memory[0x0202] = 0x00; // NOP

    REQUIRE(cpu.step() == 1);
    REQUIRE(cpu.PC == 0x0201);
    REQUIRE_FALSE(cpu.ime);
    REQUIRE(cpu.step() == 1);
    REQUIRE(cpu.PC == 0x0202);
    REQUIRE(cpu.ime);
    REQUIRE(cpu.step() == 1);
    REQUIRE(cpu.ime);
}

TEST_CASE_METHOD(CPUTest, "0xFB/0xF3:(EI then DI) IME stays false") {
    seedRegisters(cpu, 0);
    cpu.memory[0x0200] = 0xFB; // EI
    cpu.memory[0x0201] = 0xF3; // DI
    cpu.memory[0x0202] = 0x00; // NOP
    cpu.memory[0x0203] = 0x00; // NOP

    cpu.step();
    REQUIRE_FALSE(cpu.ime);
    cpu.step();
    REQUIRE_FALSE(cpu.ime);
    cpu.step();
    REQUIRE_FALSE(cpu.ime);
    cpu.step();
    REQUIRE_FALSE(cpu.ime);
    REQUIRE(cpu.PC == 0x0204);
}

TEST_CASE_METHOD(CPUTest, "0xD9:(RETI) via step sets IME right after the instruction") {
    seedRegisters(cpu, 0);
    cpu.SP = 0xDFEE;
    write16(cpu, 0xDFEE, 0x3000);
    cpu.memory[0x0200] = 0xD9;

    REQUIRE(cpu.step() == 4);
    REQUIRE(cpu.PC == 0x3000);
    REQUIRE(cpu.ime);
}

// ---------------------------------------------------------------------------
// Illegal opcodes: hard-lock (Pan Docs)
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(CPUTest, "0xD3-0xFD:(illegal) sets locked; further steps do not move PC or change state") {
    const std::uint8_t op = GENERATE(std::uint8_t{0xD3}, std::uint8_t{0xDB}, std::uint8_t{0xDD}, std::uint8_t{0xE3},
                                     std::uint8_t{0xE4}, std::uint8_t{0xEB}, std::uint8_t{0xEC}, std::uint8_t{0xED},
                                     std::uint8_t{0xF4}, std::uint8_t{0xFC}, std::uint8_t{0xFD});
    const std::uint8_t flags = GENERATE(std::uint8_t{0x00}, std::uint8_t{0xF0});
    CAPTURE(op, flags);

    seedRegisters(cpu, flags);
    cpu.memory[0x0200] = op;
    cpu.memory[0x0201] = 0x3C; // INC A, must never run
    cpu.memory[0x0202] = 0x04; // INC B
    CPU expected = cpu;

    REQUIRE_FALSE(cpu.locked);
    cpu.step();
    REQUIRE(cpu.locked);
    const std::uint16_t lockedPC = cpu.PC;
    for (int i = 0; i < 3; ++i) {
        cpu.step();
        REQUIRE(cpu.locked);
        REQUIRE(cpu.PC == lockedPC);
    }
    REQUIRE(cpu.SP == expected.SP);
    requireRegsUnchanged(cpu, expected);
}

TEST_CASE_METHOD(CPUTest, "0xD3-0xFD:(illegal) executeOpcode sets locked, flags and registers unchanged") {
    const std::uint8_t op = GENERATE(std::uint8_t{0xD3}, std::uint8_t{0xDB}, std::uint8_t{0xDD}, std::uint8_t{0xE3},
                                     std::uint8_t{0xE4}, std::uint8_t{0xEB}, std::uint8_t{0xEC}, std::uint8_t{0xED},
                                     std::uint8_t{0xF4}, std::uint8_t{0xFC}, std::uint8_t{0xFD});
    CAPTURE(op);
    seedRegisters(cpu, 0xF0);
    CPU expected = cpu;

    cpu.executeOpcode(op);

    REQUIRE(cpu.locked);
    REQUIRE(cpu.SP == expected.SP);
    requireRegsUnchanged(cpu, expected);
}
