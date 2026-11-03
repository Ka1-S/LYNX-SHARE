// Round 168r: designed editors for the effects that still had a generated knob page (included by core/PluginLayout.cpp
// inside its anonymous namespace, after the builders).
//
// FL effects: the arrangement of FL Studio 2026's own editor form (the TSynthEditorForm resource of each plugin DLL,
// read as a layout reference only — positions, sizes, control kinds and ranges; docs/analise/REA_rodada168r.md §2),
// drawn in LYNX's style. LYNX's own built-ins: LYNX's own design (§3).

// --- FL's generic "Fruity" cells (Fast LP, Bass Boost, Delay, Center, Phase Inverter): FL's bevels sit 165 apart from x = 5

Layout fruityFastLP()
{
    // FL: two 160 × 45 bevels, CUTOFF and RESONANCE, each a wheel + a value slider (335 × 55)
    auto l = fruityGrid ("Fruity Fast LP", 1, 2);
    auto& v = l.items;
    fruityCell (v, 0, 0, "Cutoff", TR_NOOP ("Cutoff"), Cell::slider);
    fruityCell (v, 1, 0, "Resonance", TR_NOOP ("Resonance"), Cell::slider);
    l.height += 70;
    v.push_back (display (6, 58, 324, 62, "fxresponse"));
    return l;
}

Layout flBassBoost()
{
    // FL: FREQ (wheel + read-out) and AMOUNT (wheel + slider), 335 × 55
    auto l = fruityGrid ("Fruity Bass Boost", 1, 2);
    auto& v = l.items;
    fruityCell (v, 0, 0, "Frequency", TR_NOOP ("Freq"));
    fruityCell (v, 1, 0, "Amount", TR_NOOP ("Amount"), Cell::slider);
    l.height += 70;
    v.push_back (display (6, 58, 324, 62, "fxresponse"));
    return l;
}

Layout flDelay1()
{
    // FL: INPUT / FEEDBACK / CUTOFF (wheel + slider) over TEMPO / STEPS / MODE (wheel + read-out), 500 × 105
    auto l = fruityGrid ("Fruity Delay", 2, 3);
    auto& v = l.items;
    fruityCell (v, 0, 0, "Input", TR_NOOP ("Input"), Cell::slider);
    fruityCell (v, 1, 0, "Feedback", TR_NOOP ("Feedback"), Cell::slider);
    fruityCell (v, 2, 0, "Cutoff", TR_NOOP ("Cutoff"), Cell::slider);
    fruityCell (v, 0, 1, "Tempo", TR_NOOP ("Tempo"));
    fruityCell (v, 1, 1, "Time", TR_NOOP ("Steps"), Cell::value, 15);
    fruityCell (v, 2, 1, "Mode", TR_NOOP ("Mode"), Cell::value, 2);
    return l;
}

Layout flCenter()
{
    // FL: one bevel, STATUS, an off / on switch (170 × 55)
    auto l = fruityGrid ("Fruity Center", 1, 1);
    auto& v = l.items;
    v.push_back (section (6, 5, 160, 45, nullptr, boxed));
    v.push_back (label (60, 8, 102, 15, TR_NOOP ("Status"), boxed));
    v.push_back (tabs (12, 25, 150, 20, "On", TR_NOOP ("Off|On")));
    return l;
}

Layout flPhaseInverter()
{
    // FL: one bevel, STATE, a wheel and its read-out (none / left / right; 170 × 55)
    auto l = fruityGrid ("Fruity Phase Inverter", 1, 1);
    auto& v = l.items;
    v.push_back (section (6, 5, 160, 45, nullptr, boxed));
    v.push_back (label (60, 8, 102, 15, TR_NOOP ("State"), boxed));
    v.push_back (tabs (12, 25, 150, 20, "Invert", TR_NOOP ("None|Left|Right"), { 0.0f, 0.5f, 1.0f }));
    return l;
}

Layout fl7BandEq()
{
    // FL: seven vertical ±18 dB faders 47.5 apart, the band's frequency under each (340 × 150); LYNX adds the curve
    Layout l;
    l.fl = "Fruity 7 Band EQ";
    l.width = 340;
    l.height = 222;
    auto& v = l.items;
    v.push_back (section (0, 0, 340, 150, nullptr, boxed));
    static const char* const bands[7] = { "63 Hz", "250 Hz", "500 Hz", "1.5 kHz", "3 kHz", "5 kHz", "8 kHz" };
    static const char* const marks[7] = { "63", "250", "500", "1500", "3000", "5000", "8000" };
    static const float xs[7] = { 15, 62, 110, 157, 205, 252, 300 };
    for (int b = 0; b < 7; ++b)
    {
        auto s = slider (Item::vslider, xs[b], 4, 29, 123, bands[b], nullptr, bipolar | noCaption);
        s.colour = b;
        v.push_back (s);
        v.push_back (label (xs[b] - 4, 130, 37, 14, marks[b]));
    }
    v.push_back (display (0, 154, 340, 68, "fxresponse"));
    return l;
}

// --- Spreader (FL: 540 × 255, the three wheels down the left, Mono under them, the field on the right) ---------------

Layout spreaderLayout()
{
    Layout l;
    l.fl = "Spreader";
    l.width = 540;
    l.height = 255;
    auto& v = l.items;
    v.push_back (section (0, 0, 78, 255, nullptr, boxed));
    v.push_back (toggle (10, 6, 62, 18, "Enable", TR_NOOP ("On")));
    v.push_back (knob (38, 55, 44, "Width", TR_NOOP ("Spread"), 88, bipolar));
    v.push_back (knob (38, 123, 44, "Separation", TR_NOOP ("Separation"), 156, bipolar));
    v.push_back (knob (38, 189, 44, "Low bypass", TR_NOOP ("LF bypass"), 222));
    v.push_back (toggle (8, 233, 66, 16, "Mono bass", TR_NOOP ("Mono"), boxed));
    v.push_back (section (82, 0, 458, 255, nullptr, boxed));
    v.push_back (display (88, 6, 446, 243, "spreadfield"));
    return l;
}

// --- Frequency Shifter (FL: 460 × 220) ---------------------------------------------------------------------------------

Layout freqShifterFl()
{
    Layout l;
    l.fl = "Frequency Shifter";
    l.width = 460;
    l.height = 220;
    auto& v = l.items;
    v.push_back (section (0, 0, 460, 132, nullptr, boxed));
    v.push_back (knob (85, 65, 88, "Frequency", TR_NOOP ("Frequency"), 120, bipolar | big));
    v.push_back (display (140, 10, 140, 64, "fshift"));
    v.push_back (label (292, 8, 158, 20, TR_NOOP ("Frequency Shifter"), dim | alignLeft));
    v.push_back (value (290, 30, 160, 24, "Frequency"));
    v.push_back (radio (165, 80, 70, 48, "Range", TR_NOOP ("20 kHz|200 Hz|Tempo"), { 0.0f, 0.5f, 1.0f }));
    v.push_back (radio (262, 80, 130, 48, "Type", TR_NOOP ("Shifter HQ|Shifter|Ring modulator"), { 0.0f, 0.5f, 1.0f }));
    v.push_back (section (0, 134, 460, 86, nullptr, boxed));
    v.push_back (knob (37, 164, 44, "L shape", TR_NOOP ("Left shape"), 200, bipolar));
    v.push_back (knob (134, 164, 44, "R shape", TR_NOOP ("Right shape"), 200, bipolar));
    v.push_back (knob (201, 166, 36, "Left phase offset", TR_NOOP ("Start phase"), 200, bipolar));
    v.push_back (knob (255, 166, 36, "L/R phase offset", TR_NOOP ("L/R phase"), 200));
    v.push_back (knob (308, 166, 36, "Feedback", TR_NOOP ("Feedback"), 200, bipolar));
    v.push_back (knob (372, 166, 36, "Mix", TR_NOOP ("Mix"), 200));
    v.push_back (knob (424, 166, 36, "Mono/stereo", TR_NOOP ("Stereo"), 200));
    return l;
}

// --- Low Lifter (FL: 680 × 280, the band view on top, the settings strip under it) -----------------------------------

Layout lowLifterFl()
{
    Layout l;
    l.fl = "Low Lifter";
    l.width = 680;
    l.height = 280;
    auto& v = l.items;
    v.push_back (display (6, 6, 668, 182, "lowlifter"));
    v.push_back (section (0, 196, 680, 84, nullptr, boxed));
    v.push_back (knob (38, 236, 40, "Pre gain", TR_NOOP ("Boost"), 270, bipolar));
    v.push_back (toggle (72, 226, 64, 16, "Multiband", TR_NOOP ("Multiband")));
    v.push_back (toggle (72, 251, 64, 16, "Look-ahead", TR_NOOP ("Look-ahead")));
    v.push_back (knob (159, 236, 40, "Slope", TR_NOOP ("Slope"), 270));
    v.push_back (toggle (186, 238, 58, 16, "Invert polarity", TR_NOOP ("Invert")));
    v.push_back (section (246, 202, 106, 74, TR_NOOP ("Diffuser"), raised));
    v.push_back (knob (273, 242, 34, "Diffuser depth", TR_NOOP ("Depth"), 270, bipolar));
    v.push_back (knob (325, 242, 34, "Diffuser mix", TR_NOOP ("Mix"), 270));
    v.push_back (section (356, 202, 200, 74, TR_NOOP ("Bands"), raised));
    // the solo switches of the two bands at the section's sides, their gains and the crossover between them
    v.push_back (toggle (361, 236, 36, 14, "Low band", TR_NOOP ("Low"), boxed));
    v.push_back (knob (419, 242, 34, "Low band gain", TR_NOOP ("Low"), 270, bipolar));
    v.push_back (knob (456, 242, 34, "Crossover", TR_NOOP ("X-over"), 270));
    v.push_back (knob (493, 242, 34, "High band gain", TR_NOOP ("High"), 270, bipolar));
    v.push_back (toggle (515, 236, 36, 14, "High band", TR_NOOP ("High"), boxed));
    v.push_back (knob (584, 242, 34, "Low cut", TR_NOOP ("Low cut"), 270));
    v.push_back (toggle (606, 206, 66, 16, "On", TR_NOOP ("On")));
    v.push_back (knob (645, 242, 34, "Dry/Wet", TR_NOOP ("Mix"), 270));
    return l;
}

// --- Peak Controller (FL: PEAK and LFO panels, the meter under them, Ramp / Mute) -------------------------------------

Layout peakControllerFl()
{
    Layout l;
    l.fl = "Fruity Peak Controller";
    l.width = 521;
    l.height = 214;
    auto& v = l.items;
    v.push_back (section (7, 7, 188, 88, TR_NOOP ("PEAK")));
    v.push_back (knob (32, 52, 34, "Base", TR_NOOP ("Base"), 84));
    v.push_back (knob (78, 52, 34, "Amount", TR_NOOP ("Vol"), 84, bipolar));
    v.push_back (knob (124, 52, 34, "Tension", TR_NOOP ("Tension"), 84, bipolar));
    v.push_back (knob (170, 52, 34, "Decay", TR_NOOP ("Decay"), 84));
    v.push_back (section (203, 7, 311, 88, TR_NOOP ("LFO")));
    v.push_back (knob (228, 52, 34, "LFO base", TR_NOOP ("Base"), 84));
    v.push_back (knob (274, 52, 34, "LFO amount", TR_NOOP ("Vol"), 84, bipolar));
    v.push_back (knob (320, 52, 34, "LFO tension", TR_NOOP ("Tension"), 84, bipolar));
    v.push_back (menuOf (360, 40, 56, 24, "LFO shape", TR_NOOP ("Shape"), TR_NOOP ("Sine|Triangle|Square|Saw|Random")));
    v.push_back (label (360, 76, 56, 12, TR_NOOP ("Shape")));
    v.push_back (knob (441, 52, 34, "LFO speed", TR_NOOP ("Speed"), 84));
    v.push_back (knob (488, 52, 34, "LFO phase", TR_NOOP ("Phase"), 84));
    v.push_back (section (7, 100, 507, 80, nullptr, boxed));
    v.push_back (display (12, 104, 497, 72, "peakmeter"));
    v.push_back (toggle (392, 186, 66, 22, "Ramp", TR_NOOP ("Ramp"), boxed));
    v.push_back (toggle (462, 186, 52, 22, "Mute", TR_NOOP ("Mute"), boxed));
    return l;
}

// --- Pitcher (FL: 523 × 354; scale / key, the twelve note buttons between the input and output pitch, the bypass row,
// Speed and Fine tune, the gender meter, Formant, MIDI / Octaves / Harmonize; with Harmonize: voice panning, stereo
// spread, replace / mix) ---------------------------------------------------------------------------------------------

Layout pitcherLayout()
{
    Layout l;
    l.fl = "Pitcher";
    l.width = 523;
    l.height = 330;
    auto& v = l.items;
    static const char* const notes[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char* const offs[12] = { "C off", "C# off", "D off", "D# off", "E off", "F off", "F# off", "G off", "G# off", "A off", "A# off", "B off" };
    v.push_back (section (0, 0, 523, 132, TR_NOOP ("Pitch")));
    v.push_back (knob (34, 50, 30, "A freq.", TR_NOOP ("A3 (Hz)"), 76));
    v.push_back (value (54, 40, 54, 18, "A freq."));
    v.push_back (menuOf (12, 84, 92, 22, "Range", TR_NOOP ("Range"), "220 Hz|170 Hz|110 Hz|80 Hz|55 Hz|25 Hz"));
    v.push_back (label (12, 108, 92, 12, TR_NOOP ("Lowest note"), dim));
    v.push_back (label (118, 28, 394, 14, TR_NOOP ("Input pitch"), dim));
    for (int n = 0; n < 12; ++n)
    {
        auto t = toggle (118.0f + 33.0f * (float) n, 46, 30, 28, notes[n], notes[n], boxed);
        v.push_back (t);
    }
    v.push_back (label (118, 78, 394, 14, TR_NOOP ("Output pitch"), dim));
    v.push_back (label (118, 98, 394, 12, TR_NOOP ("Lit notes: the voice is pulled to them"), dim));
    v.push_back (section (0, 134, 523, 52, nullptr, boxed));
    v.push_back (toggle (8, 146, 82, 26, "Bypass all", TR_NOOP ("Enable bypass"), boxed));
    for (int n = 0; n < 12; ++n)
        v.push_back (toggle (124.0f + 33.0f * (float) n, 148, 18, 18, offs[n], nullptr, boxed));
    v.push_back (label (118, 168, 394, 14, TR_NOOP ("Bypass: the note passes uncorrected"), dim));

    v.push_back (section (0, 188, 523, 142, nullptr, boxed));
    // without Harmonize: Speed on the left (FL's BG1); with Harmonize: the voice panning, the stereo spread, replace / mix
    v.push_back (when (knob (48, 232, 44, "Speed", TR_NOOP ("Speed"), 266, showValue), "Harmonize", 0.0f, 0.5f));
    v.push_back (when (label (8, 196, 120, 14, TR_NOOP ("Voice panning"), dim | alignLeft), "Harmonize", 0.5f, 1.0f));
    for (int i = 0; i < 4; ++i)
    {
        static const char* const pans[4] = { "Voice 1 pan", "Voice 2 pan", "Voice 3 pan", "Voice 4 pan" };
        v.push_back (when (slider (Item::hslider, 8, 212.0f + 20.0f * (float) i, 100, 16, pans[i], nullptr, bipolar | noCaption), "Harmonize", 0.5f, 1.0f));
    }
    v.push_back (when (knob (148, 236, 40, "Stereo", TR_NOOP ("Stereo spread"), 270, showValue), "Harmonize", 0.5f, 1.0f));
    v.push_back (when (tabs (8, 298, 100, 20, "Mix", TR_NOOP ("Replace|Mix")), "Harmonize", 0.5f, 1.0f));
    v.push_back (when (toggle (114, 298, 44, 20, "Vel", TR_NOOP ("Vel"), boxed), "Harmonize", 0.5f, 1.0f));

    v.push_back (section (196, 196, 130, 92, TR_NOOP ("Gender"), raised));
    v.push_back (knob (261, 248, 52, "Gender", nullptr, 0, bipolar | noCaption));
    v.push_back (value (206, 270, 110, 14, "Gender", 0));
    v.push_back (toggle (340, 214, 80, 22, "Formant", TR_NOOP ("Formant"), boxed));
    v.push_back (menuOf (340, 244, 80, 22, "Pitch ref.", TR_NOOP ("Reference"), TR_NOOP ("Left|Independent|Right")));
    v.push_back (label (330, 267, 100, 12, TR_NOOP ("Reference"), dim)); // FL: the channel the pitch is read from
    v.push_back (knob (468, 232, 44, "Fine", TR_NOOP ("Fine tune"), 266, bipolar | showValue));
    v.push_back (toggle (196, 298, 50, 22, "MIDI", TR_NOOP ("MIDI"), boxed));
    v.push_back (toggle (250, 298, 70, 22, "Octaves", TR_NOOP ("Octaves"), boxed));
    v.push_back (toggle (324, 298, 96, 22, "Harmonize", TR_NOOP ("Harmonize"), boxed));
    return l;
}

// --- LYNX's own simple built-ins: LYNX's own design (a graph of what the setting does on top, the controls in titled
// sections under it; captions above the knobs, values under them) -------------------------------------------------

Layout lynxEq()
{
    Layout l;
    l.width = 420;
    l.height = 236;
    auto& v = l.items;
    v.push_back (display (8, 8, 404, 120, "fxresponse"));
    v.push_back (section (8, 136, 404, 92, TR_NOOP ("THREE-BAND EQ")));
    v.push_back (knob (56, 180, 38, "Low", TR_NOOP ("Low"), 212, bipolar | showValue));
    v.push_back (knob (136, 180, 38, "Mid", TR_NOOP ("Mid"), 212, bipolar | showValue));
    v.push_back (knob (210, 180, 38, "Mid freq.", TR_NOOP ("Mid freq."), 212, showValue));
    v.push_back (knob (290, 180, 38, "High", TR_NOOP ("High"), 212, bipolar | showValue));
    v.push_back (knob (370, 180, 38, "Output", TR_NOOP ("Output"), 212, bipolar | showValue));
    return l;
}

Layout lynxDelay()
{
    Layout l;
    l.width = 420;
    l.height = 236;
    auto& v = l.items;
    v.push_back (display (8, 8, 404, 110, "delaytaps"));
    v.push_back (section (8, 126, 260, 102, TR_NOOP ("ECHO")));
    v.push_back (knob (52, 174, 40, "Time", TR_NOOP ("Time"), 208, showValue | big, 7));
    v.push_back (knob (134, 174, 40, "Feedback", TR_NOOP ("Feedback"), 208, showValue));
    v.push_back (knob (216, 174, 40, "Damping", TR_NOOP ("Damping"), 208, showValue));
    v.push_back (section (272, 126, 140, 102, TR_NOOP ("OUTPUT")));
    v.push_back (knob (312, 174, 40, "Mix", TR_NOOP ("Mix"), 208, showValue));
    v.push_back (toggle (348, 162, 60, 22, "Ping-pong", TR_NOOP ("Ping-pong"), boxed));
    return l;
}

Layout lynxReverb()
{
    Layout l;
    l.width = 460;
    l.height = 270;
    auto& v = l.items;
    v.push_back (display (8, 8, 444, 100, "reverbspace"));
    v.push_back (section (8, 116, 220, 146, TR_NOOP ("SPACE")));
    v.push_back (knob (48, 160, 34, "Predelay", TR_NOOP ("Predelay"), 190, showValue));
    v.push_back (knob (118, 160, 34, "Room", TR_NOOP ("Size"), 190, showValue | big));
    v.push_back (knob (188, 160, 34, "Damping", TR_NOOP ("Damping"), 190, showValue));
    v.push_back (knob (118, 228, 26, "Width", TR_NOOP ("Width"), 246, noCaption));
    v.push_back (label (24, 222, 70, 14, TR_NOOP ("Width"), dim));
    // TONE: each knob keeps its full size above its caption and value (rel 0.1.23: High cut was squeezed to 15 px and
    // its value ran past the section)
    v.push_back (section (232, 116, 110, 146, TR_NOOP ("TONE")));
    v.push_back (knob (287, 158, 30, "Low cut", TR_NOOP ("Low cut"), 194, showValue));
    v.push_back (knob (287, 218, 30, "High cut", TR_NOOP ("High cut"), 252, showValue));
    v.push_back (section (346, 116, 106, 146, TR_NOOP ("MIX")));
    v.push_back (slider (Item::vslider, 360, 140, 34, 116, "Dry", TR_NOOP ("Dry")));
    v.push_back (slider (Item::vslider, 404, 140, 34, 116, "Mix", TR_NOOP ("Wet")));
    return l;
}

Layout lynxLimiter()
{
    Layout l;
    l.width = 380;
    l.height = 170;
    auto& v = l.items;
    v.push_back (display (8, 8, 150, 154, "limiter"));
    v.push_back (section (166, 8, 206, 154, TR_NOOP ("LIMITER")));
    v.push_back (knob (214, 76, 50, "Gain", TR_NOOP ("Gain"), 118, showValue | big));
    v.push_back (knob (300, 64, 34, "Ceiling", TR_NOOP ("Ceiling"), 96, showValue));
    v.push_back (knob (300, 128, 30, "Release", TR_NOOP ("Release"), 156, noCaption));
    v.push_back (label (330, 118, 40, 14, TR_NOOP ("Release"), dim | alignLeft));
    v.push_back (value (330, 132, 42, 14, "Release"));
    return l;
}

Layout lynxFilter()
{
    Layout l;
    l.width = 400;
    l.height = 224;
    auto& v = l.items;
    v.push_back (display (8, 8, 384, 110, "fxresponse"));
    v.push_back (section (8, 126, 384, 90, TR_NOOP ("FILTER")));
    v.push_back (tabs (18, 150, 140, 22, "Type", TR_NOOP ("Low-pass|High-pass|Band-pass"), { 0.0f, 0.5f, 1.0f }));
    v.push_back (label (18, 178, 140, 30, TR_NOOP ("12 dB/oct state-variable filter"), dim));
    v.push_back (knob (210, 166, 40, "Cutoff", TR_NOOP ("Cutoff"), 202, showValue | big));
    v.push_back (knob (284, 166, 34, "Reso.", TR_NOOP ("Resonance"), 202, showValue));
    v.push_back (knob (352, 166, 34, "Mix", TR_NOOP ("Mix"), 202, showValue));
    return l;
}

Layout lynxDistortion()
{
    Layout l;
    l.width = 400;
    l.height = 176;
    auto& v = l.items;
    v.push_back (display (8, 8, 160, 160, "fxtransfer"));
    v.push_back (section (176, 8, 216, 160, TR_NOOP ("DRIVE")));
    v.push_back (knob (230, 70, 52, "Drive", TR_NOOP ("Drive"), 114, showValue | big));
    v.push_back (knob (330, 64, 34, "Tone", TR_NOOP ("Tone"), 96, showValue));
    v.push_back (knob (230, 138, 28, "Mix", TR_NOOP ("Mix"), 160, noCaption));
    v.push_back (label (196, 150, 20, 14, TR_NOOP ("Mix"), dim));
    v.push_back (knob (330, 132, 34, "Output", TR_NOOP ("Output"), 160, bipolar | showValue));
    return l;
}

Layout lynxChorus()
{
    Layout l;
    l.width = 380;
    l.height = 200;
    auto& v = l.items;
    v.push_back (display (8, 8, 364, 84, "chorusvoices"));
    v.push_back (section (8, 100, 364, 92, TR_NOOP ("CHORUS")));
    v.push_back (knob (56, 144, 38, "Rate", TR_NOOP ("Rate"), 176, showValue));
    v.push_back (knob (146, 144, 38, "Depth", TR_NOOP ("Depth"), 176, showValue));
    v.push_back (knob (236, 144, 38, "Feedback", TR_NOOP ("Feedback"), 176, showValue));
    v.push_back (knob (324, 144, 38, "Mix", TR_NOOP ("Mix"), 176, showValue));
    return l;
}

Layout lynxUtility()
{
    Layout l;
    l.width = 420;
    l.height = 200;
    auto& v = l.items;
    v.push_back (section (8, 8, 132, 184, TR_NOOP ("LEVEL")));
    v.push_back (knob (74, 70, 50, "Gain", TR_NOOP ("Gain"), 112, bipolar | showValue | big));
    v.push_back (knob (74, 152, 34, "Balance", TR_NOOP ("Balance"), 184, bipolar | showValue));
    v.push_back (section (144, 8, 268, 184, TR_NOOP ("STEREO MATRIX")));
    v.push_back (display (152, 32, 116, 152, "utilitymatrix"));
    v.push_back (knob (306, 64, 32, "L -> L", "L > L", 94, bipolar | showValue));
    v.push_back (knob (372, 64, 32, "R -> L", "R > L", 94, bipolar | showValue));
    v.push_back (knob (306, 144, 32, "L -> R", "L > R", 174, bipolar | showValue));
    v.push_back (knob (372, 144, 32, "R -> R", "R > R", 174, bipolar | showValue));
    return l;
}

Layout lynxLofi()
{
    Layout l;
    l.width = 360;
    l.height = 176;
    auto& v = l.items;
    v.push_back (display (8, 8, 160, 160, "fxtransfer"));
    v.push_back (section (176, 8, 176, 160, TR_NOOP ("LO-FI")));
    v.push_back (knob (220, 70, 40, "Rate", TR_NOOP ("Rate"), 104, showValue));
    v.push_back (knob (306, 70, 40, "Bits", TR_NOOP ("Bits"), 104, showValue));
    v.push_back (knob (264, 138, 30, "Mix", TR_NOOP ("Mix"), 162, noCaption));
    v.push_back (label (196, 132, 46, 14, TR_NOOP ("Mix"), dim));
    v.push_back (value (290, 132, 56, 14, "Mix"));
    return l;
}

Layout lynxCrossover()
{
    Layout l;
    l.width = 420;
    l.height = 236;
    auto& v = l.items;
    v.push_back (display (8, 8, 404, 110, "fxresponse"));
    v.push_back (section (8, 126, 200, 102, TR_NOOP ("HIGH-PASS")));
    v.push_back (toggle (16, 150, 60, 20, "High-pass", TR_NOOP ("On"), boxed));
    v.push_back (knob (112, 170, 38, "HP cutoff", TR_NOOP ("Cutoff"), 206, showValue));
    v.push_back (menuOf (152, 150, 50, 20, "HP slope", TR_NOOP ("Slope"), "6|12|18|24|36|48|72|96"));
    v.push_back (label (152, 172, 50, 12, "dB/oct", dim));
    v.push_back (section (212, 126, 200, 102, TR_NOOP ("LOW-PASS")));
    v.push_back (toggle (220, 150, 60, 20, "Low-pass", TR_NOOP ("On"), boxed));
    v.push_back (knob (316, 170, 38, "LP cutoff", TR_NOOP ("Cutoff"), 206, showValue));
    v.push_back (menuOf (356, 150, 50, 20, "LP slope", TR_NOOP ("Slope"), "6|12|18|24|36|48|72|96"));
    v.push_back (label (356, 172, 50, 12, "dB/oct", dim));
    return l;
}

Layout lynxPitchShift()
{
    Layout l;
    l.width = 400;
    l.height = 200;
    auto& v = l.items;
    v.push_back (display (8, 8, 384, 76, "pitchshift"));
    v.push_back (section (8, 92, 384, 100, TR_NOOP ("PITCH")));
    v.push_back (knob (64, 140, 48, "Semitones", TR_NOOP ("Semitones"), 180, bipolar | showValue | big, 48));
    v.push_back (knob (160, 140, 36, "Cents", TR_NOOP ("Cents"), 180, bipolar | showValue));
    v.push_back (knob (250, 140, 36, "Window", TR_NOOP ("Grain"), 180, showValue));
    v.push_back (knob (338, 140, 36, "Mix", TR_NOOP ("Mix"), 180, showValue));
    return l;
}

Layout lynxFreqShift()
{
    Layout l;
    l.width = 380;
    l.height = 200;
    auto& v = l.items;
    v.push_back (display (8, 8, 364, 84, "fshift"));
    v.push_back (section (8, 100, 364, 92, TR_NOOP ("SHIFT")));
    v.push_back (knob (80, 144, 46, "Shift", TR_NOOP ("Shift"), 182, bipolar | showValue | big));
    v.push_back (knob (200, 144, 36, "Stereo", TR_NOOP ("Stereo"), 182, showValue));
    v.push_back (knob (300, 144, 36, "Mix", TR_NOOP ("Mix"), 182, showValue));
    return l;
}

Layout lynxRhythm()
{
    // the curve on top; Period, Points and Control only; each point's position, level and tension (6 per page)
    Layout l;
    l.width = 520;
    l.height = 330;
    l.pages = { TR_NOOP ("Points 1-6"), TR_NOOP ("Points 7-12") };
    auto& v = l.items;
    v.push_back (display (8, 8, 504, 96, "rhythmcurve"));
    v.push_back (section (8, 112, 504, 58, nullptr, boxed));
    v.push_back (knob (40, 138, 30, "Period", TR_NOOP ("Period"), 0, noCaption));
    v.push_back (label (60, 124, 70, 14, TR_NOOP ("Period"), dim | alignLeft));
    v.push_back (value (60, 140, 90, 16, "Period"));
    v.push_back (stepper (170, 120, 80, 42, "Points", TR_NOOP ("Points"), 12));
    v.push_back (toggle (270, 130, 120, 22, "Control only", TR_NOOP ("Control only")));
    v.push_back (pageTabs (400, 128, 106, 26, TR_NOOP ("1-6|7-12")));
    static const char* const names[12][3] = { { "P1 pos", "P1 level", "P1 tension" }, { "P2 pos", "P2 level", "P2 tension" },
                                              { "P3 pos", "P3 level", "P3 tension" }, { "P4 pos", "P4 level", "P4 tension" },
                                              { "P5 pos", "P5 level", "P5 tension" }, { "P6 pos", "P6 level", "P6 tension" },
                                              { "P7 pos", "P7 level", "P7 tension" }, { "P8 pos", "P8 level", "P8 tension" },
                                              { "P9 pos", "P9 level", "P9 tension" }, { "P10 pos", "P10 level", "P10 tension" },
                                              { "P11 pos", "P11 level", "P11 tension" }, { "P12 pos", "P12 level", "P12 tension" } };
    static const char* const titles[12] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12" };
    for (int page = 0; page < 2; ++page)
    {
        v.push_back (onPage (label (8, 194, 60, 14, TR_NOOP ("Position"), dim | alignLeft), page));
        v.push_back (onPage (label (8, 238, 60, 14, TR_NOOP ("Level"), dim | alignLeft), page));
        v.push_back (onPage (label (8, 282, 60, 14, TR_NOOP ("Tension"), dim | alignLeft), page));
        for (int k = 0; k < 6; ++k)
        {
            const auto p = page * 6 + k;
            const auto cx = 104.0f + 70.0f * (float) k;
            v.push_back (onPage (section (cx - 32.0f, 174, 64, 150, titles[p], raised), page));
            v.push_back (onPage (knob (cx, 210, 26, names[p][0], nullptr, 0, noCaption), page));
            v.push_back (onPage (knob (cx, 254, 26, names[p][1], nullptr, 0, noCaption), page));
            v.push_back (onPage (knob (cx, 298, 26, names[p][2], nullptr, 0, bipolar | noCaption), page));
        }
    }
    return l;
}

Layout lynxStepGate()
{
    // the 32 steps as bars (two bars of sixteenths), the mix and the edges
    Layout l;
    l.width = 560;
    l.height = 222;
    auto& v = l.items;
    v.push_back (section (8, 8, 544, 132, TR_NOOP ("STEPS")));
    static const char* const steps[32] = { "Step 1", "Step 2", "Step 3", "Step 4", "Step 5", "Step 6", "Step 7", "Step 8",
                                           "Step 9", "Step 10", "Step 11", "Step 12", "Step 13", "Step 14", "Step 15", "Step 16",
                                           "Step 17", "Step 18", "Step 19", "Step 20", "Step 21", "Step 22", "Step 23", "Step 24",
                                           "Step 25", "Step 26", "Step 27", "Step 28", "Step 29", "Step 30", "Step 31", "Step 32" };
    for (int k = 0; k < 32; ++k)
    {
        auto s = slider (Item::vslider, 16.0f + 16.6f * (float) k + (k >= 16 ? 4.0f : 0.0f), 32, 15, 100, steps[k], nullptr, noCaption);
        s.colour = (k / 4) % 2 == 0 ? 0 : 3;
        v.push_back (s);
    }
    v.push_back (section (8, 144, 544, 70, TR_NOOP ("GATE")));
    v.push_back (knob (70, 184, 32, "Attack", TR_NOOP ("Attack"), 0, noCaption));
    v.push_back (label (94, 172, 80, 14, TR_NOOP ("Attack"), dim | alignLeft));
    v.push_back (value (94, 188, 90, 14, "Attack"));
    v.push_back (knob (230, 184, 32, "Release", TR_NOOP ("Release"), 0, noCaption));
    v.push_back (label (254, 172, 80, 14, TR_NOOP ("Release"), dim | alignLeft));
    v.push_back (value (254, 188, 90, 14, "Release"));
    v.push_back (knob (400, 184, 32, "Mix", TR_NOOP ("Mix"), 0, noCaption));
    v.push_back (label (424, 172, 80, 14, TR_NOOP ("Mix"), dim | alignLeft));
    v.push_back (value (424, 188, 90, 14, "Mix"));
    return l;
}

// --- Love Philter (FL: 670 × 400) and Distructor (FL: 924 × 385) -----------------------------------------------------

Layout lovePhilterLayout()
{
    // FL's form (REA_rodada168r §2.9): the eight filter units' cutoff / resonance / envelope / pan / volume, the master
    // level and LFO, the input envelope follower and the Mod X / Y pad. The units' filter types, envelopes and routing
    // come from the FL state (a preset or a project) and play as FL's; the page edits the automatable controls.
    Layout l;
    l.fl = "Fruity Love Philter";
    l.width = 670;
    l.height = 330;
    auto& v = l.items;
    v.push_back (section (0, 0, 164, 330, TR_NOOP ("MASTER"), boxed));
    v.push_back (knob (44, 52, 40, "Master level", TR_NOOP ("Level"), 84));
    v.push_back (knob (120, 52, 40, "Master LFO", TR_NOOP ("LFO"), 84));
    v.push_back (label (8, 98, 148, 14, TR_NOOP ("Envelope follower"), dim));
    v.push_back (knob (32, 132, 30, "Follower level", TR_NOOP ("Level"), 158));
    v.push_back (knob (82, 132, 30, "Follower attack", TR_NOOP ("Att"), 158));
    v.push_back (knob (132, 132, 30, "Follower release", TR_NOOP ("Rel"), 158));
    v.push_back (label (8, 172, 60, 14, TR_NOOP ("Pattern"), dim | alignLeft));
    v.push_back (value (70, 172, 86, 14, "Pattern"));
    v.push_back (display (18, 192, 128, 130, "xy", "Mod X", "Mod Y"));
    v.push_back (section (170, 0, 500, 330, TR_NOOP ("FILTER UNITS"), boxed));
    static const char* const names[8][5] = {
        { "Unit 1 cutoff", "Unit 1 resonance", "Unit 1 envelope amount", "Unit 1 pan", "Unit 1 volume" },
        { "Unit 2 cutoff", "Unit 2 resonance", "Unit 2 envelope amount", "Unit 2 pan", "Unit 2 volume" },
        { "Unit 3 cutoff", "Unit 3 resonance", "Unit 3 envelope amount", "Unit 3 pan", "Unit 3 volume" },
        { "Unit 4 cutoff", "Unit 4 resonance", "Unit 4 envelope amount", "Unit 4 pan", "Unit 4 volume" },
        { "Unit 5 cutoff", "Unit 5 resonance", "Unit 5 envelope amount", "Unit 5 pan", "Unit 5 volume" },
        { "Unit 6 cutoff", "Unit 6 resonance", "Unit 6 envelope amount", "Unit 6 pan", "Unit 6 volume" },
        { "Unit 7 cutoff", "Unit 7 resonance", "Unit 7 envelope amount", "Unit 7 pan", "Unit 7 volume" },
        { "Unit 8 cutoff", "Unit 8 resonance", "Unit 8 envelope amount", "Unit 8 pan", "Unit 8 volume" } };
    static const char* const numbers[8] = { "1", "2", "3", "4", "5", "6", "7", "8" };
    static const char* const rows[5] = { TR_NOOP ("Cutoff"), TR_NOOP ("Res"), TR_NOOP ("Env"), TR_NOOP ("Pan"), TR_NOOP ("Vol") };
    for (int r = 0; r < 5; ++r)
        v.push_back (label (176, 42.0f + 56.0f * (float) r, 40, 14, rows[r], dim | alignLeft));
    for (int u = 0; u < 8; ++u)
    {
        const auto cx = 240.0f + 56.0f * (float) u;
        v.push_back (label (cx - 24.0f, 22, 48, 14, numbers[u]));
        for (int r = 0; r < 5; ++r)
            v.push_back (knob (cx, 56.0f + 56.0f * (float) r, 30, names[u][r], nullptr, 0, noCaption | (r == 3 ? bipolar : 0)));
    }
    return l;
}

Layout distructorLayout()
{
    // FL's form (REA_rodada168r §2.8): four module panels, each with its input, dry / wet and output wheels at the
    // bottom; the module itself (distortion, filter, chorus, speaker…) comes from the FL state and is named on top;
    // SOFT CLIP OUTPUT at the right
    Layout l;
    l.fl = "Distructor";
    l.width = 720;
    l.height = 200;
    auto& v = l.items;
    static const char* const params[4][3] = { { "M1 In", "M1 Mix", "M1 Out" }, { "M2 In", "M2 Mix", "M2 Out" },
                                              { "M3 In", "M3 Mix", "M3 Out" }, { "M4 In", "M4 Mix", "M4 Out" } };
    static const char* const titles[4] = { TR_NOOP ("MODULE 1"), TR_NOOP ("MODULE 2"), TR_NOOP ("MODULE 3"), TR_NOOP ("MODULE 4") };
    for (int m = 0; m < 4; ++m)
    {
        const auto x = 150.0f * (float) m;
        v.push_back (section (x, 0, 146, 200, titles[m], boxed));
        v.push_back (display (x + 6.0f, 22, 134, 92, "distructormodule", params[m][0]));
        v.push_back (knob (x + 28.0f, 146, 32, params[m][0], TR_NOOP ("In"), 176));
        v.push_back (knob (x + 73.0f, 146, 32, params[m][1], TR_NOOP ("Mix"), 176));
        v.push_back (knob (x + 118.0f, 146, 32, params[m][2], TR_NOOP ("Out"), 176));
    }
    v.push_back (section (600, 0, 120, 200, TR_NOOP ("OUTPUT"), boxed));
    v.push_back (toggle (610, 40, 100, 36, "Clipper", TR_NOOP ("Soft clip output"), boxed));
    return l;
}
