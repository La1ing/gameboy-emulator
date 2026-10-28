// DMG memory map tests (Pan Docs: Memory Map, https://gbdev.io/pandocs/Memory_Map.html)
#include "catch.hpp"
#include "../main/Bus.hpp"
#include "../main/CPU.hpp"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace {

// Test-only cartridge: records every access and returns a value derived from the address
class RecordingCartridge : public Cartridge {
public:
    static constexpr std::uint8_t valueFor(std::uint16_t addr) noexcept {
        return static_cast<std::uint8_t>((addr ^ (addr >> 8)) ^ 0x5A);
    }

    [[nodiscard]] std::uint8_t read(std::uint16_t addr) const noexcept override {
        reads.push_back(addr);
        return valueFor(addr);
    }
    void write(std::uint16_t addr, std::uint8_t value) noexcept override {
        writes.emplace_back(addr, value);
    }

    mutable std::vector<std::uint16_t> reads;
    std::vector<std::pair<std::uint16_t, std::uint8_t>> writes;
};

struct Region {
    const char *name;
    std::uint16_t first;
    std::uint16_t last;
};

// Regions backed by plain storage inside the Bus
constexpr Region ramRegions[] = {
    {"VRAM", 0x8000, 0x9FFF},
    {"WRAM", 0xC000, 0xDFFF},
    {"OAM", 0xFE00, 0xFE9F},
    {"I/O", 0xFF00, 0xFF7F},
    {"HRAM", 0xFF80, 0xFFFE},
    {"IE", 0xFFFF, 0xFFFF},
};

} // namespace

// ---------------------------------------------------------------------------
// RAM regions
// ---------------------------------------------------------------------------

TEST_CASE("Bus: each RAM region reads back what was written at its first and last address") {
    auto region = GENERATE(values<Region>({ramRegions[0], ramRegions[1], ramRegions[2],
                                           ramRegions[3], ramRegions[4], ramRegions[5]}));
    INFO(region.name);
    Bus bus;

    bus.write(region.first, 0xA5);
    bus.write(region.last, 0x3C);
    if (region.first == region.last) {
        CHECK(bus.read(region.first) == 0x3C);
    } else {
        CHECK(bus.read(region.first) == 0xA5);
        CHECK(bus.read(region.last) == 0x3C);
    }

    // Overwrite with the boundary values 0x00 / 0xFF
    bus.write(region.first, 0xFF);
    CHECK(bus.read(region.first) == 0xFF);
    bus.write(region.first, 0x00);
    CHECK(bus.read(region.first) == 0x00);
}

TEST_CASE("Bus: every byte of a RAM region is stored independently") {
    auto region = GENERATE(values<Region>({ramRegions[0], ramRegions[1], ramRegions[2],
                                           ramRegions[3], ramRegions[4], ramRegions[5]}));
    INFO(region.name);
    Bus bus;
    for (std::uint32_t a = region.first; a <= region.last; ++a) {
        bus.write(static_cast<std::uint16_t>(a), static_cast<std::uint8_t>(a * 7 + 1));
    }
    bool allMatch = true;
    for (std::uint32_t a = region.first; a <= region.last; ++a) {
        if (bus.read(static_cast<std::uint16_t>(a)) != static_cast<std::uint8_t>(a * 7 + 1)) {
            allMatch = false;
            UNSCOPED_INFO("mismatch at 0x" << std::hex << a);
            break;
        }
    }
    CHECK(allMatch);
}

TEST_CASE("Bus: a write at a region's last address does not appear at the next region's first address") {
    // {last address of a region, first address of the next region} (A000 is cartridge, tested separately)
    using P = std::pair<std::uint16_t, std::uint16_t>;
    auto [last, next] = GENERATE(table<std::uint16_t, std::uint16_t>({
        P{0xFE9F, 0xFEA0}, // OAM end -> not usable
        P{0xFF7F, 0xFF80}, // I/O end -> HRAM
        P{0xFFFE, 0xFFFF}, // HRAM end -> IE
    }));
    INFO("last=0x" << std::hex << last << " next=0x" << next);
    Bus bus;
    bus.write(next, 0x00);
    bus.write(last, 0x77);
    CHECK(bus.read(last) == 0x77);
    CHECK(bus.read(next) != 0x77);
}

TEST_CASE("Bus: boundaries between cartridge ranges and Bus-owned regions") {
    auto cart = std::make_unique<RecordingCartridge>();
    auto *rec = cart.get();
    Bus bus(std::move(cart));

    // VRAM end does not show up at cartridge RAM start; cartridge ROM end does not show up in VRAM
    bus.write(0x8000, 0x00);
    bus.write(0x9FFF, 0x77);
    CHECK(bus.read(0x9FFF) == 0x77);
    CHECK(bus.read(0xA000) == RecordingCartridge::valueFor(0xA000));
    bus.write(0x7FFF, 0x88);
    CHECK(bus.read(0x8000) == 0x00);

    // Cartridge RAM end does not show up at WRAM start
    bus.write(0xC000, 0x00);
    bus.write(0xBFFF, 0x99);
    CHECK(bus.read(0xC000) == 0x00);

    REQUIRE(rec->writes.size() == 2);
    CHECK(rec->writes[0] == std::pair<std::uint16_t, std::uint8_t>{0x7FFF, 0x88});
    CHECK(rec->writes[1] == std::pair<std::uint16_t, std::uint8_t>{0xBFFF, 0x99});
}

TEST_CASE("Bus: writes at the start of a region do not leak into the previous region's end") {
    using P = std::pair<std::uint16_t, std::uint16_t>;
    auto [prevLast, first] = GENERATE(table<std::uint16_t, std::uint16_t>({
        P{0xFF7F, 0xFF80}, // I/O -> HRAM
        P{0xFFFE, 0xFFFF}, // HRAM -> IE
        P{0xDFFF, 0xE000}, // WRAM end -> echo (echo of C000, not DFFF)
    }));
    INFO("prevLast=0x" << std::hex << prevLast << " first=0x" << first);
    Bus bus;
    bus.write(prevLast, 0x11);
    bus.write(first, 0x99);
    CHECK(bus.read(first) == 0x99);
    CHECK(bus.read(prevLast) == 0x11);
}

TEST_CASE("Bus: VRAM, WRAM, OAM, I/O, HRAM and IE are independent of each other") {
    Bus bus;
    std::uint8_t v = 0x10;
    for (const auto &r : ramRegions) {
        bus.write(r.first, v);
        bus.write(r.last, static_cast<std::uint8_t>(v + 1));
        v = static_cast<std::uint8_t>(v + 0x10);
    }
    v = 0x10;
    for (const auto &r : ramRegions) {
        INFO(r.name);
        if (r.first != r.last) {
            CHECK(bus.read(r.first) == v);
        }
        CHECK(bus.read(r.last) == static_cast<std::uint8_t>(v + 1));
        v = static_cast<std::uint8_t>(v + 0x10);
    }
}

// ---------------------------------------------------------------------------
// Echo RAM: E000-FDFF mirrors C000-DDFF
// ---------------------------------------------------------------------------

TEST_CASE("Bus: echo RAM E000-FDFF reads what was written to C000-DDFF") {
    auto wramAddr = GENERATE(as<std::uint16_t>{}, 0xC000, 0xC001, 0xCFFF, 0xD000, 0xDDFE, 0xDDFF);
    const auto echoAddr = static_cast<std::uint16_t>(wramAddr + 0x2000);
    INFO("WRAM 0x" << std::hex << wramAddr << " / echo 0x" << echoAddr);
    Bus bus;
    bus.write(wramAddr, 0xC3);
    CHECK(bus.read(echoAddr) == 0xC3);
}

TEST_CASE("Bus: writes to echo RAM E000-FDFF land in C000-DDFF") {
    auto echoAddr = GENERATE(as<std::uint16_t>{}, 0xE000, 0xE001, 0xEFFF, 0xF000, 0xFDFE, 0xFDFF);
    const auto wramAddr = static_cast<std::uint16_t>(echoAddr - 0x2000);
    INFO("echo 0x" << std::hex << echoAddr << " / WRAM 0x" << wramAddr);
    Bus bus;
    bus.write(echoAddr, 0x3C);
    CHECK(bus.read(wramAddr) == 0x3C);
    CHECK(bus.read(echoAddr) == 0x3C);
}

TEST_CASE("Bus: WRAM DE00-DFFF is not mirrored (echo ends at FDFF)") {
    Bus bus;
    // FE00-FE9F is OAM, FEA0-FEFF not usable: neither shows DE00-DFFF
    bus.write(0xFE00, 0x00);
    bus.write(0xFE9F, 0x00);
    bus.write(0xDE00, 0xAB);
    bus.write(0xDE9F, 0xCD);
    bus.write(0xDEA0, 0xEF);
    bus.write(0xDFFF, 0x12);
    CHECK(bus.read(0xFE00) == 0x00);
    CHECK(bus.read(0xFE9F) == 0x00);
    CHECK(bus.read(0xFEA0) == 0x00);
    CHECK(bus.read(0xFEFF) == 0x00);

    // And OAM writes do not land in DE00-DFFF
    bus.write(0xFE00, 0x55);
    bus.write(0xFE9F, 0x66);
    CHECK(bus.read(0xDE00) == 0xAB);
    CHECK(bus.read(0xDE9F) == 0xCD);
    CHECK(bus.read(0xDFFF) == 0x12);
}

// ---------------------------------------------------------------------------
// FEA0-FEFF: not usable
// ---------------------------------------------------------------------------

TEST_CASE("Bus: FEA0-FEFF reads 0x00 on DMG, even after being written") {
    auto addr = GENERATE(as<std::uint16_t>{}, 0xFEA0, 0xFEA1, 0xFECF, 0xFEFE, 0xFEFF);
    INFO("addr 0x" << std::hex << addr);
    Bus bus;
    CHECK(bus.read(addr) == 0x00);
    bus.write(addr, 0xFF);
    CHECK(bus.read(addr) == 0x00);
}

TEST_CASE("Bus: writes to FEA0-FEFF do not affect OAM or I/O") {
    Bus bus;
    bus.write(0xFE00, 0x01);
    bus.write(0xFE9F, 0x02);
    bus.write(0xFF00, 0x03);
    bus.write(0xFF7F, 0x04);
    for (std::uint32_t a = 0xFEA0; a <= 0xFEFF; ++a) {
        bus.write(static_cast<std::uint16_t>(a), 0xEE);
    }
    CHECK(bus.read(0xFE00) == 0x01);
    CHECK(bus.read(0xFE9F) == 0x02);
    CHECK(bus.read(0xFF00) == 0x03);
    CHECK(bus.read(0xFF7F) == 0x04);
    // OAM - 0x60 / + 0x60 aliases a naive modulo implementation could hit
    CHECK(bus.read(0xFE40) == 0x00);
    CHECK(bus.read(0xFF5F) == 0x00);
}

TEST_CASE("Bus: FEA0-FEFF still reads 0x00 when OAM and I/O hold data") {
    Bus bus;
    for (std::uint32_t a = 0xFE00; a <= 0xFE9F; ++a) {
        bus.write(static_cast<std::uint16_t>(a), 0xA5); // Non-zero OAM, so a read that aliases OAM shows up
    }
    bus.write(0xFF00, 0x5A);
    for (std::uint32_t a = 0xFEA0; a <= 0xFEFF; ++a) {
        INFO("addr 0x" << std::hex << a);
        CHECK(bus.read(static_cast<std::uint16_t>(a)) == 0x00);
    }
}

// ---------------------------------------------------------------------------
// Cartridge routing: 0000-7FFF and A000-BFFF
// ---------------------------------------------------------------------------

TEST_CASE("Bus: reads from 0000-7FFF and A000-BFFF go to the cartridge") {
    auto addr = GENERATE(as<std::uint16_t>{}, 0x0000, 0x0001, 0x3FFF, 0x4000, 0x7FFE, 0x7FFF,
                         0xA000, 0xA001, 0xBFFE, 0xBFFF);
    INFO("addr 0x" << std::hex << addr);
    auto cart = std::make_unique<RecordingCartridge>();
    auto *rec = cart.get();
    Bus bus(std::move(cart));

    CHECK(bus.read(addr) == RecordingCartridge::valueFor(addr));
    REQUIRE(rec->reads.size() == 1);
    CHECK(rec->reads[0] == addr);
    CHECK(rec->writes.empty());
}

TEST_CASE("Bus: writes to 0000-7FFF and A000-BFFF go to the cartridge") {
    auto addr = GENERATE(as<std::uint16_t>{}, 0x0000, 0x1FFF, 0x2000, 0x3FFF, 0x4000, 0x6000, 0x7FFF,
                         0xA000, 0xBFFF);
    INFO("addr 0x" << std::hex << addr);
    auto cart = std::make_unique<RecordingCartridge>();
    auto *rec = cart.get();
    Bus bus(std::move(cart));

    bus.write(addr, 0x9D);
    REQUIRE(rec->writes.size() == 1);
    CHECK(rec->writes[0].first == addr);
    CHECK(rec->writes[0].second == 0x9D);
    CHECK(rec->reads.empty());
}

TEST_CASE("Bus: accesses outside 0000-7FFF and A000-BFFF never reach the cartridge") {
    auto addr = GENERATE(as<std::uint16_t>{}, 0x8000, 0x9FFF, 0xC000, 0xDFFF, 0xE000, 0xFDFF,
                         0xFE00, 0xFE9F, 0xFEA0, 0xFEFF, 0xFF00, 0xFF7F, 0xFF80, 0xFFFE, 0xFFFF);
    INFO("addr 0x" << std::hex << addr);
    auto cart = std::make_unique<RecordingCartridge>();
    auto *rec = cart.get();
    Bus bus(std::move(cart));

    bus.write(addr, 0x42);
    (void)bus.read(addr);
    CHECK(rec->writes.empty());
    CHECK(rec->reads.empty());
}

TEST_CASE("Bus: RAM regions still work with a cartridge inserted") {
    auto cart = std::make_unique<RecordingCartridge>();
    Bus bus(std::move(cart));
    for (const auto &r : ramRegions) {
        INFO(r.name);
        bus.write(r.last, 0x6B);
        CHECK(bus.read(r.last) == 0x6B);
    }
    bus.write(0xC000, 0x21);
    CHECK(bus.read(0xE000) == 0x21);
}

// ---------------------------------------------------------------------------
// No cartridge inserted (value read from the cartridge range is unspecified)
// ---------------------------------------------------------------------------

TEST_CASE("Bus: without a cartridge, cartridge-range accesses are harmless") {
    Bus bus;
    // Fill every RAM region with a marker
    for (const auto &r : ramRegions) {
        bus.write(r.first, 0x5E);
        bus.write(r.last, 0x5E);
    }

    const std::uint16_t cartAddrs[] = {0x0000, 0x0100, 0x3FFF, 0x4000, 0x7FFF, 0xA000, 0xBFFF};
    for (auto a : cartAddrs) {
        (void)bus.read(a); // must not crash; the value is not specified
        bus.write(a, 0xA1);
        (void)bus.read(a);
    }

    for (const auto &r : ramRegions) {
        INFO(r.name);
        CHECK(bus.read(r.first) == 0x5E);
        CHECK(bus.read(r.last) == 0x5E);
    }
}

// ---------------------------------------------------------------------------
// Integration: CPU running from WRAM on a real Bus
// ---------------------------------------------------------------------------

TEST_CASE("Bus: CPU runs a program from WRAM through the real Bus") {
    Bus bus;
    CPU cpu{bus};
    cpu.initialize();

    const std::uint8_t program[] = {
        0x21, 0x00, 0xD0, // C000 LD HL,D000
        0x3E, 0x42,       // C003 LD A,42
        0x77,             // C005 LD (HL),A        -> WRAM D000
        0xE0, 0x90,       // C006 LDH (FF90),A     -> HRAM FF90
        0x31, 0xFE, 0xFF, // C008 LD SP,FFFE
        0x01, 0x34, 0x12, // C00B LD BC,1234
        0xC5,             // C00E PUSH BC          -> FFFD=12, FFFC=34
        0xD1,             // C00F POP DE
        0x3E, 0x99,       // C010 LD A,99
        0x22,             // C012 LD (HL+),A       -> D000 = 99, HL = D001
        0xEA, 0x00, 0x80, // C013 LD (8000),A      -> VRAM
    };
    std::uint16_t addr = 0xC000;
    for (auto b : program) {
        bus.write(addr++, b);
    }
    // The program is visible through echo RAM as well
    CHECK(bus.read(0xE000) == 0x21);

    cpu.PC = 0xC000;
    cpu.ime = false;
    bus.write(0xFFFF, 0x00); // IE: no interrupts enabled

    const int instructions = 11;
    for (int i = 0; i < instructions; ++i) {
        cpu.step();
    }

    CHECK(cpu.PC == 0xC016);
    CHECK(cpu.A() == 0x99);
    CHECK(cpu.HL == 0xD001);
    CHECK(cpu.BC == 0x1234);
    CHECK(cpu.DE == 0x1234);
    CHECK(cpu.SP == 0xFFFE);

    CHECK(bus.read(0xD000) == 0x99);
    CHECK(bus.read(0xF000) == 0x99); // echo of D000
    CHECK(bus.read(0xFF90) == 0x42);
    CHECK(bus.read(0xFFFD) == 0x12);
    CHECK(bus.read(0xFFFC) == 0x34);
    CHECK(bus.read(0x8000) == 0x99);
}

TEST_CASE("Bus: CPU LDH (a8),A reaches the I/O range") {
    Bus bus;
    CPU cpu{bus};
    cpu.initialize();
    const std::uint8_t program[] = {
        0x3E, 0x5C, // LD A,5C
        0xE0, 0x01, // LDH (FF01),A   (SB: plain storage for now)
        0x3E, 0x00, // LD A,00
        0xF0, 0x01, // LDH A,(FF01)
    };
    std::uint16_t addr = 0xC100;
    for (auto b : program) {
        bus.write(addr++, b);
    }
    cpu.PC = 0xC100;
    cpu.ime = false;
    bus.write(0xFFFF, 0x00);
    for (int i = 0; i < 4; ++i) {
        cpu.step();
    }
    CHECK(bus.read(0xFF01) == 0x5C);
    CHECK(cpu.A() == 0x5C);
    CHECK(cpu.PC == 0xC108);
}
