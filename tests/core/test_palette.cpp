// T14 — Palette (port of pyqtermx palette.py): the 16 ANSI colors, the
// 6×6×6 cube and grayscale ramp that complete the 256-entry palette,
// the default fg/bg, and the `#rrggbb` hex form. Qt-free — the single
// source of truth shared by the renderer and OSC 4/10/11 replies.
#include "harness.h"
#include "palette.h"

using namespace qtermx;
using namespace qtermx::palette;

TEST_CASE(palette_16_ansi_colors)
{
    QTERMX_CHECK((paletteRgb(0) == std::array<int, 3>{0, 0, 0}));
    QTERMX_CHECK((paletteRgb(1) == std::array<int, 3>{205, 0, 0}));
    QTERMX_CHECK((paletteRgb(7) == std::array<int, 3>{229, 229, 229}));
    QTERMX_CHECK((paletteRgb(8) == std::array<int, 3>{127, 127, 127}));
    QTERMX_CHECK((paletteRgb(15) == std::array<int, 3>{255, 255, 255}));
}

TEST_CASE(palette_cube_levels)
{
    QTERMX_CHECK((paletteRgb(16) == std::array<int, 3>{0, 0, 0}));
    QTERMX_CHECK((paletteRgb(17) == std::array<int, 3>{0, 0, 95}));
    QTERMX_CHECK((paletteRgb(21) == std::array<int, 3>{0, 0, 255}));
    QTERMX_CHECK((paletteRgb(196) == std::array<int, 3>{255, 0, 0}));
    QTERMX_CHECK((paletteRgb(231) == std::array<int, 3>{255, 255, 255}));
}

TEST_CASE(palette_grayscale_ramp)
{
    QTERMX_CHECK((paletteRgb(232) == std::array<int, 3>{8, 8, 8}));
    QTERMX_CHECK((paletteRgb(255) == std::array<int, 3>{238, 238, 238}));
}

TEST_CASE(palette_out_of_range_is_black)
{
    // The documented contract: an index outside 0–255 returns black.
    // (The Python oracle's -1 wraps to white via negative list
    // indexing — an accident of the language, not the intent; the C++
    // port implements the documented behavior instead.)
    QTERMX_CHECK((paletteRgb(256) == std::array<int, 3>{0, 0, 0}));
    QTERMX_CHECK((paletteRgb(-1) == std::array<int, 3>{0, 0, 0}));
}

TEST_CASE(palette_defaults)
{
    QTERMX_CHECK((kDefaultFgRgb == std::array<int, 3>{0xE8, 0xE8, 0xE8}));
    QTERMX_CHECK((kDefaultBgRgb == std::array<int, 3>{0x10, 0x10, 0x10}));
}

TEST_CASE(palette_rgb_hex)
{
    QTERMX_CHECK(rgbHex(0xE8, 0xE8, 0xE8) == "#e8e8e8");
    QTERMX_CHECK(rgbHex(0x10, 0x10, 0x10) == "#101010");
    QTERMX_CHECK(rgbHex(255, 0, 0) == "#ff0000");
    QTERMX_CHECK(rgbHex(12, 34, 56) == "#0c2238");
}
