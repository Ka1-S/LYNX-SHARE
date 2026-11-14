// Round 190gh (0.1.28 community-complaint slices, /mnt/e/lynx-work/pain190gc/REPORT.md §4):
// B2 updater "Go back to <previous version>", B7 export parity, B8 MIDI device hot-plug, B23 first-start audio check.

#include "core/AudioCheck.h"
#include "core/ExportOptions.h"
#include "core/UpdateCheck.h"

namespace gh190
{
// --- B7: a small song rendered through the RackRenderer -------------------------------------------------------------
// three sample channels (decaying sines and a click train, made here: deterministic), inserts 1 and 2 sending to a bus
// (insert 3, a delay: linear) and insert 4 (an EQ) on the Master, insert 4's fader automated over the two bars; the
// Master without effects, so the parts of the mix add up
std::shared_ptr<const lynx::SampleData> sample (const std::string& path)
{
    auto d = std::make_shared<lynx::SampleData>();
    d->sampleRate = 48000.0;
    d->left.assign (24000, 0.0f);
    const double f = path == "a" ? 220.0 : path == "b" ? 331.0 : 0.0;
    for (std::size_t i = 0; i < d->left.size(); ++i)
    {
        const double t = (double) i / 48000.0;
        d->left[i] = f > 0.0 ? (float) (0.6 * std::sin (2.0 * 3.14159265358979 * f * t) * std::exp (-3.0 * t))
                             : (i % 2400 < 24 ? 0.5f : 0.0f);
    }
    return d;
}

lynx::Song song()
{
    using namespace lynx;
    Song s;
    s.mixer = Song::defaultMixer (4);
    s.mixer[0].effects.clear();
    for (int c = 0; c < 3; ++c)
    {
        ChannelDef ch;
        ch.instrument = InstrumentKind::sampler;
        ch.samplePath = c == 0 ? "a" : c == 1 ? "b" : "c";
        ch.name = ch.samplePath;
        ch.volume = 0.8f;
        ch.mixerTrack = c == 0 ? 1 : c == 1 ? 2 : 4;
        s.channels.push_back (ch);
    }
    for (int t : { 1, 2 })
    {
        s.mixer[(std::size_t) t].toMaster = false;
        s.mixer[(std::size_t) t].sends.push_back ({ 3, 1.0f });
    }
    s.mixer[3].effects.push_back (makeEffectSlot (EffectKind::delay));
    s.mixer[4].effects.push_back (makeEffectSlot (EffectKind::eq));
    auto& p = s.patterns.emplace_back();
    for (int k = 0; k < 8; ++k)
        for (int c = 0; c < 3; ++c)
        {
            Note n;
            n.channel = c;
            n.key = 60 + (k % 3) * 2;
            n.start = (std::int64_t) k * ppq / 2 + c * 37; // (off the grid: notes inside a block)
            n.length = ppq / 3;
            n.velocity = 0.7f + 0.03f * (float) k;
            p.notes.push_back (n);
        }
    p.sortNotes();
    Automation a;
    a.name = "Insert 4 volume";
    a.target = { AutomationTargetKind::mixerVolume, 4, 0, 0 };
    a.points = { { 0, 0.2f, 0.0f }, { 8 * ppq, 1.0f, 0.0f } };
    s.automations.push_back (a);
    Clip clip;
    clip.pattern = 0;
    clip.track = 0;
    clip.start = 0;
    clip.length = 8 * ppq;
    s.playlist.push_back (clip);
    Clip auto1;
    auto1.kind = ClipKind::automation;
    auto1.automation = 0;
    auto1.track = 1;
    auto1.length = 8 * ppq;
    s.playlist.push_back (auto1);
    s.mode = PlayMode::song;
    return s;
}

// the song played from the start in blocks of `size(k)` samples (as an audio device calls), `total` samples
std::pair<std::vector<float>, std::vector<float>> render (const lynx::Song& s, const std::function<int (int)>& size, int total)
{
    const double rate = 48000.0;
    lynx::RackRenderer r;
    r.prepare (rate, 2048);
    r.setTransportOptions (true, false, 0);
    auto st = lynx::RackState::build (s, nullptr, rate, sample);
    std::vector<float> l ((std::size_t) total), rr ((std::size_t) total);
    const auto tps = lynx::ticksPerSample (s.bpm, rate);
    double tick = 0.0;
    for (int done = 0, k = 0; done < total; ++k)
    {
        const auto n = std::min (std::max (1, size (k)), total - done);
        r.process (st.get(), true, tick, n, l.data() + done, rr.data() + done);
        tick += n * tps;
        done += n;
    }
    return { l, rr };
}

float maxDifference (const std::vector<float>& a, const std::vector<float>& b)
{
    float d = 0.0f;
    for (std::size_t i = 0; i < std::min (a.size(), b.size()); ++i)
        d = std::max (d, std::abs (a[i] - b[i]));
    return d;
}

float peakOf (const std::vector<float>& a)
{
    float p = 0.0f;
    for (const auto v : a)
        p = std::max (p, std::abs (v));
    return p;
}
} // namespace gh190

// ====================================================================================================================
// B2: updater rollback
// ====================================================================================================================
TEST (small190gh_rollback_picks_the_recorded_previous_version_else_the_newest_older_one)
{
    using namespace lynx::update;
    // the release list of the fixture in Update166bTests.inc: 0.1.23 (draft), 0.1.22, nightly (no version), 0.1.21, 0.1.20
    const auto releases = parseReleases (updateFixture);
    CHECK (releases.size() == 4);
    auto versionAt = [&] (int i) { return i >= 0 ? releases[(std::size_t) i].version.text() : std::string ("none"); };

    // running 0.1.22 after an update from 0.1.20 (recorded): that one, not the newer 0.1.21
    CHECK (versionAt (rollbackIndex (releases, parseVersion ("0.1.22-alpha"), "0.1.20-alpha")) == "0.1.20-alpha");
    // nothing recorded (an update made by a version without the field): the newest release older than the running one
    CHECK (versionAt (rollbackIndex (releases, parseVersion ("0.1.22-alpha"), "")) == "0.1.21-alpha");
    CHECK (versionAt (rollbackIndex (releases, parseVersion ("0.1.22-alpha"), "junk")) == "0.1.21-alpha");
    // a recorded version the list no longer has (or a newer one): the newest older one
    CHECK (versionAt (rollbackIndex (releases, parseVersion ("0.1.22-alpha"), "0.1.19-alpha")) == "0.1.21-alpha");
    CHECK (versionAt (rollbackIndex (releases, parseVersion ("0.1.21-alpha"), "0.1.22-alpha")) == "0.1.20-alpha");
    // never the draft, never the running one or a newer one
    CHECK (versionAt (rollbackIndex (releases, parseVersion ("0.1.24"), "0.1.23-alpha")) == "0.1.22-alpha");
    CHECK (rollbackIndex (releases, parseVersion ("0.1.20-alpha"), "") == -1); // the oldest: nothing to go back to
    CHECK (rollbackIndex (releases, Version {}, "0.1.20-alpha") == -1);        // the running version unknown
    CHECK (rollbackIndex ({}, parseVersion ("0.1.22-alpha"), "0.1.20-alpha") == -1);
}

TEST (small190gh_rollback_settings_round_trip_and_skip_the_left_version)
{
    using namespace lynx::update;
    Settings s;
    s.automatic = false;
    s.lastCheckMs = 1761900000000LL;
    s = afterUpdate (s, parseVersion ("0.1.27"));
    CHECK (s.previous == "0.1.27");
    const auto back = parseSettings (formatSettings (s));
    CHECK (back.previous == "0.1.27" && ! back.automatic && back.lastCheckMs == 1761900000000LL);
    CHECK (parseSettings ("previous=nonsense\n").previous.empty() && parseSettings ("").previous.empty());
    CHECK (formatSettings (Settings {}).find ("previous") == std::string::npos); // (nothing written when there is none)
    CHECK (afterUpdate (s, Version {}).previous == "0.1.27");                    // not a version: unchanged

    // going back from 0.1.28: the bar stops offering 0.1.28 automatically; Help › Check for updates still does
    auto after = afterRollback (s, parseVersion ("0.1.28"));
    CHECK (after.skipped == "0.1.28" && after.previous.empty() && ! after.automatic);
    Release r;
    r.version = parseVersion ("0.1.28");
    CHECK (! shouldShow (r, after, false) && shouldShow (r, after, true));
    r.version = parseVersion ("0.1.29");
    CHECK (shouldShow (r, after, false)); // the next release is offered again
    // an already skipped newer version stays the skipped one
    Settings later;
    later.skipped = "0.1.30";
    CHECK (afterRollback (later, parseVersion ("0.1.28")).skipped == "0.1.30");
}

// ====================================================================================================================
// B8: MIDI device hot-plug, simulated: a fake device list that changes between checks, and a fake "open devices" set
// that does what the engine does with the plan (as AudioEngine::enableAllMidiInputs)
// ====================================================================================================================
TEST (small190gh_midi_hotplug_simulation_opens_closes_and_reopens_by_name)
{
    using namespace lynx::prefs;
    MidiPrefs prefs;
    MidiInputPrefs off;
    off.name = "Pads";
    off.enabled = false; // turned off in Settings › MIDI
    prefs.setInput (off);

    std::vector<std::string> open, previous; // the engine's state between checks
    int messages = 0, lostMessages = 0;
    auto check = [&] (const std::vector<MidiPort>& available) {
        std::vector<std::string> openNow;
        for (const auto& id : open)
            openNow.push_back (id); // (the engine passes the available ones that are open and the vanished ones still open)
        const auto plan = planMidiInputs (prefs, available, previous, openNow);
        auto erase = [&open] (const std::string& id) { open.erase (std::remove (open.begin(), open.end(), id), open.end()); };
        for (const auto* list : { &plan.close, &plan.gone, &plan.reopen })
            for (const auto& id : *list)
                erase (id);
        for (const auto* list : { &plan.reopen, &plan.open })
            for (const auto& id : *list)
                open.push_back (id);
        previous.clear();
        for (const auto& p : available)
            previous.push_back (p.id);
        return plan;
    };
    // a message from a device: delivered only while its handle is open AND the device is there (a message for a
    // device that is gone would be a dead handle: counted as lost)
    auto deliver = [&] (const std::string& id, const std::vector<MidiPort>& available) {
        const bool isOpen = std::find (open.begin(), open.end(), id) != open.end();
        const bool there = std::any_of (available.begin(), available.end(), [&id] (const MidiPort& p) { return p.id == id; });
        if (isOpen && there)
            ++messages;
        else if (isOpen)
            ++lostMessages;
    };

    const std::vector<MidiPort> start { { "usb1#kbd", "Keystation 49" }, { "usb2#pads", "Pads" } };
    auto plan = check (start);
    CHECK (plan.open == std::vector<std::string> { "usb1#kbd" } && plan.gone.empty()); // Pads is off
    deliver ("usb1#kbd", start);

    // the keyboard is unplugged while a note plays: its handle is closed at the next check
    const std::vector<MidiPort> unplugged { { "usb2#pads", "Pads" } };
    deliver ("usb1#kbd", unplugged); // (between the unplug and the check: the handle is still open)
    plan = check (unplugged);
    CHECK (plan.gone == std::vector<std::string> { "usb1#kbd" } && plan.open.empty() && plan.close.empty());
    CHECK (open.empty());
    deliver ("usb1#kbd", unplugged); // after the check: nothing waits on the dead handle any more
    CHECK (lostMessages == 1);

    // plugged back into another USB port: another identifier, same name, opened again (the setting is by name)
    const std::vector<MidiPort> back { { "usb3#kbd", "Keystation 49" }, { "usb2#pads", "Pads" } };
    plan = check (back);
    CHECK (plan.open == std::vector<std::string> { "usb3#kbd" } && plan.gone.empty());
    deliver ("usb3#kbd", back);
    CHECK (messages == 2);

    // unplugged and back in the same port between two checks: reopened (the old handle may be dead)
    previous = { "usb2#pads" };
    plan = check (back);
    CHECK (plan.reopen == std::vector<std::string> { "usb3#kbd" });

    // the turned-off device never opens, plugged in or out
    for (const auto& list : { start, unplugged, back })
    {
        check (list);
        CHECK (std::find (open.begin(), open.end(), "usb2#pads") == open.end());
    }
    // a gone device is listed once
    CHECK (planMidiInputs (prefs, {}, {}, { "x", "x" }).gone == std::vector<std::string> { "x" });
}

TEST (small190gh_midi_device_list_changes_name_what_was_plugged_in_and_out)
{
    using namespace lynx::prefs;
    const std::vector<MidiPort> a { { "in:1", "Keystation 49" }, { "out:1", "Keystation 49" }, { "in:2", "Pads" } };
    CHECK (diffMidiDevices (a, a) == MidiDeviceChange {});
    auto c = diffMidiDevices (a, { { "in:1", "Keystation 49" }, { "out:1", "Keystation 49" } });
    CHECK (c.changed && c.added.empty() && c.removed == std::vector<std::string> { "Pads" });
    c = diffMidiDevices ({ { "in:1", "Keystation 49" } }, a);
    CHECK (c.changed && c.added == std::vector<std::string> { "Pads" } && c.removed.empty()); // (its output: same name, not new)
    // another port: changed (the ports are opened again) but nothing new for the hint
    c = diffMidiDevices (a, { { "in:7", "Keystation 49" }, { "out:7", "Keystation 49" }, { "in:2", "Pads" } });
    CHECK (c.changed && c.added.empty() && c.removed.empty());
    c = diffMidiDevices ({}, a);
    CHECK (c.added == (std::vector<std::string> { "Keystation 49", "Pads" }) && c.removed.empty());
}

// ====================================================================================================================
// B23: first-start audio check
// ====================================================================================================================
TEST (small190gh_audio_check_verdict_and_devices_to_try)
{
    using namespace lynx::audiocheck;
    CHECK (verdict (false, 0.5f, true) == Verdict::noDevice);
    CHECK (verdict (true, 0.3f, true) == Verdict::works);
    CHECK (verdict (true, 0.0f, true) == Verdict::works); // (the user's ear wins)
    CHECK (verdict (true, 0.3f, false) == Verdict::reachedNotHeard);
    CHECK (verdict (true, 0.001f, false) == Verdict::notReached);

    const std::vector<std::pair<std::string, std::vector<std::string>>> windows {
        { "Windows Audio", { "Speakers (Realtek)", "Headphones (USB)", "HDMI (NVIDIA)", "Monitor" } },
        { "Windows Audio (Exclusive Mode)", { "Speakers (Realtek)" } },
        { "DirectSound", { "Primary Sound Driver", "Speakers (Realtek)" } },
        { "ASIO", { "FL Studio ASIO", "ASIO4ALL v2" } },
        { "Windows Audio (Low Latency Mode)", {} }
    };
    // on the ASIO driver nothing is heard: the other ASIO driver, then Windows Audio's default, DirectSound...
    auto a = alternatives ("windows", { "ASIO", "FL Studio ASIO" }, windows);
    CHECK (a.size() == 4);
    CHECK (a[0] == (Choice { "ASIO", "ASIO4ALL v2" }));
    CHECK (a[1] == (Choice { "Windows Audio", "Speakers (Realtek)" }));
    CHECK (a[2] == (Choice { "DirectSound", "Primary Sound Driver" }));
    CHECK (a[3] == (Choice { "Windows Audio (Exclusive Mode)", "Speakers (Realtek)" }));
    // on the speakers: the headphones and the HDMI output first (two of the same type), then other drivers
    a = alternatives ("windows", { "Windows Audio", "Speakers (Realtek)" }, windows, 3);
    CHECK (a.size() == 3 && a[0] == (Choice { "Windows Audio", "Headphones (USB)" }) && a[1] == (Choice { "Windows Audio", "HDMI (NVIDIA)" })
           && a[2] == (Choice { "DirectSound", "Primary Sound Driver" }));
    // Linux: PipeWire before ALSA before JACK; the current device never offered
    const std::vector<std::pair<std::string, std::vector<std::string>>> linuxDevices {
        { "JACK", { "system" } }, { "ALSA", { "default", "hw:1" } }, { "PipeWire", { "System default" } }
    };
    a = alternatives ("linux", { "ALSA", "default" }, linuxDevices);
    CHECK (a.size() == 3 && a[0] == (Choice { "ALSA", "hw:1" }) && a[1] == (Choice { "PipeWire", "System default" }) && a[2] == (Choice { "JACK", "system" }));
    // nothing else to try
    CHECK (alternatives ("macos", { "CoreAudio", "Built-in Output" }, { { "CoreAudio", { "Built-in Output" } } }).empty());
    // no device open: each driver's default device
    a = alternatives ("linux", {}, linuxDevices);
    CHECK (a.size() == 3 && a[0] == (Choice { "PipeWire", "System default" }) && a[1] == (Choice { "ALSA", "default" }) && a[2] == (Choice { "JACK", "system" }));
}

// ====================================================================================================================
// B7: export parity
// ====================================================================================================================
TEST (small190gh_render_is_the_same_in_any_block_size)
{
    // what an export renders (blocks of 512) against what playback renders (the device's blocks: 441, 480, 64, uneven
    // shared-mode periods): the same samples, below −90 dBFS (render ≠ playback, complaint #9)
    const auto s = gh190::song();
    constexpr int total = 4 * 48000; // the 2 bars at 120 BPM and the delay's tail
    const auto [l0, r0] = gh190::render (s, [] (int) { return 512; }, total);
    CHECK (gh190::peakOf (l0) > 0.05f);
    for (const auto& [name, size] : std::vector<std::pair<const char*, std::function<int (int)>>> {
             { "441", [] (int) { return 441; } },
             { "480", [] (int) { return 480; } },
             { "64", [] (int) { return 64; } },
             { "2048", [] (int) { return 2048; } },
             { "uneven", [] (int k) { return 1 + (k * 7919) % 1100; } } })
    {
        const auto [l, r] = gh190::render (s, size, total);
        const auto diff = std::max (gh190::maxDifference (l0, l), gh190::maxDifference (r0, r));
        if (diff >= 3.16e-5f)
            std::printf ("  blocks %s: differs by %.3g (%.1f dBFS)\n", name, (double) diff, 20.0 * std::log10 (std::max (1.0e-12, (double) diff)));
        CHECK (diff < 3.16e-5f);
    }
}

TEST (small190gh_channel_stems_and_top_level_insert_stems_sum_to_the_mix)
{
    using namespace lynx;
    const auto s = gh190::song();
    constexpr int total = 4 * 48000;
    auto blocks = [] (int) { return 512; };
    const auto [ml, mr] = gh190::render (s, blocks, total);

    // "One per channel": each channel through its whole mixer path
    std::vector<float> sl ((std::size_t) total), sr ((std::size_t) total);
    for (auto c : exportopts::stemChannels (s, exportopts::Range::song))
    {
        const auto [l, r] = gh190::render (exportopts::makeChannelStemSong (s, c), blocks, total);
        for (int i = 0; i < total; ++i)
            sl[(std::size_t) i] += l[(std::size_t) i], sr[(std::size_t) i] += r[(std::size_t) i];
    }
    CHECK (exportopts::stemChannels (s, exportopts::Range::song).size() == 3);
    CHECK (std::max (gh190::maxDifference (ml, sl), gh190::maxDifference (mr, sr)) < 1.0e-4f);

    // "One per mixer insert": the bus (3) holds inserts 1 and 2 and its delay; the inserts on the Master (3, 4) sum to it
    const auto tracks = exportopts::stemTracks (s);
    CHECK ((tracks == std::vector<int> { 1, 2, 3, 4 }));
    std::fill (sl.begin(), sl.end(), 0.0f);
    std::fill (sr.begin(), sr.end(), 0.0f);
    for (auto t : tracks)
    {
        if (! s.mixer[(std::size_t) t].toMaster)
            continue;
        const auto [l, r] = gh190::render (makeBounceSong (s, t), blocks, total);
        for (int i = 0; i < total; ++i)
            sl[(std::size_t) i] += l[(std::size_t) i], sr[(std::size_t) i] += r[(std::size_t) i];
    }
    CHECK (std::max (gh190::maxDifference (ml, sl), gh190::maxDifference (mr, sr)) < 1.0e-4f);
    // the bus stem has its delay's echoes (group FX in "one per mixer insert")
    const auto [bl, br] = gh190::render (makeBounceSong (s, 3), blocks, total);
    const auto [dl, dr] = gh190::render ([&s] { auto dry = makeBounceSong (s, 3); dry.mixer[3].effects.clear(); return dry; }(), blocks, total);
    CHECK (gh190::maxDifference (bl, dl) > 1.0e-3f);
}

TEST (small190gh_inserts_fed_only_by_extra_outputs_get_a_stem)
{
    using namespace lynx;
    auto s = Song::makeDefault();
    s.mixer = Song::defaultMixer (12);
    for (auto& c : s.channels)
        c.mixerTrack = 1;
    CHECK ((exportopts::stemTracks (s) == std::vector<int> { 1 }));

    // a hosted multi-output instrument on insert 5: output 2 to insert 6 (+1), output 3 inactive (+2 off)
    ChannelDef multi;
    multi.instrument = InstrumentKind::plugin;
    multi.plugin.name = "Multi out drums";
    multi.mixerTrack = 5;
    multi.plugin.outputs = { {}, { 1, 1 }, { 2, 0 } };
    multi.plugin.processInactive = false;
    s.channels.push_back (multi);
    // a Fruity Send on insert 1 to insert 8
    auto send = makeEffectSlot (EffectKind::fruitySend);
    send.params[3] = 8.0f / (float) (maxMixerTracks - 1);
    s.mixer[1].effects.push_back (send);
    CHECK (fruitySendTarget (s.mixer[1].effects[0]) == 8);
    // a generator Patcher's extra output to insert 10
    ChannelDef patch;
    patch.instrument = InstrumentKind::patcher;
    patch.mixerTrack = 9;
    patch.patcher.outputTracks = { 10 };
    s.channels.push_back (patch);

    CHECK ((exportopts::stemTracks (s) == std::vector<int> { 1, 5, 6, 8, 9, 10 }));
    // "Process inactive inputs and outputs" on: the inactive output plays on insert 7 too
    s.channels[s.channels.size() - 2].plugin.processInactive = true;
    CHECK ((exportopts::stemTracks (s) == std::vector<int> { 1, 5, 6, 7, 8, 9, 10 }));
}
