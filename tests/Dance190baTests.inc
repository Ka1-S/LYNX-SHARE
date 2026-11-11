// Round 190ba, issue #121: sprite sheets for LYNX Dance in FL's Fruity Dance format (a picture of 8 frames across and
// one row per move + a names file). The rules are FL's, read in its plugin (docs/analise/REA_rodada190ba-121.md).

TEST (dance190ba_sheet_names_and_layout)
{
    using namespace lynx::dance;
    // the names file: one name per line, CR LF / LF / CR, no ending needed on the last line (FL's own Dance.txt has none)
    const auto fl = sheetNames ("Waiting\r\nStepping\r\nJumping\r\nZombie\r\nWaving\r\nHula\r\nWindmill\r\nZitabata\r\nDervish\r\nHeld");
    CHECK (fl.size() == 10 && fl == defaultSheetNames());
    CHECK (sheetNames ("Count").size() == 1 && sheetNames ("Count")[0] == "Count");
    CHECK (sheetNames ("a\nb\n").size() == 2 && sheetNames ("a\rb").size() == 2);
    CHECK (sheetNames ("a\n\nb").size() == 3 && sheetNames ("a\n\nb")[1].empty()); // (an empty line is a row too)
    CHECK (sheetNames ("").empty());
    CHECK (sheetNames ("\xef\xbb\xbfIdle\nRun")[0] == "Idle"); // (a UTF-8 mark)
    CHECK (sheetNames ("Dan\xe7" "a")[0] == "Dan\xc3\xa7" "a"); // (Latin-1 bytes)
    CHECK (sheetNames ("Dan\xc3\xa7" "a")[0] == "Dan\xc3\xa7" "a");

    // the names file's path: the picture's with .txt in place of its extension
    CHECK (sheetNamesPath ("C:\\Sprites\\BF.png") == "C:\\Sprites\\BF.txt");
    CHECK (sheetNamesPath ("/home/a.b/dancer") == "/home/a.b/dancer.txt");
    CHECK (sheetNamesPath ("/home/a/my.dancer.PNG") == "/home/a/my.dancer.txt");

    // FL's two sheets: Dance.png 1760 x 2560 with 10 names, Count.png 800 x 100 with 1
    auto l = sheetLayout (1760, 2560, 10);
    CHECK (l.valid && l.rows == 10 && l.frameWidth == 220 && l.frameHeight == 256);
    l = sheetLayout (800, 100, 1);
    CHECK (l.valid && l.rows == 1 && l.frameWidth == 100 && l.frameHeight == 100);
    // no names (an empty file): one row
    l = sheetLayout (800, 100, 0);
    CHECK (l.valid && l.rows == 1 && l.frameHeight == 100);
    // "Bitmap size is incorrect.": not a whole number of frames across or down
    CHECK (! sheetLayout (1763, 2560, 10).valid);
    CHECK (! sheetLayout (1760, 2565, 10).valid);
    CHECK (! sheetLayout (4, 100, 1).valid && ! sheetLayout (800, 3, 10).valid);
    // (FL's test is width mod (width / 8): a remainder that the frame divides passes, e.g. 9 wide = frames of 1)
    CHECK (sheetLayout (9, 4, 1).valid && sheetLayout (9, 4, 1).frameWidth == 1);

    // the moves: Freestyle, then the rows but the last (the row shown while the dancer is held)
    const auto moves = sheetMoveNames (fl);
    CHECK (moves.size() == 10 && moves[0] == "Freestyle" && moves[1] == "Waiting" && moves[9] == "Dervish");
    CHECK (sheetMoveCount (10) == 10 && sheetHeldRow (10) == 9);
    CHECK (sheetMoveNames ({ "Count" }).size() == 1 && sheetMoveCount (1) == 1 && sheetHeldRow (1) == 0);
    CHECK (sheetMoveNames ({}).size() == 1 && sheetMoveCount (0) == 1 && sheetHeldRow (0) == 0);

    // the keys follow the sheet: B4 up = the moves, C4 down = the same mirrored, F4 / F#4 hide / show
    for (const int rows : { 1, 3, 10, 40, 128 })
    {
        const auto n = sheetMoveCount (rows);
        CHECK (actionForKey (59, n).kind == KeyAction::Kind::move && actionForKey (59, n).move == 0);
        CHECK (actionForKey (48, n).kind == KeyAction::Kind::move && actionForKey (48, n).mirrored);
        CHECK (actionForKey (hideKey, n).kind == KeyAction::Kind::hide && actionForKey (showKey, n).kind == KeyAction::Kind::show);
        for (int key = 0; key < 128; ++key)
        {
            const auto a = actionForKey (key, n);
            if (a.kind == KeyAction::Kind::move)
                CHECK (a.move >= 0 && a.move < n && (a.mirrored ? a.move == 48 - key : a.move == key - 59));
        }
        const auto top = std::min (59 + n - 1, 127);
        CHECK (actionForKey (top, n).move == top - 59);
        CHECK (top == 127 || actionForKey (top + 1, n).kind == KeyAction::Kind::none);
        const auto low = std::max (48 - (n - 1), 0);
        CHECK (actionForKey (low, n).move == 48 - low && actionForKey (low, n).mirrored);
        CHECK (low == 0 || actionForKey (low - 1, n).kind == KeyAction::Kind::none);
    }
    // LYNX's own dancer: as before
    for (int key = 0; key < 128; ++key)
        CHECK (actionForKey (key).kind == actionForKey (key, moveCount).kind && actionForKey (key).move == actionForKey (key, moveCount).move);
}

TEST (dance190ba_sheet_state)
{
    using namespace lynx::dance;
    // a dancer without a sheet keeps the state text it had (no new lines)
    {
        Settings s;
        s.move = hula;
        const auto bytes = toState (s);
        const std::string text (bytes.begin(), bytes.end());
        CHECK (text.find ("sheet=") == std::string::npos && text.find ("blend=") == std::string::npos && text.find ("move=hula") != std::string::npos);
    }
    // with a sheet: the path, the blend and a move beyond LYNX's own eleven
    {
        Settings s;
        s.sheet = "C:\\Users\\Me\\Sprites\\Boyfriend = cool.png";
        s.blend = 0.5f;
        s.move = 17;
        s.mirrored = true;
        const auto back = fromState (toState (s));
        CHECK (back && *back == s);
        s.move = 3;
        CHECK (fromState (toState (s)) && *fromState (toState (s)) == s);
        s.sheet = "/home/me/dancers/\xc3\xa7.png";
        CHECK (read (toState (s)) == s);
    }

    // FL's state, version 2: the 40 bytes of version 1, then the sheet's file (a length byte, the characters)
    auto flState = [] (int version, int move, int blend, const std::string& path, bool longForm = false) {
        std::vector<std::uint8_t> b (40, 0);
        auto put = [&b] (int at, std::int32_t v) { std::memcpy (b.data() + at, &v, 4); };
        put (0, version);
        put (4, move);
        put (12, 1);
        put (16, blend);
        put (24, 300);
        put (28, 420);
        put (32, 64); // (the reflection)
        if (version >= 2)
        {
            if (longForm || path.size() >= 255)
            {
                b.push_back (0xff);
                for (int k = 0; k < 4; ++k)
                    b.push_back ((std::uint8_t) ((std::uint32_t) path.size() >> (8 * k)));
            }
            else
                b.push_back ((std::uint8_t) path.size());
            b.insert (b.end(), path.begin(), path.end());
        }
        return b;
    };
    {
        const auto f = fromFl (flState (2, 5, 64, "C:\\Sprites\\BF.png"));
        CHECK (f && f->sheet == "C:\\Sprites\\BF.png" && f->move == 5 && std::abs (f->blend - 0.25f) < 1.0e-6f && f->scale == 1.0f && f->x == 300 && f->y == 420);
        // FL's own dancer: an empty name
        const auto own = fromFl (flState (2, 2, 64, ""));
        CHECK (own && own->sheet.empty() && own->move == 2);
        // version 1 has no name; bytes after it are not read as one
        auto v1 = flState (1, 2, 64, "");
        v1.push_back (3);
        v1.insert (v1.end(), { 'a', 'b', 'c' });
        CHECK (fromFl (v1) && fromFl (v1)->sheet.empty());
        // the long form (255, then an int) and a name of 300 characters
        const std::string longName = "C:\\" + std::string (300, 'x') + ".png";
        CHECK (fromFl (flState (2, 1, 64, longName)) && fromFl (flState (2, 1, 64, longName))->sheet == longName);
        CHECK (fromFl (flState (2, 1, 64, "D:\\a.png", true)) && fromFl (flState (2, 1, 64, "D:\\a.png", true))->sheet == "D:\\a.png");
        // control characters at the end are dropped; Latin-1 becomes UTF-8
        CHECK (fromFl (flState (2, 1, 64, std::string ("D:\\a.png\0\r\n", 11)))->sheet == "D:\\a.png");
        CHECK (fromFl (flState (2, 1, 64, "D:\\dan\xe7" "a.png"))->sheet == "D:\\dan\xc3\xa7" "a.png");
        // cut short: the dancer is still read, without a sheet
        auto cut = flState (2, 4, 128, "C:\\Sprites\\BF.png");
        cut.resize (cut.size() - 5);
        CHECK (fromFl (cut) && fromFl (cut)->sheet.empty() && fromFl (cut)->move == 4 && std::abs (fromFl (cut)->blend - 0.5f) < 1.0e-6f);
        auto cutLong = flState (2, 4, 128, "x", true);
        cutLong.resize (43);
        CHECK (fromFl (cutLong) && fromFl (cutLong)->sheet.empty());
        // LYNX writes FL's state for an exported .flp (round 190ba: before, an edited dancer went out as LYNX's text)
        Settings out;
        out.move = 4;
        out.mirrored = true;
        out.visible = false;
        out.blend = 0.5f;
        out.speed = 2.5f;
        out.x = 640;
        out.y = 333;
        out.sheet = "C:\\Sprites\\BF.png";
        const auto bytes = toFl (out);
        CHECK (bytes.size() == 40 + 1 + out.sheet.size() && bytes[0] == 2 && bytes[32] == 64 && bytes[36] == 1 && bytes[39] == 1 && bytes[40] == out.sheet.size());
        CHECK (fromFl (bytes) && *fromFl (bytes) == out);
        out.speed = 0.5f;
        out.sheet.clear();
        CHECK (toFl (out).size() == 41 && fromFl (toFl (out)) && *fromFl (toFl (out)) == out);
        out.sheet = "C:\\" + std::string (400, 'y') + ".png";
        CHECK (fromFl (toFl (out)) && fromFl (toFl (out))->sheet == out.sheet);
        CHECK (! fromState (toFl (out))); // (not mistaken for LYNX's text)
        // a move beyond LYNX's own moves is kept (the sheet's rows decide), and survives LYNX's state
        const auto far = fromFl (flState (2, 23, 64, "C:\\Sprites\\Many.png"));
        CHECK (far && far->move == 23 && read (toState (*far)) == *far);
    }
}

TEST (dance190ba_sheet_animation)
{
    using namespace lynx::dance;
    // a set move: its row (value - 1), the 8 frames in order over one cycle, no blending below 1 - blend of a frame
    {
        SheetAnimator a (7);
        for (int k = 0; k < 8; ++k)
        {
            const auto f = a.advance (k / 8.0 + 0.01, 10, 3, false, true, 0.25f);
            CHECK (f.row == 2 && f.column == k && f.mix == 0.0f && ! f.mirrored && f.nextRow == f.row && f.nextColumn == f.column);
        }
        // the last quarter of a frame fades to the next one, in steps of 1/255
        auto f = a.advance (1.0 + (2.0 + 0.875) / 8.0, 10, 3, false, true, 0.25f);
        CHECK (f.row == 2 && f.column == 2 && f.nextRow == 2 && f.nextColumn == 3 && std::abs (f.mix - 128.0f / 255.0f) < 1.0e-6f);
        f = a.advance (1.0 + (2.0 + 0.75) / 8.0 + 1.0e-9, 10, 3, false, true, 0.25f);
        CHECK (f.mix >= 0.0f && f.mix < 0.01f);
        f = a.advance (1.0 + (2.0 + 0.999) / 8.0, 10, 3, false, true, 0.25f);
        CHECK (f.mix > 0.99f && f.nextColumn == 3);
        // blend 0: never; blend 1: all the way through
        CHECK (a.advance (2.0 + 0.99 / 8.0, 10, 3, false, true, 0.0f).mix == 0.0f);
        f = a.advance (3.0 + 0.5 / 8.0, 10, 3, false, true, 1.0f);
        CHECK (std::abs (f.mix - 128.0f / 255.0f) < 1.0e-6f && f.column == 0 && f.nextColumn == 1);
    }
    // a change waits for the cycle's start ("Sync changes"); the last frame already fades to the coming row's first
    {
        SheetAnimator a (7);
        CHECK (a.advance (0.1, 10, 3, false, true, 0.25f).row == 2);
        CHECK (a.advance (0.5, 10, 6, true, true, 0.25f).row == 2);
        CHECK (! a.advance (0.6, 10, 6, true, true, 0.25f).mirrored);
        const auto last = a.advance (7.9 / 8.0, 10, 6, true, true, 0.25f);
        CHECK (last.row == 2 && last.column == 7 && last.nextRow == 5 && last.nextColumn == 0 && last.mix > 0.0f && ! last.mirrored);
        const auto next = a.advance (1.02, 10, 6, true, true, 0.25f);
        CHECK (next.row == 5 && next.column == 0 && next.mirrored);
        // a value past the sheet's moves: the last move (FL clamps the value to rows - 1)
        a.advance (1.5, 10, 99, false, true, 0.25f);
        CHECK (a.advance (2.01, 10, 99, false, true, 0.25f).row == 8);
    }
    // not playing: the first row ("Waiting"), whatever the move
    {
        SheetAnimator a (3);
        CHECK (a.advance (0.2, 10, 4, false, false, 0.25f).row == 0);
        CHECK (a.advance (1.2, 10, 0, false, false, 0.25f).row == 0);
        CHECK (a.advance (2.1, 10, 4, false, true, 0.25f).row == 3);
    }
    // Freestyle: a row from the second to the one before the last, drawn again each cycle; several different ones
    for (const int rows : { 3, 4, 10, 30 })
    {
        SheetAnimator a ((std::uint32_t) rows * 31u + 5u);
        std::set<int> seen;
        for (int cycle = 0; cycle < 400; ++cycle)
        {
            const auto f = a.advance (cycle + 0.05, rows, 0, false, true, 0.25f);
            CHECK (f.row >= 1 && f.row <= rows - 2);
            seen.insert (f.row);
            CHECK (a.advance (cycle + 0.5, rows, 0, false, true, 0.25f).row == f.row); // (the same all cycle long)
        }
        CHECK ((int) seen.size() == rows - 2);
    }
    // one or two rows: Freestyle is the first row
    for (const int rows : { 1, 2 })
    {
        SheetAnimator a (9);
        for (int cycle = 0; cycle < 20; ++cycle)
            CHECK (a.advance (cycle + 0.3, rows, 0, false, true, 0.25f).row == 0);
        CHECK (a.advance (30.3, rows, 1, false, true, 0.25f).row == 0);
    }
    // held with the mouse: the last row, unmirrored, blend 0.7; the move comes back afterwards
    {
        SheetAnimator a (5);
        a.advance (0.1, 10, 3, true, true, 0.25f);
        auto f = a.advance (0.2, 10, 3, true, true, 0.25f, true);
        CHECK (f.row == 9 && ! f.mirrored);
        f = a.advance ((3.0 + 0.66) / 8.0, 10, 3, true, true, 0.25f, true);
        CHECK (f.row == 9 && f.column == 3 && f.nextRow == 9 && f.nextColumn == 4 && std::abs (f.mix - 131.0f / 255.0f) < 1.0e-6f);
        f = a.advance (7.9 / 8.0, 10, 5, true, true, 0.25f, true);
        CHECK (f.row == 9 && f.nextRow == 9 && f.nextColumn == 0);
        f = a.advance (0.99, 10, 3, true, true, 0.0f);
        CHECK (f.row == 2 && f.mirrored);
        CHECK (a.row() == 2);
    }
    // the same seed gives the same dance; odd clocks do no harm
    {
        SheetAnimator a (11), b (11);
        for (int k = 0; k < 300; ++k)
            CHECK (a.advance (k * 0.137, 12, 0, false, true, 0.25f) == b.advance (k * 0.137, 12, 0, false, true, 0.25f));
        SheetAnimator c (1);
        for (const double t : { -3.25, std::nan (""), 1.0e12, -0.0 })
        {
            const auto f = c.advance (t, 5, 2, false, true, 0.25f);
            CHECK (f.row >= 0 && f.row < 5 && f.column >= 0 && f.column < 8 && f.nextColumn >= 0 && f.nextColumn < 8 && f.mix >= 0.0f && f.mix <= 1.0f);
        }
        const auto f = c.advance (0.5, 500, 2, false, true, 0.25f);
        CHECK (f.row >= 0 && f.row < maxMoves);
    }
}
