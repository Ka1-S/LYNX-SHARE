// FL arrangements of round 105 (docs/analise/REA_rodada105.md): Fruity Vocoder, Frequency Splitter, Fruity Delay Bank,
// EQUO, Multiband Delay, Fruity Convolver, Pitch Shifter, Hardcore, Effector, Vocodex. Included by PluginLayout.cpp
// inside its anonymous namespace (it uses the builders defined there). Positions measured on FL Studio 2026's windows
// (captures in tmp/r93/flref and tmp/r105/flref, never in the repository), one unit = one FL pixel, body origin.

LayoutItem menu (float x, float y, float w, float h, const char* param, const char* options, std::vector<float> values = {})
{
    LayoutItem i;
    i.type = Item::menu;
    i.x = x, i.y = y, i.w = w, i.h = h;
    i.param = param;
    i.options = options;
    i.values = std::move (values);
    return i;
}

// a read-out on a plate above a fader or knob (FL's green value boxes)
LayoutItem plate (float x, float y, float w, float h, const char* param)
{
    return value (x, y, w, h, param, boxed);
}

// a page selector that sets one digit of the editor's page: page = … + option · stride (two selectors share a page:
// EQUO's bank and target, Multiband Delay's)
LayoutItem pageDigit (float x, float y, float w, float h, const char* options, int stride)
{
    auto i = pageTabs (x, y, w, h, options);
    i.steps = stride;
    return i;
}

// a knob without caption plus its caption above it (FL's Delay Bank globals)
void knobCaptionAbove (std::vector<LayoutItem>& v, float cx, float cy, float d, const char* param, const char* caption, float captionY)
{
    v.push_back (knob (cx, cy, d, param, nullptr, 0, noCaption));
    v.push_back (label (cx - 24, captionY - 6, 48, 12, caption));
}

// the name of a state control (core/StateParams.h) as a stable C string for a layout item
const char* stateName (EffectKind kind, const std::string& name)
{
    const auto* table = stateparams::tableFor (kind);
    const auto i = table != nullptr ? table->index (name.c_str()) : -1;
    return i >= 0 ? table->name (i) : "";
}

Layout fruityVocoder()
{
    // FREQ (FORM fader, MIN, MAX, SCALE, BW, INVERT) · ENV (ATT, REL) · MIX (listen modulator / carrier / mix LEDs,
    // the L / R inputs, MOD, CAR, WET faders) over the band display with HOLD, BANDS 4…128 and FILTER 1…3
    Layout l;
    l.fl = "Fruity Vocoder";
    l.width = 416;
    l.height = 306;
    auto& v = l.items;
    v.push_back (section (12, 6, 201, 143, TR_NOOP ("Freq")));
    v.push_back (slider (Item::vslider, 22, 30, 20, 100, "Formant", nullptr, noCaption | bipolar));
    v.push_back (label (8, 132, 48, 12, TR_NOOP ("Form")));
    v.push_back (knob (83, 50, 32, "Min freq.", TR_NOOP ("Min"), 79));
    v.push_back (knob (132, 50, 32, "Max freq.", TR_NOOP ("Max"), 79));
    v.push_back (knob (181, 50, 32, "Scale", TR_NOOP ("Scale"), 79, bipolar));
    v.push_back (knob (83, 109, 32, "Width", TR_NOOP ("BW"), 138));
    v.push_back (toggle (172, 97, 20, 12, "Invert", nullptr));
    v.push_back (label (157, 113, 48, 12, TR_NOOP ("Invert")));
    v.push_back (section (219, 6, 62, 143, TR_NOOP ("Env")));
    v.push_back (knob (250, 50, 32, "Attack", TR_NOOP ("Att"), 79));
    v.push_back (knob (250, 109, 32, "Release", TR_NOOP ("Rel"), 138));
    v.push_back (section (287, 6, 116, 143, TR_NOOP ("Mix")));
    v.push_back (toggle (306, 18, 16, 13, "Swap L/R", "L", boxed));
    v.push_back (toggle (338, 18, 16, 13, "Swap L/R", "R", boxed));
    v.push_back (tabs (298, 34, 96, 9, "Listen", TR_NOOP ("Modulator|Carrier|Mix")));
    v.push_back (slider (Item::vslider, 303, 48, 20, 82, "Modulator", nullptr, noCaption));
    v.push_back (slider (Item::vslider, 335, 48, 20, 82, "Carrier", nullptr, noCaption));
    v.push_back (slider (Item::vslider, 368, 48, 20, 82, "Wet", nullptr, noCaption));
    v.push_back (label (295, 133, 36, 12, TR_NOOP ("Mod")));
    v.push_back (label (327, 133, 36, 12, TR_NOOP ("Car")));
    v.push_back (label (360, 133, 36, 12, TR_NOOP ("Wet")));
    v.push_back (section (13, 160, 388, 132, nullptr, boxed | raised));
    v.push_back (label (18, 163, 200, 18, "Fruity Vocoder", dim | alignLeft));
    v.push_back (toggle (345, 165, 50, 15, "Hold", TR_NOOP ("Hold"), boxed));
    v.push_back (display (18, 183, 378, 86, "vocbands", "Bands"));
    v.push_back (label (17, 273, 54, 14, TR_NOOP ("Bands"), alignLeft));
    v.push_back (tabs (76, 273, 154, 14, "Bands", "4|8|16|32|64|128"));
    v.push_back (label (285, 273, 54, 14, TR_NOOP ("Filter")));
    v.push_back (tabs (343, 273, 53, 14, "Order", "1|2|3"));
    l.menuSwitches = { "Side inputs" };
    return l;
}

Layout frequencySplitterLayout()
{
    // the band graph on top; CROSSOVER (slope, freq, bands 2 / 3, freq, slope) · MAIN OUT (low / mid / high level and
    // their switches) · SENDS (each band's send: output switch, level and switch); linear phase, precision and speed at
    // the bottom. The solo switches (FL: behind the panels' arrow buttons, not confirmed) are in the options menu.
    Layout l;
    l.fl = "Frequency Splitter";
    l.width = 776;
    l.height = 340;
    auto& v = l.items;
    v.push_back (display (10, 6, 755, 214, "fsplit"));
    v.push_back (section (10, 223, 282, 80, nullptr, boxed));
    v.push_back (stepper (19, 243, 48, 19, "Low/mid slope", nullptr, 7));
    v.push_back (label (19, 267, 48, 12, TR_NOOP ("Slope")));
    v.push_back (knob (97, 250, 40, "Low/mid freq.", TR_NOOP ("Freq"), 285));
    v.push_back (toggle (136, 245, 30, 13, "3 bands", "3", boxed));
    v.push_back (label (127, 263, 48, 12, TR_NOOP ("Bands")));
    v.push_back (knob (202, 250, 40, "Mid/high freq.", TR_NOOP ("Freq"), 285));
    v.push_back (stepper (237, 243, 48, 19, "Mid/high slope", nullptr, 7));
    v.push_back (label (237, 267, 48, 12, TR_NOOP ("Slope")));
    v.push_back (label (14, 289, 100, 12, TR_NOOP ("Crossover"), dim | alignLeft));
    v.push_back (section (295, 223, 190, 80, nullptr, boxed));
    const char* const outs[3] { "Low level", "Mid level", "High level" };
    const char* const outMutes[3] { "Low mute", "Mid mute", "High mute" };
    const char* const sends[3] { "Low send", "Mid send", "High send" };
    const char* const sendMutes[3] { "Low send mute", "Mid send mute", "High send mute" };
    const char* const sendOut[3] { "Out: low", "Out: mid", "Out: high" };
    const char* const names[3] { TR_NOOP ("Low"), TR_NOOP ("Mid"), TR_NOOP ("High") };
    for (int b = 0; b < 3; ++b)
    {
        const auto cx = 337.0f + 59.0f * (float) b;
        auto k = knob (cx, 248, 32, outs[b], nullptr, 0, noCaption);
        k.colour = b;
        v.push_back (k);
        v.push_back (toggle (cx - 25, 268, 20, 11, outMutes[b], nullptr, inverted));
        v.push_back (label (cx - 6, 268, 30, 11, names[b], alignLeft));
    }
    v.push_back (label (299, 289, 100, 12, TR_NOOP ("Main out"), dim | alignLeft));
    v.push_back (section (490, 223, 275, 80, nullptr, boxed));
    for (int b = 0; b < 3; ++b)
    {
        const auto x0 = 497.0f + 90.0f * (float) b;
        v.push_back (toggle (x0, 241, 38, 18, sendOut[b], TR_NOOP ("Out"), boxed));
        auto k = knob (x0 + 65.0f, 248, 32, sends[b], nullptr, 0, noCaption);
        k.colour = b;
        v.push_back (k);
        v.push_back (toggle (x0 + 40.0f, 268, 20, 11, sendMutes[b], nullptr, inverted));
        v.push_back (label (x0 + 59.0f, 268, 30, 11, names[b], alignLeft));
    }
    v.push_back (label (494, 289, 100, 12, TR_NOOP ("Sends"), dim | alignLeft));
    v.push_back (toggle (12, 313, 100, 16, "FFT mode", TR_NOOP ("Linear phase")));
    v.push_back (label (114, 313, 46, 16, TR_NOOP ("Precision")));
    v.push_back (menu (162, 313, 62, 16, "FFT quality", TR_NOOP ("Lowest|Low|Medium|High|Higher|Highest"), { 0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f }));
    v.push_back (label (229, 313, 40, 16, TR_NOOP ("Speed")));
    v.push_back (slider (Item::hslider, 272, 313, 70, 16, "FFT fade", nullptr, noCaption));
    v.push_back (label (560, 306, 200, 28, "Frequency Splitter", dim));
    l.menuSwitches = { "Low solo", "Mid solo", "High solo", "Low send solo", "Mid send solo", "High send solo" };
    return l;
}

Layout fruityDelayBankLayout()
{
    // bank switches (LEDs) and the bank tabs 1…8 on top, OVERSAMPLE, DRY / WET / IN / FB; then the selected bank:
    // IN (pan, vol) · FILTER (type, sections, post, gain, cut, res) · FEEDBACK (pattern tap, tempo sync, mode
    // off / inv / norm / ping-pong, time / offset / separation faders, pan, vol) · FB FILTER · GRAIN (div, shape) ·
    // OUT (send to the next bank, pan, vol). Everything but the four globals lives in the slot state.
    constexpr auto k = EffectKind::fruityDelayBank;
    Layout l;
    l.fl = "Fruity Delay Bank";
    l.width = 583;
    l.height = 278;
    l.pages = { "1", "2", "3", "4", "5", "6", "7", "8" };
    auto& v = l.items;
    v.push_back (section (6, 4, 571, 58, nullptr, boxed));
    for (int b = 0; b < 8; ++b)
        v.push_back (toggle (17.0f + 28.0f * (float) b, 16, 13, 10, stateName (k, stateparams::bankControl (b, "On")), nullptr, boxed));
    v.push_back (pageTabs (10, 33, 222, 26, "1|2|3|4|5|6|7|8"));
    LayoutItem ovs = menu (322, 31, 70, 16, stateName (k, "Oversample"), TR_NOOP ("Off|2x|4x"), { 0.0f, 0.5f, 1.0f });
    v.push_back (ovs);
    v.push_back (label (322, 47, 70, 11, TR_NOOP ("Oversample")));
    knobCaptionAbove (v, 418, 41, 28, "Dry", TR_NOOP ("Dry"), 13);
    knobCaptionAbove (v, 463, 41, 28, "Output", TR_NOOP ("Wet"), 13);
    knobCaptionAbove (v, 507, 41, 28, "Input", TR_NOOP ("In"), 13);
    knobCaptionAbove (v, 552, 41, 28, "Feedback", TR_NOOP ("FB"), 13);

    const auto filterTypes = TR_NOOP ("Off|Low pass|Band pass|Notch|High pass|Low shelf|Peaking|High shelf");
    const std::vector<float> eighths { 0.0f, 1.0f / 7, 2.0f / 7, 3.0f / 7, 4.0f / 7, 5.0f / 7, 6.0f / 7, 1.0f };
    for (int b = 0; b < 8; ++b)
    {
        const auto p = [&] (const char* control) { return stateName (k, stateparams::bankControl (b, control)); };
        const auto add = [&] (LayoutItem i) { v.push_back (onPage (std::move (i), b)); };
        const auto dial = [&] (float cx, float cy, const char* control, const char* caption, std::uint16_t style = 0) {
            add (knob (cx, cy, 30, p (control), caption, cy + 29, style));
        };
        const auto fader = [&] (float cx, const char* control, const char* caption, std::uint16_t style = 0) {
            add (slider (Item::vslider, cx - 10, 140, 20, 108, p (control), nullptr, noCaption | style));
            add (label (cx - 18, 255, 36, 12, caption));
        };
        add (section (10, 68, 56, 205, TR_NOOP ("In")));
        dial (37, 175, "Input pan", TR_NOOP ("Pan"), bipolar);
        dial (37, 233, "Input volume", TR_NOOP ("Vol"), bipolar);

        add (section (70, 68, 107, 205, TR_NOOP ("Filter")));
        add (menu (76, 98, 46, 16, p ("Filter 1 type"), filterTypes, eighths));
        add (tabs (125, 98, 45, 16, p ("Filter 1 order"), "1|2|3"));
        add (toggle (90, 172, 16, 10, p ("Filter 1 post"), nullptr, boxed));
        add (label (74, 199, 46, 12, TR_NOOP ("Post")));
        dial (145, 175, "Filter 1 gain", TR_NOOP ("Gain"), bipolar);
        dial (97, 233, "Filter 1 cutoff", TR_NOOP ("Cut"));
        dial (149, 233, "Filter 1 resonance", TR_NOOP ("Res"));

        add (section (181, 68, 149, 205, TR_NOOP ("Feedback")));
        add (toggle (188, 88, 14, 13, p ("Tap"), "|", boxed));
        add (toggle (188, 107, 14, 13, p ("Sync"), "T", boxed));
        LayoutItem mode = radio (210, 88, 116, 32, p ("Mode"), TR_NOOP ("Off|Norm|Inv|P.pong"), { 0.0f, 1.0f / 3, 2.0f / 3, 1.0f });
        mode.columns = 2;
        add (mode);
        fader (194, "Time", TR_NOOP ("Time"));
        fader (228, "Stereo offset", TR_NOOP ("Ofs"), bipolar);
        fader (263, "Feedback cross", TR_NOOP ("Sep"), bipolar);
        dial (303, 175, "Feedback pan", TR_NOOP ("Pan"), bipolar);
        dial (303, 233, "Feedback level", TR_NOOP ("Vol"), bipolar);

        add (section (335, 68, 107, 205, TR_NOOP ("FB filter")));
        add (menu (341, 98, 46, 16, p ("Filter 2 type"), filterTypes, eighths));
        add (tabs (390, 98, 45, 16, p ("Filter 2 order"), "1|2|3"));
        dial (408, 175, "Filter 2 gain", TR_NOOP ("Gain"), bipolar);
        dial (362, 233, "Filter 2 cutoff", TR_NOOP ("Cut"));
        dial (413, 233, "Filter 2 resonance", TR_NOOP ("Res"));

        add (section (446, 68, 65, 205, TR_NOOP ("Grain")));
        fader (462, "Steps", TR_NOOP ("Div"));
        fader (495, "Shape", TR_NOOP ("Sh"), bipolar);

        add (section (516, 68, 56, 205, TR_NOOP ("Out")));
        if (b < 7)
            dial (543, 116, "Send to next", TR_NOOP ("Next"), bipolar);
        dial (543, 175, "Output pan", TR_NOOP ("Pan"), bipolar);
        dial (543, 233, "Output volume", TR_NOOP ("Vol"), bipolar);
    }
    // the solo bank (FL: right-click on a bank tab, not confirmed) and bank 8's missing send
    l.editedElsewhere = { stateName (k, "Solo"), stateName (k, stateparams::bankControl (7, "Unused")) };
    return l;
}

Layout equoLayout()
{
    // GRAPHIC EQ: the 31 bands of the selected bank and target (drag to draw), BANK M / 1…8, VOL / PAN / SEND;
    // MORPH (shift, mix, morph, smooth, vol) and SETUP (send only, add mode, oversample, bandwidth). The page is the
    // target (×1) and the bank (×3).
    Layout l;
    l.fl = "EQUO";
    l.width = 572;
    l.height = 245;
    auto& v = l.items;
    v.push_back (section (12, 10, 388, 220, nullptr, boxed | raised));
    v.push_back (label (19, 13, 150, 22, "Graphic EQ", dim | alignLeft));
    v.push_back (display (19, 40, 371, 160, "equobars"));
    v.push_back (label (19, 208, 36, 15, TR_NOOP ("Bank"), boxed));
    v.push_back (pageDigit (58, 208, 164, 15, "M|1|2|3|4|5|6|7|8", 3));
    v.push_back (pageDigit (267, 208, 123, 15, TR_NOOP ("Vol|Pan|Send"), 1));
    v.push_back (section (407, 5, 155, 130, TR_NOOP ("Morph")));
    v.push_back (knob (426, 45, 30, "Band shift", TR_NOOP ("Shift"), 71, bipolar));
    v.push_back (knob (483, 45, 30, "Band mix", TR_NOOP ("Mix"), 71, bipolar));
    v.push_back (knob (542, 45, 30, "Morph", TR_NOOP ("Morph"), 71));
    v.push_back (knob (457, 100, 30, "Smoothness", TR_NOOP ("Smooth"), 126));
    v.push_back (knob (515, 100, 30, "Volume", TR_NOOP ("Vol"), 126));
    v.push_back (section (407, 138, 155, 105, TR_NOOP ("Setup")));
    v.push_back (toggle (410, 160, 84, 12, "Send only", TR_NOOP ("Send only")));
    // the send's track (round 173d3, FL shot: a "---" box right of SEND ONLY; right-click lists the tracks the insert routes to)
    v.push_back (label (496, 160, 32, 12, "SEND"));
    v.push_back (display (529, 156, 33, 20, "equosend"));
    v.push_back (toggle (410, 205, 100, 12, "Add mode", TR_NOOP ("Add mode")));
    v.push_back (toggle (410, 224, 100, 12, "Oversample", TR_NOOP ("Oversample")));
    v.push_back (knob (539, 200, 28, "Bandwidth", TR_NOOP ("BW"), 229));
    // the band cells are drawn in the graph (core/StateParams.h: "Bank 1 volume 1" …)
    for (const auto& p : stateparams::tableFor (EffectKind::equo)->params)
        l.editedElsewhere.push_back (p.name.c_str());
    return l;
}

Layout multibandDelayLayout()
{
    // the 16 bands of the selected bank and target (drag to draw) with their frequencies; DELAY / VOL / PAN, BANK 1…8;
    // crossover mode · SCALE (×0.1, harmonic, weighted), FEEDBACK · SMOOTHING TIME, KEEP PITCH · MORPH · WET (comp / sat /
    // clip), HP · MIX. The selected target and bank are FL's (saved in the state).
    constexpr auto k = EffectKind::multibandDelay;
    Layout l;
    l.fl = "Multiband Delay";
    l.width = 568;
    l.height = 292;
    auto& v = l.items;
    v.push_back (display (11, 8, 544, 180, "mbdbars"));
    v.push_back (tabs (93, 195, 143, 15, stateName (k, "Target"), TR_NOOP ("Delay|Vol|Pan")));
    v.push_back (label (245, 195, 35, 15, TR_NOOP ("Bank")));
    v.push_back (tabs (282, 195, 134, 15, stateName (k, "Bank"), "1|2|3|4|5|6|7|8"));
    v.push_back (label (432, 192, 123, 20, "Multiband Delay", dim));
    v.push_back (radio (11, 226, 80, 44, "Filter mode", TR_NOOP ("Gentle|Steep|Linear phase")));
    v.push_back (section (92, 222, 143, 50, nullptr, boxed | raised));
    v.push_back (knob (118, 247, 32, "Delay scale", nullptr, 0, noCaption | bipolar));
    v.push_back (toggle (145, 230, 40, 11, "Delay x0.1", TR_NOOP ("x 0.1")));
    v.push_back (toggle (145, 254, 16, 14, "Harmonic scale", "H", boxed));
    v.push_back (toggle (167, 254, 16, 14, "Weighting", "W", boxed));
    v.push_back (knob (209, 247, 32, "Feedback", nullptr, 0, noCaption));
    v.push_back (label (94, 275, 48, 12, TR_NOOP ("Scale")));
    v.push_back (label (182, 275, 54, 12, TR_NOOP ("Feedback")));
    v.push_back (section (241, 222, 116, 50, nullptr, boxed | raised));
    v.push_back (knob (267, 247, 32, "Smoothing", nullptr, 0, noCaption));
    v.push_back (toggle (290, 230, 66, 11, "Keep pitch", TR_NOOP ("Keep pitch")));
    v.push_back (label (241, 275, 116, 12, TR_NOOP ("Smoothing time")));
    v.push_back (section (363, 222, 50, 50, nullptr, boxed | raised));
    v.push_back (knob (388, 247, 32, "Morph", nullptr, 0, noCaption));
    v.push_back (label (363, 275, 50, 12, TR_NOOP ("Morph")));
    v.push_back (section (420, 222, 90, 50, nullptr, boxed | raised));
    v.push_back (radio (424, 226, 42, 44, "Saturation", TR_NOOP ("Comp|Sat|Clip")));
    v.push_back (knob (488, 247, 28, "High pass", nullptr, 0, noCaption));
    v.push_back (label (420, 275, 46, 12, TR_NOOP ("Wet")));
    v.push_back (label (470, 275, 36, 12, TR_NOOP ("HP")));
    v.push_back (knob (536, 247, 34, "Mix", nullptr, 0, noCaption | bipolar));
    v.push_back (label (514, 275, 44, 12, TR_NOOP ("Mix")));
    for (int i = 0; i < mbdelay::numBanks * 48; ++i)
        l.editedElsewhere.push_back (stateparams::tableFor (k)->name (i));
    return l;
}

Layout convolverLayout()
{
    // dry, in (input separation), wet, wet separation · delay, self-conv, stretch, eq (the state's knobs); NORMALIZE;
    // the impulse response's waveform with its rate, format and length on the impulse page; the equalizer page draws and
    // edits the EQ curve over FL's frequency axis (round 173d3, FL shot: the "impulse | equalizer" switch under the
    // logo). FL's sample editor tools are not reproduced.
    constexpr auto k = EffectKind::convolver;
    Layout l;
    l.fl = "Fruity Convolver";
    l.width = 722;
    l.height = 300;
    auto& v = l.items;
    v.push_back (section (2, 2, 718, 96, nullptr, boxed));
    v.push_back (knob (41, 32, 40, "Dry", TR_NOOP ("Dry"), 68));
    v.push_back (knob (112, 36, 28, "Input separation", TR_NOOP ("In"), 68, bipolar));
    v.push_back (knob (162, 32, 40, "Wet", TR_NOOP ("Wet"), 68));
    v.push_back (knob (211, 36, 28, "Wet separation", TR_NOOP ("Wet"), 68, bipolar));
    v.push_back (knob (281, 32, 40, stateName (k, "Predelay"), TR_NOOP ("Delay"), 68));
    v.push_back (knob (340, 32, 40, stateName (k, "Self-conv"), TR_NOOP ("Self-conv"), 68));
    v.push_back (knob (397, 32, 40, stateName (k, "Stretch"), TR_NOOP ("Stretch"), 68, bipolar));
    v.push_back (knob (456, 32, 40, stateName (k, "EQ"), TR_NOOP ("EQ"), 68));
    v.push_back (toggle (622, 8, 90, 12, stateName (k, "Normalize"), TR_NOOP ("Normalize")));
    v.push_back (label (560, 28, 150, 40, "Fruity Convolver", dim));
    v.push_back (pageTabs (540, 76, 170, 18, TR_NOOP ("Impulse|Equalizer")));
    v.push_back (onPage (display (5, 104, 712, 20, "irinfo"), 0));
    v.push_back (onPage (display (5, 128, 712, 168, "irwave"), 0));
    v.push_back (onPage (display (5, 104, 712, 192, "cvcurve"), 1));
    return l;
}

Layout pitchShifterLayout()
{
    // VOICE / MUSIC tabs (the engine: PSOLA or granular) along the left, the pitch graph; then the page's controls —
    // VOICE: PITCH, FORMANT faders, FORMANT CORRECTION and PITCH ANALYSIS (pitch mode, detector, channel); MUSIC: PITCH,
    // DURATION faders, DENSITY, JITTER, RAND —, the DELAY panel (delay and feedback faders, T, OFF / POST / PRE /
    // NO SHIFT) and MIX. Value plates above the faders and knobs, as FL's read-outs.
    Layout l;
    l.fl = "Pitch Shifter";
    l.width = 744;
    l.height = 280;
    auto& v = l.items;
    const auto voice = [&] (LayoutItem i) { v.push_back (when (std::move (i), "Granular", 0.0f, 0.5f)); };
    const auto music = [&] (LayoutItem i) { v.push_back (when (std::move (i), "Granular", 0.5f, 1.0f)); };
    const auto fader = [&] (float cx, float bottom, const char* param, const char* caption, std::uint16_t style, auto add) {
        add (plate (cx - 21, 17, 42, 18, param));
        add (slider (Item::vslider, cx - 10, 46, 20, bottom - 46, param, nullptr, noCaption | style));
        add (label (cx - 30, 250, 60, 12, caption));
    };
    const auto always = [&] (LayoutItem i) { v.push_back (std::move (i)); };
    v.push_back (tabs (8, 8, 30, 264, "Granular", TR_NOOP ("Voice|Music")));
    v.push_back (section (40, 5, 260, 270, nullptr, boxed));
    voice (display (44, 9, 252, 230, "xy", "Formant", "Pitch"));
    music (display (44, 9, 252, 230, "psgraph"));
    v.push_back (label (48, 244, 160, 26, "Pitch Shifter", dim | alignLeft));
    v.push_back (section (304, 8, 120, 262, nullptr, boxed));
    fader (333, 236, "Pitch", TR_NOOP ("Pitch"), bipolar, always);
    fader (391, 236, "Formant", TR_NOOP ("Formant"), bipolar, voice);
    fader (391, 236, "Duration", TR_NOOP ("Duration"), 0, music);
    v.push_back (section (427, 8, 119, 262, nullptr, boxed));
    voice (plate (465, 17, 42, 18, "Formant correction"));
    voice (knob (486, 75, 44, "Formant correction", TR_NOOP ("Formant correction"), 117));
    voice (menu (447, 156, 80, 16, "Pitch mode", TR_NOOP ("Relative|C4|C5|C6"), { 0.0f, 1.0f / 3, 2.0f / 3, 1.0f }));
    voice (menu (447, 189, 80, 16, "Accurate detector", TR_NOOP ("Fast|Accurate")));
    voice (menu (447, 222, 80, 16, "Channel", TR_NOOP ("Left|Right|Left + right")));
    voice (label (432, 250, 110, 12, TR_NOOP ("Pitch analysis")));
    music (plate (465, 17, 42, 18, "Density"));
    music (knob (486, 75, 44, "Density", TR_NOOP ("Density"), 117));
    music (plate (436, 171, 42, 18, "Jitter"));
    music (knob (457, 218, 28, "Jitter", TR_NOOP ("Jitter"), 255));
    music (plate (494, 171, 42, 18, "Random"));
    music (knob (515, 218, 28, "Random", TR_NOOP ("Rand"), 255));
    v.push_back (section (549, 8, 123, 262, nullptr, boxed));
    v.push_back (plate (558, 17, 46, 18, "Delay"));
    v.push_back (slider (Item::vslider, 570, 46, 20, 106, "Delay", nullptr, noCaption));
    v.push_back (plate (617, 17, 42, 18, "Feedback"));
    v.push_back (slider (Item::vslider, 628, 46, 20, 106, "Feedback", nullptr, noCaption));
    v.push_back (label (556, 160, 36, 12, TR_NOOP ("Delay")));
    v.push_back (toggle (594, 160, 12, 12, "Tempo sync", "T", boxed));
    v.push_back (label (609, 160, 58, 12, TR_NOOP ("Feedback")));
    v.push_back (radio (588, 180, 70, 60, "Delay mode", TR_NOOP ("Off|Post|Pre|No shift"), { 0.0f, 1.0f / 3, 2.0f / 3, 1.0f }));
    v.push_back (label (556, 250, 110, 12, TR_NOOP ("Delay mode")));
    v.push_back (section (675, 8, 63, 262, nullptr, boxed));
    fader (706, 236, "Mix", TR_NOOP ("Mix"), 0, always);
    return l;
}

Layout effectorLayout()
{
    // input / output gain on the title strip; X PARAM, Y PARAM, BYPASS, DRY/WET at the left; the X/Y pad; the tempo
    // divisions; X MOD, Y MOD, the LFO shape and TEMPO at the right; the twelve effect buttons along the bottom
    Layout l;
    l.fl = "Effector";
    l.width = 436;
    l.height = 412;
    auto& v = l.items;
    v.push_back (section (5, 3, 426, 50, nullptr, boxed));
    v.push_back (knob (37, 23, 26, "Input", TR_NOOP ("Gain"), 46));
    v.push_back (knob (399, 23, 26, "Output", TR_NOOP ("Gain"), 46, bipolar));
    v.push_back (label (130, 8, 176, 38, "Effector", dim));
    v.push_back (section (5, 60, 62, 258, nullptr, boxed));
    v.push_back (knob (37, 91, 30, "X", TR_NOOP ("X param"), 116));
    v.push_back (knob (37, 153, 30, "Y", TR_NOOP ("Y param"), 178));
    v.push_back (toggle (27, 205, 20, 20, "Bypass", nullptr, boxed));
    v.push_back (label (10, 232, 54, 12, TR_NOOP ("Bypass")));
    v.push_back (knob (37, 280, 30, "Wet", TR_NOOP ("Dry/wet"), 305));
    v.push_back (display (72, 60, 258, 258, "xy", "X", "Y"));
    v.push_back (radio (334, 62, 30, 254, "Mod rate", "8/1|4/1|2/1|1/1|3/4|1/2|3/8|1/4|1/8",
                        { 1.0f, 7.0f / 8, 6.0f / 8, 5.0f / 8, 4.0f / 8, 3.0f / 8, 2.0f / 8, 1.0f / 8, 0.0f }));
    v.push_back (section (369, 60, 62, 258, nullptr, boxed));
    v.push_back (knob (399, 91, 30, "Mod X", TR_NOOP ("X mod"), 116, bipolar));
    v.push_back (knob (399, 153, 30, "Mod Y", TR_NOOP ("Y mod"), 178, bipolar));
    v.push_back (radio (382, 196, 34, 54, "LFO shape", TR_NOOP ("Sine|Saw|Square")));
    v.push_back (knob (399, 280, 30, "Tempo red.", TR_NOOP ("Tempo"), 305));
    LayoutItem type = radio (8, 326, 420, 80, "Type", TR_NOOP ("Distort|Lo-fi|Flanger|Phaser|Filter|Delay|Reverb|Stereo|Trans|Grain|Vox|Ring"));
    type.columns = 6;
    v.push_back (type);
    return l;
}

Layout hardcoreLayout()
{
    // INPUT, OUTPUT; EQ on and its eight band faders (125 Hz … 16 kHz); CABINET; five pedal boxes, each with its LED
    // (box on), the knobs of its pedal (captions per pedal type, the knobs a pedal does not use are hidden) and the
    // pedal type under it. FL's preset browser (.hdprg) is not reproduced.
    Layout l;
    l.fl = "Hardcore";
    l.width = 772;
    l.height = 475;
    auto& v = l.items;
    v.push_back (section (5, 5, 762, 57, nullptr, boxed));
    v.push_back (label (17, 9, 150, 40, "Hardcore", dim | alignLeft));
    v.push_back (knob (183, 29, 26, "Input level", nullptr, 0, noCaption | bipolar));
    v.push_back (label (199, 14, 50, 12, TR_NOOP ("Input"), alignLeft));
    v.push_back (value (199, 32, 60, 14, "Input level"));
    v.push_back (knob (273, 29, 26, "Output level", nullptr, 0, noCaption | bipolar));
    v.push_back (label (289, 14, 50, 12, TR_NOOP ("Output"), alignLeft));
    v.push_back (value (289, 32, 60, 14, "Output level"));
    v.push_back (section (362, 8, 262, 50, nullptr, boxed | raised));
    v.push_back (label (374, 12, 30, 12, TR_NOOP ("EQ")));
    v.push_back (toggle (380, 30, 16, 16, "EQ on", nullptr, boxed));
    static const char* const bands[8] { "125", "250", "500", "1k", "2k", "4k", "8k", "16k" };
    static const char* const bandParams[8] { "EQ band 1", "EQ band 2", "EQ band 3", "EQ band 4", "EQ band 5", "EQ band 6", "EQ band 7", "EQ band 8" };
    for (int b = 0; b < 8; ++b)
    {
        const auto x = 410.0f + 16.0f * (float) b;
        v.push_back (label (x - 1, 11, 16, 10, bands[b]));
        v.push_back (slider (Item::vslider, x, 22, 14, 34, bandParams[b], nullptr, noCaption | bipolar));
    }
    v.push_back (label (545, 12, 70, 12, TR_NOOP ("Cabinet")));
    v.push_back (menu (545, 28, 70, 18, "Cabinet",
                       "None|Model A|Model B|Model C|Model D|Model E|Model F|Model G|Model H|Model I|Model J|Model K|Model L|Model M|Model N|Model O|Model P|Model Q|Model R|Model S|Model T"));

    // pedal types 1…11 and the captions of their knobs (FL's pedal artwork); 3-knob pedals put the third one in the middle
    struct Pedal
    {
        const char* name;
        const char* knobs[4];
    };
    static const Pedal pedals[12] { { "", {} },
                                    { TR_NOOP ("Distortion"), { TR_NOOP ("Dist"), TR_NOOP ("Comb"), TR_NOOP ("Inferno"), TR_NOOP ("Glass") } },
                                    { TR_NOOP ("Chorus"), { TR_NOOP ("Width"), TR_NOOP ("Speed"), TR_NOOP ("Intensity"), nullptr } },
                                    { TR_NOOP ("Flanger"), { TR_NOOP ("Width"), TR_NOOP ("Speed"), TR_NOOP ("Intensity"), nullptr } },
                                    { TR_NOOP ("Phaser"), { TR_NOOP ("Speed"), TR_NOOP ("Tune"), TR_NOOP ("Width"), TR_NOOP ("Intensity") } },
                                    { TR_NOOP ("Reverb"), { TR_NOOP ("Decay"), TR_NOOP ("Hi-damp"), TR_NOOP ("Mix"), nullptr } },
                                    { TR_NOOP ("Delay"), { TR_NOOP ("Time"), TR_NOOP ("Decay"), TR_NOOP ("Mix"), nullptr } },
                                    { TR_NOOP ("Noise gate"), { TR_NOOP ("Attack"), TR_NOOP ("Release"), TR_NOOP ("Threshold"), nullptr } },
                                    { TR_NOOP ("Equalizer"), { TR_NOOP ("Low"), TR_NOOP ("Middle"), TR_NOOP ("Treble"), TR_NOOP ("High") } },
                                    { TR_NOOP ("Compressor"), { TR_NOOP ("Threshold"), TR_NOOP ("Ratio"), TR_NOOP ("Attack"), TR_NOOP ("Release") } },
                                    { TR_NOOP ("Modulator"), { TR_NOOP ("Pitch"), TR_NOOP ("Amp"), TR_NOOP ("Pan"), TR_NOOP ("Rate") } },
                                    { TR_NOOP ("Tube drive"), { TR_NOOP ("Low"), TR_NOOP ("High"), TR_NOOP ("Gain"), nullptr } } };
    static const char* const typeParams[5] { "Box 1 pedal", "Box 2 pedal", "Box 3 pedal", "Box 4 pedal", "Box 5 pedal" };
    static const char* const onParams[5] { "Box 1 on", "Box 2 on", "Box 3 on", "Box 4 on", "Box 5 on" };
    static const char* const knobParams[5][5] {
        { "Box 1 knob 1", "Box 1 knob 2", "Box 1 knob 3", "Box 1 knob 4", "Box 1 knob 5" },
        { "Box 2 knob 1", "Box 2 knob 2", "Box 2 knob 3", "Box 2 knob 4", "Box 2 knob 5" },
        { "Box 3 knob 1", "Box 3 knob 2", "Box 3 knob 3", "Box 3 knob 4", "Box 3 knob 5" },
        { "Box 4 knob 1", "Box 4 knob 2", "Box 4 knob 3", "Box 4 knob 4", "Box 4 knob 5" },
        { "Box 5 knob 1", "Box 5 knob 2", "Box 5 knob 3", "Box 5 knob 4", "Box 5 knob 5" } };
    for (int box = 0; box < 5; ++box)
    {
        const auto x0 = 22.0f + 147.0f * (float) box;
        v.push_back (section (x0, 150, 130, 270, nullptr, boxed));
        v.push_back (section (x0 + 8, 175, 114, 128, nullptr, boxed | raised));
        v.push_back (toggle (x0 + 61, 226, 14, 14, onParams[box], nullptr, boxed));
        for (int t = 1; t <= 11; ++t)
        {
            const auto lo = ((float) t - 0.499f) / 11.0f, hi = ((float) t + 0.501f) / 11.0f; // as the engine rounds a saved 0.5
            const auto& p = pedals[t];
            const auto three = p.knobs[3] == nullptr;
            for (int k = 0; k < (three ? 3 : 4); ++k)
            {
                const auto cx = three && k == 2 ? x0 + 68.0f : x0 + 37.0f + 60.0f * (float) (k % 2);
                const auto cy = k < 2 ? 203.0f : (three ? 257.0f : 255.0f);
                v.push_back (when (knob (cx, cy, 26, knobParams[box][k], p.knobs[k], cy + 20), typeParams[box], lo, hi));
            }
            v.push_back (when (label (x0 + 10, 282, 110, 16, p.name), typeParams[box], lo, hi));
        }
        v.push_back (menu (x0 + 3, 453, 127, 16, typeParams[box],
                           TR_NOOP ("(empty)|Distortion|Chorus|Flanger|Phaser|Reverb|Delay|Noise gate|Equalizer|Compressor|Modulator|Tube drive")));
        l.editedElsewhere.push_back (knobParams[box][4]); // no pedal has a fifth knob
    }
    l.editedElsewhere.push_back ("(unused 12)");
    l.editedElsewhere.push_back ("(unused 38)");
    l.editedElsewhere.push_back ("(unused 44)");
    return l;
}

Layout vocodexLayout()
{
    // Mixer (WET, SG, MOD, CAR, +NOISE faders, the three filters, MOD / CAR inputs, L-R ENCODING), Contour (from car /
    // consonants, to mod / noise), DRAFT, THREADED; the envelope follower (HOLD, ATT, REL, their curves, PEAK / RMS,
    // MIN TIMES, freeze), ORDER, the band and unison knobs, the number of bands; then FL's map editor (round 173d3: the
    // map dropdown over the 13 maps, the page = the map shown; the carrier synthesizer's patch browser is not
    // reproduced). All controls live in the slot state.
    constexpr auto k = EffectKind::vocodex;
    const auto p = [] (const char* name) { return stateName (EffectKind::vocodex, name); };
    Layout l;
    l.fl = "Vocodex";
    l.width = 598;
    l.height = 600;
    auto& v = l.items;
    v.push_back (section (5, 3, 588, 140, nullptr, boxed | raised));
    v.push_back (label (14, 8, 60, 18, TR_NOOP ("Mixer"), alignLeft));
    v.push_back (label (40, 30, 54, 11, TR_NOOP ("Wet")));
    v.push_back (knob (52, 84, 74, p ("Wet"), nullptr, 0, noCaption | big));
    const auto fader = [&] (float cx, float top, const char* param, const char* caption, float captionY) {
        v.push_back (slider (Item::vslider, cx - 9, top, 18, 112 - top, param, nullptr, noCaption));
        v.push_back (label (cx - 22, captionY, 44, 11, caption));
    };
    fader (113, 42, p ("SG"), TR_NOOP ("SG"), 30);
    fader (157, 42, p ("Mod level"), TR_NOOP ("Mod"), 30);
    fader (186, 42, p ("Car level"), TR_NOOP ("Car"), 30);
    fader (215, 54, p ("Noise"), TR_NOOP ("+Noise"), 42);
    v.push_back (knob (148, 128, 20, p ("Mod HP"), nullptr, 0, noCaption));
    v.push_back (label (122, 122, 18, 12, TR_NOOP ("HP")));
    v.push_back (knob (183, 128, 20, p ("Car LP"), nullptr, 0, noCaption));
    v.push_back (knob (215, 128, 20, p ("Noise HP"), nullptr, 0, noCaption));
    v.push_back (label (228, 122, 18, 12, TR_NOOP ("HP")));
    v.push_back (stepper (142, 8, 32, 17, p ("Mod input"), nullptr, 8));
    v.push_back (stepper (180, 8, 32, 17, p ("Car input"), nullptr, 9));
    v.push_back (toggle (222, 8, 100, 13, p ("L-R encoding"), TR_NOOP ("L-R encoding")));
    v.push_back (label (262, 62, 76, 16, TR_NOOP ("Contour")));
    v.push_back (toggle (280, 82, 110, 12, p ("From car"), TR_NOOP ("Car")));
    v.push_back (toggle (280, 97, 110, 12, p ("From consonants"), TR_NOOP ("Consonants")));
    v.push_back (toggle (280, 111, 110, 12, p ("To mod"), TR_NOOP ("Mod")));
    v.push_back (toggle (280, 125, 110, 12, p ("To noise"), TR_NOOP ("Noise")));
    v.push_back (label (246, 97, 34, 12, TR_NOOP ("From")));
    v.push_back (label (246, 111, 34, 12, TR_NOOP ("To")));
    v.push_back (toggle (428, 8, 56, 13, p ("Draft"), TR_NOOP ("Draft")));
    v.push_back (toggle (486, 8, 72, 13, p ("Threaded"), TR_NOOP ("Threaded")));
    v.push_back (label (400, 60, 186, 50, "Vocodex", dim));

    v.push_back (section (5, 147, 588, 135, nullptr, boxed));
    v.push_back (knob (47, 180, 36, p ("Hold"), TR_NOOP ("Hold"), 208));
    v.push_back (knob (104, 180, 36, p ("Attack"), TR_NOOP ("Att"), 208));
    v.push_back (knob (159, 180, 36, p ("Release"), TR_NOOP ("Rel"), 208));
    v.push_back (toggle (196, 172, 70, 13, p ("Min times"), TR_NOOP ("Min times")));
    v.push_back (radio (14, 220, 46, 30, p ("Hold mode"), TR_NOOP ("Peak|RMS")));
    v.push_back (knob (108, 234, 20, p ("Attack curve"), nullptr, 0, noCaption));
    v.push_back (knob (163, 234, 20, p ("Release curve"), nullptr, 0, noCaption));
    v.push_back (label (10, 262, 116, 16, TR_NOOP ("Envelope follower"), alignLeft));
    v.push_back (toggle (130, 262, 18, 16, p ("Freeze"), "*", boxed));
    v.push_back (tabs (281, 213, 56, 9, p ("Order"), "1|2|3|4"));
    v.push_back (label (281, 225, 56, 11, TR_NOOP ("Order")));
    v.push_back (knob (378, 178, 36, p ("Band width"), TR_NOOP ("Width"), 207));
    v.push_back (knob (435, 178, 28, p ("Mod band width"), nullptr, 0, noCaption));
    v.push_back (label (410, 201, 50, 12, TR_NOOP ("Mod width")));
    v.push_back (knob (500, 178, 36, p ("Mod pitch"), TR_NOOP ("Mod pitch"), 207, bipolar));
    v.push_back (knob (560, 178, 36, p ("Unison"), TR_NOOP ("Unison"), 207, 0, 4));
    v.push_back (knob (379, 236, 22, p ("Flatness"), nullptr, 0, noCaption));
    v.push_back (label (354, 250, 50, 12, TR_NOOP ("Flatness")));
    v.push_back (knob (510, 236, 22, p ("Unison pitch"), nullptr, 0, noCaption));
    v.push_back (label (485, 250, 50, 12, TR_NOOP ("Uni. pitch")));
    v.push_back (knob (560, 236, 22, p ("Unison pan"), nullptr, 0, noCaption));
    v.push_back (label (537, 250, 48, 12, TR_NOOP ("Uni. pan")));
    v.push_back (stepper (266, 262, 36, 17, p ("Bands"), nullptr, 95));
    v.push_back (label (306, 262, 130, 17, TR_NOOP ("Bands - distribution"), alignLeft));
    v.push_back (display (10, 288, 160, 17, "vxmapsel"));
    v.push_back (display (10, 308, 578, 260, "vxcurve"));
    v.push_back (display (10, 574, 578, 22, "vcxcarrier"));
    l.menuSwitches = { p ("Subtractive gate") };
    (void) k;
    return l;
}
