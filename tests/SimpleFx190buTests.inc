// --- Round 190bu: the Simple workflow's track dock — one-knob effects (core/GoalKnobs.h), chain presets and the chain's
// Blend (core/ChainPresets.h, RackRenderer), the sound library and its audition (core/SoundLibrary.h).

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <set>

#include "core/ChainPresets.h"
#include "core/Effects.h"
#include "core/FlpExport.h"
#include "core/GoalKnobs.h"
#include "core/MasterLooks.h"
#include "core/RackRenderer.h"
#include "core/SoundLibrary.h"

namespace simplefx190bu
{
// two seconds of pink-ish noise at −20 dB, the same every time
inline std::shared_ptr<const lynx::SampleData> noise()
{
    static const auto data = [] {
        auto d = std::make_shared<lynx::SampleData>();
        d->sampleRate = 48000.0;
        d->left.resize (96000);
        std::uint32_t seed = 0x190b;
        float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
        for (auto& v : d->left)
        {
            seed = seed * 1664525u + 1013904223u;
            const auto white = (float) ((seed >> 8) & 0xffff) / 32768.0f - 1.0f;
            b0 = 0.99765f * b0 + white * 0.0990460f;
            b1 = 0.96300f * b1 + white * 0.2965164f;
            b2 = 0.57000f * b2 + white * 1.0526913f;
            v = (b0 + b1 + b2 + white * 0.1848f) * 0.03f;
        }
        return std::shared_ptr<const lynx::SampleData> (d);
    }();
    return data;
}

// The noise played by a sampler through insert 1, whose chain `tweak` sets up; the left channel of `blocks` blocks
inline std::vector<float> render (const std::function<void (lynx::MixerTrack&)>& tweak, int blocks = 150, bool* hasStage = nullptr, float volume = 1.0f)
{
    using namespace lynx;
    Song s;
    s.bpm = 120.0;
    s.mixer.resize (2);
    ChannelDef c;
    c.instrument = InstrumentKind::sampler;
    c.samplePath = "noise";
    c.volume = volume;
    c.mixerTrack = 1;
    s.channels.push_back (c);
    s.patterns.emplace_back();
    tweak (s.mixer[1]);
    auto provider = [] (const std::string&) { return noise(); };
    auto st = RackState::build (s, nullptr, 48000.0, provider);
    if (hasStage != nullptr)
        *hasStage = st->chainBlends.size() > 1 && st->chainBlends[1] != nullptr;
    RackRenderer renderer;
    renderer.prepare (48000.0, 256);
    LiveEventQueue live;
    NoteEvent on;
    on.channel = 0;
    on.key = 60;
    on.velocity = 1.0f;
    live.push (on);
    std::vector<float> out, l (256), r (256);
    for (int block = 0; block < blocks; ++block)
    {
        renderer.process (st.get(), false, 0.0, 256, l.data(), r.data(), &live, nullptr);
        out.insert (out.end(), l.begin(), l.end());
    }
    return out;
}

inline double rms (const std::vector<float>& x, std::size_t from = 0)
{
    double sum = 0.0;
    for (std::size_t i = from; i < x.size(); ++i)
        sum += (double) x[i] * x[i];
    return std::sqrt (sum / (double) std::max<std::size_t> (1, x.size() - from));
}

inline double db (double ratio) { return 20.0 * std::log10 (std::max (1.0e-9, ratio)); }

// the level of (a − b) against b, in dB, with `a` taken as late as it best matches (an effect's own latency, which
// the mixer's delay compensation hides from the other tracks, is not a change of the sound)
inline double differenceDb (const std::vector<float>& a, const std::vector<float>& b, int maxLag = 256)
{
    double best = 1.0e9;
    const auto n = std::min (a.size(), b.size());
    for (int lag = 0; lag <= maxLag; ++lag)
    {
        double sum = 0.0;
        for (std::size_t i = 4800; i + (std::size_t) lag < n && i < 4800 + 9600; ++i)
        {
            const auto d = (double) a[i + (std::size_t) lag] - b[i];
            sum += d * d;
        }
        best = std::min (best, sum);
    }
    double ref = 0.0;
    for (std::size_t i = 4800; i < n && i < 4800 + 9600; ++i)
        ref += (double) b[i] * b[i];
    return db (std::sqrt (best / std::max (1.0e-18, ref)));
}

inline bool hasParam (lynx::EffectKind kind, const char* name)
{
    for (const auto& p : lynx::effectInfo (kind).params)
        if (std::strcmp (p.name, name) == 0)
            return true;
    return false;
}
} // namespace simplefx190bu

TEST (simplefx190bu_goal_table)
{
    using namespace lynx;
    const std::size_t moves[] { 0, 7, 10, 5, 5, 7, 6, 8, 8 }; // every parameter the table names exists in its effect (190dg: Punch's Output)
    std::set<std::string> names;
    for (int id = 1; id <= goals::count; ++id)
    {
        const std::string name = goals::name (id);
        CHECK (! name.empty() && names.insert (name).second);
        CHECK (std::string (goals::description (id)).size() > 4);
        CHECK (goals::idOfName (name) == id);
        CHECK (goals::defaultAmount (id) > 0.0f && goals::defaultAmount (id) < 1.0f);
        CHECK (goals::mapping (id, 0.5f).size() == moves[id]);
        // every value stays in 0..1 and moves one way only as the knob goes up
        auto before = goals::mapping (id, 0.0f);
        std::vector<int> direction (before.size(), 0);
        for (int step = 1; step <= 20; ++step)
        {
            const auto now = goals::mapping (id, (float) step / 20.0f);
            CHECK (now.size() == before.size());
            for (std::size_t i = 0; i < now.size() && i < before.size(); ++i)
            {
                CHECK (now[i].param == before[i].param && now[i].param >= 0 && now[i].param < maxEffectParams);
                CHECK (now[i].value >= 0.0f && now[i].value <= 1.0f);
                const auto d = now[i].value > before[i].value + 1.0e-7f ? 1 : now[i].value < before[i].value - 1.0e-7f ? -1 : 0;
                CHECK (d == 0 || direction[i] == 0 || d == direction[i]);
                if (d != 0)
                    direction[i] = d;
            }
            before = now;
        }
        // the knob moves something
        CHECK (goals::mapping (id, 0.0f) != goals::mapping (id, 1.0f));

        // a new slot: the goal's effect with the knob at its start
        auto slot = goals::make (id);
        CHECK (slot.kind == goals::kindOf (id) && slot.goal == id && goals::active (slot));
        CHECK (std::abs (slot.goalAmount - goals::defaultAmount (id)) < 1.0e-6f);
        goals::setAmount (slot, 0.8f);
        CHECK (goals::active (slot) && std::abs (slot.goalAmount - 0.8f) < 1.0e-6f);
        for (const auto& m : goals::mapping (id, 0.8f))
            CHECK (slot.params[(std::size_t) m.param] == m.value);
        goals::setAmount (slot, 7.0f); // clamped
        CHECK (slot.goalAmount == 1.0f);
        // a parameter moved by hand in the full editor: an ordinary slot, until the knob is turned again
        const auto moved = goals::mapping (id, 1.0f).back().param;
        slot.params[(std::size_t) moved] = slot.params[(std::size_t) moved] > 0.5f ? 0.1f : 0.9f;
        CHECK (! goals::active (slot));
        goals::setAmount (slot, 0.3f);
        CHECK (goals::active (slot));
        // the same slot holding another effect is not a one-knob effect
        slot.kind = EffectKind::eq;
        const auto params = slot.params;
        CHECK (! goals::active (slot));
        goals::setAmount (slot, 0.9f);
        CHECK (slot.params == params && std::abs (slot.goalAmount - 0.3f) < 1.0e-6f);
    }
    // the names the table uses, one by one (a renamed parameter must fail here, not go silent)
    using simplefx190bu::hasParam;
    for (const char* p : { "Dynamic", "High air", "Mid air", "Air", "Harmonics", "Output", "Mix" })
        CHECK (hasParam (EffectKind::aether, p));
    for (const char* p : { "Mix", "Feedback", "Low cut", "High cut", "Ducking", "Style", "Sync L", "Note L", "Sync R", "Note R" })
        CHECK (hasParam (EffectKind::echoform, p));
    for (const char* p : { "Attack", "Sustain", "Guard", "Output", "Mix" })
        CHECK (hasParam (EffectKind::strikeShaper, p));
    for (const char* p : { "Mix", "Decay", "Late size", "Predelay", "Low cut" })
        CHECK (hasParam (EffectKind::atrium, p));
    for (const char* p : { "Auto makeup", "Makeup ref", "Voicing", "Peak reduction", "Gain", "Mix" })
        CHECK (hasParam (EffectKind::glowLeveler, p));
    for (const char* p : { "Style", "Drive", "Tone", "Low cut", "High cut", "Auto gain", "Output", "Mix" })
        CHECK (hasParam (EffectKind::ember, p));
    for (const char* p : { "Output" })
        CHECK (hasParam (EffectKind::facetEq, p));
    for (const char* p : { "Output" })
        CHECK (hasParam (EffectKind::ember, p));
    // no goal
    CHECK (goals::mapping (0, 0.5f).empty() && goals::mapping (9, 0.5f).empty());
    CHECK (std::string (goals::name (0)).empty() && goals::idOfName ("Nope") == 0 && goals::idOfName ("") == 0);
    const auto plain = goals::make (0);
    CHECK (plain.goal == 0 && ! goals::active (plain));
    CHECK (! goals::active (makeEffectSlot (EffectKind::atrium)));
    // the two fields are part of the slot
    auto a = goals::make (goals::room), b = a;
    CHECK (a == b);
    b.goalAmount = 0.9f;
    CHECK (! (a == b));
}

// What each knob does to pink noise: nothing at 0, and no jump in level on the way up
TEST (simplefx190bu_goal_levels)
{
    using namespace lynx;
    using namespace simplefx190bu;
    const bool print = std::getenv ("LYNX_GOAL_CALIB") != nullptr;
    const auto dry = render ([] (MixerTrack&) {});
    const auto skip = (std::size_t) 24000; // (past the effects' first half second)
    CHECK (rms (dry, skip) > 0.01);
    for (int id = 1; id <= goals::count; ++id)
    {
        double at[5] {};
        double zeroDiff = 0.0;
        for (int i = 0; i < 5; ++i)
        {
            const auto amount = (float) i * 0.25f;
            const auto wet = render ([id, amount] (MixerTrack& t) { t.effects.push_back (goals::make (id, amount)); });
            bool finite = true;
            for (auto v : wet)
                finite = finite && std::isfinite (v);
            CHECK (finite);
            at[i] = db (rms (wet, skip) / rms (dry, skip));
            if (i == 0)
                zeroDiff = differenceDb (wet, dry);
        }
        if (print)
            std::printf ("GOAL %-9s diff@0 %7.1f dB | level %+5.1f %+5.1f %+5.1f %+5.1f %+5.1f dB\n", goals::name (id), zeroDiff, at[0], at[1],
                         at[2], at[3], at[4]);
        // at 0 the level is the dry one, and what differs is far below it
        CHECK (std::abs (at[0]) < 0.5);
        CHECK (zeroDiff < -30.0);
        // halfway the level is within 3 dB of the dry sound (Bass and Echo add energy by design; none more than 6 dB at the top)
        CHECK (std::abs (at[2]) < 3.0);
        CHECK (at[4] < 6.0 && at[4] > -4.0);
        // and it does something (Level is made to leave a steady sound's level alone: its own check follows)
        const auto half = render ([id] (MixerTrack& t) { t.effects.push_back (goals::make (id, 0.5f)); });
        if (id != goals::level)
            CHECK (differenceDb (half, dry) > -30.0);
    }
    // Level: the same sound played loud and quiet comes out closer together, more so as the knob goes up
    const auto quiet = render ([] (MixerTrack&) {}, 150, nullptr, 0.45f);
    const auto apart = db (rms (dry, skip) / rms (quiet, skip));
    CHECK (apart > 8.0);
    double before = apart;
    for (float amount : { 0.5f, 1.0f })
    {
        const auto chain = [amount] (MixerTrack& t) { t.effects.push_back (goals::make (goals::level, amount)); };
        const auto now = db (rms (render (chain), skip) / rms (render (chain, 150, nullptr, 0.45f), skip));
        if (print)
            std::printf ("LEVEL %3.0f%%: loud and quiet %.1f dB apart (dry %.1f dB)\n", amount * 100.0f, now, apart);
        CHECK (now < before - 1.0);
        before = now;
    }
}

TEST (simplefx190bu_chain_presets)
{
    using namespace lynx;
    const auto& list = chains::factory();
    CHECK (list.size() >= 20);
    std::set<std::string> names, categories;
    for (const auto* c : chains::categories())
        CHECK (categories.insert (c).second);
    std::set<std::string> used;
    for (const auto& p : list)
    {
        CHECK (names.insert (p.name).second);
        CHECK (categories.count (p.category) == 1);
        used.insert (p.category);
        CHECK (std::string (p.description).size() > 4);
        CHECK (p.blend > 0.0f && p.blend <= 1.0f);
        CHECK (chains::find (p.name) == &p);
        const auto slots = chains::slots (p);
        CHECK (slots.size() == p.steps.size() && ! slots.empty() && slots.size() <= (std::size_t) maxEffectSlots);
        for (std::size_t i = 0; i < slots.size(); ++i)
        {
            const auto& step = p.steps[i];
            CHECK (slots[i].row == (int) i && slots[i].enabled);
            if (step.goal != 0)
            {
                CHECK (goals::active (slots[i]) && slots[i].goal == step.goal && std::abs (slots[i].goalAmount - step.amount) < 1.0e-6f);
                CHECK (step.params.empty());
            }
            else
            {
                CHECK (slots[i].kind == step.kind && slots[i].goal == 0 && slots[i].mix == step.mix);
                CHECK (slots[i].kind != EffectKind::plugin);
                for (const auto& [name, value] : step.params)
                {
                    CHECK (simplefx190bu::hasParam (step.kind, name));
                    CHECK (value >= 0.0f && value <= 1.0f);
                }
            }
        }
    }
    CHECK (used.size() == categories.size()); // no empty category in the menu
    CHECK (chains::find ("Nope") == nullptr && chains::find ("") == nullptr);

    Song song;
    song.mixer.resize (3);
    song.mixer[1].volume = 0.4f;
    song.mixer[1].pan = -0.3f;
    song.mixer[1].name = "Voice";
    const auto* preset = chains::find ("Whisper Air");
    CHECK (preset != nullptr);
    if (preset == nullptr)
        return;
    CHECK (! chains::unchanged (song, 1));
    CHECK (chains::apply (song, 1, *preset));
    CHECK (song.mixer[1].effects == chains::slots (*preset));
    CHECK (song.mixer[1].chainBlend == preset->blend && song.mixer[1].chainPreset == "Whisper Air");
    CHECK (song.mixer[1].volume == 0.4f && song.mixer[1].pan == -0.3f && song.mixer[1].name == "Voice"); // the strip stays
    CHECK (song.mixer[2].effects.empty() && song.mixer[2].chainPreset.empty());
    CHECK (chains::unchanged (song, 1));
    // an edit shows: a knob, the Blend, a removed slot
    auto edited = song;
    goals::setAmount (edited.mixer[1].effects[0], 0.93f);
    CHECK (! chains::unchanged (edited, 1));
    edited = song;
    edited.mixer[1].chainBlend = 0.5f;
    CHECK (! chains::unchanged (edited, 1));
    edited = song;
    edited.mixer[1].effects.pop_back();
    CHECK (! chains::unchanged (edited, 1));
    // another preset replaces the chain; no chain clears it
    CHECK (chains::apply (song, 1, list.front()));
    CHECK (song.mixer[1].effects == chains::slots (list.front()) && song.mixer[1].chainPreset == list.front().name);
    CHECK (! chains::apply (song, 7, *preset) && ! chains::apply (song, -1, *preset));

    // a user's preset keeps the slots, Blend and name, nothing of the strip
    song.mixer[1].chainBlend = 0.65f;
    const auto state = chains::stateOf (song.mixer[1], "Mine");
    MixerTrack expected;
    expected.name = state.name;
    expected.effects = song.mixer[1].effects;
    expected.chainBlend = 0.65f;
    expected.chainPreset = "Mine";
    CHECK (state == expected);
    CHECK (chains::applyChain (song, 2, state.effects, state.chainBlend, "Mine"));
    CHECK (song.mixer[2].effects == song.mixer[1].effects && song.mixer[2].chainBlend == 0.65f && song.mixer[2].chainPreset == "Mine");
    CHECK (chains::unchanged (song, 2)); // (a user's preset: its file is not read here)
    // more than FL's ten slots are cut, bad rows are laid out again
    std::vector<EffectSlot> many (13, makeEffectSlot (EffectKind::eq));
    for (auto& m : many)
        m.row = 4;
    CHECK (chains::applyChain (song, 2, many, 2.0f, {}));
    CHECK (song.mixer[2].effects.size() == (std::size_t) maxEffectSlots && song.mixer[2].chainBlend == 1.0f && song.mixer[2].chainPreset.empty());
    std::set<int> rows;
    for (const auto& fx : song.mixer[2].effects)
        CHECK (fx.row >= 0 && fx.row < maxEffectSlots && rows.insert (fx.row).second);
    CHECK (chains::applyChain (song, 2, {}, 1.0f, {}));
    CHECK (song.mixer[2].effects.empty());
}

TEST (simplefx190bu_chain_presets_sound)
{
    using namespace lynx;
    using namespace simplefx190bu;
    const bool print = std::getenv ("LYNX_GOAL_CALIB") != nullptr;
    const auto dry = render ([] (MixerTrack&) {}, 120);
    for (const auto& p : chains::factory())
    {
        const auto wet = render ([&p] (MixerTrack& t) {
                                     t.effects = chains::slots (p);
                                     t.chainBlend = p.blend;
                                 },
                                 120);
        float peak = 0.0f;
        bool finite = true;
        for (auto v : wet)
        {
            finite = finite && std::isfinite (v);
            peak = std::max (peak, std::abs (v));
        }
        const auto level = db (rms (wet, 24000) / rms (dry, 24000));
        if (print)
            std::printf ("CHAIN %-14s level %+5.1f dB  peak %.3f  diff %6.1f dB\n", p.name, level, peak, differenceDb (wet, dry));
        CHECK (finite && peak < 1.0f);
        CHECK (level > -9.0 && level < 9.0);       // a preset changes the sound, not the fader
        CHECK (differenceDb (wet, dry) > -30.0);   // and it is heard
    }
}

TEST (simplefx190bu_blend_stage)
{
    using namespace lynx;
    using namespace simplefx190bu;
    bool stage = true;
    const auto dry = render ([] (MixerTrack&) {}, 100, &stage);
    CHECK (! stage);
    // (two effects without latency: the dry path is then the input itself)
    const auto chain = [] (MixerTrack& t) {
        auto drive = makeEffectSlot (EffectKind::distortion);
        drive.params[0] = 0.8f;
        t.effects.push_back (drive);
        t.effects.push_back (goals::make (goals::bass, 0.8f));
    };
    // 100 %: no stage is built, the same samples as before this round
    const auto wet = render (chain, 100, &stage);
    CHECK (! stage);
    const auto full = render ([&] (MixerTrack& t) { chain (t); t.chainBlend = 1.0f; }, 100, &stage);
    CHECK (! stage && full == wet);
    CHECK (differenceDb (wet, dry) > -20.0);
    // 0 %: the track without its effects, sample for sample
    const auto none = render ([&] (MixerTrack& t) { chain (t); t.chainBlend = 0.0f; }, 100, &stage);
    CHECK (stage);
    CHECK (none == dry);
    // in between: the two, in proportion
    for (float blend : { 0.25f, 0.6f })
    {
        const auto mixed = render ([&] (MixerTrack& t) { chain (t); t.chainBlend = blend; }, 100);
        double worst = 0.0;
        for (std::size_t i = 0; i < mixed.size(); ++i)
            worst = std::max (worst, (double) std::abs (mixed[i] - (dry[i] + (wet[i] - dry[i]) * blend)));
        CHECK (worst < 1.0e-6);
    }
    // an empty chain has nothing to blend
    render ([] (MixerTrack& t) { t.chainBlend = 0.3f; }, 4, &stage);
    CHECK (! stage);

    // a chain with latency (the lookahead limiter, far below its ceiling: a pure delay): the dry path is as late as
    // the chain, so any Blend gives the same delayed sound and nothing combs
    const auto limiter = [] (MixerTrack& t) { t.effects.push_back (makeEffectSlot (EffectKind::limiter)); };
    const auto late = render (limiter, 100);
    std::size_t delay = 0;
    {
        double best = 1.0e9;
        for (std::size_t d = 1; d < 400; ++d)
        {
            double sum = 0.0;
            for (std::size_t i = 4800; i < 9600; ++i)
                sum += std::abs ((double) late[i + d] - dry[i]);
            if (sum < best)
            {
                best = sum;
                delay = d;
            }
        }
    }
    CHECK (delay == 72); // 1.5 ms at 48 kHz
    for (float blend : { 0.0f, 0.5f })
    {
        const auto mixed = render ([&] (MixerTrack& t) { limiter (t); t.chainBlend = blend; }, 100, &stage);
        CHECK (stage);
        double worst = 0.0;
        for (std::size_t i = 0; i + delay < mixed.size(); ++i)
            worst = std::max (worst, (double) std::abs (mixed[i + delay] - dry[i]));
        if (blend == 0.0f)
            CHECK (worst < 1.0e-7); // the dry sound, as late as the chain
        CHECK (differenceDb (mixed, late) < -60.0);
    }
    // a bypassed slot adds no delay to the dry path
    const auto off = render ([&] (MixerTrack& t) { limiter (t); t.effects[0].enabled = false; t.chainBlend = 0.0f; }, 100);
    CHECK (off == dry);
}

TEST (simplefx190bu_sound_library)
{
    using namespace lynx;
    using soundlib::Category;
    for (int c = 0; c < (int) Category::count; ++c)
        CHECK (std::string (soundlib::categoryName ((Category) c)).size() > 2);
    CHECK (soundlib::categoryFor ("Bass", "Deep Sub") == Category::bass);
    CHECK (soundlib::categoryFor ("", "808 Kick") == Category::drums);
    CHECK (soundlib::categoryFor ("Pad", "Warm Choir") == Category::voices);
    CHECK (soundlib::categoryFor ("Synth pad", "Slow Sweep") == Category::pad);
    CHECK (soundlib::categoryFor ("", "Church Organ") == Category::organ);
    CHECK (soundlib::categoryFor ("", "Glass Bell") == Category::bells);
    CHECK (soundlib::categoryFor ("", "Grand Piano") == Category::keys);

    const auto& list = soundlib::builtIn();
    CHECK (list.size() >= 40);
    std::size_t categoriesUsed = 0;
    for (int c = 0; c < (int) Category::count; ++c)
    {
        const auto rows = soundlib::filter (list, (Category) c);
        categoriesUsed += rows.empty() ? 0 : 1;
        for (auto r : rows)
            CHECK (list[(std::size_t) r].category == (Category) c);
        CHECK (std::is_sorted (rows.begin(), rows.end()));
    }
    CHECK (categoriesUsed >= 6);
    // by category, then by name
    for (std::size_t i = 1; i < list.size(); ++i)
        CHECK ((int) list[i - 1].category <= (int) list[i].category);

    ChannelDef mine;
    mine.instrument = InstrumentKind::synth;
    mine.name = "My track";
    mine.volume = 0.42f;
    mine.pan = -0.25f;
    mine.mixerTrack = 7;
    mine.colour = 0xff112233u;
    for (std::size_t i = 0; i < list.size(); ++i)
    {
        const auto& s = list[i];
        CHECK (! s.name.empty() && ! s.origin.empty() && s.source != soundlib::Source::soundFont);
        const auto def = soundlib::channelFor (s);
        CHECK (def.has_value());
        if (! def)
            continue;
        CHECK (def->instrument == s.kind && def->name == s.name);
        // the track keeps its own settings and takes the sound
        const auto swapped = soundlib::withSound (mine, s);
        CHECK (swapped.has_value());
        if (! swapped)
            continue;
        CHECK (swapped->name == "My track" && swapped->volume == 0.42f && swapped->pan == -0.25f && swapped->mixerTrack == 7 && swapped->colour == 0xff112233u);
        CHECK (soundlib::sameSound (*swapped, *def));
        // and the list finds it again (the same sound under another name counts)
        const auto found = soundlib::find (list, *swapped);
        CHECK (found >= 0);
        if (found >= 0 && found != (int) i)
            CHECK (soundlib::sameSound (*soundlib::channelFor (list[(std::size_t) found]), *def));
    }
    // a sound edited by hand is none of the list
    {
        auto def = *soundlib::channelFor (list.front());
        ChannelDef other;
        other.instrument = InstrumentKind::sampler;
        CHECK (soundlib::find (list, other) == -1);
        CHECK (! soundlib::sameSound (def, other));
    }
    // ‹ › step inside the category and wrap
    for (int c = 0; c < (int) Category::count; ++c)
    {
        const auto rows = soundlib::filter (list, (Category) c);
        if (rows.empty())
        {
            CHECK (soundlib::step (list, -1, 1, (Category) c) == -1);
            continue;
        }
        CHECK (soundlib::step (list, -1, 1, (Category) c) == rows.front());
        CHECK (soundlib::step (list, -1, -1, (Category) c) == rows.back());
        CHECK (soundlib::step (list, rows.back(), 1) == rows.front());
        CHECK (soundlib::step (list, rows.front(), -1) == rows.back());
        if (rows.size() > 1)
            CHECK (soundlib::step (list, rows[0], 1) == rows[1]);
    }
    // the search: every word, in the name or the origin, any case
    {
        const auto& first = list[(std::size_t) soundlib::filter (list, Category::bass).front()];
        const auto hits = soundlib::filter (list, Category::bass, "  " + first.name.substr (0, 3) + " ");
        CHECK (! hits.empty());
        CHECK (soundlib::filter (list, Category::bass, "zzzzqq").empty());
        CHECK (soundlib::filter (list, Category::bass, first.name + " zzzzqq").empty());
    }

    // the user's SoundFonts: one sound per preset, the General MIDI family as its category
    sf::IndexedFont font;
    font.path = "/fonts/Band.sf2";
    font.name = "Band";
    font.presets.push_back ({ "Grand", 0, 0 });
    font.presets.push_back ({ "Finger Bass", 0, 33 });
    font.presets.push_back ({ "Standard Kit", 128, 0 });
    font.presets.push_back ({ "Strings", 0, 48 });
    const auto fonts = soundlib::fromFonts ({ font });
    CHECK (fonts.size() == 4);
    const auto in = [&fonts] (Category c) {
        const auto rows = soundlib::filter (fonts, c);
        return rows.size() == 1 ? fonts[(std::size_t) rows[0]].name : std::string();
    };
    CHECK (in (Category::keys) == "Grand" && in (Category::bass) == "Finger Bass" && in (Category::drums) == "Standard Kit" && in (Category::strings) == "Strings");
    const auto& bassSound = fonts[(std::size_t) soundlib::filter (fonts, Category::bass)[0]];
    CHECK (bassSound.origin == "Band" && bassSound.source == soundlib::Source::soundFont);
    const auto def = soundlib::withSound (mine, bassSound);
    CHECK (def.has_value());
    if (def)
    {
        CHECK (def->instrument == InstrumentKind::soundfont && def->samplePath == "/fonts/Band.sf2" && def->name == "My track");
        CHECK (soundlib::find (fonts, *def) == soundlib::filter (fonts, Category::bass)[0]);
        CHECK (soundlib::find (list, *def) == -1);
    }
}

// Every sound of the list plays with nothing but LYNX on the computer: the audition is never silent
TEST (simplefx190bu_every_sound_auditions)
{
    using namespace lynx;
    const bool print = std::getenv ("LYNX_GOAL_CALIB") != nullptr;
    const auto& list = soundlib::builtIn();
    int silent = 0;
    for (const auto& s : list)
    {
        const auto def = soundlib::channelFor (s);
        if (! def)
            continue;
        const auto keys = soundlib::auditionKeys (s);
        CHECK (! keys.empty() && keys.size() <= 4);
        const auto audio = renderAudition (*def, keys, 0.2, 0.1, 120.0, 22050.0, {});
        if (audio.left.empty())
        {
            ++silent;
            if (print)
                std::printf ("SILENT %s (%s)\n", s.name.c_str(), s.origin.c_str());
            continue;
        }
        float peak = 0.0f;
        for (std::size_t i = 0; i < audio.left.size(); ++i)
            peak = std::max (peak, std::max (std::abs (audio.left[i]), std::abs (audio.right[i])));
        CHECK (audio.left.size() == audio.right.size() && std::abs (peak - 0.5f) < 1.0e-3f);
        // (the end fades out: no click when the preview stops)
        CHECK (std::abs (audio.left.back()) < 1.0e-3f);
    }
    if (print)
        std::printf ("SOUNDS %d, silent %d\n", (int) list.size(), silent);
    CHECK (silent == 0);
    // nothing to play: nothing comes back
    ChannelDef synth;
    synth.instrument = InstrumentKind::synth;
    CHECK (renderAudition (synth, {}, 0.2, 0.1, 120.0, 22050.0, {}).left.empty());
    const auto one = renderAudition (synth, { 60, 64, 67 }, 0.32, 0.9, 120.0, 48000.0, {});
    CHECK (one.left.size() == (std::size_t) ((0.32 * 3 + 0.9) * 48000.0));
}

// FL has no dry/wet for a whole chain: the export writes the Blend into each slot's Mix and says so
TEST (simplefx190bu_blend_in_the_fl_export)
{
    using namespace lynx;
    Song s = Song::makeDefault();
    auto eq = looks::chain (MasterLookSettings { looks::balanced, 0.5f, { 0, 0, 0 }, 0 }).front(); // (exported as FL's own Parametric EQ 2)
    eq.row = 0;
    eq.mix = 0.8f;
    s.mixer[1].effects.push_back (eq);
    flpexport::Report plainReport;
    const auto plain = flpexport::exportFlp (s, {}, plainReport);
    const auto noted = [] (const flpexport::Report& r) {
        return std::any_of (r.notes.begin(), r.notes.end(), [] (const std::string& n) { return n.find ("Blend (50%)") != std::string::npos; });
    };
    CHECK (! plain.empty() && ! noted (plainReport));
    // Blend 50 % of a slot at 80 % is the same file as that slot at 40 %
    s.mixer[1].chainBlend = 0.5f;
    flpexport::Report report;
    const auto blended = flpexport::exportFlp (s, {}, report);
    CHECK (noted (report) && blended != plain);
    s.mixer[1].chainBlend = 1.0f;
    s.mixer[1].effects[0].mix = 0.4f;
    flpexport::Report byHand;
    CHECK (flpexport::exportFlp (s, {}, byHand) == blended && ! noted (byHand));
}
