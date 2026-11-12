// Round 190cs: the Remote control settings made FL-exact from the live FL answers (round 190cq) and static reads of FL's
// expression parser and link dispatcher (docs/analise/REA_rodada190bt.md section 7): operator priorities, FL's error
// texts, inter / internoswap / fold / warp / map, the link Mode (Default / Hold / Latch / Inc / Stay) and key links.
namespace link190cs
{
inline double value (const std::string& text, double input)
{
    std::string error;
    int at = 0;
    const auto f = lynx::linkformula::Formula::compile (text, &error, &at);
    return at == -1 ? f.evaluate (input) : -12345.0;
}
inline std::string error (const std::string& text)
{
    std::string e;
    int at = 0;
    lynx::linkformula::Formula::compile (text, &e, &at);
    return at >= 0 ? e : std::string();
}
inline bool near (double a, double b, double tolerance = 1.0e-9) { return std::abs (a - b) <= tolerance; }
} // namespace link190cs

TEST (link190cs_parser_priorities_are_fls)
{
    using link190cs::near;
    using link190cs::value;
    // the unary minus binds tighter than ^ (FL draws -Input^2 as +x^2)
    CHECK (near (value ("-Input^2", 0.5), 0.25) && near (value ("-Input^2", 0.9), 0.81));
    CHECK (near (value ("1-Input^2", 0.5), 0.75) && near (value ("-2^2", 0), 4.0) && near (value ("2^-1", 0), 0.5));
    // ^ over * / over + -, then comparisons, then not, then and / or
    CHECK (near (value ("2*3^2", 0), 18.0) && near (value ("1+2*3", 0), 7.0) && near (value ("2^3^2", 0), 64.0)); // (left to right)
    CHECK (near (value ("1+1>1", 0), 1.0) && near (value ("Input>0.5", 0.7), 1.0) && near (value ("Input>0.5", 0.3), 0.0));
    CHECK (near (value ("not Input>0.5", 0.7), 0.0) && near (value ("not Input>0.5", 0.2), 1.0)); // not (Input > 0.5)
    CHECK (near (value ("Input>0.2 and Input<0.8", 0.5), 1.0) && near (value ("Input>0.2 and Input<0.8", 0.9), 0.0));
    CHECK (near (value ("Input=0.5", 0.5), 1.0) && near (value ("Input<>0.5", 0.5), 0.0) && near (value ("Input>=0.5", 0.5), 1.0));
    CHECK (near (value ("Input<0.5", 0.4), 1.0) && near (value ("Input<=0.5", 0.6), 0.0));
    CHECK (near (value ("7 fmod 2", 0), 1.0) && near (value ("-7 fmod 2", 0), -1.0) && near (value ("5!", 0), 120.0) && near (value ("50%", 0), 0.5));
    CHECK (near (value ("Input in 0.5", 0.5), 1.0) && near (value ("2+Input in 0.5", 0.5), 3.0)); // ("in" binds as tightly as the minus)
}

TEST (link190cs_parser_errors_are_fls_texts)
{
    using link190cs::error;
    // FL's messages, in FL's order of checks (names, brackets, neighbours, the tree)
    CHECK (error ("Input*") == "Syntax error: function or operand has too few arguments");
    CHECK (error ("Min(Input)") == "Syntax error: function or operand has too few arguments");
    CHECK (error ("*2") == "Syntax error: function or operand has too few arguments");
    CHECK (error ("foo(Input)") == "Invalid identifier: foo");
    CHECK (error ("input*piq4") == "Invalid identifier: piq4" && error ("1q0") == "Invalid identifier: q0");
    CHECK (error ("Sin(Input") == "Unequal brackets" && error ("Input)") == "Unequal brackets");
    CHECK (error ("foo(Input") == "Invalid identifier: foo"); // (names first)
    CHECK (error ("Input 2") == "input two space limited variables/constants");
    CHECK (error ("()") == "Empty brackets ()" && error ("Rand()").empty());
    CHECK (error ("(1)(2)") == "Missing operand between )(");
    CHECK (error ("(1)2") == "Missing operand between ) and constant/variable");
    CHECK (error ("2(1)") == "Missing operand between constant/variable and (");
    CHECK (error ("Min(1,2,3)") == "Too many arguments");
    CHECK (error ("1 + # 2") == "Syntax error");
    std::string deep;
    for (int i = 0; i < 3000; ++i)
        deep += "(";
    deep += "1";
    for (int i = 0; i < 3000; ++i)
        deep += ")";
    CHECK (! error (deep).empty()); // (no crash on absurd nesting)
}

TEST (link190cs_inter_internoswap_fold_warp_map)
{
    using link190cs::near;
    using link190cs::value;
    // inter (x, a, b): 1 between a and b in either order (at thousandths), else 0; internoswap: a <= b only
    CHECK (near (value ("inter(Input,0.2,0.8)", 0.5), 1.0) && near (value ("inter(Input,0.8,0.2)", 0.5), 1.0));
    CHECK (near (value ("inter(Input,0.2,0.8)", 0.9), 0.0) && near (value ("inter(Input,0.2,0.8)", 0.2), 1.0) && near (value ("inter(Input,0.2,0.8)", 0.8), 1.0));
    CHECK (near (value ("internoswap(Input,0.2,0.8)", 0.5), 1.0) && near (value ("internoswap(Input,0.8,0.2)", 0.5), 0.0));
    CHECK (near (value ("inter(0.2,0.8,Input)", 0.1), 1.0) && near (value ("inter(0.2,0.8,Input)", 0.3), 0.0)); // (FL's curve)
    CHECK (near (value ("inter(Input,0.5,0.5)", 0.5004), 1.0) && near (value ("inter(Input,0.5,0.5)", 0.5006), 0.0)); // thousandths
    CHECK (near (value ("internoswap(0.8,0.2,Input)", 0.9), 1.0) && near (value ("internoswap(0.8,0.2,Input)", 0.7), 0.0));
    // fold: 0 -> 1 -> 0 over 0 -> 1 -> 2, any x
    CHECK (near (value ("fold(Input*2)", 0.0), 0.0) && near (value ("fold(Input*2)", 0.5), 1.0) && near (value ("fold(Input*2)", 1.0), 0.0));
    CHECK (near (value ("fold(Input*2)", 0.25), 0.5) && near (value ("fold(Input*2-0.5)", 0.0), 0.5) && near (value ("fold(Input*3)", 1.0), 1.0));
    CHECK (near (value ("fold(-0.25)", 0), 0.25) && near (value ("fold(2.25)", 0), 0.25));
    // warp (x, p): 0...1 stays, 1 wraps to 0; outside, FMod (x + p, 1), keeping the sign of a negative x + p
    CHECK (near (value ("warp(Input,0.5)", 0.3), 0.3) && near (value ("warp(Input,0.5)", 1.0), 0.0) && near (value ("warp(Input,0.5)", 0.0), 0.0));
    CHECK (near (value ("warp(Input*2,0.5)", 0.6), 0.7, 1.0e-12) && near (value ("warp(Input*2,0)", 0.75), 0.5));
    CHECK (near (value ("warp(Input*2-1,0.5)", 0.4), 0.3, 1.0e-12) && near (value ("warp(Input*2-1,0.5)", 0.1), -0.3, 1.0e-12));
    CHECK (near (value ("warp(Input*2,0.25)", 0.6), 0.45, 1.0e-12) && near (value ("warp(Input*2,0.25)", 0.9), 0.05, 1.0e-12));
    // map (x, lo, hi) = lo + (hi - lo) x
    CHECK (near (value ("map(Input,0.2,0.6)", 0.5), 0.4) && near (value ("map(Input,1,0)", 0.25), 0.75));
    // names in any letter case, the default mapping untouched
    CHECK (near (value ("MAP(Input,0,0.5)", 1.0), 0.5) && lynx::linkformula::Formula::compile ("Input").isDefault());
}

TEST (link190cs_link_modes)
{
    using namespace lynx::linkformula;
    const Formula oneToOne;
    const auto is = [] (std::optional<float> v, float expected) { return v.has_value() && std::abs (*v - expected) < 1.0e-6f; };
    // Default and Hold: every event as it is (a key: velocity / 127, its release 0), the formula applied
    for (int mode : { 0, 1 })
    {
        CHECK (is (linkModeValue (mode, oneToOne, 100.0f / 127.0f, 0.3f), 100.0f / 127.0f));
        CHECK (is (linkModeValue (mode, oneToOne, 0.0f, 0.3f), 0.0f));
        CHECK (is (linkModeValue (mode, Formula::compile ("1-Input"), 0.0f, 0.3f), 1.0f));
        CHECK (linkModeIsAbsolute (mode));
    }
    // Latch: a press switches to whichever of f(0) and f(1) is farther; releases do nothing
    CHECK (is (linkModeValue (2, oneToOne, 0.8f, 0.2f), 1.0f) && is (linkModeValue (2, oneToOne, 0.8f, 0.9f), 0.0f));
    CHECK (is (linkModeValue (2, oneToOne, 0.8f, 0.5f), 1.0f)); // (as far: f(1))
    CHECK (! linkModeValue (2, oneToOne, 0.0f, 0.2f));
    CHECK (is (linkModeValue (2, Formula::compile ("Input*0.5+0.25"), 1.0f, 0.25f), 0.75f) && is (linkModeValue (2, Formula::compile ("Input*0.5+0.25"), 1.0f, 0.75f), 0.25f));
    // Inc: a press adds f(1/128), limited to 0...1
    CHECK (is (linkModeValue (3, oneToOne, 0.5f, 0.25f), 0.25f + 1.0f / 128.0f) && ! linkModeValue (3, oneToOne, 0.0f, 0.25f));
    CHECK (is (linkModeValue (3, Formula::compile ("0.1"), 1.0f, 0.25f), 0.35f) && is (linkModeValue (3, Formula::compile ("-Input"), 1.0f, 0.0f), 0.0f));
    CHECK (is (linkModeValue (3, Formula::compile ("0.1"), 1.0f, 0.95f), 1.0f));
    // Stay: a press sets f(velocity); the release keeps it
    CHECK (is (linkModeValue (4, oneToOne, 64.0f / 127.0f, 0.9f), 64.0f / 127.0f) && ! linkModeValue (4, oneToOne, 0.0f, 0.9f));
    CHECK (! linkModeIsAbsolute (2) && ! linkModeIsAbsolute (3) && ! linkModeIsAbsolute (4));
}

TEST (link190cs_key_links_and_modes_in_the_settings_text)
{
    using namespace lynx;
    const AutomationTarget volume { AutomationTargetKind::channelVolume, 0 }, insert { AutomationTargetKind::mixerVolume, 1 };
    CHECK (isNoteLink (linkNoteBase) && isNoteLink (linkNoteBase + 127) && ! isNoteLink (127) && ! isNoteLink (linkNoteBase + 128));
    MidiLink key { linkNoteBase + 60, 0, volume };
    CHECK (! key.hasSettings());
    key.mode = 2;
    CHECK (key.hasSettings());
    // Omni on a mixer track's volume follows the selected track
    MidiLink omni { 7, 0, insert };
    omni.omni = true;
    CHECK (linkOmniTarget (omni, 3, 5) == (AutomationTarget { AutomationTargetKind::mixerVolume, 5 }));

    // the MIDI settings text: a key link on its own line, the mode after it; old text unchanged
    prefs::MidiPrefs p;
    prefs::GlobalLink g { linkNoteBase + 60, 0, (int) AutomationTargetKind::channelVolume, 0, 0, 0 };
    g.mode = 3;
    prefs::GlobalLink c { 7, -1, (int) AutomationTargetKind::channelPan, 0, 0, 0 };
    p.globalLinks = { c, g };
    const auto text = p.toText();
    CHECK (text.find ("link=7|-1|2|0|0|0\n") != std::string::npos && text.find ("notelink=60|0|1|0|0|0\nlinkset=-1|0|469|\nlinkmode=3\n") != std::string::npos);
    CHECK (prefs::MidiPrefs::fromText (text) == p);
    // an older version skips the lines it does not know: the controller link stays, nothing becomes CC 127
    std::string stripped;
    std::istringstream in (text);
    for (std::string line; std::getline (in, line);)
        if (line.rfind ("notelink=", 0) != 0 && line.rfind ("linkset=", 0) != 0 && line.rfind ("linkmode=", 0) != 0)
            stripped += line + "\n";
    const auto older = prefs::MidiPrefs::fromText (stripped);
    CHECK (older.globalLinks.size() == 1 && older.globalLinks[0] == c);
    CHECK (prefs::MidiPrefs::fromText ("linkmode=2\n").globalLinks.empty()); // (no link before it)

    // (the project file's key links, modes and "Record automation" round trip are checked by --linkshot: the .lynx
    // writer lives in the engine, outside this core test binary)
}
