// GameBoy power-up state and serial port tests
// Spec: Pan Docs, Power Up Sequence (https://gbdev.io/pandocs/Power_Up_Sequence.html)
//       and Serial Data Transfer (Link Cable) (https://gbdev.io/pandocs/Serial_Data_Transfer_(Link_Cable).html)
#include "catch.hpp"
#include "../main/Bus.hpp"
#include "../main/CPU.hpp"
#include "../main/GameBoy.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::size_t romSize = 0x8000; // 32 KiB, ROM-only
constexpr std::uint16_t programStart = 0x0150;

// Header checksum over 0134-014C, as the boot ROM computes it (Pan Docs: The Cartridge Header, 014D)
std::uint8_t computeHeaderChecksum(const std::vector<std::uint8_t> &rom) {
    std::uint8_t x = 0;
    for (std::size_t a = 0x0134; a <= 0x014C; ++a) {
        x = static_cast<std::uint8_t>(x - rom[a] - 1);
    }
    return x;
}

/**
 * 32 KiB ROM image: JP 0150 at 0100, `program` at 0150, title "TEST", the given cartridge type,
 * 32 KiB ROM / no RAM, and a correct header checksum at 014D.
 * With `zeroChecksum`, title byte 0134 is chosen so that the (correct) checksum byte is 0x00.
 */
std::vector<std::uint8_t> makeRom(std::initializer_list<std::uint8_t> program, std::uint8_t type = 0x00,
                                  bool zeroChecksum = false) {
    std::vector<std::uint8_t> rom(romSize, 0x00);
    rom[0x0100] = 0xC3; // JP a16
    rom[0x0101] = lowByte(programStart);
    rom[0x0102] = highByte(programStart);
    const char title[] = "TEST";
    for (std::size_t i = 0; i < 4; ++i) {
        rom[0x0134 + i] = static_cast<std::uint8_t>(title[i]);
    }
    rom[0x0147] = type;
    rom[0x0148] = 0x00; // 32 KiB
    rom[0x0149] = 0x00; // no RAM
    if (zeroChecksum) {
        rom[0x0134] = 0x00;
        rom[0x0134] = computeHeaderChecksum(rom); // lowers the checksum by exactly that value, to 0
    }
    rom[0x014D] = computeHeaderChecksum(rom);

    std::size_t addr = programStart;
    for (std::uint8_t b : program) {
        rom[addr++] = b;
    }
    return rom;
}

// Steps through JP 0150 and then `instructions` program instructions
void run(GameBoy &gb, int instructions) {
    for (int i = 0; i < 1 + instructions; ++i) {
        gb.step();
    }
}

bool flag(const CPU &cpu, std::uint16_t f) { return (cpu.F() & f) != 0; }

constexpr std::uint8_t ifSerialBit = 1 << 3;

void sendOnBus(Bus &bus, std::uint8_t value) {
    bus.write(Bus::serialTransferData, value);
    bus.write(Bus::serialControl, 0x81); // start, internal clock
}

} // namespace

// ---------------------------------------------------------------------------
// Post-boot CPU state (Pan Docs: Power Up Sequence, CPU registers, DMG column)
// ---------------------------------------------------------------------------

TEST_CASE("GameBoy: post-boot DMG CPU registers", "[gameboy][powerup]") {
    const bool zeroChecksum = GENERATE(false, true);
    auto rom = makeRom({}, 0x00, zeroChecksum);
    CAPTURE(zeroChecksum, static_cast<int>(rom[0x014D]));
    GameBoy gb(rom);
    const CPU &cpu = gb.cpuState();

    CHECK(cpu.A() == 0x01);
    CHECK(cpu.B() == 0x00);
    CHECK(cpu.C() == 0x13);
    CHECK(cpu.D() == 0x00);
    CHECK(cpu.E() == 0xD8);
    CHECK(cpu.H() == 0x01);
    CHECK(cpu.L() == 0x4D);
    CHECK(cpu.SP == 0xFFFE);
    CHECK(cpu.PC == 0x0100);
    CHECK((cpu.F() & 0x0F) == 0x00); // lower nibble of F is always 0
}

TEST_CASE("GameBoy: post-boot flags, header checksum byte non-zero -> Z=1 N=0 H=1 C=1", "[gameboy][powerup]") {
    auto rom = makeRom({}, 0x00, false);
    REQUIRE(rom[0x014D] != 0x00);
    GameBoy gb(rom);
    const CPU &cpu = gb.cpuState();
    CHECK(flag(cpu, zFlag));
    CHECK_FALSE(flag(cpu, nFlag));
    CHECK(flag(cpu, hFlag));
    CHECK(flag(cpu, cFlag));
    CHECK(cpu.F() == 0xB0);
}

TEST_CASE("GameBoy: post-boot flags, header checksum byte 0x00 -> Z=1 N=0 H=0 C=0", "[gameboy][powerup]") {
    auto rom = makeRom({}, 0x00, true);
    REQUIRE(rom[0x014D] == 0x00);
    GameBoy gb(rom);
    const CPU &cpu = gb.cpuState();
    CHECK(flag(cpu, zFlag));
    CHECK_FALSE(flag(cpu, nFlag));
    CHECK_FALSE(flag(cpu, hFlag));
    CHECK_FALSE(flag(cpu, cFlag));
    CHECK(cpu.F() == 0x80);
}

TEST_CASE("GameBoy: post-boot IME off, not halted, not locked", "[gameboy][powerup]") {
    GameBoy gb(makeRom({}));
    const CPU &cpu = gb.cpuState();
    CHECK_FALSE(cpu.ime);
    CHECK_FALSE(cpu.halted);
    CHECK_FALSE(cpu.locked);
}

TEST_CASE("GameBoy: execution starts at 0100 (JP 0150 lands at 0150)", "[gameboy][powerup]") {
    GameBoy gb(makeRom({0x00})); // NOP
    gb.step();
    CHECK(gb.cpuState().PC == 0x0150);
    gb.step();
    CHECK(gb.cpuState().PC == 0x0151);
}

// ---------------------------------------------------------------------------
// Post-boot hardware registers (Pan Docs: Power Up Sequence, hardware registers, DMG column)
// Read by a program, since GameBoy exposes no bus accessor.
// ---------------------------------------------------------------------------

TEST_CASE("GameBoy: post-boot IF (FF0F) = E1 and IE (FFFF) = 00", "[gameboy][powerup]") {
    GameBoy gb(makeRom({
        0xF0, 0x0F, // LDH A,(0F)
        0x47,       // LD B,A
        0xF0, 0xFF, // LDH A,(FF)
        0x4F,       // LD C,A
        0x18, 0xFE, // JR -2
    }));
    run(gb, 4);
    const CPU &cpu = gb.cpuState();
    REQUIRE(cpu.PC == 0x0156);
    CHECK(cpu.B() == 0xE1);
    CHECK(cpu.C() == 0x00);
}

TEST_CASE("GameBoy: post-boot SB (FF01) = 00 and SC (FF02) = 7E", "[gameboy][powerup][serial]") {
    GameBoy gb(makeRom({
        0xF0, 0x01, // LDH A,(01)
        0x57,       // LD D,A
        0xF0, 0x02, // LDH A,(02)
        0x5F,       // LD E,A
        0x18, 0xFE, // JR -2
    }));
    run(gb, 4);
    const CPU &cpu = gb.cpuState();
    REQUIRE(cpu.PC == 0x0156);
    CHECK(cpu.D() == 0x00);
    CHECK(cpu.E() == 0x7E);
    CHECK(gb.serialOutput().empty());
}

// DMG / MGB column of Pan Docs Power_Up_Sequence (LY is 91 only on DMG0)
TEST_CASE("GameBoy: post-boot LY, NR52, NR10, LCDC and DIV", "[gameboy][powerup]") {
    GameBoy gb(makeRom({
        0xF0, 0x44, 0x47, // LDH A,(44) LY;   LD B,A
        0xF0, 0x26, 0x4F, // LDH A,(26) NR52; LD C,A
        0xF0, 0x10, 0x57, // LDH A,(10) NR10; LD D,A
        0xF0, 0x40, 0x5F, // LDH A,(40) LCDC; LD E,A
        0xF0, 0x04, 0x67, // LDH A,(04) DIV;  LD H,A
        0x18, 0xFE,       // JR -2
    }));
    run(gb, 10);
    const CPU &cpu = gb.cpuState();
    REQUIRE(cpu.PC == 0x015F);
    CHECK(cpu.B() == 0x00);
    CHECK(cpu.C() == 0xF1);
    CHECK(cpu.D() == 0x80);
    CHECK(cpu.E() == 0x91);
    CHECK(cpu.H() == 0xAB);
}

// ---------------------------------------------------------------------------
// Serial port on a plain Bus (Pan Docs: Serial Data Transfer)
// Real transfers take 8 bit times at 8192 Hz; timing is not asserted (completes on the write here,
// which the spec does not describe).
// ---------------------------------------------------------------------------

TEST_CASE("Bus serial: register addresses are FF01 (SB) and FF02 (SC)", "[bus][serial]") {
    STATIC_REQUIRE(Bus::serialTransferData == 0xFF01);
    STATIC_REQUIRE(Bus::serialControl == 0xFF02);
}

TEST_CASE("Bus serial: SB then 0x81 to SC sends the byte", "[bus][serial]") {
    const std::uint8_t value = GENERATE(as<std::uint8_t>{}, 0x00, 0x41, 0x7F, 0x80, 0xFF);
    CAPTURE(static_cast<int>(value));
    Bus bus;
    REQUIRE(bus.serialOutput().empty());
    REQUIRE((bus.read(ifAddress) & ifSerialBit) == 0);

    sendOnBus(bus, value);

    REQUIRE(bus.serialOutput().size() == 1);
    CHECK(static_cast<std::uint8_t>(bus.serialOutput()[0]) == value);
    CHECK(bus.read(Bus::serialTransferData) == 0xFF); // no partner: received $FF
    CHECK((bus.read(Bus::serialControl) & 0x80) == 0); // bit 7 cleared on completion
    CHECK((bus.read(ifAddress) & ifSerialBit) != 0); // serial interrupt requested
}

TEST_CASE("Bus serial: completion keeps other pending IF bits", "[bus][serial]") {
    Bus bus;
    bus.write(ifAddress, 0x05); // VBlank + Timer pending
    sendOnBus(bus, 'x');
    CHECK((bus.read(ifAddress) & 0x1F) == (0x05 | ifSerialBit));
}

TEST_CASE("Bus serial: SC = 0x80 (external clock) does not complete without a partner", "[bus][serial]") {
    Bus bus;
    bus.write(Bus::serialTransferData, 0x42);
    bus.write(Bus::serialControl, 0x80);
    CHECK(bus.serialOutput().empty());
    CHECK((bus.read(ifAddress) & ifSerialBit) == 0);
    CHECK((bus.read(Bus::serialControl) & 0x80) != 0); // still waiting for a clock
    CHECK(bus.read(Bus::serialTransferData) == 0x42);
}

TEST_CASE("Bus serial: SC without bit 7 does not start a transfer", "[bus][serial]") {
    const std::uint8_t sc = GENERATE(as<std::uint8_t>{}, 0x00, 0x01);
    CAPTURE(static_cast<int>(sc));
    Bus bus;
    bus.write(Bus::serialTransferData, 0x42);
    bus.write(Bus::serialControl, sc);
    CHECK(bus.serialOutput().empty());
    CHECK((bus.read(ifAddress) & ifSerialBit) == 0);
    CHECK(bus.read(Bus::serialTransferData) == 0x42);
}

TEST_CASE("Bus serial: writing SB alone does not send", "[bus][serial]") {
    Bus bus;
    bus.write(Bus::serialTransferData, 0x42);
    CHECK(bus.serialOutput().empty());
    CHECK(bus.read(Bus::serialTransferData) == 0x42);
}

TEST_CASE("Bus serial: several transfers append in order", "[bus][serial]") {
    Bus bus;
    const std::string msg = "Passed\n";
    for (char ch : msg) {
        sendOnBus(bus, static_cast<std::uint8_t>(ch));
    }
    CHECK(bus.serialOutput() == msg);
}

// ---------------------------------------------------------------------------
// End to end
// ---------------------------------------------------------------------------

TEST_CASE("GameBoy: ROM printing \"Hi\" over serial", "[gameboy][serial]") {
    GameBoy gb(makeRom({
        0x3E, 'H',  // LD A,'H'
        0xE0, 0x01, // LDH (01),A
        0x3E, 0x81, // LD A,81
        0xE0, 0x02, // LDH (02),A
        0x3E, 'i',  // LD A,'i'
        0xE0, 0x01, // LDH (01),A
        0x3E, 0x81, // LD A,81
        0xE0, 0x02, // LDH (02),A
        0x18, 0xFE, // JR -2
    }));
    run(gb, 4);
    CHECK(gb.serialOutput() == "H");
    for (int i = 0; i < 4; ++i) {
        gb.step();
    }
    CHECK(gb.serialOutput() == "Hi");
    for (int i = 0; i < 10; ++i) {
        gb.step(); // JR -2 loop sends nothing more
    }
    CHECK(gb.serialOutput() == "Hi");
    CHECK(gb.cpuState().PC == 0x0160);
}

// ---------------------------------------------------------------------------
// Errors and ROM files
// ---------------------------------------------------------------------------

TEST_CASE("GameBoy: unsupported cartridge type throws", "[gameboy][errors]") {
    // 0x05 MBC2, 0x0F MBC3, 0x19 MBC5, 0xFC Pocket Camera, 0x04 unassigned
    const std::uint8_t type = GENERATE(as<std::uint8_t>{}, 0x04, 0x05, 0x0F, 0x19, 0xFC);
    CAPTURE(static_cast<int>(type));
    CHECK_THROWS_AS(GameBoy(makeRom({}, type)), std::runtime_error);
}

TEST_CASE("GameBoy: readRomFile of a missing path throws", "[gameboy][errors]") {
    const auto path = std::filesystem::temp_directory_path() / "gb_test_definitely_missing_rom_9f3a.gb";
    std::filesystem::remove(path);
    CHECK_THROWS_AS(GameBoy::readRomFile(path), std::runtime_error);
}

TEST_CASE("GameBoy: readRomFile round-trips a file", "[gameboy]") {
    const auto path = std::filesystem::temp_directory_path() / "gb_test_roundtrip_rom_9f3a.gb";
    std::vector<std::uint8_t> data(romSize);
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<std::uint8_t>((i * 31) ^ (i >> 8)); // includes 0x00, 0x0A, 0x1A, 0xFF bytes
    }
    {
        std::ofstream out(path, std::ios::binary);
        REQUIRE(out);
        out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    std::vector<std::uint8_t> read;
    try {
        read = GameBoy::readRomFile(path);
    } catch (...) {
        std::filesystem::remove(path);
        throw;
    }
    std::filesystem::remove(path);
    CHECK(read == data);

    // and the bytes read are a usable ROM
    auto rom = makeRom({0x00});
    {
        std::ofstream out(path, std::ios::binary);
        REQUIRE(out);
        out.write(reinterpret_cast<const char *>(rom.data()), static_cast<std::streamsize>(rom.size()));
    }
    std::vector<std::uint8_t> romRead;
    try {
        romRead = GameBoy::readRomFile(path);
    } catch (...) {
        std::filesystem::remove(path);
        throw;
    }
    std::filesystem::remove(path);
    GameBoy gb(romRead);
    CHECK(gb.cpuState().PC == 0x0100);
}
