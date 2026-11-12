// Round 190ea, two bugs found by round 190dv.
// (1) A Song with no pattern at all crashed the docked Piano roll (PianoRollComponent::changeListenerCallback read the
//     current pattern of an empty list). FL always has a pattern: deleting the last one leaves an empty "Pattern 1".
//     Song::keepOnePattern is that invariant, AudioEngine::publishSong applies it to every song state the UI can see,
//     and Song::activePattern is the safe accessor the editors use. Here: every way a song can end up without a pattern
//     (delete, imports, templates, MIDI, the export copy) and the source guards; the app route --patterns190ea drives
//     the engine and the three layouts.
// (2) The Master looks ended in Fruity Limiter, which holds sample peaks only: bright mixes left +0.8 … +3.3 dBTP.
//     Every look now ends in Apex Limiter with True peak on at −1 dBTP (core/MasterLooks.h). Measured here on the
//     16× reference meter of round 190co (apexfix190co::reference16x). LYNX_190EA_REPORT=1 prints the numbers.

#include <chrono>
#include <cstdlib>

#include "core/AlsImport.h"
#include "core/ApexLimiter.h"
#include "core/FlmImport.h"
#include "core/FlpExport.h"
#include "core/FlpImport.h"
#include "core/Loudness.h"
#include "core/MasterLooks.h"
#include "core/MidiImport.h"
#include "core/MixerOps.h"
#include "core/RackOps.h"
#include "core/ShowcaseSong.h"
#include "core/Templates.h"

namespace fix190ea
{
using namespace lynx;

inline bool onePattern (const Song& s)
{
    return ! s.patterns.empty() && s.currentPattern >= 0 && s.currentPattern < (int) s.patterns.size();
}

constexpr double rate = 48000.0;

// a hot, bright drum loop: kick, clap and open hats on a 16th grid at 128 BPM, rim clicks, a sub on the kick; sample
// peak −0.1 dBFS (as a loop out of a sample pack, already limited), stereo with the hats panned
inline void hotDrums (std::vector<float>& l, std::vector<float>& r, double seconds)
{
    const auto n = (std::size_t) (seconds * rate);
    std::vector<double> a (n, 0.0), b (n, 0.0);
    apexfix190co::Noise rnd { 190 };
    const double step = 60.0 / 128.0 / 4.0;
    for (int k = 0; k * step < seconds; ++k)
    {
        const auto at = (std::size_t) (k * step * rate);
        const auto put = [&] (std::size_t i, double left, double right) {
            if (at + i < n)
                a[at + i] += left, b[at + i] += right;
        };
        if (k % 4 == 0) // kick: a falling sine with a click
            for (std::size_t i = 0; i < (std::size_t) (0.25 * rate); ++i)
            {
                const double t = (double) i / rate;
                const double v = std::sin (2.0 * apexfix190co::pi * (45.0 * t + 160.0 / 35.0 * (1.0 - std::exp (-t * 35.0)))) * std::exp (-t * 9.0)
                                 + (i < 24 ? 0.6 * rnd.next() : 0.0);
                put (i, v, v);
            }
        if (k % 8 == 4) // clap: three noise bursts
            for (int burst = 0; burst < 3; ++burst)
                for (std::size_t i = 0; i < (std::size_t) (0.04 * rate); ++i)
                {
                    const double v = 0.8 * rnd.next() * std::exp (-(double) i / (0.008 * rate));
                    put (i + (std::size_t) (burst * 0.011 * rate), v, 0.9 * v);
                }
        // hats every 16th, the off-beats open: white noise through a one-zero high-pass (bright)
        {
            const auto len = (std::size_t) ((k % 2 == 1 ? 0.09 : 0.03) * rate);
            double prev = 0.0;
            for (std::size_t i = 0; i < len; ++i)
            {
                const double w = rnd.next();
                const double v = 0.45 * (w - prev) * std::exp (-(double) i / (len / 4.0));
                prev = w;
                put (i, 0.6 * v, v);
            }
        }
        if (k % 16 == 14) // a rim click: two full-scale samples of opposite sign
            put (0, 1.0, 1.0), put (1, -1.0, -1.0);
    }
    double peak = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        peak = std::max ({ peak, std::abs (a[i]), std::abs (b[i]) });
    const double g = std::pow (10.0, -0.1 / 20.0) / peak;
    l.resize (n), r.resize (n);
    for (std::size_t i = 0; i < n; ++i)
        l[i] = (float) (a[i] * g), r[i] = (float) (b[i] * g);
}

// round 190cn's dense mix (low tones, thumps, noise bursts, clicks) at 0 dBFS sample peak, the right channel reversed
inline void brightMix (std::vector<float>& l, std::vector<float>& r, double seconds)
{
    l = apexfix190co::mix ((int) (seconds * rate), rate, 11);
    for (auto& v : l)
        v = std::clamp (v / 0.7079f, -1.0f, 1.0f);
    r.assign (l.rbegin(), l.rend());
}

// the slots one after the other, in blocks of 256 (as the mixer runs them); the effects' processing time in ms
inline double runChain (const std::vector<EffectSlot>& chain, std::vector<float>& l, std::vector<float>& r)
{
    std::vector<std::unique_ptr<Effect>> fx;
    for (const auto& s : chain)
    {
        fx.push_back (makeEffect (s));
        fx.back()->prepare (rate);
    }
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < l.size(); i += 256)
    {
        const int n = (int) std::min<std::size_t> (256, l.size() - i);
        for (std::size_t k = 0; k < fx.size(); ++k)
            fx[k]->process (l.data() + i, r.data() + i, n, chain[k].params, 128.0);
    }
    return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();
}

struct Measured
{
    double truePeak = -200.0, lufs = -200.0, samplePeak = -200.0;
};

// true peak on the 16× reference (`reference`) or on LYNX's own meter (fixed in round 190co; faster)
inline Measured measure (const std::vector<float>& l, const std::vector<float>& r, bool reference)
{
    Measured m;
    if (reference)
        m.truePeak = std::max (apexfix190co::reference16x (l, 0, l.size()), apexfix190co::reference16x (r, 0, r.size()));
    loudness::Meter meter;
    meter.prepare (rate);
    auto a = l, b = r;
    for (std::size_t i = 0; i < a.size(); i += 512)
        meter.process (a.data() + i, b.data() + i, (int) std::min<std::size_t> (512, a.size() - i));
    meter.flushTruePeak();
    m.lufs = meter.integratedLoudness();
    if (! reference)
        m.truePeak = meter.truePeakDb();
    float peak = 0.0f;
    for (std::size_t i = 0; i < l.size(); ++i)
        peak = std::max ({ peak, std::abs (l[i]), std::abs (r[i]) });
    m.samplePeak = 20.0 * std::log10 (std::max (1e-12, (double) peak));
    return m;
}

// a Master with an EQ and an older project's four-slot look (no true-peak stage) at rows 1…4
inline Song legacyLookSong (const MasterLookSettings& s)
{
    Song song;
    song.mixer = Song::defaultMixer (2);
    auto eq = makeEffectSlot (EffectKind::eq);
    eq.row = 0;
    song.mixer[0].effects.push_back (eq);
    auto chain = looks::chain (s);
    chain.resize ((std::size_t) looks::legacySlots);
    int row = 1;
    for (auto& slot : chain)
    {
        slot.row = row++;
        song.mixer[0].effects.push_back (slot);
    }
    song.masterLook = s;
    return song;
}
} // namespace fix190ea

// --- (1) at least one pattern --------------------------------------------------------------------------------------

TEST (fix190ea_song_keeps_one_pattern)
{
    using namespace lynx;
    using namespace fix190ea;
    Song bare; // what only code builds by hand (Song{}): no pattern
    CHECK (bare.patterns.empty());
    const auto& constBare = bare;
    CHECK (constBare.activePattern().notes.empty() && constBare.activePattern().events.empty()); // (no crash, no change)
    CHECK (bare.patterns.empty());
    CHECK (bare.keepOnePattern());
    CHECK (bare.patterns.size() == 1 && bare.patterns[0].name == "Pattern 1" && bare.currentPattern == 0);
    CHECK (! bare.keepOnePattern()); // (nothing more to do)

    // an index out of range is put back in range, both ways
    Song s;
    s.patterns.resize (3);
    s.currentPattern = 7;
    CHECK (std::addressof (std::as_const (s).activePattern()) == std::addressof (s.patterns[2])); // (const: read, clamped)
    CHECK (s.currentPattern == 7);
    CHECK (s.keepOnePattern() && s.currentPattern == 2);
    s.currentPattern = -4;
    CHECK (std::addressof (s.activePattern()) == std::addressof (s.patterns[0]) && s.currentPattern == 0);

    // the non-const accessor makes the pattern an edit writes into
    Song edit;
    edit.activePattern().notes.push_back ({});
    CHECK (edit.patterns.size() == 1 && edit.patterns[0].notes.size() == 1);
}

TEST (fix190ea_deleting_the_last_pattern_leaves_pattern_1)
{
    using namespace lynx;
    using namespace fix190ea;
    auto s = Song::makeDefault();
    s.patterns.resize (3);
    s.patterns[1].notes.push_back ({});
    Clip clip;
    clip.pattern = 1;
    clip.length = ppq * 4;
    s.playlist.push_back (clip);
    s.currentPattern = 2;
    // Patterns › Delete one by one (MainComponent: s.removePattern (s.currentPattern))
    for (int k = 0; k < 5; ++k)
    {
        s.removePattern (s.currentPattern);
        CHECK (onePattern (s));
    }
    CHECK (s.patterns.size() == 1 && s.patterns[0].name == "Pattern 1" && s.patterns[0].notes.empty() && s.playlist.empty());

    // the picker's Delete unused / Merge identical (deletePatterns keeps one)
    auto t = Song::makeDefault();
    t.patterns.resize (4);
    CHECK (deletePatterns (t, { 0, 1, 2, 3 }) == 3 && onePattern (t));
    t.patterns.resize (4);
    deletePatterns (t, unusedPatterns (t));
    CHECK (onePattern (t));
    t.patterns.resize (4);
    mergeIdenticalPatterns (t);
    CHECK (onePattern (t));
}

TEST (fix190ea_new_projects_and_imports_have_a_pattern)
{
    using namespace lynx;
    using namespace fix190ea;
    // File › New: every built-in template (Empty, Basic beat, Trap with 808, House) and the default song
    const auto names = templateNames();
    CHECK (names.size() >= 4 && builtInTemplateIndex ("Empty") >= 0);
    for (int i = 0; i < (int) names.size(); ++i)
    {
        CHECK (onePattern (makeTemplate (i)));
        CHECK (onePattern (makeNewProject (i)));
    }
    CHECK (onePattern (makeNewProject()) && onePattern (Song::makeDefault()));

    // an FL project with no pattern (FL itself always saves one; a hand-made or damaged file may not)
    CHECK (onePattern (importFlp (flp::Project {})));
    // an FL Studio Mobile project with none
    if (const auto flm = importFlmProject (flm::Project {}); flm)
        CHECK (onePattern (*flm));
    // a Live set with no track and no clip
    std::string error;
    const std::string set = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Ableton MajorVersion=\"5\" MinorVersion=\"12.0_12117\" Creator=\"test\">"
                            "<LiveSet><Tracks /><MainTrack><DeviceChain><DeviceChain><Devices /></DeviceChain></DeviceChain></MainTrack></LiveSet></Ableton>";
    const auto als = importAls (std::vector<std::uint8_t> (set.begin(), set.end()), {}, &error);
    CHECK (als.has_value());
    if (als)
        CHECK (onePattern (*als));

    // MIDI › Start new project: a file with no note is refused (nothing reaches the engine); one note gives one pattern
    midiimport::ParsedMidi midi;
    midiimport::Options o;
    o.newProject = true;
    Song fromMidi;
    CHECK (! midiimport::importInto (fromMidi, midi, o, "MIDI", error));
    midi.trackNames = { "Track 1" };
    midi.events.push_back ({ 0, 0, 0x90, 60, 100 });
    midi.events.push_back ({ 0, ppq, 0x80, 60, 0 });
    const bool imported = midiimport::importInto (fromMidi, midi, o, "MIDI", error);
    CHECK (imported);
    if (imported)
        CHECK (onePattern (fromMidi));

    // the export's copy of a song without a pattern
    std::int64_t ticks = 0;
    CHECK (onePattern (makeExportSong (Song {}, false, 0, ticks)));
}

TEST (fix190ea_engine_and_editors_keep_and_guard_the_pattern)
{
    using namespace i18ntest;
    // the engine keeps the invariant on every publish (new / opened / imported song, edit, undo, redo, a remote song)
    // and right after an edit (before the edit's follow-ups read the current pattern)
    const auto engine = readFile ("src/engine/AudioEngine.cpp");
    const auto publish = engine.find ("void AudioEngine::publishSong()");
    CHECK (publish != std::string::npos);
    const auto keep = engine.find ("song.keepOnePattern();", publish);
    const auto normalize = engine.find ("uids::normalize (song);", publish);
    CHECK (keep != std::string::npos && normalize != std::string::npos && keep < normalize);
    const auto editSong = engine.find ("void AudioEngine::editSong (");
    const auto afterEdit = engine.find ("edit (song);\n    song.keepOnePattern();", editSong);
    CHECK (editSong != std::string::npos && afterEdit != std::string::npos && afterEdit < engine.find ("void AudioEngine::editSongPatternOrder"));
    // the editors' current-pattern helpers go through the safe accessor
    const auto roll = readFile ("src/ui/PianoRollComponent.cpp");
    CHECK (roll.find ("const Pattern& currentPattern (const Song& s)\n{\n    return s.activePattern();") != std::string::npos);
    CHECK (roll.find ("Pattern& currentPattern (Song& s)\n{\n    return s.activePattern();") != std::string::npos);
    CHECK (readFile ("src/ui/EventEditor.cpp").find ("return engine.getSong().activePattern();") != std::string::npos);
    // no unchecked "song.patterns[(std::size_t) song.currentPattern]" is left in the UI
    for (const char* file : { "src/ui/PianoRollComponent.cpp", "src/ui/ChannelRackComponent.cpp", "src/ui/MainComponent.cpp", "src/ui/EditorTools.cpp",
                              "src/ui/PlaylistComponent.cpp", "src/ui/ControllerBridge.cpp", "src/ui/EventEditor.cpp" })
    {
        const auto code = readFile (file);
        CHECK (! code.empty());
        for (const char* var : { "song", "s", "followSong" })
        {
            const auto unchecked = std::string (var) + ".patterns[(std::size_t) " + var + ".currentPattern]";
            if (code.find (unchecked) != std::string::npos)
                std::printf ("  unchecked current pattern in %s: %s\n", file, unchecked.c_str());
            CHECK (code.find (unchecked) == std::string::npos);
        }
        CHECK (code.find ("engine.getSong().patterns[(std::size_t) engine.getSong().currentPattern]") == std::string::npos);
    }
}

// --- (2) the Master looks end true-peak safe ------------------------------------------------------------------------

TEST (fix190ea_looks_end_in_a_true_peak_stage)
{
    using namespace lynx;
    for (int id = 1; id <= looks::count; ++id)
    {
        const auto c = looks::chain (MasterLookSettings { id, 0.5f, { 0, 0, 0 }, 0 });
        CHECK (c.size() == (std::size_t) looks::chainSlots && looks::chainSlots == 5);
        if (c.size() != 5)
            continue;
        const auto& tp = c.back();
        CHECK (tp.kind == EffectKind::apexLimiter && tp.look == id);
        CHECK (apex::gainDb (tp.params[(std::size_t) apex::pGain]) == 0.0);
        CHECK (std::abs (apex::ceilingDb (tp.params[(std::size_t) apex::pCeiling]) - (double) (looks::trueCeilingDb - looks::truePeakMarginDb)) < 1e-4);
        CHECK (tp.params[(std::size_t) apex::pTruePeak] >= 0.5f);
        CHECK (apex::oversamplingFactor (tp.params[(std::size_t) apex::pOversampling]) == 1 && apex::ditherBits (tp.params[(std::size_t) apex::pDither]) == 0);
    }
    CHECK (looks::trueCeilingDb == -1.0f);
    // the showcase song (Demo.lynx, the website's demo.mp3) ends with the same stage, after its own Limiter
    const auto showcase = makeShowcaseSong();
    const auto& master = showcase.mixer[0].effects;
    CHECK (master.size() >= 2 && master.back().kind == EffectKind::apexLimiter && master.back().look == 0
           && master[master.size() - 2].kind == EffectKind::limiter && master.back().params == looks::truePeakStage().params);
}

// Below its ceiling the stage changes nothing but the time (its constant latency): the looks keep their sound
TEST (fix190ea_true_peak_stage_is_transparent_below_the_ceiling)
{
    using namespace lynx;
    using namespace fix190ea;
    const auto slot = looks::chain (MasterLookSettings { looks::balanced, 0.5f, { 0, 0, 0 }, 0 }).back();
    auto l = apexfix190co::mix ((int) rate, rate, 5), r = l;
    for (auto& v : l)
        v *= 0.25f; // peak −15 dBFS, true peak well under −1 dBTP
    std::reverse (r.begin(), r.end());
    for (auto& v : r)
        v *= 0.25f;
    const auto inL = l, inR = r;
    auto fx = makeEffect (slot);
    fx->prepare (rate);
    const auto latency = fx->latencySamples();
    CHECK (latency > 0 && latency < (int) (0.01 * rate)); // (a few ms: 5 ms look-ahead reach + the guard)
    for (std::size_t i = 0; i < l.size(); i += 256)
        fx->process (l.data() + i, r.data() + i, (int) std::min<std::size_t> (256, l.size() - i), slot.params, 120.0);
    double worst = 0.0;
    for (std::size_t i = (std::size_t) latency; i < l.size(); ++i)
        worst = std::max ({ worst, std::abs ((double) l[i] - inL[i - (std::size_t) latency]), std::abs ((double) r[i] - inR[i - (std::size_t) latency]) });
    if (worst > 1e-6)
        std::printf ("  true-peak stage below its ceiling: largest difference %.3g\n", worst);
    CHECK (worst <= 1e-6); // (−120 dB)
}

TEST (fix190ea_every_look_holds_minus_1_dbtp)
{
    using namespace lynx;
    using namespace fix190ea;
    const bool report = std::getenv ("LYNX_190EA_REPORT") != nullptr;
    std::vector<float> drumsL, drumsR, mixL, mixR;
    hotDrums (drumsL, drumsR, 2.0);
    brightMix (mixL, mixR, 1.5);
    double worstBefore = -200.0, worstAfter = -200.0, worstLoudness = 0.0, msBefore = 0.0, msAfter = 0.0;
    if (report)
        std::printf ("  look         intensity signal  | before: dBTP  LUFS   | after: dBTP  LUFS\n");
    for (int id = 1; id <= looks::count; ++id)
        for (const float intensity : { 0.25f, 0.5f, 0.85f, 1.0f })
            for (int signal = 0; signal < 2; ++signal)
            {
                if (intensity == 1.0f && id != looks::loud)
                    continue; // (Heavy's trend beyond 0.85: the loudest look only, to keep the test short)
                const auto& inL = signal == 0 ? drumsL : mixL;
                const auto& inR = signal == 0 ? drumsR : mixR;
                auto chain = looks::chain (MasterLookSettings { id, intensity, { 0, 0, 0 }, 0 });
                auto legacy = chain;
                legacy.resize ((std::size_t) looks::legacySlots); // the 0.1.26 look (Fruity Limiter last)
                auto bl = inL, br = inR, al = inL, ar = inR;
                msBefore += runChain (legacy, bl, br);
                msAfter += runChain (chain, al, ar);
                const auto b = measure (bl, br, report), a = measure (al, ar, true);
                worstBefore = std::max (worstBefore, b.truePeak);
                worstAfter = std::max (worstAfter, a.truePeak);
                worstLoudness = std::max (worstLoudness, std::abs (a.lufs - b.lufs));
                if (report)
                    std::printf ("  %-12s %.2f      %-7s |        %+6.2f %6.2f |       %+6.2f %6.2f\n", looks::name (id), intensity, signal == 0 ? "drums" : "mix",
                                 b.truePeak, b.lufs, a.truePeak, a.lufs);
                CHECK (a.truePeak <= (double) looks::trueCeilingDb + 0.005); // (the stated ceiling, on the 16× reference)
                CHECK (a.samplePeak <= (double) looks::trueCeilingDb);
                CHECK (std::abs (a.lufs - b.lufs) < 0.5); // only the overs are touched
            }
    if (report)
        std::printf ("  worst true peak: before %+.2f dBTP, after %+.2f dBTP; loudness change at most %.2f LU; chain time %.0f -> %.0f ms\n",
                     worstBefore, worstAfter, worstLoudness, msBefore, msAfter);
    CHECK (worstBefore > -0.5); // (the signals do show the bug on the four-slot look)
    CHECK (worstAfter <= (double) looks::trueCeilingDb + 0.005);
}

// Old projects: a look saved before this round keeps its four slots (nothing re-creates a look on load: the slots are
// the project's own effects). Applying a look again gives the five-slot chain, in place where the next row is free.
TEST (fix190ea_older_four_slot_looks)
{
    using namespace lynx;
    using namespace fix190ea;
    const MasterLookSettings normal { looks::warmTape, 0.5f, { 1, 0, -1 }, -2 };
    auto song = legacyLookSong (normal);
    const auto saved = song.mixer[0].effects;
    CHECK (looks::present (song) && ! looks::custom (song) && ! looks::bypassed (song)); // (not "edited by hand")
    looks::setBypassed (song, true);
    CHECK (looks::bypassed (song));
    looks::setBypassed (song, false);
    CHECK (song.mixer[0].effects.size() == saved.size());

    // a user effect two rows after the look: the true-peak slot takes row 5, the user effect stays at row 7
    auto withAfter = song;
    auto delay = makeEffectSlot (EffectKind::delay);
    delay.row = 7;
    withAfter.mixer[0].effects.push_back (delay);
    looks::setBypassed (withAfter, true); // ("Hear without" is kept)
    const MasterLookSettings heavy { looks::warmTape, 0.85f, { 1, 0, -1 }, -2 };
    CHECK (looks::apply (withAfter, heavy));
    const auto& fx = withAfter.mixer[0].effects;
    CHECK (fx.size() == 7 && fx[5].kind == EffectKind::apexLimiter && fx[5].look == looks::warmTape && fx[5].row == 5);
    CHECK (fx[6].kind == EffectKind::delay && fx[6].row == 7 && fx[1].row == 1 && fx[4].row == 4);
    CHECK (looks::bypassed (withAfter) && ! looks::custom (withAfter) && withAfter.masterLook == heavy);
    const auto rows = mixerops::effectRows (fx);
    CHECK (std::is_sorted (rows.begin(), rows.end()) && std::adjacent_find (rows.begin(), rows.end()) == rows.end());

    // the row after the look is taken: that effect moves down one row (the order stays), still bypassed
    auto packed = legacyLookSong (normal);
    auto right = makeEffectSlot (EffectKind::delay);
    right.row = 5;
    packed.mixer[0].effects.push_back (right);
    right.row = 7;
    packed.mixer[0].effects.push_back (right);
    looks::setBypassed (packed, true);
    CHECK (looks::apply (packed, heavy));
    const auto& p = packed.mixer[0].effects;
    CHECK (p.size() == 8 && p[5].kind == EffectKind::apexLimiter && p[5].row == 5 && p[6].kind == EffectKind::delay && p[6].row == 6
           && p[7].row == 7);
    CHECK (looks::bypassed (packed) && ! looks::custom (packed));

    // no room at all (ten slots in use): nothing changes, the Finish dock says so
    auto full = legacyLookSong (normal);
    for (int row = 5; row < maxEffectSlots; ++row)
    {
        right.row = row;
        full.mixer[0].effects.push_back (right);
    }
    const auto before = full.mixer[0].effects.size();
    CHECK (before == (std::size_t) maxEffectSlots && ! looks::apply (full, heavy) && full.mixer[0].effects.size() == before && full.masterLook == normal);
    // a fresh chain when the old one is not in one piece: after the Master's last effect
    auto split = legacyLookSong (normal);
    split.mixer[0].effects.erase (split.mixer[0].effects.begin()); // (no EQ before it)
    auto mid = makeEffectSlot (EffectKind::delay);
    mid.row = 3;
    split.mixer[0].effects.insert (split.mixer[0].effects.begin() + 2, mid); // look, look, delay, look, look
    split.mixer[0].effects[3].row = 4, split.mixer[0].effects[4].row = 5;
    CHECK (looks::apply (split, heavy));
    CHECK (split.mixer[0].effects.size() == 6 && split.mixer[0].effects[0].look == 0 && split.mixer[0].effects.back().kind == EffectKind::apexLimiter);

    // a hand edit of an old look is still "Custom"
    auto edited = legacyLookSong (normal);
    edited.mixer[0].effects[2].params[6] = 0.1f;
    CHECK (looks::custom (edited));
}

TEST (fix190ea_fl_export_of_a_look)
{
    using namespace lynx;
    auto song = Song::makeDefault();
    CHECK (looks::apply (song, MasterLookSettings { looks::balanced, 0.5f, { 0, 0, 0 }, 0 }));
    flpexport::Report report;
    const auto bytes = flpexport::exportFlp (song, {}, report);
    CHECK (! bytes.empty());
    // FL's four plugins are written exactly; the true-peak stage has no FL equivalent and is listed as left out
    int fl = 0;
    for (const auto& e : report.exact)
        for (const char* name : { "Fruity Parametric EQ 2", "Fruity Multiband Compressor", "Fruity Soft Clipper", "Fruity Limiter" })
            fl += e.find (name) != std::string::npos ? 1 : 0;
    CHECK (fl >= 4);
    CHECK (std::any_of (report.dropped.begin(), report.dropped.end(), [] (const std::string& d) { return d.find ("Apex Limiter") != std::string::npos; }));
}
