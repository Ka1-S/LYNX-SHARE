// Round 167a (left167): leftovers from 0.1.21 (docs/analise/REA_rodada167a.md).

#include "core/EffectPresets.h"
#include "core/FruityBasicFx.h"
#include "core/HostSlide.h"
#include "core/MixerOps.h"

#include <set>

namespace left167
{
lynx::Song sendSong (int target)
{
    using namespace lynx;
    Song s;
    s.mixer = Song::defaultMixer (200);
    auto slot = makeEffectSlot (EffectKind::fruitySend);
    flbasic::sendToParams ({ 0, 256, 256, target }, slot.params.data());
    s.mixer[1].effects.push_back (slot);
    return s;
}
} // namespace left167

TEST (left167_fruity_send_files_keep_the_legacy_span_when_every_target_fits)
{
    using namespace lynx;
    // the slot alone: 0..127 when its target fits (older LYNX versions read the same insert), else today's span
    CHECK (flbasic::saveSendSpanOf (left167::sendSong (12), 1, 0) == flbasic::legacySendSpan);
    CHECK (flbasic::saveSendSpanOf (left167::sendSong (127), 1, 0) == flbasic::legacySendSpan);
    CHECK (flbasic::saveSendSpanOf (left167::sendSong (128), 1, 0) == maxMixerTracks - 1);
    // an automation clip or an event lane of "Send to" that reaches past 127 needs today's span too
    auto s = left167::sendSong (12);
    const AutomationTarget sendTo { AutomationTargetKind::effectParam, 1, 0, 3 };
    Automation a;
    a.target = sendTo;
    a.points.push_back ({ 0, flbasic::sendTrackValue (5), 0.0f });
    a.points.push_back ({ 96, flbasic::sendTrackValue (100), 0.0f });
    s.automations.push_back (a);
    CHECK (flbasic::saveSendSpanOf (s, 1, 0) == flbasic::legacySendSpan);
    s.patterns.emplace_back();
    PatternEvents lane;
    lane.target = sendTo;
    lane.events.push_back ({ 0, flbasic::sendTrackValue (190) });
    s.patterns.back().events.push_back (lane);
    CHECK (flbasic::saveSendSpanOf (s, 1, 0) == maxMixerTracks - 1);
    CHECK (flbasic::sendToTargetOf (s, sendTo) == &s.mixer[1].effects[0]);
    CHECK (flbasic::sendToTargetOf (s, { AutomationTargetKind::effectParam, 1, 0, 2 }) == nullptr); // its Dry knob
    // every insert survives the trip through the file's 4 decimals in either span
    for (int track = 0; track <= 127; ++track)
    {
        const auto v = flbasic::sendTrackValue (track);
        const auto saved = (float) (std::lround (flbasic::sendTrackValueInSpan (v, 127) * 10000.0) / 10000.0);
        CHECK (flbasic::sendTrack (flbasic::rescaleSendTrackValue (saved, 127)) == track);
        CHECK (std::lround (saved * 127.0f) == track); // what LYNX 0.1.20 and older read
    }
}

TEST (left167_fruity_send_presets_carry_their_span)
{
    using namespace lynx;
    auto slot = makeEffectSlot (EffectKind::fruitySend);
    flbasic::sendToParams ({ 0, 256, 256, 9 }, slot.params.data());
    const auto text = presets::toText (EffectKind::fruitySend, "Bus 9", slot.params);
    CHECK (text.find ("Send to span = 127") != std::string::npos);
    const auto back = presets::fromText (EffectKind::fruitySend, text);
    CHECK (back && flbasic::sendFromParams (back->params.data()).track == 9);
    // a preset saved before round 166d (no span line): "Send to" over 0..127
    const auto old = presets::fromText (EffectKind::fruitySend, "LYNX preset 1\nengine = Fruity Send\nname = Old\nSend to = "
                                                                       + std::to_string (40.0 / 127.0) + "\n");
    CHECK (old && flbasic::sendFromParams (old->params.data()).track == 40);
    // past insert 127: today's span
    flbasic::sendToParams ({ 0, 256, 256, 333 }, slot.params.data());
    const auto far = presets::fromText (EffectKind::fruitySend, presets::toText (EffectKind::fruitySend, "Far", slot.params));
    CHECK (far && flbasic::sendFromParams (far->params.data()).track == 333);
}

// FL's saver writes the Playlist's "Automatic crossfades" (this project) as byte event 0x2C [DECOMP, REA_rodada167a §3];
// files without it (before FL 21) open with it off. The demo projects carry 0 or 1.
TEST (left167_flp_import_reads_the_automatic_crossfades_flag)
{
    for (const int flag : { -1, 0, 1 })
    {
        FlpBuilder b;
        b.u8 (0x9C); // tempo
        b.u32 (120000);
        if (flag >= 0)
        {
            b.u8 (0x2C);
            b.u8 ((std::uint8_t) flag);
        }
        const auto project = flp::parseProject (b.bytes);
        CHECK (project.autoCrossfades == (flag == 1));
        const auto song = lynx::importFlp (project, {});
        CHECK (song.autoCrossfades == (flag == 1));
    }
    if (! realFlpDir.empty()) // FL's own: "Basic with limiter" (FL 2026) on, "Mehdi Ebrahiminejad - Never Out" off
        for (const auto& [name, on] : { std::pair<const char*, bool> { "Templates/Minimal/Basic with limiter/Basic with limiter.flp", true },
                                        { "Demo projects/Demo songs/Mehdi Ebrahiminejad - Never Out.flp", false } })
        {
            const auto path = realFlpDir + "/" + name;
            std::ifstream in (path, std::ios::binary);
            if (! in)
            {
                std::printf ("  (missing %s)\n", path.c_str());
                continue;
            }
            std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), {});
            CHECK (flp::parseProject (bytes).autoCrossfades == on);
        }
}

namespace left167
{
// one held C4 on `channel` (Pattern mode, 120 BPM, 48 kHz), the channel's PITCH knob (`knob` semitones) or the main
// pitch (`main` cents) turned at `at` seconds, as a live tweak does: the rack state is rebuilt from the edited song
// (the instruments are kept and configured again)
std::vector<float> renderLiveTweak (const lynx::ChannelDef& channel, float knob, float main, double at, double seconds)
{
    using namespace lynx;
    Song s;
    s.mixer = Song::defaultMixer (2);
    s.bpm = 120.0;
    auto c = channel;
    c.mixerTrack = 1;
    s.channels.push_back (c);
    s.patterns.emplace_back();
    Note held;
    held.key = 60;
    held.length = ppq * 16;
    held.velocity = 0.8f;
    s.patterns[0].notes = { held };
    s.patterns[0].lengthTicks = (std::int64_t) (ppq * 32);
    s.mode = PlayMode::pattern;
    auto samples = [] (const std::string& path) -> std::shared_ptr<const SampleData> {
        auto p = path;
        std::replace (p.begin(), p.end(), '\\', '/');
        return tools::readWav (existingPathIgnoringCase (p));
    };
    std::shared_ptr<const RackState> state = RackState::build (s, nullptr, 48000.0, samples);
    RackRenderer r;
    r.prepare (48000.0, 512);
    const auto n = (int) (seconds * 48000.0);
    const auto tweak = (int) (at * 48000.0);
    std::vector<float> out;
    const auto tps = 120.0 / 60.0 * ppq / 48000.0;
    bool tweaked = false;
    for (int done = 0; done < n; done += 512)
    {
        if (! tweaked && done >= tweak)
        {
            tweaked = true;
            s.channels[0].sampler.pitch += knob;
            s.masterPitch += main;
            state = RackState::build (s, state.get(), 48000.0, samples);
        }
        std::vector<float> l (512), rr (512);
        r.process (state.get(), true, done * tps, 512, l.data(), rr.data(), nullptr);
        out.insert (out.end(), l.begin(), l.end());
    }
    return out;
}
} // namespace left167

// FL's generators follow a live PITCH knob or main pitch move on the note that is already sounding (FL 2025 render,
// REA_rodada167a §1: every synth reads +200 cents right after a +200 move). LYNX's engines took both at note on only.
TEST (left167_sounding_notes_follow_a_live_pitch_knob_and_main_pitch)
{
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    const auto print = std::getenv ("LYNX_PITCH_AUDIT") != nullptr;
    // FL measured (allpitch2.flp) the first twelve following it at once (Sakura too, but the autocorrelation can't read
    // its string model here, as in issue14_pitch_audit_of_the_instruments); Morphine, Toxic and Kepler take the same host
    // path in LYNX. The others are drums, own pitch models or unreadable by the estimate.
    const std::set<std::string> follow { "3x Osc", "Sytrus", "Harmless", "MiniSynth", "Sawer", "SimSynth", "PoiZone", "GMS",
                                         "Harmor", "Fruity DX10", "FL Keys", "BooBass", "Morphine", "Toxic Biohazard", "Kepler" };
    const auto fl = natives142::flAccess();
    for (const auto& e : natives::instrumentEngines())
    {
        const auto c = natives142::writtenWith (e.kind, fl); // (round 169l2: the preset it was measured with)
        if (! c)
            continue;
        const auto knobOut = left167::renderLiveTweak (*c, 2.0f, 0.0f, 0.75, 2.0);
        const auto mainOut = left167::renderLiveTweak (*c, 0.0f, 200.0f, 0.75, 2.0);
        const auto base = issue14::pitchAt (knobOut, 0.5);
        const auto knob = issue14::cents (base, issue14::pitchAt (knobOut, 1.25));
        const auto main = issue14::cents (issue14::pitchAt (mainOut, 0.5), issue14::pitchAt (mainOut, 1.25));
        if (print)
            std::printf ("  %-28s %9.1f knob %+6.0f main %+6.0f\n", e.name, base, knob, main);
        if (follow.count (e.name) != 0)
        {
            if (std::abs (knob - 200.0) > 30.0 || std::abs (main - 200.0) > 30.0)
                std::printf ("  live pitch: %s doesn't follow (knob %+.0f, main %+.0f cents)\n", e.name, knob, main);
            CHECK (std::abs (knob - 200.0) < 30.0);
            CHECK (std::abs (main - 200.0) < 30.0);
        }
    }
}

// The live follow keeps the output bit-exact when nothing moves, and a later slide note still lands on its pitch
TEST (left167_host_slide_live_jump)
{
    using namespace lynx;
    HostSlide s;
    s.anchor (100.0f);
    s.follow (100.0f);
    CHECK (! s.moving());
    s.follow (300.0f); // +200
    CHECK (s.moving());
    CHECK (s.advance (64.0f) == 200.0f);
    CHECK (! s.moving());
    s.follow (300.0f);
    CHECK (! s.moving());
    // a pending jump and a slide in the same block: the glide ends where it was asked to
    s.follow (400.0f);
    s.start (900.0f, 100.0f);
    s.advance (50.0f);
    CHECK (std::abs (s.cents - 600.0f) < 0.01f); // from 300 (200 + the jump) half-way to 900
    s.advance (100.0f);
    CHECK (s.cents == 900.0f);
    HostSlide untracked; // never anchored: an engine driven alone doesn't follow
    untracked.follow (500.0f);
    CHECK (! untracked.moving());
}

// FL's "Render armed tracks to wave file(s)" with "Auto-create audio clip" (FL 2026 [SHOT s28–s37]): a new audio clip
// channel named after the file, to the Master ("---"), and a clip of the whole file from bar 1 on the first Playlist
// track after every used track (Track 28 when 27 were used), the track keeping its name
TEST (left167_rendered_tracks_go_on_the_first_free_playlist_track)
{
    using namespace lynx;
    Song s;
    s.mixer = Song::defaultMixer (4);
    s.bpm = 120.0;
    s.patterns.emplace_back();
    Clip c;
    c.pattern = 0, c.track = 26, c.start = 0, c.length = ppq * 4;
    s.playlist.push_back (c);
    c.track = 3;
    s.playlist.push_back (c);
    CHECK (mixerops::firstTrackAfterUsed (s) == 27);
    const auto channels = s.channels.size();
    const auto placed = mixerops::placeRenderedFiles (s, { { "C:\\Users\\me\\Recorded\\recto.wav", 48000.0 * 4.0, 48000.0 },
                                                           { "/tmp/none.wav", 0.0, 48000.0 },
                                                           { "/tmp/two.flac", 44100.0 * 2.0, 44100.0 } });
    CHECK (placed == 2);
    CHECK (s.channels.size() == channels + 2);
    const auto& ch = s.channels[channels];
    CHECK (ch.name == "recto" && ch.instrument == InstrumentKind::audioClip && ch.mixerTrack == 0);
    CHECK (ch.samplePath == "C:\\Users\\me\\Recorded\\recto.wav");
    CHECK (s.channels[channels + 1].name == "two");
    const auto& clip = s.playlist[2];
    CHECK (clip.kind == ClipKind::audio && clip.channel == (int) channels && clip.track == 27 && clip.start == 0);
    CHECK (clip.length == (std::int64_t) (ppq * 8)); // 4 s at 120 BPM = 8 beats
    CHECK (s.playlist[3].track == 28 && s.playlist[3].length == (std::int64_t) (ppq * 4));
    CHECK (s.playlistTracks.size() <= 27 || s.playlistTracks[27].name.empty()); // the track keeps its (default) name
    Song empty;
    CHECK (mixerops::firstTrackAfterUsed (empty) == 0);
    CHECK (mixerops::insertDisplayName (s, 2) == "Insert 2");
    s.mixer[2].name = "Vocals";
    CHECK (mixerops::insertDisplayName (s, 2) == "Vocals");
}

// Slide notes on drums (REA_rodada167a §2): FL never retriggers a hit on a slide; BassDrum and Drumaxx read the host's
// per-note pitch every block, so the hit still ringing glides; Fruit Kick reads it only at the trigger (no change)
TEST (left167_drum_slides_bend_bassdrum_and_drumaxx_only)
{
    using namespace lynx;
    if (realFlpDir.empty())
        return;
    const auto fl = natives142::flAccess();
    Note hit;
    hit.key = 60;
    hit.length = ppq * 2;
    hit.velocity = 0.8f;
    Note slide;
    slide.key = 67;
    slide.start = ppq / 4;
    slide.length = ppq / 4;
    slide.slide = true;
    for (const auto kind : { InstrumentKind::bassdrum, InstrumentKind::drumaxx, InstrumentKind::fruitkick })
    {
        const auto c = natives::makeChannel (kind, fl);
        if (! c)
            continue;
        const auto plain = issue14::renderPattern (*c, { hit }, 0.0f, 0.5);
        const auto slid = issue14::renderPattern (*c, { hit, slide }, 0.0f, 0.5);
        const auto at = (std::size_t) (0.125 * 48000.0); // the slide starts at 1/4 beat = 0.125 s (120 BPM)
        bool same = true, sameBefore = true;
        double energy = 0.0;
        for (std::size_t i = 0; i < plain.size() && i < slid.size(); ++i)
        {
            if (plain[i] != slid[i])
                (i < at ? sameBefore : same) = false;
            energy += (double) plain[i] * plain[i];
        }
        CHECK (sameBefore); // a slide note starts nothing
        if (energy > 1e-6)
            CHECK (same == (kind == InstrumentKind::fruitkick));
    }
}
