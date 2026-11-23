// Round 190py (0.1.32): FL-native plug-in parity in FL's demo songs — what the host did not hand the engines: the
// channel PITCH knob's automation (FL channel parameter 4), Gross Beat's automation, the raw scales of Love Philter's
// and Harmless' song-start values / pattern events, and a desktop 3x Osc following FL's automation of its parameters and
// of the channel's voice-processor values (engine revision 4; REA_rodada190py).
#include "core/FlpExport.h"
#include "core/GrossBeat.h"
#include "core/ThreeOsc.h"
#include <cmath>

namespace parity190py
{
std::vector<std::uint8_t> readFile (const std::filesystem::path& p)
{
    std::ifstream in (p, std::ios::binary);
    return { std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char>() };
}

std::filesystem::path demos()
{
    const auto d = std::filesystem::path (realFlpDir) / "Data" / "Demo projects";
    return std::filesystem::exists (d) ? d : std::filesystem::path (realFlpDir) / "Demo projects";
}

// one channel playing key 60 for 2 beats from tick 0, with `lanes` in its pattern; `seconds` at 44.1 kHz
std::vector<float> render (const lynx::ChannelDef& channel, const std::vector<lynx::PatternEvents>& lanes, double seconds = 0.8)
{
    using namespace lynx;
    constexpr double rate = 44100.0;
    auto s = Song::makeDefault();
    s.bpm = 120.0;
    s.mode = PlayMode::song;
    s.channels = { channel };
    Pattern p;
    Note n;
    n.start = 0, n.length = 2 * ppq, n.key = 60, n.velocity = 0.78125f;
    p.notes.push_back (n);
    p.events = lanes;
    s.patterns = { p };
    Clip clip;
    clip.pattern = 0;
    clip.length = 4 * ppq;
    s.playlist = { clip };
    auto state = RackState::build (s, nullptr, rate, [] (const std::string&) { return std::shared_ptr<const SampleData>(); });
    RackRenderer rr;
    constexpr int block = 256;
    rr.prepare (rate, block);
    const auto total = (int) (seconds * rate);
    std::vector<float> l ((std::size_t) total), r ((std::size_t) total);
    const auto tps = ticksPerSample (s.bpm, rate);
    for (int at = 0; at < total; at += block)
    {
        const auto len = std::min (block, total - at);
        rr.process (state.get(), true, (double) at * tps, len, l.data() + at, r.data() + at, nullptr);
    }
    return l;
}

lynx::PatternEvents lane (const lynx::AutomationTarget& t, float value)
{
    lynx::PatternEvents e;
    e.target = t;
    e.events = { { 0, value } };
    return e;
}

double rmsDb (const std::vector<float>& x, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    for (auto i = from; i < to && i < x.size(); ++i)
        sum += (double) x[i] * (double) x[i];
    return 10.0 * std::log10 (sum / (double) std::max<std::size_t> (1, to - from) + 1e-30);
}

// the strongest frequency of x[from, from + n) (n a power of two), by a plain DFT search around `guess` ± 1 octave
double peakHz (const std::vector<float>& x, std::size_t from, std::size_t n, double guess, double rate = 44100.0)
{
    double best = 0.0, bestHz = 0.0;
    for (double hz = guess / 2.0; hz < guess * 2.0; hz *= 1.0005)
    {
        double re = 0.0, im = 0.0;
        for (std::size_t i = 0; i < n && from + i < x.size(); ++i)
        {
            const auto w = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979 * (double) i / (double) n);
            re += w * x[from + i] * std::cos (2.0 * 3.14159265358979 * hz * (double) i / rate);
            im += w * x[from + i] * std::sin (2.0 * 3.14159265358979 * hz * (double) i / rate);
        }
        if (re * re + im * im > best)
            best = re * re + im * im, bestHz = hz;
    }
    return bestHz;
}
} // namespace parity190py

TEST (parity190py_channel_pitch_knob_target)
{
    using namespace lynx;
    auto s = Song::makeDefault();
    ChannelDef c;
    c.instrument = InstrumentKind::sampler;
    c.misc.pitchRange = 12;
    c.misc.rootNote = 48; // (the root's shift lives in sampler.pitch too: +12)
    c.sampler.pitch = 12.0f + 3.0f; // the knob at +3 semitones
    s.channels = { c };
    const AutomationTarget t { AutomationTargetKind::channelParam, 0, 0, channelPitchParam };
    CHECK (std::abs (channelPitchShift (s.channels[0]) - 12.0f) < 1e-6f);
    CHECK (std::abs (s.targetValue (t) - (3.0f / 48.0f + 1.0f) * 0.5f) < 1e-6f); // +3 of FL's ±48
    CHECK (s.setTargetValue (t, (-6.0f / 48.0f + 1.0f) * 0.5f));                    // −6 semitones
    CHECK (std::abs (s.channels[0].sampler.pitch - 6.0f) < 1e-5f);
    // the knob's range (±12 here) does not hold the value: FL plays −24 and +48 as they are
    CHECK (std::abs (channelPitchSemitones (s.channels[0], 0.25f) + 24.0f) < 1e-6f);
    CHECK (std::abs (channelPitchSemitones (s.channels[0], 1.0f) - 48.0f) < 1e-6f);
    // FL's 0xD8 / pattern events carry cents −4800 … 4800: the import's scale gives the same value
    CHECK (std::abs (channelPitchSemitones (s.channels[0], (float) ((-600.0 + 4800.0) / 9600.0)) + 6.0f) < 1e-5f);
}

TEST (parity190py_channel_pitch_automation_moves_the_channel)
{
    using namespace lynx;
    // a 3x Osc channel (default state: a sine on osc 1) playing C5; its PITCH knob automated to +7 semitones (range 12)
    // plays what the same knob saved at +7 plays, bit for bit
    ChannelDef ch;
    ch.instrument = InstrumentKind::threeosc;
    ch.pluginState = flpexport::defaultThreeOscState();
    ch.misc.pitchRange = 12;
    const AutomationTarget t { AutomationTargetKind::channelParam, 0, 0, channelPitchParam };
    const auto automated = parity190py::render (ch, { parity190py::lane (t, (7.0f / 48.0f + 1.0f) * 0.5f) });
    auto saved = ch;
    saved.sampler.pitch = 7.0f;
    const auto still = parity190py::render (saved, {});
    const auto plain = parity190py::render (ch, {});
    CHECK (automated.size() == still.size());
    // (not bit for bit: the automated knob retunes the voice the way a live knob does, from its configured pitch)
    const auto f0 = parity190py::peakHz (plain, 8192, 8192, 261.6), f7 = parity190py::peakHz (automated, 8192, 8192, 392.0),
               fs = parity190py::peakHz (still, 8192, 8192, 392.0);
    const auto la = parity190py::rmsDb (automated, 4410, 30000), ls = parity190py::rmsDb (still, 4410, 30000);
    std::printf ("  3x Osc pitch knob automated +7 st / saved +7 st / at 0: peaks %.1f / %.1f / %.1f Hz, levels %.2f / %.2f dB\n", f7, fs, f0, la, ls);
    CHECK (std::abs (f7 / fs - 1.0) < 0.001);
    CHECK (std::abs (la - ls) < 0.1);
    CHECK (std::abs (f7 / f0 - std::pow (2.0, 7.0 / 12.0)) < 0.005);
}

TEST (parity190py_three_osc_follows_fl_automation_from_revision_4)
{
    using namespace lynx;
    // FL's ProcessParam places and ranges (the DLL, REA_rodada190py): parameter k ≥ 23 = the snapshot's int k − 23
    CHECK (threeosc::flRange (110) == (std::pair { 0, 256 }) && threeosc::flRange (111) == (std::pair { 0, 256 }));
    CHECK (threeosc::flRange (113) == (std::pair { 0, 7 }) && threeosc::flRange (2) == (std::pair { -24, 24 }));
    CHECK (threeosc::flRange (99) == (std::pair { -2400, 2400 }) && threeosc::flRange (31) == (std::pair { -64, 64 }));
    CHECK (threeosc::flRange (25) == (std::pair { 100, 65536 }) && threeosc::flRange (35) == (std::pair { 200, 65536 }));
    CHECK (! threeosc::isFlTarget (23) && ! threeosc::isFlTarget (40) && ! threeosc::isFlTarget (21) && ! threeosc::isFlTarget (112));
    auto state = flpexport::defaultThreeOscState();
    CHECK (threeosc::writeFlParam (state.data(), 110, 77));
    std::int32_t at = 0;
    std::memcpy (&at, state.data() + 0x60 + 0x15c, 4);
    CHECK (at == 77);

    // a channel with its filter on (low pass) and FL's cutoff automated shut: revision 4 follows it, 3 does not
    ChannelDef ch;
    ch.instrument = InstrumentKind::threeosc;
    ch.pluginState = flpexport::defaultThreeOscState();
    const AutomationTarget cut { AutomationTargetKind::channelParam, 0, 0, 110 };
    const auto play = [&] (int rev, bool automate) {
        ch.engineRev = rev;
        return parity190py::render (ch, automate ? std::vector<PatternEvents> { parity190py::lane (cut, 0.02f) } : std::vector<PatternEvents> {});
    };
    const auto open3 = play (3, false), shut3 = play (3, true), open4 = play (4, false), shut4 = play (4, true);
    CHECK (open3 == shut3);  // a project saved before: bit for bit as before
    CHECK (open3 == open4);  // nothing automated: the revision changes nothing
    const auto a = parity190py::rmsDb (open4, 4410, 30000), b = parity190py::rmsDb (shut4, 4410, 30000);
    std::printf ("  3x Osc cutoff automated to 0.02: %.1f -> %.1f dB\n", a, b);
    CHECK (a - b > 6.0);
}

TEST (parity190py_gross_beat_and_scales_in_the_demos)
{
    using namespace lynx;
    // Gross Beat's FL ranges = the exact engine's (the DLL: 0…35 ×2, 0…256 ×2, 0…10000 ×2, −256…256, 0…1, 0…65536)
    CHECK (grossbeat::rawFromParam (0, 1.0f) == 35 && grossbeat::rawFromParam (6, 0.0f) == -256 && grossbeat::rawFromParam (8, 0.0f) == 65536);
    if (realFlpDir.empty() || ! std::filesystem::exists (parity190py::demos()))
        return;
    const char* const scaled[] = { "Fruity Love Philter", "Harmless", "3x Osc" };
    int songs = 0, pitchTargets = 0, grossBeat = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator (parity190py::demos()))
    {
        if (entry.path().extension() != ".flp")
            continue;
        FlpImportReport report;
        FlpImportOptions options;
        options.report = &report;
        std::optional<Song> song;
        try
        {
            song = importFlp (flp::parseProject (parity190py::readFile (entry.path())), options);
        }
        catch (...)
        {
            continue;
        }
        ++songs;
        for (const auto* f : scaled)
            CHECK (report.notReproduced.count (std::string ("Pattern events: value scale not known (") + f + ")") == 0);
        for (const auto& a : song->automations)
        {
            pitchTargets += a.target.kind == AutomationTargetKind::channelParam && a.target.param == channelPitchParam ? 1 : 0;
            if (a.target.kind == AutomationTargetKind::effectParam && a.target.index < (int) song->mixer.size()
                && a.target.slot < (int) song->mixer[(std::size_t) a.target.index].effects.size()
                && song->mixer[(std::size_t) a.target.index].effects[(std::size_t) a.target.slot].kind == EffectKind::grossBeat)
                ++grossBeat;
        }
    }
    std::printf ("  %d demo songs: %d clips on a channel's PITCH knob, %d on Gross Beat\n", songs, pitchTargets, grossBeat);
    CHECK (songs < 40 || (pitchTargets >= 12 && grossBeat >= 8)); // (a clip linked to several knobs counts once)
}
