// Cartridge header and MBC tests
// Spec: Pan Docs, The Cartridge Header (https://gbdev.io/pandocs/The_Cartridge_Header.html)
//       and MBC1 (https://gbdev.io/pandocs/MBC1.html)
#include "catch.hpp"
#include "../main/Bus.hpp"
#include "../main/CPU.hpp"
#include "../main/Cartridge.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::size_t bankSize = 0x4000; // 16 KiB ROM bank

// Header checksum over 0134-014C, as the boot ROM computes it (Pan Docs: 014D)
std::uint8_t headerChecksum(const std::vector<std::uint8_t> &rom) {
    std::uint8_t x = 0;
    for (std::size_t a = 0x0134; a <= 0x014C; ++a) {
        x = static_cast<std::uint8_t>(x - rom[a] - 1);
    }
    return x;
}

// ROM size code for a bank count (Pan Docs: 32 KiB << code)
std::uint8_t romCodeFor(std::size_t banks) {
    std::uint8_t code = 0;
    while ((std::size_t{2} << code) < banks) {
        ++code;
    }
    return code;
}

/**
 * Builds a ROM image of `banks` 16 KiB banks. Every byte gets a position-dependent filler value,
 * then the first and last byte of each bank are set to the bank number (low 8 bits),
 * and the header (title, type, ROM size, RAM size, checksum) is filled in.
 */
std::vector<std::uint8_t> makeRom(std::size_t banks, std::uint8_t type, std::uint8_t ramCode = 0x00,
                                  const std::string &title = "TEST") {
    std::vector<std::uint8_t> rom(banks * bankSize);
    for (std::size_t i = 0; i < rom.size(); ++i) {
        rom[i] = static_cast<std::uint8_t>((i * 7) ^ (i >> 8) ^ ((i >> 14) * 0x3B));
    }
    for (std::size_t b = 0; b < banks; ++b) {
        rom[b * bankSize] = static_cast<std::uint8_t>(b);
        rom[b * bankSize + bankSize - 1] = static_cast<std::uint8_t>(b);
    }
    for (std::size_t a = 0x0134; a <= 0x0143; ++a) {
        rom[a] = 0x00;
    }
    for (std::size_t i = 0; i < title.size() && i < 16; ++i) {
        rom[0x0134 + i] = static_cast<std::uint8_t>(title[i]);
    }
    rom[0x0147] = type;
    rom[0x0148] = romCodeFor(banks);
    rom[0x0149] = ramCode;
    rom[0x014D] = headerChecksum(rom);
    return rom;
}

void fixChecksum(std::vector<std::uint8_t> &rom) {
    rom[0x014D] = headerChecksum(rom);
}

// MBC1 register writes
void enableRam(Cartridge &c) { c.write(0x0000, 0x0A); }
void disableRam(Cartridge &c) { c.write(0x0000, 0x00); }
void selectRomBank(Cartridge &c, std::uint8_t v) { c.write(0x2000, v); }
void selectBank2(Cartridge &c, std::uint8_t v) { c.write(0x4000, v); }
void selectMode(Cartridge &c, std::uint8_t v) { c.write(0x6000, v); }

} // namespace

// ===========================================================================
// CartridgeHeader::parse
// ===========================================================================

TEST_CASE("Header: title stops at the first 0x00") {
    auto rom = makeRom(2, 0x00, 0x00, "HELLO");
    CHECK(CartridgeHeader::parse(rom).title == "HELLO");

    // Bytes after the terminating 0x00 are not part of the title
    rom[0x0134 + 2] = 0x00; // "HE\0LO"
    fixChecksum(rom);
    CHECK(CartridgeHeader::parse(rom).title == "HE");
}

TEST_CASE("Header: an all-zero title is empty") {
    auto rom = makeRom(2, 0x00, 0x00, "");
    CHECK(CartridgeHeader::parse(rom).title.empty());
}

TEST_CASE("Header: a title using all 16 bytes is 16 characters, not more") {
    auto rom = makeRom(2, 0x00, 0x00, "ABCDEFGHIJKLMNOP");
    // 0x0144 (new licensee code) follows the title; make it a printable character to catch overruns
    rom[0x0144] = 'Q';
    rom[0x0145] = 'R';
    fixChecksum(rom);
    const auto header = CartridgeHeader::parse(rom);
    CHECK(header.title == "ABCDEFGHIJKLMNOP");
    CHECK(header.title.size() == 16);
}

TEST_CASE("Header: cartridge type byte 0x0147 is reported") {
    auto type = GENERATE(as<int>{}, 0x00, 0x01, 0x03, 0x08, 0x11, 0x19, 0xFF);
    INFO("type " << type);
    const auto rom = makeRom(2, static_cast<std::uint8_t>(type));
    CHECK(CartridgeHeader::parse(rom).type == type);
}

TEST_CASE("Header: each ROM size code 0x00-0x08 maps to its bank count") {
    auto code = GENERATE(range(0, 9));
    const std::size_t banks = std::size_t{2} << code; // 32 KiB << code, in 16 KiB banks
    INFO("code " << code << ", " << banks << " banks");
    const auto rom = makeRom(banks, 0x00);
    REQUIRE(rom[0x0148] == code);
    CHECK(CartridgeHeader::parse(rom).romBanks == banks);
}

TEST_CASE("Header: each RAM size code maps to its 8 KiB bank count") {
    auto [code, banks] = GENERATE(table<int, std::size_t>({
        {0x00, 0},
        {0x01, 0}, // unused code, no RAM
        {0x02, 1},
        {0x03, 4},
        {0x04, 16},
        {0x05, 8},
    }));
    INFO("code " << code);
    const auto rom = makeRom(2, 0x00, static_cast<std::uint8_t>(code));
    CHECK(CartridgeHeader::parse(rom).ramBanks == banks);
}

TEST_CASE("Header: a correct header checksum is valid") {
    auto rom = makeRom(2, 0x01, 0x00, "CHECKSUM");
    rom[0x0144] = 0x30;
    rom[0x014A] = 0x01;
    rom[0x014B] = 0x33;
    rom[0x014C] = 0x7F;
    fixChecksum(rom);
    CHECK(CartridgeHeader::parse(rom).checksumValid);
}

TEST_CASE("Header: known checksum for an all-zero 0134-014C range is 0xE7") {
    // 25 bytes of 0: x = 0 - 25 * 1 = -25 = 0xE7 (mod 256)
    std::vector<std::uint8_t> rom(2 * bankSize, 0x00);
    rom[0x014D] = 0xE7;
    CHECK(CartridgeHeader::parse(rom).checksumValid);
    rom[0x014D] = 0xE6;
    CHECK_FALSE(CartridgeHeader::parse(rom).checksumValid);
}

TEST_CASE("Header: an incorrect header checksum is invalid") {
    auto rom = makeRom(2, 0x00);

    SECTION("014D off by one") {
        rom[0x014D] = static_cast<std::uint8_t>(rom[0x014D] + 1);
    }
    SECTION("a byte in 0134-014C changed after the checksum was computed") {
        auto addr = GENERATE(as<std::size_t>{}, 0x0134, 0x0140, 0x014C);
        INFO("changed byte at " << addr);
        rom[addr] = static_cast<std::uint8_t>(rom[addr] ^ 0x01);
    }
    CHECK_FALSE(CartridgeHeader::parse(rom).checksumValid);
}

TEST_CASE("Header: bytes outside 0134-014C do not affect the checksum") {
    auto rom = makeRom(2, 0x00);
    rom[0x0133] = static_cast<std::uint8_t>(rom[0x0133] ^ 0xFF);
    rom[0x014E] = static_cast<std::uint8_t>(rom[0x014E] ^ 0xFF); // global checksum, not checked
    rom[0x014F] = static_cast<std::uint8_t>(rom[0x014F] ^ 0xFF);
    CHECK(CartridgeHeader::parse(rom).checksumValid);
}

TEST_CASE("Header: an image too small to hold the header throws") {
    // The header occupies 0100-014F; anything that ends before 014D cannot even hold the checksum
    auto size = GENERATE(as<std::size_t>{}, 0, 1, 0x100, 0x134, 0x148, 0x14D);
    INFO("size " << size);
    const std::vector<std::uint8_t> rom(size, 0x00);
    CHECK_THROWS_AS(CartridgeHeader::parse(rom), std::runtime_error);
}

TEST_CASE("Header: an unknown ROM size code throws") {
    auto code = GENERATE(as<int>{}, 0x09, 0x0A, 0x10, 0x51, 0x55, 0xFF);
    INFO("ROM size code " << code);
    auto rom = makeRom(2, 0x00);
    rom[0x0148] = static_cast<std::uint8_t>(code);
    fixChecksum(rom);
    CHECK_THROWS_AS(CartridgeHeader::parse(rom), std::runtime_error);
}

TEST_CASE("Header: an unknown RAM size code throws") {
    auto code = GENERATE(as<int>{}, 0x06, 0x07, 0x10, 0xFF);
    INFO("RAM size code " << code);
    auto rom = makeRom(2, 0x00);
    rom[0x0149] = static_cast<std::uint8_t>(code);
    fixChecksum(rom);
    CHECK_THROWS_AS(CartridgeHeader::parse(rom), std::runtime_error);
}

// ===========================================================================
// Cartridge::load
// ===========================================================================

TEST_CASE("Cartridge::load: type 0x00 gives a ROM-only cartridge with all 32 KiB readable") {
    const auto rom = makeRom(2, 0x00);
    auto cart = Cartridge::load(rom);
    REQUIRE(cart != nullptr);
    CHECK(dynamic_cast<RomOnly *>(cart.get()) != nullptr);

    bool allMatch = true;
    for (std::size_t a = 0; a < 0x8000; ++a) {
        if (cart->read(static_cast<std::uint16_t>(a)) != rom[a]) {
            allMatch = false;
            FAIL_CHECK("ROM byte differs at " << a);
            break;
        }
    }
    CHECK(allMatch);
    CHECK(cart->read(0x0000) == 0);
    CHECK(cart->read(0x3FFF) == 0);
    CHECK(cart->read(0x4000) == 1);
    CHECK(cart->read(0x7FFF) == 1);
}

TEST_CASE("Cartridge::load: ROM-only ignores writes to the ROM range") {
    const auto rom = makeRom(2, 0x00);
    auto cart = Cartridge::load(rom);

    for (std::uint16_t a : {0x0000, 0x0150, 0x1FFF, 0x2000, 0x3FFF, 0x4000, 0x5FFF, 0x6000, 0x7FFF}) {
        cart->write(a, static_cast<std::uint8_t>(rom[a] ^ 0xFF));
    }
    cart->write(0x2000, 0x02); // would be a bank switch on an MBC
    cart->write(0x0000, 0x0A);
    cart->write(0x6000, 0x01);

    bool allMatch = true;
    for (std::size_t a = 0; a < 0x8000; ++a) {
        if (cart->read(static_cast<std::uint16_t>(a)) != rom[a]) {
            allMatch = false;
            FAIL_CHECK("ROM byte changed at " << a);
            break;
        }
    }
    CHECK(allMatch);
}

TEST_CASE("Cartridge::load: types 0x01-0x03 give a working MBC1") {
    auto [type, ramCode] = GENERATE(table<int, int>({{0x01, 0x00}, {0x02, 0x02}, {0x03, 0x02}}));
    INFO("type " << type);
    auto cart = Cartridge::load(makeRom(8, static_cast<std::uint8_t>(type), static_cast<std::uint8_t>(ramCode)));
    REQUIRE(cart != nullptr);
    CHECK(dynamic_cast<Mbc1 *>(cart.get()) != nullptr);

    CHECK(cart->read(0x0000) == 0);
    CHECK(cart->read(0x4000) == 1);
    selectRomBank(*cart, 5);
    CHECK(cart->read(0x4000) == 5);
    CHECK(cart->read(0x7FFF) == 5);
    CHECK(cart->read(0x0000) == 0);

    if (ramCode != 0) {
        enableRam(*cart);
        cart->write(0xA000, 0x3C);
        cart->write(0xBFFF, 0xC3);
        CHECK(cart->read(0xA000) == 0x3C);
        CHECK(cart->read(0xBFFF) == 0xC3);
    }
}

TEST_CASE("Cartridge::load: unsupported cartridge types throw") {
    // MBC2, MMM01, MBC3, MBC5, MBC6, MBC7, Pocket Camera, HuC1
    auto type = GENERATE(as<int>{}, 0x05, 0x06, 0x0B, 0x0F, 0x11, 0x13, 0x19, 0x1B, 0x20, 0x22, 0xFC, 0xFF);
    INFO("type " << type);
    const auto rom = makeRom(4, static_cast<std::uint8_t>(type));
    CHECK_THROWS_AS(Cartridge::load(rom), std::runtime_error);
}

TEST_CASE("Cartridge::load: an invalid header throws") {
    SECTION("too small") {
        CHECK_THROWS_AS(Cartridge::load(std::vector<std::uint8_t>(0x100, 0x00)), std::runtime_error);
    }
    SECTION("unknown ROM size code") {
        auto rom = makeRom(2, 0x00);
        rom[0x0148] = 0x09;
        fixChecksum(rom);
        CHECK_THROWS_AS(Cartridge::load(rom), std::runtime_error);
    }
}

// ===========================================================================
// ROM+RAM (no MBC)
// ===========================================================================

TEST_CASE("ROM+RAM: A000-BFFF reads back writes") {
    auto type = GENERATE(as<int>{}, 0x08, 0x09);
    INFO("type " << type);
    auto cart = Cartridge::load(makeRom(2, static_cast<std::uint8_t>(type), 0x02));
    REQUIRE(cart != nullptr);
    CHECK(dynamic_cast<RomOnly *>(cart.get()) != nullptr);

    for (std::uint16_t a : {0xA000, 0xA001, 0xAFFF, 0xB000, 0xBFFE, 0xBFFF}) {
        cart->write(a, static_cast<std::uint8_t>(a ^ (a >> 8)));
    }
    for (std::uint16_t a : {0xA000, 0xA001, 0xAFFF, 0xB000, 0xBFFE, 0xBFFF}) {
        INFO("addr " << a);
        CHECK(cart->read(a) == static_cast<std::uint8_t>(a ^ (a >> 8)));
    }
    // ROM still intact
    CHECK(cart->read(0x0000) == 0);
    CHECK(cart->read(0x4000) == 1);
}

TEST_CASE("RomOnly constructed directly with 8 KiB RAM") {
    const auto rom = makeRom(2, 0x08, 0x02);
    RomOnly cart{rom, 0x2000};
    CHECK(cart.read(0x0000) == 0);
    CHECK(cart.read(0x7FFF) == 1);
    cart.write(0xA000, 0x11);
    cart.write(0xBFFF, 0x22);
    CHECK(cart.read(0xA000) == 0x11);
    CHECK(cart.read(0xBFFF) == 0x22);
}

// ===========================================================================
// MBC1: RAM enable
// ===========================================================================

TEST_CASE("MBC1: a value with 0xA in the lower nibble enables RAM") {
    auto value = GENERATE(as<int>{}, 0x0A, 0x1A, 0x5A, 0xFA);
    auto addr = GENERATE(as<int>{}, 0x0000, 0x1000, 0x1FFF);
    INFO("value " << value << " at " << addr);
    auto cart = Cartridge::load(makeRom(4, 0x03, 0x02));

    cart->write(static_cast<std::uint16_t>(addr), static_cast<std::uint8_t>(value));
    cart->write(0xA000, 0x42);
    cart->write(0xBFFF, 0x24);
    CHECK(cart->read(0xA000) == 0x42);
    CHECK(cart->read(0xBFFF) == 0x24);
}

TEST_CASE("MBC1: any other value disables RAM again") {
    auto value = GENERATE(as<int>{}, 0x00, 0x0B, 0x09, 0x01, 0xA0, 0xAB, 0xFF);
    INFO("value " << value);
    auto cart = Cartridge::load(makeRom(4, 0x03, 0x02));

    enableRam(*cart);
    cart->write(0xA000, 0x42);
    REQUIRE(cart->read(0xA000) == 0x42);

    cart->write(0x0000, static_cast<std::uint8_t>(value));
    // Disabled: reads are open bus (not guaranteed 0xFF), so only check the stored data is not visible
    CHECK(cart->read(0xA000) != 0x42);

    // Writes while disabled are ignored
    cart->write(0xA000, 0x99);
    enableRam(*cart);
    CHECK(cart->read(0xA000) == 0x42);
}

TEST_CASE("MBC1: RAM starts disabled, so writes before enabling are ignored") {
    auto cart = Cartridge::load(makeRom(4, 0x03, 0x02));
    enableRam(*cart);
    cart->write(0xA000, 0x42);
    disableRam(*cart);

    cart->write(0xA000, 0x77);
    cart->write(0xBFFF, 0x77);
    enableRam(*cart);
    CHECK(cart->read(0xA000) == 0x42);
    CHECK(cart->read(0xBFFF) != 0x77);
}

TEST_CASE("MBC1: RAM enable does not affect ROM reads") {
    auto cart = Cartridge::load(makeRom(4, 0x03, 0x02));
    enableRam(*cart);
    CHECK(cart->read(0x0000) == 0);
    CHECK(cart->read(0x4000) == 1);
    disableRam(*cart);
    CHECK(cart->read(0x0000) == 0);
    CHECK(cart->read(0x4000) == 1);
}

// ===========================================================================
// MBC1: ROM bank register (2000-3FFF)
// ===========================================================================

TEST_CASE("MBC1: bank 1 is mapped at 4000-7FFF after power-on") {
    auto cart = Cartridge::load(makeRom(32, 0x01));
    CHECK(cart->read(0x4000) == 1);
    CHECK(cart->read(0x7FFF) == 1);
    CHECK(cart->read(0x0000) == 0);
    CHECK(cart->read(0x3FFF) == 0);
}

TEST_CASE("MBC1: every bank 1-31 is reachable at 4000-7FFF on a 512 KiB ROM") {
    auto bank = GENERATE(range(1, 32));
    auto addr = GENERATE(as<int>{}, 0x2000, 0x2ABC, 0x3FFF);
    INFO("bank " << bank << " written at " << addr);
    auto cart = Cartridge::load(makeRom(32, 0x01));

    cart->write(static_cast<std::uint16_t>(addr), static_cast<std::uint8_t>(bank));
    CHECK(cart->read(0x4000) == bank);
    CHECK(cart->read(0x7FFF) == bank);
    // 0000-3FFF stays bank 0
    CHECK(cart->read(0x0000) == 0);
    CHECK(cart->read(0x3FFF) == 0);
}

TEST_CASE("MBC1: full bank contents follow the selected bank") {
    const auto rom = makeRom(32, 0x01);
    auto cart = Cartridge::load(rom);
    selectRomBank(*cart, 0x13);
    bool allMatch = true;
    for (std::size_t a = 0x4000; a < 0x8000; ++a) {
        if (cart->read(static_cast<std::uint16_t>(a)) != rom[0x13 * bankSize + (a - 0x4000)]) {
            allMatch = false;
            FAIL_CHECK("byte differs at " << a);
            break;
        }
    }
    CHECK(allMatch);
}

TEST_CASE("MBC1: writing 0 to the ROM bank register selects bank 1") {
    auto cart = Cartridge::load(makeRom(32, 0x01));
    selectRomBank(*cart, 7);
    REQUIRE(cart->read(0x4000) == 7);
    selectRomBank(*cart, 0);
    CHECK(cart->read(0x4000) == 1);
    CHECK(cart->read(0x7FFF) == 1);
}

TEST_CASE("MBC1: the ROM bank register is 5 bits, higher bits are discarded") {
    auto [value, bank] = GENERATE(table<int, int>({
        {0x23, 0x03},
        {0xE5, 0x05},
        {0x3F, 0x1F},
        {0xFF, 0x1F},
        {0x20, 0x01}, // lower 5 bits are 0 -> bank 1
        {0xE0, 0x01},
    }));
    INFO("value " << value);
    auto cart = Cartridge::load(makeRom(32, 0x01));
    selectRomBank(*cart, static_cast<std::uint8_t>(value));
    CHECK(cart->read(0x4000) == bank);
}

TEST_CASE("MBC1: the ROM bank number is masked to the cartridge size") {
    auto [banks, value, expected] = GENERATE(table<std::size_t, int, int>({
        {4, 5, 1},
        {4, 6, 2},
        {4, 7, 3},
        {4, 0x1F, 3},
        {8, 9, 1},
        {8, 0x0F, 7},
        {16, 0x11, 1},
        {16, 0x1F, 15},
    }));
    INFO(banks << " banks, value " << value);
    auto cart = Cartridge::load(makeRom(banks, 0x01));
    selectRomBank(*cart, static_cast<std::uint8_t>(value));
    CHECK(cart->read(0x4000) == expected);
    CHECK(cart->read(0x7FFF) == expected);
}

TEST_CASE("MBC1: the 0->1 translation uses the full 5-bit value, so masked-to-0 values select bank 0") {
    // Pan Docs: on carts of 256 KiB or less, writing 0x10 selects bank 0 at 4000-7FFF
    auto [banks, value] = GENERATE(table<std::size_t, int>({
        {16, 0x10}, // 256 KiB
        {8, 0x10},  // 128 KiB
        {8, 0x08},
        {4, 0x10},  // 64 KiB
        {4, 0x04},
        {4, 0x0C},
        {2, 0x02},  // 32 KiB
        {2, 0x10},
    }));
    INFO(banks << " banks, value " << value);
    auto cart = Cartridge::load(makeRom(banks, 0x01));
    selectRomBank(*cart, static_cast<std::uint8_t>(value));
    CHECK(cart->read(0x4000) == 0);
    CHECK(cart->read(0x7FFF) == 0);
}

TEST_CASE("MBC1: on a 512 KiB ROM writing 0x10 selects bank 0x10") {
    auto cart = Cartridge::load(makeRom(32, 0x01));
    selectRomBank(*cart, 0x10);
    CHECK(cart->read(0x4000) == 0x10);
}

// ===========================================================================
// MBC1: large ROMs (bank2 supplies bits 5-6)
// ===========================================================================

TEST_CASE("MBC1: on a 2 MiB ROM bank2 supplies bits 5-6 of the 4000-7FFF bank") {
    static const auto rom = makeRom(128, 0x01);
    auto mode = GENERATE(0, 1); // 4000-7FFF is the same in both modes
    auto hi = GENERATE(range(0, 4));
    auto lo = GENERATE(range(0, 32));
    const int expected = (hi << 5) | (lo == 0 ? 1 : lo);
    INFO("mode " << mode << ", bank2 " << hi << ", ROM bank " << lo);

    auto cart = Cartridge::load(rom);
    selectMode(*cart, static_cast<std::uint8_t>(mode));
    selectBank2(*cart, static_cast<std::uint8_t>(hi));
    selectRomBank(*cart, static_cast<std::uint8_t>(lo));
    CHECK(cart->read(0x4000) == expected);
    CHECK(cart->read(0x7FFF) == expected);
}

TEST_CASE("MBC1: banks 0x20, 0x40, 0x60 are unreachable at 4000-7FFF and give 0x21, 0x41, 0x61") {
    auto hi = GENERATE(1, 2, 3);
    INFO("bank2 " << hi);
    auto cart = Cartridge::load(makeRom(128, 0x01));
    selectBank2(*cart, static_cast<std::uint8_t>(hi));
    selectRomBank(*cart, 0x00);
    CHECK(cart->read(0x4000) == (hi << 5) + 1);
}

TEST_CASE("MBC1: bank2 is a 2-bit register") {
    auto cart = Cartridge::load(makeRom(128, 0x01));
    selectRomBank(*cart, 0x05);
    selectBank2(*cart, 0xFE); // only bits 0-1 -> 2
    CHECK(cart->read(0x4000) == 0x45);
    selectBank2(*cart, 0x07); // -> 3
    CHECK(cart->read(0x4000) == 0x65);
}

TEST_CASE("MBC1: bank2 can be written anywhere in 4000-5FFF") {
    auto addr = GENERATE(as<int>{}, 0x4000, 0x4ABC, 0x5FFF);
    INFO("addr " << addr);
    auto cart = Cartridge::load(makeRom(128, 0x01));
    selectRomBank(*cart, 0x02);
    cart->write(static_cast<std::uint16_t>(addr), 0x01);
    CHECK(cart->read(0x4000) == 0x22);
}

TEST_CASE("MBC1: on a 1 MiB ROM only bit 5 from bank2 is used") {
    auto cart = Cartridge::load(makeRom(64, 0x01));
    selectRomBank(*cart, 0x05);
    selectBank2(*cart, 0x01);
    CHECK(cart->read(0x4000) == 0x25);
    selectBank2(*cart, 0x03); // bit 6 does not exist on a 64-bank ROM
    CHECK(cart->read(0x4000) == 0x25);
    selectBank2(*cart, 0x02);
    CHECK(cart->read(0x4000) == 0x05);
}

TEST_CASE("MBC1: mode 1 on a 2 MiB ROM maps bank (bank2 << 5) at 0000-3FFF") {
    auto hi = GENERATE(range(0, 4));
    INFO("bank2 " << hi);
    auto cart = Cartridge::load(makeRom(128, 0x01));
    selectRomBank(*cart, 0x03);
    selectBank2(*cart, static_cast<std::uint8_t>(hi));

    // Mode 0: 0000-3FFF is locked to bank 0
    CHECK(cart->read(0x0000) == 0);
    CHECK(cart->read(0x3FFF) == 0);

    selectMode(*cart, 0x01);
    CHECK(cart->read(0x0000) == hi << 5);
    CHECK(cart->read(0x3FFF) == hi << 5);
    CHECK(cart->read(0x4000) == ((hi << 5) | 0x03));

    // Back to mode 0: bank 0 again
    selectMode(*cart, 0x00);
    CHECK(cart->read(0x0000) == 0);
    CHECK(cart->read(0x3FFF) == 0);
    CHECK(cart->read(0x4000) == ((hi << 5) | 0x03));
}

TEST_CASE("MBC1: the mode register is 1 bit and can be written anywhere in 6000-7FFF") {
    auto addr = GENERATE(as<int>{}, 0x6000, 0x7000, 0x7FFF);
    INFO("addr " << addr);
    auto cart = Cartridge::load(makeRom(128, 0x01));
    selectBank2(*cart, 0x02);
    cart->write(static_cast<std::uint16_t>(addr), 0x01);
    CHECK(cart->read(0x0000) == 0x40);
    cart->write(static_cast<std::uint16_t>(addr), 0xFE); // bit 0 clear -> mode 0
    CHECK(cart->read(0x0000) == 0);
    cart->write(static_cast<std::uint16_t>(addr), 0xFF); // bit 0 set -> mode 1
    CHECK(cart->read(0x0000) == 0x40);
}

TEST_CASE("MBC1: on a small ROM bank2 and mode 1 do not change the ROM mapping") {
    auto banks = GENERATE(as<std::size_t>{}, 4, 32);
    INFO(banks << " banks");
    auto cart = Cartridge::load(makeRom(banks, 0x03, 0x02));
    selectRomBank(*cart, 0x02);
    selectBank2(*cart, 0x03);
    CHECK(cart->read(0x4000) == 2);
    selectMode(*cart, 0x01);
    CHECK(cart->read(0x0000) == 0);
    CHECK(cart->read(0x3FFF) == 0);
    CHECK(cart->read(0x4000) == 2);
}

// ===========================================================================
// MBC1: RAM banking
// ===========================================================================

TEST_CASE("MBC1: with 32 KiB RAM, mode 1 lets bank2 select RAM banks 0-3") {
    auto banks = GENERATE(as<std::size_t>{}, 4, 32); // ROM <= 512 KiB
    INFO(banks << " ROM banks");
    auto cart = Cartridge::load(makeRom(banks, 0x03, 0x03));
    enableRam(*cart);
    selectMode(*cart, 0x01);

    for (std::uint8_t b = 0; b < 4; ++b) {
        selectBank2(*cart, b);
        cart->write(0xA000, static_cast<std::uint8_t>(0x10 + b));
        cart->write(0xBFFF, static_cast<std::uint8_t>(0x20 + b));
    }
    for (std::uint8_t b = 0; b < 4; ++b) {
        INFO("RAM bank " << int(b));
        selectBank2(*cart, b);
        CHECK(cart->read(0xA000) == 0x10 + b);
        CHECK(cart->read(0xBFFF) == 0x20 + b);
    }
}

TEST_CASE("MBC1: with 32 KiB RAM, mode 0 locks A000-BFFF to RAM bank 0") {
    auto cart = Cartridge::load(makeRom(4, 0x03, 0x03));
    enableRam(*cart);
    selectMode(*cart, 0x01);
    for (std::uint8_t b = 0; b < 4; ++b) {
        selectBank2(*cart, b);
        cart->write(0xA000, static_cast<std::uint8_t>(0x10 + b));
    }

    selectMode(*cart, 0x00);
    auto hi = GENERATE(0, 1, 2, 3);
    INFO("bank2 " << hi);
    selectBank2(*cart, static_cast<std::uint8_t>(hi));
    CHECK(cart->read(0xA000) == 0x10);

    // A write in mode 0 also goes to RAM bank 0
    cart->write(0xA000, 0x5A);
    selectMode(*cart, 0x01);
    selectBank2(*cart, 0x00);
    CHECK(cart->read(0xA000) == 0x5A);
    selectBank2(*cart, 0x02);
    CHECK(cart->read(0xA000) == 0x12);
}

TEST_CASE("MBC1: with 32 KiB RAM, bank2 does not affect 4000-7FFF on a small ROM") {
    auto cart = Cartridge::load(makeRom(4, 0x03, 0x03));
    selectRomBank(*cart, 0x03);
    selectMode(*cart, 0x01);
    selectBank2(*cart, 0x02);
    CHECK(cart->read(0x4000) == 3);
    CHECK(cart->read(0x0000) == 0);
}

TEST_CASE("MBC1: with 8 KiB RAM, bank2 does not change which RAM is seen") {
    auto mode = GENERATE(0, 1);
    INFO("mode " << mode);
    auto cart = Cartridge::load(makeRom(4, 0x03, 0x02));
    enableRam(*cart);
    selectMode(*cart, static_cast<std::uint8_t>(mode));
    cart->write(0xA000, 0x55);
    cart->write(0xBFFF, 0xAA);
    for (std::uint8_t b = 1; b < 4; ++b) {
        INFO("bank2 " << int(b));
        selectBank2(*cart, b);
        CHECK(cart->read(0xA000) == 0x55);
        CHECK(cart->read(0xBFFF) == 0xAA);
    }
    // And a write with bank2 set lands in the same RAM
    selectBank2(*cart, 0x03);
    cart->write(0xA000, 0x66);
    selectBank2(*cart, 0x00);
    CHECK(cart->read(0xA000) == 0x66);
}

TEST_CASE("MBC1 constructed directly") {
    const auto rom = makeRom(8, 0x03, 0x02);
    Mbc1 cart{rom, 8, 0x2000};
    CHECK(cart.read(0x0000) == 0);
    CHECK(cart.read(0x4000) == 1);
    cart.write(0x2000, 0x06);
    CHECK(cart.read(0x4000) == 6);
    cart.write(0x0000, 0x0A);
    cart.write(0xA123, 0x77);
    CHECK(cart.read(0xA123) == 0x77);
}

// ===========================================================================
// Through the real Bus
// ===========================================================================

TEST_CASE("Cartridge on Bus: ROM reads, MBC writes and RAM are routed to the cartridge") {
    Bus bus{Cartridge::load(makeRom(32, 0x03, 0x03))};

    CHECK(bus.read(0x0000) == 0);
    CHECK(bus.read(0x3FFF) == 0);
    CHECK(bus.read(0x4000) == 1);
    CHECK(bus.read(0x7FFF) == 1);

    bus.write(0x2000, 0x1D);
    CHECK(bus.read(0x4000) == 0x1D);
    CHECK(bus.read(0x7FFF) == 0x1D);
    // The ROM itself is unchanged by the register write
    CHECK(bus.read(0x2000) != 0x1D);

    bus.write(0x0000, 0x0A);
    bus.write(0x6000, 0x01);
    bus.write(0x4000, 0x02);
    bus.write(0xA000, 0xBE);
    bus.write(0xBFFF, 0xEF);
    CHECK(bus.read(0xA000) == 0xBE);
    CHECK(bus.read(0xBFFF) == 0xEF);
    bus.write(0x4000, 0x00);
    CHECK(bus.read(0xA000) != 0xBE);
    bus.write(0x4000, 0x02);
    CHECK(bus.read(0xA000) == 0xBE);

    bus.write(0x0000, 0x00);
    CHECK(bus.read(0xA000) != 0xBE);
}

TEST_CASE("Cartridge on Bus: a CPU program switches ROM banks with LD (2000),A") {
    auto rom = makeRom(8, 0x01);
    const std::uint8_t program[] = {
        0x3E, 0x05,       // LD A,0x05
        0xEA, 0x00, 0x20, // LD (0x2000),A
        0xFA, 0x00, 0x40, // LD A,(0x4000)
        0x47,             // LD B,A
        0x3E, 0x07,       // LD A,0x07
        0xEA, 0xFF, 0x3F, // LD (0x3FFF),A
        0xFA, 0xFF, 0x7F, // LD A,(0x7FFF)
    };
    for (std::size_t i = 0; i < sizeof program; ++i) {
        rom[0x0150 + i] = program[i];
    }
    Bus bus{Cartridge::load(rom)};
    CPU cpu{bus};
    cpu.initialize();
    cpu.PC = 0x0150;
    cpu.ime = false;

    for (int i = 0; i < 7; ++i) {
        cpu.step();
    }
    CHECK(cpu.PC == 0x0150 + sizeof program);
    CHECK(cpu.B() == 0x05);
    CHECK(cpu.A() == 0x07);
}
