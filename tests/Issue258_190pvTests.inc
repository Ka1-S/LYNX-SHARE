// Round 190pv (issue #258, "Transistor bass duplicated instrument automation doesn't work"): a cloned channel's own
// automation clip drives its sound, as in FL (a clone is a channel of its own)

#include "core/NativeEngines.h"
#include "core/RackOps.h"
#include "core/SynthParams.h"

namespace issue258
{
using namespace lynx;

// a Song-mode song with `channels`: one pattern whose note plays `noteChannel` at `key` for two bars (track 0) and, when
// `param` >= 0, an automation clip of (target channel, param) ramping from `from` to `to` over those bars (track 1)
inline Song song (const std::vector<ChannelDef>& channels, int noteChannel, int key, int target, int param, float from, float to)
{
    Song s;
    s.bpm = 140.0;
    s.mode = PlayMode::song;
    s.channels = channels;
    s.patterns.resize (1);
    Note n;
    n.channel = noteChannel;
    n.key = key;
    n.start = 0;
    n.length = 8 * ppq;
    n.velocity = 0.78f;
    s.patterns[0].notes.push_back (n);
    Clip p;
    p.kind = ClipKind::pattern;
    p.pattern = 0;
    p.track = 0;
    p.start = 0;
    p.length = 8 * ppq;
    s.playlist.push_back (p);
    if (param >= 0)
    {
        Automation a;
        a.name = "issue 258";
        a.target = { AutomationTargetKind::channelParam, target, 0, param };
        a.points = { { 0, from, 0.0f }, { 8 * ppq, to, 0.0f } };
        s.automations.push_back (a);
        Clip c;
        c.kind = ClipKind::automation;
        c.automation = 0;
        c.track = 1;
        c.start = 0;
        c.length = 8 * ppq;
        s.playlist.push_back (c);
    }
    s.playlistTracks.resize (4);
    s.uniqueInstances(); // (as the engine's publish: the clone gets its own instance)
    return s;
}

// the song through the renderer, `blocks` of 512 samples at 44.1 kHz (left channel)
inline std::vector<float> render (const Song& s, int blocks)
{
    auto state = RackState::build (s, nullptr, 44100.0);
    RackRenderer renderer;
    renderer.prepare (44100.0, 512);
    std::vector<float> l (512), r (512), all;
    for (int b = 0; b < blocks; ++b)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        renderer.process (state.get(), true, b * 512.0 * ticksPerSample (s.bpm, 44100.0), 512, l.data(), r.data());
        all.insert (all.end(), l.begin(), l.end());
    }
    return all;
}

inline double peak (const std::vector<float>& x)
{
    double m = 0.0;
    for (const auto v : x)
        m = std::max (m, (double) std::fabs (v));
    return m;
}
} // namespace issue258

// the reporter's steps on Transistor Bass (FL's "Empty" preset: sequencer on, the editor follows the notes): the shown
// program's sequence gated, the channel cloned, a note on the clone at the program's key, an automation clip on the
// clone's cutoff: the clone's sound follows the clip, and matches the original under the same clip
TEST (issue258_cloned_transistor_bass_follows_its_automation)
{
    using namespace lynx;
    using namespace issue258;
    auto c = tb169g2::channel ("Empty.fst");
    if (! c)
        return;
    for (const auto program : { 60, 81 })
    {
        auto tb = *c;
        CHECK (transistorBassSelectProgram (tb.pluginState, program));
        for (int i = 0; i < tbseq::steps; ++i)
            CHECK (modelparams::setRawValue (tb, tbseq::firstStep + 5 * i + 2, 1.0f));
        Song base;
        base.channels.push_back (tb);
        CHECK (cloneChannel (base, 0) == 1);
        CHECK (base.channels.size() == 2 && base.channels[1].pluginState == tb.pluginState);
        const auto blocks = 2 * 44100 * 60 / 140 * 8 / 2 / 512; // (the two bars)
        const auto originalAuto = render (song (base.channels, 0, program, 0, 2, 0.0f, 1.0f), blocks);
        const auto originalPlain = render (song (base.channels, 0, program, 0, -1, 0.0f, 0.0f), blocks);
        const auto cloneAuto = render (song (base.channels, 1, program, 1, 2, 0.0f, 1.0f), blocks);
        const auto clonePlain = render (song (base.channels, 1, program, 1, -1, 0.0f, 0.0f), blocks);
        const auto cloneOther = render (song (base.channels, 1, program, 1, 2, 1.0f, 0.0f), blocks);
        std::printf ("  program %d: peaks %.4f %.4f %.4f %.4f; orig auto==plain %d, clone auto==plain %d, clone==orig %d, ramps differ %d\n",
                     program, peak (originalAuto), peak (originalPlain), peak (cloneAuto), peak (clonePlain),
                     (int) (originalAuto == originalPlain), (int) (cloneAuto == clonePlain), (int) (cloneAuto == originalAuto),
                     (int) (cloneAuto != cloneOther));
        CHECK (peak (clonePlain) > 1e-3);
        CHECK (originalAuto != originalPlain);
        CHECK (cloneAuto != clonePlain);
        CHECK (cloneAuto != cloneOther);
        CHECK (cloneAuto == originalAuto);
    }
}

namespace issue258
{
// the parameters a channel's automation clip can drive on a native engine, those most likely to move a held note first
inline std::vector<int> automatableParams (const lynx::ChannelDef& c)
{
    using namespace lynx;
    std::vector<int> out;
    if (const auto* t = modelparams::tableOf (c.instrument); t != nullptr && t->params != nullptr)
    {
        std::vector<int> first, rest;
        for (const auto& p : *t->params)
        {
            if (p.stepped || p.hasUnits())
                continue;
            auto name = p.name;
            std::transform (name.begin(), name.end(), name.begin(), [] (unsigned char ch) { return (char) std::tolower (ch); });
            const auto likely = name.find ("cut") != std::string::npos || name.find ("vol") != std::string::npos
                                || name.find ("level") != std::string::npos || name.find ("freq") != std::string::npos
                                || name.find ("gain") != std::string::npos || name.find ("pitch") != std::string::npos;
            (likely ? first : rest).push_back (p.index);
        }
        out = first;
        out.insert (out.end(), rest.begin(), rest.end());
    }
    if (c.instrument == InstrumentKind::sytrus)
        for (const auto k : { 0, 1, 2, 18, 19 })
            if (sytrus::automatable (k))
                out.push_back (k);
    if (c.instrument == InstrumentKind::harmor && c.engineRev >= 3)
        for (const auto k : { 52, 64, 438, 450, 772, 773, 774 })
            if (harmor::automatable (k))
                out.push_back (k);
    if (c.instrument == InstrumentKind::synth)
        for (const int k : { (int) synthparam::cutoff, synthparam::oscParam (0, synthparam::level), (int) synthparam::resonance })
            out.push_back (synthparam::target (k));
    if (out.size() > 24)
        out.resize (24);
    return out;
}
} // namespace issue258

// every native engine that takes automation, cloned: the clone's own clip moves the clone's sound exactly as the same
// clip moves the original's (round 190pv; issue #258 reported it on Transistor Bass)
TEST (issue258_every_cloned_native_follows_its_automation)
{
    using namespace lynx;
    using namespace issue258;
    std::vector<ChannelDef> made;
    made.emplace_back(); // (LYNX Synth, the default channel)
    for (const auto& e : natives::instrumentEngines())
        if (auto c = natives::makeChannel (e.kind, natives142::flAccess()))
            made.push_back (*c);
    int checked = 0, none = 0;
    for (const auto& c : made)
    {
        const auto params = automatableParams (c);
        if (params.empty())
            continue;
        Song base;
        base.channels.push_back (c);
        if (cloneChannel (base, 0) != 1)
        {
            CHECK (false);
            continue;
        }
        const int blocks = 120;
        bool found = false;
        for (const auto param : params)
        {
            const auto originalAuto = render (song (base.channels, 0, 60, 0, param, 0.0f, 1.0f), blocks);
            const auto originalOther = render (song (base.channels, 0, 60, 0, param, 1.0f, 0.0f), blocks);
            if (originalAuto == originalOther)
                continue; // (this note doesn't hear that parameter: the next one)
            const auto cloneAuto = render (song (base.channels, 1, 60, 1, param, 0.0f, 1.0f), blocks);
            const auto cloneOther = render (song (base.channels, 1, 60, 1, param, 1.0f, 0.0f), blocks);
            const auto ok = cloneAuto == originalAuto && cloneOther == originalOther && cloneAuto != cloneOther;
            if (! ok)
                std::printf ("  %s (kind %d) param %d: the clone's automation %s\n", c.name.c_str(), (int) c.instrument, param,
                             cloneAuto == cloneOther ? "does nothing" : "plays differently from the original's");
            CHECK (ok);
            found = true;
            ++checked;
            break;
        }
        if (! found)
        {
            ++none;
            std::printf ("  %s (kind %d): no automated parameter moved its note\n", c.name.c_str(), (int) c.instrument);
        }
    }
    std::printf ("  issue258: %d cloned engines checked, %d without an audible parameter\n", checked, none);
    CHECK (checked >= 1);
}
