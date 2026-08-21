#ifndef QTERMX_PALETTE_H
#define QTERMX_PALETTE_H

// The terminal color palette — Qt-free (ADR-0005). The single source of
// truth for the colors the emulator can answer OSC 4/10/11 color queries
// with: the 16 ANSI colors, the cube levels and grayscale ramp that
// complete the 256-entry palette, and the default foreground/background.
// The GUI renderer imports the same tables, so a themed terminal reports
// the themed colors — never a hardcoded default (port of pyqtermx
// palette.py).
//
// Header-only: the module is pure constants plus two small pure
// functions, so a header keeps the Qt-free core build simple (no .cpp to
// wire into qtermx_core) — a deliberate deviation from the header+source
// convention for the other core modules, which carry real state.

#include <array>
#include <cstdio>
#include <string>

namespace qtermx::palette {

// xterm's 16 ANSI colors (bright variants in the second half).
inline constexpr std::array<int, 16> kPalette16 = {
    0x000000, 0xCD0000, 0x00CD00, 0xCDCD00, 0x0000EE, 0xCD00CD, 0x00CDCD, 0xE5E5E5,
    0x7F7F7F, 0xFF0000, 0x00FF00, 0xFFFF00, 0x5C5CFF, 0xFF00FF, 0x00FFFF, 0xFFFFFF,
};

// The 6×6×6 color cube levels (16–231).
inline constexpr std::array<int, 6> kCubeLevels = {0, 95, 135, 175, 215, 255};

// The default foreground/background RGB (the `-1` cell colors).
inline constexpr std::array<int, 3> kDefaultFgRgb = {0xE8, 0xE8, 0xE8};
inline constexpr std::array<int, 3> kDefaultBgRgb = {0x10, 0x10, 0x10};

// The (r, g, b) of palette index 0–255: the 16 ANSI colors, then the
// 6×6×6 cube (16–231), then the grayscale ramp (232–255). An index
// outside 0–255 returns black (the renderer's fallback for invalid
// codes).
inline std::array<int, 3> paletteRgb(int index)
{
    if (index < 16) {
        const int value = kPalette16[index];
        return {(value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF};
    }
    if (index < 232) {
        const int value = index - 16;
        return {kCubeLevels[value / 36], kCubeLevels[(value / 6) % 6], kCubeLevels[value % 6]};
    }
    if (index < 256) {
        const int gray = 8 + 10 * (index - 232);
        return {gray, gray, gray};
    }
    return {0, 0, 0};
}

// `#rrggbb` lowercase hex — the `QColor.name(HexRgb)` form the widget
// forwards, and the input form `set_palette` accepts.
inline std::string rgbHex(int r, int g, int b)
{
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", r, g, b);
    return buf;
}

} // namespace qtermx::palette

#endif // QTERMX_PALETTE_H
