// Round 169h: Toxic Biohazard's MIDI and SEQ pages (docs/analise/REA_rodada169h-toxic.md) — the step sequencer
// (FUN_18002ed20 pattern, FUN_18001bbb0 clock), the modulation slots' MIDI sources, sustain and the CC link map.

namespace toxic169h
{
std::vector<std::uint8_t> state()
{
    return lynx::toxicDefaultState();
}

void set (std::vector<std::uint8_t>& s, int index, float raw)
{
    const auto at = lynx::toxicParamOffset (s, index);
    CHECK (at >= 0);
    if (at >= 0)
        std::memcpy (s.data() + at, &raw, 4);
}

// a sequencer value in FL's units through the parameter's own law
void setUnit (std::vector<std::uint8_t>& s, int index, int unit)
{
    const auto* p = lynx::findEngineParam (lynx::toxicParams(), index);
    CHECK (p != nullptr && p->hasUnits());
    if (p != nullptr)
        set (s, index, p->rawOfUnit (unit));
}

// Seq Play on, `steps` steps with these values
std::vector<std::uint8_t> pattern (std::initializer_list<int> values, int steps)
{
    auto s = state();
    set (s, 157, 1.0f);
    setUnit (s, 162, steps);
    int i = 0;
    for (auto v : values)
        setUnit (s, 163 + i++, v);
    return s;
}

lynx::toxic::RefNote held (int key, int start = 0, int release = -1)
{
    lynx::toxic::RefNote n;
    n.key = key, n.start = start, n.release = release;
    return n;
}

std::vector<int> onsets (const std::vector<lynx::toxic::SeqEvent>& ev, int key)
{
    std::vector<int> out;
    for (const auto& e : ev)
        if (e.key == key && e.velocity > 0.0f)
            out.push_back ((int) e.sample);
    return out;
}

std::vector<int> playedKeys (const std::vector<lynx::toxic::SeqEvent>& ev)
{
    std::vector<int> out;
    for (const auto& e : ev)
        if (e.velocity > 0.0f)
            out.push_back (e.key);
    return out;
}
} // namespace toxic169h

TEST (toxic169h_parameter_units)
{
    using namespace lynx;
    const auto* step = findEngineParam (toxicParams(), 163);
    CHECK (step != nullptr && step->hasUnits() && step->unitLow == -26 && step->unitHigh == 24);
    for (int u = -26; u <= 24; ++u) // every unit survives the engine's truncation (FUN_18003acb0)
        CHECK (step->unitOf (step->rawOfUnit (u)) == u);
    CHECK (step->unitString (-26) == "---" && step->unitString (-25) == "SUS" && step->unitString (7) == "7");
    const auto* steps = findEngineParam (toxicParams(), 162);
    CHECK (steps != nullptr && steps->unitOf (0.25f) == 16); // the constructor's 0.25 = 16 steps
    const auto* amount = findEngineParam (toxicParams(), 332);
    CHECK (amount != nullptr && amount->unitOf (0.5f) == 0 && amount->unitOf (1.0f) == 100 && amount->unitOf (0.0f) == -100);
    CHECK (findEngineParam (toxicParams(), 316)->high == 134.0f && findEngineParam (toxicParams(), 331)->high == 294.0f);
    CHECK (toxicModSourceName (8) == "CC 1" && toxicModSourceName (4) == "Aftertouch" && toxicModSourceName (0) == "---");
}

TEST (toxic169h_sequencer_steps_and_clock)
{
    using namespace toxic169h;
    // one key (C5 = 60): no chord, identity scale; 0 = the key, 7 = a fifth up, 12 = an octave up, −26 = rest
    const auto s = pattern ({ 0, 7, 12, -26 }, 4);
    const auto ev = lynx::toxic::sequencerEvents (s, { held (60) }, {}, 44100.0, 30000, 125.0);
    // a 16th note (15/125 s = 5292 samples) quantised to the engine's 64-sample chunks
    CHECK ((onsets (ev, 60) == std::vector<int> { 0, 21184 }));
    CHECK ((onsets (ev, 67) == std::vector<int> { 5312 }));
    CHECK ((onsets (ev, 72) == std::vector<int> { 10624 }));
    // the rest stops the lane's note and plays nothing
    bool released72 = false;
    for (const auto& e : ev)
        released72 = released72 || (e.key == 72 && e.velocity == 0.0f && e.sample == 15936);
    CHECK (released72);
}

TEST (toxic169h_sequencer_swing_and_modes)
{
    using namespace toxic169h;
    auto s = pattern ({ 0, 1, 2 }, 3);
    set (s, 158, 1.0f); // swing: the steps last 1.25 and 0.75 sixteenths in turn
    auto ev = lynx::toxic::sequencerEvents (s, { held (60) }, {}, 44100.0, 22000, 125.0);
    CHECK ((onsets (ev, 61) == std::vector<int> { 6656 }));
    CHECK ((onsets (ev, 62) == std::vector<int> { 10624 }));
    CHECK ((onsets (ev, 60) == std::vector<int> { 0, 17216 }));

    s = pattern ({ 0, 1, 2 }, 3);
    set (s, 159, 1.0f); // ping-pong: 0 1 2 1 0 1 2 1 0
    ev = lynx::toxic::sequencerEvents (s, { held (60) }, {}, 44100.0, 47680, 125.0); // (9 steps)
    CHECK ((playedKeys (ev) == std::vector<int> { 60, 61, 62, 61, 60, 61, 62, 61, 60 }));

    s = pattern ({ 0, 1, 2 }, 3);
    set (s, 160, 1.0f); // random: steps drawn from the generator, all within the pattern
    ev = lynx::toxic::sequencerEvents (s, { held (60) }, {}, 44100.0, 20 * 5312, 125.0);
    const auto keys = playedKeys (ev);
    CHECK (keys.size() >= 19);
    bool varied = false;
    for (auto k : keys)
    {
        CHECK (k >= 60 && k <= 62);
        varied = varied || k != keys.front();
    }
    CHECK (varied);
}

TEST (toxic169h_sequencer_chords_dual_and_stop)
{
    using namespace toxic169h;
    // C major held (60 64 67): the major scale from the root, degree 3 → E, 8 → A
    auto s = pattern ({ 3, 8, 1 }, 3);
    auto ev = lynx::toxic::sequencerEvents (s, { held (60), held (64), held (67) }, {}, 44100.0, 3 * 5312 - 100, 125.0);
    CHECK ((playedKeys (ev) == std::vector<int> { 64, 69, 61 }));
    // A minor held (57 60 64): the minor scale from A, degree 0 → A, 3 → C, 4 → C (FL's table folds 4 onto 3)
    s = pattern ({ 0, 3, 4 }, 3);
    ev = lynx::toxic::sequencerEvents (s, { held (57), held (60), held (64) }, {}, 44100.0, 3 * 5312 - 100, 125.0);
    CHECK ((playedKeys (ev) == std::vector<int> { 57, 60, 60 }));

    // dual: step k also plays step k + 32 (lane 2)
    s = pattern ({ 0 }, 1);
    set (s, 161, 1.0f);
    setUnit (s, 163 + 32, 7);
    ev = lynx::toxic::sequencerEvents (s, { held (60) }, {}, 44100.0, 64, 125.0);
    CHECK ((playedKeys (ev) == std::vector<int> { 60, 67 }));

    // letting the key go stops the sequencer: its notes end at once, nothing plays after
    s = pattern ({ 0, 2 }, 2);
    ev = lynx::toxic::sequencerEvents (s, { held (60, 0, 7000) }, {}, 44100.0, 30000, 125.0);
    CHECK ((playedKeys (ev) == std::vector<int> { 60, 62 }));
    CHECK (! ev.empty() && ev.back().key == 62 && ev.back().velocity == 0.0f && ev.back().sample == 6976);
}

TEST (toxic169h_seq_off_plays_keys_and_sustain)
{
    using namespace toxic169h;
    // Seq Play off: the keys play the synth directly
    auto ev = lynx::toxic::sequencerEvents (state(), { held (60, 0, 640) }, {}, 44100.0, 2000, 125.0);
    CHECK (ev.size() == 2 && ev[0].key == 60 && ev[0].velocity > 0.0f && ev[1].velocity == 0.0f && ev[1].sample == 640);
    // the sustain pedal (CC64) keeps the key until it goes up
    ev = lynx::toxic::sequencerEvents (state(), { held (60, 0, 640) }, { { 0, 0xB0, 64, 127 }, { 1280, 0xB0, 64, 0 } }, 44100.0, 2000, 125.0);
    CHECK (ev.size() == 2 && ev[1].velocity == 0.0f && ev[1].sample == 1280);
    // all notes off (CC123) lets every key go, even under the pedal
    ev = lynx::toxic::sequencerEvents (state(), { held (60) }, { { 0, 0xB0, 64, 127 }, { 640, 0xB0, 123, 0 } }, 44100.0, 2000, 125.0);
    int offs60 = 0;
    for (const auto& e : ev)
        if (e.key == 60 && e.velocity == 0.0f)
            offs60 += e.sample == 640 ? 1 : 100;
    CHECK (offs60 == 1);
}

TEST (toxic169h_modulation_slot_midi_sources)
{
    using namespace toxic169h;
    auto s = state();
    set (s, 316, 8.0f);             // slot 1 source: CC 1 (7 + 1)
    set (s, 324, 16.0f);            // destination: parameter 15 (Cutoff), stored 1 + index
    setUnit (s, 332, 50);           // amount ≈ +0.5
    set (s, 15, 0.25f);
    const auto x = lynx::findEngineParam (lynx::toxicParams(), 332)->rawOfUnit (50);
    const auto amt = (float) ((double) std::clamp ((int) (199.0f * x + -99.0f), -99, 100) * 0.01); // the engine's acb(x, −99, 100)/100
    CHECK (std::fabs (lynx::toxic::modulatedParam (s, {}, 15) - 0.25f) < 1e-6f); // CC 1 starts at 0
    CHECK (std::fabs (lynx::toxic::modulatedParam (s, { { 0, 0xB0, 1, 127 } }, 15) - (0.25f + amt)) < 1e-6f);
    CHECK (std::fabs (lynx::toxic::modulatedParam (s, { { 0, 0xB0, 1, 64 } }, 15) - (64.0f * 0.00787401572f * amt + 0.25f)) < 1e-6f);
    // reset all controllers (CC121) brings CC 1 back to 0
    CHECK (std::fabs (lynx::toxic::modulatedParam (s, { { 0, 0xB0, 1, 127 }, { 0, 0xB0, 121, 0 } }, 15) - 0.25f) < 1e-6f);
    // channel pressure as the source
    set (s, 316, 4.0f);
    CHECK (std::fabs (lynx::toxic::modulatedParam (s, { { 0, 0xD0, 127, 0 } }, 15) - (0.25f + amt)) < 1e-6f);
    // CC 7 is not forwarded by FL's wrapper: its source keeps the constructor's 1.0
    set (s, 316, 14.0f);
    CHECK (std::fabs (lynx::toxic::modulatedParam (s, { { 0, 0xB0, 7, 0 } }, 15) - (0.25f + amt)) < 1e-6f);
    // the pitch wheel source stays 0 (the wrapper never writes it)
    set (s, 316, 5.0f);
    CHECK (std::fabs (lynx::toxic::modulatedParam (s, {}, 15) - 0.25f) < 1e-6f);
}

TEST (toxic169h_cc_link_map_and_state)
{
    using namespace toxic169h;
    auto s = state();
    const auto linkAt = (std::size_t) lynx::toxicParamOffset (s, 0) + 4 * 340;
    CHECK (s[linkAt + 15] == 0xff); // nothing linked by default
    s[linkAt + 15] = 74;            // Cutoff linked to CC 74
    CHECK (std::fabs (lynx::toxic::modulatedParam (s, { { 0, 0xB0, 74, 127 } }, 15) - 1.0f) < 1e-6f);
    CHECK (std::fabs (lynx::toxic::modulatedParam (s, { { 0, 0xB0, 74, 0 } }, 15)) < 1e-6f);
    CHECK (std::fabs (lynx::toxic::modulatedParam (s, { { 0, 0xB0, 75, 127 } }, 15) - 0.5f) < 1e-6f); // another CC: untouched

    // the pages' values live in the TBi2 state like every other parameter (round trip through the model table)
    lynx::ChannelDef c;
    c.instrument = lynx::InstrumentKind::toxic;
    c.pluginState = pattern ({ 5, -25 }, 12);
    CHECK (lynx::modelparams::rawValue (c, 157) == 1.0f);
    const auto* step = lynx::findEngineParam (lynx::toxicParams(), 164);
    CHECK (step->unitOf (*lynx::modelparams::rawValue (c, 164)) == -25);
    CHECK (lynx::modelparams::setRawValue (c, 324, 16.0f) && lynx::modelparams::rawValue (c, 324) == 16.0f);
    CHECK (lynx::toxicStateUsable (c.pluginState));
}

TEST (toxic169h_layout_pages)
{
    using namespace lynx;
    const auto* layout = plugui::instrumentLayout (InstrumentKind::toxic);
    CHECK (layout != nullptr && layout->pages.size() == 3);
    if (layout == nullptr)
        return;
    int midi = 0, seq = 0, mod = 0;
    for (const auto& item : layout->items)
    {
        if (item.page == 1 && item.param != nullptr)
            ++midi;
        if (item.page == 2 && item.param != nullptr)
            ++seq;
        if (item.page == 0 && item.param != nullptr)
            ++mod;
        if (item.param != nullptr && std::string (item.param).rfind ("FM ", 0) == 0)
            CHECK (item.page == 0);
    }
    CHECK (midi == 24);     // 8 × source, amount, destination
    CHECK (seq == 64 + 6);  // the steps, the number of steps and five switches
    CHECK (mod > 60);
    // the destination menu lists "---" and the 294 named parameters
    for (const auto& item : layout->items)
        if (item.param != nullptr && std::string (item.param) == "Mod 1 destination")
            CHECK (plugui::optionLabels (item).size() == 295);
}

TEST (toxic169h_seq_audio)
{
    using namespace toxic169h;
    // the sequencer's notes are heard: a held key with Seq Play on and only rests is silent, with steps it sounds
    auto rests = pattern ({}, 4);
    auto notes = pattern ({ 0, 12 }, 2);
    const auto quiet = lynx::toxic::renderReference (rests, { held (60) }, 44100.0, 12000, {}, 512, 125.0, 0, false);
    const auto loud = lynx::toxic::renderReference (notes, { held (60) }, 44100.0, 12000, {}, 512, 125.0, 0, false);
    CHECK (! quiet.empty() && ! loud.empty());
    float pq = 0.0f, pl = 0.0f;
    for (auto v : quiet)
        pq = std::max (pq, std::fabs (v));
    for (auto v : loud)
        pl = std::max (pl, std::fabs (v));
    CHECK (pq < 1e-6f);
    CHECK (pl > 0.01f);
}
