#pragma once

// The Piano roll's keyboard styles (round 190j, discussion #136).
//
// "Flat": every key is one row of the note grid, the black keys are shorter and the rest of a black key's row is empty
// (LYNX's default until round 190oo). "Piano" draws a traditional keyboard on the same rows: the white keys are wider
// than their rows and meet behind the black keys, with a dark line between every two white keys, so each key is one
// clearly bordered shape. "FL" (round 190oo, design review O-2, the default since): FL Studio 2026's own keyboard — the
// white keys meet behind the black keys as in "Piano", but FL draws a faint line only where two white keys touch
// without a black key between them (B / C and E / F), and its black keys shade from dark at the left to lighter at the
// tip (fl-pianoroll.png, 11-15). The rows of the note grid stay where they are in every style.
// No JUCE here (tests/PianoKeys190jTests.inc).

namespace lynx::pianokeys
{
enum class Style : int { flat = 0, piano = 1, fl = 2 };

// The default since round 190oo (O-2): FL's keyboard.
constexpr Style defaultStyle = Style::fl;

// "Piano" and "FL": the white keys reach behind the black keys (a click right of a black key plays a white key).
constexpr bool whiteKeysMeet (Style style) noexcept { return style == Style::piano || style == Style::fl; }

// ui-options.txt "prKeyStyle2" (round 190oo): any other number reads as the default.
constexpr Style styleFromInt (int value) noexcept
{
    return value == 0 ? Style::flat : value == 1 ? Style::piano : value == 2 ? Style::fl : defaultStyle;
}

constexpr bool isBlack (int key) noexcept
{
    const auto n = ((key % 12) + 12) % 12;
    return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
}

// Piano style: the white key above a black key's row covers the top `upperPart` pixels of what the black key leaves
// free, the white key below it the rest (the line between them is the first pixel of the lower part).
constexpr int upperPart (int rowHeight) noexcept { return rowHeight / 2; }

// Piano and FL styles: a white key has a line along the bottom of its own row when the key below it is white too (C over
// B, F over E); in the piano style the other borders between white keys run through the middle of a black key's row.
constexpr bool lineBelow (int key) noexcept { return ! isBlack (key) && key > 0 && ! isBlack (key - 1); }

// The key a point of the keyboard plays. `rowKey` is the key of the note grid's row under the point, `yInRow` the
// point's distance from the row's top (0 .. rowHeight - 1), `x` its distance from the keyboard's left edge and
// `blackWidth` the length of a black key. Flat: the row's key. Piano and FL: to the right of a black key, the white key
// above or below it.
constexpr int keyAt (Style style, int rowKey, int yInRow, int rowHeight, int x, int blackWidth) noexcept
{
    if (! whiteKeysMeet (style) || ! isBlack (rowKey) || x < blackWidth)
        return rowKey;
    const auto key = yInRow < upperPart (rowHeight) ? rowKey + 1 : rowKey - 1;
    return key < 0 ? 0 : key > 127 ? 127 : key;
}
} // namespace lynx::pianokeys
