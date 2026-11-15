// Round 190fs: the user's own presets of LYNX's built-in generators (core/GeneratorPresets.h): the file round trip is
// bit-identical for every generator kind, a loaded preset sounds the same (null test), the channel keeps its own
// settings, other preset files are told apart, and FL's .fst of a third-party plugin names its plugin.

#include "../tools/WavReader.h"
#include "core/EffectPresets.h"
#include "core/Effects.h"
#include "core/FlWrapper.h"
#include "core/FlpExport.h"
#include "core/GeneratorPresets.h"
#include "core/History.h"
#include "core/LynxBanks.h"
#include "core/NativeEngines.h"
#include "core/PluginPresets.h"
#include "core/SynthPresets.h"
#include "flp/FlpWrite.h"

namespace genpreset190fs
{
// One note (C5, velocity 0.8) on channel 0 of a song holding `c` alone: 0.3 s at 44.1 kHz, both sides interleaved
std::vector<float> render (const lynx::ChannelDef& c)
{
    lynx::Song s;
    s.mixer = lynx::Song::defaultMixer (2);
    auto ch = c;
    ch.mixerTrack = 1;
    s.channels.push_back (ch);
    s.patterns.emplace_back();
    // (the engines' data files and the channel's sample as the app reads them)
    auto samples = [] (const std::string& path) -> std::shared_ptr<const lynx::SampleData> {
        auto p = path;
        std::replace (p.begin(), p.end(), '\\', '/');
        return lynx::tools::readWav (lynx::existingPathIgnoringCase (p));
    };
    auto state = lynx::RackState::build (s, nullptr, 44100.0, samples);
    lynx::RackRenderer r;
    r.prepare (44100.0, 512);
    lynx::LiveEventQueue live;
    lynx::NoteEvent on;
    on.channel = 0;
    on.key = 60;
    on.velocity = 0.8f;
    live.push (on);
    std::vector<float> out;
    std::vector<float> l (512), rr (512);
    for (int done = 0; done < 13230; done += 512)
    {
        r.process (state.get(), false, 0.0, 512, l.data(), rr.data(), done == 0 ? &live : nullptr);
        for (int i = 0; i < 512; ++i)
            out.push_back (l[(std::size_t) i]), out.push_back (rr[(std::size_t) i]);
    }
    return out;
}

bool sameBits (const std::vector<float>& a, const std::vector<float>& b)
{
    return a.size() == b.size() && (a.empty() || std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0);
}

double peak (const std::vector<float>& a)
{
    double p = 0.0;
    for (const auto v : a)
        p = std::max (p, (double) std::fabs (v));
    return p;
}

// Save → load of `c` as a user preset named `name`, onto a channel of the same generator with other settings and
// another sound: the sound comes back bit for bit, the channel keeps its own settings and takes the preset's name
bool roundTrip (const lynx::ChannelDef& c, const std::string& name, lynx::ChannelDef* loaded = nullptr)
{
    using namespace lynx;
    const auto text = genpresets::toText (name, c);
    const auto p = genpresets::fromText (text);
    if (! p || p->kind != c.instrument || p->name != name || ! genpresets::sameSound (p->sound, c))
        return false;
    ChannelDef other;
    other.instrument = c.instrument;
    other.name = "Other";
    other.volume = 0.31f;
    other.pan = -0.4f;
    other.mixerTrack = 7;
    other.colour = 0xff123456u;
    other.misc.pitchRange = 12;
    other.pluginState = { 1, 2, 3 };
    other.modelParams = { 0.5f };
    const auto on = genpresets::withPreset (other, *p);
    if (! on || ! genpresets::sameSound (*on, c) || on->name != name || on->volume != 0.31f || on->pan != -0.4f || on->mixerTrack != 7
        || on->colour != 0xff123456u || on->misc.pitchRange != 12)
        return false;
    // the text of the loaded preset is the text that was saved (nothing drifts over saves)
    if (genpresets::toText (name, *on) != text)
        return false;
    // the null test's channel: `c`'s own settings, another sound, then the preset loaded onto it
    if (loaded != nullptr)
    {
        auto scrambled = c;
        scrambled.pluginState = { 1, 2, 3 };
        scrambled.modelParams = { 0.5f };
        scrambled.samplePath.clear();
        scrambled.zones.clear();
        scrambled.synth = SynthSettings {};
        scrambled.synth.cutoff = 123.0f;
        const auto back = genpresets::withPreset (scrambled, *p);
        if (! back || ! genpresets::sameSound (*back, c))
            return false;
        *loaded = *back;
        loaded->name = c.name; // (the name is not part of the sound)
    }
    return true;
}
} // namespace genpreset190fs

TEST (genpreset190fs_generator_ids_round_trip_and_are_unique)
{
    using namespace lynx;
    std::set<std::string> seen;
    for (int k = 0; k <= (int) InstrumentKind::pocketSuperSaw; ++k)
    {
        const auto kind = (InstrumentKind) k;
        const auto id = genpresets::kindId (kind);
        CHECK (! id.empty());
        CHECK (seen.insert (id).second);
        CHECK (genpresets::kindFromId (id) == kind);
        CHECK (! genpresets::engineName (kind).empty());
    }
    CHECK (! genpresets::kindFromId ("no-such-generator").has_value());
    // the ids are the .lynx project's (engine/ProjectFile.cpp instrumentId)
    CHECK (genpresets::kindId (InstrumentKind::dx10) == "dx10" && genpresets::kindId (InstrumentKind::threeosc) == "threeosc"
           && genpresets::kindId (InstrumentKind::keplerExo) == "keplerexo" && genpresets::kindId (InstrumentKind::midiOut) == "midiOut");
    // folders: FL's plugin names (FL-native equivalents keep FL's names)
    CHECK (genpresets::folderName (InstrumentKind::dx10) == "Fruity DX10");
    CHECK (genpresets::folderName (InstrumentKind::threeosc) == "3x Osc");
    CHECK (genpresets::folderName (InstrumentKind::synth) == "Synth");
    CHECK (genpresets::folderName (InstrumentKind::plucked) == "Plucked!");
}

TEST (genpreset190fs_save_is_offered_where_the_window_has_a_preset_box)
{
    using namespace lynx;
    for (auto k : { InstrumentKind::threeosc, InstrumentKind::sytrus, InstrumentKind::harmor, InstrumentKind::gms, InstrumentKind::harmless,
                    InstrumentKind::minisynth, InstrumentKind::dx10, InstrumentKind::fruitkick, InstrumentKind::ogun, InstrumentKind::sakura,
                    InstrumentKind::synth, InstrumentKind::simsynth, InstrumentKind::toxic, InstrumentKind::poizone, InstrumentKind::drumsynth })
        CHECK (genpresets::canSave (k));
    for (auto k : { InstrumentKind::sampler, InstrumentKind::plugin, InstrumentKind::layer, InstrumentKind::patcher, InstrumentKind::audioClip })
        CHECK (! genpresets::canSave (k));
    // every kind with a LYNX bank or an FL preset folder can save (the channel window's ◀ name ▶ box)
    for (const auto k : banks::kindsWithBanks())
        CHECK (genpresets::canSave (k));
    for (const auto& e : natives::instrumentEngines())
        if (e.presetFolder[0] != 0)
            CHECK (genpresets::canSave (e.kind));
}

TEST (genpreset190fs_every_field_comes_back_bit_identical)
{
    using namespace lynx;
    ChannelDef c;
    c.instrument = InstrumentKind::directwave;
    c.samplePath = "C:\\Samples\\Pi\xc3\xa1no \\ new\nline.wav";
    std::mt19937 rng (190);
    for (int i = 0; i < 4099; ++i)
        c.pluginState.push_back ((std::uint8_t) (rng() & 0xff));
    c.pluginState[0] = 0;
    c.controllerState = { 0, 255, 10, 13, 0 };
    c.modelParams = { 0.0f, -0.0f, 1e-45f, -3.4e38f, 0.1f, 1.0f / 3.0f, std::numeric_limits<float>::infinity(), 123456.789f, -7.5f };
    for (int i = 0; i < 300; ++i)
    {
        std::uint32_t bits = (std::uint32_t) rng();
        if (((bits >> 23) & 0xff) == 0xff)
            bits &= ~(1u << 30); // (no NaN: NaN != NaN for the comparison)
        float f;
        std::memcpy (&f, &bits, 4);
        c.modelParams.push_back (f);
    }
    SampleZone z;
    z.path = "/home/user/My Samples/K\xc3\xb6rper kick 1.wav";
    z.rootKey = 36, z.lowKey = 30, z.highKey = 40, z.lowVelocity = 10, z.highVelocity = 120, z.tune = -0.37f, z.gain = 0.71f;
    z.loopStart = 1234567890123LL, z.loopEnd = 1234567899999LL, z.loopMode = 2;
    c.zones = { z, SampleZone {} };
    c.keyLabels = { { 36, "Kick \\ big" }, { 38, "Snare\nrim" }, { 127, "" } };
    // a synth with every table used, including negative zero
    for (int h = 0; h < SynthSettings::spectrumSize; ++h)
    {
        c.synth.spectrum[(std::size_t) h] = 1.0f / (float) (h + 1);
        c.synth.spectrumPhase[(std::size_t) h] = h == 3 ? -0.0f : 0.0f;
        c.synth.spectrumDecay[(std::size_t) h] = 0.01f * (float) h;
        c.synth.spectrumTarget[(std::size_t) h] = h % 2 == 0 ? 0.25f : 0.0f;
    }
    c.synth.osc[2].wave = Waveform::spectral;
    c.synth.lfo[lfoPitch].amount = 0.3f;
    c.synth.lfo[lfoPitch].sync = true;
    c.synth.lfo[lfoPitch].secondsTiming = true;
    c.synth.filterEnv.shape = 1;
    c.synth.pitchEnv.hold = 0.123f;
    c.synth.fm = 2.5f, c.synth.fmSustain = 0.2f, c.synth.ring = true, c.synth.am = true, c.synth.bandGain = { 0.5f, 1.5f, 2.0f };
    const std::string name = "Bell \\ \"FM\"\nnew line \xe2\x98\x85";
    ChannelDef loaded;
    CHECK (genpreset190fs::roundTrip (c, name, &loaded));
    CHECK (std::memcmp (loaded.modelParams.data(), c.modelParams.data(), c.modelParams.size() * 4) == 0);
    CHECK (std::signbit (loaded.synth.spectrumPhase[3]) && std::signbit (loaded.modelParams[1]));
    CHECK (loaded.zones == c.zones && loaded.keyLabels == c.keyLabels && loaded.pluginState == c.pluginState);
    // a preset of nothing but the generator (an empty state) round-trips too
    ChannelDef empty;
    empty.instrument = InstrumentKind::dx10;
    CHECK (genpreset190fs::roundTrip (empty, "Init"));
    // Windows line ends (a file edited by hand) read the same
    auto text = genpresets::toText (name, c);
    std::string crlf;
    for (const auto ch : text)
        crlf += ch == '\n' ? std::string ("\r\n") : std::string (1, ch);
    const auto fromCrlf = genpresets::fromText (crlf);
    CHECK (fromCrlf.has_value() && genpresets::sameSound (fromCrlf->sound, c));
}

TEST (genpreset190fs_lynx_banks_round_trip_and_null_test)
{
    // every LYNX bank preset of every engine with a bank (DX10, Harmless, SimSynth, Toxic, PoiZone, Fruit Kick, Plucked!,
    // Drumpad, Sakura, Drumaxx, DrumSynth Live, BeepMap, Granulizer): bit-identical state; the first presets of each
    // bank also render bit-identical before and after (the null test)
    using namespace lynx;
    int presets = 0, rendered = 0, audible = 0;
    for (const auto kind : banks::kindsWithBanks())
    {
        const auto base = banks::defaultChannel (kind, genpresets::engineName (kind));
        CHECK (base.has_value());
        if (! base)
            continue;
        const auto& bank = banks::bankOf (kind);
        for (int i = 0; i < (int) bank.size(); ++i)
        {
            const auto c = banks::withPreset (*base, i);
            CHECK (c.has_value());
            if (! c)
                continue;
            ChannelDef loaded;
            const auto ok = genpreset190fs::roundTrip (*c, bank[(std::size_t) i].name, &loaded);
            CHECK (ok);
            ++presets;
            if (ok && i < 3 && kind != InstrumentKind::granulizer) // (Granulizer plays the channel's sample: none here)
            {
                const auto before = genpreset190fs::render (*c), after = genpreset190fs::render (loaded);
                CHECK (genpreset190fs::sameBits (before, after));
                if (! genpreset190fs::sameBits (before, after))
                    std::printf ("  null test failed: %s / %s\n", genpresets::kindId (kind).c_str(), bank[(std::size_t) i].name.c_str());
                ++rendered;
                audible += genpreset190fs::peak (before) > 1e-4 ? 1 : 0;
            }
        }
    }
    std::printf ("  bank presets round-tripped: %d, rendered: %d (audible %d)\n", presets, rendered, audible);
    CHECK (presets > 100);
    CHECK (rendered >= 30 && audible >= rendered * 3 / 4); // (the null test compares sound, not two silences)
}

TEST (genpreset190fs_native_engines_round_trip_and_null_test)
{
    // every FL engine LYNX adds: from LYNX's own default (3x Osc, Harmless, DX10, SimSynth, Plucked!, Slicex, FPC...) and,
    // with FL Studio's data, from FL's Default preset (Sytrus, Harmor, GMS, Ogun, MiniSynth, Sakura, Morphine...)
    using namespace lynx;
    const auto fl = natives142::flAccess();
    int engines = 0, rendered = 0;
    for (const auto& e : natives::instrumentEngines())
    {
        const auto c = natives::makeChannel (e.kind, fl);
        if (! c)
            continue; // (needs FL Studio, not installed here)
        ChannelDef loaded;
        const auto ok = genpreset190fs::roundTrip (*c, std::string (e.name) + " (mine)", &loaded);
        CHECK (ok);
        if (! ok)
            std::printf ("  round trip failed: %s\n", e.name);
        ++engines;
        if (ok && e.kind != InstrumentKind::slicex && e.kind != InstrumentKind::fpc && e.kind != InstrumentKind::granulizer
            && e.kind != InstrumentKind::fruityslicer && e.kind != InstrumentKind::wavetraveller) // (sample players: nothing loaded)
        {
            const auto before = genpreset190fs::render (*c), after = genpreset190fs::render (loaded);
            CHECK (genpreset190fs::sameBits (before, after));
            if (! genpreset190fs::sameBits (before, after))
                std::printf ("  null test failed: %s\n", e.name);
            ++rendered;
        }
    }
    std::printf ("  native engines round-tripped: %d, rendered: %d%s\n", engines, rendered, fl.roots.data.empty() ? " (without FL Studio)" : "");
    CHECK (engines >= 8 && rendered >= 5);
}

TEST (genpreset190fs_lynx_synth_presets_round_trip_and_null_test)
{
    using namespace lynx;
    int n = 0;
    for (const auto& [name, settings] : synthPresets())
    {
        ChannelDef c;
        c.instrument = InstrumentKind::synth;
        c.synth = settings;
        ChannelDef loaded;
        CHECK (genpreset190fs::roundTrip (c, name, &loaded));
        CHECK (loaded.synth == settings);
        if (n++ < 4)
        {
            const auto before = genpreset190fs::render (c);
            CHECK (genpreset190fs::sameBits (before, genpreset190fs::render (loaded)) && genpreset190fs::peak (before) > 1e-4);
        }
    }
    CHECK (n >= 4);
}

TEST (genpreset190fs_other_files_are_told_apart)
{
    using namespace lynx;
    ChannelDef c;
    c.instrument = InstrumentKind::dx10;
    c.pluginState = { 9, 8, 7 };
    const auto text = genpresets::toText ("Mine", c);
    CHECK (genpresets::looksLikePreset (text) && genpresets::looksLikePreset (text.substr (0, 30)));
    // a hosted plugin's .lynxpreset and a mixer effect's are not generator presets, and those readers skip ours
    pluginpresets::Preset pp;
    pp.name = "X", pp.pluginName = "Surge XT", pp.pluginId = "VST3-Surge", pp.state = { 1, 2 };
    const auto pluginText = pluginpresets::toText (pp);
    CHECK (! genpresets::fromText (pluginText).has_value() && ! genpresets::looksLikePreset (pluginText));
    CHECK (! pluginpresets::fromText (text).has_value());
    const auto effectText = presets::toText (EffectKind::reverb, "Hall", makeEffectSlot (EffectKind::reverb).params, nullptr);
    CHECK (! genpresets::fromText (effectText).has_value());
    CHECK (! presets::fromText (EffectKind::reverb, text).has_value());
    // damaged or unknown files
    CHECK (! genpresets::fromText ("").has_value());
    CHECK (! genpresets::fromText (std::string (genpresets::header) + "X\nname: a\ngenerator: dx10\n").has_value());
    CHECK (! genpresets::fromText (std::string (genpresets::header) + "\nname: a\n").has_value());            // (no generator)
    CHECK (! genpresets::fromText (std::string (genpresets::header) + "\ngenerator: wobble9000\n").has_value()); // (unknown one)
    CHECK (! genpresets::fromText (std::string (genpresets::header) + "\ngenerator: dx10\nmodelParams: 3f80000\n").has_value());
    CHECK (! genpresets::fromText (std::string (genpresets::header) + "\ngenerator: dx10\npluginState: @@@\n").has_value());
    CHECK (! genpresets::fromText (std::string (genpresets::header) + "\ngenerator: synth\nsynth: 1 2 3\n").has_value());
    // keys of a newer version are skipped
    const auto newer = genpresets::fromText (text + "futureThing: 42\n");
    CHECK (newer.has_value() && genpresets::sameSound (newer->sound, c));
    // a preset of one generator does not load into another
    const auto p = genpresets::fromText (text);
    ChannelDef sytrus;
    sytrus.instrument = InstrumentKind::sytrus;
    CHECK (p.has_value() && ! genpresets::withPreset (sytrus, *p).has_value());
    // file names: path separators and reserved characters go
    CHECK (genpresets::fileNameFor ("Bass / Lead: \"big\"") == "Bass _ Lead_ _big_");
}

TEST (genpreset190fs_load_is_one_step_and_browser_channel)
{
    // the load is a single change of the song (one undo step: the history compares two songs), and the Browser's new
    // channel has the preset's generator, sound and name
    using namespace lynx;
    const auto dx = banks::defaultChannel (InstrumentKind::dx10, "Fruity DX10");
    CHECK (dx.has_value());
    if (! dx)
        return;
    const auto bell = banks::withPreset (*dx, 3);
    CHECK (bell.has_value());
    const auto p = genpresets::fromText (genpresets::toText ("My Bell", *bell));
    CHECK (p.has_value());
    if (! p || ! bell)
        return;
    Song before;
    before.channels.push_back (*dx);
    before.channels.back().mixerTrack = 2;
    auto after = before;
    after.channels[0] = *genpresets::withPreset (after.channels[0], *p);
    CHECK (describeChangeText (before, after) != describeChangeText (before, before));
    CHECK (genpresets::sameSound (after.channels[0], *bell) && after.channels[0].mixerTrack == 2 && after.channels[0].name == "My Bell");
    const auto fresh = genpresets::newChannel (*p);
    CHECK (fresh.instrument == InstrumentKind::dx10 && fresh.name == "My Bell" && genpresets::sameSound (fresh, *bell));
    CHECK (genpreset190fs::sameBits (genpreset190fs::render (fresh), genpreset190fs::render (*bell)));
}

TEST (genpreset190fs_fl_preset_of_a_third_party_plugin_names_it)
{
    // the Browser's message for an FL .fst of another company's plugin names that plugin (FL's wrapper record)
    using namespace lynx;
    ForeignPlugin f;
    f.format = ForeignPlugin::Format::vst2;
    f.vst2Id = pluginpresets::fourCC ("Syn1");
    f.name = "Some Synth";
    f.state = { 1, 2, 3, 4 };
    flp::Project project;
    flp::Channel c;
    c.type = flp::ChannelType::instrument;
    c.name = "Some Synth";
    c.pluginName = "Fruity Wrapper";
    c.pluginState = flpexport::writeWrapper (f, true);
    project.channels.push_back (c);
    const auto bytes = flp::writeProject (project);
    ForeignPlugin read;
    CHECK (foreignFromFlPreset (bytes, read));
    CHECK (read.name == "Some Synth" && read.vst2Id == f.vst2Id && read.format == ForeignPlugin::Format::vst2);
    // FL's own plugins' presets and other files are not third-party presets
    project.channels[0].pluginName = "3x Osc";
    project.channels[0].pluginState = { 1, 0, 0, 0 };
    CHECK (! foreignFromFlPreset (flp::writeProject (project), read));
    CHECK (! foreignFromFlPreset ({ 'n', 'o', 'p', 'e' }, read));
    CHECK (! foreignFromFlPreset ({}, read));
}
