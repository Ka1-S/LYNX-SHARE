// --- Round 190ot (research190oe P5): the Simple layout's "Duck under..." (a Fruity Limiter keyed by another track through
// FL's level-0 sidechain send, core/SimpleTracks.h setDuck, goals::duck) and its shared Echo send (sharedEcho / setEchoSend).

#include <cmath>
#include <vector>

#include "core/EchoSuite.h"
#include "core/FlpExport.h"
#include "core/FlpImport.h"
#include "core/FruityDynamics.h"
#include "core/GoalKnobs.h"
#include "core/MixerOps.h"
#include "core/RackRenderer.h"
#include "core/SimpleTracks.h"

namespace simpleduck190ot
{
constexpr double rate = 48000.0;
constexpr double bpm = 120.0; // a beat = 0.5 s = 24000 samples

inline lynx::Song freshSong()
{
    lynx::Song s;
    s.bpm = bpm;
    s.mode = lynx::PlayMode::song;
    s.mixer.resize (6);
    s.mixer[0].name = "Master";
    for (int i = 1; i < 6; ++i)
        s.mixer[(std::size_t) i].name = "Insert " + std::to_string (i);
    return s;
}

// a Simple audio track playing `file` from the start for 8 beats
inline lynx::simpletracks::Result audioTrack (lynx::Song& s, const char* name, const char* file)
{
    lynx::simpletracks::Request q;
    q.kind = lynx::simpletracks::Kind::audio;
    q.name = name;
    q.audioFile = file;
    q.audioLength = 8 * lynx::ppq;
    return lynx::simpletracks::addTrack (s, q);
}

// "kick.wav": a 60 Hz burst at -6 dBFS for 80 ms on every beat (decaying); "pad.wav": a steady 330 Hz sine at -12 dBFS
inline std::shared_ptr<const lynx::SampleData> sample (const std::string& path)
{
    auto d = std::make_shared<lynx::SampleData>();
    d->sampleRate = rate;
    d->left.resize ((std::size_t) (rate * 4.5));
    for (std::size_t i = 0; i < d->left.size(); ++i)
    {
        const auto t = (double) i / rate;
        if (path == "kick.wav")
        {
            const auto inBeat = std::fmod (t, 0.5);
            d->left[i] = inBeat < 0.08 ? (float) (0.5 * std::exp (-inBeat * 20.0) * std::sin (2.0 * 3.14159265358979 * 60.0 * inBeat)) : 0.0f;
        }
        else
            d->left[i] = (float) (0.25 * std::sin (2.0 * 3.14159265358979 * 330.0 * t));
    }
    return d;
}

inline std::vector<float> render (const lynx::Song& s, int frames)
{
    auto state = lynx::RackState::build (s, nullptr, rate, [] (const std::string& p) { return sample (p); });
    lynx::RackRenderer r;
    r.prepare (rate, 512);
    std::vector<float> all, l (512), rr (512);
    const auto tps = lynx::ticksPerSample (s.bpm, rate);
    for (int done = 0; done < frames; done += 512)
    {
        r.process (state.get(), true, done * tps, 512, l.data(), rr.data());
        all.insert (all.end(), l.begin(), l.end());
    }
    all.resize ((std::size_t) frames);
    return all;
}

// RMS in dB of `x` over [from, from + length)
inline double rmsDb (const std::vector<float>& x, int from, int length)
{
    double sum = 0.0;
    for (int i = from; i < from + length && i < (int) x.size(); ++i)
        sum += (double) x[(std::size_t) i] * x[(std::size_t) i];
    return 10.0 * std::log10 (sum / length + 1e-30);
}

// the pad's level drop on each kick: (level between the kicks) − (level in the 60 ms after each kick), beats 2 … 7
inline double dipDb (const std::vector<float>& out)
{
    double worst = 1e9;
    for (int beat = 2; beat < 7; ++beat)
    {
        const auto at = beat * 24000;
        const auto under = rmsDb (out, at + 480, 2880);       // 10 … 70 ms after the hit
        const auto between = rmsDb (out, at + 19200, 4320);   // 400 … 490 ms (the release is over)
        worst = std::min (worst, between - under);
    }
    return worst;
}

struct Pair
{
    lynx::Song song;
    int kick = -1, pad = -1;
};

inline Pair kickAndPad()
{
    Pair p { freshSong() };
    p.kick = audioTrack (p.song, "Kick", "kick.wav").insert;
    p.pad = audioTrack (p.song, "Pad", "pad.wav").insert;
    return p;
}
} // namespace simpleduck190ot

TEST (simpleduck190ot_goal_maps_fruity_limiter_compressor)
{
    using namespace lynx;
    namespace fd = fldyn;
    // not in the "+" menu's one-knob effects, but a goal with its effect
    CHECK (goals::count == 8 && goals::lastId == goals::duck);
    CHECK (std::string (goals::name (goals::duck)) == "Duck" && goals::kindOf (goals::duck) == EffectKind::fruityLimiter);
    auto slot = goals::make (goals::duck, 0.0f);
    CHECK (slot.kind == EffectKind::fruityLimiter && slot.goal == goals::duck && goals::active (slot));
    // at 0 the compressor is off (threshold raw 1000 = 0 dB, FL's "off")
    CHECK (fd::raw (slot.params[8], fd::limiterMin[8], fd::limiterMax[8]) == 1000);
    // at half: about -24 dB, 6:1, hard knee, 1 ms attack, about 150 ms release (FL's own value texts)
    goals::setAmount (slot, 0.5f);
    CHECK (goals::active (slot));
    std::printf ("  [190ot] duck at 50 %%: threshold %s, ratio %s, knee %s, attack %s, release %s\n", fd::limiterParamText (8, slot.params[8]).c_str(),
                 fd::limiterParamText (9, slot.params[9]).c_str(), fd::limiterParamText (10, slot.params[10]).c_str(),
                 fd::limiterParamText (11, slot.params[11]).c_str(), fd::limiterParamText (12, slot.params[12]).c_str());
    CHECK (fd::limiterParamText (8, slot.params[8]) == "-24.0 dB");
    CHECK (fd::limiterParamText (9, slot.params[9]).rfind ("6.0:1", 0) == 0);
    CHECK (fd::limiterParamText (10, slot.params[10]) == "0.0%");
    const auto ms = [] (const std::string& t) { return std::atof (t.c_str()); };
    CHECK (std::abs (ms (fd::limiterParamText (11, slot.params[11])) - 1.0) < 0.05);
    CHECK (std::abs (ms (fd::limiterParamText (12, slot.params[12])) - 150.0) < 1.0);
    // the limiter section is a new FL instance's (only the compressor is the goal's)
    const auto fresh = makeEffectSlot (EffectKind::fruityLimiter);
    for (int i : { 0, 1, 2, 3, 4, 5, 6, 7, 13, 14, 15, 16, 17, 18 })
        CHECK (slot.params[(std::size_t) i] == fresh.params[(std::size_t) i]);
    // deeper as the knob goes up
    auto hard = goals::make (goals::duck, 1.0f);
    CHECK (hard.params[8] < slot.params[8] && hard.params[9] > slot.params[9]);
    // a goal id an older release does not know stays an ordinary slot there: nothing in the table past lastId
    CHECK (std::string (goals::name (goals::lastId + 1)).empty());
}

TEST (simpleduck190ot_duck_under_makes_fl_sidechain_routing)
{
    using namespace lynx;
    namespace st = simpletracks;
    auto [s, kick, pad] = simpleduck190ot::kickAndPad();
    CHECK (kick > 0 && pad > 0 && kick != pad);
    CHECK (st::duckSlot (s, pad) < 0 && st::duckTrigger (s, pad) == -1);

    // a chain already there: the duck goes last
    s.mixer[(std::size_t) pad].effects.push_back (makeEffectSlot (EffectKind::eq));
    CHECK (st::setDuck (s, pad, kick) == st::DuckResult::set);
    const auto slot = st::duckSlot (s, pad);
    CHECK (slot == 1);
    const auto& fx = s.mixer[(std::size_t) pad].effects[(std::size_t) slot];
    CHECK (fx.kind == EffectKind::fruityLimiter && fx.goal == goals::duck && goals::active (fx) && fx.sidechain == kick);
    CHECK (std::abs (fx.goalAmount - st::duckAmount (st::DuckStyle::pumping)) < 1.0e-6f);
    CHECK (st::duckTrigger (s, pad) == kick);
    // FL's "Sidechain to this track": a level-0 send, the kick still on the Master; the Mixer sees FL's sidechain link
    CHECK (s.routes (kick, pad) && s.routeLevel (kick, pad) == 0.0f && s.mixer[(std::size_t) kick].toMaster);
    CHECK (mixerops::soleSidechainSender (s, pad) == kick);
    CHECK (mixerops::sidechainIndexOf (s, pad, kick) == 1);

    // the styles and the knob
    CHECK (st::setDuckAmount (s, pad, st::duckAmount (st::DuckStyle::hard)));
    CHECK (std::abs (s.mixer[(std::size_t) pad].effects[(std::size_t) slot].goalAmount - 0.85f) < 1.0e-6f);
    CHECK (! st::setDuckAmount (s, kick, 0.5f)); // (no duck there)
    CHECK (std::string (st::duckStyleName (st::DuckStyle::subtle)) == "Subtle" && st::duckAmount (st::DuckStyle::subtle) < st::duckAmount (st::DuckStyle::pumping));

    // another trigger: the slot is re-keyed (Amount kept), the kick's level-0 send goes; the third track's comes
    const auto hat = simpleduck190ot::audioTrack (s, "Hat", "pad.wav").insert;
    CHECK (st::setDuck (s, pad, hat) == st::DuckResult::set);
    CHECK (st::duckSlot (s, pad) == slot && st::duckTrigger (s, pad) == hat);
    CHECK (std::abs (s.mixer[(std::size_t) pad].effects[(std::size_t) slot].goalAmount - 0.85f) < 1.0e-6f);
    CHECK (! s.routes (kick, pad) && s.routes (hat, pad) && s.routeLevel (hat, pad) == 0.0f);

    // a send with a level of the user's own is kept when the duck goes
    s.setRouteLevel (hat, pad, 0.5f);
    CHECK (st::setDuck (s, pad, -1) == st::DuckResult::removed);
    CHECK (st::duckSlot (s, pad) < 0 && s.mixer[(std::size_t) pad].effects.size() == 1 && s.routes (hat, pad));
    // ... and a level-0 one made for the duck goes with it
    CHECK (st::setDuck (s, pad, kick) == st::DuckResult::set && s.routes (kick, pad));
    CHECK (st::setDuck (s, pad, -1) == st::DuckResult::removed && ! s.routes (kick, pad));
    CHECK (st::setDuck (s, pad, -1) == st::DuckResult::removed); // (nothing to remove: fine)

    // a loop: the pad feeds the kick, so the kick cannot key the pad; nothing changes
    s.setRoute (pad, kick, true);
    const auto before = s.mixer;
    CHECK (st::setDuck (s, pad, kick) == st::DuckResult::cycle && s.mixer == before);
    CHECK (st::setDuck (s, pad, pad) == st::DuckResult::invalid && st::setDuck (s, 99, kick) == st::DuckResult::invalid);

    // a full chain: nothing changes
    auto [f, k2, p2] = simpleduck190ot::kickAndPad();
    for (int i = 0; i < maxEffectSlots; ++i)
        f.mixer[(std::size_t) p2].effects.push_back (makeEffectSlot (EffectKind::eq));
    const auto full = f.mixer;
    CHECK (st::setDuck (f, p2, k2) == st::DuckResult::full && f.mixer == full);
}

// Acceptance (TOP25 P5): a kick on one track, a pad on another, Duck under the kick at Amount 50 %: the pad dips by
// 6 dB or more on each kick. The kick's insert sends only its level-0 sidechain link (not to the Master), so the output
// is the pad alone.
TEST (simpleduck190ot_pad_dips_6_db_on_each_kick)
{
    using namespace lynx;
    namespace st = simpletracks;
    using namespace simpleduck190ot;
    auto [s, kick, pad] = kickAndPad();
    s.mixer[(std::size_t) kick].toMaster = false;
    const auto frames = 8 * 24000;
    const auto dry = render (s, frames);
    const auto dryDip = dipDb (dry);
    CHECK (st::setDuck (s, pad, kick, 0.5f) == st::DuckResult::set);
    const auto ducked = render (s, frames);
    const auto dip = dipDb (ducked);
    // the knob at 0: the compressor is off
    st::setDuckAmount (s, pad, 0.0f);
    const auto off = dipDb (render (s, frames));
    st::setDuckAmount (s, pad, st::duckAmount (st::DuckStyle::subtle));
    const auto subtle = dipDb (render (s, frames));
    st::setDuckAmount (s, pad, st::duckAmount (st::DuckStyle::hard));
    const auto hard = dipDb (render (s, frames));
    std::printf ("  [190ot] pad dip on each kick: none %.2f dB, Amount 0 %.2f dB, Subtle %.2f dB, 50 %% %.2f dB, Hard %.2f dB\n", dryDip, off, subtle, dip, hard);
    CHECK (std::abs (dryDip) < 0.2);
    CHECK (dip >= 6.0);
    CHECK (std::abs (off) < 0.2);
    CHECK (subtle > 1.0 && subtle < dip && hard > dip);
    // between the kicks the pad comes back to its own level (within 1 dB of the dry pad)
    CHECK (std::abs (rmsDb (ducked, 6 * 24000 + 19200, 4320) - rmsDb (dry, 6 * 24000 + 19200, 4320)) < 1.0);
}

TEST (simpleduck190ot_echo_send_is_a_shared_insert)
{
    using namespace lynx;
    namespace st = simpletracks;
    namespace ef = echosuite::echoform;
    auto [s, kick, pad] = simpleduck190ot::kickAndPad();
    // a send at 0 makes no insert
    const auto mixer = s.mixer;
    CHECK (st::setEchoSend (s, pad, 0.0f) && s.mixer == mixer && st::sharedEcho (s) == -1 && st::echoSend (s, pad) == 0.0f);
    // the first send makes the Echo insert: Echoform fully wet, a dotted quarter on both sides at the song's tempo, 35 %
    CHECK (st::setEchoSend (s, pad, 0.4f));
    const auto echo = st::sharedEcho (s);
    CHECK (echo > 0 && echo != kick && echo != pad && s.mixer[(std::size_t) echo].name == "Echo");
    const auto& fx = s.mixer[(std::size_t) echo].effects.front();
    CHECK (fx.kind == EffectKind::echoform && fx.params[ef::mix] == 1.0f);
    CHECK (std::abs (ef::sideMs (fx.params, false, simpleduck190ot::bpm) - 750.0) < 1.0e-6 && std::abs (ef::sideMs (fx.params, true, 140.0) - 1.5 * 60000.0 / 140.0) < 1.0e-6);
    CHECK (echosuite::paramText (EffectKind::echoform, ef::feedback, fx.params[ef::feedback]) == "35%");
    CHECK (s.mixer[(std::size_t) echo].toMaster && s.mixer[(std::size_t) echo].inputChannel == -1);
    // an ordinary FL send from the track's insert
    CHECK (s.routes (pad, echo) && std::abs (s.routeLevel (pad, echo) - 0.4f) < 1.0e-6f && std::abs (st::echoSend (s, pad) - 0.4f) < 1.0e-6f);
    // a second track shares it; the echo is not a Simple track
    CHECK (st::setEchoSend (s, kick, 0.2f) && st::sharedEcho (s) == echo && s.routes (kick, echo));
    for (int t = 0; t < (int) s.playlistTracks.size(); ++t)
        CHECK (st::insertOf (s, t) != echo);
    // the Echo and the Reverb are two inserts
    CHECK (st::setReverbSend (s, pad, 0.3f) && st::sharedReverb (s) > 0 && st::sharedReverb (s) != echo);
    // 0 removes the send, the insert stays (the other track still uses it)
    CHECK (st::setEchoSend (s, pad, 0.0f) && ! s.routes (pad, echo) && st::sharedEcho (s) == echo);
    // the echo cannot send into itself
    CHECK (st::setEchoSend (s, echo, 0.5f) == false || ! s.routes (echo, echo));
}

// .flp export: the duck is FL's own Fruity Limiter with its side input on the kick's place in the pad's inputs list, and
// the kick → pad route at level 0 (FL's sidechain); the Echo send is an ordinary route with its level. Read back by the
// importer: the same sidechain.
TEST (simpleduck190ot_flp_export_writes_fl_sidechain)
{
    using namespace lynx;
    namespace st = simpletracks;
    auto made = simpleduck190ot::kickAndPad();
    auto& s = made.song;
    const int kick = made.kick, pad = made.pad;
    CHECK (st::setDuck (s, pad, kick, 0.5f) == st::DuckResult::set);
    CHECK (st::setEchoSend (s, pad, 0.5f));
    const auto echo = st::sharedEcho (s);
    flpexport::Options options;
    flpexport::Report report;
    const auto project = flp::parseProject (flpexport::exportFlp (s, options, report));
    const flp::MixerInsert* padInsert = nullptr;
    bool routed = false, echoRouted = false;
    for (const auto& m : project.mixer)
    {
        if (m.index == pad)
            padInsert = &m;
        if (m.index == kick)
            routed = std::find (m.routes.begin(), m.routes.end(), pad) != m.routes.end();
        if (m.index == pad)
            echoRouted = std::find (m.routes.begin(), m.routes.end(), echo) != m.routes.end();
    }
    CHECK (routed && echoRouted);
    CHECK (padInsert != nullptr);
    if (padInsert != nullptr)
    {
        CHECK (padInsert->slots.size() == 1);
        if (! padInsert->slots.empty())
        {
            const auto& slot = padInsert->slots.front();
            CHECK (slot.plugin == "Fruity Limiter");
            const auto state = fldyn::parseLimiter (slot.state);
            CHECK (state.has_value());
            if (state)
            {
                CHECK (state->sidechain == mixerops::sidechainIndexOf (s, pad, kick) && state->sidechain == 1);
                const auto& fx = s.mixer[(std::size_t) pad].effects[(std::size_t) st::duckSlot (s, pad)];
                for (int i = 0; i < fldyn::limiterControls; ++i)
                    CHECK (state->p[(std::size_t) i] == fldyn::raw (fx.params[(std::size_t) i], fldyn::limiterMin[(std::size_t) i], fldyn::limiterMax[(std::size_t) i]));
            }
        }
    }
    CHECK (std::any_of (report.exact.begin(), report.exact.end(), [] (const std::string& e) { return e.find ("Fruity Limiter (Duck, sidechain)") != std::string::npos; }));
    // read back: FL's Fruity Limiter keyed by the kick, no audible kick → pad send
    FlpImportOptions io;
    io.fileExists = [] (const std::string&) { return true; };
    const auto back = importFlp (project, io);
    CHECK ((int) back.mixer.size() > pad);
    if ((int) back.mixer.size() > pad)
    {
        const auto& fx = back.mixer[(std::size_t) pad].effects;
        CHECK (fx.size() == 1 && ! fx.empty() && fx.front().kind == EffectKind::fruityLimiter && fx.front().sidechain == kick);
        CHECK (std::none_of (back.mixer[(std::size_t) kick].sends.begin(), back.mixer[(std::size_t) kick].sends.end(), [pad] (const MixerSend& x) { return x.target == pad && x.level > 0.0f; }));
        CHECK (std::any_of (back.mixer[(std::size_t) pad].sends.begin(), back.mixer[(std::size_t) pad].sends.end(), [echo] (const MixerSend& x) { return x.target == echo && x.level > 0.0f; }));
    }
}
