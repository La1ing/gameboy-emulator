// CB-prefixed opcodes 0xCB00-0xCBFF: rotates/shifts/SWAP, BIT, RES, SET.
// Spec: meganesu opcode table, "opcodes prefixed by CB" (https://meganesu.github.io/generate-gb-opcodes/),
// bytes / M-cycles / Z N H C:
//   RLC/RRC/RL/RR/SLA/SRA/SRL r : 2 bytes, 2, Z 0 0 C(bit shifted out)    (HL) form: 2 bytes, 4
//   SWAP r                      : 2 bytes, 2, Z 0 0 0                     (HL) form: 2 bytes, 4
//   BIT b, r                    : 2 bytes, 2, !bit 0 1 -                  (HL) form: 2 bytes, 3
//   RES b, r / SET b, r         : 2 bytes, 2, - - - -                     (HL) form: 2 bytes, 4
// Encoding (Pan Docs CPU instruction set): CB xx ooo rrr
//   x=0: ooo 0=RLC 1=RRC 2=RL 3=RR 4=SLA 5=SRA 6=SWAP 7=SRL; x=1 BIT, x=2 RES, x=3 SET (ooo = bit)
//   rrr: 0=B 1=C 2=D 3=E 4=H 5=L 6=(HL) 7=A
// Exact semantics from rgbds gbz80(7) (linked from Pan Docs):
//   RLC: C <- b7, b0 <- b7      RRC: C <- b0, b7 <- b0
//   RL : C <- b7, b0 <- old C   RR : C <- b0, b7 <- old C
//   SLA: C <- b7, b0 <- 0       SRA: C <- b0, b7 unchanged      SRL: C <- b0, b7 <- 0
//   SWAP: swap nibbles, C = 0
//   RLCA/RRCA/RLA/RRA (unprefixed, 0x07/0x0F/0x17/0x1F): flags 0 0 0 C, i.e. Z is always 0
//   (meganesu table for those: "0 0 0 A7"/"0 0 0 A0").
#include "test_fixture.hpp"

#include <cstdint>
#include <vector>

namespace {

constexpr std::uint8_t Z = 0x80, N = 0x40, H = 0x20, C = 0x10;

constexpr const char *regName[8] = {"B", "C", "D", "E", "H", "L", "(HL)", "A"};
constexpr const char *shiftName[8] = {"RLC", "RRC", "RL", "RR", "SLA", "SRA", "SWAP", "SRL"};

constexpr std::uint16_t kHL = 0xC6F7;

void setHigh(std::uint16_t &pair, std::uint8_t v) { pair = static_cast<std::uint16_t>((pair & 0x00FF) | (v << 8)); }
void setLow(std::uint16_t &pair, std::uint8_t v) { pair = static_cast<std::uint16_t>((pair & 0xFF00) | v); }
void setA(CPU &cpu, std::uint8_t v) { setHigh(cpu.AF, v); }

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

// Distinct value in every register, (HL) byte seeded; F given by `flags`
void seedRegisters(CPU &cpu, std::uint8_t flags) {
    cpu.AF = static_cast<std::uint16_t>(0xA100 | flags);
    cpu.BC = 0xB2C3;
    cpu.DE = 0xD4E5;
    cpu.HL = kHL;
    cpu.SP = 0xDFF0;
    cpu.PC = 0x0200;
    cpu.memory[kHL] = 0x99;
}

std::uint16_t cbOp(int x, int y, int r) { return static_cast<std::uint16_t>(0xCB00 | (x << 6) | (y << 3) | r); }

// Reference model straight from the gbz80(7) descriptions above
struct ShiftResult {
    std::uint8_t value;
    bool carry;
};

ShiftResult refShift(int op, std::uint8_t v, bool cin) {
    const bool b7 = (v & 0x80) != 0;
    const bool b0 = (v & 0x01) != 0;
    switch (op) {
        case 0: return {static_cast<std::uint8_t>((v << 1) | (b7 ? 1 : 0)), b7};               // RLC
        case 1: return {static_cast<std::uint8_t>((v >> 1) | (b0 ? 0x80 : 0)), b0};            // RRC
        case 2: return {static_cast<std::uint8_t>((v << 1) | (cin ? 1 : 0)), b7};              // RL
        case 3: return {static_cast<std::uint8_t>((v >> 1) | (cin ? 0x80 : 0)), b0};           // RR
        case 4: return {static_cast<std::uint8_t>(v << 1), b7};                                // SLA
        case 5: return {static_cast<std::uint8_t>((v >> 1) | (v & 0x80)), b0};                 // SRA
        case 6: return {static_cast<std::uint8_t>(((v << 4) | (v >> 4)) & 0xFF), false};       // SWAP
        default: return {static_cast<std::uint8_t>(v >> 1), b0};                               // SRL
    }
}

// Every register except `idx` (and F) unchanged; for idx 4/5 HL itself changes, so it is skipped
void requireOthersUnchanged(const CPU &cpu, const CPU &expected, int idx) {
    for (int r = 0; r < 8; ++r) {
        if (r == idx || r == 6) continue;
        CAPTURE(regName[r]);
        REQUIRE(getReg(cpu, r) == getReg(expected, r));
    }
    if (idx != 6 && idx != 4 && idx != 5) REQUIRE(cpu.memory[kHL] == expected.memory[kHL]);
    REQUIRE(cpu.SP == expected.SP);
}

const std::vector<std::uint8_t> kShiftValues = {0x00, 0x01, 0x80, 0x81, 0xFF, 0x5A, 0xA5, 0x0F, 0xF0, 0x42, 0x7E};

} // namespace

// ---------------------------------------------------------------------------
// x=0: RLC, RRC, RL, RR, SLA, SRA, SWAP, SRL
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(CPUTest, "0xCB00-0xCB3F:(shift/rotate r) result and flags Z 0 0 C for every op/operand/value/carry-in") {
    const int op = GENERATE(range(0, 8));
    const int r = GENERATE(range(0, 8));
    const std::uint8_t v = GENERATE(from_range(kShiftValues));
    const std::uint8_t initialF = GENERATE(std::uint8_t{0x00}, std::uint8_t{0xF0}, std::uint8_t{C}, std::uint8_t{Z | N | H});
    const std::uint16_t opcode = cbOp(0, op, r);
    const bool cin = (initialF & C) != 0;
    CAPTURE(opcode, shiftName[op], regName[r], v, initialF);

    seedRegisters(cpu, initialF);
    setReg(cpu, r, v);
    CPU expected = cpu;
    const ShiftResult res = refShift(op, v, cin);
    const std::uint8_t expectedF = static_cast<std::uint8_t>((res.value == 0 ? Z : 0) | (res.carry ? C : 0));

    const int cycles = cpu.executeOpcode(opcode);

    REQUIRE(getReg(cpu, r) == res.value);
    REQUIRE(cpu.F() == expectedF);
    REQUIRE(cpu.PC == 0x0202);
    REQUIRE(cycles == (r == 6 ? 4 : 2));
    requireOthersUnchanged(cpu, expected, r);
}

namespace {

// Hand-checked vectors (gbz80(7)); applied to A and to (HL)
struct ShiftCase {
    int op;
    std::uint8_t in;
    bool cin;
    std::uint8_t out;
    std::uint8_t flags;
};

const std::vector<ShiftCase> kShiftCases = {
    {0, 0x00, false, 0x00, Z},          // RLC 0x00 -> 0, Z=1
    {0, 0x00, true, 0x00, Z},           // RLC ignores carry-in
    {0, 0x80, false, 0x01, C},
    {0, 0x85, false, 0x0B, C},
    {0, 0x7F, true, 0xFE, 0},
    {1, 0x00, true, 0x00, Z},           // RRC 0x00
    {1, 0x01, false, 0x80, C},
    {1, 0x02, true, 0x01, 0},
    {2, 0x80, false, 0x00, Z | C},      // RL: result 0 with carry out
    {2, 0x80, true, 0x01, C},           // RL carry-in 1
    {2, 0x00, true, 0x01, 0},
    {2, 0x00, false, 0x00, Z},
    {2, 0x95, false, 0x2A, C},
    {3, 0x01, false, 0x00, Z | C},      // RR: result 0 with carry out
    {3, 0x01, true, 0x80, C},           // RR carry-in 1
    {3, 0x00, true, 0x80, 0},
    {3, 0x8A, false, 0x45, 0},
    {4, 0x80, false, 0x00, Z | C},      // SLA 0x80 -> 0, C=1
    {4, 0x80, true, 0x00, Z | C},       // SLA shifts in 0 even with C=1
    {4, 0xFF, true, 0xFE, C},
    {4, 0x41, false, 0x82, 0},
    {5, 0x80, false, 0xC0, 0},          // SRA keeps bit 7
    {5, 0x81, true, 0xC0, C},
    {5, 0x01, false, 0x00, Z | C},      // SRA 0x01 -> 0
    {5, 0x7E, true, 0x3F, 0},           // SRA of positive: bit 7 stays 0
    {6, 0x00, true, 0x00, Z},           // SWAP 0 -> Z, C cleared
    {6, 0xF0, true, 0x0F, 0},           // SWAP clears C
    {6, 0x12, false, 0x21, 0},
    {7, 0x01, false, 0x00, Z | C},      // SRL 0x01 -> 0, C=1
    {7, 0x01, true, 0x00, Z | C},       // SRL shifts in 0 even with C=1
    {7, 0xFF, true, 0x7F, C},
    {7, 0x80, false, 0x40, 0},
};

} // namespace

TEST_CASE_METHOD(CPUTest, "0xCB00-0xCB3F:(shift/rotate) hand-checked vectors incl. Z=1 cases, on A and (HL)") {
    const ShiftCase tc = GENERATE(from_range(kShiftCases));
    const int r = GENERATE(7, 6);
    const std::uint8_t noise = GENERATE(std::uint8_t{0x00}, std::uint8_t{Z | N | H});
    const std::uint16_t opcode = cbOp(0, tc.op, r);
    CAPTURE(opcode, shiftName[tc.op], regName[r], tc.in, tc.cin, noise);

    seedRegisters(cpu, static_cast<std::uint8_t>(noise | (tc.cin ? C : 0)));
    setReg(cpu, r, tc.in);

    const int cycles = cpu.executeOpcode(opcode);

    REQUIRE(getReg(cpu, r) == tc.out);
    REQUIRE(cpu.F() == tc.flags);
    REQUIRE(cpu.PC == 0x0202);
    REQUIRE(cycles == (r == 6 ? 4 : 2));
}

// ---------------------------------------------------------------------------
// x=1: BIT b, r
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(CPUTest, "0xCB40-0xCB7F:(BIT b, r) Z = !bit, N=0, H=1, C unchanged, operand unchanged") {
    const int b = GENERATE(range(0, 8));
    const int r = GENERATE(range(0, 8));
    const std::uint8_t mask = static_cast<std::uint8_t>(1 << b);
    const std::uint8_t v = GENERATE_COPY(std::uint8_t{0x00}, std::uint8_t{0xFF}, mask,
                                         static_cast<std::uint8_t>(~mask), std::uint8_t{0x5A});
    const std::uint8_t initialF = GENERATE(std::uint8_t{0x00}, std::uint8_t{C}, std::uint8_t{0xF0},
                                           std::uint8_t{Z | N | H});
    const std::uint16_t opcode = cbOp(1, b, r);
    CAPTURE(opcode, b, regName[r], v, initialF);

    seedRegisters(cpu, initialF);
    setReg(cpu, r, v);
    CPU expected = cpu;
    const std::uint8_t expectedF =
        static_cast<std::uint8_t>(((v & mask) == 0 ? Z : 0) | H | (initialF & C));

    const int cycles = cpu.executeOpcode(opcode);

    REQUIRE(cpu.F() == expectedF);
    REQUIRE(getReg(cpu, r) == v);
    REQUIRE(cpu.memory[kHL] == expected.memory[kHL]);
    REQUIRE(cpu.A() == expected.A());
    REQUIRE(cpu.BC == expected.BC);
    REQUIRE(cpu.DE == expected.DE);
    REQUIRE(cpu.HL == expected.HL);
    REQUIRE(cpu.SP == expected.SP);
    REQUIRE(cpu.PC == 0x0202);
    REQUIRE(cycles == (r == 6 ? 3 : 2));
}

// ---------------------------------------------------------------------------
// x=2 / x=3: RES b, r and SET b, r
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(CPUTest, "0xCB80-0xCBFF:(RES/SET b, r) only the target bit changes, flags unchanged") {
    const int x = GENERATE(2, 3);
    const int b = GENERATE(range(0, 8));
    const int r = GENERATE(range(0, 8));
    const std::uint8_t v = GENERATE(std::uint8_t{0x00}, std::uint8_t{0xFF}, std::uint8_t{0x5A}, std::uint8_t{0xA5});
    const std::uint8_t initialF = GENERATE(std::uint8_t{0x00}, std::uint8_t{0xF0}, std::uint8_t{0x50}, std::uint8_t{0xA0});
    const std::uint16_t opcode = cbOp(x, b, r);
    const std::uint8_t mask = static_cast<std::uint8_t>(1 << b);
    const std::uint8_t result = (x == 2) ? static_cast<std::uint8_t>(v & ~mask) : static_cast<std::uint8_t>(v | mask);
    CAPTURE(opcode, (x == 2 ? "RES" : "SET"), b, regName[r], v, initialF);

    seedRegisters(cpu, initialF);
    setReg(cpu, r, v);
    CPU expected = cpu;

    const int cycles = cpu.executeOpcode(opcode);

    REQUIRE(getReg(cpu, r) == result);
    if (r == 6) { REQUIRE(cpu.memory[kHL] == result); }
    REQUIRE(cpu.F() == initialF);
    REQUIRE(cpu.PC == 0x0202);
    REQUIRE(cycles == (r == 6 ? 4 : 2));
    requireOthersUnchanged(cpu, expected, r);
}

// ---------------------------------------------------------------------------
// Length and cycles for all 256 CB opcodes
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(CPUTest, "0xCB00-0xCBFF:(all CB) PC +2; 2 cycles for r, (HL): BIT 3, others 4") {
    const int op = GENERATE(range(0, 256));
    const std::uint16_t opcode = static_cast<std::uint16_t>(0xCB00 | op);
    const bool isHL = (op & 0x07) == 6;
    const bool isBit = (op >> 6) == 1;
    const int expectedCycles = !isHL ? 2 : (isBit ? 3 : 4);
    CAPTURE(opcode, expectedCycles);

    seedRegisters(cpu, 0x00);

    const int cycles = cpu.executeOpcode(opcode);

    REQUIRE(cpu.PC == 0x0202);
    REQUIRE(cycles == expectedCycles);
    REQUIRE_FALSE(cpu.locked);
}

// ---------------------------------------------------------------------------
// Through step(): 0xCB prefix byte in memory
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(CPUTest, "0xCB37:(SWAP A) via step reads the CB prefix, PC +2, 2 cycles") {
    seedRegisters(cpu, 0xF0);
    setA(cpu, 0x3C);
    cpu.memory[0x0200] = 0xCB;
    cpu.memory[0x0201] = 0x37;
    cpu.memory[0x0202] = 0x00;

    REQUIRE(cpu.step() == 2);
    REQUIRE(cpu.A() == 0xC3);
    REQUIRE(cpu.F() == 0x00);
    REQUIRE(cpu.PC == 0x0202);
}

TEST_CASE_METHOD(CPUTest, "0xCB11/0xCB7E/0xCBDE:(RL C, BIT 7,(HL), SET 3,(HL)) sequence via step") {
    seedRegisters(cpu, C);
    setLow(cpu.BC, 0x80);
    cpu.memory[kHL] = 0x00;
    cpu.memory[0x0200] = 0xCB; cpu.memory[0x0201] = 0x11; // RL C
    cpu.memory[0x0202] = 0xCB; cpu.memory[0x0203] = 0x7E; // BIT 7, (HL)
    cpu.memory[0x0204] = 0xCB; cpu.memory[0x0205] = 0xDE; // SET 3, (HL)
    cpu.memory[0x0206] = 0xCB; cpu.memory[0x0207] = 0x5E; // BIT 3, (HL)

    REQUIRE(cpu.step() == 2);
    REQUIRE(cpu.C() == 0x01);
    REQUIRE(cpu.F() == C);
    REQUIRE(cpu.PC == 0x0202);

    REQUIRE(cpu.step() == 3);
    REQUIRE(cpu.F() == (Z | H | C));
    REQUIRE(cpu.memory[kHL] == 0x00);
    REQUIRE(cpu.PC == 0x0204);

    REQUIRE(cpu.step() == 4);
    REQUIRE(cpu.memory[kHL] == 0x08);
    REQUIRE(cpu.F() == (Z | H | C));
    REQUIRE(cpu.PC == 0x0206);

    REQUIRE(cpu.step() == 3);
    REQUIRE(cpu.F() == (H | C));
    REQUIRE(cpu.PC == 0x0208);
}

// ---------------------------------------------------------------------------
// Regression: the unprefixed A rotates never set Z
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(CPUTest, "0x07/0x0F/0x17/0x1F:(RLCA/RRCA/RLA/RRA) Z=0 even when the result is 0") {
    // {opcode, A in, carry-in, A out, flags}; meganesu: "0 0 0 C" for all four
    const auto [op, in, cin, out, flags] = GENERATE(table<std::uint16_t, std::uint8_t, bool, std::uint8_t, std::uint8_t>({
        {0x07, 0x00, false, 0x00, 0},
        {0x07, 0x00, true, 0x00, 0},
        {0x0F, 0x00, false, 0x00, 0},
        {0x0F, 0x00, true, 0x00, 0},
        {0x17, 0x80, false, 0x00, C},
        {0x17, 0x00, false, 0x00, 0},
        {0x1F, 0x01, false, 0x00, C},
        {0x1F, 0x00, false, 0x00, 0},
    }));
    CAPTURE(op, in, cin);

    seedRegisters(cpu, static_cast<std::uint8_t>(Z | N | H | (cin ? C : 0)));
    setA(cpu, in);

    const int cycles = cpu.executeOpcode(op);

    REQUIRE(cpu.A() == out);
    REQUIRE(cpu.F() == flags);
    REQUIRE(cpu.PC == 0x0201);
    REQUIRE(cycles == 1);
}
