// Round 190ks (GitHub #198): while recording, the pads clicked in a drum plugin's own window were not recorded. The rack
// now keeps the notes a hosted plugin plays from its window (its MIDI output, not the echo of what it was sent) on the
// channels the engine marks, with their sample, for the engine to record as played notes (core/OwnNotes.h,
// RackRenderer::takeOwnNotes).

#include "core/OwnNotes.h"
#include "core/SoundFont.h"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace own190ks
{
// a stand-in for a hosted drum plugin: while its capture is on, it "sends" the hits scripted for its renders
struct PadPlugin final : lynx::Instrument
{
    void prepare (double) noexcept override {}
    void noteOn (int key, float) noexcept override { received.push_back (key); }
    void noteOff (int) noexcept override {}
    void allNotesOff() noexcept override {}
    void render (float*, float*, int n, float, float) noexcept override
    {
        for (const auto& h : script)
            if (h.render == renders && capture && count < (int) kept.size())
                kept[(std::size_t) count++] = { std::min (h.sample, n - 1), (std::uint8_t) h.key, 9, h.velocity };
        ++renders;
    }
    void setRecordOwnNotes (bool on) noexcept override
    {
        capture = on;
        ++switches;
    }
    int takeOwnNotes (OwnNote* out, int max) noexcept override
    {
        const auto n = std::min (max, count);
        std::copy (kept.begin(), kept.begin() + n, out);
        count = 0;
        return n;
    }
    struct Hit
    {
        int render = 0, sample = 0, key = 36;
        float velocity = 0.8f;
    };
    std::vector<Hit> script;
    std::array<OwnNote, 64> kept {};
    int count = 0, renders = 0, switches = 0;
    bool capture = false;
    std::vector<int> received;
};

lynx::Song twoPlugins()
{
    lynx::Song s;
    s.bpm = 120.0;
    s.mixer = lynx::Song::defaultMixer (2);
    for (int c = 0; c < 2; ++c)
    {
        lynx::ChannelDef ch;
        ch.instrument = lynx::InstrumentKind::plugin;
        ch.plugin.id = "VST3-Pads-" + std::to_string (c);
        s.channels.push_back (ch);
    }
    s.patterns.emplace_back();
    s.patterns[0].lengthTicks = lynx::ppq * 4;
    s.mode = lynx::PlayMode::pattern;
    return s;
}

std::array<std::uint64_t, lynx::RackRenderer::ownNoteWords> marks (std::initializer_list<int> channels)
{
    std::array<std::uint64_t, lynx::RackRenderer::ownNoteWords> m {};
    for (const auto c : channels)
        m[(std::size_t) c >> 6] |= std::uint64_t { 1 } << (c & 63);
    return m;
}
} // namespace own190ks

TEST (own190ks_echo_guard_keeps_the_plugins_own_notes_and_drops_the_echo_of_its_input)
{
    lynx::ownnotes::EchoGuard g;
    const std::int64_t window = 2400; // 50 ms at 48 kHz
    // a pad clicked: its note on and off are the plugin's own
    CHECK (g.noteOut (36, true, 1000, window));
    CHECK (g.noteOut (36, false, 1500, window));
    // a note the host sent (the pattern, a MIDI keyboard) that a "MIDI thru" plugin passes on: not its own, nor its off
    g.noteIn (38, 5000);
    CHECK (! g.noteOut (38, true, 5000, window));
    CHECK (! g.noteOut (38, true, 5000 + window, window)); // (a plugin that delays it by up to 50 ms)
    CHECK (! g.noteOut (38, false, 9000, window));
    // the same pad clicked later than 50 ms after the host's note: its own again
    CHECK (g.noteOut (38, true, 5000 + window + 1, window));
    CHECK (g.noteOut (38, false, 20000, window));
    // a stray note off (no note on of its own before): nothing to record
    CHECK (! g.noteOut (40, false, 20000, window));
    // out-of-range keys are ignored
    CHECK (! g.noteOut (128, true, 0, window));
    g.noteIn (-1, 0);
}

TEST (own190ks_the_rack_takes_the_marked_plugins_own_notes_with_their_sample_in_time_order)
{
    using namespace own190ks;
    auto a = std::make_shared<PadPlugin>();
    auto b = std::make_shared<PadPlugin>();
    // channel 0 (window open): hits in its 1st, 2nd and 4th renders (one render per block of 256 here)
    a->script = { { 0, 100, 36, 0.5f }, { 1, 10, 38, 1.0f }, { 3, 5, 36, 0.0f } };
    // channel 1 (window closed): its hits are not taken
    b->script = { { 0, 0, 42, 0.7f } };
    lynx::RackState::Factories f;
    auto made = 0;
    f.pluginInstruments = [&] (const lynx::PluginRef&, double) -> std::shared_ptr<lynx::Instrument> { return made++ == 0 ? a : b; };
    const auto state = lynx::RackState::build (twoPlugins(), nullptr, 48000.0, f);
    lynx::RackRenderer r;
    r.prepare (48000.0, 256);
    std::vector<float> l (256), rr (256);
    std::array<lynx::RackRenderer::OwnNoteEvent, lynx::RackRenderer::ownNoteCapacity> taken {};

    // nothing marked: no capture is switched on, nothing comes back (no cost when no plugin window is open)
    r.process (state.get(), true, 0.0, 256, l.data(), rr.data(), nullptr);
    CHECK ((r.takeOwnNotes (taken.data(), (int) taken.size())) == (0));
    CHECK ((a->switches) == (0));
    CHECK (! a->capture && ! b->capture);

    // channel 0 marked: its capture is on, channel 1's stays off
    a->renders = 0;
    r.setOwnNoteChannels (marks ({ 0 }));
    std::vector<lynx::RackRenderer::OwnNoteEvent> all;
    for (int block = 0; block < 4; ++block)
    {
        r.process (state.get(), true, block * 256.0 * 120.0 * lynx::ppq / (60.0 * 48000.0), 256, l.data(), rr.data(), nullptr);
        const auto n = r.takeOwnNotes (taken.data(), (int) taken.size());
        for (int k = 0; k < n; ++k)
            all.push_back (taken[(std::size_t) k]);
        for (int k = 1; k < n; ++k)
            CHECK (taken[(std::size_t) k - 1].sample <= taken[(std::size_t) k].sample);
    }
    CHECK (a->capture);
    CHECK (! b->capture);
    CHECK (((int) all.size()) == (3));
    if (all.size() == 3)
    {
        CHECK ((all[0].channel) == (0));
        CHECK (((int) all[0].key) == (36));
        CHECK (std::abs (all[0].velocity - 0.5f) < 1.0e-6f);
        CHECK (((int) all[0].midiChannel) == (9));
        CHECK (((int) all[1].key) == (38));
        CHECK ((all[0].sample) == (100)); // (its sample in the process() block)
        CHECK ((all[1].sample) == (10));
        CHECK (((int) all[2].key) == (36));
        CHECK (all[2].velocity == 0.0f); // (the note off)
        for (const auto& e : all)
            CHECK (e.sample >= 0 && e.sample < 256);
    }

    // unmarked again: the capture is switched off once, then left alone
    r.setOwnNoteChannels (marks ({}));
    const auto before = a->switches;
    for (int block = 0; block < 3; ++block)
        r.process (state.get(), true, 0.0, 256, l.data(), rr.data(), nullptr);
    CHECK (! a->capture);
    CHECK ((a->switches) == (before + 1));
    CHECK ((r.takeOwnNotes (taken.data(), (int) taken.size())) == (0));
}

TEST (own190ks_a_plugins_own_notes_go_into_the_take_like_played_notes)
{
    // what the engine does with them: each becomes a RecordedEvent on the plugin's channel (the typing keyboard's
    // path), so the take's note has the pad's key, its velocity and its length
    lynx::Song song = own190ks::twoPlugins();
    lynx::NoteRecorder rec;
    rec.begin (song.patterns[0].effectiveLength (song.meter()));
    rec.add (song, { (double) lynx::ppq, 1, 36, 100.0f / 127.0f, true });
    rec.add (song, { (double) lynx::ppq + 24, 1, 36, 0.0f, false });
    const auto notes = rec.takeFinished();
    CHECK (((int) notes.size()) == (1));
    if (! notes.empty())
    {
        CHECK ((notes[0].channel) == (1));
        CHECK ((notes[0].key) == (36));
        CHECK ((notes[0].start) == ((std::int64_t) lynx::ppq));
        CHECK (std::abs (notes[0].velocity - 100.0f / 127.0f) < 1.0e-6f);
    }
}

TEST (own190ks_a_soundfont_bank_copied_to_another_file_is_held_once)
{
    // Discord ("can plugins of the same type share resources?"): one bank per path was already shared by every channel
    // playing it; a byte copy of it (a project folder's copy of the library's bank) is now shared too, and a different
    // bank is not
    namespace fs = std::filesystem;
    const auto stamp = std::to_string ((long long) std::chrono::steady_clock::now().time_since_epoch().count());
    const auto dir = fs::temp_directory_path() / ("lynx-own190ks-" + stamp);
    fs::create_directories (dir);
    const auto bytes = lynx::sf::makeTestFont ({ { "Piano", 0, 0 }, { "Kit", 128, 0 } });
    auto other = lynx::sf::makeTestFont ({ { "Organ", 0, 16 } });
    auto write = [&] (const std::string& name, const std::vector<std::uint8_t>& data) {
        const auto path = (dir / name).string();
        std::ofstream (path, std::ios::binary).write ((const char*) data.data(), (std::streamsize) data.size());
        return path;
    };
    const auto a = write ("library.sf2", bytes), b = write ("project-copy.sf2", bytes), c = write ("other.sf2", other);
    auto fa = lynx::sf::loadFontFile (a), fb = lynx::sf::loadFontFile (b), fc = lynx::sf::loadFontFile (c);
    CHECK (fa != nullptr && fb != nullptr && fc != nullptr);
    CHECK (fa == fb); // (one font in memory for both files)
    CHECK (fa != fc);
    // once nothing plays it, it goes: a later load of the copy parses it again
    std::weak_ptr<const lynx::sf::Font> gone = fa;
    fa.reset();
    fb.reset();
    CHECK (gone.expired());
    const auto again = lynx::sf::loadFontFile (b);
    CHECK (again != nullptr && again->presets.size() == 2);
    std::error_code ec;
    fs::remove_all (dir, ec);
}
