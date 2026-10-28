// CPU side of interrupts and HALT, driven through step() (interrupts are only checked there).
// Spec sources:
//   Pan Docs, Interrupts (https://gbdev.io/pandocs/Interrupts.html):
//     - IE ($FFFF) / IF ($FF0F) bits 0-4: VBlank, LCD STAT, Timer, Serial, Joypad. Only bits 0-4
//       name interrupt sources; bits 5-7 have none.
//     - An interrupt is handled only when IME is set and both its IE and IF bits are set.
//     - Handling: IF bit and IME are cleared, 2 wait M-cycles, PC pushed (2 M-cycles),
//       PC set to $40/$48/$50/$58/$60 (1 M-cycle): 5 M-cycles in total.
//     - Several pending: the lowest bit (highest priority) is serviced first.
//     - EI takes effect one instruction later; EI followed by DI lets no interrupt through.
//   Pan Docs, HALT (https://gbdev.io/pandocs/halt.html):
//     - The CPU wakes as soon as IE & IF != 0.
//     - IME set: the handler is called before the instruction after HALT is executed.
//     - IME clear, nothing pending: wait for IE & IF != 0, then continue without servicing.
//     - IME clear, interrupt pending when HALT runs: HALT exits immediately and the byte after
//       HALT is read twice (the halt bug).
//     - EI right before a bugged HALT: the interrupt is serviced and the handler returns to the HALT.
//     - RST right after a bugged HALT: the pushed return address points at the RST itself.
//     - No cycle cost is given for waking from HALT, so these tests never assert one
//       (they step a bounded number of times instead).
//   RGBDS gbz80(7) (https://rgbds.gbdev.io/docs/gbz80.7):
//     - EI: "The flag is only set after the instruction following EI."
//     - RETI: return from subroutine and enable interrupts; IME is set right after this
//       instruction (no delay, unlike EI).
//     - HALT: enter low-power mode until an interrupt occurs.
#include "test_fixture.hpp"

#include <cstdint>
#include <vector>

namespace {

constexpr std::uint8_t NOP = 0x00, INC_A = 0x3C, INC_B = 0x04, LD_A_D8 = 0x3E, HALT = 0x76,
                       DI = 0xF3, EI = 0xFB, RET = 0xC9, RETI = 0xD9, RST_08 = 0xCF;

constexpr std::uint16_t kVector[5] = {0x40, 0x48, 0x50, 0x58, 0x60};
constexpr const char *kName[5] = {"VBlank", "Stat", "Timer", "Serial", "Joypad"};

constexpr std::uint8_t bit(int i) { return static_cast<std::uint8_t>(1u << i); }

std::uint8_t &IE(FlatMemory &memory) { return memory[ieAddress]; }
std::uint8_t &IF(FlatMemory &memory) { return memory[ifAddress]; }

// Known register state, all memory NOP, IE = IF = 0, IME off
void setup(CPU &cpu, FlatMemory &memory) {
    memory = FlatMemory{};
    cpu.AF = 0x0000;
    cpu.BC = 0x0000;
    cpu.DE = 0xD4E5;
    cpu.HL = 0xC6F7;
    cpu.SP = 0xDFF0;
    cpu.PC = 0x0200;
    cpu.ime = false;
    cpu.halted = false;
    cpu.locked = false;
}

std::uint16_t stackWord(const FlatMemory &memory, std::uint16_t addr) {
    return static_cast<std::uint16_t>(memory[addr] | (memory[static_cast<std::uint16_t>(addr + 1)] << 8));
}

// Steps until pred() holds, at most maxSteps times. Returns true if pred() became true.
// Records every PC seen before each step.
template <typename Pred>
bool stepUntil(CPU &cpu, Pred pred, int maxSteps, std::vector<std::uint16_t> *pcs = nullptr) {
    for (int i = 0; i < maxSteps; ++i) {
        if (pred()) return true;
        if (pcs) pcs->push_back(cpu.PC);
        cpu.step();
    }
    return pred();
}

} // namespace

// ---------------------------------------------------------------------------
// requestInterrupt
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(CPUTest, "requestInterrupt() sets only its IF bit") {
    setup(cpu, memory);
    const int i = GENERATE(range(0, 5));
    CAPTURE(kName[i]);
    IF(memory) = 0x00;
    cpu.requestInterrupt(static_cast<Interrupt>(i));
    REQUIRE((IF(memory) & 0x1F) == bit(i));

    IF(memory) = static_cast<std::uint8_t>(0x1F & ~bit(i));
    cpu.requestInterrupt(static_cast<Interrupt>(i));
    REQUIRE((IF(memory) & 0x1F) == 0x1F);
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(CPUTest, "Interrupt dispatch: IF bit and IME cleared, PC pushed, jump to vector, 5 cycles") {
    setup(cpu, memory);
    const int i = GENERATE(range(0, 5));
    CAPTURE(kName[i]);
    cpu.PC = 0x1234;
    memory[0x1234] = INC_A; // must not be executed
    cpu.ime = true;
    IE(memory) = bit(i);           // only this one enabled
    IF(memory) = 0x1F;             // everything requested
    CPU before = cpu;

    const int cycles = cpu.step();

    REQUIRE(cpu.PC == kVector[i]);
    REQUIRE_FALSE(cpu.ime);
    REQUIRE((IF(memory) & 0x1F) == (0x1F & ~bit(i))); // only the serviced bit is cleared
    REQUIRE(IE(memory) == bit(i));
    REQUIRE(cpu.SP == 0xDFEE);
    REQUIRE(memory[0xDFEE] == 0x34); // low byte at SP
    REQUIRE(memory[0xDFEF] == 0x12); // high byte at SP+1
    REQUIRE(cycles == 5);
    REQUIRE(cpu.AF == before.AF); // INC A not executed, flags untouched
    REQUIRE(cpu.BC == before.BC);
    REQUIRE(cpu.DE == before.DE);
    REQUIRE(cpu.HL == before.HL);
    REQUIRE_FALSE(cpu.halted);
}

TEST_CASE_METHOD(CPUTest, "Interrupt dispatch via requestInterrupt() with all of IE enabled") {
    setup(cpu, memory);
    const int i = GENERATE(range(0, 5));
    CAPTURE(kName[i]);
    cpu.ime = true;
    IE(memory) = 0x1F;
    cpu.requestInterrupt(static_cast<Interrupt>(i));

    REQUIRE(cpu.step() == 5);
    REQUIRE(cpu.PC == kVector[i]);
    REQUIRE((IF(memory) & 0x1F) == 0x00);
    REQUIRE(stackWord(memory, cpu.SP) == 0x0200);
    REQUIRE_FALSE(cpu.ime);
}

// ---------------------------------------------------------------------------
// No dispatch
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(CPUTest, "No dispatch when IME is 0: instruction at PC runs, IF kept") {
    setup(cpu, memory);
    const int i = GENERATE(range(0, 5));
    CAPTURE(kName[i]);
    memory[0x0200] = INC_A;
    IE(memory) = 0x1F;
    IF(memory) = bit(i);

    REQUIRE(cpu.step() == 1);
    REQUIRE(cpu.PC == 0x0201);
    REQUIRE(cpu.A() == 0x01);
    REQUIRE((IF(memory) & 0x1F) == bit(i));
    REQUIRE(cpu.SP == 0xDFF0);
    REQUIRE_FALSE(cpu.ime);
}

TEST_CASE_METHOD(CPUTest, "No dispatch when the IE bit of the requested interrupt is clear") {
    setup(cpu, memory);
    const int i = GENERATE(range(0, 5));
    CAPTURE(kName[i]);
    memory[0x0200] = INC_A;
    cpu.ime = true;
    IE(memory) = static_cast<std::uint8_t>(0x1F & ~bit(i));
    IF(memory) = bit(i);

    REQUIRE(cpu.step() == 1);
    REQUIRE(cpu.PC == 0x0201);
    REQUIRE(cpu.A() == 0x01);
    REQUIRE((IF(memory) & 0x1F) == bit(i));
    REQUIRE(cpu.SP == 0xDFF0);
    REQUIRE(cpu.ime);
}

TEST_CASE_METHOD(CPUTest, "No dispatch when only IF bits 5-7 are set (no interrupt source there)") {
    setup(cpu, memory);
    memory[0x0200] = INC_A;
    cpu.ime = true;
    IE(memory) = 0xFF;
    IF(memory) = 0xE0;

    REQUIRE(cpu.step() == 1);
    REQUIRE(cpu.PC == 0x0201);
    REQUIRE(cpu.A() == 0x01);
    REQUIRE(cpu.SP == 0xDFF0);
    REQUIRE(cpu.ime);
}

// ---------------------------------------------------------------------------
// Priority and RETI
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(CPUTest, "Priority: lowest pending bit first, then the next after RETI") {
    setup(cpu, memory);
    for (auto v : kVector) memory[v] = RETI;
    memory[0x0200] = INC_A;
    cpu.ime = true;
    IE(memory) = 0x1F;
    IF(memory) = 0x1F;

    for (int i = 0; i < 5; ++i) {
        CAPTURE(kName[i]);
        REQUIRE(cpu.step() == 5);
        REQUIRE(cpu.PC == kVector[i]);
        REQUIRE(stackWord(memory, cpu.SP) == 0x0200);
        REQUIRE((IF(memory) & 0x1F) == (0x1F & ~((bit(i) << 1) - 1))); // this and all higher-priority bits cleared
        REQUIRE_FALSE(cpu.ime);
        REQUIRE(cpu.step() == 4); // RETI
        REQUIRE(cpu.PC == 0x0200);
        REQUIRE(cpu.SP == 0xDFF0);
        REQUIRE(cpu.ime);
    }
    REQUIRE(cpu.A() == 0x00); // INC A never ran while interrupts were pending
    REQUIRE(cpu.step() == 1);
    REQUIRE(cpu.A() == 0x01);
    REQUIRE(cpu.PC == 0x0201);
}

TEST_CASE_METHOD(CPUTest, "Priority: a higher-priority request wins even if a lower one was pending first") {
    setup(cpu, memory);
    cpu.ime = true;
    IE(memory) = 0x1F;
    cpu.requestInterrupt(Interrupt::Joypad);
    cpu.requestInterrupt(Interrupt::Timer);

    REQUIRE(cpu.step() == 5);
    REQUIRE(cpu.PC == 0x50);
    REQUIRE((IF(memory) & 0x1F) == bit(4));
}

TEST_CASE_METHOD(CPUTest, "RETI in a handler returns and re-enables IME; a second pending interrupt is then serviced") {
    setup(cpu, memory);
    memory[0x40] = NOP;
    memory[0x41] = RETI;
    memory[0x50] = RETI;
    memory[0x0200] = INC_A;
    cpu.ime = true;
    IE(memory) = 0x1F;
    cpu.requestInterrupt(Interrupt::VBlank);

    REQUIRE(cpu.step() == 5); // VBlank
    REQUIRE(cpu.PC == 0x40);

    cpu.requestInterrupt(Interrupt::Timer); // arrives inside the handler, IME = 0 blocks it
    REQUIRE(cpu.step() == 1); // NOP in handler
    REQUIRE(cpu.PC == 0x41);
    REQUIRE((IF(memory) & 0x1F) == bit(2));

    REQUIRE(cpu.step() == 4); // RETI
    REQUIRE(cpu.PC == 0x0200);
    REQUIRE(cpu.SP == 0xDFF0);
    REQUIRE(cpu.ime);

    REQUIRE(cpu.step() == 5); // Timer serviced before INC A
    REQUIRE(cpu.PC == 0x50);
    REQUIRE(stackWord(memory, cpu.SP) == 0x0200);
    REQUIRE(cpu.A() == 0x00);
    REQUIRE_FALSE(cpu.ime);
}

TEST_CASE_METHOD(CPUTest, "RET (not RETI) in a handler leaves IME off; pending interrupt not serviced") {
    setup(cpu, memory);
    memory[0x40] = RET;
    memory[0x0200] = INC_A;
    cpu.ime = true;
    IE(memory) = 0x1F;
    IF(memory) = bit(0) | bit(2);

    REQUIRE(cpu.step() == 5);
    REQUIRE(cpu.PC == 0x40);
    REQUIRE(cpu.step() == 4); // RET
    REQUIRE(cpu.PC == 0x0200);
    REQUIRE_FALSE(cpu.ime);
    REQUIRE(cpu.step() == 1); // INC A, Timer still pending
    REQUIRE(cpu.A() == 0x01);
    REQUIRE((IF(memory) & 0x1F) == bit(2));
}

// ---------------------------------------------------------------------------
// EI delay
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(CPUTest, "EI; NOP with an interrupt pending: serviced only after the NOP") {
    setup(cpu, memory);
    memory[0x0200] = EI;
    memory[0x0201] = NOP;
    memory[0x0202] = INC_A;
    IE(memory) = 0x1F;
    cpu.requestInterrupt(Interrupt::Serial);

    REQUIRE(cpu.step() == 1); // EI
    REQUIRE(cpu.PC == 0x0201);
    REQUIRE(cpu.SP == 0xDFF0);
    REQUIRE(cpu.step() == 1); // NOP still runs
    REQUIRE(cpu.PC == 0x0202);
    REQUIRE(cpu.SP == 0xDFF0);
    REQUIRE((IF(memory) & 0x1F) == bit(3));

    REQUIRE(cpu.step() == 5); // now serviced
    REQUIRE(cpu.PC == 0x58);
    REQUIRE(stackWord(memory, cpu.SP) == 0x0202); // points after the NOP
    REQUIRE(cpu.SP == 0xDFEE);
    REQUIRE(cpu.A() == 0x00);
    REQUIRE((IF(memory) & 0x1F) == 0x00);
}

TEST_CASE_METHOD(CPUTest, "EI; DI with an interrupt pending: never serviced") {
    setup(cpu, memory);
    memory[0x0200] = EI;
    memory[0x0201] = DI;
    IE(memory) = 0x1F;
    cpu.requestInterrupt(Interrupt::VBlank);

    for (int s = 0; s < 6; ++s) {
        CAPTURE(s);
        REQUIRE(cpu.step() == 1);
        REQUIRE(cpu.PC == 0x0201 + s);
        REQUIRE(cpu.SP == 0xDFF0);
        REQUIRE_FALSE(cpu.ime);
    }
    REQUIRE((IF(memory) & 0x1F) == bit(0));
}

// ---------------------------------------------------------------------------
// HALT, IME = 1
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(CPUTest, "HALT with IME=1: stays halted until requested, then services and returns after HALT") {
    setup(cpu, memory);
    memory[0x0200] = HALT;
    memory[0x0201] = INC_A;
    memory[0x50] = RETI;
    cpu.ime = true;
    IE(memory) = bit(2);

    cpu.step(); // HALT
    REQUIRE(cpu.halted);
    const std::uint16_t haltPC = cpu.PC;
    for (int s = 0; s < 4; ++s) {
        CAPTURE(s);
        REQUIRE(cpu.step() == 1);
        REQUIRE(cpu.halted);
        REQUIRE(cpu.PC == haltPC);
        REQUIRE(cpu.A() == 0x00);
    }

    // A request with its IE bit clear does not wake the CPU
    cpu.requestInterrupt(Interrupt::Joypad);
    REQUIRE(cpu.step() == 1);
    REQUIRE(cpu.halted);
    REQUIRE(cpu.PC == haltPC);

    cpu.requestInterrupt(Interrupt::Timer);
    // Wake-up cost is unspecified in Pan Docs: allow a few steps, don't check cycles
    REQUIRE(stepUntil(cpu, [&] { return cpu.PC == 0x50; }, 3));
    REQUIRE_FALSE(cpu.halted);
    REQUIRE(cpu.A() == 0x00); // handler before the instruction after HALT
    REQUIRE(cpu.SP == 0xDFEE);
    REQUIRE(stackWord(memory, cpu.SP) == 0x0201);
    REQUIRE((IF(memory) & 0x1F) == bit(4)); // Timer cleared, Joypad kept
    REQUIRE_FALSE(cpu.ime);

    REQUIRE(cpu.step() == 4); // RETI
    REQUIRE(cpu.PC == 0x0201);
    REQUIRE(cpu.step() == 1); // INC A
    REQUIRE(cpu.A() == 0x01);
    REQUIRE(cpu.PC == 0x0202);
}

TEST_CASE_METHOD(CPUTest, "HALT with IME=1: interrupt pending before HALT is serviced first; HALT then halts normally (no halt bug)") {
    setup(cpu, memory);
    memory[0x0200] = HALT;
    memory[0x0201] = INC_A;
    memory[0x0202] = NOP;
    memory[0x40] = RETI;
    cpu.ime = true;
    IE(memory) = 0x1F;
    IF(memory) = 0x00;

    // With IME=1 a pending interrupt is dispatched before HALT is even fetched,
    // so HALT is never bugged; it is reached via the handler's RETI and halts normally.
    cpu.requestInterrupt(Interrupt::VBlank);
    REQUIRE(cpu.step() == 5); // serviced before HALT even runs
    REQUIRE(cpu.PC == 0x40);
    REQUIRE(stackWord(memory, cpu.SP) == 0x0200);
    REQUIRE(cpu.step() == 4); // RETI -> HALT
    REQUIRE(cpu.PC == 0x0200);

    cpu.step(); // HALT, nothing pending
    REQUIRE(cpu.halted);
    cpu.requestInterrupt(Interrupt::VBlank);
    REQUIRE(stepUntil(cpu, [&] { return cpu.PC == 0x40; }, 3));
    REQUIRE(stackWord(memory, cpu.SP) == 0x0201);
    REQUIRE(cpu.step() == 4); // RETI
    REQUIRE(cpu.step() == 1); // INC A runs once only
    REQUIRE(cpu.A() == 0x01);
    REQUIRE(cpu.PC == 0x0202);
}

// ---------------------------------------------------------------------------
// HALT, IME = 0, nothing pending
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(CPUTest, "HALT with IME=0: stays halted, then resumes after HALT without servicing") {
    setup(cpu, memory);
    const int i = GENERATE(range(0, 5));
    CAPTURE(kName[i]);
    memory[0x0200] = HALT;
    memory[0x0201] = INC_A;
    memory[0x0202] = NOP;
    IE(memory) = bit(i);

    cpu.step(); // HALT
    REQUIRE(cpu.halted);
    const std::uint16_t haltPC = cpu.PC;
    for (int s = 0; s < 4; ++s) {
        CAPTURE(s);
        REQUIRE(cpu.step() == 1);
        REQUIRE(cpu.halted);
        REQUIRE(cpu.PC == haltPC);
    }

    cpu.requestInterrupt(static_cast<Interrupt>(i));
    std::vector<std::uint16_t> pcs;
    REQUIRE(stepUntil(cpu, [&] { return cpu.A() != 0; }, 3, &pcs));
    for (auto pc : pcs) REQUIRE(pc != kVector[i]);
    REQUIRE_FALSE(cpu.halted);
    REQUIRE(cpu.A() == 0x01); // INC A ran exactly once
    REQUIRE(cpu.PC == 0x0202);
    REQUIRE(cpu.SP == 0xDFF0); // nothing pushed
    REQUIRE(memory[0xDFEE] == 0x00);
    REQUIRE(memory[0xDFEF] == 0x00);
    REQUIRE((IF(memory) & 0x1F) == bit(i)); // not acknowledged
    REQUIRE_FALSE(cpu.ime);
}

// ---------------------------------------------------------------------------
// HALT bug (IME = 0, interrupt pending when HALT runs)
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(CPUTest, "HALT bug: HALT; INC A runs INC A twice") {
    setup(cpu, memory);
    memory[0x0200] = HALT;
    memory[0x0201] = INC_A;
    memory[0x0202] = NOP;
    IE(memory) = 0x1F;
    cpu.requestInterrupt(Interrupt::Timer);

    cpu.step(); // HALT exits immediately
    REQUIRE_FALSE(cpu.halted);
    REQUIRE(cpu.step() == 1); // INC A
    REQUIRE(cpu.A() == 0x01);
    REQUIRE(cpu.PC == 0x0201); // byte re-read
    REQUIRE(cpu.step() == 1); // INC A again
    REQUIRE(cpu.A() == 0x02);
    REQUIRE(cpu.PC == 0x0202);
    REQUIRE(cpu.step() == 1); // NOP
    REQUIRE(cpu.A() == 0x02);
    REQUIRE(cpu.PC == 0x0203);
    REQUIRE(cpu.SP == 0xDFF0);
    REQUIRE((IF(memory) & 0x1F) == bit(2));
}

TEST_CASE_METHOD(CPUTest, "HALT bug: HALT; LD A,d8 loads 0x3E, then runs the d8 byte as an opcode") {
    setup(cpu, memory);
    memory[0x0200] = HALT;
    memory[0x0201] = LD_A_D8;
    memory[0x0202] = INC_B;
    memory[0x0203] = NOP;
    IE(memory) = 0x1F;
    cpu.requestInterrupt(Interrupt::VBlank);

    cpu.step(); // HALT
    REQUIRE_FALSE(cpu.halted);
    REQUIRE(cpu.step() == 2); // LD A,d8 with d8 = the 0x3E byte itself
    REQUIRE(cpu.A() == 0x3E);
    REQUIRE(cpu.PC == 0x0202);
    REQUIRE(cpu.step() == 1); // INC B (the intended operand byte)
    REQUIRE(cpu.B() == 0x01);
    REQUIRE(cpu.PC == 0x0203);
}

TEST_CASE_METHOD(CPUTest, "HALT bug: only the next byte is repeated, not every later one") {
    setup(cpu, memory);
    memory[0x0200] = HALT;
    memory[0x0201] = INC_A;
    memory[0x0202] = INC_B;
    memory[0x0203] = NOP;
    IE(memory) = 0x1F;
    cpu.requestInterrupt(Interrupt::Serial);

    for (int s = 0; s < 4; ++s) cpu.step(); // HALT, INC A, INC A, INC B
    REQUIRE(cpu.A() == 0x02);
    REQUIRE(cpu.B() == 0x01);
    REQUIRE(cpu.PC == 0x0203);
}

TEST_CASE_METHOD(CPUTest, "HALT bug: EI right before HALT -> serviced, handler RETI returns to the HALT, which halts again") {
    setup(cpu, memory);
    memory[0x0200] = EI;
    memory[0x0201] = HALT;
    memory[0x0202] = INC_A;
    memory[0x0203] = NOP;
    memory[0x40] = RETI;
    IE(memory) = 0x1F;
    cpu.requestInterrupt(Interrupt::VBlank);

    REQUIRE(cpu.step() == 1); // EI
    cpu.step();               // HALT (bugged)
    REQUIRE(cpu.step() == 5); // interrupt serviced
    REQUIRE(cpu.PC == 0x40);
    REQUIRE(cpu.SP == 0xDFEE);
    REQUIRE(stackWord(memory, cpu.SP) == 0x0201); // return address is the HALT itself
    REQUIRE(cpu.A() == 0x00);

    REQUIRE(cpu.step() == 4); // RETI
    REQUIRE(cpu.PC == 0x0201);
    REQUIRE(cpu.ime);

    cpu.step(); // HALT again, nothing pending now -> halts
    REQUIRE(cpu.halted);
    const std::uint16_t haltPC = cpu.PC;
    for (int s = 0; s < 3; ++s) {
        REQUIRE(cpu.step() == 1);
        REQUIRE(cpu.halted);
        REQUIRE(cpu.PC == haltPC);
        REQUIRE(cpu.A() == 0x00);
    }

    cpu.requestInterrupt(Interrupt::VBlank);
    REQUIRE(stepUntil(cpu, [&] { return cpu.PC == 0x40; }, 3));
    REQUIRE(stackWord(memory, cpu.SP) == 0x0202);
    REQUIRE(cpu.step() == 4); // RETI
    REQUIRE(cpu.step() == 1); // INC A, once
    REQUIRE(cpu.A() == 0x01);
    REQUIRE(cpu.PC == 0x0203);
}

TEST_CASE_METHOD(CPUTest, "HALT bug: RST right after HALT pushes the address of the RST itself") {
    setup(cpu, memory);
    memory[0x0200] = HALT;
    memory[0x0201] = RST_08;
    memory[0x0008] = RET;
    IE(memory) = 0x1F;
    cpu.requestInterrupt(Interrupt::Stat);

    cpu.step(); // HALT (bugged)
    REQUIRE_FALSE(cpu.halted);
    REQUIRE(cpu.step() == 4); // RST 08
    REQUIRE(cpu.PC == 0x0008);
    REQUIRE(cpu.SP == 0xDFEE);
    REQUIRE(stackWord(memory, cpu.SP) == 0x0201); // points at the RST

    REQUIRE(cpu.step() == 4); // RET
    REQUIRE(cpu.PC == 0x0201);
    REQUIRE(cpu.SP == 0xDFF0);

    REQUIRE(cpu.step() == 4); // RST 08 again, now normal
    REQUIRE(cpu.PC == 0x0008);
    REQUIRE(stackWord(memory, cpu.SP) == 0x0202);
    REQUIRE((IF(memory) & 0x1F) == bit(1)); // never serviced, IME = 0
}
