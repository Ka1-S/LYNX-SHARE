// Round 158: LYNX Soundfont (SF2 / SF3 player, GitHub issue #6). docs/analise/REA_rodada158.md.

#include "core/SoundFont.h"
#include "core/SoundFontIndex.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <filesystem>
#include <fstream>
#include <new>

// the real-time check uses the allocation counter of LynxTuneTests95.inc (operator new replaced there)
namespace
{
namespace sft
{
std::string tempDir (const char* name)
{
    const auto dir = std::filesystem::temp_directory_path() / ("lynx-sf158-" + processTempTag() + "-" + std::string (name)); // per process: parallel runs clobbered each other
    std::error_code ec;
    std::filesystem::remove_all (dir, ec);
    std::filesystem::create_directories (dir);
    return dir.string();
}

std::string writeFile (const std::string& path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream out (path, std::ios::binary);
    out.write ((const char*) bytes.data(), (std::streamsize) bytes.size());
    return path;
}

struct Rig
{
    std::shared_ptr<lynx::sf::FontHandle> font;
    std::unique_ptr<lynx::Instrument> inst;
    lynx::ChannelDef channel;
    double rate = 48000.0;

    explicit Rig (const std::string& path, double sampleRate = 48000.0) : rate (sampleRate)
    {
        font = lynx::sf::FontLibrary::instance().getNow (path);
        inst = lynx::sf::makeInstrument (font);
        inst->prepare (rate);
        channel = lynx::sf::makeChannel (path);
        inst->configure (channel, 120.0);
    }

    void set (int param, float v)
    {
        channel.modelParams[(std::size_t) param] = v;
        inst->configure (channel, 120.0);
    }

    // renders `seconds`, returns the left channel
    std::vector<float> run (double seconds)
    {
        const auto n = (int) (seconds * rate);
        std::vector<float> l ((std::size_t) n, 0.0f), r ((std::size_t) n, 0.0f);
        for (int at = 0; at < n; at += 256)
            inst->render (l.data() + at, r.data() + at, std::min (256, n - at), 1.0f, 1.0f);
        return l;
    }
};

float peak (const std::vector<float>& x, std::size_t from = 0, std::size_t to = SIZE_MAX)
{
    float p = 0;
    for (std::size_t i = from; i < std::min (to, x.size()); ++i)
        p = std::max (p, std::abs (x[i]));
    return p;
}

// zero-crossing frequency over the second half
double frequency (const std::vector<float>& x, double rate)
{
    int crossings = 0;
    std::size_t first = 0, last = 0;
    for (std::size_t i = x.size() / 2 + 1; i < x.size(); ++i)
        if (x[i - 1] < 0 && x[i] >= 0)
        {
            if (crossings == 0)
                first = i;
            last = i;
            ++crossings;
        }
    return crossings > 1 ? (crossings - 1) * rate / (double) (last - first) : 0.0;
}

std::string testFont (const char* name, const std::vector<lynx::sf::TestPreset>& presets, int layers = 2)
{
    return writeFile (tempDir (name) + "/font.sf2", lynx::sf::makeTestFont (presets, layers));
}
} // namespace sft
} // namespace

TEST (soundfont158_parse_generated_font)
{
    const auto bytes = lynx::sf::makeTestFont ({ { "Grand Piano", 0, 0 }, { "808 Kit", 128, 0 }, { "Warm Pad", 0, 89 } }, 3);
    std::string error;
    const auto font = lynx::sf::parseFont (bytes, &error);
    CHECK (font != nullptr);
    if (font == nullptr)
    {
        std::printf ("  parse error: %s\n", error.c_str());
        return;
    }
    CHECK (font->name == "LYNX test font");
    CHECK (font->presets.size() == 3);
    // sorted by bank, then program
    CHECK (font->presets[0].name == "Grand Piano" && font->presets[0].bank == 0 && font->presets[0].program == 0);
    CHECK (font->presets[1].name == "Warm Pad" && font->presets[1].program == 89);
    CHECK (font->presets[2].bank == 128);
    for (const auto& p : font->presets)
        CHECK (p.regions.size() == 6); // 2 key zones × 3 velocity layers
    const auto& r = font->presets[0].regions[0];
    CHECK (r.loKey == 0 && r.hiKey == 59 && r.loVel == 0 && r.hiVel == 41);
    CHECK (r.loop == lynx::sf::LoopMode::continuous && r.loopEnd > r.loopStart && r.loopEnd <= r.end);
    CHECK (r.rootKey == 48 && r.sampleRate == 22050);
    CHECK (std::abs (r.ampEnv.attack - 0.01f) < 0.001f && std::abs (r.ampEnv.release - 0.1f) < 0.005f);
    CHECK (font->findPreset (0, 89) == 1 && font->findPreset (128, 5) == 2 && font->findPreset (3, 0) == 0);

    // the quick scan reads the same presets (file order), from bytes and from a file without the sample data
    const auto info = lynx::sf::scanFont (bytes);
    CHECK (info.valid && info.presets.size() == 3 && info.presets[1].name == "808 Kit" && info.presets[1].bank == 128);
    const auto path = sft::writeFile (sft::tempDir ("scan") + "/font.sf2", bytes);
    const auto fileInfo = lynx::sf::scanFontFile (path);
    CHECK (fileInfo.valid && fileInfo.presets == info.presets && fileInfo.name == "LYNX test font");
    CHECK (std::string (lynx::sf::category (128, 0, "Standard")) == "Drums");
    CHECK (std::string (lynx::sf::category (0, 0, "Grand Piano")) == "Piano");
    CHECK (std::string (lynx::sf::category (0, 89, "Warm")) == "Synth pad");
    CHECK (std::string (lynx::sf::category (0, 38, "TR-808 Bass")) == "Drums"); // the "808" word wins
}

TEST (soundfont158_damaged_files)
{
    const auto bytes = lynx::sf::makeTestFont ({ { "A", 0, 0 }, { "B", 0, 1 } });
    CHECK (lynx::sf::parseFont ({}) == nullptr);
    CHECK (lynx::sf::parseFont (std::vector<std::uint8_t> (100, 0)) == nullptr);
    // every truncation and many byte flips: rejected or parsed, never a crash / out-of-range read
    int parsed = 0;
    for (std::size_t len = 0; len < bytes.size(); len += 7)
    {
        std::vector<std::uint8_t> cut (bytes.begin(), bytes.begin() + (std::ptrdiff_t) len);
        if (lynx::sf::parseFont (cut) != nullptr)
            ++parsed;
        (void) lynx::sf::scanFont (cut);
    }
    CHECK (parsed < 50);
    std::uint32_t seed = 158;
    for (int k = 0; k < 400; ++k)
    {
        auto bad = bytes;
        for (int f = 0; f < 6; ++f)
        {
            seed = seed * 1664525u + 1013904223u;
            bad[(seed >> 8) % bad.size()] = (std::uint8_t) (seed >> 24);
        }
        if (auto font = lynx::sf::parseFont (bad))
        {
            // and it plays without reading out of bounds
            auto inst = lynx::sf::makeInstrument (nullptr);
            for (const auto& p : font->presets)
                for (const auto& r : p.regions)
                    CHECK (r.start < r.end && r.end <= font->samples.size() && r.loopStart >= r.start && r.loopEnd <= r.end);
        }
    }
    const auto missing = lynx::sf::FontLibrary::instance().getNow ("/nonexistent/lynx/font.sf2");
    CHECK (missing->finished() && missing->font() == nullptr && ! missing->error().empty());
}

TEST (soundfont158_sf3_terminal_sample_out_of_range)
{
    // an SF3 whose terminal (EOS) sample header is flagged compressed and points far outside the sample chunk
    auto bytes = lynx::sf::makeTestFont ({ { "A", 0, 0 } });
    std::size_t shdr = 0;
    for (std::size_t i = 0; i + 8 <= bytes.size(); ++i)
        if (std::memcmp (bytes.data() + i, "shdr", 4) == 0)
            shdr = i + 8;
    CHECK (shdr != 0);
    const auto put32 = [&bytes] (std::size_t at, std::uint32_t v) {
        for (int b = 0; b < 4; ++b)
            bytes[at + (std::size_t) b] = (std::uint8_t) (v >> (8 * b));
    };
    const auto records = (std::size_t) (bytes[shdr - 4] | bytes[shdr - 3] << 8 | bytes[shdr - 2] << 16 | bytes[shdr - 1] << 24) / 46;
    bytes[shdr + 44] = 0x10; // the first sample: compressed, no Ogg stream
    const auto eos = shdr + 46 * (records - 1);
    put32 (eos + 20, 0xFFFFFF00u), put32 (eos + 24, 0xFFFFFFFFu);
    bytes[eos + 44] = 0x10;
    // rejected or parsed, never read out of range
    if (auto font = lynx::sf::parseFont (bytes))
        for (const auto& p : font->presets)
            for (const auto& r : p.regions)
                CHECK (r.start < r.end && r.end <= font->samples.size());
}

TEST (soundfont158_note_on_off_and_loops)
{
    const auto path = sft::testFont ("notes", { { "Looped", 0, 0, 0, true }, { "Unlooped", 0, 1, 0, false } });
    sft::Rig rig (path);
    CHECK (rig.font->font() != nullptr);
    rig.inst->noteOn (48, 0.8f);
    const auto held = rig.run (1.0);
    // a looped 0.25 s sample still sounds after a second
    CHECK (sft::peak (held, 40000, 48000) > 0.1f);
    CHECK (std::abs (sft::frequency (held, 48000.0) - 130.81) < 1.0);
    CHECK (lynx::sf::activeVoices (*rig.inst) == 1);
    rig.inst->noteOff (48);
    const auto after = rig.run (0.5);
    CHECK (sft::peak (after, 20000) < 1.0e-4f); // 100 ms release
    CHECK (lynx::sf::activeVoices (*rig.inst) == 0);
    CHECK (rig.inst->idle());

    // without its loop the sample ends; "Use loops" off does the same to a looped one
    rig.set (lynx::sf::pProgram, 1);
    rig.inst->noteOn (48, 0.8f);
    auto x = rig.run (0.6);
    CHECK (sft::peak (x, 0, 10000) > 0.1f && sft::peak (x, 14000) == 0.0f);
    rig.set (lynx::sf::pProgram, 0);
    rig.set (lynx::sf::pUseLoops, 0);
    rig.inst->noteOn (48, 0.8f);
    x = rig.run (0.6);
    CHECK (sft::peak (x, 0, 10000) > 0.1f && sft::peak (x, 14000) == 0.0f);
}

TEST (soundfont158_velocity_layers_and_presets)
{
    const auto path = sft::testFont ("layers", { { "First", 0, 0 }, { "Second", 0, 1 } }, 2);
    sft::Rig rig (path);
    rig.set (lynx::sf::pVelocityCurve, 4); // fixed: only the layer changes the level
    const auto level = [&] (int program, float velocity) {
        rig.set (lynx::sf::pProgram, (float) program);
        rig.inst->noteOn (72, velocity);
        const auto x = rig.run (0.2);
        rig.inst->noteOff (72);
        (void) rig.run (0.3);
        return sft::peak (x, 4800);
    };
    const auto soft = level (0, 0.3f), loud = level (0, 0.9f);
    CHECK (std::abs (loud / soft - 0.8f / 0.5f) < 0.03f); // layer amplitudes 0.5 and 0.8
    // the second preset plays its own samples (half the level)
    CHECK (std::abs (level (1, 0.9f) / loud - 0.5f) < 0.02f);
    // the SoundFont velocity curve: 40·log10(v) dB
    rig.set (lynx::sf::pVelocityCurve, 0);
    const auto part = level (0, 0.6f), full = level (0, 1.0f); // (both in the loud layer)
    const auto half = part, expect = std::pow ((float) std::lround (0.6f * 127.0f) / 127.0f, 2.0f);
    CHECK (std::abs (half / full - expect) < 0.02f);
    // a program change (MIDI) picks the preset too
    const std::uint8_t pc[2] { 0xC0, 1 };
    rig.set (lynx::sf::pVelocityCurve, 4);
    rig.set (lynx::sf::pProgram, 0);
    rig.inst->midiMessage (pc, 2);
    rig.inst->noteOn (72, 0.9f);
    const auto x = rig.run (0.2);
    rig.inst->allNotesOff();
    CHECK (std::abs (sft::peak (x, 4800) / loud - 0.5f) < 0.02f);
}

TEST (soundfont158_exclusive_class_sustain_polyphony)
{
    const auto path = sft::testFont ("excl", { { "Hats", 128, 0, 1, true }, { "Keys", 0, 0, 0, true } }, 1);
    sft::Rig rig (path);
    rig.set (lynx::sf::pBank, 128);
    rig.set (lynx::sf::pProgramMode, 1); // (instrument: note-offs release)
    rig.inst->noteOn (42, 0.8f);
    (void) rig.run (0.1);
    rig.inst->noteOn (46, 0.8f); // same exclusive class: the first stops
    (void) rig.run (0.1);
    CHECK (lynx::sf::activeVoices (*rig.inst) == 1);
    rig.inst->allNotesOff();
    (void) rig.run (0.3);
    CHECK (lynx::sf::activeVoices (*rig.inst) == 0);

    // the sustain pedal holds released notes until it lifts
    rig.set (lynx::sf::pBank, 0);
    const std::uint8_t down[3] { 0xB0, 64, 127 }, up[3] { 0xB0, 64, 0 };
    rig.inst->midiMessage (down, 3);
    rig.inst->noteOn (60, 0.8f);
    (void) rig.run (0.1);
    rig.inst->noteOff (60);
    const auto sustained = rig.run (0.5);
    CHECK (sft::peak (sustained, 20000) > 0.05f && lynx::sf::activeVoices (*rig.inst) == 1);
    rig.inst->midiMessage (up, 3);
    (void) rig.run (0.3);
    CHECK (lynx::sf::activeVoices (*rig.inst) == 0);

    // polyphony 2: the oldest voices make room
    rig.set (lynx::sf::pPolyphony, 2);
    for (int k = 0; k < 6; ++k)
        rig.inst->noteOn (60 + k, 0.8f);
    (void) rig.run (0.1);
    CHECK (lynx::sf::activeVoices (*rig.inst) <= 2);
    rig.inst->allNotesOff();
    (void) rig.run (0.3);
    // drums mode: a note-off is ignored, the (unlooped) sample plays out
    rig.set (lynx::sf::pPolyphony, 64);
    rig.set (lynx::sf::pUseLoops, 0);
    rig.set (lynx::sf::pProgramMode, 2);
    rig.inst->noteOn (60, 0.8f);
    rig.inst->noteOff (60);
    const auto drum = rig.run (0.2);
    CHECK (sft::peak (drum, 7000, 9000) > 0.1f);
}

TEST (soundfont158_pitch_bend_and_sample_rates)
{
    const auto path = sft::testFont ("bend", { { "Keys", 0, 0 } }, 1);
    for (const double rate : { 44100.0, 96000.0 })
    {
        sft::Rig rig (path, rate);
        rig.inst->noteOn (48, 0.8f);
        const auto plain = rig.run (0.5);
        CHECK (std::abs (sft::frequency (plain, rate) - 130.81) < 1.0); // independent of the output rate
        const std::uint8_t bendUp[3] { 0xE0, 127, 127 };
        rig.inst->midiMessage (bendUp, 3);
        const auto bent = rig.run (0.5);
        CHECK (std::abs (sft::frequency (bent, rate) - 130.81 * std::pow (2.0, 2.0 / 12.0)) < 1.5); // ±2 semitones by default
        rig.set (lynx::sf::pBendRange, 12);
        const auto octave = rig.run (0.5);
        CHECK (std::abs (sft::frequency (octave, rate) - 261.63) < 2.5);
        // RPN 0 sets the range from MIDI
        for (const std::uint8_t cc : { 101, 100 })
        {
            const std::uint8_t m[3] { 0xB0, cc, 0 };
            rig.inst->midiMessage (m, 3);
        }
        const std::uint8_t data[3] { 0xB0, 6, 7 };
        rig.inst->midiMessage (data, 3);
        const auto fifth = rig.run (0.5);
        CHECK (std::abs (sft::frequency (fifth, rate) - 130.81 * std::pow (2.0, 7.0 / 12.0)) < 2.0);
        // every resampling quality plays the same pitch
        for (int q = 0; q < 4; ++q)
        {
            rig.set (lynx::sf::pResampling, (float) q);
            const std::uint8_t centre[3] { 0xE0, 0, 64 };
            rig.inst->midiMessage (centre, 3);
            const auto x = rig.run (0.4);
            CHECK (std::abs (sft::frequency (x, rate) - 130.81) < 1.0);
        }
    }
}

TEST (soundfont158_realtime_render_does_not_allocate)
{
    const auto path = sft::testFont ("rt", { { "Keys", 0, 0 } }, 2);
    sft::Rig rig (path);
    rig.set (lynx::sf::pReverbAmount, 0.5f);
    rig.set (lynx::sf::pChorusAmount, 0.5f);
    rig.set (lynx::sf::pResampling, 3);
    std::vector<float> l (512), r (512);
    rig.inst->render (l.data(), r.data(), 512, 1, 1); // (first block: settings taken)
    rt95::allocs = 0;
    rt95::countAllocs = true;
    for (int k = 0; k < 400; ++k)
    {
        if (k % 20 == 0)
            rig.inst->noteOn (40 + k % 40, 0.7f);
        if (k % 20 == 10)
            rig.inst->noteOff (40 + (k - 10) % 40);
        if (k == 100)
        {
            const std::uint8_t m[3] { 0xB0, 64, 127 };
            rig.inst->midiMessage (m, 3);
        }
        rig.inst->setParameter (lynx::sf::pCutoff, 0.5f);
        rig.inst->render (l.data(), r.data(), 512, 1, 1);
    }
    rt95::countAllocs = false;
    CHECK (rt95::allocs.load() == 0);
}

TEST (soundfont158_send_to_mixer_insert)
{
    // FL's SEND TO: the reverb send (here from CC 91) goes dry into mixer insert 3; the chorus sends nowhere
    sft::Rig rig (sft::testFont ("send", { { "Low", 0, 0 } }, 1));
    rig.set (lynx::sf::pReverbTrack, 3);
    rig.set (lynx::sf::pReverbBuiltIn, 0);
    const std::uint8_t cc91[] = { 0xB0, 91, 127 };
    rig.inst->midiMessage (cc91, 3);
    rig.inst->noteOn (60, 0.8f);
    std::vector<float> l (1024, 0.0f), r (1024, 0.0f);
    rig.inst->beginChunk (1024);
    rig.inst->render (l.data(), r.data(), 512, 1.0f, 1.0f);
    rig.inst->render (l.data() + 512, r.data() + 512, 512, 0.5f, 0.5f);
    CHECK (rig.inst->auxOutputCount() == 2 && rig.inst->auxOutputTrack (0) == 3 && rig.inst->auxOutputTrack (1) == -1);
    const auto* a = rig.inst->auxOutputData (0, 0);
    CHECK (a != nullptr);
    if (a != nullptr)
    {
        const std::vector<float> send (a, a + 1024);
        CHECK (sft::peak (send, 0, 512) > 1.0e-3f && sft::peak (send, 512) > 1.0e-3f);
    }
    rig.inst->beginChunk (1024); // a new chunk starts from silence
    CHECK (a == nullptr || a[0] == 0.0f);
    rig.set (lynx::sf::pReverbTrack, -1);
    rig.inst->render (l.data(), r.data(), 256, 1.0f, 1.0f);
    CHECK (rig.inst->auxOutputTrack (0) == -1);
}

TEST (soundfont158_params_roundtrip)
{
    const auto d = lynx::sf::defaultParams();
    CHECK ((int) d.size() == lynx::sf::paramCount);
    const auto p = lynx::sf::readParams ({});
    for (int i = 0; i < lynx::sf::paramCount; ++i)
        CHECK (p[(std::size_t) i] == d[(std::size_t) i]);
    // out of range / NaN / truncated states read as valid values
    const auto q = lynx::sf::readParams ({ 999.0f, NAN, -5.0f });
    CHECK (q[lynx::sf::pBank] == 128 && q[lynx::sf::pProgram] == 0 && q[lynx::sf::pReverbAmount] == 0 && q[lynx::sf::pPolyphony] == 64);
    for (int i = 0; i < lynx::sf::paramCount; ++i)
    {
        const auto& info = lynx::sf::paramInfo (i);
        CHECK (std::abs (lynx::sf::denormalise (i, lynx::sf::normalise (i, info.def)) - info.def) < 1.0e-4f);
        CHECK (! lynx::sf::displayValue (i, info.def).empty());
    }
    const auto c = lynx::sf::makeChannel ("x.sf2", 128, 25);
    CHECK (c.instrument == lynx::InstrumentKind::soundfont && c.samplePath == "x.sf2" && c.modelParams[lynx::sf::pBank] == 128
           && c.modelParams[lynx::sf::pProgram] == 25);
}

TEST (soundfont158_preset_search_index)
{
    const auto dir = sft::tempDir ("index");
    std::filesystem::create_directories (dir + "/sub");
    sft::writeFile (dir + "/General.sf2", lynx::sf::makeTestFont ({ { "Grand Piano", 0, 0 }, { "Bright Piano", 0, 1 }, { "Fingered Bass", 0, 33 } }));
    sft::writeFile (dir + "/sub/Drums.SF2", lynx::sf::makeTestFont ({ { "TR-808", 128, 25 }, { "Standard", 128, 0 } }));
    sft::writeFile (dir + "/sub/notafont.sf2", std::vector<std::uint8_t> (64, 7));
    CHECK (lynx::sf::findFontFiles (dir).size() == 3);
    lynx::sf::PresetIndex index;
    index.setCacheFile (dir + "/cache/index.txt");
    index.setFolders ({ dir });
    for (int k = 0; k < 500 && index.busy(); ++k)
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    CHECK (! index.busy());
    CHECK (index.presetCount() == 5);
    auto hits = index.search ("piano");
    CHECK (hits.size() == 2 && hits[0].category == "Piano");
    hits = index.search ("808");
    CHECK (hits.size() == 1 && hits[0].presetName == "TR-808" && hits[0].bank == 128 && hits[0].program == 25);
    hits = index.search ("drums");
    CHECK (hits.size() == 2);
    hits = index.search ("bass -piano");
    CHECK (hits.size() == 1 && hits[0].presetName == "Fingered Bass");
    CHECK (index.search ("").size() == 5);
    // whole-word name matches rank first
    hits = lynx::sf::searchFonts ({ { "a.sf2", "A", 0, 0, { { "Pianola", 0, 0 }, { "Piano", 0, 1 } } } }, "piano");
    CHECK (hits.size() == 2 && hits[0].presetName == "Piano");
    // the cache file brings the index back
    lynx::sf::PresetIndex again;
    again.setCacheFile (dir + "/cache/index.txt");
    CHECK (again.presetCount() == 5);
    CHECK (lynx::sf::PresetIndex::fromText (lynx::sf::PresetIndex::toText (index.fonts())).size() == 2);
}

namespace
{
// FL Soundfont Player state, version 6 (REA_rodada158 §1)
std::vector<std::uint8_t> flSoundfontState (const std::array<int, 13>& p, const std::string& path, int presetIndex, int resampling,
                                            int programMode, bool reverbBuiltIn)
{
    std::vector<std::uint8_t> s;
    const auto i32 = [&s] (int v) {
        for (int b = 0; b < 4; ++b)
            s.push_back ((std::uint8_t) ((std::uint32_t) v >> (8 * b)));
    };
    i32 (6);
    for (auto v : p)
        i32 (v);
    s.push_back ((std::uint8_t) path.size());
    s.insert (s.end(), path.begin(), path.end());
    i32 (-1), s.push_back (reverbBuiltIn ? 1 : 0); // reverb: send to, built-in
    i32 (4), s.push_back (0);                     // chorus: send to insert 5
    s.push_back (1);
    s.push_back (1), s.push_back (0);             // fix levels, velocity filter off
    i32 (resampling), i32 (1), i32 (presetIndex);
    s.push_back (0);                              // EMU off
    s.push_back (0), s.push_back (1);             // reverb level box off
    i32 (programMode);
    return s;
}
} // namespace

TEST (soundfont158_fl_state_import)
{
    const auto path = sft::testFont ("fl", { { "Low", 0, 5 }, { "Kit", 128, 0 }, { "High", 0, 2 } }, 1);
    // P: program 1, bank 0, reverb 128 (×1), chorus 256 (×20), mod 127, A 100 ms, D 250 (= 1 s), S disabled, R 50 (= 200 ms),
    // DEL disabled, AMT 127 (1000 cents), SPD disabled, CUT 127 (fully open)
    const std::array<int, 13> p { 1, 0, 128, 256, 127, 100, 250, -1, 50, -1, 127, -1, 127 };
    const auto state = flSoundfontState (p, path, 1, 6, 1, true); // index 1 of (0:2, 0:5, 128:0) = bank 0 program 5

    lynx::sf::FlPlayerState fl;
    CHECK (lynx::sf::parseFlPlayerState (state, fl));
    CHECK (fl.version == 6 && fl.path == path && fl.presetIndex == 1 && fl.resampling == 6 && fl.programMode == 1);
    CHECK (fl.reverbBuiltIn && ! fl.chorusBuiltIn && fl.chorusTarget == 4 && ! fl.velocityFilter && ! fl.emuEnvelope && ! fl.reverbLevel);
    // damaged / short / other versions are rejected, never read past the end
    for (std::size_t n = 0; n < 57; ++n)
        CHECK (! lynx::sf::parseFlPlayerState (std::vector<std::uint8_t> (state.begin(), state.begin() + (std::ptrdiff_t) n), fl));
    auto wrongVersion = state;
    wrongVersion[0] = 9;
    CHECK (! lynx::sf::parseFlPlayerState (wrongVersion, fl));
    for (std::size_t n = 57; n < state.size(); ++n)
        lynx::sf::parseFlPlayerState (std::vector<std::uint8_t> (state.begin(), state.begin() + (std::ptrdiff_t) n), fl);

    // the FLP import: the channel becomes a LYNX Soundfont with the preset and FL's overrides
    flp::Project project;
    flp::Channel channel;
    channel.index = 0;
    channel.type = flp::ChannelType::native;
    channel.name = "Strings";
    channel.pluginName = "Fruity Soundfont Player";
    channel.pluginState = state;
    project.channels = { channel };
    channel.index = 1;
    channel.pluginName = "SoundFont Player"; // (FL 2026's name), a version 2 state: P0/P1 pick the preset (129 = drum kit 0)
    channel.pluginState = flSoundfontState ({ 129, 0, 128, 128, 0, -1, -1, -1, -1, -1, -128, -1, -1 }, path, 0, 4, 0, false);
    channel.pluginState[0] = 2;
    channel.pluginState.resize (4 + 52 + 1 + path.size() + 11);
    project.channels.push_back (channel);
    // automation clips on the first channel: CUT (P12) from the minimum ("preset") to the top, and the patch (P0: FL
    // does not automate it)
    for (const auto& [index, param] : { std::pair { 2, 12 }, std::pair { 3, 0 } })
    {
        flp::Channel clip;
        clip.index = index;
        clip.type = flp::ChannelType::automationClip;
        clip.automation = { { 0.0, 0.0 }, { 96.0, 1.0 } };
        project.channels.push_back (clip);
        project.automationLinks.push_back ({ index, { (std::uint32_t) (0x8000 + param) } });
    }
    lynx::FlpImportReport report;
    lynx::FlpImportOptions options;
    options.report = &report;
    const auto song = lynx::importFlp (project, options);
    CHECK (song.channels.size() == 2);
    CHECK (song.automations.size() == 2);
    if (song.automations.size() == 2)
    {
        const auto& cut = song.automations[0];
        CHECK (cut.target.kind == lynx::AutomationTargetKind::channelParam && cut.target.index == 0 && cut.target.param == lynx::sf::pCutoff);
        CHECK (cut.points.size() == 2 && cut.points[0].value == 0.0f && std::abs (cut.points[1].value - 1.0f) < 1.0e-6f);
        CHECK (song.automations[1].target.kind == lynx::AutomationTargetKind::none);
    }
    // FL's scale: half way on A (−1…5940) = 2969 ms
    if (const auto a = lynx::sf::automationFromFl (5, 0.5f))
        CHECK (a->first == lynx::sf::pAttack && lynx::sf::displayValue (a->first, lynx::sf::denormalise (a->first, a->second)) == "2.97 s");
    else
        CHECK (false);
    if (song.channels.size() == 2)
    {
        const auto& c = song.channels[0];
        const auto q = lynx::sf::readParams (c.modelParams);
        CHECK (c.instrument == lynx::InstrumentKind::soundfont && c.samplePath == path && c.name == "Strings");
        CHECK (q[lynx::sf::pBank] == 0 && q[lynx::sf::pProgram] == 5);
        CHECK (lynx::sf::displayValue (lynx::sf::pAttack, q[lynx::sf::pAttack]) == "100 ms");
        CHECK (lynx::sf::displayValue (lynx::sf::pDecay, q[lynx::sf::pDecay]) == "1.00 s");
        CHECK (lynx::sf::displayValue (lynx::sf::pRelease, q[lynx::sf::pRelease]) == "200 ms");
        CHECK (q[lynx::sf::pSustain] == 0.0f && q[lynx::sf::pLfoDelay] == 0.0f && q[lynx::sf::pLfoSpeed] == 0.0f);
        CHECK (lynx::sf::displayValue (lynx::sf::pLfoAmount, q[lynx::sf::pLfoAmount]) == "+1000 cents");
        CHECK (std::abs (q[lynx::sf::pCutoff] - 1.0f) < 1.0e-6f);
        CHECK (lynx::sf::displayValue (lynx::sf::pReverbAmount, q[lynx::sf::pReverbAmount]) == "100 %");
        CHECK (lynx::sf::displayValue (lynx::sf::pChorusAmount, q[lynx::sf::pChorusAmount]) == "2000 %");
        CHECK (q[lynx::sf::pReverbOverride] == 0.0f && q[lynx::sf::pChorusOverride] == 1.0f);
        CHECK (q[lynx::sf::pReverbBuiltIn] == 1.0f && q[lynx::sf::pChorusBuiltIn] == 0.0f && q[lynx::sf::pChorusTrack] == 5);
        CHECK (q[lynx::sf::pResampling] == 2 && q[lynx::sf::pProgramMode] == 1 && q[lynx::sf::pVelocityToCutoff] == 0.0f);
        CHECK (q[lynx::sf::pModWheel] == 1.0f && lynx::sf::displayValue (lynx::sf::pModWheel, q[lynx::sf::pModWheel]) == "100 %");
        const auto& d = song.channels[1];
        const auto r = lynx::sf::readParams (d.modelParams);
        CHECK (d.instrument == lynx::InstrumentKind::soundfont && r[lynx::sf::pBank] == 128 && r[lynx::sf::pProgram] == 0);
        CHECK (r[lynx::sf::pAttack] == 0.0f && r[lynx::sf::pLfoAmount] == 0.0f && r[lynx::sf::pCutoff] == 0.0f && r[lynx::sf::pResampling] == 1);
        CHECK (report.converted["Fruity Soundfont Player"] == 1 && report.converted["SoundFont Player"] == 1);

        // it plays: the imported channel renders its preset
        auto font = lynx::sf::FontLibrary::instance().getNow (path);
        auto inst = lynx::sf::makeInstrument (font);
        inst->prepare (48000.0);
        inst->configure (c, 120.0);
        inst->noteOn (60, 0.8f);
        std::vector<float> l (4800), rr (4800);
        for (int at = 0; at < 4800; at += 480)
            inst->render (l.data() + at, rr.data() + at, 480, 1.0f, 1.0f);
        float peak = 0;
        for (auto x : l)
            peak = std::max (peak, std::abs (x));
        CHECK (peak > 0.01f);
    }

    // FL's own states (plugin database, the SoundFont tracks) [DATA]
    if (! realFlpDir.empty())
    {
        lynx::FlPathRoots roots;
        roots.data = realFlpDir;
        roots.factoryData = std::filesystem::path (realFlpDir).parent_path().string();
        int found = 0, read = 0, fonts = 0;
        for (const char* rel : { "Patches/Plugin database/Generators/Misc/SoundFont Player.fst", "System/SoundFontTrack.fst",
                                 "System/SoundfontPlayerTrack.fst" })
        {
            const auto file = std::filesystem::path (realFlpDir) / rel;
            if (! std::filesystem::exists (file))
                continue;
            ++found;
            std::ifstream f (file, std::ios::binary);
            std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
            try
            {
                const auto preset = flp::parseProject (bytes, true);
                lynx::sf::FlPlayerState s;
                if (! preset.channels.empty() && lynx::sf::parseFlPlayerState (preset.channels.front().pluginState, s))
                {
                    ++read;
                    if (! s.path.empty())
                    {
                        const auto resolved = lynx::resolveFlPath (s.path, roots);
                        const auto info = lynx::sf::scanFontFile (resolved);
                        fonts += info.valid ? 1 : 0;
                        std::printf ("  %s: v%d, %s -> %s (%d presets)\n", rel, s.version, s.path.c_str(), resolved.c_str(), (int) info.presets.size());
                    }
                }
            }
            catch (const flp::Error&)
            {
            }
        }
        std::printf ("  %d FL SoundFont Player states, %d read, %d fonts found\n", found, read, fonts);
        CHECK (read == found);
    }
}
