// Round 168p: the channel pages of FL's drum and physical-model generators (Fruit Kick, Plucked!, Drumpad, Drumaxx,
// BassDrum, Fruity DrumSynth Live, Sakura, FL Slayer), included by PluginLayout.cpp inside its anonymous namespace.
//
// Positions are FL's own control places read from each plugin's editor form (the binary DFM of TSynthEditorForm in
// the plugin's DLL: Left/Top/Width/Height of the control whose Tag is the parameter index; docs/analise/
// REA_rodada168p.md §1), so FL users find every control where they expect it. Sections, captions and the drawing are
// LYNX's own (FL draws its captions into bitmaps; nothing of FL's artwork is used). Value items name the knobs of
// ChannelSettings' model panel (src/ui/ChannelSettingsDrums.inc); values are 0..1 over each knob's range.

// --- option lists -------------------------------------------------------------------------------------------------

// Drumpad / Drumaxx velocity morph slots: value t = (target parameter − 1), 0 = none (REA_rodada28 §2.1)
constexpr const char* velocityTargets = "(none)|Mallet amp|Mallet decay|Mallet noise|Noise RP|Membrane decay|Membrane cutoff|"
                                        "Membrane tension|Membrane phase|Membrane material|Membrane size|Membrane shape|Low freq|Low res|"
                                        "SOF freq|SOF decay|SOF level|Mid freq|Mid level|Mid res|Mid freq mod|Mid level mod|Mid attack|"
                                        "High freq|High level|High res|Lo-fi|Hold|Pitch";
// Sakura's modulation matrix (the plugin form's own lists)
constexpr const char* sakuraSources = "(none)|Envelope|LFO|Mod wheel|Pitch wheel|Aftertouch|Velocity|Key|Release";
constexpr const char* sakuraTargets = "LFO amount|Level|Pan|Pitch|Lo-cut cutoff|Hi-cut cutoff|Hi-cut attack|Hi-cut decay|"
                                      "String 1 decay|String 2 decay|Strings decay|String 1 damp|String 2 damp|Strings damp|Noise rate";
// DrumSynth Live's voice modes (TRIG digit wheel 0…5: the front-end's mute groups, REA_rodada30 §1.3)
constexpr const char* dslVoiceModes = "Poly|Mono|Group 1|Group 2|Group 3|Group 4";
// FL Slayer's playing aids and effects (the form's own lists)
constexpr const char* slayerModes = "None|Auto chords|Power chords|Strumming|Solo fixed|Solo dynamic";
constexpr const char* slayerEffects = "Dry|Amp: Phaser|Amp: Tremolo|Amp: Wah-wah|Amp: Wah-wah LFO|Amp: Ring mod|Amp: Fuzz box|"
                                      "Amp: Harmonizer|Amp: Talk box|Master: Chorus|Master: Flanger|Master: Phaser|Master: Leslie|"
                                      "Master: Tremolo|Master: Dub delay|Master: Multi tap";

Layout fruitKick()
{
    // the pitch sweep (MAX, MIN, DECAY) in one bevel, then AMP DECAY, CLICK and DIST in three
    Layout l;
    l.fl = "Fruit Kick";
    l.width = 380;
    l.height = 96;
    auto& v = l.items;
    v.push_back (section (5, 5, 178, 70, TR_NOOP ("Pitch"), boxed));
    v.push_back (section (188, 5, 59, 70, nullptr, boxed));
    v.push_back (section (252, 5, 59, 70, nullptr, boxed));
    v.push_back (section (316, 5, 59, 70, nullptr, boxed));
    v.push_back (knob (37, 33, 40, "Max freq", TR_NOOP ("Max"), 64, bipolar));
    v.push_back (knob (94, 33, 40, "Min freq", TR_NOOP ("Min"), 64, bipolar));
    v.push_back (knob (152, 33, 40, "Freq decay", TR_NOOP ("Decay"), 64));
    v.push_back (knob (218, 32, 40, "Amp decay", TR_NOOP ("Amp decay"), 64));
    v.push_back (knob (281, 32, 40, "Click", TR_NOOP ("Click"), 64));
    v.push_back (knob (345, 32, 40, "Distortion", TR_NOOP ("Dist"), 64));
    v.push_back (label (240, 78, 136, 16, "Fruit Kick", dim));
    return l;
}

Layout plucked()
{
    // two big wheels (DECAY, COLOR) and the NORM / GATE / WIDEN pads on the right
    Layout l;
    l.fl = "Plucked!";
    l.width = 360;
    l.height = 156;
    auto& v = l.items;
    v.push_back (section (0, 0, 290, 156, nullptr, boxed));
    v.push_back (label (14, 6, 80, 16, TR_NOOP ("Decay"), alignLeft));
    v.push_back (knob (102, 69, 80, "Decay", nullptr, 0, noCaption | big));
    v.push_back (label (160, 30, 70, 16, TR_NOOP ("Color"), alignLeft));
    v.push_back (knob (222, 89, 80, "Color", nullptr, 0, noCaption | big));
    v.push_back (toggle (301, 5, 55, 25, "Normalize", TR_NOOP ("Norm"), boxed));
    v.push_back (toggle (301, 32, 55, 25, "Gate", TR_NOOP ("Gate"), boxed));
    v.push_back (toggle (301, 127, 55, 25, "Widen", TR_NOOP ("Widen"), boxed));
    v.push_back (label (4, 126, 150, 26, "Plucked!", dim | alignLeft));
    return l;
}

// Drumpad's voice (also Drumaxx's selected pad, moved by dx, dy): MALLET, MEMBRANE, LOW, SOF, MID, HIGH, OUTPUT and
// the four VELOCITY morph slots, at the places of Drumpad's form
void drumpadVoice (std::vector<LayoutItem>& v, float dx, float dy)
{
    const auto sec = [&v, dx, dy] (float x, float w, const char* title, float h = 186) { v.push_back (section (dx + x, dy + 32, w, h, title)); };
    sec (30, 116, TR_NOOP ("Mallet"));
    sec (148, 110, TR_NOOP ("Membrane"));
    sec (260, 54, TR_NOOP ("Low"), 136);
    sec (312, 54, TR_NOOP ("SOF"));
    sec (366, 126, TR_NOOP ("Mid"));
    sec (494, 58, TR_NOOP ("High"));
    sec (556, 60, TR_NOOP ("Output"));
    sec (618, 140, TR_NOOP ("Velocity"));
    v.push_back (toggle (dx + 97, dy + 166, 34, 20, "Mallet retrig", TR_NOOP ("Retrig")));
    v.push_back (knob (dx + 62, dy + 78, 30, "Mallet amp", TR_NOOP ("Amp"), dy + 100));
    v.push_back (knob (dx + 114, dy + 78, 30, "Mallet decay", TR_NOOP ("Decay"), dy + 100));
    v.push_back (stepper (dx + 467, dy + 123, 20, 66, "Mid retrig", TR_NOOP ("Retrig"), 4));
    v.push_back (knob (dx + 175, dy + 77, 32, "Membrane decay", TR_NOOP ("Decay"), dy + 100));
    v.push_back (knob (dx + 62, dy + 128, 30, "Mallet noise", TR_NOOP ("Noise"), dy + 150));
    v.push_back (knob (dx + 114, dy + 128, 30, "Mallet noise RP", TR_NOOP ("Noise RP"), dy + 150));
    v.push_back (knob (dx + 339, dy + 179, 30, "SOF level", TR_NOOP ("Level"), dy + 201));
    v.push_back (menuOf (dx + 625, dy + 68, 88, 20, "Vel mod 1 target", nullptr, velocityTargets));
    v.push_back (menuOf (dx + 625, dy + 102, 88, 20, "Vel mod 2 target", nullptr, velocityTargets));
    v.push_back (knob (dx + 735, dy + 78, 28, "Vel mod 1 value", nullptr, 0, noCaption));
    v.push_back (knob (dx + 735, dy + 112, 28, "Vel mod 2 value", nullptr, 0, noCaption));
    v.push_back (menuOf (dx + 625, dy + 136, 88, 20, "Vel mod 3 target", nullptr, velocityTargets));
    v.push_back (menuOf (dx + 625, dy + 170, 88, 20, "Vel mod 4 target", nullptr, velocityTargets));
    v.push_back (knob (dx + 735, dy + 146, 28, "Vel mod 3 value", nullptr, 0, noCaption));
    v.push_back (knob (dx + 735, dy + 180, 28, "Vel mod 4 value", nullptr, 0, noCaption));
    v.push_back (knob (dx + 230, dy + 77, 32, "Membrane cutoff", TR_NOOP ("Cutoff"), dy + 100));
    v.push_back (knob (dx + 175, dy + 126, 32, "Membrane tension", TR_NOOP ("Tension"), dy + 149));
    v.push_back (knob (dx + 230, dy + 127, 32, "Membrane phase", TR_NOOP ("Phase"), dy + 150));
    v.push_back (knob (dx + 175, dy + 178, 32, "Membrane material", TR_NOOP ("Material"), dy + 201));
    v.push_back (knob (dx + 230, dy + 178, 32, "Membrane size", TR_NOOP ("Size"), dy + 201));
    v.push_back (knob (dx + 287, dy + 178, 32, "Membrane shape", TR_NOOP ("Shape"), dy + 201));
    v.push_back (knob (dx + 287, dy + 78, 30, "Low freq", TR_NOOP ("Freq"), dy + 100));
    v.push_back (knob (dx + 287, dy + 128, 30, "Low res", TR_NOOP ("Res"), dy + 150));
    v.push_back (knob (dx + 339, dy + 78, 30, "SOF freq", TR_NOOP ("Freq"), dy + 100));
    v.push_back (knob (dx + 339, dy + 128, 30, "SOF decay", TR_NOOP ("Decay"), dy + 150));
    v.push_back (knob (dx + 391, dy + 179, 30, "Mid res", TR_NOOP ("Res"), dy + 201));
    v.push_back (knob (dx + 391, dy + 78, 30, "Mid freq", TR_NOOP ("Freq"), dy + 100));
    v.push_back (knob (dx + 391, dy + 128, 30, "Mid level", TR_NOOP ("Level"), dy + 150, bipolar));
    v.push_back (knob (dx + 442, dy + 179, 30, "Mid attack", TR_NOOP ("Attack"), dy + 201));
    v.push_back (knob (dx + 442, dy + 78, 30, "Mid freq mod", TR_NOOP ("Freq mod"), dy + 100, bipolar));
    v.push_back (knob (dx + 442, dy + 128, 30, "Mid level mod", TR_NOOP ("Level mod"), dy + 150, bipolar));
    v.push_back (knob (dx + 531, dy + 179, 30, "High res", TR_NOOP ("Res"), dy + 201));
    v.push_back (knob (dx + 531, dy + 78, 30, "High freq", TR_NOOP ("Freq"), dy + 100));
    v.push_back (knob (dx + 531, dy + 128, 30, "High level", TR_NOOP ("Level"), dy + 150, bipolar));
    v.push_back (knob (dx + 586, dy + 77, 32, "Hold", TR_NOOP ("Hold"), dy + 100));
    v.push_back (knob (dx + 586, dy + 127, 32, "Lo-fi", TR_NOOP ("Lo-fi"), dy + 150));
    v.push_back (knob (dx + 587, dy + 178, 32, "Pitch", TR_NOOP ("Pitch"), dy + 201, bipolar));
    v.push_back (label (dx + 266, dy + 168, 44, 12, TR_NOOP ("Shape"), dim)); // (the membrane's last knob sits under LOW)
}

Layout drumpad()
{
    Layout l;
    l.fl = "Drumpad";
    l.width = 794;
    l.height = 225;
    drumpadVoice (l.items, 0, 0);
    l.items.push_back (label (30, 2, 200, 26, "Drumpad", dim | alignLeft));
    return l;
}

Layout drumaxx()
{
    // the 16 pads in two banks of eight (1-8 / 9-16, the page tabs), each with its volume, pan, mute and solo and a
    // play pad that also selects it; the selected pad's voice (Drumpad's controls) below; the master EQ and limiter
    // at the bottom. Drumaxx's pattern sequencer is not played by LYNX (REA_rodada67 §9).
    Layout l;
    l.fl = "Drumaxx";
    l.width = 762;
    l.height = 614;
    l.pages = { TR_NOOP ("Pads 1-8"), TR_NOOP ("Pads 9-16") };
    auto& v = l.items;
    v.push_back (label (30, 8, 220, 30, "Drumaxx", dim | alignLeft));
    v.push_back (display (260, 14, 330, 22, "drumaxxkit"));
    v.push_back (knob (717, 34, 32, "Master", TR_NOOP ("Master"), 58));
    v.push_back (pageTabs (611, 79, 115, 52, "1-8|9-16"));
    v.push_back (section (24, 52, 590, 240, nullptr, boxed));
    static const char* const names[16][4] = {
        { "Pad 1 volume", "Pad 1 pan", "Pad 1 mute", "Pad 1 solo" }, { "Pad 2 volume", "Pad 2 pan", "Pad 2 mute", "Pad 2 solo" },
        { "Pad 3 volume", "Pad 3 pan", "Pad 3 mute", "Pad 3 solo" }, { "Pad 4 volume", "Pad 4 pan", "Pad 4 mute", "Pad 4 solo" },
        { "Pad 5 volume", "Pad 5 pan", "Pad 5 mute", "Pad 5 solo" }, { "Pad 6 volume", "Pad 6 pan", "Pad 6 mute", "Pad 6 solo" },
        { "Pad 7 volume", "Pad 7 pan", "Pad 7 mute", "Pad 7 solo" }, { "Pad 8 volume", "Pad 8 pan", "Pad 8 mute", "Pad 8 solo" },
        { "Pad 9 volume", "Pad 9 pan", "Pad 9 mute", "Pad 9 solo" }, { "Pad 10 volume", "Pad 10 pan", "Pad 10 mute", "Pad 10 solo" },
        { "Pad 11 volume", "Pad 11 pan", "Pad 11 mute", "Pad 11 solo" }, { "Pad 12 volume", "Pad 12 pan", "Pad 12 mute", "Pad 12 solo" },
        { "Pad 13 volume", "Pad 13 pan", "Pad 13 mute", "Pad 13 solo" }, { "Pad 14 volume", "Pad 14 pan", "Pad 14 mute", "Pad 14 solo" },
        { "Pad 15 volume", "Pad 15 pan", "Pad 15 mute", "Pad 15 solo" }, { "Pad 16 volume", "Pad 16 pan", "Pad 16 mute", "Pad 16 solo" } };
    static const float columns[4] = { 35, 177, 318, 461 };
    for (int pad = 0; pad < 16; ++pad)
    {
        const auto page = pad / 8, slot = pad % 8;
        const auto x = columns[slot % 4], y = 57.0f + 109.0f * (float) (slot / 4);
        v.push_back (onPage (section (x - 4, y, 138, 104, nullptr, boxed | raised), page));
        auto pad_ = display (x + 48, y + 26, 45, 45, "drumaxxpad");
        pad_.steps = pad; // (the pad this play button selects)
        v.push_back (onPage (pad_, page));
        auto name = display (x + 2, y + 2, 96, 15, "drumaxxpadname");
        name.steps = pad;
        v.push_back (onPage (name, page));
        v.push_back (onPage (knob (x + 24, y + 40, 32, names[pad][0], nullptr, 0, noCaption), page));
        v.push_back (onPage (knob (x + 24, y + 78, 26, names[pad][1], nullptr, 0, noCaption | bipolar), page));
        v.push_back (onPage (toggle (x + 102, y + 29, 26, 18, names[pad][2], TR_NOOP ("M"), boxed | inverted), page));
        v.push_back (onPage (toggle (x + 102, y + 52, 26, 18, names[pad][3], TR_NOOP ("S"), boxed), page));
    }
    drumpadVoice (v, -16, 255);
    v.push_back (section (14, 456, 744, 88, TR_NOOP ("Sequencer")));
    v.push_back (label (24, 486, 724, 40, TR_NOOP ("Drumaxx's pattern sequencer is not played by LYNX: play the pads from the Piano roll or the Channel rack"), dim));
    v.push_back (section (14, 548, 468, 64, TR_NOOP ("Equalizer")));
    v.push_back (section (488, 548, 270, 64, TR_NOOP ("Master")));
    v.push_back (knob (52, 576, 32, "EQ band 1", TR_NOOP ("63"), 599, bipolar));
    v.push_back (knob (107, 576, 32, "EQ band 2", TR_NOOP ("125"), 599, bipolar));
    v.push_back (knob (164, 576, 32, "EQ band 3", TR_NOOP ("250"), 599, bipolar));
    v.push_back (knob (219, 576, 32, "EQ band 4", TR_NOOP ("500"), 599, bipolar));
    v.push_back (knob (274, 576, 32, "EQ band 5", TR_NOOP ("1k"), 599, bipolar));
    v.push_back (knob (331, 576, 32, "EQ band 6", TR_NOOP ("2k"), 599, bipolar));
    v.push_back (knob (394, 576, 32, "EQ band 7", TR_NOOP ("4k"), 599, bipolar));
    v.push_back (knob (451, 576, 32, "EQ band 8", TR_NOOP ("8k"), 599, bipolar));
    v.push_back (knob (652, 576, 32, "Limiter gain", TR_NOOP ("Gain"), 599));
    v.push_back (knob (709, 576, 32, "Limiter release", TR_NOOP ("Release"), 599));
    v.push_back (toggle (495, 565, 34, 22, "EQ on", TR_NOOP ("EQ")));
    v.push_back (toggle (572, 565, 34, 22, "Limiter on", TR_NOOP ("Limiter")));
    return l;
}

Layout bassDrum()
{
    // the body of the drum: DURATION, OUTPUT and SAMPLING at the top, the pitch sweep (BASE, PEAK, SLIDE) down the left
    // with its VARIATION knobs, DRIVE, SUB BASS and ATTACK down the right with the CLICK, the noise layer in the middle
    Layout l;
    l.fl = "BassDrum";
    l.width = 389;
    l.height = 420;
    auto& v = l.items;
    v.push_back (section (0, 0, 389, 420, nullptr, boxed));
    v.push_back (label (8, 392, 160, 24, "BassDrum", dim | alignLeft));
    v.push_back (section (118, 196, 154, 122, TR_NOOP ("Noise")));
    v.push_back (display (126, 218, 138, 18, "bassdrumnoise"));
    v.push_back (knob (59, 70, 46, "Duration", TR_NOOP ("Duration"), 100));
    v.push_back (knob (296, 125, 36, "Sub bass", TR_NOOP ("Sub bass"), 150));
    v.push_back (knob (251, 93, 36, "Drive", TR_NOOP ("Drive"), 118));
    v.push_back (knob (141, 93, 36, "Base", TR_NOOP ("Base"), 118));
    v.push_back (knob (327, 172, 36, "Attack", TR_NOOP ("Attack"), 197));
    v.push_back (knob (95, 125, 36, "Peak", TR_NOOP ("Peak"), 150));
    v.push_back (knob (64, 172, 36, "Slide", TR_NOOP ("Slide"), 197));
    v.push_back (knob (55, 230, 36, "Var base", TR_NOOP ("Var base"), 255));
    v.push_back (knob (330, 70, 46, "Sampling", TR_NOOP ("Sampling"), 100));
    v.push_back (knob (244, 256, 24, "Noise start", TR_NOOP ("Start"), 275));
    v.push_back (knob (172, 256, 24, "Noise D", TR_NOOP ("D"), 275));
    v.push_back (knob (204, 256, 24, "Noise S", TR_NOOP ("S"), 275));
    v.push_back (knob (68, 281, 36, "Var peak", TR_NOOP ("Var peak"), 306));
    v.push_back (knob (98, 327, 36, "Var slide", TR_NOOP ("Var slide"), 352));
    v.push_back (knob (144, 355, 36, "Var phase", TR_NOOP ("Var phase"), 380));
    v.push_back (knob (324, 280, 36, "Click level", TR_NOOP ("Click"), 305));
    v.push_back (knob (248, 355, 36, "Click freq", TR_NOOP ("Freq"), 380));
    v.push_back (knob (293, 326, 36, "Click cut", TR_NOOP ("Cut"), 351));
    v.push_back (knob (234, 290, 24, "Noise mix", TR_NOOP ("Mix"), 309));
    v.push_back (knob (156, 290, 24, "Noise pitch", TR_NOOP ("Pitch"), 309));
    v.push_back (knob (195, 290, 24, "Noise filter", TR_NOOP ("Filter"), 309));
    v.push_back (knob (140, 256, 24, "Noise A", TR_NOOP ("A"), 275));
    v.push_back (knob (195, 70, 46, "Output", TR_NOOP ("Output"), 100));
    return l;
}

Layout drumSynthLive()
{
    // WAVE and TRIG on the left, OSC 1 and OSC 2 in the middle, MAIN OUT / SEND TO / SELECTED on the right and the
    // patch keyboard with MORPH TO at the bottom; the knobs edit the patch of the key selected on that keyboard
    Layout l;
    l.fl = "Fruity DrumSynth Live";
    l.width = 580;
    l.height = 358;
    auto& v = l.items;
    v.push_back (section (5, 5, 140, 100, TR_NOOP ("Wave")));
    v.push_back (section (5, 109, 140, 151, TR_NOOP ("Trig")));
    v.push_back (section (149, 5, 316, 128, TR_NOOP ("Osc 1")));
    v.push_back (section (149, 136, 316, 124, TR_NOOP ("Osc 2")));
    v.push_back (section (469, 5, 106, 255, TR_NOOP ("Main out")));
    v.push_back (section (5, 264, 570, 90, nullptr, boxed));
    v.push_back (label (474, 96, 96, 13, TR_NOOP ("Send to"), dim));
    v.push_back (label (474, 176, 96, 13, TR_NOOP ("Selected"), dim));
    v.push_back (display (473, 195, 97, 22, "dslpatch"));
    v.push_back (display (480, 126, 34, 22, "dslsend"));
    v.push_back (label (474, 161, 44, 9, TR_NOOP ("Num"), dim));
    v.push_back (label (139, 266, 96, 13, TR_NOOP ("Morph to"), dim));
    v.push_back (display (132, 282, 104, 20, "dslmorph"));
    v.push_back (display (262, 269, 309, 81, "dslkeys"));
    v.push_back (label (10, 310, 120, 40, "DrumSynth Live", dim | alignLeft));
    v.push_back (knob (550, 57, 40, "Level", TR_NOOP ("Vol"), 84));
    v.push_back (knob (28, 226, 36, "Vel > tone", TR_NOOP ("Vel"), 251, bipolar));
    v.push_back (knob (173, 43, 36, "Vel > osc1 freq", TR_NOOP ("Vel"), 68, bipolar));
    v.push_back (knob (307, 98, 38, "Osc1 sweep", TR_NOOP ("Sweep"), 124));
    v.push_back (knob (262, 225, 38, "Osc2 decay", TR_NOOP ("Dec"), 251));
    v.push_back (knob (173, 169, 36, "Vel > osc2 freq", TR_NOOP ("Vel"), 194, bipolar));
    v.push_back (knob (262, 98, 38, "Osc1 decay", TR_NOOP ("Dec"), 124));
    v.push_back (knob (395, 226, 36, "Vel > osc2 level", TR_NOOP ("Vel"), 251, bipolar));
    v.push_back (knob (75, 169, 38, "Retrig rate", TR_NOOP ("Rate"), 195));
    v.push_back (knob (395, 99, 36, "Vel > osc1 level", TR_NOOP ("Vel"), 124, bipolar));
    v.push_back (knob (173, 99, 36, "Vel > osc1 harm", TR_NOOP ("Vel"), 124, bipolar));
    v.push_back (knob (121, 70, 38, "Wave mix", TR_NOOP ("Mix"), 96));
    v.push_back (knob (75, 71, 38, "Wave tune", TR_NOOP ("Tune"), 97, bipolar));
    v.push_back (knob (28, 71, 36, "Vel > wave tune", TR_NOOP ("Vel"), 96, bipolar));
    v.push_back (knob (217, 98, 38, "Osc1 harmonics", TR_NOOP ("Harm"), 124, bipolar));
    v.push_back (knob (121, 169, 38, "Retrig decay", TR_NOOP ("Dec"), 195));
    v.push_back (knob (28, 169, 38, "Retrig count", TR_NOOP ("Count"), 195));
    v.push_back (knob (441, 98, 38, "Osc1 level", TR_NOOP ("Mix"), 124));
    v.push_back (knob (495, 57, 40, "Pan", TR_NOOP ("Pan"), 84, bipolar));
    v.push_back (knob (351, 225, 38, "Release", TR_NOOP ("Rel"), 251));
    v.push_back (knob (441, 225, 38, "Osc2 level", TR_NOOP ("Mix"), 251));
    v.push_back (knob (307, 226, 38, "Osc2 shape", TR_NOOP ("Shape"), 252));
    v.push_back (knob (217, 225, 38, "Osc2 attack", TR_NOOP ("Att"), 251));
    v.push_back (knob (173, 225, 38, "Ring mod", TR_NOOP ("Ring"), 251));
    v.push_back (knob (121, 225, 38, "Click", TR_NOOP ("Click"), 251));
    v.push_back (knob (75, 225, 38, "Tone", TR_NOOP ("Tone"), 251, bipolar));
    v.push_back (toggle (335, 80, 33, 12, "Osc1 noise", TR_NOOP ("Noise")));
    v.push_back (menuOf (10, 127, 132, 18, "Voice mode", nullptr, dslVoiceModes));
    v.push_back (toggle (335, 105, 33, 12, "Osc1 invert", TR_NOOP ("Invert")));
    v.push_back (stepper (10, 24, 132, 18, "Waveform", nullptr, 19));
    v.push_back (knob (550, 139, 40, "Send level", TR_NOOP ("Lvl"), 166));
    v.push_back (slider (Item::vslider, 242, 269, 15, 80, "Morph amount", nullptr, noCaption));
    v.push_back (slider (Item::hslider, 198, 45, 259, 14, "Osc1 width", nullptr, noCaption));
    v.push_back (slider (Item::hslider, 198, 153, 259, 14, "Osc2 freq", nullptr, noCaption));
    v.push_back (slider (Item::hslider, 198, 173, 259, 14, "Osc2 width", nullptr, noCaption));
    v.push_back (slider (Item::hslider, 198, 25, 259, 14, "Osc1 freq", nullptr, noCaption));
    return l;
}

Layout sakura()
{
    // exciter and filters on the left, the two strings with the comb between them, string mix / stereo / saturation,
    // the amp envelope, the eight resonators on the right; the modulation EG, LFO and matrix below; the delay, chorus
    // and reverb at the bottom
    Layout l;
    l.fl = "Sakura";
    l.width = 855;
    l.height = 532;
    auto& v = l.items;
    v.push_back (label (12, 10, 200, 40, "Sakura", dim | alignLeft));
    v.push_back (section (560, 6, 290, 70, nullptr, boxed));
    v.push_back (knob (713, 34, 36, "Master", TR_NOOP ("Master"), 64));
    v.push_back (section (6, 92, 66, 254, TR_NOOP ("Exciter")));
    v.push_back (section (76, 92, 126, 254, TR_NOOP ("Hi-cut")));
    v.push_back (label (80, 279, 118, 10, TR_NOOP ("Lo-cut"), dim));
    v.push_back (section (206, 92, 278, 122, TR_NOOP ("String 1")));
    v.push_back (section (206, 224, 278, 122, TR_NOOP ("String 2")));
    v.push_back (section (488, 92, 64, 254, TR_NOOP ("Comb")));
    v.push_back (section (556, 92, 62, 254, TR_NOOP ("Strings")));
    v.push_back (section (622, 92, 58, 254, TR_NOOP ("Amp")));
    v.push_back (section (686, 92, 164, 254, TR_NOOP ("Resonators")));
    v.push_back (section (6, 352, 844, 96, TR_NOOP ("Modulation")));
    v.push_back (section (6, 452, 434, 76, TR_NOOP ("Delay")));
    v.push_back (section (444, 452, 204, 76, TR_NOOP ("Chorus")));
    v.push_back (section (652, 452, 198, 76, TR_NOOP ("Reverb")));
    v.push_back (label (12, 270, 54, 10, TR_NOOP ("Noise"), dim));
    v.push_back (label (560, 268, 54, 10, TR_NOOP ("Stereo"), dim));
    v.push_back (label (369, 196, 106, 10, TR_NOOP ("Comb ratio"), dim));
    v.push_back (knob (648.5, 128.5, 45, "Amp attack", TR_NOOP ("Attack"), 158));
    v.push_back (knob (588.5, 128.5, 45, "String mix", TR_NOOP ("Mix"), 158, bipolar));
    v.push_back (knob (168.5, 128.5, 45, "Hi-cut attack", TR_NOOP ("Attack"), 158));
    v.push_back (knob (648.5, 188.5, 45, "Amp decay", TR_NOOP ("Decay"), 218));
    v.push_back (knob (108.5, 128.5, 45, "Hi-cut", TR_NOOP ("Cutoff"), 158));
    v.push_back (knob (108.5, 188.5, 45, "Hi-cut res", TR_NOOP ("Res"), 218));
    v.push_back (knob (168.5, 188.5, 45, "Hi-cut decay", TR_NOOP ("Decay"), 218));
    v.push_back (knob (713, 148, 20, "Res 2 feedback", nullptr, 0, noCaption));
    v.push_back (knob (713, 176, 20, "Res 3 feedback", nullptr, 0, noCaption));
    v.push_back (knob (713, 204, 20, "Res 4 feedback", nullptr, 0, noCaption));
    v.push_back (knob (108.5, 248.5, 45, "Hi-cut drive", TR_NOOP ("Drive"), 278));
    v.push_back (knob (168.5, 248.5, 45, "Hi-cut sustain", TR_NOOP ("Sustain"), 278));
    v.push_back (knob (108.5, 308.5, 45, "Lo-cut", TR_NOOP ("Cutoff"), 338));
    v.push_back (knob (648.5, 248.5, 45, "Amp sustain", TR_NOOP ("Sustain"), 278));
    v.push_back (knob (168.5, 308.5, 45, "Lo-cut res", TR_NOOP ("Res"), 338));
    v.push_back (knob (648.5, 308.5, 45, "Amp release", TR_NOOP ("Release"), 338));
    v.push_back (knob (713, 260, 20, "Res 6 feedback", nullptr, 0, noCaption));
    v.push_back (knob (713, 234, 20, "Res 5 feedback", nullptr, 0, noCaption));
    v.push_back (knob (713, 288, 20, "Res 7 feedback", nullptr, 0, noCaption));
    v.push_back (knob (713, 120, 20, "Res 1 feedback", nullptr, 0, noCaption));
    v.push_back (knob (588.5, 308.5, 45, "Saturation", TR_NOOP ("Saturate"), 338));
    v.push_back (knob (713, 316, 20, "Res 8 feedback", nullptr, 0, noCaption));
    v.push_back (slider (Item::hslider, 739, 110, 87, 20, "Res 1 freq", nullptr, noCaption));
    v.push_back (slider (Item::hslider, 739, 138, 87, 20, "Res 2 freq", nullptr, noCaption));
    v.push_back (slider (Item::hslider, 739, 166, 87, 20, "Res 3 freq", nullptr, noCaption));
    v.push_back (slider (Item::hslider, 739, 194, 87, 20, "Res 4 freq", nullptr, noCaption));
    v.push_back (slider (Item::hslider, 739, 222, 87, 20, "Res 5 freq", nullptr, noCaption));
    v.push_back (slider (Item::hslider, 739, 250, 87, 20, "Res 6 freq", nullptr, noCaption));
    v.push_back (slider (Item::hslider, 739, 278, 87, 20, "Res 7 freq", nullptr, noCaption));
    v.push_back (slider (Item::hslider, 739, 306, 87, 20, "Res 8 freq", nullptr, noCaption));
    v.push_back (slider (Item::vslider, 18, 173, 27, 88, "Noise level", TR_NOOP ("Noise"), noCaption));
    v.push_back (slider (Item::vslider, 575, 177, 27, 86, "Stereo sep", TR_NOOP ("Stereo"), noCaption));
    v.push_back (knob (32.5, 404.5, 33, "EG attack", TR_NOOP ("A"), 428));
    v.push_back (knob (72.5, 404.5, 33, "EG decay", TR_NOOP ("D"), 428));
    v.push_back (knob (112.5, 404.5, 33, "EG sustain", TR_NOOP ("S"), 428));
    v.push_back (knob (152.5, 404.5, 33, "EG release", TR_NOOP ("R"), 428));
    v.push_back (knob (205.5, 404.5, 33, "LFO rate", TR_NOOP ("LFO"), 428));
    v.push_back (knob (254.5, 404.5, 33, "Mod 1 amount", nullptr, 0, noCaption));
    v.push_back (knob (330.5, 404.5, 33, "Mod 2 amount", nullptr, 0, noCaption));
    v.push_back (toggle (276, 397, 22, 14, "Mod 1 negative", nullptr));
    v.push_back (knob (404.5, 404.5, 33, "Mod 3 amount", nullptr, 0, noCaption));
    v.push_back (toggle (353, 397, 22, 14, "Mod 2 negative", nullptr));
    v.push_back (knob (480.5, 404.5, 33, "Mod 4 amount", nullptr, 0, noCaption));
    v.push_back (toggle (426, 397, 22, 14, "Mod 3 negative", nullptr));
    v.push_back (knob (555.5, 404.5, 33, "Mod 5 amount", nullptr, 0, noCaption));
    v.push_back (toggle (501, 397, 22, 14, "Mod 4 negative", nullptr));
    v.push_back (knob (629.5, 404.5, 33, "Mod 6 amount", nullptr, 0, noCaption));
    v.push_back (toggle (575, 397, 22, 14, "Mod 5 negative", nullptr));
    v.push_back (knob (704.5, 404.5, 33, "Mod 7 amount", nullptr, 0, noCaption));
    v.push_back (toggle (652, 397, 22, 14, "Mod 6 negative", nullptr));
    v.push_back (toggle (725, 397, 22, 14, "Mod 7 negative", nullptr));
    v.push_back (knob (48.5, 487.5, 47, "Delay time", TR_NOOP ("Time"), 518));
    v.push_back (stepper (89, 485, 33, 17, "Delay sync", TR_NOOP ("Sync"), 10));
    v.push_back (knob (227.5, 487.5, 47, "Delay speed", TR_NOOP ("Speed"), 518));
    v.push_back (knob (167.5, 487.5, 47, "Delay feedback", TR_NOOP ("Feedback"), 518));
    v.push_back (knob (347.5, 487.5, 47, "Delay blur", TR_NOOP ("Blur"), 518));
    v.push_back (knob (287.5, 487.5, 47, "Delay depth", TR_NOOP ("Depth"), 518));
    v.push_back (knob (487.5, 487.5, 47, "Chorus depth", TR_NOOP ("Depth"), 518));
    v.push_back (knob (407.5, 487.5, 47, "Delay wet", TR_NOOP ("Wet"), 518));
    v.push_back (knob (608.5, 487.5, 47, "Chorus wet", TR_NOOP ("Wet"), 518));
    v.push_back (knob (547.5, 487.5, 47, "Chorus rate", TR_NOOP ("Rate"), 518));
    v.push_back (knob (686.5, 487.5, 47, "Reverb decay", TR_NOOP ("Decay"), 518));
    v.push_back (knob (808.5, 487.5, 47, "Reverb mix", TR_NOOP ("Mix"), 518));
    v.push_back (knob (747.5, 487.5, 47, "Reverb color", TR_NOOP ("Color"), 518));
    v.push_back (toggle (427, 508, 18, 18, "Delay on", TR_NOOP ("Delay")));
    v.push_back (toggle (627, 508, 18, 18, "Chorus on", TR_NOOP ("Chorus")));
    v.push_back (toggle (827, 508, 18, 18, "Reverb on", TR_NOOP ("Reverb")));
    v.push_back (toggle (830, 111, 18, 18, "Res 1 on", nullptr));
    v.push_back (toggle (830, 139, 18, 18, "Res 2 on", nullptr));
    v.push_back (toggle (830, 167, 18, 18, "Res 3 on", nullptr));
    v.push_back (toggle (830, 195, 18, 18, "Res 4 on", nullptr));
    v.push_back (toggle (830, 223, 18, 18, "Res 5 on", nullptr));
    v.push_back (toggle (830, 251, 18, 18, "Res 6 on", nullptr));
    v.push_back (toggle (830, 279, 18, 18, "Res 7 on", nullptr));
    v.push_back (toggle (830, 307, 18, 18, "Res 8 on", nullptr));
    v.push_back (knob (243, 308, 38, "Str2 decay", TR_NOOP ("Decay"), 334));
    v.push_back (knob (243, 128, 38, "Str1 decay", TR_NOOP ("Decay"), 154));
    v.push_back (knob (363, 133, 28, "Str1 damp sharp", TR_NOOP ("Sharp"), 154));
    v.push_back (knob (452, 124, 28, "Str1 damp offset", TR_NOOP ("Offset"), 145));
    v.push_back (knob (363, 302, 28, "Str2 damp sharp", TR_NOOP ("Sharp"), 323));
    v.push_back (knob (452, 309, 28, "Str2 damp offset", TR_NOOP ("Offset"), 330));
    v.push_back (knob (32, 313, 26, "Noise rate", TR_NOOP ("Rate"), 333));
    v.push_back (knob (588, 36, 28, "Transpose", TR_NOOP ("Octave"), 56, bipolar));
    v.push_back (knob (648, 36, 28, "Tune", TR_NOOP ("Tune"), 56, bipolar));
    v.push_back (knob (513, 308, 38, "Comb feedback", TR_NOOP ("Feedback"), 334));
    v.push_back (knob (513, 128, 38, "Comb level", TR_NOOP ("Level"), 154));
    v.push_back (toggle (228, 167, 30, 15, "Str1 decay inv", TR_NOOP ("Inv")));
    v.push_back (toggle (228, 250, 30, 15, "Str2 decay inv", TR_NOOP ("Inv")));
    v.push_back (toggle (494, 167, 30, 15, "Comb level inv", TR_NOOP ("Inv")));
    v.push_back (toggle (494, 250, 30, 15, "Comb feedback inv", TR_NOOP ("Inv")));
    v.push_back (stepper (226, 215, 33, 17, "Str2 ratio", TR_NOOP ("Ratio"), 7));
    v.push_back (slider (Item::hslider, 369, 209, 106, 20, "Comb ratio", TR_NOOP ("Comb ratio"), noCaption));
    v.push_back (knob (303, 128, 38, "Str1 damp", TR_NOOP ("Damp"), 154));
    v.push_back (knob (303, 308, 38, "Str2 damp", TR_NOOP ("Damp"), 334));
    v.push_back (knob (780.5, 404.5, 33, "Mod 8 amount", nullptr, 0, noCaption));
    v.push_back (toggle (801, 397, 22, 14, "Mod 8 negative", nullptr));
    v.push_back (menuOf (239, 372, 70, 12, "Mod 1 source", nullptr, sakuraSources));
    v.push_back (menuOf (314, 372, 70, 12, "Mod 2 source", nullptr, sakuraSources));
    v.push_back (menuOf (389, 372, 70, 12, "Mod 3 source", nullptr, sakuraSources));
    v.push_back (menuOf (464, 372, 70, 12, "Mod 4 source", nullptr, sakuraSources));
    v.push_back (menuOf (539, 372, 70, 12, "Mod 5 source", nullptr, sakuraSources));
    v.push_back (menuOf (614, 372, 70, 12, "Mod 6 source", nullptr, sakuraSources));
    v.push_back (menuOf (689, 372, 70, 12, "Mod 7 source", nullptr, sakuraSources));
    v.push_back (menuOf (765, 372, 70, 12, "Mod 8 source", nullptr, sakuraSources));
    v.push_back (menuOf (239, 426, 70, 12, "Mod 1 target", nullptr, sakuraTargets));
    v.push_back (menuOf (314, 426, 70, 12, "Mod 2 target", nullptr, sakuraTargets));
    v.push_back (menuOf (389, 426, 70, 12, "Mod 3 target", nullptr, sakuraTargets));
    v.push_back (menuOf (464, 426, 70, 12, "Mod 4 target", nullptr, sakuraTargets));
    v.push_back (menuOf (539, 426, 70, 12, "Mod 5 target", nullptr, sakuraTargets));
    v.push_back (menuOf (614, 426, 70, 12, "Mod 6 target", nullptr, sakuraTargets));
    v.push_back (menuOf (689, 426, 70, 12, "Mod 7 target", nullptr, sakuraTargets));
    v.push_back (menuOf (765, 426, 70, 12, "Mod 8 target", nullptr, sakuraTargets));
    return l;
}

Layout slayer()
{
    // FL Slayer's guitar in LYNX's panels, in the form's order: STRINGS / COILS / PICKUP at the top, the playing
    // controls (TONE, SLAP, FRET, HARMONIC, DAMP, velocity), the AID mode and timing, then AMP, EQ, CABINET and FX
    Layout l;
    l.fl = "FL Slayer";
    l.width = 396;
    l.height = 404;
    auto& v = l.items;
    v.push_back (label (8, 4, 150, 34, "FL Slayer", dim | alignLeft));
    v.push_back (section (160, 4, 232, 46, TR_NOOP ("Aid")));
    v.push_back (menuOf (168, 24, 120, 20, "Mode", nullptr, slayerModes));
    v.push_back (knob (318, 30, 22, "Timing", nullptr, 0, noCaption));
    v.push_back (toggle (344, 22, 44, 22, "Latch", TR_NOOP ("Latch"), boxed));
    v.push_back (section (4, 56, 388, 124, TR_NOOP ("Strings")));
    v.push_back (label (12, 78, 60, 12, TR_NOOP ("Type"), dim));
    v.push_back (stepper (16, 96, 48, 30, "Strings", nullptr, 7));
    v.push_back (label (80, 78, 60, 12, TR_NOOP ("Coils"), dim));
    v.push_back (stepper (86, 96, 48, 30, "Coils", nullptr, 3));
    v.push_back (label (150, 78, 236, 12, TR_NOOP ("Pickup position"), dim));
    v.push_back (slider (Item::hslider, 150, 96, 236, 30, "Pickup", nullptr, noCaption));
    v.push_back (section (4, 184, 388, 100, TR_NOOP ("Playing")));
    v.push_back (toggle (12, 214, 48, 22, "Glissando", TR_NOOP ("Gliss"), boxed));
    v.push_back (knob (36, 262, 26, "Pitchbend 2", TR_NOOP ("Bend 2"), 280, bipolar));
    v.push_back (knob (90, 228, 30, "Tone", TR_NOOP ("Tone"), 252, bipolar));
    v.push_back (knob (136, 228, 30, "Slap", TR_NOOP ("Slap"), 252));
    v.push_back (knob (182, 228, 30, "Fret", TR_NOOP ("Fret"), 252));
    v.push_back (knob (228, 228, 30, "Harmonic", TR_NOOP ("Harm"), 252));
    v.push_back (knob (274, 228, 30, "Damp", TR_NOOP ("Damp"), 252));
    v.push_back (knob (320, 228, 30, "Vel volume", TR_NOOP ("Vel vol"), 252));
    v.push_back (knob (364, 228, 30, "Vel damp", TR_NOOP ("Vel damp"), 252));
    v.push_back (section (4, 288, 120, 112, TR_NOOP ("Amp")));
    v.push_back (radio (10, 308, 56, 88, "Amp type", "Clean|Overdrive|Distortion|Fuzz"));
    v.push_back (radio (70, 308, 50, 88, "Cabinet", "Cab 1|Cab 2|Cab 3|Cab 4"));
    v.push_back (section (128, 288, 264, 56, nullptr, boxed));
    v.push_back (knob (148, 310, 24, "Drive", TR_NOOP ("Drive"), 332));
    v.push_back (knob (190, 310, 24, "Presence", TR_NOOP ("Pres"), 332));
    v.push_back (knob (232, 310, 24, "Feedback", TR_NOOP ("Fdbk"), 332));
    v.push_back (knob (284, 310, 24, "EQ low", TR_NOOP ("Low"), 332, bipolar));
    v.push_back (knob (326, 310, 24, "EQ mid", TR_NOOP ("Mid"), 332, bipolar));
    v.push_back (knob (368, 310, 24, "EQ high", TR_NOOP ("High"), 332, bipolar));
    v.push_back (section (128, 348, 264, 52, TR_NOOP ("FX")));
    v.push_back (menuOf (134, 370, 140, 20, "Effect", nullptr, slayerEffects));
    v.push_back (knob (306, 378, 22, "FX par 1", TR_NOOP ("Par 1"), 396));
    v.push_back (knob (356, 378, 22, "FX par 2", TR_NOOP ("Par 2"), 396));
    return l;
}

// the layouts above by instrument kind (nullptr = not one of this group)
const Layout* drumLayout (InstrumentKind kind)
{
    static const Layout kick = fruitKick(), pluck = plucked(), pad = drumpad(), maxx = drumaxx(), bass = bassDrum(), dsl = drumSynthLive(),
                        sak = sakura(), slay = slayer();
    switch (kind)
    {
        case InstrumentKind::fruitkick: return &kick;
        case InstrumentKind::plucked: return &pluck;
        case InstrumentKind::drumpad: return &pad;
        case InstrumentKind::drumaxx: return &maxx;
        case InstrumentKind::bassdrum: return &bass;
        case InstrumentKind::drumsynth: return &dsl;
        case InstrumentKind::sakura: return &sak;
        case InstrumentKind::slayer: return &slay;
        default: return nullptr;
    }
}
