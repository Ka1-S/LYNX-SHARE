// Round 169e: where a new Drumaxx (and LYNX's other drum banks) sound — FL's own key layout (REA_rodada169e)

#include "core/BankStates.h"
#include "core/Drumaxx.h"
#include "core/DrumSynthLive.h"
#include "core/LynxBanks.h"
#include "core/NativeEngines.h"

namespace drumaxx169e
{
// FL's fresh Drumaxx ("Default Kit"): the keys its pads sound on (pads 1–10 on 36 38 42 44 49 45 38 42 37 43)
inline const std::vector<int>& flDefaultKitKeys()
{
    static const std::vector<int> keys { 36, 37, 38, 42, 43, 44, 45, 49 };
    return keys;
}

// one channel in Pattern mode (120 BPM, 48 kHz): `steps` from the step sequencer (FL's steps play the channel's
// root note, C5 = 60), `keys` as Piano roll notes, one every beat after them; the output's peak per beat
inline std::vector<float> beatPeaks (const lynx::ChannelDef& channel, const std::vector<int>& steps, const std::vector<int>& keys)
{
    lynx::Song s;
    s.mixer = lynx::Song::defaultMixer (2);
    s.bpm = 120.0;
    auto c = channel;
    c.mixerTrack = 1;
    s.channels.push_back (c);
    s.patterns.emplace_back();
    auto& p = s.patterns[0];
    for (const auto step : steps)
        p.toggleStep (0, step * lynx::stepsPerBeat); // (one step on each of these beats)
    for (std::size_t i = 0; i < keys.size(); ++i)
    {
        lynx::Note n;
        n.channel = 0;
        n.key = keys[i];
        n.start = (std::int64_t) ((steps.size() + i) * lynx::ppq);
        n.length = lynx::ppq / 2;
        p.notes.push_back (n);
    }
    p.sortNotes();
    const auto beats = (int) (steps.size() + keys.size());
    p.lengthTicks = (std::int64_t) (lynx::ppq * std::max (4, beats + 1));
    s.mode = lynx::PlayMode::pattern;
    auto state = lynx::RackState::build (s, nullptr, 48000.0, [] (const std::string&) { return std::shared_ptr<const lynx::SampleData>(); });
    lynx::RackRenderer r;
    r.prepare (48000.0, 512);
    const auto tps = 120.0 / 60.0 * lynx::ppq / 48000.0;
    const int perBeat = 24000; // samples per beat at 120 BPM, 48 kHz
    std::vector<float> peaks ((std::size_t) beats, 0.0f);
    for (int done = 0; done < beats * perBeat; done += 512)
    {
        std::vector<float> l (512), rr (512);
        r.process (state.get(), true, done * tps, 512, l.data(), rr.data(), nullptr);
        for (int i = 0; i < 512; ++i)
        {
            const auto beat = (done + i) / perBeat;
            if (beat < beats)
                peaks[(std::size_t) beat] = std::max ({ peaks[(std::size_t) beat], std::abs (l[(std::size_t) i]), std::abs (rr[(std::size_t) i]) });
        }
    }
    return peaks;
}

inline std::vector<int> padKeys (const std::vector<std::uint8_t>& state)
{
    std::vector<int> keys;
    for (int k = 0; k < 16; ++k)
        if (lynx::drumaxxParam (state, 44 * k) > 0.0f && lynx::drumaxxParam (state, 44 * k + 2) == 0.0f
            && lynx::drumaxxParam (state, 44 * k + 4) > 0.0f) // volume, not muted, a key (FL's spare pads sit on key 0)
            keys.push_back ((int) lynx::drumaxxParam (state, 44 * k + 4));
    std::sort (keys.begin(), keys.end());
    keys.erase (std::unique (keys.begin(), keys.end()), keys.end());
    return keys;
}
} // namespace drumaxx169e

TEST (drumaxx169e_fresh_drumaxx_without_fl_sounds_on_fl_keys)
{
    using namespace lynx;
    // without FL Studio: a new Drumaxx is LYNX's first kit, its pads on FL's Default Kit keys; like FL's, it is
    // silent on the step sequencer's C5 (60) and on C4 (48) (FL's command-line render, REA_rodada169e §2)
    natives::FlAccess none;
    const auto c = natives::makeChannel (InstrumentKind::drumaxx, none);
    CHECK (c.has_value() && c->instrument == InstrumentKind::drumaxx);
    if (! c)
        return;
    CHECK (drumaxx169e::padKeys (c->pluginState) == drumaxx169e::flDefaultKitKeys());
    const auto peaks = drumaxx169e::beatPeaks (*c, { 0 }, { 60, 48, 36, 37, 38, 42, 43, 44, 45, 49 });
    CHECK (peaks.size() == 11u);
    if (peaks.size() != 11u)
        return;
    CHECK (peaks[0] < 1.0e-5f); // the step (C5)
    CHECK (peaks[1] < 1.0e-5f); // C5 from the Piano roll
    CHECK (peaks[2] < 1.0e-5f); // C4
    for (std::size_t i = 3; i < peaks.size(); ++i)
    {
        CHECK (peaks[i] > 1.0e-3f && peaks[i] < 0.95f);
        if (! (peaks[i] > 1.0e-3f && peaks[i] < 0.95f))
            std::printf ("  pad key %d: peak %g\n", drumaxx169e::flDefaultKitKeys()[i - 3], (double) peaks[i]);
    }

    // every LYNX kit sounds exactly on those keys (each key one pad role: kick 36, snare 38, hats 42 / 44...)
    const auto& bank = banks::bankOf (InstrumentKind::drumaxx);
    CHECK (bank.size() >= 3);
    for (int i = 0; i < (int) bank.size(); ++i)
    {
        ChannelDef d;
        d.instrument = InstrumentKind::drumaxx;
        const auto kit = banks::withPreset (d, i);
        CHECK (kit && drumaxx169e::padKeys (kit->pluginState) == drumaxx169e::flDefaultKitKeys());
        if (kit)
            for (int k = 8; k < 16; ++k) // the unused pads: silent, on key 0 like FL's
                CHECK (drumaxxParam (kit->pluginState, 44 * k) == 0.0f && drumaxxParam (kit->pluginState, 44 * k + 4) == 0.0f);
    }
}

TEST (drumaxx169e_lynx_drum_banks_play_from_the_step_sequencer)
{
    using namespace lynx;
    // the single-voice LYNX banks (Fruit Kick, Drumpad, Sakura, Plucked!) play any key: a fresh one sounds from the
    // step sequencer's C5, as FL's does
    natives::FlAccess none;
    for (const auto kind : { InstrumentKind::fruitkick, InstrumentKind::drumpad, InstrumentKind::sakura, InstrumentKind::plucked })
    {
        const auto c = natives::makeChannel (kind, none);
        CHECK (c.has_value());
        if (! c)
            continue;
        const auto peaks = drumaxx169e::beatPeaks (*c, { 0 }, { 48 });
        CHECK (peaks.size() == 2u && peaks[0] > 1.0e-3f && peaks[1] > 1.0e-3f);
        if (peaks.size() == 2u && ! (peaks[0] > 1.0e-3f))
            std::printf ("  silent step: kind %d (%g)\n", (int) kind, (double) peaks[0]);
    }

    // DrumSynth Live: LYNX's one-octave kits repeat every 12 keys over the 120 keys (FL's kits fill every key:
    // its default repeats its 24 patches), so C5 plays the kit's C
    const auto& bank = banks::bankOf (InstrumentKind::drumsynth);
    CHECK (bank.size() >= 2);
    for (int i = 0; i < (int) bank.size(); ++i)
    {
        ChannelDef d;
        d.instrument = InstrumentKind::drumsynth;
        const auto kit = banks::withPreset (d, i);
        CHECK (kit.has_value() && kit->modelParams.size() == (std::size_t) drumSynthParamCount);
        if (! kit || kit->modelParams.size() != (std::size_t) drumSynthParamCount)
            continue;
        const auto at = [&kit] (int key, int param) { return kit->modelParams[(std::size_t) (drumSynthPatchParams * drumSynthKeyToSlot (key) + param)]; };
        for (int key = 0; key < 120; ++key)
            for (int param = 0; param < drumSynthPatchParams; ++param)
                if (at (key, param) != at (48 + key % 12, param))
                {
                    CHECK (false);
                    std::printf ("  %s: key %d param %d differs from key %d\n", bank[(std::size_t) i].name.c_str(), key, param, 48 + key % 12);
                    key = 120;
                    break;
                }
        CHECK (at (60, 0) > 0.0f); // C5: the kit's kick level
        const auto peaks = drumaxx169e::beatPeaks (*kit, { 0 }, { 48, 36, 72 });
        CHECK (peaks.size() == 4u);
        for (const auto p : peaks)
            CHECK (p > 1.0e-3f && p < 0.95f);
    }
    // a patch on its own key wins over another's repeat; keys with no patch in the octave stay silent
    std::vector<bankstates::DrumSynthPatch> patches (2);
    patches[0].key = 48, patches[0].values.assign (38, 10);
    patches[1].key = 60, patches[1].values.assign (38, 20);
    const auto m = bankstates::drumSynthParams (patches);
    const auto level = [&m] (int key) { return m[(std::size_t) (drumSynthPatchParams * drumSynthKeyToSlot (key))]; };
    CHECK (level (48) == 10.0f && level (60) == 20.0f && level (36) == 10.0f && level (72) == 10.0f && level (49) == 0.0f);
}

TEST (drumaxx169e_fresh_drumaxx_with_fl_is_fl_default_kit)
{
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    // with FL Studio: a new Drumaxx is FL's own fresh one, the Plugin database's "Default Kit" (FL ships no
    // Default.fst for it); it plays where FL's does (FL's render: silent on 60 and 48, the kit on its keys)
    const auto fl = natives142::flAccess();
    const auto c = natives::makeChannel (InstrumentKind::drumaxx, fl);
    CHECK (c.has_value());
    if (! c)
        return;
    CHECK (drumaxxKitName (c->pluginState) == "Default Kit");
    CHECK (drumaxx169e::padKeys (c->pluginState) == drumaxx169e::flDefaultKitKeys());
    const auto peaks = drumaxx169e::beatPeaks (*c, { 0 }, { 48, 36, 38, 42 });
    CHECK (peaks.size() == 5u);
    if (peaks.size() == 5u)
        CHECK (peaks[0] < 1.0e-5f && peaks[1] < 1.0e-5f && peaks[2] > 1.0e-3f && peaks[3] > 1.0e-3f && peaks[4] > 1.0e-3f);
    // the drums FL ships without a Default.fst start from their Plugin database entry, before the folder's presets;
    // the other engines keep their folder order (Fruit Kick: its Default.fst, no Plugin database entry)
    for (const auto* folder : { "Drumaxx", "Drumpad", "BassDrum" })
    {
        const auto list = natives::presetCandidates (folder, true, fl);
        CHECK (! list.empty() && list.front().find ("Plugin database") != std::string::npos);
    }
    const auto kick = natives::presetCandidates ("Fruit Kick", true, fl);
    CHECK (! kick.empty() && kick[0].find ("Default.fst") != std::string::npos);
    for (const auto& path : kick)
        CHECK (path.find ("Plugin database") == std::string::npos);
}
