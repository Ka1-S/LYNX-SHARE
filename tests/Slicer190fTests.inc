// Round 190f, issue #129: Fruity Slicer's Envelope / instrument settings (core/FlVoiceSettings.h) and its note-off

#include "core/FlVoiceSettings.h"
#include "core/FruitySlicer.h"

namespace slicer190f
{
// a steady tone long enough for every test (one slice)
std::shared_ptr<lynx::SampleData> tone (int frames = 44100)
{
    auto d = std::make_shared<lynx::SampleData>();
    d->sampleRate = 44100.0;
    for (int i = 0; i < frames; ++i)
        d->left.push_back (0.5f * (float) std::sin (i * 0.2));
    d->right = d->left;
    return d;
}

// What a Slicer channel with these voice settings plays: the note held for `held` frames, then released
std::vector<float> play (const lynx::flvoice::Settings& voice, int held, int total)
{
    using namespace lynx;
    auto state = fslicer::fresh ("C:\\loops\\tone.wav", 44100);
    fslicer::setVoiceSettings (state, voice);
    const auto sample = tone();
    ChannelDef def;
    def.instrument = InstrumentKind::fruityslicer;
    fslicer::configureSampler (def, state, (std::int64_t) sample->frames());
    auto instrument = makeInstrument (InstrumentKind::fruityslicer, fslicer::prepare (*sample, state));
    instrument->prepare (44100.0);
    instrument->configure (def, 120.0);
    instrument->settleNew();

    std::vector<float> left ((std::size_t) total, 0.0f), right ((std::size_t) total, 0.0f);
    instrument->noteOn (60, 0.78125f);
    instrument->render (left.data(), right.data(), held, 1.0f, 1.0f);
    instrument->noteOff (60);
    for (int pos = held; pos < total; pos += 512)
        instrument->render (left.data() + pos, right.data() + pos, std::min (512, total - pos), 1.0f, 1.0f);
    return left;
}

float peak (const std::vector<float>& x, int from, int to)
{
    float p = 0.0f;
    for (int i = from; i < to && i < (int) x.size(); ++i)
        p = std::max (p, std::fabs (x[(std::size_t) i]));
    return p;
}
} // namespace slicer190f

TEST (slicer190f_voice_settings_round_trip)
{
    using namespace lynx;
    // the plugins' defaults: the envelopes off, the volume record's flag 4 and release tension −101
    const auto defaults = flvoice::read ({});
    CHECK (! defaults.articulators[flvoice::volume].envelope && defaults.articulators[flvoice::volume].flags == 4);
    CHECK (defaults.articulators[flvoice::volume].releaseTension == -101 && defaults.modX == 256 && defaults.filterType == 0);
    CHECK (defaults.articulators[flvoice::pitch].lfoSpeed == 32950 && defaults.articulators[flvoice::pan].sustain == 50);

    // a block written from its own settings is the same bytes (also the bytes the settings do not cover)
    std::vector<std::uint8_t> block (364);
    for (std::size_t i = 0; i < block.size(); ++i)
        block[i] = (std::uint8_t) (i * 7 + 3);
    auto copy = block;
    flvoice::write (copy, flvoice::read (block));
    CHECK (copy == block);

    // an edit lands in FL's record and survives the state's own round trip
    auto state = slicer180c::threeSlices();
    {
        // a new Slicer is FL's (a Fruity Slicer added in FL 2026 and saved, round 190m): ANIMATE and DECLICK on, DEC
        // 4 ms, the volume envelope on — no delay / attack / hold / decay, full sustain, release 6500 — and the rest
        // of the block the plugin's defaults
        CHECK (state.animate == 1 && state.declick == 1 && state.fadeOutMs == 4 && state.fadeInMs == 0 && state.autoDump == 1 && state.autoFit == 1);
        CHECK (state.voiceBlock.size() == 364);
        auto expected = defaults;
        auto& v = expected.articulators[flvoice::volume];
        v.envelope = true;
        v.delay = v.attack = v.hold = v.decay = 100;
        v.sustain = 128;
        v.release = 6500;
        CHECK (fslicer::voiceSettings (state) == expected);
        std::int32_t on = 0;
        std::memcpy (&on, state.voiceBlock.data() + 0x48, 4);
        CHECK (on == -1); // as FL writes it
        // a state without the block still reads as the plugin's defaults
        state.voiceBlock.clear();
        CHECK (fslicer::voiceSettings (state) == defaults);
    }
    auto voice = defaults;
    auto& pitch = voice.articulators[flvoice::pitch];
    pitch.lfoAmount = 50;
    pitch.lfoSpeed = 20000;
    pitch.lfoShape = 1;
    pitch.setFlag (0x20, true);
    auto& volume = voice.articulators[flvoice::volume];
    volume.envelope = true;
    volume.release = 30000;
    voice.modX = 128, voice.filterType = 1;
    fslicer::setVoiceSettings (state, voice);
    CHECK (state.voiceBlock.size() == 364);
    std::int32_t enabled = 0, speed = 0;
    std::memcpy (&enabled, state.voiceBlock.data() + 0x44 * 1 + 4, 4);
    std::memcpy (&speed, state.voiceBlock.data() + 0x44 * 4 + 12 * 4, 4);
    CHECK (enabled == -1 && speed == 20000); // FL's "on" is −1
    const auto back = fslicer::parse (fslicer::write (state));
    CHECK (back && fslicer::voiceSettings (*back) == voice);
    CHECK (back && fslicer::voiceSettings (*back).articulators[flvoice::pitch].lfoGlobal());
}

TEST (slicer190f_note_off_ends_the_slice)
{
    using namespace lynx;
    const int held = 4410, total = 44100;

    // the volume envelope off (a new Slicer): the note-off starts the processor's 10 ms end fade
    const auto plain = slicer190f::play (flvoice::read ({}), held, total);
    CHECK (slicer190f::peak (plain, 0, held) > 0.1f);
    CHECK (slicer190f::peak (plain, held, held + 200) > 0.01f);       // still fading
    CHECK (slicer190f::peak (plain, held + 441 + 64, total) == 0.0f); // gone 10 ms later (it played on to the end)

    // the volume envelope on: the note-off plays its release
    auto voice = flvoice::read ({});
    auto& v = voice.articulators[flvoice::volume];
    v.envelope = true;
    v.delay = v.attack = v.hold = v.decay = 100;
    v.sustain = 128;
    v.releaseTension = 0;
    v.release = 30000; // ≈ 1.45 s at 120 BPM: 0.064·(1000^(30000/65536) − 1)
    const auto slow = slicer190f::play (voice, held, total);
    const auto early = slicer190f::peak (slow, held + 2205, held + 4410), late = slicer190f::peak (slow, held + 22050, held + 24255);
    CHECK (early > 0.2f && late > 0.02f && late < early * 0.9f); // still sounding long after the 10 ms, and falling

    v.release = 6500; // FL's demo Slicers: a short release
    const auto fast = slicer190f::play (voice, held, total);
    CHECK (slicer190f::peak (fast, held + 441 + 64, held + 2205) > 0.0f); // longer than the end fade
    CHECK (slicer190f::peak (fast, held + 13230, total) == 0.0f);         // and over well within 0.3 s
}

TEST (slicer190f_lfos_reach_the_voice)
{
    using namespace lynx;
    const int held = 22050, total = 22050;
    const auto plain = slicer190f::play (flvoice::read ({}), held, total);

    // pitch LFO (vibrato): the same level, another waveform
    auto vibrato = flvoice::read ({});
    vibrato.articulators[flvoice::pitch].lfoAmount = 100; // cents
    vibrato.articulators[flvoice::pitch].lfoSpeed = 15000;
    const auto wobbling = slicer190f::play (vibrato, held, total);
    double difference = 0.0;
    for (int i = 0; i < total; ++i)
        difference = std::max (difference, (double) std::fabs (wobbling[(std::size_t) i] - plain[(std::size_t) i]));
    CHECK (difference > 0.05);
    CHECK (std::fabs (slicer190f::peak (wobbling, 0, total) - slicer190f::peak (plain, 0, total)) < 0.02f);

    // volume LFO (tremolo): the level rises and falls
    auto tremolo = flvoice::read ({});
    tremolo.articulators[flvoice::volume].lfoAmount = 100;
    tremolo.articulators[flvoice::volume].lfoSpeed = 15000;
    const auto pulsing = slicer190f::play (tremolo, held, total);
    float low = 1.0f, high = 0.0f;
    for (int from = 2205; from + 441 <= total; from += 441)
    {
        const auto p = slicer190f::peak (pulsing, from, from + 441);
        low = std::min (low, p), high = std::max (high, p);
    }
    CHECK (high > low * 2.0f);
}

TEST (slicer190f_voice_settings_survive_flp_export_and_import)
{
    using namespace lynx;
    auto state = slicer180c::threeSlices();
    auto voice = fslicer::voiceSettings (state);
    voice.articulators[flvoice::volume].envelope = true;
    voice.articulators[flvoice::volume].release = 12345;
    voice.articulators[flvoice::pitch].lfoAmount = 37;
    voice.articulators[flvoice::pan].lfoAmount = -20;
    voice.articulators[flvoice::cutoff].envelope = true;
    voice.articulators[flvoice::cutoff].amount = -64;
    voice.modX = 100, voice.modY = 30, voice.filterType = 3;
    fslicer::setVoiceSettings (state, voice);

    Song song = Song::makeDefault();
    ChannelDef slicer;
    slicer.name = "Beat";
    slicer.instrument = InstrumentKind::fruityslicer;
    slicer.samplePath = "C:\\loops\\beat.wav";
    slicer.pluginState = fslicer::write (state);
    song.channels.push_back (slicer);
    const auto index = song.channels.size() - 1;

    flpexport::Options options;
    flpexport::Report report;
    const auto project = flp::parseProject (flpexport::exportFlp (song, options, report));
    CHECK (index < project.channels.size() && project.channels[index].pluginName == "Fruity Slicer");

    FlpImportOptions importOptions;
    importOptions.fileExists = [] (const std::string&) { return true; };
    const auto back = importFlp (project, importOptions);
    CHECK (index < back.channels.size() && back.channels[index].instrument == InstrumentKind::fruityslicer);
    if (index < back.channels.size())
    {
        const auto read = fslicer::parse (back.channels[index].pluginState);
        CHECK (read && fslicer::voiceSettings (*read) == voice);

        // an edit made after the import is what the next export writes (not the imported original)
        if (read)
        {
            auto edited = *read;
            auto changed = voice;
            changed.articulators[flvoice::pitch].lfoAmount = 80;
            fslicer::setVoiceSettings (edited, changed);
            auto second = back;
            second.channels[index].pluginState = fslicer::write (edited);
            flpexport::Report again;
            const auto rewritten = flp::parseProject (flpexport::exportFlp (second, options, again));
            const auto last = index < rewritten.channels.size() ? fslicer::parse (rewritten.channels[index].pluginState) : std::nullopt;
            CHECK (last && fslicer::voiceSettings (*last) == changed);
        }
    }
}
