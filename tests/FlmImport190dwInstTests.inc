// --- FL Studio Mobile (.flm) import, round 190dw item 4: GMSynth's FLNG / F HP / RING / VOX effects and its mod wheel,
// the rack's MIDI CC and Aftertouch lanes (CCLK controller links), a drum pad's output link, lanes on SoundFont Player
// and 3x Osc parameters, 3x Osc's custom-shape file and the bend of SoundFont Player / Transistor Bass
// (docs/analise/FLM_FORMAT.md §3, §6, §7.4). Every project is written by the tests; no file of Image-Line's is
// committed. The engine tests read FL's copy of FLM's data files and skip without it.

#include "core/GmsMobile.h"
#include "core/MidiControls.h"
#include "core/SoundFont.h"

namespace flm190dw
{
using namespace flm182c;
using Bytes = flm182c::Bytes;

// a RACK chunk with a CCLK chunk (cc, module instance, parameter) appended to its body
inline Bytes withLinks (const Bytes& rackChunk, const std::vector<std::array<int, 3>>& links)
{
    Bytes body (rackChunk.begin() + 8, rackChunk.end());
    Bytes l;
    for (const auto& k : links)
        for (const auto v : k)
            u32 (l, (std::uint32_t) v);
    const auto c = chunk ("CCLK", l);
    put (body, c.data(), c.size());
    return chunk ("RACK", body);
}

// a CSTM chunk body with one PATH ("@R@" root + the relative text), the Slicer's and 3x Osc's layout
inline Bytes pathCustom (const std::string& root, const std::string& rel)
{
    Bytes p;
    const auto text = root + std::string (1, '\0') + rel;
    u32 (p, (std::uint32_t) text.size());
    put (p, text.data(), text.size());
    return chunk ("PATH", p);
}

// the RMS of one side of interleaved stereo between two frames
inline double rmsOf (const std::vector<float>& y, int from, int to, int side)
{
    double a = 0.0;
    for (int i = from; i < to; ++i)
        a += (double) y[(std::size_t) (2 * i + side)] * (double) y[(std::size_t) (2 * i + side)];
    return std::sqrt (a / std::max (1, to - from));
}
// the level of frequency f in one side (a single DFT bin over [from, to))
inline double toneOf (const std::vector<float>& y, int from, int to, int side, double f, double fs)
{
    double re = 0.0, im = 0.0;
    for (int i = from; i < to; ++i)
    {
        const auto ph = 2.0 * 3.141592653589793 * f * i / fs;
        re += y[(std::size_t) (2 * i + side)] * std::cos (ph);
        im -= y[(std::size_t) (2 * i + side)] * std::sin (ph);
    }
    return 2.0 * std::hypot (re, im) / (to - from);
}
} // namespace flm190dw

// GMSynth's four effects that passed the signal through until round 190dw, against their laws (FLM's processors
// 0x1806dc340, 0x1806ddfc0, 0x1806dda30 and the dispatcher's FLNG, spec in the round's notes); the mod wheel
TEST (flm190dw_gmsynth_fx_kinds_and_mod_wheel)
{
    using namespace lynx;
    namespace gm = gmsmobile;
    const auto root = flm190ab::realResources();
    if (root.empty())
        return;
    const int fs = 44100, n = 30000;
    const double f0 = 440.0 * std::pow (2.0, (60 - 69) / 12.0); // key 60, the default patch's sine
    const auto base = gm::defaultPatch();
    const auto with = [&] (int kind, float x, float y) {
        auto p = base;
        const auto b = (std::size_t) gm::param::fxBase;
        p.P[b + gm::param::fxKind] = ((float) kind + 0.5f) / 9.0f;
        p.P[b + gm::param::fxEnable] = 1.0f;
        p.P[b + gm::param::fxAmount] = 0.5f; // (no LFO)
        p.P[b + gm::param::fxX] = x;
        p.P[b + gm::param::fxY] = y;
        return gm::renderReference (p, { { 0, 60, 1.0f, 0.5f, -1 } }, fs, n, root);
    };
    const auto dry = gm::renderReference (base, { { 0, 60, 1.0f, 0.5f, -1 } }, fs, n, root);
    CHECK (dry.size() == 2u * (std::size_t) n);
    if (dry.empty())
        return;
    const auto dryTone = flm190dw::toneOf (dry, 8000, 28000, 0, f0, fs);
    for (const auto kind : { gm::fx::flanger, gm::fx::highPass, gm::fx::ring, gm::fx::vox })
        CHECK (gm::fx::implemented (kind));
    // F HP, X ½, Y 0: four biquads at 2^(7.424·½ + 5) Hz, q 1, feed-forward (1 + cos)/(sin/q + 2), feedback as F LP's:
    // the sine's level is |H(f0)|⁴ of the dry one
    {
        const auto y = with (gm::fx::highPass, 0.5f, 0.0f);
        const double fc = std::pow (2.0, 7.424 * 0.5 + 5.0), q = 1.0, w = 2.0 * 3.141592653589793 * fc / fs;
        const double s = std::sin (w), c = std::cos (w), b = (1.0 + c) / (s / q + 2.0), a1 = -2.0 * q * c / (s + q), a2 = (q - s) / (s + q);
        const std::complex<double> z = std::polar (1.0, -2.0 * 3.141592653589793 * f0 / fs);
        const auto h = std::abs (b * (1.0 - 2.0 * z + z * z) / (1.0 + a1 * z + a2 * z * z));
        const auto got = flm190dw::toneOf (y, 8000, 28000, 0, f0, fs) / dryTone;
        CHECK (std::fabs (20.0 * std::log10 (got) - 80.0 * std::log10 (h)) < 0.05);
    }
    // RING, X ½: a sine of 2^(8.7·½ + 5) Hz times the signal, fully wet once the second gain has risen: the key's tone is
    // gone, the sum and difference tones carry half its amplitude each; Y does nothing
    {
        const auto y = with (gm::fx::ring, 0.5f, 0.0f), y2 = with (gm::fx::ring, 0.5f, 0.9f);
        const double fr = std::pow (2.0, 8.7 * 0.5 + 5.0);
        CHECK (flm190dw::toneOf (y, 8000, 28000, 0, f0, fs) < 0.01 * dryTone);
        CHECK (std::fabs (flm190dw::toneOf (y, 8000, 28000, 0, fr + f0, fs) / dryTone - 0.5) < 0.02
               && std::fabs (flm190dw::toneOf (y, 8000, 28000, 0, fr - f0, fs) / dryTone - 0.5) < 0.02);
        CHECK (y == y2);
        // the second gain starts at 0 and rises by 0.005 a sample: the first sample is 99.5 % dry
        CHECK (std::fabs (y[0] - dry[0]) <= 0.0101f * std::fabs (dry[0]) + 1e-7f);
    }
    // VOX, X ⅓ (formant set B alone, the sum × 0.875), Y ½ (no shift): the eight constant-skirt band-passes (Q 12) at
    // B's centres; the key's level is their summed response
    {
        const auto y = with (gm::fx::vox, 1.0f / 3.0f, 0.5f);
        const double centres[8] { 700, 1250, 2500, 3500, 4200, 8500, 10000, 12000 }, q = 12.0;
        const std::complex<double> z = std::polar (1.0, -2.0 * 3.141592653589793 * f0 / fs);
        std::complex<double> sum = 0.0;
        for (const auto fc : centres)
        {
            const double w = 2.0 * 3.141592653589793 * fc / fs, s = std::sin (w), c = std::cos (w), k = 1.0 / (s / (2.0 * q) + 1.0);
            sum += 0.875 * (s * 0.5 * k) * (1.0 - z * z) / (1.0 - 2.0 * c * k * z + (1.0 - s / (2.0 * q)) * k * z * z);
        }
        const auto got = flm190dw::toneOf (y, 8000, 28000, 0, f0, fs) / dryTone;
        CHECK (std::fabs (20.0 * std::log10 (got) - 20.0 * std::log10 (std::abs (sum))) < 0.05);
    }
    // FLNG, X 0, Y 0: −(L + R)/2 through the whole 5 ms tape (⌊fs·0.005⌋ = 220 samples), added to both sides: a comb
    // 1 − e^(−jωD); feedback −0.96·Y
    {
        const auto y = with (gm::fx::flanger, 0.0f, 0.0f);
        const auto expect = 2.0 * std::fabs (std::sin (3.141592653589793 * f0 * 220.0 / fs));
        const auto got = flm190dw::toneOf (y, 8000, 28000, 0, f0, fs) / dryTone;
        CHECK (std::fabs (20.0 * std::log10 (got / expect)) < 0.3);
        bool finite = true;
        for (const auto v : with (gm::fx::flanger, 0.7f, 1.0f))
            finite = finite && std::isfinite (v) && std::fabs (v) < 8.0f;
        CHECK (finite);
    }
    // switched off, every kind leaves the sound as it was
    for (const auto kind : { gm::fx::flanger, gm::fx::highPass, gm::fx::ring, gm::fx::vox })
    {
        auto p = base;
        p.P[(std::size_t) (gm::param::fxBase + gm::param::fxKind)] = ((float) kind + 0.5f) / 9.0f;
        CHECK (gm::renderReference (p, { { 0, 60, 1.0f, 0.5f, -1 } }, fs, n, root) == dry);
    }
    // the mod wheel on the cutoff (CSTM MW destination 0, amount 1): a saw at cutoff 0.3 opens when the wheel is up
    {
        auto p = base;
        p.waveA = p.waveB = 16;
        p.P[gm::param::cutoff] = 0.3f;
        p.mwDest = 0.0f;
        p.mwAmount = 1.0f;
        const auto play = [&] (float wheel) {
            gm::Player player;
            player.prepare (fs);
            player.setPatch (p, root);
            player.setModWheel (wheel);
            player.note (48, 1.0f);
            std::vector<float> l (512), r (512), out;
            for (int k = 0; k < 40; ++k)
            {
                std::fill (l.begin(), l.end(), 0.0f);
                std::fill (r.begin(), r.end(), 0.0f);
                player.render (l.data(), r.data(), 512, 120.0, -1.0);
                out.insert (out.end(), l.begin(), l.end());
            }
            double d = 0.0;
            for (std::size_t i = 8000; i + 1 < out.size(); ++i)
                d += (double) (out[i + 1] - out[i]) * (double) (out[i + 1] - out[i]);
            return d;
        };
        CHECK (play (1.0f) > 2.0 * play (0.0f));
    }
}

// The rack's MIDI CC and Aftertouch lanes (FLM_FORMAT §3.2, the rack's setter 0x18059e0c0): CC #1 is the mod wheel of
// MiniSynth / GMSynth / SuperSaw, CC #7 the Level, CC #8 / #10 the Pan, CC #64 the sustain pedal, every CC moves the
// parameters CCLK links to it; Aftertouch reaches no module
TEST (flm190dw_cc_and_aftertouch_lanes)
{
    using namespace lynx;
    namespace t = flm190de;
    namespace rp = flm::rackparam;
    const auto ramp = [] { return flm187d::autoClip (0, 4, 4, 1, { { 0, 1, 0 }, { 2, -1, 32768 }, { 4, 1, 65535 } }); };
    const auto bytes = flm190bs::song ({
        t::rack (flm::kindMaster, {}),
        flm190dw::withLinks (t::rack (flm::kindNormal, { t::moduleWith (23, 2, t::superSawDefaults()) }), { { 74, 2, 19 } }),
        t::rack (flm::kindNormal, { t::moduleWith (39, 2, std::vector<float> (149, 0.5f)) }),
        t::channel (flm::kindMaster, "MASTER", 0, {}),
        t::channel (flm::kindNormal, "Saw", 1, { t::lane (0, "Saw", { t::clip (0, 4, 4, 0, 1, { { 0, 1, 60 } }) }),
                                                 t::lane (1, "MIDI CC #1", { ramp() }, 0, rp::firstMidiCc + 1),
                                                 t::lane (1, "MIDI CC #7", { ramp() }, 0, rp::firstMidiCc + 7),
                                                 t::lane (1, "MIDI CC #10", { ramp() }, 0, rp::firstMidiCc + 10),
                                                 t::lane (1, "MIDI CC #74", { ramp() }, 0, rp::firstMidiCc + 74),
                                                 t::lane (1, "MIDI CC #20", { ramp() }, 0, rp::firstMidiCc + 20),
                                                 t::lane (1, "MIDI CC #64", { ramp() }, 0, rp::firstMidiCc + 64),
                                                 t::lane (1, "Aftertouch", { ramp() }, 0, rp::aftertouch) }),
        t::channel (flm::kindNormal, "Osc", 2, { t::lane (0, "Osc", { t::clip (0, 4, 4, 0, 1, { { 0, 1, 60 } }) }),
                                                 t::lane (1, "MIDI CC #1", { ramp() }, 0, rp::firstMidiCc + 1) }) });
    FlpImportReport report;
    const auto s = importFlm (bytes, flm190bs::anyFile (&report));
    CHECK (s.has_value());
    if (! s)
        return;
    const auto* saw = flm182c::channelNamed (*s, "Saw");
    CHECK (saw != nullptr && saw->instrument == InstrumentKind::pocketSuperSaw);
    if (saw == nullptr)
        return;
    const auto sawAt = (int) (saw - s->channels.data());
    const auto insert = saw->mixerTrack;
    // CC #1 → the SuperSaw's wheel, the lane's values as they are
    const auto* wheel = t::automationOf (*s, AutomationTargetKind::channelParam, sawAt, midictl::ccBase + 1);
    CHECK (wheel != nullptr && wheel->points.size() == 3);
    if (wheel != nullptr && wheel->points.size() == 3)
        CHECK (wheel->points[0].value == 0.0f && std::fabs (wheel->points[1].value - 32768.0f / 65535.0f) < 1e-6f && wheel->points[2].value == 1.0f);
    // CC #7 → the insert's volume on FLM's fader law (as a Level lane), CC #10 → its pan
    const auto* level = t::automationOf (*s, AutomationTargetKind::mixerVolume, insert);
    const auto* pan = t::automationOf (*s, AutomationTargetKind::mixerPan, insert);
    CHECK (level != nullptr && pan != nullptr);
    if (level != nullptr && level->points.size() == 3)
        CHECK (level->points[0].value == 0.0f && std::fabs (level->points[2].value - faderPosition (maxFaderGain)) < 1e-4f);
    if (pan != nullptr && pan->points.size() == 3)
        CHECK (std::fabs (pan->points[1].value - 32768.0f / 65535.0f) < 1e-6f);
    // CC #74, linked by CCLK to the SuperSaw's Filter Cutoff (19): that parameter's lane (Pocket SuperSaw's 18)
    const auto* p = modelparams::find (InstrumentKind::pocketSuperSaw, 18);
    const auto* cutoff = t::automationOf (*s, AutomationTargetKind::channelParam, sawAt, 18);
    CHECK (p != nullptr && cutoff != nullptr);
    if (p != nullptr && cutoff != nullptr && cutoff->points.size() == 3)
        CHECK (std::fabs (cutoff->points[2].value - p->normalised (1.0f)) < 1e-6f);
    // the 3x Osc ignores the wheel: nothing to play
    CHECK (report.converted.count ("FL Studio Mobile: mod-wheel automation") == 1
           && report.converted.count ("FL Studio Mobile: mod-wheel automation on instruments that ignore the wheel (nothing to play)") == 1);
    CHECK (report.converted.count ("FL Studio Mobile: Aftertouch automation (no FL Studio Mobile instrument responds to it)") == 1);
    CHECK (report.converted.count ("FL Studio Mobile: MIDI CC automation without a linked parameter (no effect in FL Studio Mobile)") == 1);
    CHECK (report.unsupported.count ("FL Studio Mobile: sustain pedal automation (not imported)") == 1);
    CHECK (report.unsupported.count ("FL Studio Mobile: MIDI CC automation (not imported)") == 0
           && report.unsupported.count ("FL Studio Mobile: Pitch Bend/Aftertouch automation (not imported)") == 0);
    CHECK (report.converted.count ("FL Studio Mobile: Level automation") == 1 && report.converted.count ("FL Studio Mobile: Pan automation") == 1);
}

// A drum pad's output link (LINk's fourth word): the pad plays through the target channel's insert (the Master's id:
// the Master); the bend of SoundFont Player (±2 semitones) and Transistor Bass (none)
TEST (flm190dw_pad_output_link_and_bends)
{
    using namespace lynx;
    namespace t = flm190bs;
    const auto d = std::vector<float> (flm::samplepad::defaults, flm::samplepad::defaults + flm::samplepad::count);
    const auto bytes = t::song ({ flm190de::rack (flm::kindMaster, {}),
                                  flm190de::rack (flm::kindNormal, {}, { t::sampleOf ({ "Own", d }), t::sampleOf ({ "ToBus", d, false, 0.0, 0, true, 0, 1.0, 0.0, 2 }),
                                                                         t::sampleOf ({ "ToMaster", d, false, 0.0, 0, true, 0, 1.0, 0.0, 0 }) }),
                                  flm190de::rack (flm::kindBus, {}),
                                  t::channel (flm::kindMaster, "MASTER", 0, {}),
                                  t::channel (flm::kindNormal, "Kit", 1, { t::lane (3, "Kit", { t::clip (0, 4, 4, 0, 1, { { 0, 0.25, 0 }, { 1, 0.25, 1 }, { 2, 0.25, 2 } }) }) }),
                                  t::channel (flm::kindBus, "Bus", 2, {}) });
    FlpImportReport report;
    const auto song = importFlm (bytes, t::anyFile (&report));
    CHECK (song.has_value());
    if (! song)
        return;
    const auto* own = flm182c::channelNamed (*song, "Own");
    const auto* toBus = flm182c::channelNamed (*song, "ToBus");
    const auto* toMaster = flm182c::channelNamed (*song, "ToMaster");
    CHECK (own != nullptr && toBus != nullptr && toMaster != nullptr);
    if (own == nullptr || toBus == nullptr || toMaster == nullptr)
        return;
    int bus = -1;
    for (std::size_t i = 0; i < song->mixer.size(); ++i)
        if (song->mixer[i].name == "Bus")
            bus = (int) i;
    CHECK (bus > 0 && own->mixerTrack > 0 && own->mixerTrack != bus && toBus->mixerTrack == bus && toMaster->mixerTrack == 0);
    CHECK (report.converted.count ("FL Studio Mobile: drum pad output link (plays through the target channel's insert)") == 1
           && report.converted.at ("FL Studio Mobile: drum pad output link (plays through the target channel's insert)") == 2);

    // a held Pitch Bend of 0.75 (4v − 2 = +1 to every module): SoundFont Player bends by it × 2 / 2 = +1 semitone,
    // Transistor Bass not at all
    const auto bent = [] (std::uint32_t type, std::size_t params, const flm182c::Bytes& custom) {
        flm182c::Bytes r;
        flm182c::u32 (r, flm::kindNormal);
        flm182c::tag (r, "10KR");
        flm182c::Bytes rprm;
        for (const float v : { 0.853125f, 0.5f, 0.0f, 0.0f, 0.75f, 0.0f })
            flm182c::f32 (rprm, v);
        auto body = flm182c::cat ({ r, flm182c::chunk ("RPRM", rprm), flm190de::moduleWith (type, 2, std::vector<float> (params, 0.5f), {}, custom) });
        return flm182c::chunk ("RACK", body);
    };
    flm182c::Bytes sf2;
    {
        flm182c::Bytes c;
        flm182c::tag (c, "SF2D");
        const std::string font = "E:/flm/Song/Piano.sf2";
        flm182c::u32 (c, (std::uint32_t) (font.size() + 8));
        flm182c::u32 (c, (std::uint32_t) font.size());
        flm182c::put (c, font.data(), font.size());
        flm182c::u32 (c, 0);
        sf2 = c;
    }
    const auto bends = t::song ({ flm190de::rack (flm::kindMaster, {}), bent (52, 6, sf2), bent (19, 14, {}),
                                  t::channel (flm::kindMaster, "MASTER", 0, {}),
                                  t::channel (flm::kindNormal, "Font", 1, { t::lane (0, "Font", { t::clip (0, 4, 4, 0, 1, { { 0, 1, 60 } }) }) }),
                                  t::channel (flm::kindNormal, "Bass", 2, { t::lane (0, "Bass", { t::clip (0, 4, 4, 0, 1, { { 0, 1, 60 } }) }) }) });
    FlpImportReport r2;
    const auto s2 = importFlm (bends, t::anyFile (&r2));
    CHECK (s2.has_value());
    if (! s2)
        return;
    const auto* font = flm182c::channelNamed (*s2, "Font");
    const auto* bass = flm182c::channelNamed (*s2, "Bass");
    CHECK (font != nullptr && bass != nullptr);
    if (font != nullptr)
        CHECK (font->instrument == InstrumentKind::soundfont && std::fabs (font->sampler.pitch - 1.0f) < 1e-5f && font->misc.pitchRange == 2);
    if (bass != nullptr)
        CHECK (bass->instrument == InstrumentKind::transistorbass && bass->sampler.pitch == 0.0f && bass->misc.pitchRange == 0);
    CHECK (r2.partialEffects.count ("FL Studio Mobile: channel pitch bend on an instrument whose bend range is not known (not imported)") == 0);
}

// Lanes on SoundFont Player's envelope knobs (only with Envelope = Custom) and on 3x Osc's Pan; 3x Osc's custom shape
// plays the file its CSTM names
TEST (flm190dw_soundfont_and_3xosc_lanes_and_custom_shape)
{
    using namespace lynx;
    namespace t = flm190de;
    flm182c::Bytes sf2;
    {
        flm182c::Bytes c;
        flm182c::tag (c, "SF2D");
        const std::string font = "E:/flm/Song/Piano.sf2";
        flm182c::u32 (c, (std::uint32_t) (font.size() + 8));
        flm182c::u32 (c, (std::uint32_t) font.size());
        flm182c::put (c, font.data(), font.size());
        flm182c::u32 (c, 0);
        sf2 = c;
    }
    std::vector<float> custom { 1.0f, 0.2f, 0.3f, 0.6f, 0.4f, 1.0f }, original { 1.0f, 0.2f, 0.3f, 0.6f, 0.4f, 0.0f };
    auto osc = std::vector<float> (149, 0.5f);
    osc[0] = 1.0f;
    osc[1] = 1.0f; // osc 1: shape ⌊6·1 + ½⌋ = 6, the custom shape
    const auto ramp = [] { return flm187d::autoClip (0, 4, 4, 1, { { 0, 1, 0 }, { 4, 1, 65535 } }); };
    const auto bytes = flm190bs::song ({
        t::rack (flm::kindMaster, {}),
        t::rack (flm::kindNormal, { t::moduleWith (52, 2, custom, {}, sf2) }),
        t::rack (flm::kindNormal, { t::moduleWith (52, 2, original, {}, sf2) }),
        t::rack (flm::kindNormal, { t::moduleWith (39, 2, osc, { 0.5f, 1.0f, 0.75f }, flm190dw::pathCustom ("@R@", "3xOsc Shapes\\HQ\\HQ_Saw.wav")) }),
        t::channel (flm::kindMaster, "MASTER", 0, {}),
        t::channel (flm::kindNormal, "Custom", 1, { t::lane (0, "Custom", { t::clip (0, 4, 4, 0, 1, { { 0, 1, 60 } }) }),
                                                    t::lane (1, "Attack : SoundFont Player", { ramp() }, 2, 1),
                                                    t::lane (1, "Sustain : SoundFont Player", { ramp() }, 2, 3) }),
        t::channel (flm::kindNormal, "Original", 2, { t::lane (0, "Original", { t::clip (0, 4, 4, 0, 1, { { 0, 1, 60 } }) }),
                                                      t::lane (1, "Attack : SoundFont Player", { ramp() }, 2, 1) }),
        t::channel (flm::kindNormal, "Osc", 3, { t::lane (0, "Osc", { t::clip (0, 4, 4, 0, 1, { { 0, 1, 60 } }) }),
                                                 t::lane (1, "Pan : 3x Osc", { ramp() }, 2, 113) }) });
    FlpImportReport report;
    const auto s = importFlm (bytes, flm190bs::anyFile (&report));
    CHECK (s.has_value());
    if (! s)
        return;
    const auto at = [&] (const char* n) {
        const auto* c = flm182c::channelNamed (*s, n);
        return c == nullptr ? -1 : (int) (c - s->channels.data());
    };
    const auto customAt = at ("Custom"), originalAt = at ("Original"), oscAt = at ("Osc");
    CHECK (customAt >= 0 && originalAt >= 0 && oscAt >= 0);
    if (customAt < 0 || originalAt < 0 || oscAt < 0)
        return;
    // the attack lane through the import's law: the end point is the Attack knob at 1, the start the one at 0
    const auto* attack = t::automationOf (*s, AutomationTargetKind::channelParam, customAt, sf::pAttack);
    const auto* sustain = t::automationOf (*s, AutomationTargetKind::channelParam, customAt, sf::pSustain);
    CHECK (attack != nullptr && sustain != nullptr);
    const auto knob = [] (double sec) { return (float) std::clamp (std::log (sec / 0.001) / std::log (20000.0), 1.0e-4, 1.0); };
    if (attack != nullptr && attack->points.size() == 2)
        CHECK (std::fabs (attack->points[0].value - sf::normalise (sf::pAttack, knob (0.005))) < 1e-5f
               && std::fabs (attack->points[1].value - sf::normalise (sf::pAttack, knob (10.0))) < 1e-5f);
    if (sustain != nullptr && sustain->points.size() == 2)
        CHECK (std::fabs (sustain->points[1].value - sf::normalise (sf::pSustain, 1.0f)) < 1e-5f);
    // with the font's own envelope (Envelope = Original) FLM ignores the knob: no lane
    CHECK (t::automationOf (*s, AutomationTargetKind::channelParam, originalAt, sf::pAttack) == nullptr);
    // 3x Osc's Pan on the channel's pan, the Wet Pan (0.75) added
    const auto* pan = t::automationOf (*s, AutomationTargetKind::channelPan, oscAt);
    CHECK (pan != nullptr);
    if (pan != nullptr && pan->points.size() == 2)
        CHECK (std::fabs (pan->points[0].value - 0.25f) < 1e-6f && pan->points[1].value == 1.0f);
    // the custom shape's file, from FLM's resources
    const auto& o = s->channels[(std::size_t) oscAt];
    CHECK (o.instrument == InstrumentKind::threeosc && o.samplePath == "C:/FLM/3xOsc Shapes/HQ/HQ_Saw.wav");
    CHECK (report.converted.count ("FL Studio Mobile: 3x Osc custom shape") == 1);
}
