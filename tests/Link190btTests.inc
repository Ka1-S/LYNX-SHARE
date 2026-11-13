// Round 190bt: FL's "Remote control settings" (docs/analise/REA_rodada190bt.md): the mapping formula of a controller
// link (core/LinkFormula.h), the smoothing slider, and a link's settings in the song and in the MIDI settings text.
namespace link190bt
{
inline bool close (double a, double b, double tolerance = 1.0e-9) { return std::abs (a - b) <= tolerance; }
inline double value (const std::string& text, double input)
{
    std::string error;
    int at = 0;
    const auto f = lynx::linkformula::Formula::compile (text, &error, &at);
    return at == -1 ? f.evaluate (input) : -12345.0;
}
} // namespace link190bt

TEST (link190bt_default_mapping_is_one_to_one)
{
    using namespace lynx::linkformula;
    std::string error = "x";
    int at = 0;
    for (const char* text : { "", "   ", "Input", "input", "INPUT", "  Input  " })
    {
        const auto f = Formula::compile (text, &error, &at);
        CHECK (f.isDefault() && at == -2 && error.empty()); // FL: "(default 1:1 mapping)"
        CHECK (isDefaultMapping (text));
        CHECK (f.map (0.25f) == 0.25f && f.map (0.0f) == 0.0f && f.map (1.0f) == 1.0f);
    }
    CHECK (Formula().isDefault() && Formula().evaluate (0.7) == 0.7);
    CHECK (! isDefaultMapping ("1-Input") && ! isDefaultMapping ("Input*1"));
}

TEST (link190bt_formula_presets_are_fls_and_all_compile)
{
    using namespace lynx::linkformula;
    const auto& list = presets();
    int formulas = 0, headers = 0;
    for (const auto& p : list)
    {
        if (p.formula == nullptr)
        {
            ++headers;
            continue;
        }
        ++formulas;
        std::string error;
        int at = 0;
        Formula::compile (p.formula, &error, &at);
        CHECK (at == -1 || at == -2); // (the two "Input" presets are the default mapping)
        CHECK (error.empty());
    }
    CHECK (headers == 3 && formulas == 25); // FL's FormulaMenu: Linear (absolute), Incremental (relative), Special
    CHECK (std::string (list[0].name) == "Linear (absolute)" && std::string (list[1].name) == "Default" && std::string (list[1].formula) == "Input");
    CHECK (std::string (list.back().name) == "Launchpad XY - Y" && std::string (list.back().formula) == "Int(Input*8)*0.14285714285714285");
}

TEST (link190bt_preset_formulas_give_fls_curves)
{
    using link190bt::close;
    using link190bt::value;
    CHECK (close (value ("1-Input", 0.25), 0.75));                      // Inverted
    CHECK (close (value ("Round(Input)", 0.49), 0.0) && close (value ("Round(Input)", 0.51), 1.0)); // Switch
    CHECK (close (value ("Round(Input)", 0.5), 0.0));                   // (FL rounds halves to the even number)
    CHECK (close (value ("Round(Input*3)", 0.5), 2.0));                 // 1.5 -> 2
    CHECK (close (value ("Int(Input*9)/8", 0.5), 0.5));                 // Steppy: 9 steps of 1/8
    CHECK (close (value ("Int(Input*9)/8", 0.99), 1.0) && close (value ("Int(Input*9)/8", 0.1), 0.0));
    CHECK (close (value ("Int(Input*25)/24", 0.5), 12.0 / 24.0));       // cents to semitones
    CHECK (close (value ("Input*0.5", 1.0), 0.5) && close (value ("Input*0.5+0.5", 0.0), 0.5)); // First / Last half
    // Center dead zone (small): flat at 0.5 around the centre, 0 at the bottom, 1 at the top
    const std::string small = "Min(Input*1.1,0.5)+Max(Input*1.1,0.6)-0.6";
    CHECK (close (value (small, 0.0), 0.0) && close (value (small, 0.5), 0.5) && close (value (small, 0.48), 0.5) && close (value (small, 1.0), 1.0));
    const std::string large = "Min(Input*1.3,0.5)+Max(Input*1.3,0.8)-0.8";
    CHECK (close (value (large, 0.0), 0.0) && close (value (large, 0.45), 0.5) && close (value (large, 0.6), 0.5) && close (value (large, 1.0), 1.0));
    CHECK (close (value ("Input*1.00793650794", 63.0 / 127.0), 0.5, 1.0e-9)); // MIDI CC centered around 63
    CHECK (close (value ("Input*0.9921875", 64.0 / 127.0), 0.5));       // ... around 64
    CHECK (close (value ("-Input", 0.25), -0.25) && close (value ("Input*4", 0.25), 1.0) && close (value ("0.02", 0.9), 0.02));
    CHECK (close (value ("Ceil(Input)", 0.0), 0.0) && close (value ("Ceil(Input)", 0.01), 1.0)); // Force boolean
    CHECK (close (value ("Int(Input*8)*0.14285714285714285", 1.0), 8.0 / 7.0));
    CHECK (close (value ("(Frac(Input*7.875+0.001)-0.001)*1.142857142857143", 0.0), 0.0, 1.0e-12));

    // what the control gets: the result limited to 0...1
    const auto f = lynx::linkformula::Formula::compile ("Input*4-1");
    CHECK (f.map (0.0f) == 0.0f && f.map (1.0f) == 1.0f && std::abs (f.map (0.375f) - 0.5f) < 1.0e-6f);
    CHECK (lynx::linkformula::Formula::compile ("Ln(Input-1)").map (0.5f) == 0.0f); // (not a number: the bottom)
}

TEST (link190bt_tension_is_fls_curve)
{
    using link190bt::close;
    using lynx::linkformula::tension;
    CHECK (tension (0.3, 0.0) == 0.3);
    // t > 0 ("Log scale", Tension(Input,10)): fast at the bottom; 1 - (11^(1-x) - 1) / 10
    CHECK (close (tension (0.0, 10.0), 0.0) && close (tension (1.0, 10.0), 1.0));
    CHECK (close (tension (0.5, 10.0), 1.0 - (std::sqrt (11.0) - 1.0) / 10.0, 1.0e-12));
    // t < 0 ("Inverted log scale"): slow at the bottom; (11^x - 1) / 10
    CHECK (close (tension (0.0, -10.0), 0.0) && close (tension (1.0, -10.0), 1.0));
    CHECK (close (tension (0.5, -10.0), (std::sqrt (11.0) - 1.0) / 10.0, 1.0e-12));
    CHECK (tension (0.5, 10.0) > 0.5 && tension (0.5, -10.0) < 0.5);
    CHECK (close (link190bt::value ("Tension(Input,10)", 0.5), tension (0.5, 10.0)));
    CHECK (close (link190bt::value ("tension(input,-10)", 0.5), tension (0.5, -10.0))); // (any letter case)
}

TEST (link190bt_formula_functions)
{
    using link190bt::close;
    using link190bt::value;
    // FL's own additions
    CHECK (close (value ("Min(Input,0.3)", 0.5), 0.3) && close (value ("Max(Input,0.3)", 0.5), 0.5));
    CHECK (close (value ("Int(-1.7)", 0), -1.0) && close (value ("Frac(2.75)", 0), 0.75) && close (value ("Frac(-2.75)", 0), -0.75));
    CHECK (close (value ("IfE(Input,0.5)", 0.5), 1.0) && close (value ("IfE(Input,0.5)", 0.4), 0.0));
    CHECK (close (value ("IfL(Input,0.5)", 0.4), 1.0) && close (value ("IfL(Input,0.5)", 0.5), 0.0));
    CHECK (close (value ("IfG(Input,0.5)", 0.6), 1.0) && close (value ("IfG(Input,0.5)", 0.5), 0.0));
    CHECK (close (value ("IfLE(Input,0.5)", 0.5), 1.0) && close (value ("IfLE(Input,0.5)", 0.6), 0.0));
    CHECK (close (value ("IfGE(Input,0.5)", 0.5), 1.0) && close (value ("IfGE(Input,0.5)", 0.4), 0.0));
    CHECK (close (value ("Case(IfG(Input,0.5),0.9,0.1)", 0.7), 0.9) && close (value ("Case(IfG(Input,0.5),0.9,0.1)", 0.2), 0.1));
    CHECK (close (value ("Sum(Input,0.25)", 0.5), 0.75) && close (value ("Neg(Input)", 0.5), -0.5));
    CHECK (close (value ("Log2(8)", 0), 3.0) && close (value ("Tg(0)", 0), 0.0) && close (value ("ArcTg(0)", 0), 0.0));
    CHECK (close (value ("Sec(0)", 0), 1.0) && close (value ("CoSec(pi/2)", 0), 1.0, 1.0e-12) && close (value ("CTg(pi/4)", 0), 1.0, 1.0e-12));
    const auto r = value ("Rand()", 0), seeded = value ("SeededRand(Input)", 0.3);
    CHECK (r >= 0.0 && r < 1.0 && seeded >= 0.0 && seeded < 1.0 && close (seeded, value ("SeededRand(Input)", 0.3)));
    // the general set
    CHECK (close (value ("sqr(Input)", 0.5), 0.25) && close (value ("sqrt(Input)", 0.25), 0.5) && close (value ("abs(Input-1)", 0.25), 0.75));
    CHECK (close (value ("power(Input,2)", 0.5), 0.25) && close (value ("pow(2,3)", 0), 8.0) && close (value ("exp(ln(Input))", 0.4), 0.4, 1.0e-12));
    CHECK (close (value ("floor(2.7)+ceil(2.2)+trunc(-2.7)", 0), 3.0) && close (value ("log10(1000)", 0), 3.0, 1.0e-12));
    CHECK (close (value ("sin(pi/2)", 0), 1.0) && close (value ("cos(0)", 0), 1.0) && close (value ("if(Input>0.5,1,0)", 0.6), 1.0));
    // operators: the usual precedence, brackets, powers, comparisons, numbers
    CHECK (close (value ("1+2*3", 0), 7.0) && close (value ("(1+2)*3", 0), 9.0) && close (value ("2^3", 0), 8.0) && close (value ("-Input+1", 0.25), 0.75));
    CHECK (close (value ("7 div 2", 0), 3.0) && close (value ("7 mod 2", 0), 1.0) && close (value ("1e-1 + .5 + $10", 0), 16.6));
    CHECK (close (value ("Input >= 0.5", 0.5), 1.0) && close (value ("Input <> 0.5", 0.5), 0.0) && close (value ("not (Input < 0.5)", 0.2), 0.0));
}

TEST (link190bt_formula_mistakes_are_reported)
{
    using lynx::linkformula::Formula;
    const auto wrong = [] (const char* text, int expectedAt) {
        std::string error;
        int at = -5;
        const auto f = Formula::compile (text, &error, &at);
        // a formula that does not compile maps 1:1, says why and where
        return f.isDefault() && ! error.empty() && at >= 0 && (expectedAt < 0 || at == expectedAt) && f.map (0.3f) == 0.3f;
    };
    CHECK (wrong ("Input*", -1));
    CHECK (wrong ("Min(Input*2,", -1));
    CHECK (wrong ("Min(Input)", 0));       // two values expected
    CHECK (wrong ("Tension(Input,1,2)", 0));
    CHECK (wrong ("Foo(Input)", 0));       // unknown name
    CHECK (wrong ("Inputs", 0));
    CHECK (wrong ("Input 2", 6));
    CHECK (wrong ("(Input", -1));
    CHECK (wrong ("Input)", -1));          // (FL counts the brackets first: "Unequal brackets")
    CHECK (wrong ("1 + # 2", 4));
    std::string deep (5000, '(');
    CHECK (wrong (deep.c_str(), -1));      // (no crash on absurd nesting)
}

TEST (link190bt_smoothing_slider_reads_as_fl)
{
    using namespace lynx::linkformula;
    // the hint's time: (11 ^ (|position| / 1024) - 1) * 100 ms; negative positions 1 pole, positive ones 3 poles
    CHECK (smoothingMs (0) == 0.0);
    CHECK (std::abs (smoothingMs (1024) - 1000.0) < 1.0e-6 && std::abs (smoothingMs (-1024) - 1000.0) < 1.0e-6);
    CHECK (std::abs (smoothingMs (smoothingDefault) - 200.0) < 1.0); // FL's default 469: about 200 ms
    CHECK (smoothingPoles (smoothingDefault) == 3 && smoothingPoles (-300) == 1);

    // the filter: starts where the control is, walks to the target, arrives
    for (int position : { 469, -469 })
    {
        Smoother s;
        CHECK (s.step (0.2f, 0.0, position) == 0.2f); // (the first call sets where it starts)
        float last = 0.2f;
        bool rising = true;
        for (int i = 0; i < 400 && ! s.settled (1.0f); ++i)
        {
            const auto v = s.step (1.0f, 33.3, position);
            rising = rising && v >= last && v <= 1.0f;
            last = v;
        }
        CHECK (rising && s.settled (1.0f) && last > 0.999f);
        Smoother fast, slow; // more time = slower
        fast.step (0.0f, 0.0, 100);
        slow.step (0.0f, 0.0, 900);
        CHECK (fast.step (1.0f, 33.3, 100) > slow.step (1.0f, 33.3, 900));
    }
}

TEST (link190bt_link_settings_and_conflicts)
{
    using namespace lynx;
    const AutomationTarget volume { AutomationTargetKind::channelVolume, 2 }, pan { AutomationTargetKind::channelPan, 2 },
        insert { AutomationTargetKind::mixerVolume, 4 }, slot { AutomationTargetKind::effectParam, 3, 1, 5 };
    // a learned link has no settings; FL's defaults
    MidiLink learned { 7, 0, volume };
    CHECK (! learned.hasSettings() && learned.port == -1 && learned.formula.empty() && ! learned.smooth && learned.smoothing == 469 && ! learned.omni);
    auto withPort = learned;
    withPort.port = 0;
    CHECK (withPort.hasSettings() && ! sameController (learned, withPort)); // the port is part of the controller's identity
    CHECK (sameController (learned, MidiLink { 7, 0, pan }) && ! sameController (learned, MidiLink { 7, 1, pan }));

    std::vector<MidiLink> links { learned, { 10, 0, pan }, { 7, 0, insert } };
    // conflicts: other controls on the same controller
    CHECK (countLinkConflicts (links, MidiLink { 7, 0, pan }) == 2 && countLinkConflicts (links, MidiLink { 7, 0, volume }) == 1);
    CHECK (countLinkConflicts (links, MidiLink { 99, 0, pan }) == 0 && countLinkConflicts (links, MidiLink { -1, 0, pan }) == 0);

    // Accept without Remove conflicts: the control's links change, the others stay
    MidiLink edited { 7, 0, pan };
    edited.formula = "1-Input";
    auto kept = links;
    CHECK (setTargetLinks (kept, pan, { edited }, false) == 0);
    CHECK (kept.size() == 3 && kept[0] == learned && kept[1] == edited && kept[2].target == insert);
    // with Remove conflicts: the other controls' links on that controller go
    auto cleaned = links;
    CHECK (setTargetLinks (cleaned, pan, { edited }, true) == 2);
    CHECK (cleaned.size() == 1 && cleaned[0] == edited);
    // several links on one control ("Link 1", "Link 2"); one with no controller is dropped
    auto several = links;
    MidiLink second { 11, 3, pan }, none { -1, 0, pan };
    setTargetLinks (several, pan, { edited, second, none }, false);
    CHECK (several.size() == 4 && several[1] == edited && several[2] == second);
    // accepting a control with no controller unlinks it
    auto unlinked = links;
    setTargetLinks (unlinked, pan, { none }, true);
    CHECK (unlinked.size() == 2 && unlinked[0] == learned && unlinked[1].target == insert);
    // a control that had no link gets one at the end
    auto added = links;
    setTargetLinks (added, slot, { MidiLink { 30, 0, {} } }, false);
    CHECK (added.size() == 4 && added[3].target == slot && added[3].cc == 30);

    // Omni: a channel's control follows the selected channel, a mixer slot's the selected track; others stay
    CHECK (linkOmniAvailable (AutomationTargetKind::channelVolume) && linkOmniAvailable (AutomationTargetKind::effectParam));
    CHECK (! linkOmniAvailable (AutomationTargetKind::tempo) && linkOmniAvailable (AutomationTargetKind::mixerVolume)); // (190cs: FL live)
    MidiLink omni { 7, 0, volume };
    CHECK (linkOmniTarget (omni, 5, 9) == volume); // (off)
    omni.omni = true;
    CHECK (linkOmniTarget (omni, 5, 9) == (AutomationTarget { AutomationTargetKind::channelVolume, 5 }));
    CHECK (linkOmniTarget (omni, -1, 9) == volume); // (nothing selected)
    MidiLink omniSlot { 7, 0, slot };
    omniSlot.omni = true;
    CHECK (linkOmniTarget (omniSlot, 5, 9) == (AutomationTarget { AutomationTargetKind::effectParam, 9, 1, 5 }));
    MidiLink omniTempo { 7, 0, { AutomationTargetKind::tempo, 0 } };
    omniTempo.omni = true;
    CHECK (linkOmniTarget (omniTempo, 5, 9).index == 0);

    // a project link overrides a global one of the same controller: the port is part of it
    const std::vector<MidiLink> project { withPort };
    const std::vector<MidiLink> global { MidiLink { 7, 0, pan } };
    CHECK (linksWithGlobal (project, global).size() == 2); // (port 0 and no port: two controllers)
    CHECK (linksWithGlobal ({ learned }, global).size() == 1);
}

TEST (link190bt_midi_settings_text_keeps_ports_and_global_link_settings)
{
    using namespace lynx::prefs;
    // settings written by an older version: read as before, with the new fields at FL's defaults
    const std::string old = "velocity=linear\npickup=0\nfootPedal=1\ninput=1|0|-1|My Keys\nlink=7|0|4|2|0|0\nlink=10|-1|5|2|0|0\n";
    const auto p = MidiPrefs::fromText (old);
    CHECK (p.globalLinks.size() == 2 && p.globalLinks[0].cc == 7 && ! p.globalLinks[0].hasSettings() && p.globalLinks[1].channel == -1);
    CHECK (p.input ("My Keys").port == -1);
    const auto plain = p.toText(); // written back: nothing new appears for plain links
    CHECK (plain.find ("linkset=") == std::string::npos && plain.find ("inputport=") == std::string::npos);
    CHECK (plain.find ("link=7|0|4|2|0|0\nlink=10|-1|5|2|0|0\n") != std::string::npos && MidiPrefs::fromText (plain) == p);

    auto q = p;
    auto keys = q.input ("My Keys");
    keys.port = 12;
    q.setInput (keys);
    q.globalLinks[1].port = 12;
    q.globalLinks[1].formula = "Tension(Input,-10)";
    q.globalLinks[1].smooth = true;
    q.globalLinks[1].smoothing = -512;
    const auto text = q.toText();
    CHECK (text.find ("inputport=12|My Keys\n") != std::string::npos);
    CHECK (text.find ("link=10|-1|5|2|0|0\nlinkset=12|1|-512|Tension(Input,-10)\n") != std::string::npos);
    const auto back = MidiPrefs::fromText (text);
    CHECK (back == q && back.input ("My Keys").port == 12 && back.globalLinks[0] == p.globalLinks[0]);
    // an older version reading the new text skips the lines it does not know: the plain links stay
    std::string stripped;
    std::istringstream in (text);
    for (std::string line; std::getline (in, line);)
        if (line.rfind ("linkset=", 0) != 0 && line.rfind ("inputport=", 0) != 0)
            stripped += line + "\n";
    CHECK (stripped.find ("link=7|0|4|2|0|0\nlink=10|-1|5|2|0|0\n") != std::string::npos && MidiPrefs::fromText (stripped) == p);
    // a "linkset" line with no link before it, or after a link that was not kept, is ignored
    CHECK (MidiPrefs::fromText ("linkset=1|1|100|1-Input\n").globalLinks.empty());
    CHECK (MidiPrefs::fromText ("link=7|0|99|0|0|0\nlinkset=1|1|100|1-Input\n").globalLinks.empty());
}
