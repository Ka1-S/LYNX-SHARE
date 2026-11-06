// --- Round 144: parity items (docs/PARITY.md, REA_rodada144) -----------------------------------------------------

namespace p144
{
// a rhythm echo with Free / Grid taps at fractions of a 2-beat pattern (1000 ms at 120 BPM)
inline lynx::EffectParams freeRhythm (std::vector<float> at, float edit = 0.5f)
{
    namespace e = lynx::echosuite::echoform;
    auto p = p137::rhythmEcho (2, 3, 0.0, 0.0f);
    p[e::rhythmEdit] = edit;
    p[e::rhythmRepeats] = (float) at.size() / 15.0f;
    for (std::size_t k = 0; k < at.size(); ++k)
        p[(std::size_t) (e::rhythmTime1 + (int) k)] = at[k];
    return p;
}
} // namespace p144

TEST (echoform_rhythm_editor_defaults_round144)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    CHECK ((int) effectInfo (EffectKind::echoform).params.size() == e::paramCount);
    CHECK (e::rhythmEdit == e::tapeVoicing + 1); // appended after round 136's tape voicing
    CHECK (e::paramCount <= maxEffectParams); // round 144: 102 parameters (the slot holds 128)
    const auto p = makeEffectSlot (EffectKind::echoform).params;
    CHECK (e::rhythmEditOf (p[e::rhythmEdit]) == e::RhythmEdit::even);
    CHECK (e::rhythmShapeOf (p[e::rhythmShape]) == e::RhythmShape::decay);
    CHECK (formatEffectParam (EffectKind::echoform, e::rhythmGrid, p[e::rhythmGrid], 120.0) == "1/16");
    CHECK (formatEffectParam (EffectKind::echoform, e::rhythmEdit, 0.5f, 120.0) == "free");
    CHECK (formatEffectParam (EffectKind::echoform, e::rhythmEdit, 1.0f, 120.0) == "grid");
    CHECK (formatEffectParam (EffectKind::echoform, e::rhythmShape, 0.5f, 120.0) == "swell");
    CHECK (formatEffectParam (EffectKind::echoform, e::rhythmTime1, 0.25f, 120.0) == "25.0% of the pattern");
    // Even with the decay shape = round 137's measured law, whatever the tap times say
    auto q = p137::rhythmEcho (4, 3, 12.0, 0.0f);
    const auto before = e::rhythmTaps (q, 120.0);
    q[e::rhythmTime1] = 0.9f;
    const auto after = e::rhythmTaps (q, 120.0);
    CHECK (before.size() == 3 && after.size() == 3);
    for (std::size_t k = 0; k < before.size(); ++k)
        CHECK (before[k].ms == after[k].ms && before[k].level == after[k].level);
}

TEST (echoform_rhythm_free_and_grid_round144)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    // Free: each tap at its own time, levels equal-power over the slots as in Even (4 slots: 1/2 each)
    auto p = p144::freeRhythm ({ 0.1f, 0.3f, 0.7f });
    double slot0 = 0.0;
    auto taps = e::rhythmTaps (p, 120.0, &slot0);
    CHECK (taps.size() == 3);
    CHECK (std::abs (taps[0].ms - 100.0) < 1e-3 && std::abs (taps[1].ms - 300.0) < 1e-3 && std::abs (taps[2].ms - 700.0) < 1e-3);
    CHECK (std::abs (taps[1].level - 0.5) < 1e-9 && std::abs (slot0 - 0.5) < 1e-9);
    // Free taps are not dropped by the spacing (Even keeps only the slots that fit in the length)
    auto many = p144::freeRhythm ({ 0.05f, 0.1f, 0.15f, 0.2f, 0.25f, 0.3f, 0.35f, 0.4f, 0.45f, 0.5f, 0.55f, 0.6f, 0.65f, 0.7f, 0.75f });
    CHECK (e::rhythmTaps (many, 120.0).size() == 15);
    // Grid 1/16 (125 ms at 120 BPM): times snap to it, never onto the pattern's start or end
    p = p144::freeRhythm ({ 0.3f, 0.7f, 0.99f, 0.0f }, 1.0f);
    taps = e::rhythmTaps (p, 120.0);
    CHECK (taps.size() == 4);
    CHECK (std::abs (taps[0].ms - 250.0) < 1e-6 && std::abs (taps[1].ms - 750.0) < 1e-6);
    CHECK (std::abs (taps[2].ms - 875.0) < 1e-6 && std::abs (taps[3].ms - 125.0) < 1e-6);
    p[e::rhythmGrid] = 2.5f / 6.0f; // 1/8
    CHECK (std::abs (e::rhythmTapMs (p, 0, 120.0) - 250.0) < 1e-6 && std::abs (e::rhythmTapMs (p, 1, 120.0) - 750.0) < 1e-6);
    // the editor's inverse: a mouse time → the value (snapped in Grid)
    CHECK (std::abs (e::rhythmTapMs (p, 0, 120.0) - 250.0) < 1e-6);
    p[e::rhythmTime1] = e::rhythmTimeValue (p, 610.0, 120.0);
    CHECK (std::abs (e::rhythmTapMs (p, 0, 120.0) - 500.0) < 1e-6);
    // the tempo moves Free taps with the pattern (fractions of its length)
    p = p144::freeRhythm ({ 0.25f });
    CHECK (std::abs (e::rhythmTaps (p, 60.0)[0].ms - 500.0) < 1e-3);
    // turning the even pattern into Free taps keeps the times
    auto even = p137::rhythmEcho (4, 3, 0.0, 0.0f);
    auto conv = even;
    for (const auto& [param, value] : e::rhythmFreeFromEven (even, 120.0))
        conv[(std::size_t) param] = value;
    CHECK (e::rhythmEditOf (conv[e::rhythmEdit]) == e::RhythmEdit::free);
    const auto a = e::rhythmTaps (even, 120.0), b = e::rhythmTaps (conv, 120.0);
    CHECK (a.size() == b.size());
    for (std::size_t k = 0; k < a.size() && k < b.size(); ++k)
        CHECK (std::abs (a[k].ms - b[k].ms) < 0.05 && std::abs (a[k].level - b[k].level) < 1e-9);
}

TEST (echoform_rhythm_shapes_round144)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    using S = e::RhythmShape;
    CHECK (e::rhythmShapeDb (S::decay, 20.0, 1.0) == -20.0 && e::rhythmShapeDb (S::decay, 20.0, 0.0) == 0.0);
    CHECK (e::rhythmShapeDb (S::reverse, 20.0, 0.0) == -20.0 && e::rhythmShapeDb (S::reverse, 20.0, 1.0) == 0.0);
    CHECK (e::rhythmShapeDb (S::swell, 20.0, 0.5) == 0.0 && e::rhythmShapeDb (S::swell, 20.0, 0.0) == -20.0);
    CHECK (std::abs (e::rhythmShapeDb (S::fade, 20.0, 1.0) - -20.0) < 1e-9);
    CHECK (std::abs (e::rhythmShapeDb (S::fade, 20.0, 0.5) - 20.0 * std::log10 (0.55)) < 1e-9); // straight in level
    CHECK (e::rhythmShapeDb (S::nonlin, 20.0, 0.5) > -3.0 && e::rhythmShapeDb (S::nonlin, 20.0, 1.0) == -20.0);
    // Reverse: the last heard tap is the loudest; Swell: the middle one
    auto p = p137::rhythmEcho (8, 3, 20.0, 0.0f);
    p[e::rhythmLength] = 3.0f / 7.0f; // 4 beats: the 8 slots of 1/8 fit
    p[e::rhythmShape] = 0.3f;         // reverse
    auto taps = e::rhythmTaps (p, 120.0);
    CHECK (taps.size() == 7 && taps.back().level > taps.front().level);
    p[e::rhythmShape] = 0.5f; // swell
    taps = e::rhythmTaps (p, 120.0);
    CHECK (taps[2].level > taps[0].level && taps[2].level > taps[6].level);
    // the power over the slots stays 1 whatever the shape
    for (const float sh : { 0.1f, 0.3f, 0.5f, 0.7f, 0.9f })
    {
        p[e::rhythmShape] = sh;
        double slot0 = 0.0;
        taps = e::rhythmTaps (p, 120.0, &slot0);
        double power = slot0 * slot0;
        for (const auto& t : taps)
            power += t.level * t.level;
        CHECK (std::abs (power - 1.0) < 1e-9);
    }
}

TEST (echoform_rhythm_patterns_round144)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    CHECK (e::rhythmPresetCount() >= 10);
    CHECK (std::string (e::rhythmPresetName (e::rhythmPresetCount())).empty());
    for (int i = 0; i < e::rhythmPresetCount(); ++i)
    {
        auto p = makeEffectSlot (EffectKind::echoform).params;
        for (const auto& [param, value] : e::rhythmPresetEdits (i))
            p[(std::size_t) param] = value;
        CHECK (p[e::rhythm] == 1.0f && e::rhythmEditOf (p[e::rhythmEdit]) != e::RhythmEdit::even);
        const auto length = e::rhythmLengthMs (p, 120.0);
        const auto taps = e::rhythmTaps (p, 120.0);
        CHECK (! taps.empty());
        for (const auto& t : taps)
            CHECK (t.ms > 0.0 && t.ms < length && t.level > 0.0);
        CHECK (std::strlen (e::rhythmPresetName (i)) > 0);
    }
    // "Gallop" (the first): sixteenths 2, 3, 6, 7 of 2 beats at 120 BPM
    auto p = makeEffectSlot (EffectKind::echoform).params;
    for (const auto& [param, value] : e::rhythmPresetEdits (0))
        p[(std::size_t) param] = value;
    const auto taps = e::rhythmTaps (p, 120.0);
    CHECK (taps.size() == 4);
    CHECK (std::abs (taps[0].ms - 250.0) < 1e-6 && std::abs (taps[1].ms - 375.0) < 1e-6 && std::abs (taps[3].ms - 875.0) < 1e-6);
    CHECK (std::string (e::rhythmPresetName (0)) == "Gallop");
}

TEST (echoform_rhythm_free_impulse_round144)
{
    using namespace lynx;
    // Free taps at 100 / 300 / 700 ms: the impulse comes back there (−6 dB each), nothing at the even spacing (250 ms)
    std::vector<float> l, r;
    p135w::echoImpulse (p144::freeRhythm ({ 0.1f, 0.3f, 0.7f }), l, r, 1);
    for (const int at : { 4800, 14400, 33600 })
        CHECK (std::abs (p137::peakNear (l, at) - 0.25f) < 0.03f);
    CHECK (p137::peakNear (l, 12000, 200) < 0.01f);
}

TEST (echoform_rhythm_editor_layout_round144)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    const auto kind = EffectKind::echoform;
    const auto& l = plugui::layoutFor (kind);
    bool hasEditor = false, hasPatterns = false;
    for (const auto& item : l.items)
        if (item.type == plugui::Item::display && item.display != nullptr)
        {
            hasEditor = hasEditor || std::strcmp (item.display, "rhythmedit") == 0;
            hasPatterns = hasPatterns || std::strcmp (item.display, "rhythmpatterns") == 0;
        }
    CHECK (hasEditor && hasPatterns);
    // Rhythm on, each edit: nothing shown overlaps, every new control is shown where it belongs
    auto params = makeEffectSlot (kind).params;
    params[e::rhythm] = 1.0f;
    for (const float edit : { 0.0f, 0.5f, 1.0f })
        for (const float sync : { 0.25f, 0.75f })
        {
            params[e::rhythmEdit] = edit;
            params[e::rhythmSync] = sync;
            std::vector<const plugui::LayoutItem*> shown;
            bool grid = false;
            for (const auto& item : l.items)
                if ((plugui::isValueControl (item.type) || item.type == plugui::Item::display) && plugui::isShown (kind, item, 1, params)) // (tp172d: the Tweak page)
                {
                    shown.push_back (&item);
                    grid = grid || (item.param != nullptr && std::strcmp (item.param, "Rhythm grid") == 0);
                }
            CHECK (grid == (edit == 1.0f));
            for (std::size_t i = 0; i < shown.size(); ++i)
                for (std::size_t j = i + 1; j < shown.size(); ++j)
                {
                    const auto& p = *shown[i];
                    const auto& q = *shown[j];
                    const auto overlap = p.x + 0.5f < q.x + q.w && q.x + 0.5f < p.x + p.w && p.y + 0.5f < q.y + q.h && q.y + 0.5f < p.y + p.h;
                    if (overlap)
                        std::printf ("  Echoform: '%s' overlaps '%s'\n", p.param ? p.param : (p.display ? p.display : "-"), q.param ? q.param : (q.display ? q.display : "-"));
                    CHECK (! overlap);
                }
        }
}

TEST (echoform_style_edit_round144)
{
    using namespace lynx;
    namespace e = echosuite::echoform;
    const auto d = makeEffectSlot (EffectKind::echoform).params;
    CHECK (e::inputLevel == e::rhythmTimeLast + 1 && e::loopTreble + 1 == e::voicing);
    CHECK (formatEffectParam (EffectKind::echoform, e::inputLevel, d[e::inputLevel], 120.0) == "+0.0 dB");
    CHECK (formatEffectParam (EffectKind::echoform, e::loopMid, d[e::loopMid], 120.0) == "+0.0 dB");
    CHECK (formatEffectParam (EffectKind::echoform, e::loopBass, 1.0f, 120.0) == "+18.0 dB");
    CHECK (formatEffectParam (EffectKind::echoform, e::diffPost, d[e::diffPost], 120.0) == "loop");
    CHECK (formatEffectParam (EffectKind::echoform, e::modShape, d[e::modShape], 120.0) == "sine");
    CHECK (formatEffectParam (EffectKind::echoform, e::modSync, d[e::modSync], 120.0) == "free");
    CHECK (formatEffectParam (EffectKind::echoform, e::modSync, 3.5f / 7.0f, 120.0) == "1/4");
    CHECK (e::modSyncBeats (e::modSyncOf (6.5f / 7.0f)) == 8.0);
    std::vector<float> l, r, l2, r2;
    // the defaults are the old sound
    auto p = p135w::cleanEcho (250.0, 0.3f);
    p135w::echoImpulse (p, l, r, 1);
    auto q = p;
    q[e::inputLevel] = 0.5f; q[e::fbBalance] = 0.5f; q[e::loopBass] = q[e::loopMid] = q[e::loopTreble] = 0.5f;
    p135w::echoImpulse (q, l2, r2, 1);
    CHECK (l == l2 && r == r2);
    // Input level −6 dB: the first echo half as loud (the dry impulse unchanged)
    q = p;
    q[e::inputLevel] = 0.5f - 6.0f / 48.0f;
    p135w::echoImpulse (q, l2, r2, 1);
    CHECK (std::abs (p137::peakNear (l2, 12000) / p137::peakNear (l, 12000) - 0.501f) < 0.03f);
    CHECK (std::abs (l2[0] - l[0]) < 1e-6f);
    // FB balance fully right: the left side has no second repeat, the right side keeps it
    q = p;
    q[e::fbBalance] = 1.0f;
    p135w::echoImpulse (q, l2, r2, 1);
    CHECK (p137::peakNear (l2, 24000) < 0.01f && p137::peakNear (r2, 24000) > 0.5f * p137::peakNear (r, 24000));
    CHECK (p137::peakNear (l2, 12000) > 0.9f * p137::peakNear (l, 12000));
    // the loop EQ shapes the later repeats more than the first: −18 dB treble darkens repeat 3 against repeat 1
    q = p;
    q[e::feedback] = 0.45f;
    q[e::loopTreble] = 0.0f;
    p135w::echoImpulse (q, l2, r2, 1);
    auto hiEnergy = [] (const std::vector<float>& x, int from, int n) {
        double h = 0.0, all = 0.0;
        for (int i = from + 1; i < from + n; ++i)
        {
            h += (double) (x[(std::size_t) i] - x[(std::size_t) i - 1]) * (x[(std::size_t) i] - x[(std::size_t) i - 1]);
            all += (double) x[(std::size_t) i] * x[(std::size_t) i];
        }
        return h / std::max (1e-20, all);
    };
    CHECK (hiEnergy (l2, 36000 - 200, 2000) < 0.7 * hiEnergy (l2, 12000 - 200, 2000));
    // Diffusion post: the loop stays clean (later repeats as sharp as the first) — in the loop they smear
    q = p;
    q[e::feedback] = 0.45f;
    q[e::diffusion] = 1.0f;
    q[e::diffPost] = 1.0f;
    p135w::echoImpulse (q, l2, r2, 1);
    const auto ratioPost = p137::peakNear (l2, 36000, 1200) / std::max (1e-9f, p137::peakNear (l2, 12000, 1200));
    q[e::diffPost] = 0.0f;
    p135w::echoImpulse (q, l2, r2, 1);
    const auto ratioLoop = p137::peakNear (l2, 36000, 1200) / std::max (1e-9f, p137::peakNear (l2, 12000, 1200));
    CHECK (ratioPost > ratioLoop);
    // every LFO shape and the sync stay finite and bounded (a lo-fi mode uses the LFO)
    q = p;
    q[e::mode] = e::modeValue (e::Mode::lofi);
    q[e::modDepth] = 1.0f;
    for (const float sh : { 0.1f, 0.3f, 0.5f, 0.7f, 0.9f })
        for (const float sy : { 0.0f, 0.3f, 0.95f })
        {
            q[e::modShape] = sh;
            q[e::modSync] = sy;
            p135w::echoImpulse (q, l2, r2, 1);
            bool ok = true;
            for (const auto x : l2)
                ok = ok && std::isfinite (x) && std::abs (x) < 4.0f;
            CHECK (ok);
        }
}
