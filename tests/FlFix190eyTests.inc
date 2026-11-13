// Round 190ey: what the live FL session 190ew found different (part A: the Sampler page, the control menus, the Playlist,
// issue #152's question). FL facts: /mnt/e/lynx-work/live190ew/ANSWERS.md (shots in its shots/ folder).
#include "core/AudioEdit.h"
#include "core/ClipOps.h"
#include "core/FlClipMenu.h"
#include "core/FlControlMenus.h"
#include "core/LastTweaked.h"
#include "core/LiveInput.h"
#include "core/SamplerPage.h"
#include "core/TempoDialog.h"

namespace flfix190ey
{
inline void put32 (std::vector<std::uint8_t>& v, std::uint32_t x)
{
    for (int i = 0; i < 4; ++i)
        v.push_back ((std::uint8_t) (x >> (8 * i)));
}
inline void chunk (std::vector<std::uint8_t>& v, const char* id, const std::vector<std::uint8_t>& body)
{
    v.insert (v.end(), id, id + 4);
    put32 (v, (std::uint32_t) body.size());
    v.insert (v.end(), body.begin(), body.end());
    if (body.size() & 1)
        v.push_back (0);
}
// a RIFF WAVE holding only `chunks` (the extras reader needs no fmt / data)
inline std::vector<std::uint8_t> wave (const std::vector<std::pair<const char*, std::vector<std::uint8_t>>>& chunks)
{
    std::vector<std::uint8_t> body { 'W', 'A', 'V', 'E' };
    for (const auto& [id, b] : chunks)
        chunk (body, id, b);
    std::vector<std::uint8_t> out { 'R', 'I', 'F', 'F' };
    put32 (out, (std::uint32_t) body.size());
    out.insert (out.end(), body.begin(), body.end());
    return out;
}
// FL's "strc" list (round 190aj layout): u32, u32 count, 20 header bytes, then 24-byte entries
inline std::vector<std::uint8_t> strc (std::uint32_t count)
{
    std::vector<std::uint8_t> b;
    put32 (b, 1);
    put32 (b, count);
    for (int i = 0; i < 5; ++i)
        put32 (b, 0);
    for (std::uint32_t i = 0; i < count; ++i)
        for (int k = 0; k < 6; ++k)
            put32 (b, k == 1 ? i * 1000 : (i == 0 && k == 0 ? 2u : 0u));
    return b;
}
inline std::vector<std::string> waveMenuTexts()
{
    std::vector<std::string> out;
    for (const auto& e : lynx::samplerpage::waveMenu())
        out.push_back (e.text == nullptr ? std::string ("-") : std::string (e.text));
    return out;
}
} // namespace flfix190ey

TEST (flfix190ey_sampler_content_greys_as_fl)
{
    using namespace lynx::samplerpage;
    // SMP-1: Resample greys at the project's rate (44.1 kHz 808s greyed; a 48 kHz sample in a 44.1 kHz project enabled)
    CHECK (! resampleAvailable (true, 44100.0, 44100.0));
    CHECK (resampleAvailable (true, 48000.0, 44100.0));
    CHECK (resampleAvailable (true, 44100.0, 48000.0));
    // SMP-2: no sample, the whole Content box is greyed
    CHECK (! resampleAvailable (false, 48000.0, 44100.0) && ! loadRegionsAvailable (false, true) && ! loadSliceMarkersAvailable (false, true));
    // Load regions / Load slice markers: only for a file that has them
    CHECK (! loadRegionsAvailable (true, false) && loadRegionsAvailable (true, true));
    CHECK (! loadSliceMarkersAvailable (true, false) && loadSliceMarkersAvailable (true, true));
}

TEST (flfix190ey_wav_slice_markers_are_read)
{
    using namespace lynx;
    // FL's own save of one region (live check 190ew EDI-2: strc 76 bytes = 28 + 2 x 24, count 2): one slice marker
    const auto one = flfix190ey::wave ({ { "strc", flfix190ey::strc (2) } });
    CHECK (one.size() == 12 + 8 + 76);
    CHECK (audioedit::decodeWavExtras (one.data(), one.size(), 44100).sliceMarkers == 1);
    const auto three = flfix190ey::wave ({ { "strc", flfix190ey::strc (4) } });
    CHECK (audioedit::decodeWavExtras (three.data(), three.size(), 44100).sliceMarkers == 3);
    // no list, or a list with its first entry only: none
    const auto none = flfix190ey::wave ({ { "acid", std::vector<std::uint8_t> (24, 0) } });
    CHECK (audioedit::decodeWavExtras (none.data(), none.size(), 44100).sliceMarkers == 0);
    const auto empty = flfix190ey::wave ({ { "strc", flfix190ey::strc (1) } });
    CHECK (audioedit::decodeWavExtras (empty.data(), empty.size(), 44100).sliceMarkers == 0);
    // a short (broken) list is ignored
    const auto broken = flfix190ey::wave ({ { "strc", std::vector<std::uint8_t> (20, 7) } });
    CHECK (audioedit::decodeWavExtras (broken.data(), broken.size(), 44100).sliceMarkers == 0);
}

TEST (flfix190ey_crossfade_on_a_loopless_sample_loops_it_all)
{
    using namespace lynx;
    using namespace lynx::samplerpage;
    // SMP-4: CROSSFADE 0 -> > 0 on a sample with no loop: Use loop points on, END at the sample's end, START at 0
    SamplerSettings s;
    s.useLoopPoints = false;
    turnCrossfade (s, 300, 44100);
    CHECK (s.useLoopPoints && s.precompute.crossfade == 300);
    CHECK (s.loopStart == 0 && s.loopEnd == 44099);
    // turning it further keeps the loop as it is now
    s.loopEnd = 20000;
    turnCrossfade (s, 500, 44100);
    CHECK (s.loopStart == 0 && s.loopEnd == 20000);
    // back to 0: Use loop points stays on, the loop stays
    turnCrossfade (s, 0, 44100);
    CHECK (s.useLoopPoints && s.precompute.crossfade == 0 && s.loopEnd == 20000);
    // the file's own loop: no loop knob points are made
    SamplerSettings f;
    turnCrossfade (f, 300, 44100, 1000, 5000);
    CHECK (f.useLoopPoints && ! f.hasLoopOverride());
    // loop knob points already set: kept
    SamplerSettings k;
    k.loopStart = 100;
    k.loopEnd = 900;
    turnCrossfade (k, 300, 44100);
    CHECK (k.loopStart == 100 && k.loopEnd == 900);
    // the old call (no length known) only ticks Use loop points, as before
    SamplerSettings o;
    o.useLoopPoints = false;
    turnCrossfade (o, 300);
    CHECK (o.useLoopPoints && ! o.hasLoopOverride());
}

TEST (flfix190ey_mul_text_bit_depth_and_summary)
{
    using namespace lynx::samplerpage;
    // SMP-5: "Time stretch (multiplicator): 105.70%" / 111.73% / 89.50%
    CHECK (mulText (800) == "105.70%");
    CHECK (mulText (1600) == "111.73%");
    CHECK (mulText (-1600) == "89.50%");
    CHECK (mulText (0) == "100.00%");
    CHECK (mulText (10000) == "200.00%");
    // SMP-7b: the box shows the file's bit depth, 32 once precomputed
    CHECK (bitDepthShown (16, false) == 16 && bitDepthShown (24, false) == 24 && bitDepthShown (16, true) == 32);
    CHECK (bitDepthShown (0, false) == 0);
    // SMP-7: FL's hint "44100Hz, 16Bit int stereo, 0:263, in RAM"
    CHECK (sampleSummary (44100.0, 16, false, true, 0.263, false) == "44100Hz, 16Bit int stereo, 0:263, in RAM");
    CHECK (sampleSummary (48000.0, 32, true, false, 51.0634, true) == "48000Hz, 32Bit float mono, 51:063, on disk");
}

TEST (flfix190ey_waveform_menu_is_fls)
{
    using namespace lynx;
    // SMP-7 (FL 2026 shot SMP-7-waveform-right-click-menu.png), top to bottom
    const std::vector<std::string> fl { "Reload", "Save as...", "Locate file", "Copy audio to clipboard", "Paste audio from clipboard", "-",
                                        "Edit in audio editor", "Edit in pitch corrector", "Edit in time warper", "-",
                                        "Open in new sampler channel", "Open in new Fruity Granulizer channel", "Open in new Fruity Slicer channel",
                                        "Open in new Fruity Slicer 2 channel", "Open in new Slicex channel", "-", "Detect tempo", "Get properties", "-",
                                        "Spectrum view", "Multichannel waveform view" };
    CHECK (flfix190ey::waveMenuTexts() == fl);
    // what LYNX performs (the rest is left out of the menu, no dead stubs)
    std::vector<std::string> have;
    for (const auto& e : samplerpage::waveMenu())
        if (e.text != nullptr && e.have)
            have.push_back (e.text);
    const std::vector<std::string> expected { "Reload", "Save as...", "Locate file", "Copy audio to clipboard", "Paste audio from clipboard",
                                              "Edit in audio editor", "Open in new sampler channel", "Open in new Fruity Granulizer channel",
                                              "Detect tempo" };
    CHECK (have == expected);
    for (const auto& e : samplerpage::waveMenu())
        if (e.text != nullptr && (std::string (e.text) == "Copy audio to clipboard" || std::string (e.text) == "Edit in audio editor"))
            CHECK (std::string (e.shortcut) == (std::string (e.text) == "Copy audio to clipboard" ? "Ctrl+C" : "Ctrl+E"));
}

TEST (flfix190ey_remove_sample_renames_the_channel)
{
    using namespace lynx;
    // CTL-4: "808 Clap" -> "Sampler", no question; the sample, zones, slices and loop points go
    ChannelDef d;
    d.name = "808 Clap";
    d.instrument = InstrumentKind::sampler;
    d.samplePath = "/x/808 Clap.wav";
    d.sampler.slices = { 0.0f, 0.5f };
    d.sampler.loopStart = 10;
    d.sampler.loopEnd = 20;
    d.sampler.useLoopPoints = false;
    samplerpage::removeSample (d);
    CHECK (d.name == "Sampler" && d.samplePath.empty() && d.sampler.slices.empty() && ! d.sampler.hasLoopOverride());
    CHECK (! d.sampler.useLoopPoints); // (FL: the mode and loop switch are kept)
}

TEST (flfix190ey_multilink_off_menu_has_the_control_column)
{
    using namespace lynx::mlmenu;
    // CTL-1: off with a last tweaked control: two columns; on: FL's menu alone; nothing tweaked: one column
    CHECK (showsOwnColumn (false, true));
    CHECK (! showsOwnColumn (true, true) && ! showsOwnColumn (false, false));
    // LYNX's greyed one-step lines are gone while off
    CHECK (! showsLynxOneStepItems (false) && showsLynxOneStepItems (true));
    // the right column stays FL's "Last 2 tweaked parameters" menu
    CHECK (captionFor (false, 2) == "Last 2 tweaked parameters");
}

TEST (flfix190ey_automate_names_and_level)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    song.channels.clear();
    song.playlist.clear();
    song.automations.clear();
    ChannelDef bass;
    bass.name = "Amount Too - Bass";
    bass.instrument = InstrumentKind::audioClip;
    bass.misc.levels.volume = 1.468f; // (the session's 147 %)
    song.channels.push_back (bass);
    Clip audio;
    audio.kind = ClipKind::audio;
    audio.channel = 0;
    audio.track = 2;
    audio.start = 4 * ppq;
    audio.length = 6 * 4 * ppq;
    song.playlist.push_back (audio);
    const auto target = clipops::automateTarget (song, 0, clipops::AutomateWhat::volume);
    const std::string base = "Amount Too - Bass - Volume multiplier";
    // PL-1 / PL-2: the first clip has the plain name, the next " #2", " #3"
    CHECK (clipops::automateName (song, target, base) == base);
    const auto first = clipops::automateAudioClip (song, 0, clipops::AutomateWhat::volume, clipops::automateName (song, target, base));
    CHECK (first >= 0 && clipops::automateName (song, target, base) == base + " #2");
    clipops::automateAudioClip (song, 0, clipops::AutomateWhat::volume, clipops::automateName (song, target, base));
    CHECK (song.automations.back().name == base + " #2" && clipops::automateName (song, target, base) == base + " #3");
    // another target keeps its own count
    const auto pan = clipops::automateTarget (song, 0, clipops::AutomateWhat::panning);
    CHECK (clipops::automateName (song, pan, "p") == "p");
    // a live controller link of the same target is no clip: not counted
    Automation link;
    link.target = target;
    link.source.channel = 0;
    song.automations.push_back (link);
    CHECK (clipops::automateName (song, target, base) == base + " #3");
    // PL-2: with Levels VOL at 147 % FL's clip plays unity (range top at unity, line at the top): 0.5 of LYNX's travel
    const auto& a = song.automations[(std::size_t) first];
    CHECK (a.points.front().value == 0.5f && a.points.front().value == a.points.back().value);
    CHECK (lasttweak::valueText (song, target, a.points.front().value) == "100%  0.0dB  1.00");
    // below unity the line is at the value (75 % -> 0.375)
    song.channels[0].misc.levels.volume = 0.75f;
    const auto low = clipops::automateAudioClip (song, 0, clipops::AutomateWhat::volume, "low");
    CHECK (low >= 0 && std::abs (song.automations[(std::size_t) low].points.front().value - 0.375f) < 1.0e-6f);
    // Panning keeps its value
    song.channels[0].misc.levels.pan = 0.5f;
    const auto panned = clipops::automateAudioClip (song, 0, clipops::AutomateWhat::panning, "p");
    CHECK (panned >= 0 && std::abs (song.automations[(std::size_t) panned].points.front().value - 0.75f) < 1.0e-6f);
}

TEST (flfix190ey_level_volume_text_is_fls)
{
    using namespace lynx;
    // PL-6: "Volume multiplier: 147%  10.3dB  3.28", "100%  0.0dB  1.00", "106%  1.4dB  1.18" (FL's curve, levelAdjustGain)
    CHECK (levelVolumeText (1.468f) == "147%  10.3dB  3.28");
    CHECK (levelVolumeText (1.0f) == "100%  0.0dB  1.00");
    CHECK (levelVolumeText (1.063f) == "106%  1.4dB  1.18");
    CHECK (levelVolumeText (0.0f) == "0%  -INF dB  0.00");
    CHECK (levelVolumeText (2.0f).rfind ("200%  ", 0) == 0);
}

TEST (flfix190ey_tempo_dialog_buttons)
{
    using namespace lynx::tempodlg;
    // PL-8: "117.5 (estimated)", the four ranges, Type in (BPM), Type in (beats), "100 (project)", No tempo
    const auto b = buttons (117.5, 100.0);
    CHECK (b.size() == 9);
    CHECK (b[0].choice == Choice::estimated && bpmText (b[0].bpm) == "117.5");
    CHECK (b[1].choice == Choice::range && b[1].low == 50.0 && b[1].high == 100.0);
    CHECK (b[4].choice == Choice::range && b[4].low == 150.0 && b[4].high == 300.0);
    CHECK (b[5].choice == Choice::typeBpm && b[6].choice == Choice::typeBeats);
    CHECK (b[7].choice == Choice::project && bpmText (b[7].bpm) == "100" && b[8].choice == Choice::noTempo);
    // nothing estimated: no estimate button
    CHECK (buttons (0.0, 120.0).size() == 8 && buttons (0.0, 120.0)[0].choice == Choice::range);
    CHECK (bpmText (128.0) == "128" && bpmText (99.95) == "99.95" && bpmText (117.504) == "117.5");
    // the session's estimate: 117.5 BPM over 51.063 s = 100 beats
    CHECK (std::abs (beatsAt (51.063, 117.5) - 100.0) < 0.01);
    CHECK (beatsAt (10.0, 0.0) == 0.0);
}

TEST (flfix190ey_fade_curve_submenu_is_fls)
{
    using namespace lynx;
    // PL-7 (FL 2026 shot PL-7-fade-curve-submenu.png), the same for Fade in curve and Fade out curve
    const std::vector<std::string> fl { "Reset", "Normalize", "Normalize selection as a group", "Create crossfade", "(v) Manual", "[Declicker]",
                                        "(v) Automatic", "(v) Disabled", "(v) Enabled", "[Curve shape]", "(v) Smooth curve", "(v) Single curve" };
    std::vector<std::string> texts;
    for (const auto& e : flclipmenu::fadeCurve())
        texts.push_back (e.kind == flclipmenu::Entry::Kind::header ? "[" + e.text + "]" : e.kind == flclipmenu::Entry::Kind::check ? "(v) " + e.text : e.text);
    CHECK (texts == fl);
    // the clip menu shows both submenus now
    int curves = 0;
    for (const auto& e : flclipmenu::shown (true, false))
        curves += e.text == "Fade in curve" || e.text == "Fade out curve" ? 1 : 0;
    CHECK (curves == 2);
}

TEST (flfix190ey_external_input_question_only_from_no_input)
{
    using namespace lynx;
    // ISS152-1: (none) -> In 1 - In 2 asks; In 1 - In 2 -> In 1 does not; (none) -> In 1 asks again
    CHECK (asksExternalInputOnly (18, -1, 0, RecordSource::postEq));
    CHECK (! asksExternalInputOnly (18, 0, 1, RecordSource::postEq));
    CHECK (asksExternalInputOnly (25, -1, 1, RecordSource::postEq));
    // External input only already, the Master, or no input picked: no question
    CHECK (! asksExternalInputOnly (18, -1, 0, RecordSource::externalInput));
    CHECK (! asksExternalInputOnly (0, -1, 0, RecordSource::postEq));
    CHECK (! asksExternalInputOnly (18, -1, -1, RecordSource::postEq));
}

TEST (flfix190ey_clip_preview_plays_through_the_channel_as_the_song_does)
{
    using namespace lynx;
    // PL-3: the Preview note is the song's own note for the clip (core audioClipNote), on the live queue: the channel plays
    // it, so it goes through the channel's insert (its fader) exactly as the song plays the clip
    const auto makeSong = [] (float insertVolume) {
        auto s = Song::makeDefault();
        s.channels.clear();
        s.playlist.clear();
        s.patterns.clear();
        s.patterns.emplace_back();
        ChannelDef c;
        c.name = "Vox";
        c.instrument = InstrumentKind::audioClip;
        c.samplePath = "tone";
        c.mixerTrack = 1;
        s.channels.push_back (c);
        s.mixer[1].volume = insertVolume;
        s.mode = PlayMode::song;
        Clip clip;
        clip.kind = ClipKind::audio;
        clip.channel = 0;
        clip.track = 0;
        clip.start = 0;
        clip.length = 2 * ppq;
        clip.offset = ppq / 4;
        clip.gain = 0.7f;
        s.playlist.push_back (clip);
        return s;
    };
    const auto provider = [] (const std::string&) {
        auto d = std::make_shared<SampleData>();
        d->sampleRate = 48000.0;
        d->left.resize (96000);
        for (std::size_t i = 0; i < d->left.size(); ++i)
            d->left[i] = 0.5f * (float) std::sin ((double) i * 0.05);
        return std::shared_ptr<const SampleData> (d);
    };
    const auto render = [&provider] (const Song& song, bool asPreview) {
        auto st = RackState::build (song, nullptr, 48000.0, provider);
        RackRenderer renderer;
        renderer.prepare (48000.0, 256);
        LiveEventQueue live;
        const auto tps = ticksPerSample (song.bpm, 48000.0);
        if (asPreview)
            live.push (audioClipNote (song, song.playlist[0], 0, tps));
        std::vector<float> out, l (256), r (256);
        double tick = 0.0;
        for (int block = 0; block < 40; ++block)
        {
            renderer.process (st.get(), ! asPreview, tick, 256, l.data(), r.data(), &live, nullptr);
            out.insert (out.end(), l.begin(), l.end());
            tick += 256 * tps;
        }
        return out;
    };
    const auto song = makeSong (0.8f);
    const auto e = audioClipNote (song, song.playlist[0], 0, ticksPerSample (song.bpm, 48000.0));
    CHECK (e.type == NoteEvent::Type::on && e.key == audioClipKey (0) && e.channel == 0 && e.velocity == 0.7f);
    CHECK (std::abs (e.startSeconds - 0.25 * 60.0 / song.bpm) < 1.0e-9);
    const auto preview = render (song, true);
    const auto played = render (song, false);
    float peak = 0.0f, diff = 0.0f;
    for (std::size_t i = 0; i < preview.size(); ++i)
    {
        peak = std::max (peak, std::abs (preview[i]));
        diff = std::max (diff, std::abs (preview[i] - played[i]));
    }
    CHECK (peak > 0.01f);
    CHECK (diff < 1.0e-4f);
    // through the insert: its fader down, the preview is silent
    const auto muted = render (makeSong (0.0f), true);
    float mutedPeak = 0.0f;
    for (const auto v : muted)
        mutedPeak = std::max (mutedPeak, std::abs (v));
    CHECK (mutedPeak == 0.0f);
}
