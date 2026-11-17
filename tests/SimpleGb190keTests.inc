// --- Round 190ke: GarageBand / BandLab ideas in the Simple layout (owner decisions D2 / D4 / D5 of coverage190jw):
// the song fade-out (core/SongFade.h), the Controls tab's eight knobs (core/SmartControls.h), the tuner's display rules
// and its detector on guitar / bass / voice pitches (core/SimpleTuner.h, core/FlExtraFx.h), and the tags of LYNX's own
// factory content (core/FactoryContent.h). LYNX_190KE_REPORT=1 prints the fade's measured levels.

#include <cmath>
#include <cstdlib>

#include "core/BrowserCollections.h"
#include "core/ChainPresets.h"
#include "core/FactoryContent.h"
#include "core/FlExtraFx.h"
#include "core/GoalKnobs.h"
#include "core/RackRenderer.h"
#include "core/SimpleTuner.h"
#include "core/SmartControls.h"
#include "core/SongFade.h"
#include "core/SoundLibrary.h"
#include "core/SynthParams.h"

namespace gb190ke
{
using namespace lynx;
constexpr double pi = 3.14159265358979323846;

inline Song empty()
{
    Song s;
    s.mixer = Song::defaultMixer();
    return s;
}

// a Drums track whose kick plays every beat for `bars` bars (clips repeated)
inline simpletracks::Result addBeat (Song& s, int bars)
{
    simpletracks::Request q;
    q.kind = simpletracks::Kind::drums;
    ChannelDef kick;
    kick.instrument = InstrumentKind::kick;
    q.kit = { kick };
    Pattern p;
    p.lengthTicks = 16 * ticksPerStep;
    for (int step = 0; step < 16; step += 4)
        p.toggleStep (0, step);
    q.pattern = p;
    q.fillBars = bars;
    return simpletracks::addTrack (s, q);
}

inline simpletracks::Result addSynth (Song& s)
{
    simpletracks::Request q;
    q.kind = simpletracks::Kind::instrument;
    ChannelDef c;
    c.instrument = InstrumentKind::synth;
    q.channel = c;
    return simpletracks::addTrack (s, q);
}

// RMS (dB) of the song rendered through the real path, per window of `windowSeconds`
inline std::vector<double> renderWindows (const Song& source, double windowSeconds)
{
    constexpr int rate = 48000, block = 240;
    std::int64_t ticks = 0;
    const auto song = makeExportSong (source, true, 0, ticks);
    const auto tps = ticksPerSample (song.bpm, rate);
    auto state = RackState::build (song, nullptr, rate);
    RackRenderer renderer;
    renderer.prepare (rate, block);
    const auto total = (std::int64_t) ((double) ticks / tps);
    const auto window = (std::int64_t) (windowSeconds * rate);
    std::vector<float> l (block), r (block);
    std::vector<double> out;
    double tick = 0.0, sum = 0.0;
    std::int64_t inWindow = 0;
    for (std::int64_t pos = 0; pos + block <= total; pos += block)
    {
        renderer.process (state.get(), true, tick, block, l.data(), r.data());
        tick += block * tps;
        for (int i = 0; i < block; ++i)
            sum += 0.5 * ((double) l[(std::size_t) i] * l[(std::size_t) i] + (double) r[(std::size_t) i] * r[(std::size_t) i]);
        inWindow += block;
        if (inWindow >= window)
        {
            out.push_back (10.0 * std::log10 (sum / (double) inWindow + 1.0e-20));
            sum = 0.0;
            inWindow = 0;
        }
    }
    return out;
}
} // namespace gb190ke

TEST (gb190ke_fade_out_places_garageband_points_on_the_master)
{
    using namespace lynx;
    using namespace gb190ke;
    auto s = empty();
    // nothing placed: nothing to fade
    CHECK (! songfade::apply (s, 10.0).ok);
    CHECK (! songfade::present (s));

    s.bpm = 120.0; // (a bar = 2 s)
    const auto beat = addBeat (s, 16);
    CHECK (beat.ok());
    const auto end = songfade::musicEnd (s);
    CHECK (end == 16 * s.barTicks());
    const auto r = songfade::apply (s, 10.0);
    CHECK (r.ok);
    CHECK (songfade::present (s));
    CHECK (std::abs (songfade::seconds (s) - 10.0) < 1.0e-6);
    const auto clip = s.playlist[(std::size_t) r.clip]; // (a copy: the playlist grows below)
    CHECK (clip.kind == ClipKind::automation && clip.end() == end && clip.start == end - songfade::ticksFor (s, 10.0));
    CHECK (clip.track == beat.track + 1);
    CHECK (s.playlistTracks[(std::size_t) clip.track].name == songfade::kTrackName);
    const auto& a = s.automations[(std::size_t) r.automation];
    CHECK (a.target.kind == AutomationTargetKind::mixerVolume && a.target.index == 0);
    // GarageBand's four points: 10, 7, 4 s before the end and the end; from the Master's level to silence
    CHECK (a.points.size() == 4);
    CHECK (a.points[0].tick == 0 && std::abs (a.points[0].value - s.mixer[0].volume) < 1.0e-6f);
    CHECK (std::abs (songfade::secondsOf (s, a.points[1].tick) - 3.0) < 0.01 && std::abs (songfade::secondsOf (s, a.points[2].tick) - 6.0) < 0.01);
    CHECK (a.points[3].tick == clip.length && a.points[3].value == 0.0f);
    CHECK (a.points[0].value > a.points[1].value && a.points[1].value > a.points[2].value && a.points[2].value > a.points[3].value);
    // the fade's own clip does not count as music: applied again (5 s) it stays at the same end, one clip, one automation
    const auto automations = s.automations.size(), clips = s.playlist.size();
    const auto r2 = songfade::apply (s, 5.0);
    CHECK (r2.ok && r2.automation == r.automation && s.automations.size() == automations && s.playlist.size() == clips);
    CHECK (s.playlist[(std::size_t) r2.clip].end() == end && std::abs (songfade::seconds (s) - 5.0) < 1.0e-6);
    // the song grows: the fade follows the new end
    Clip more = s.playlist[0];
    more.start = end;
    s.playlist.push_back (more);
    const auto r3 = songfade::apply (s, 10.0);
    CHECK (s.playlist[(std::size_t) r3.clip].end() == songfade::musicEnd (s) && songfade::musicEnd (s) > end);
    // longer than the music: it starts at the music's start
    auto shortSong = empty();
    shortSong.bpm = 120.0;
    addBeat (shortSong, 1);
    const auto r4 = songfade::apply (shortSong, 30.0);
    CHECK (r4.ok && r4.start == 0 && r4.length == shortSong.barTicks());
    // removed: no clip, no automation, the track's name goes
    CHECK (songfade::remove (s));
    CHECK (! songfade::present (s) && songfade::findAutomation (s) < 0);
    CHECK (s.playlistTracks[(std::size_t) clip.track].name.empty());
    CHECK (! songfade::remove (s));
    // an armed Guitar track with no take yet is in use: the fade goes under it, not on it
    auto withGuitar = empty();
    withGuitar.bpm = 120.0;
    addBeat (withGuitar, 4);
    simpletracks::Request g;
    g.kind = simpletracks::Kind::guitar;
    const auto gt = simpletracks::addTrack (withGuitar, g);
    const auto r5 = songfade::apply (withGuitar, 4.0);
    CHECK (gt.ok() && r5.ok && r5.track == gt.track + 1);
}

TEST (gb190ke_fade_out_renders_in_the_export)
{
    using namespace lynx;
    using namespace gb190ke;
    auto s = empty();
    s.bpm = 120.0;
    addBeat (s, 8); // 16 s
    const auto before = renderWindows (s, 2.0);
    songfade::apply (s, 6.0);
    const auto after = renderWindows (s, 2.0);
    CHECK (before.size() >= 8 && after.size() == before.size());
    if (before.size() < 8 || after.size() != before.size())
        return;
    if (std::getenv ("LYNX_190KE_REPORT") != nullptr)
        for (std::size_t i = 0; i < before.size(); ++i)
            std::printf ("  window %d: %.2f dB -> %.2f dB\n", (int) i, before[i], after[i]);
    // before the fade the mix is untouched (the first point is the Master's own level)
    for (std::size_t i = 0; i < 5; ++i)
        CHECK (std::abs (before[i] - after[i]) < 0.05);
    // the last window (the fade's last two seconds) is far quieter, and each window of the fade quieter than the last
    CHECK (after[7] < before[7] - 12.0);
    CHECK (after[5] > after[6] && after[6] > after[7]);
}

TEST (gb190ke_smart_controls_default_mapping)
{
    using namespace lynx;
    using namespace gb190ke;
    namespace sc = smartcontrols;
    auto s = empty();
    const auto t = addSynth (s);
    CHECK (t.ok() && ! t.channels.empty());
    const sc::Owner o { t.channels.front(), t.insert };
    // a synth track without effects: Cutoff, Resonance, Attack, Release, then Reverb, Pan, Volume, one empty knob
    auto m = sc::defaults (s, o);
    CHECK (sc::labelOf (s, o, m[0]).name == "Cutoff" && sc::labelOf (s, o, m[0]).group == sc::Group::sound);
    CHECK (sc::labelOf (s, o, m[1]).name == "Resonance");
    CHECK (sc::labelOf (s, o, m[2]).name == "Attack" && sc::labelOf (s, o, m[3]).name == "Release");
    CHECK (m[4].source == sc::Source::reverbSend && sc::labelOf (s, o, m[4]).name == "Reverb");
    CHECK (sc::labelOf (s, o, m[5]).name == "Pan" && sc::labelOf (s, o, m[6]).name == "Volume");
    CHECK (m[7].source == sc::Source::none);
    // a one-knob effect and a plain effect on the chain: their knob and Mix come before the track's
    s.mixer[(std::size_t) t.insert].effects.push_back (goals::make (goals::echo));
    s.mixer[(std::size_t) t.insert].effects.push_back (makeEffectSlot (EffectKind::chorus));
    m = sc::defaults (s, o);
    CHECK (m[4].source == sc::Source::goal && m[4].target.slot == 0 && sc::labelOf (s, o, m[4]).group == sc::Group::effects);
    CHECK (m[5].source == sc::Source::reverbSend);
    CHECK (m[6].target.kind == AutomationTargetKind::mixerPan && m[7].target.kind == AutomationTargetKind::mixerVolume);
    // (the chain's second effect did not fit: it is still a candidate)
    const auto all = sc::candidates (s, o);
    CHECK (std::any_of (all.begin(), all.end(), [] (const sc::Control& c) { return c.target.kind == AutomationTargetKind::effectMix && c.target.slot == 1; }));
    CHECK ((int) all.size() > synthparam::count);
}

TEST (gb190ke_smart_controls_turn_the_tracks_own_controls)
{
    using namespace lynx;
    using namespace gb190ke;
    namespace sc = smartcontrols;
    auto s = empty();
    const auto t = addSynth (s);
    const sc::Owner o { t.channels.front(), t.insert };
    s.mixer[(std::size_t) t.insert].effects.push_back (goals::make (goals::echo));
    const auto m = sc::mappingOf (s, o);
    // the synth's cutoff: the channel's own field, as the Synth page's knob moves it
    CHECK (sc::setValue (s, o, m[0], 0.25f));
    CHECK (std::abs (synthparam::get (s.channels[(std::size_t) o.channel].synth, synthparam::cutoff) - 0.25f) < 1.0e-4f);
    CHECK (std::abs (sc::valueOf (s, o, m[0]) - 0.25f) < 1.0e-4f);
    // the one-knob effect's knob
    CHECK (sc::setValue (s, o, m[4], 0.8f));
    CHECK (std::abs (s.mixer[(std::size_t) t.insert].effects[0].goalAmount - 0.8f) < 1.0e-6f && goals::active (s.mixer[(std::size_t) t.insert].effects[0]));
    // the track's volume and pan
    const auto vol = std::find_if (m.begin(), m.end(), [] (const sc::Control& c) { return c.target.kind == AutomationTargetKind::mixerVolume; });
    CHECK (vol != m.end() && sc::setValue (s, o, *vol, 0.5f) && std::abs (s.mixer[(std::size_t) t.insert].volume - 0.5f) < 1.0e-6f);
    CHECK (std::abs (sc::defaultValueOf (s, o, *vol) - unityFader) < 1.0e-6f);
    // an empty knob does nothing
    CHECK (! sc::setValue (s, o, sc::Control {}, 0.5f));
}

TEST (gb190ke_smart_controls_remap_is_kept_on_the_insert)
{
    using namespace lynx;
    using namespace gb190ke;
    namespace sc = smartcontrols;
    auto s = empty();
    const auto t = addSynth (s);
    const sc::Owner o { t.channels.front(), t.insert };
    // knob 7 (empty) gets the synth's FM; knob 0 is emptied
    const sc::Control fm { sc::Source::target, { AutomationTargetKind::channelParam, o.channel, 0, synthparam::target (synthparam::fm) } };
    CHECK (sc::remap (s, o, 7, fm));
    CHECK (sc::remap (s, o, 0, sc::Control {}));
    const auto text = s.mixer[(std::size_t) t.insert].smartControls;
    CHECK (! text.empty());
    auto m = sc::mappingOf (s, o);
    CHECK (m[7] == fm && sc::labelOf (s, o, m[7]).name == "FM");
    CHECK (m[0].source == sc::Source::none);
    CHECK (sc::labelOf (s, o, m[1]).name == "Resonance"); // (the others stay the default's)
    // the text round-trips and names no channel / insert (a knob belongs to its track)
    const auto decoded = sc::decode (text);
    CHECK (sc::encode (decoded) == text);
    CHECK (text.find ("t:") != std::string::npos);
    // a remapped knob whose control went away falls back to the default's
    CHECK (sc::remap (s, o, 3, sc::Control { sc::Source::goal, { AutomationTargetKind::effectParam, o.insert, 5, 0 } }));
    m = sc::mappingOf (s, o);
    CHECK (sc::labelOf (s, o, m[3]).name == "Release");
    // back to the default's knob, then everything back
    CHECK (sc::remap (s, o, 7, std::nullopt));
    CHECK (sc::mappingOf (s, o)[7].source == sc::Source::none);
    CHECK (sc::reset (s, o) && s.mixer[(std::size_t) t.insert].smartControls.empty());
    CHECK (sc::mappingOf (s, o) == sc::defaults (s, o));
    // an audio track (no channel): the chain and the track's controls only
    simpletracks::Request q;
    q.kind = simpletracks::Kind::voice;
    const auto v = simpletracks::addTrack (s, q);
    const sc::Owner vo { -1, v.insert };
    const auto vm = sc::defaults (s, vo);
    CHECK (vm[0].source == sc::Source::reverbSend);
    CHECK (std::none_of (vm.begin(), vm.end(), [] (const sc::Control& c) { return c.target.kind == AutomationTargetKind::channelParam; }));
}

TEST (gb190ke_tuner_display_rules)
{
    using namespace lynx;
    namespace st = simpletuner;
    CHECK (st::clampReference (440.04) == 440.0 && st::clampReference (500.0) == 470.0 && st::clampReference (300.0) == 410.0);
    CHECK (st::referenceText (442.0) == "442.0 Hz");
    CHECK (st::keynote (63).first == "D" && st::keynote (63).second); // D#
    CHECK (st::keynote (64).first == "E" && ! st::keynote (64).second);
    CHECK (st::centsText (20.2) == "+20 cents" && st::centsText (0.3) == "0 cents" && st::centsText (-7.0) == "\xe2\x88\x92" "7 cents");
    CHECK (st::inTune (2.5) && ! st::inTune (-4.0));
    CHECK (std::abs (st::needleAngle (50.0) - st::maxAngle) < 1.0e-9 && std::abs (st::needleAngle (-80.0) + st::maxAngle) < 1.0e-9);
    // the input: the insert's own, else the device's first channel
    Song s;
    s.mixer.resize (3);
    s.mixer[2].inputChannel = 1;
    s.mixer[2].inputStereo = false;
    CHECK (st::inputFor (s, 2, 4) == std::make_pair (1, 1));
    CHECK (st::inputFor (s, 1, 4) == std::make_pair (0, 1));
    CHECK (st::inputFor (s, 2, 0).second == 0);
}

TEST (gb190ke_tuner_detects_guitar_bass_and_voice)
{
    using namespace lynx;
    // guitar strings E2…E4, a bass's E1 (with the Bass range), a sung A3 and C5; 30 cents off; another reference
    struct Case
    {
        double hz;
        int note;
        bool bass;
        double reference;
    };
    const Case cases[] = { { 82.41, 40, false, 440.0 }, { 110.0, 45, false, 440.0 }, { 146.83, 50, false, 440.0 }, { 196.0, 55, false, 440.0 },
                           { 246.94, 59, false, 440.0 }, { 329.63, 64, false, 440.0 }, { 41.2, 28, true, 440.0 }, { 220.0, 57, false, 440.0 },
                           { 523.25, 72, false, 440.0 }, { 442.0, 69, false, 442.0 } };
    for (const auto& c : cases)
    {
        flextra::tuner::Analyzer a;
        a.prepare (48000.0);
        a.setOptions (c.reference, 0.5f, c.bass);
        std::vector<float> x (48000);
        for (std::size_t i = 0; i < x.size(); ++i) // a plucked-like tone: fundamental + two harmonics
            x[i] = (float) (0.4 * std::sin (2.0 * gb190ke::pi * c.hz * (double) i / 48000.0) + 0.2 * std::sin (4.0 * gb190ke::pi * c.hz * (double) i / 48000.0)
                            + 0.1 * std::sin (6.0 * gb190ke::pi * c.hz * (double) i / 48000.0));
        for (std::size_t at = 0; at < x.size(); at += 480)
            a.push (x.data() + at, 480);
        const auto r = a.reading();
        CHECK (r.valid && r.note == c.note && std::abs (r.cents) < 2.0);
    }
    // 30 cents sharp of A3
    flextra::tuner::Analyzer a;
    a.prepare (48000.0);
    a.setOptions (440.0, 0.5f, false);
    const auto hz = 220.0 * std::pow (2.0, 30.0 / 1200.0);
    std::vector<float> x (48000);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = (float) (0.4 * std::sin (2.0 * gb190ke::pi * hz * (double) i / 48000.0));
    for (std::size_t at = 0; at < x.size(); at += 480)
        a.push (x.data() + at, 480);
    CHECK (a.reading().valid && a.reading().note == 57 && std::abs (a.reading().cents - 30.0) < 2.0);
}

TEST (gb190ke_factory_content_tags)
{
    using namespace lynx;
    namespace fc = factorycontent;
    const auto& items = fc::items();
    int sounds = 0, beats = 0, chains = 0;
    for (const auto& it : items)
    {
        CHECK (! it.tags.empty());
        CHECK (fc::isKey (it.key) && fc::find (it.key) == &it);
        sounds += it.kind == fc::Kind::sound;
        beats += it.kind == fc::Kind::beat;
        chains += it.kind == fc::Kind::chain;
    }
    CHECK (sounds == (int) soundlib::builtIn().size() && sounds > 0);
    CHECK (beats >= 8 && chains == (int) lynx::chains::factory().size());
    // GarageBand's keyword families: instrument, genre, mood
    const auto t808 = fc::soundTags ("808 Bass", "Bass");
    CHECK (std::find (t808.begin(), t808.end(), "Bass") != t808.end() && std::find (t808.begin(), t808.end(), "Trap") != t808.end());
    const auto pad = fc::soundTags ("Dark Pad", "Pad");
    CHECK (std::find (pad.begin(), pad.end(), "Dark") != pad.end() && std::find (pad.begin(), pad.end(), "Ambient") != pad.end());
    const auto bankWords = fc::soundTags ("Squelch", "Lead", { "analog", "dark" });
    CHECK (std::find (bankWords.begin(), bankWords.end(), "Vintage") != bankWords.end() && std::find (bankWords.begin(), bankWords.end(), "Dark") != bankWords.end());
    const auto lofi = fc::chainTags ("Worn Cassette", "Lo-fi");
    CHECK (std::find (lofi.begin(), lofi.end(), "Lo-fi") != lofi.end() && std::find (lofi.begin(), lofi.end(), "Effect chain") != lofi.end());
    const auto trap = fc::beatTags ("trap", "Trap");
    CHECK (std::find (trap.begin(), trap.end(), "Trap") != trap.end() && std::find (trap.begin(), trap.end(), "Drums") != trap.end());
    // the Browser's filter: the user's tags and the factory's together (Any / All)
    browsertags::Store user;
    user.set ("/music/my kick.wav", "Trap", true);
    const auto merged = fc::merged (user);
    const auto trapItems = merged.filter ({ "trap" }, browsertags::Match::any);
    CHECK (std::find (trapItems.begin(), trapItems.end(), "/music/my kick.wav") != trapItems.end());
    CHECK ((int) trapItems.size() > 1);
    const auto darkTrapBeats = merged.filter ({ "Trap", "Dark", "Beat" }, browsertags::Match::all);
    CHECK (! darkTrapBeats.empty());
    for (const auto& i : darkTrapBeats)
        CHECK (fc::find (collections::pluginKeyOf (i)) != nullptr && fc::find (collections::pluginKeyOf (i))->kind == fc::Kind::beat);
    CHECK (merged.nameOf ("trap") == "Trap");
    // the factory's tags are not written into the user's store
    CHECK (user.items.size() == 1);
}

TEST (gb190ke_factory_beat_and_chain_actions)
{
    using namespace lynx;
    using namespace gb190ke;
    namespace fc = factorycontent;
    auto s = empty();
    const fc::Item* beat = nullptr;
    const fc::Item* chain = nullptr;
    for (const auto& it : fc::items())
    {
        if (beat == nullptr && it.kind == fc::Kind::beat && it.genre == "house")
            beat = &it;
        if (chain == nullptr && it.kind == fc::Kind::chain)
            chain = &it;
    }
    CHECK (beat != nullptr && chain != nullptr);
    if (beat == nullptr || chain == nullptr)
        return;
    const auto r = fc::addBeat (s, *beat, 2, 4);
    CHECK (r.ok() && r.pattern >= 0);
    std::int64_t first = -1, last = 0;
    for (const auto& c : s.playlist)
        if (c.track == r.track && c.kind == ClipKind::pattern)
        {
            first = first < 0 ? c.start : std::min (first, c.start);
            last = std::max (last, c.end());
        }
    CHECK (first == 2 * s.barTicks() && last == 6 * s.barTicks());
    CHECK (! s.patterns[(std::size_t) r.pattern].notes.empty());
    // the same item, the same beat
    auto again = empty();
    const auto r2 = fc::addBeat (again, *beat, 2, 4);
    CHECK (again.patterns[(std::size_t) r2.pattern].notes == s.patterns[(std::size_t) r.pattern].notes);
    // the chain on the beat's insert
    CHECK (fc::applyChain (s, *chain, r.insert));
    CHECK (s.mixer[(std::size_t) r.insert].chainPreset == chain->name && ! s.mixer[(std::size_t) r.insert].effects.empty());
    CHECK (! fc::applyChain (s, *beat, r.insert));
}
