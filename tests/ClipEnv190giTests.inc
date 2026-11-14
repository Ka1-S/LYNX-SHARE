// Round 190gi: audio clip envelopes (slice B3, core/ClipEnvelope.h) and warp from the Playlist (slice B6, core/ClipWarp.h)

#include "core/AlsExport.h"
#include "core/AlsImport.h"
#include "core/ClipEnvelope.h"
#include "core/ClipWarp.h"
#include "core/FlpExport.h"
#include "core/PlaylistTrackOps.h"
#include "core/XmlLite.h"

namespace clipenv190gi
{
// one audio clip channel playing a DC sample of 1.0 (4 s at 48 kHz) from tick 0 for 4 beats at 120 BPM (2 s)
inline lynx::Song dcSong()
{
    lynx::Song s;
    s.bpm = 120.0;
    s.mode = lynx::PlayMode::song;
    s.mixer = lynx::Song::defaultMixer (1);
    lynx::ChannelDef c;
    c.instrument = lynx::InstrumentKind::audioClip;
    c.samplePath = "dc.wav";
    c.volume = 1.0f;
    c.mixerTrack = 1;
    s.channels.push_back (c);
    s.patterns.emplace_back();
    lynx::Clip clip;
    clip.kind = lynx::ClipKind::audio;
    clip.channel = 0;
    clip.length = lynx::ppq * 4;
    s.playlist = { clip };
    return s;
}

// the left output, `frames` samples from tick 0
inline std::vector<float> render (const lynx::Song& s, int frames, std::vector<float>* rightOut = nullptr)
{
    auto provider = [] (const std::string&) {
        auto d = std::make_shared<lynx::SampleData>();
        d->left.assign (48000 * 4, 1.0f);
        return std::shared_ptr<const lynx::SampleData> (d);
    };
    auto state = lynx::RackState::build (s, nullptr, 48000.0, provider);
    lynx::RackRenderer r;
    r.prepare (48000.0, 512);
    std::vector<float> all, right, l (512), rr (512);
    const auto tps = lynx::ticksPerSample (s.bpm, 48000.0);
    for (int done = 0; done < frames; done += 512)
    {
        r.process (state.get(), true, done * tps, 512, l.data(), rr.data());
        all.insert (all.end(), l.begin(), l.end());
        right.insert (right.end(), rr.begin(), rr.end());
    }
    all.resize ((std::size_t) frames);
    right.resize ((std::size_t) frames);
    if (rightOut != nullptr)
        *rightOut = right;
    return all;
}

inline lynx::AutomationPoint point (std::int64_t tick, float value)
{
    lynx::AutomationPoint p;
    p.tick = tick;
    p.value = value;
    return p;
}

// a volume envelope from 1 (unity) at the clip's start down to 0.4 at its end, straight
inline lynx::Song rampSong()
{
    auto s = dcSong();
    lynx::ClipEnvelope e;
    e.volume = { point (0, 1.0f), point (lynx::ppq * 4, 0.4f) };
    s.clipEnvelopes.push_back (e);
    s.playlist[0].envelope = 0;
    return s;
}
} // namespace clipenv190gi

TEST (clipenv190gi_gain_law_is_fl_levels_adjustment)
{
    using namespace lynx;
    CHECK (clipenv::gainOf (1.0f) == 1.0f && clipenv::gainOf (0.0f) == 0.0f);
    CHECK (std::abs (clipenv::gainOf (0.5f) - (std::pow (11.0f, 0.5f) - 1.0f) / 10.0f) < 1.0e-6f);
    CHECK (clipenv::gainOf (0.5f) == levelAdjustGain (0.5f));
    CHECK (clipenv::panOf (0.5f) == 0.0f && clipenv::panOf (0.0f) == -1.0f && clipenv::panOf (1.0f) == 1.0f);
    CHECK (clipenv::toLive (0.0f) == -1.0 && clipenv::toLive (1.0f) == 1.0 && clipenv::fromLive (0.0) == 0.5f);
    ClipEnvelope flat;
    flat.volume = { clipenv190gi::point (0, 1.0f), clipenv190gi::point (10, 1.0f) };
    flat.pan = { clipenv190gi::point (0, 0.5f) };
    CHECK (! clipenv::active (flat));
    flat.pan[0].value = 0.3f;
    CHECK (clipenv::active (flat));
}

TEST (clipenv190gi_render_follows_the_volume_envelope)
{
    // rendered with the envelope / rendered without = the envelope's gain at each sample (16-sample ramps between reads)
    using namespace lynx;
    const auto plain = clipenv190gi::render (clipenv190gi::dcSong(), 48000);
    const auto song = clipenv190gi::rampSong();
    const auto shaped = clipenv190gi::render (song, 48000);
    const auto tps = ticksPerSample (song.bpm, 48000.0);
    int checked = 0;
    float worst = 0.0f;
    for (int i = 1000; i < 47000; i += 997)
    {
        if (std::abs (plain[(std::size_t) i]) < 1.0e-3f)
            continue;
        const auto expected = clipenv::gainAt (song.clipEnvelopes[0], i * tps);
        worst = std::max (worst, std::abs (shaped[(std::size_t) i] / plain[(std::size_t) i] - expected));
        ++checked;
    }
    CHECK (checked > 40);
    CHECK (worst < 0.002f);
    // a quarter way (sample 24000 = 0.5 s = one beat at 120 BPM): v = 1 − 0.6 / 4 = 0.85 → (11^0.85 − 1) / 10
    CHECK (std::abs (shaped[24000] / plain[24000] - (std::pow (11.0f, 0.85f) - 1.0f) / 10.0f) < 0.003f);
}

TEST (clipenv190gi_flat_or_missing_envelope_renders_bit_identical)
{
    using namespace lynx;
    const auto plain = clipenv190gi::render (clipenv190gi::dcSong(), 24000);
    auto flat = clipenv190gi::dcSong();
    ClipEnvelope e;
    e.volume = { clipenv190gi::point (0, 1.0f), clipenv190gi::point (ppq * 4, 1.0f) };
    e.shown = clipenv::volumeLane;
    flat.clipEnvelopes.push_back (e);
    flat.playlist[0].envelope = 0;
    CHECK (clipenv190gi::render (flat, 24000) == plain);
    auto stale = clipenv190gi::dcSong();
    stale.playlist[0].envelope = 5; // (an index past the table: none)
    CHECK (clipenv190gi::render (stale, 24000) == plain);
    // the scheduler hands a voice only an active envelope
    CHECK (clipenv::activeOf (flat, flat.playlist[0]) == nullptr && clipenv::activeOf (stale, stale.playlist[0]) == nullptr);
}

TEST (clipenv190gi_pan_envelope_moves_the_clip)
{
    using namespace lynx;
    auto song = clipenv190gi::dcSong();
    ClipEnvelope e;
    e.pan = { clipenv190gi::point (0, 0.0f) }; // hard left all along
    song.clipEnvelopes.push_back (e);
    song.playlist[0].envelope = 0;
    std::vector<float> right, plainRight;
    const auto left = clipenv190gi::render (song, 12000, &right);
    const auto plain = clipenv190gi::render (clipenv190gi::dcSong(), 12000, &plainRight);
    CHECK (std::abs (right[6000]) < 1.0e-4f && std::abs (plainRight[6000]) > 0.1f);
    CHECK (left[6000] > plain[6000] * 1.3f); // (constant power: the left side gets √2)
}

TEST (clipenv190gi_envelope_stays_on_its_audio_when_trimmed_split_or_stretched)
{
    using namespace lynx;
    auto song = clipenv190gi::rampSong();
    const auto& e = song.clipEnvelopes[0];
    const auto at = clipenv::gainAt (e, clipenv::clipTick (song.playlist[0], ppq * 3.0));
    // trim the start by a beat: the same song tick plays the same point of the envelope
    auto trimmed = song.playlist[0];
    trimmed.start += ppq;
    trimmed.offset += ppq;
    trimmed.length -= ppq;
    CHECK (clipenv::gainAt (e, clipenv::clipTick (trimmed, ppq * 3.0)) == at);
    // a split: both halves share the entry and keep the mapping; editing one gives it its own copy
    song.playlist.push_back (splitClip (song.playlist[0], ppq * 2));
    CHECK (song.playlist[1].envelope == 0 && clipenv::users (song, 0) == 2);
    CHECK (clipenv::gainAt (song.clipEnvelopes[0], clipenv::clipTick (song.playlist[1], ppq * 3.0)) == at);
    auto& own = clipenv::own (song, song.playlist[1]);
    own.volume[1].value = 0.0f;
    CHECK (song.playlist[1].envelope == 1 && song.playlist[0].envelope == 0 && song.clipEnvelopes[0].volume[1].value == 0.4f);
    // FL's Shift+drag stretch ×2 (called at every move with the start state): the points scale once, from the start state
    auto stretched = clipenv190gi::rampSong();
    const auto before = stretched.playlist;
    pltracks::stretchAudioClip (stretched, before, 0, 0, ppq * 6, false);
    pltracks::stretchAudioClip (stretched, before, 0, 0, ppq * 8, false);
    const auto& scaled = stretched.clipEnvelopes[(std::size_t) stretched.playlist[0].envelope];
    CHECK (stretched.playlist[0].envelope != 0 && scaled.volume[1].tick == ppq * 8 && stretched.clipEnvelopes[0].volume[1].tick == ppq * 4);
    CHECK (stretched.clipEnvelopes.size() == 2); // (the drag reused its own entry)
}

TEST (clipenv190gi_show_reset_and_save_order)
{
    using namespace lynx;
    auto song = clipenv190gi::dcSong();
    clipenv::show (song, song.playlist[0], clipenv::panLane);
    CHECK (song.playlist[0].envelope == 0 && song.clipEnvelopes[0].shown == clipenv::panLane);
    CHECK (song.clipEnvelopes[0].pan.size() == 2 && song.clipEnvelopes[0].pan[0].value == 0.5f && song.clipEnvelopes[0].volume.empty());
    CHECK (! clipenv::active (song.clipEnvelopes[0])); // (a lane just shown changes nothing)
    song.clipEnvelopes[0].pan[1].value = 1.0f;
    clipenv::reset (song, song.playlist[0], clipenv::panLane);
    CHECK (! clipenv::active (song.clipEnvelopes[0]) && song.clipEnvelopes[0].pan.size() == 2);
    clipenv::show (song, song.playlist[0], 0);
    CHECK (song.clipEnvelopes[0].shown == 0);
    // the save writes the used entries only, in the order of first use
    song.clipEnvelopes.emplace_back();
    song.clipEnvelopes.emplace_back();
    song.playlist[0].envelope = 2;
    int count = 0;
    const auto remap = clipenv::usedOrder (song, &count);
    CHECK (count == 1 && remap[0] == -1 && remap[1] == -1 && remap[2] == 0);
    auto none = clipenv190gi::dcSong();
    CHECK (clipenv::usedOrder (none, &count).empty() && count == 0 && ! clipenv::anyActive (none));
}

TEST (clipenv190gi_flp_export_writes_fl_automate_clips_that_sound_the_same)
{
    // FL has no clip envelope: the export writes FL's Automate › Volume / Panning (an automation clip of the clip
    // channel's Levels adjustment over the clip), the same curve, and LYNX plays that the same
    using namespace lynx;
    auto song = clipenv190gi::rampSong();
    song.clipEnvelopes[0].pan = { clipenv190gi::point (0, 0.5f), clipenv190gi::point (ppq * 4, 0.25f) };
    auto baked = song;
    CHECK (clipenv::bakeToAutomation (baked) == 2);
    CHECK (baked.playlist[0].envelope == -1 && baked.automations.size() == 2 && baked.playlist.size() == 3);
    const auto& vol = baked.automations[0];
    CHECK (vol.target.kind == AutomationTargetKind::channelAdjust && vol.target.index == 0 && vol.target.param == leveladjust::volume);
    CHECK (vol.points.front().tick == 0 && std::abs (vol.points.front().value - 0.5f) < 1.0e-6f);
    CHECK (std::abs (vol.points[vol.points.size() - 2].value - 0.2f) < 1.0e-6f && vol.points.back().value == 0.5f); // (unity after the clip)
    CHECK (baked.automations[1].target.param == leveladjust::pan && std::abs (baked.automations[1].points[1].value - 0.25f) < 1.0e-6f);
    CHECK (baked.playlist[1].kind == ClipKind::automation && baked.playlist[1].start == 0 && baked.playlist[1].length == ppq * 4);
    // LYNX plays the baked automation as it plays the envelope (the channel's Levels adjustment per block)
    auto volumeOnly = clipenv190gi::rampSong();
    auto bakedVolume = volumeOnly;
    clipenv::bakeToAutomation (bakedVolume);
    const auto a = clipenv190gi::render (volumeOnly, 46000), b = clipenv190gi::render (bakedVolume, 46000);
    double diff = 0.0, ref = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        diff += (a[i] - b[i]) * (a[i] - b[i]);
        ref += a[i] * a[i];
    }
    CHECK (ref > 0.0 && std::sqrt (diff / ref) < 0.01);
    // through the .flp: Levels adjustment automation clips of that channel come back
    flpexport::Report report;
    const auto project = flpexport::toProject (song, {}, report);
    CHECK (std::any_of (report.notes.begin(), report.notes.end(), [] (const std::string& n) { return n.rfind ("Clip envelopes: 2 envelope(s)", 0) == 0; }));
    FlpImportOptions o;
    o.fileExists = [] (const std::string&) { return true; };
    const auto back = importFlp (flp::parseProject (flp::writeProject (project)), o);
    int adjust = 0;
    for (const auto& x : back.automations)
        if (x.target.kind == AutomationTargetKind::channelAdjust)
            ++adjust;
    CHECK (adjust == 2);
}

TEST (clipenv190gi_als_round_trip_of_clip_gain_and_track_panning)
{
    using namespace lynx;
    auto song = clipenv190gi::rampSong();
    song.clipEnvelopes[0].pan = { clipenv190gi::point (0, 0.5f), clipenv190gi::point (ppq * 2, 0.0f) };
    const auto out = exportAls (song);
    CHECK (out.xml.find ("<ClipEnvelope") != std::string::npos);
    CHECK (out.xml.find ("@lynx-clip") == std::string::npos); // (every placeholder became the track's target id)
    // the Clip Gain envelope points at the track's MainSequencer/VolumeModulationTarget
    xml::Document doc;
    CHECK (doc.parse (out.xml));
    bool pointsAtVolume = false;
    for (const auto& t : doc.root().child ("LiveSet").child ("Tracks").children ("AudioTrack"))
    {
        const auto vol = t.path ("DeviceChain/MainSequencer/VolumeModulationTarget").attr ("Id");
        for (const auto& clip : t.path ("DeviceChain/MainSequencer/Sample/ArrangerAutomation/Events").children ("AudioClip"))
            for (const auto& env : clip.path ("Envelopes/Envelopes").children ("ClipEnvelope"))
                pointsAtVolume = pointsAtVolume || (! vol.empty() && env.path ("EnvelopeTarget/PointeeId").attr ("Value") == vol);
    }
    CHECK (pointsAtVolume);
    AlsImportOptions o;
    o.common.projectFolder = "E:/sets";
    o.common.fileExists = [] (const std::string&) { return true; };
    const auto back = importAls (out.bytes, o);
    CHECK (back.has_value());
    if (! back)
        return;
    const Clip* audio = nullptr;
    for (const auto& c : back->playlist)
        if (c.kind == ClipKind::audio)
            audio = &c;
    CHECK (audio != nullptr && clipenv::activeOf (*back, *audio) != nullptr);
    if (audio == nullptr || clipenv::of (*back, *audio) == nullptr)
        return;
    const auto& e = *clipenv::of (*back, *audio);
    // the same curve at the same song ticks (Live's clip beats ↔ LYNX's content ticks)
    for (const auto t : { 0.0, ppq * 1.0, ppq * 2.5, ppq * 3.9 })
    {
        CHECK (std::abs (clipenv::valueAt (e, clipenv::volumeLane, clipenv::clipTick (*audio, t)) - clipenv::valueAt (song.clipEnvelopes[0], clipenv::volumeLane, t)) < 1.0e-4f);
        CHECK (std::abs (clipenv::valueAt (e, clipenv::panLane, clipenv::clipTick (*audio, t)) - clipenv::valueAt (song.clipEnvelopes[0], clipenv::panLane, t)) < 1.0e-4f);
    }
    // a set without clip envelopes: nothing made
    const auto plain = importAls (exportAls (clipenv190gi::dcSong()).bytes, o);
    CHECK (plain.has_value() && plain->clipEnvelopes.empty());
}

TEST (clipenv190gi_playlist_warp_commands)
{
    // Auto-warp, Quantize audio and a marker added on the flag strip, on a clip (core/ClipWarp.h): one channel edited, a
    // shared channel copied first
    using namespace lynx;
    auto song = clipenv190gi::dcSong();
    SampleData clicks;
    clicks.sampleRate = 48000.0;
    clicks.left.assign (48000 * 4, 0.0f); // 4 s, a click every 0.5 s (120 BPM), slightly late every other beat
    for (int b = 0; b < 8; ++b)
    {
        const auto at = b * 24000 + (b % 2 == 1 ? 1200 : 0);
        for (int i = 0; i < 400; ++i)
            clicks.left[(std::size_t) (at + i)] = (i % 2 == 0 ? 0.9f : -0.9f) * (1.0f - (float) i / 400.0f);
    }
    song.playlist.push_back (song.playlist[0]); // (two clips of one channel)
    song.playlist[1].start = ppq * 8;
    const auto placed = clipwarp::autoWarp (song, 1, clicks, "clicks 120 bpm");
    CHECK (placed >= 2);
    CHECK (song.channels.size() == 2 && song.playlist[1].channel == 1 && song.playlist[0].channel == 0); // (its own channel)
    CHECK (song.channels[1].sampler.warp.size() >= 2 && song.channels[1].sampler.keepPitch && song.channels[0].sampler.warp.empty());
    const auto quantized = clipwarp::quantize (song, 1, clicks);
    CHECK (quantized > 0);
    const auto before = song.channels[1].sampler.warp.size();
    const auto added = clipwarp::addMarker (song, 1, 0.73, 4.0);
    CHECK (added > 0 && song.channels[1].sampler.warp.size() == before + 1);
    // nothing to read a tempo from (a file of 8 frames): nothing changed
    SampleData silence;
    silence.sampleRate = 48000.0;
    silence.left.assign (8, 0.0f);
    auto quiet = clipenv190gi::dcSong();
    CHECK (clipwarp::autoWarp (quiet, 0, silence, "x") == 0 && quiet.channels[0].sampler.warp.empty());
}
