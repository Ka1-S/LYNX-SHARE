// Round 181b: FL's Audio logger (a rolling buffer in memory, dump) and Chord detection panel (REA_rodada181b.md)

#include "core/ChordReadout.h"
#include "core/RollingAudioLog.h"

namespace
{
std::vector<float> rollingRamp (int n, float start)
{
    std::vector<float> v ((std::size_t) n);
    for (int i = 0; i < n; ++i)
        v[(std::size_t) i] = start + (float) i * 0.001f;
    return v;
}
using RollPos = lynx::RollingAudioLog::Position;
}

TEST (logger181b_memory_per_length)
{
    lynx::RollingAudioLog log;
    CHECK (log.bytesInUse() == 0); // off: nothing allocated
    log.setSampleRate (48000.0);
    CHECK (log.bytesInUse() == 0);
    log.setEnabled (true);
    CHECK (log.bytesInUse() == 60u * 48000u * 8u); // 22 MiB
    log.setLengthSeconds (120);
    CHECK (log.bytesInUse() == 120u * 48000u * 8u);
    log.setLengthSeconds (300);
    CHECK (log.bytesInUse() == 300u * 48000u * 8u);
    log.setLengthSeconds (600);
    CHECK (log.bytesInUse() == 600u * 48000u * 8u); // 220 MiB
    log.setSampleRate (44100.0);
    CHECK (log.bytesInUse() == 600u * 44100u * 8u);
    log.setEnabled (false);
    CHECK (log.bytesInUse() == 0); // freed when off
}

TEST (logger181b_off_writes_nothing_and_enable_waits_for_the_rate)
{
    lynx::RollingAudioLog log;
    log.setEnabled (true); // on, but no device yet
    CHECK (log.bytesInUse() == 0);
    const auto block = rollingRamp (64, 0.1f);
    log.push (RollPos::postEffects, block.data(), block.data(), 64); // must not crash
    lynx::RollingAudioLog::Dump dump;
    CHECK (! log.dump (dump));
    log.setSampleRate (1000.0);
    CHECK (log.framesCapacity() == 60000u);
    log.setEnabled (false);
    log.push (RollPos::postEffects, block.data(), block.data(), 64);
    CHECK (! log.dump (dump));
}

TEST (logger181b_position_takes_only_its_own_tap)
{
    lynx::RollingAudioLog log;
    log.setSampleRate (1000.0);
    log.setEnabled (true);
    CHECK (log.getPosition() == RollPos::postEffects); // FL's default
    const std::vector<float> pre (10, 0.25f), post (10, 0.5f);
    log.push (RollPos::preEffects, pre.data(), pre.data(), 10);
    log.push (RollPos::postEffects, post.data(), post.data(), 10);
    lynx::RollingAudioLog::Dump d;
    CHECK (log.dump (d) && d.left.size() == 10 && d.left[0] == 0.5f);
    log.setPosition (RollPos::preEffects);
    log.push (RollPos::postEffects, post.data(), post.data(), 10);
    log.push (RollPos::preEffects, pre.data(), pre.data(), 10);
    CHECK (log.dump (d) && d.left.size() == 20 && d.left[19] == 0.25f && d.left[9] == 0.5f);
}

TEST (logger181b_wraps_keeping_the_newest_in_order)
{
    lynx::RollingAudioLog log;
    log.setSampleRate (100.0);
    log.setEnabled (true); // 6000 frames
    log.setLengthSeconds (1); // 100 frames
    CHECK (log.framesCapacity() == 100u);
    float value = 1.0f;
    for (int block = 0; block < 7; ++block) // 280 frames: 180 fall out
    {
        std::vector<float> l (40), r (40);
        for (int i = 0; i < 40; ++i)
        {
            l[(std::size_t) i] = value;
            r[(std::size_t) i] = -value;
            value += 1.0f;
        }
        log.push (RollPos::postEffects, l.data(), r.data(), 40);
    }
    lynx::RollingAudioLog::Dump d;
    CHECK (log.dump (d));
    CHECK (d.left.size() == 100u && d.rate == 100.0);
    CHECK (d.left.front() == 181.0f && d.left.back() == 280.0f && d.right.back() == -280.0f);
    // the log isn't cleared by a dump and keeps logging
    const float one[] = { 500.0f };
    log.push (RollPos::postEffects, one, one, 1);
    CHECK (log.dump (d) && d.left.front() == 182.0f && d.left.back() == 500.0f);
    // a block longer than the buffer keeps its end
    const auto huge = rollingRamp (250, 1.0f);
    log.push (RollPos::postEffects, huge.data(), huge.data(), 250);
    CHECK (log.dump (d) && d.left.size() == 100u && d.left.back() == huge.back() && d.left.front() == huge[150]);
}

TEST (logger181b_silence_stops_storing_after_8_seconds)
{
    lynx::RollingAudioLog log;
    log.setSampleRate (100.0);
    log.setEnabled (true); // 6000 frames, 800 of silence stay
    const std::vector<float> tone (100, 0.5f), zero (100, 0.0f), faint (100, 0.9e-6f);
    log.push (RollPos::postEffects, tone.data(), tone.data(), 100);
    for (int s = 0; s < 40; ++s) // 40 s of silence: 8 s stored
        log.push (RollPos::postEffects, s % 2 == 0 ? zero.data() : faint.data(), zero.data(), 100);
    log.push (RollPos::postEffects, tone.data(), tone.data(), 100);
    lynx::RollingAudioLog::Dump d;
    CHECK (log.dump (d));
    CHECK (d.left.size() == 100u + 800u + 100u);
    CHECK (d.left[99] == 0.5f && d.left[100] == 0.0f && d.left[900] == 0.5f);
    // only silence: nothing above the level, "the audio logger is empty"
    lynx::RollingAudioLog quiet;
    quiet.setSampleRate (100.0);
    quiet.setEnabled (true);
    quiet.push (RollPos::postEffects, faint.data(), faint.data(), 100);
    CHECK (! quiet.dump (d));
    CHECK (quiet.isEnabled()); // and it keeps working after that (FL's bug 21682)
    quiet.push (RollPos::postEffects, tone.data(), tone.data(), 100);
    CHECK (quiet.dump (d));
}

TEST (logger181b_resize_keeps_the_newest_audio)
{
    lynx::RollingAudioLog log;
    log.setSampleRate (100.0);
    log.setEnabled (true);
    log.setLengthSeconds (2);
    const auto a = rollingRamp (150, 1.0f);
    log.push (RollPos::postEffects, a.data(), a.data(), 150);
    log.setLengthSeconds (1); // shorter: the last 100 frames
    lynx::RollingAudioLog::Dump d;
    CHECK (log.dump (d) && d.left.size() == 100u && d.left.front() == a[50] && d.left.back() == a[149]);
    log.setLengthSeconds (3); // longer: the same audio, with room after it
    CHECK (log.dump (d) && d.left.size() == 100u && d.left.front() == a[50] && d.left.back() == a[149]);
    log.push (RollPos::postEffects, a.data(), a.data(), 10);
    CHECK (log.dump (d) && d.left.size() == 110u && d.left[100] == a[0]);
    // another rate starts empty
    log.setSampleRate (200.0);
    CHECK (! log.dump (d));
}

// Every frame carries its number, so a lost, repeated or torn frame shows in a dump.
TEST (logger181b_no_audio_is_lost_while_dumps_and_new_lengths_have_the_ring)
{
    lynx::RollingAudioLog log;
    log.setSampleRate (48000.0);
    log.setEnabled (true);
    std::atomic<bool> stop { false };
    std::atomic<int> blocks { 0 };
    std::thread audio ([&] {
        float next = 1.0f;
        std::vector<float> l (64), r (64);
        while (! stop.load())
        {
            for (int i = 0; i < 64; ++i)
            {
                l[(std::size_t) i] = next;
                r[(std::size_t) i] = -next;
                next += 1.0f;
            }
            log.push (RollPos::postEffects, l.data(), r.data(), 64);
            blocks.fetch_add (1);
            std::this_thread::sleep_for (std::chrono::microseconds (20)); // (faster than real time, not free-running)
        }
    });

    auto intact = true;
    lynx::RollingAudioLog::Dump d;
    for (int i = 0; i < 60; ++i)
    {
        const auto before = blocks.load();
        while (blocks.load() < before + 2)
            std::this_thread::yield();
        log.setLengthSeconds (i % 3 + 1);
        if (log.dump (d))
            for (std::size_t f = 0; f < d.left.size(); ++f)
                intact = intact && d.right[f] == -d.left[f] && (f == 0 || d.left[f] == d.left[f - 1] + 1.0f);
    }
    stop.store (true);
    audio.join();
    CHECK (log.lostFrames() > 0 || intact); // (a thread stalled for over a second is the one case that may leave a gap)
    CHECK (log.isEnabled());
}

TEST (logger181b_audio_comes_back_after_switching_off_and_on)
{
    lynx::RollingAudioLog log;
    log.setSampleRate (1000.0);
    log.setEnabled (true);
    std::atomic<bool> stop { false };
    std::atomic<int> blocks { 0 };
    std::thread audio ([&] {
        const auto block = rollingRamp (64, 0.1f);
        while (! stop.load())
        {
            log.push (RollPos::postEffects, block.data(), block.data(), 64);
            blocks.fetch_add (1);
        }
    });
    lynx::RollingAudioLog::Dump d;
    for (int i = 0; i < 100; ++i)
    {
        log.setEnabled (false);
        log.setEnabled (true);
        const auto before = blocks.load();
        while (blocks.load() < before + 3) // the third block has certainly met the new ring
            std::this_thread::yield();
        CHECK (log.dump (d));
    }
    stop.store (true);
    audio.join();
}

TEST (chord181b_names)
{
    auto name = [] (std::vector<int> keys, bool sharps = true) { lynx::chordread::NameOptions o; o.sharps = sharps; return lynx::chordread::nameKeys (keys, o).text; };
    CHECK (name ({ 60 }).empty());
    CHECK (name ({ 60, 64 }) == "M 3rd");
    CHECK (name ({ 60, 67 }) == "5th");
    CHECK (name ({ 60, 72 }) == "Octave");
    CHECK (name ({ 60, 84 }) == "Octave +");
    CHECK (name ({ 64, 60 }) == "M 3rd");
    CHECK (name ({ 60, 64, 67 }) == "C");
    CHECK (name ({ 69, 72, 76 }) == "Am");
    CHECK (name ({ 69, 72, 76, 79 }) == "Am 7");
    CHECK (name ({ 67, 71, 74, 77 }) == "G 7");
    CHECK (name ({ 60, 64, 67, 71 }) == "C \xE2\x88\x86");
    CHECK (name ({ 61, 65, 68 }) == "C\xE2\x99\xAF");
    CHECK (name ({ 61, 65, 68 }, false) == "D\xE2\x99\xAD");
    CHECK (name ({ 48, 64, 67, 72 }) == "C");    // the bass is the root
    CHECK (name ({ 40, 60, 67, 72 }) == "C/E");  // a bass far below
    CHECK (name ({ 60, 65, 67 }) == "Csus4");
    CHECK (name ({ 60, 62, 67 }) == "Gsus4"); // (the same notes as Csus2: sus4 is the likelier family)
}

TEST (chord181b_novelty_past_four_tensions)
{
    using namespace lynx::chordread;
    const auto n = nameKeys ({ 60, 61, 62, 63, 64, 65, 66, 67, 68 });
    if (n.novelty)
        CHECK (std::find_if (noveltyNames().begin(), noveltyNames().end(), [&n] (const char* s) { return n.text == s; }) != noveltyNames().end());
    for (const auto* s : noveltyNames()) // (LYNX's own wording)
        CHECK (std::string (s) != "Jaazzz" && std::string (s) != "Mayhem");
}

TEST (chord181b_roman_needs_a_key)
{
    using namespace lynx::chordread;
    NameOptions o;
    o.roman = true;
    CHECK (nameKeys ({ 67, 71, 74 }, o).text == "G"); // no key: letter names
    o.tonic = 0;
    CHECK (nameKeys ({ 67, 71, 74 }, o).text == "V");
    CHECK (nameKeys ({ 69, 72, 76 }, o).text == "vi");
    CHECK (nameKeys ({ 63, 67, 70 }, o).text == "\xE2\x99\xADIII");
    o.tonic = 7;
    CHECK (nameKeys ({ 67, 71, 74 }, o).text == "I");
}

TEST (chord181b_timing)
{
    using namespace lynx::chordread;
    Readout r;
    CHECK (r.text() == noChord);
    r.note (0, 60, true, 0);
    CHECK (r.text() == noChord); // one key
    r.note (0, 64, true, 0);
    CHECK (r.text() == "M 3rd"); // note-on: at once
    r.note (0, 67, true, 10);
    CHECK (r.text() == "C");
    r.note (0, 67, false, 1000); // note-off: 100 ms later with no delay
    CHECK (r.text() == "C" && ! r.tick (1099) && r.text() == "C");
    CHECK (r.tick (1100) && r.text() == "M 3rd");

    // the keys of another channel don't add up
    r.note (1, 67, true, 2000);
    CHECK (r.text() == noChord);
    r.note (1, 67, false, 2000);
    r.tick (2200);

    r.note (0, 64, false, 3000);
    r.note (0, 60, false, 3000);
    r.tick (3100);
    CHECK (r.text() == noChord);

    // Display delay: a released chord stays
    r.setDelayMs (3000);
    r.note (0, 60, true, 4000);
    r.note (0, 64, true, 4000);
    r.note (0, 67, true, 4000);
    r.note (0, 60, false, 5000);
    r.note (0, 64, false, 5000);
    r.note (0, 67, false, 5000);
    CHECK (! r.tick (7999) && r.text() == "C");
    CHECK (r.tick (8000) && r.text() == noChord);
    // Show forever: 10000 s
    r.setDelayMs (foreverSeconds * 1000u);
    r.note (0, 60, true, 0);
    r.note (0, 64, true, 0);
    r.note (0, 67, true, 0);
    r.note (0, 67, false, 1);
    CHECK (! r.tick (9999000) && r.text() == "C");
    r.note (0, 60, false, 1);
    r.note (0, 64, false, 1);

    // more than ten keys: no name
    Readout many;
    for (int k = 0; k < 11; ++k)
        many.note (0, 40 + k * 2, true, 0);
    CHECK (many.text() == noChord);
}

TEST (chord181b_selection_shows_at_least_2_seconds)
{
    using namespace lynx::chordread;
    Readout r;
    r.selection ({ 60, 64, 67, 60 }, 1000); // (the same key twice counts once)
    CHECK (r.text() == "C");
    CHECK (! r.tick (2999) && r.text() == "C");
    CHECK (r.tick (3000) && r.text() == noChord);
    r.selection ({ 60 }, 4000); // one key: empty, then ---
    CHECK (r.text().empty());
    r.tick (6000);
    CHECK (r.text() == noChord);
}

TEST (toolbar181b_chord_panel_is_on_the_default_toolbar)
{
    using namespace lynx::toolbarlayout;
    auto d = Layout::makeDefault();
    CHECK (d.isShown (Group::chord) && d.isDefault());
    CHECK (d.orderToString().find ("chord,meters") != std::string::npos); // on the left of the meters, as in FL

    Layout old;
    old.orderFromString ("transport,master,record,snap,windows,file,meters,output"); // from an older version
    CHECK (old.isShown (Group::chord) && old.isDefault());

    // a customised toolbar keeps its order and what it hides, and doesn't gain the panel (it is in the edit mode)
    Layout custom;
    custom.orderFromString ("transport,-master,snap,record,file,windows,output,meters");
    CHECK (custom.orderToString() == "-link,transport,-master,snap,record,file,-key,-chord,windows,-multilink,output,meters"); // (177b-4: Multilink, hidden)
    CHECK (! custom.isShown (Group::chord));
    custom.setShown (Group::chord, true); // and it stays once shown
    Layout again;
    again.orderFromString (custom.orderToString());
    CHECK (again.isShown (Group::chord) && again.orderToString() == "-link,transport,-master,snap,record,file,-key,chord,windows,-multilink,output,meters");
    // the default order with one panel hidden is customised too
    Layout hidden;
    hidden.orderFromString ("transport,master,record,snap,windows,file,-meters,output");
    CHECK (! hidden.isShown (Group::chord) && ! hidden.isShown (Group::meters) && hidden.isShown (Group::output));
    // a saved preset still reads as that preset
    Layout compact;
    compact.orderFromString (applyPreset (Layout::makeDefault(), Preset::compact).orderToString());
    CHECK (matchingPreset (compact) == (int) Preset::compact);

    d.setShown (Group::chord, false); // and hiding it stays
    CHECK (! d.isDefault());
    Layout back;
    back.orderFromString (d.orderToString());
    CHECK (! back.isShown (Group::chord));
}

// Round 181b: names measured from FL 2026's own naming function (FLEngine 0xc43c20) on random key sets; the complete
// comparison (11552 sets with sharps, 300 with flats, 8000 Roman: 0 differences) is in docs/analise/REA_rodada181b.md.
TEST (chord181b_names_match_the_measured_fl_sample)
{
    using namespace lynx::chordread;
    struct Row { std::vector<int> keys; const char* name; };
    auto check = [] (const std::vector<Row>& rows, const NameOptions& options) {
        for (const auto& r : rows)
            CHECK (nameKeys (r.keys, options).text == r.name);
    };
    check ({
        { { 36, 45, 47, 53, 56, 59, 71, 86 }, "Bø 7,13/C" },
        { { 41, 48, 51, 53, 68, 69, 79, 83 }, "F 9♯9♯11" },
        { { 36, 61, 62, 66 }, "D ∆/C" },
        { { 37, 41, 72, 73, 79, 83 }, "C♯ ∆7♯11♯13" },
        { { 47, 64, 65, 69 }, "Bø 7,11" },
        { { 54, 57, 59, 69, 74, 79, 85, 92, 95 }, "Bm 13♭13" },
        { { 47, 65, 70, 71 }, "Bdim ∆" },
        { { 51, 58, 62 }, "D♯ ∆" },
        { { 36, 60, 62, 67 }, "Csus2" },
        { { 36, 61, 64, 70 }, "A♯dim/C" },
        { { 42, 58, 63, 76, 85, 89 }, "F♯ ∆7,13♯13" },
        { { 38, 51, 55, 64, 65, 74, 76, 81, 94 }, "Dm add♭6,9♭9" },
        { { 38, 40, 44, 47, 49, 53, 57, 76, 80 }, "E ♭9,11,13" },
        { { 62, 68, 71 }, "G♯dim" },
        { { 35, 66, 69, 70 }, "B ∆7♯13" },
        { { 48, 67, 81, 82, 83, 92 }, "C ∆13♭13♯13" },
        { { 50, 65, 69 }, "Dm" },
        { { 37, 44, 58, 62, 64, 66, 73, 88 }, "C♯m add4,6♭9" },
        { { 39, 45, 46, 48, 71, 82 }, "D♯m add♯4,6♭6" },
        { { 48, 50, 79, 81, 85 }, "D ∆7♯13sus4" },
        { { 42, 46, 48, 59, 62, 68, 74, 90 }, "F♯aug add4♯4,9" },
        { { 49, 52, 58 }, "A♯dim" },
        { { 37, 63, 70, 72, 73 }, "A♯m add4,9/C♯" },
        { { 37, 40, 48, 49, 91 }, "C♯dim ∆" },
        { { 30, 33, 38, 43 }, "D add4" },
        { { 41, 46, 54, 61, 79, 84, 95 }, "F♯ ∆♭9,11♯11" },
        { { 36, 40, 41, 43, 46, 47, 48, 53, 62, 92 }, "Fm ∆11♯11,13" },
        { { 45, 46, 51, 52, 58, 64, 66, 75, 76, 77 }, "Edim ∆9♭9,11" },
        { { 39, 47, 57, 66, 69, 81 }, "D♯dim add♭6" },
        { { 46, 47, 54, 62, 63, 70, 80, 83, 85 }, "G♯m 11♯11" },
        { { 37, 44, 48, 67, 68, 71, 76, 80, 81 }, "C♯m ∆♯11♭13♯13" },
        { { 62, 69, 71 }, "Bm 7" },
        { { 52, 64, 69, 71 }, "Esus4" },
        { { 39, 51, 59, 71, 80, 88 }, "G♯m add♭6/D♯" },
        { { 37, 42, 72, 76, 90, 92 }, "F♯ 9♯11" },
        { { 45, 48, 52, 62, 68, 70, 72, 74, 79, 84 }, "C 13♭13" },
        { { 39, 47, 51, 59, 74, 76, 80, 82, 86, 94 }, "G♯m add♯4♭6,9/D♯" },
        { { 40, 42, 56, 58, 60, 63, 72, 90 }, "G♯ 9♭13" },
        { { 52, 63, 65, 71 }, "E ∆7♭9" },
        { { 46, 62, 63, 65, 91 }, "A♯ add4,6" },
    }, {});
    NameOptions flats;
    flats.sharps = false;
    check ({
        { { 39, 45, 50, 67, 78, 82, 91 }, "Gm ∆9♭13" },
        { { 37, 47, 51, 55, 58, 61, 81, 83 }, "B ∆9♭13♯13/D♭" },
        { { 45, 54, 57, 63, 87, 89 }, "E♭dim add9/A" },
        { { 41, 43, 47, 64, 84, 95 }, "Em add♭6♭9" },
        { { 47, 53, 56, 60 }, "Fm add♯4" },
        { { 41, 62, 67, 71 }, "G/F" },
        { { 36, 38, 48, 82, 87, 89 }, "Cm 11" },
        { { 46, 60, 65, 79, 80, 85, 86 }, "B♭ 9♯13" },
        { { 40, 41, 49, 60, 69, 84, 86 }, "F ∆7,13♭13" },
        { { 60, 61, 68, 69 }, "D♭ ∆7♭13" },
        { { 45, 48, 55, 56, 63, 83, 85, 88, 93, 95 }, "A ∆♯9♯11♯13" },
        { { 61, 62, 68, 71 }, "A♭dim add4" },
        { { 43, 59, 71, 73, 75, 80, 92, 94 }, "A♭m add4,9/G" },
        { { 24, 45, 53, 79 }, "F add9/C" },
        { { 37, 68, 69, 70, 71 }, "D♭m 7,13♭13" },
    }, flats);
    NameOptions inC;
    inC.roman = true;
    inC.tonic = 0;
    check ({
        { { 42, 49, 58, 67, 89, 90, 91, 93 }, "♭V ∆7♭9♯9" },
        { { 52, 66, 67, 70 }, "iiiᵒ add9" },
        { { 36, 58, 61, 79, 81 }, "vᵒ add9/C" },
        { { 41, 47, 52, 61, 77, 78, 95 }, "viiᵒ add4,5,9" },
        { { 51, 58, 59, 69, 71, 72 }, "VII ∆7♭9♯13/D♯" },
        { { 38, 44, 52, 57, 77, 78, 87 }, "II add♯4♭9♯9" },
        { { 24, 26, 50, 54 }, "II 7" },
        { { 45, 74, 75, 77, 92 }, "ii add♯4♭9/A" },
        { { 44, 58, 64, 70, 93 }, "♭viiø ∆/G♯" },
        { { 52, 60, 69, 71 }, "vi add9/E" },
        { { 55, 61, 63, 64 }, "♭iiᵒ add9" },
        { { 33, 45, 78, 82 }, "♭v add♭4/A" },
        { { 48, 52, 57 }, "vi" },
        { { 53, 55, 63 }, "♭III add9" },
        { { 45, 48, 49, 52, 70, 77, 82 }, "♭vii ∆9♯11" },
    }, inC);
    NameOptions inG = inC;
    inG.tonic = 7;
    check ({
        { { 36, 43, 49, 71, 74, 76, 94 }, "IV ∆9♭9♯13" },
        { { 52, 64, 71, 76, 90, 94, 95 }, "VI add♯4,9" },
        { { 40, 47, 54, 66, 79, 85 }, "vi add6,9" },
        { { 48, 57, 66 }, "viiᵒ/C" },
        { { 37, 39, 50, 56, 87, 94 }, "♭VI ∆7♯13sus4" },
        { { 37, 42, 47, 54, 69, 93 }, "vii add4" },
        { { 37, 60, 73, 80, 82, 89 }, "♭V ∆7,13" },
        { { 46, 51, 54, 69, 70, 78, 88 }, "♭vi add♯4♭9" },
        { { 37, 51, 73, 74, 84, 85, 88, 89, 95 }, "♭V ∆♭9♯9♯13" },
        { { 39, 41, 55, 59, 64, 67, 80, 81, 85, 88 }, "II ∆♯11♭13♯13" },
    }, inG);
}

TEST (macros181b_declick_changes_only_selected_audio_clips)
{
    lynx::Song song;
    song.channels.resize (3);
    song.channels[0].instrument = lynx::InstrumentKind::audioClip;
    song.channels[1].instrument = lynx::InstrumentKind::sampler;
    song.channels[2].instrument = lynx::InstrumentKind::audioClip;
    CHECK (lynx::audioClipsAmong (song, { 1 }) == 0);
    CHECK (lynx::audioClipsAmong (song, { 0, 1, 7, -1 }) == 1);
    CHECK (lynx::setDeclickOfAudioClips (song, { 0, 1 }, 4) == 1);
    CHECK (song.channels[0].sampler.declickMode == 4);
    CHECK (song.channels[1].sampler.declickMode == 0);
    CHECK (song.channels[2].sampler.declickMode == 0);
}

TEST (macros181b_prepare_for_midi_export_follows_fl)
{
    lynx::Song song;
    song.channels.resize (20);
    song.channels[2].plugin.name = "FPC";
    song.channels[3].name = "Lead";
    lynx::prepareForMidiExport (song);
    std::vector<int> channels;
    for (const auto& c : song.channels)
    {
        CHECK (c.instrument == lynx::InstrumentKind::midiOut);
        CHECK (c.plugin.name.empty());
        channels.push_back (lynx::midictl::parseMidiOut (c.pluginState).channel);
    }
    // 0 1 FPC 2 3 ... 8, skipping 9 (MIDI channel 10), 10 ... 15, then 0 again
    const std::vector<int> expected { 0, 1, 9, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3 };
    CHECK (channels == expected);
    CHECK (song.channels[3].name == "Lead");

    lynx::Song kept;
    kept.channels.resize (3);
    kept.channels[1].instrument = lynx::InstrumentKind::layer;
    lynx::prepareForMidiExport (kept);
    // as FL 2026 does (seen at runtime): a Layer becomes a MIDI Out too, and takes its turn
    CHECK (kept.channels[1].instrument == lynx::InstrumentKind::midiOut);
    CHECK (lynx::midictl::parseMidiOut (kept.channels[2].pluginState).channel == 2);
}

TEST (macros181b_switch_audio_clips_sets_the_mode_of_clips_only)
{
    lynx::Song song;
    song.channels.resize (3);
    song.channels[0].instrument = lynx::InstrumentKind::audioClip;
    song.channels[2].instrument = lynx::InstrumentKind::audioClip;
    CHECK (lynx::setStretchOfAudioClips (song, { 0, 1, 2 }, true, lynx::StretchMethod::tones) == 2);
    CHECK (song.channels[0].sampler.keepPitch && song.channels[0].sampler.stretchMethod == lynx::StretchMethod::tones);
    CHECK (! song.channels[1].sampler.keepPitch);
    // Resample: the stretch engine goes off, the method stays for when it comes back on
    CHECK (lynx::setStretchOfAudioClips (song, { 2 }, false, lynx::StretchMethod::general) == 1);
    CHECK (! song.channels[2].sampler.keepPitch && song.channels[2].sampler.stretchMethod == lynx::StretchMethod::tones);
    CHECK (song.channels[0].sampler.keepPitch);
    CHECK (lynx::setStretchOfAudioClips (song, { 1 }, true, lynx::StretchMethod::beats) == 0);
}
