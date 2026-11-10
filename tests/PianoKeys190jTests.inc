// Round 190j (discussion #136): the Piano roll's "Piano keys" keyboard style (core/PianoKeys.h).

TEST (pianokeys190j_flat_style_plays_the_rows_key)
{
    using namespace lynx::pianokeys;
    for (int key = 0; key < 128; ++key)
        for (int x : { 0, 30, 59 })
            for (int y : { 0, 6, 13 })
                CHECK (keyAt (Style::flat, key, y, 14, x, 36) == key);
}

TEST (pianokeys190j_piano_style_white_keys_meet_behind_the_black_keys)
{
    using namespace lynx::pianokeys;
    const int rowHeight = 14, blackWidth = 36;
    // on a black key itself, and on any white key's own row: that key
    CHECK (keyAt (Style::piano, 61, 0, rowHeight, 0, blackWidth) == 61);
    CHECK (keyAt (Style::piano, 61, 13, rowHeight, blackWidth - 1, blackWidth) == 61);
    CHECK (keyAt (Style::piano, 60, 0, rowHeight, 50, blackWidth) == 60);
    CHECK (keyAt (Style::piano, 60, 13, rowHeight, 0, blackWidth) == 60);
    // to the right of C#5: D5 above the middle, C5 below it
    CHECK (keyAt (Style::piano, 61, 0, rowHeight, blackWidth, blackWidth) == 62);
    CHECK (keyAt (Style::piano, 61, 6, rowHeight, 50, blackWidth) == 62);
    CHECK (keyAt (Style::piano, 61, 7, rowHeight, 50, blackWidth) == 60);
    CHECK (keyAt (Style::piano, 61, 13, rowHeight, 50, blackWidth) == 60);
    // every pixel of the keyboard belongs to a key next to its row, and to a white key right of the black keys
    for (int key = 0; key < 128; ++key)
        for (int y = 0; y < rowHeight; ++y)
        {
            const auto k = keyAt (Style::piano, key, y, rowHeight, blackWidth + 3, blackWidth);
            CHECK (k >= 0 && k <= 127 && std::abs (k - key) <= 1);
            CHECK (! isBlack (k));
        }
    // an odd and a one-pixel row still split without leaving the range
    CHECK (keyAt (Style::piano, 1, 0, 1, 40, 36) == 0);
    CHECK (keyAt (Style::piano, 126, 2, 7, 40, 36) == 127);
    CHECK (keyAt (Style::piano, 126, 3, 7, 40, 36) == 125);
}

TEST (pianokeys190j_lines_between_white_keys)
{
    using namespace lynx::pianokeys;
    // an octave has seven white keys and so seven borders: two along a row's bottom (C over B, F over E) and five
    // through the black keys' rows
    int along = 0, through = 0;
    for (int key = 60; key < 72; ++key)
    {
        along += lineBelow (key) ? 1 : 0;
        through += isBlack (key) ? 1 : 0;
    }
    CHECK (along == 2 && through == 5);
    CHECK (lineBelow (60) && lineBelow (65));
    CHECK (! lineBelow (62) && ! lineBelow (61) && ! lineBelow (0));
    CHECK (upperPart (14) == 7 && upperPart (7) == 3);
}
