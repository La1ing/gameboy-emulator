// Opcodes 0x80-0xBF (ALU A, r), the immediate forms 0xC6/CE/D6/DE/E6/EE/F6/FE (ALU A, d8) and DAA (0x27)
// Spec: meganesu opcode table (https://meganesu.github.io/generate-gb-opcodes/)
//   ADD/ADC A, r : 1 byte, 1 M-cycle, Z 0 H CY      ADD/ADC A, (HL) : 1 byte, 2 M-cycles
//   SUB/SBC/CP r : 1 byte, 1 M-cycle, Z 1 H CY      SUB/SBC/CP (HL) : 1 byte, 2 M-cycles
//   AND r        : 1 byte, 1 M-cycle, Z 0 1 0       AND (HL)        : 1 byte, 2 M-cycles
//   XOR/OR r     : 1 byte, 1 M-cycle, Z 0 0 0       XOR/OR (HL)     : 1 byte, 2 M-cycles
//   op A, d8     : 2 bytes, 2 M-cycles, same flags as the register form
//   DAA          : 1 byte, 1 M-cycle, Z - 0 CY
// Encoding (Pan Docs CPU instruction set): 0b10 ooo rrr / 0b11 ooo 110 with
//   ooo: 0=ADD 1=ADC 2=SUB 3=SBC 4=AND 5=XOR 6=OR 7=CP; rrr: 0=B 1=C 2=D 3=E 4=H 5=L 6=(HL) 7=A
// H/C semantics (rgbds gbz80(7), linked from Pan Docs):
//   ADD/ADC: H = carry from bit 3, C = carry from bit 7 (carry-in included for ADC)
//   SUB/SBC/CP: H = borrow from bit 4, C = borrow (i.e. b (+carry) > a)
// DAA semantics: rgbds gbz80(7) (linked from Pan Docs), see the DAA section below.
#include "test_fixture.hpp"

#include <cstdint>
#include <vector>

namespace {

constexpr const char *regName[8] = {"B", "C", "D", "E", "H", "L", "(HL)", "A"};
constexpr const char *opName[8] = {"ADD", "ADC", "SUB", "SBC", "AND", "XOR", "OR", "CP"};

enum AluOp : int { ADD = 0, ADC, SUB, SBC, AND, XOR, OR, CP };

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

void setA(CPU &cpu, std::uint8_t v) { setHigh(cpu.AF, v); }
void setF(CPU &cpu, std::uint8_t v) { setLow(cpu.AF, v); }

std::uint16_t regOpcode(int op, int src) { return static_cast<std::uint16_t>(0x80 | (op << 3) | src); }
std::uint16_t immOpcode(int op) { return static_cast<std::uint16_t>(0xC6 | (op << 3)); }

constexpr std::uint8_t Z = 0x80, N = 0x40, H = 0x20, C = 0x10;

// One ALU case: A op b (with carry-in) -> result in A (A unchanged for CP) and exact flags
struct AluCase {
    int op;
    std::uint8_t a;
    std::uint8_t b;
    bool carryIn;
    std::uint8_t result; // value of A after the op
    std::uint8_t flags;  // expected F (upper nibble)
};

// Hand-computed from the meganesu flag columns and the gbz80(7) H/C definitions
const std::vector<AluCase> kCases = {
    // ADD: Z 0 H CY, carry-in ignored
    {ADD, 0x12, 0x34, false, 0x46, 0},
    {ADD, 0x12, 0x34, true,  0x46, 0},          // carry-in must not be added
    {ADD, 0x0E, 0x01, false, 0x0F, 0},          // just below half-carry
    {ADD, 0x0F, 0x01, false, 0x10, H},          // 0x0F -> 0x10 half-carry
    {ADD, 0x08, 0x08, false, 0x10, H},
    {ADD, 0x10, 0x0F, false, 0x1F, 0},
    {ADD, 0xFF, 0x01, false, 0x00, Z | H | C},  // zero via overflow
    {ADD, 0xF0, 0x10, false, 0x00, Z | C},      // carry without half-carry
    {ADD, 0x80, 0x80, false, 0x00, Z | C},
    {ADD, 0x3A, 0xC6, false, 0x00, Z | H | C},
    {ADD, 0xF0, 0x0F, true,  0xFF, 0},
    {ADD, 0x00, 0x00, false, 0x00, Z},
    {ADD, 0xF8, 0x10, false, 0x08, C},
    // ADC: Z 0 H CY, carry-in added
    {ADC, 0x12, 0x34, false, 0x46, 0},
    {ADC, 0x12, 0x34, true,  0x47, 0},
    {ADC, 0x0E, 0x01, false, 0x0F, 0},
    {ADC, 0x0E, 0x01, true,  0x10, H},          // carry-in pushes over the nibble
    {ADC, 0x0F, 0x00, true,  0x10, H},          // carry-in alone causes H
    {ADC, 0x0F, 0x00, false, 0x0F, 0},
    {ADC, 0xFF, 0x00, true,  0x00, Z | H | C},  // carry-in alone causes C and zero
    {ADC, 0xF0, 0x0F, true,  0x00, Z | H | C},
    {ADC, 0xE1, 0x1E, true,  0x00, Z | H | C},
    {ADC, 0x80, 0x7F, true,  0x00, Z | H | C},
    {ADC, 0x80, 0x80, false, 0x00, Z | C},
    {ADC, 0x08, 0x08, false, 0x10, H},
    {ADC, 0xFF, 0x01, false, 0x00, Z | H | C},
    {ADC, 0x00, 0x00, false, 0x00, Z},
    // SUB: Z 1 H CY, carry-in ignored
    {SUB, 0x3E, 0x3E, false, 0x00, Z | N},
    {SUB, 0x10, 0x01, false, 0x0F, N | H},      // borrow from bit 4
    {SUB, 0x00, 0x01, false, 0xFF, N | H | C},  // full borrow
    {SUB, 0x3E, 0x0F, false, 0x2F, N | H},
    {SUB, 0x3E, 0x40, false, 0xFE, N | C},      // borrow without half-borrow
    {SUB, 0x3E, 0x3F, false, 0xFF, N | H | C},
    {SUB, 0x55, 0x11, true,  0x44, N},          // carry-in must not be subtracted
    {SUB, 0x12, 0x34, true,  0xDE, N | H | C},
    {SUB, 0x0F, 0x0F, true,  0x00, Z | N},
    // SBC: Z 1 H CY, carry-in subtracted
    {SBC, 0x20, 0x10, false, 0x10, N},
    {SBC, 0x20, 0x10, true,  0x0F, N | H},      // carry-in alone causes H
    {SBC, 0x10, 0x00, true,  0x0F, N | H},
    {SBC, 0x00, 0x00, true,  0xFF, N | H | C},  // carry-in alone causes C
    {SBC, 0x00, 0x00, false, 0x00, Z | N},
    {SBC, 0x01, 0x00, true,  0x00, Z | N},
    {SBC, 0x3B, 0x2A, true,  0x10, N},
    {SBC, 0x3B, 0x4F, true,  0xEB, N | H | C},
    {SBC, 0x00, 0xFF, true,  0x00, Z | N | H | C}, // b + carry = 0x100
    {SBC, 0x00, 0xFF, false, 0x01, N | H | C},
    {SBC, 0x10, 0x0F, true,  0x00, Z | N | H},
    {SBC, 0x10, 0x01, false, 0x0F, N | H},
    // AND: Z 0 1 0
    {AND, 0x5A, 0x3C, false, 0x18, H},
    {AND, 0x5A, 0xA5, false, 0x00, Z | H},
    {AND, 0xFF, 0xFF, true,  0xFF, H},          // C cleared
    {AND, 0x0F, 0xF0, true,  0x00, Z | H},
    // XOR: Z 0 0 0
    {XOR, 0x5A, 0x3C, false, 0x66, 0},
    {XOR, 0x5A, 0x5A, false, 0x00, Z},
    {XOR, 0xFF, 0x0F, true,  0xF0, 0},          // C cleared
    // OR: Z 0 0 0
    {OR, 0x5A, 0x3C, false, 0x7E, 0},
    {OR, 0x00, 0x00, false, 0x00, Z},
    {OR, 0x00, 0x80, true,  0x80, 0},           // C cleared
    {OR, 0x0F, 0x00, true,  0x0F, 0},
    // CP: flags as SUB, A unchanged
    {CP, 0x3C, 0x2F, false, 0x3C, N | H},
    {CP, 0x3C, 0x3C, false, 0x3C, Z | N},
    {CP, 0x3C, 0x40, false, 0x3C, N | C},
    {CP, 0x10, 0x01, false, 0x10, N | H},
    {CP, 0x00, 0x01, false, 0x00, N | H | C},
    {CP, 0x3C, 0x3C, true,  0x3C, Z | N},       // carry-in ignored
    {CP, 0x50, 0x20, true,  0x50, N},
};

// Distinct values in every register; A, flags and source are overwritten per case
void seedRegisters(CPU &cpu) {
    cpu.AF = 0xA100;
    cpu.BC = 0xB2C3;
    cpu.DE = 0xD4E5;
    cpu.HL = 0xC6F7;
    cpu.SP = 0xDFF0;
    cpu.PC = 0x0200;
}

// Z/N/H noise is set before the op so that "flag must be 0" is actually checked
std::uint8_t initialFlags(bool noise, bool carryIn) {
    return static_cast<std::uint8_t>((noise ? (Z | N | H) : 0) | (carryIn ? C : 0));
}

// Same cases with A as the source (b == a)
struct SelfCase {
    int op;
    std::uint8_t a;
    bool carryIn;
    std::uint8_t result;
    std::uint8_t flags;
};

const std::vector<SelfCase> kSelfCases = {
    {ADD, 0x12, false, 0x24, 0},
    {ADD, 0x08, false, 0x10, H},
    {ADD, 0x88, false, 0x10, H | C},
    {ADD, 0x80, false, 0x00, Z | C},
    {ADD, 0x00, true,  0x00, Z},
    {ADC, 0x88, true,  0x11, H | C},
    {ADC, 0x80, true,  0x01, C},
    {ADC, 0x00, true,  0x01, 0},
    {ADC, 0x00, false, 0x00, Z},
    {ADC, 0x08, false, 0x10, H},
    {SUB, 0x5A, false, 0x00, Z | N},
    {SUB, 0x5A, true,  0x00, Z | N},
    {SUB, 0x00, false, 0x00, Z | N},
    {SBC, 0x5A, false, 0x00, Z | N},
    {SBC, 0x5A, true,  0xFF, N | H | C},
    {SBC, 0x00, true,  0xFF, N | H | C},
    {AND, 0x5A, true,  0x5A, H},
    {AND, 0x00, false, 0x00, Z | H},
    {XOR, 0x5A, true,  0x00, Z},
    {XOR, 0xFF, false, 0x00, Z},
    {OR, 0x5A, true,  0x5A, 0},
    {OR, 0x00, false, 0x00, Z},
    {CP, 0x5A, true,  0x5A, Z | N},
    {CP, 0x00, false, 0x00, Z | N},
};

} // namespace

TEST_CASE_METHOD(CPUTest, "0x80-0xBF:(ALU A, r) result, flags, PC +1, cycles for B,C,D,E,H,L,(HL)") {
    const AluCase tc = GENERATE(from_range(kCases));
    const int src = GENERATE(0, 1, 2, 3, 4, 5, 6);
    const bool noise = GENERATE(false, true);
    const std::uint16_t op = regOpcode(tc.op, src);
    CAPTURE(op, opName[tc.op], regName[src], tc.a, tc.b, tc.carryIn, noise);

    seedRegisters(cpu);
    setA(cpu, tc.a);
    setF(cpu, initialFlags(noise, tc.carryIn));
    setReg(cpu, src, tc.b);

    CPU expected = cpu;
    const std::uint16_t hl = cpu.HL;

    const int cycles = cpu.executeOpcode(op);

    REQUIRE(cpu.A() == tc.result);
    REQUIRE(cpu.F() == tc.flags);
    REQUIRE(cpu.BC == expected.BC);
    REQUIRE(cpu.DE == expected.DE);
    REQUIRE(cpu.HL == expected.HL);
    REQUIRE(cpu.SP == expected.SP);
    REQUIRE(cpu.memory[hl] == expected.memory[hl]);
    REQUIRE(cpu.PC == 0x0201);
    REQUIRE(cycles == (src == 6 ? 2 : 1));
}

TEST_CASE_METHOD(CPUTest, "0x87-0xBF:(ALU A, A) source is A itself") {
    const SelfCase tc = GENERATE(from_range(kSelfCases));
    const bool noise = GENERATE(false, true);
    const std::uint16_t op = regOpcode(tc.op, 7);
    CAPTURE(op, opName[tc.op], tc.a, tc.carryIn, noise);

    seedRegisters(cpu);
    setA(cpu, tc.a);
    setF(cpu, initialFlags(noise, tc.carryIn));
    CPU expected = cpu;

    const int cycles = cpu.executeOpcode(op);

    REQUIRE(cpu.A() == tc.result);
    REQUIRE(cpu.F() == tc.flags);
    REQUIRE(cpu.BC == expected.BC);
    REQUIRE(cpu.DE == expected.DE);
    REQUIRE(cpu.HL == expected.HL);
    REQUIRE(cpu.SP == expected.SP);
    REQUIRE(cpu.PC == 0x0201);
    REQUIRE(cycles == 1);
}

TEST_CASE_METHOD(CPUTest, "0xC6-0xFE:(ALU A, d8) operand at PC+1, result, flags, PC +2, 2 cycles") {
    const AluCase tc = GENERATE(from_range(kCases));
    const bool noise = GENERATE(false, true);
    const std::uint16_t op = immOpcode(tc.op);
    CAPTURE(op, opName[tc.op], tc.a, tc.b, tc.carryIn, noise);

    seedRegisters(cpu);
    setA(cpu, tc.a);
    setF(cpu, initialFlags(noise, tc.carryIn));
    cpu.memory[0x0201] = tc.b;
    cpu.memory[cpu.HL] = static_cast<std::uint8_t>(~tc.b); // must not be used
    CPU expected = cpu;

    const int cycles = cpu.executeOpcode(op);

    REQUIRE(cpu.A() == tc.result);
    REQUIRE(cpu.F() == tc.flags);
    REQUIRE(cpu.BC == expected.BC);
    REQUIRE(cpu.DE == expected.DE);
    REQUIRE(cpu.HL == expected.HL);
    REQUIRE(cpu.SP == expected.SP);
    REQUIRE(cpu.memory[0x0201] == tc.b);
    REQUIRE(cpu.PC == 0x0202);
    REQUIRE(cycles == 2);
}

TEST_CASE_METHOD(CPUTest, "0xC6:(ADD A, d8) operand read wraps at PC = 0xFFFF") {
    cpu.PC = 0xFFFF;
    setA(cpu, 0x01);
    setF(cpu, 0);
    cpu.memory[0x0000] = 0x02;

    const int cycles = cpu.executeOpcode(0xC6);

    REQUIRE(cpu.A() == 0x03);
    REQUIRE(cpu.F() == 0);
    REQUIRE(cpu.PC == 0x0001);
    REQUIRE(cycles == 2);
}

// ---------------------------------------------------------------------------
// DAA (0x27). Meganesu: 1 byte, 1 M-cycle, flags Z - 0 CY.
// Exact semantics from rgbds gbz80(7) (linked from Pan Docs' instruction set page):
//   N=1: adj = (H ? 0x06 : 0) + (C ? 0x60 : 0); A -= adj; C unaffected.
//   N=0: adj = (H || (A & 0xF) > 9 ? 0x06 : 0) + (C || A > 0x99 ? 0x60 : 0), C set if the 0x60 part applies;
//        A += adj.
//   Z from result, N unchanged, H = 0.
// ---------------------------------------------------------------------------

namespace {

struct DaaSeq {
    int op;             // ADD/ADC/SUB/SBC via the immediate form
    std::uint8_t a;
    std::uint8_t b;
    bool carryIn;
    std::uint8_t result; // A after DAA
    std::uint8_t flags;  // F after DAA
};

const std::vector<DaaSeq> kDaaSeqs = {
    // Addition (N=0)
    {ADD, 0x45, 0x38, false, 0x83, 0},          // 0x7D: low nibble > 9
    {ADD, 0x15, 0x27, false, 0x42, 0},          // 0x3C: low nibble > 9
    {ADD, 0x12, 0x34, false, 0x46, 0},          // already BCD, no adjust
    {ADD, 0x09, 0x08, false, 0x17, 0},          // 0x11 with H=1: H drives low adjust
    {ADD, 0x38, 0x29, false, 0x67, 0},          // 0x61 with H=1
    {ADD, 0x99, 0x01, false, 0x00, Z | C},      // 0x9A: both adjusts, wraps to 0, sets C
    {ADD, 0x50, 0x50, false, 0x00, Z | C},      // 0xA0: A > 0x99 sets C
    {ADD, 0x90, 0x90, false, 0x80, C},          // 0x20 with C=1: C drives high adjust
    {ADD, 0x99, 0x99, false, 0x98, C},          // 0x32 with H=1, C=1
    {ADD, 0x00, 0x00, false, 0x00, Z},
    {ADC, 0x19, 0x28, true,  0x48, 0},          // 0x42 with H=1 (19+28+1=48)
    {ADC, 0x49, 0x50, true,  0x00, Z | C},      // 0x9A -> 100
    // Subtraction (N=1)
    {SUB, 0x83, 0x38, false, 0x45, N},          // 0x4B with H=1
    {SUB, 0x10, 0x01, false, 0x09, N},          // 0x0F with H=1
    {SUB, 0x47, 0x12, false, 0x35, N},          // no adjust
    {SUB, 0x20, 0x30, false, 0x90, N | C},      // 0xF0 with C=1: high adjust, C stays
    {SUB, 0x00, 0x01, false, 0x99, N | C},      // 0xFF with H=1, C=1
    {SUB, 0x45, 0x45, false, 0x00, Z | N},
    {SBC, 0x50, 0x25, true,  0x24, N},          // 0x2A with H=1 (50-25-1=24)
    {SBC, 0x00, 0x00, true,  0x99, N | C},      // 0xFF with H=1, C=1
};

struct DaaDirect {
    std::uint8_t a;
    std::uint8_t fIn;
    std::uint8_t result;
    std::uint8_t flags;
};

const std::vector<DaaDirect> kDaaDirect = {
    {0x9A, 0,         0x00, Z | C},      // N=0: A > 0x99 and low > 9
    {0x00, C,         0x60, C},          // N=0: C alone drives +0x60, C stays set
    {0x00, H,         0x06, 0},          // N=0: H alone drives +0x06, H cleared
    {0x00, Z,         0x00, Z},          // Z recomputed from result
    {0x45, Z | H,     0x4B, 0},          // stale Z cleared, H cleared
    {0xA0, 0,         0x00, Z | C},      // N=0: high nibble > 9 only
    {0x0A, 0,         0x10, 0},          // N=0: low nibble > 9 only
    {0xAA, N,         0xAA, N},          // N=1: no H/C -> no adjust even if not BCD
    {0x0F, N | H,     0x09, N},          // N=1: H -> -6, H cleared
    {0xF0, N | C,     0x90, N | C},      // N=1: C -> -0x60, C unaffected (stays 1)
    {0x66, N | H | C, 0x00, Z | N | C},  // N=1: both, zero result
};

} // namespace

TEST_CASE_METHOD(CPUTest, "0x27:(DAA) after ADD/ADC/SUB/SBC sequences") {
    const DaaSeq tc = GENERATE(from_range(kDaaSeqs));
    CAPTURE(opName[tc.op], tc.a, tc.b, tc.carryIn);

    seedRegisters(cpu);
    setA(cpu, tc.a);
    setF(cpu, tc.carryIn ? C : 0);
    cpu.memory[0x0201] = tc.b;

    REQUIRE(cpu.executeOpcode(immOpcode(tc.op)) == 2);
    REQUIRE(cpu.PC == 0x0202);
    const std::uint8_t fBefore = cpu.F();
    CAPTURE(cpu.A(), fBefore);

    CPU expected = cpu;
    const int cycles = cpu.executeOpcode(0x27);

    REQUIRE(cpu.A() == tc.result);
    REQUIRE(cpu.F() == tc.flags);
    REQUIRE((cpu.F() & N) == (fBefore & N)); // N unchanged
    REQUIRE(cpu.BC == expected.BC);
    REQUIRE(cpu.DE == expected.DE);
    REQUIRE(cpu.HL == expected.HL);
    REQUIRE(cpu.SP == expected.SP);
    REQUIRE(cpu.PC == 0x0203);
    REQUIRE(cycles == 1);
}

TEST_CASE_METHOD(CPUTest, "0x27:(DAA) from directly set A and flags") {
    const DaaDirect tc = GENERATE(from_range(kDaaDirect));
    CAPTURE(tc.a, tc.fIn);

    seedRegisters(cpu);
    setA(cpu, tc.a);
    setF(cpu, tc.fIn);

    const int cycles = cpu.executeOpcode(0x27);

    REQUIRE(cpu.A() == tc.result);
    REQUIRE(cpu.F() == tc.flags);
    REQUIRE(cpu.PC == 0x0201);
    REQUIRE(cycles == 1);
}
