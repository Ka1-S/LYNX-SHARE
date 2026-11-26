// Round 190rt, issue #309 (Wanderlust063-afk: "a short note by itself plays the whole sample"): as FL's Sampler, a
// LYNX Sampler plays the whole sample (one-shot) until its volume envelope shapes the note — in LYNX the Sample page's
// Attack / Decay / Sustain / Release (Release above 0: the note's end starts the release); in FL the Envelope /
// instrument page's Volume target with "Envelope" on. The .flp export turns FL's Envelope switch on exactly then, and
// an FL sampler with its volume envelope on (LYNX's FL voice) also ends at the note's end.

namespace issue309
{
constexpr double rate = 44100.0;

inline std::shared_ptr<const lynx::SampleData> tone (double seconds)
{
    auto d = std::make_shared<lynx::SampleData>();
    d->sampleRate = rate;
    d->left.resize ((std::size_t) (seconds * rate));
    for (std::size_t i = 0; i < d->left.size(); ++i)
        d->left[i] = 0.5f * (float) std::sin (2.0 * 3.141592653589793 * 220.0 * (double) i / rate);
    return d;
}

// a 0.1 s note on a 2 s sample: the peak level in [from, to) seconds
inline float peakAfterShortNote (const lynx::ChannelDef& c, double from, double to)
{
    auto in = lynx::makeInstrument (lynx::InstrumentKind::sampler, tone (2.0));
    in->prepare (rate);
    in->configure (c, 120.0);
    in->noteOn (60, 0.8f);
    const int block = 256;
    std::vector<float> l (block), r (block);
    float peak = 0.0f;
    long long done = 0;
    bool off = false;
    while (done < (long long) (to * rate))
    {
        if (! off && done >= (long long) (0.1 * rate))
        {
            in->noteOff (60);
            off = true;
        }
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        in->render (l.data(), r.data(), block, 1.0f, 1.0f);
        for (int i = 0; i < block; ++i)
            if ((double) (done + i) >= from * rate)
                peak = std::max (peak, std::max (std::abs (l[(std::size_t) i]), std::abs (r[(std::size_t) i])));
        done += block;
    }
    return peak;
}

inline lynx::ChannelDef samplerChannel()
{
    lynx::ChannelDef c;
    c.instrument = lynx::InstrumentKind::sampler;
    c.samplePath = "tone.wav";
    return c;
}

inline flp::Channel exportedChannel (const lynx::ChannelDef& c)
{
    auto s = lynx::Song::makeDefault();
    s.channels = { c };
    lynx::flpexport::Options options;
    lynx::flpexport::Report report;
    const auto p = flp::parseProject (lynx::flpexport::exportFlp (s, options, report));
    for (const auto& ch : p.channels)
        if (ch.type == flp::ChannelType::sampler || ch.type == flp::ChannelType::instrument)
            return ch;
    return {};
}
} // namespace issue309

TEST (issue309_sampler_plays_the_whole_sample_until_its_envelope_shapes_the_note)
{
    using namespace issue309;
    // a new Sampler: Release 0 (FL: Envelope off) — the short note plays the sample on to its end
    auto c = samplerChannel();
    CHECK (c.sampler.release == 0.0f && c.sampler.attack == 0.0f && c.sampler.sustain == 1.0f);
    CHECK (peakAfterShortNote (c, 1.0, 1.5) > 0.3f);
    // Release 50 ms (FL: Envelope on, REL short): silent from 0.1 + 0.05 s on (a little headroom for the block)
    c.sampler.release = 0.05f;
    CHECK (peakAfterShortNote (c, 0.0, 0.1) > 0.3f);
    CHECK (peakAfterShortNote (c, 0.17, 1.5) < 1.0e-4f);
    // Release 0.5 s: still sounding inside the release, gone after it
    c.sampler.release = 0.5f;
    CHECK (peakAfterShortNote (c, 0.3, 0.35) > 0.05f);
    CHECK (peakAfterShortNote (c, 0.62, 1.5) < 1.0e-4f);
    // decay to a lower sustain with a release: the level falls while held, then ends
    c.sampler.decay = 0.05f;
    c.sampler.sustain = 0.25f;
    c.sampler.release = 0.05f;
    CHECK (peakAfterShortNote (c, 0.08, 0.1) < 0.2f);
    CHECK (peakAfterShortNote (c, 0.17, 1.5) < 1.0e-4f);
}

TEST (issue309_flp_export_turns_fl_volume_envelope_on_with_the_lynx_envelope)
{
    using namespace issue309;
    auto c = samplerChannel();
    const auto plain = exportedChannel (c);
    CHECK (plain.envelopes[1].present && ! plain.envelopes[1].enabled); // FL: one-shot, as LYNX
    c.sampler.release = 0.05f;
    const auto shaped = exportedChannel (c);
    CHECK (shaped.envelopes[1].present && shaped.envelopes[1].enabled); // FL: Envelope on, the note's end releases it

    // optional: the two projects for FL's /R render check (LYNX_ART309 = folder; the sample is tone.wav beside them)
    if (const char* dir = std::getenv ("LYNX_ART309"))
    {
        for (const auto release : { 0.0f, 0.05f, 0.3f })
        {
            auto s = lynx::Song::makeDefault();
            auto ch = samplerChannel();
            ch.name = "Tone";
            ch.samplePath = std::string (dir) + "/tone.wav";
            if (const char* win = std::getenv ("LYNX_ART309_WIN"))
                ch.samplePath = std::string (win) + "\\tone.wav";
            ch.sampler.release = release;
            s.channels = { ch };
            s.patterns.resize (1);
            lynx::Note n;
            n.start = 0;
            n.length = lynx::ticksPerStep; // a 1/16 note at the song's tempo
            n.channel = 0;
            n.key = 60;
            s.patterns[0].notes = { n };
            lynx::Clip clip;
            clip.pattern = 0;
            clip.track = 0;
            clip.start = 0;
            clip.length = 4 * 4 * lynx::ticksPerStep; // one bar
            s.playlist = { clip };
            lynx::flpexport::Options options;
            lynx::flpexport::Report report;
            const auto bytes = lynx::flpexport::exportFlp (s, options, report);
            const auto name = std::string (dir) + "/rel" + std::to_string ((int) std::lround (release * 1000.0f)) + ".flp";
            std::ofstream out (name, std::ios::binary);
            out.write ((const char*) bytes.data(), (std::streamsize) bytes.size());
            if (release == 0.05f)
            {
                // two FL step notes (length 0: LYNX's held steps), C5 then G5 eight steps later
                auto two = s;
                lynx::Note a = n, b = n;
                a.held = b.held = true;
                b.start = 8 * lynx::ticksPerStep;
                b.key = 67;
                two.patterns[0].notes = { a, b };
                lynx::flpexport::Report r2;
                const auto steps = lynx::flpexport::exportFlp (two, options, r2);
                std::ofstream o2 (std::string (dir) + "/rel50-2steps.flp", std::ios::binary);
                o2.write ((const char*) steps.data(), (std::streamsize) steps.size());
            }
        }
    }
}

// FL 2026 sends no note-off for a step note (length 0) on a Sampler whose volume envelope is on: it sounds past its step
// and past the channel's next notes (renders rel50 / rel50-2steps, round 190rt). So the .flp export writes a one-step LYNX
// note on such a Sampler with its length (FL then ends it where LYNX does); on a one-shot Sampler it stays a step. (The
// import of FL's own steps on such Samplers still plays them one step long: QUEUE-0.1.33, it needs a note kind without
// a note-off.)
TEST (issue309_step_notes_on_an_enveloped_sampler)
{
    using namespace issue309;
    auto songWith = [] (float release, bool held) {
        auto s = lynx::Song::makeDefault();
        auto c = samplerChannel();
        c.sampler.release = release;
        s.channels = { c };
        s.patterns.resize (1);
        lynx::Note n;
        n.length = lynx::ticksPerStep;
        n.key = 60;
        n.held = held;
        s.patterns[0].notes = { n };
        lynx::Clip clip;
        clip.length = 16 * lynx::ticksPerStep;
        s.playlist = { clip };
        return s;
    };
    auto exported = [] (const lynx::Song& s) {
        lynx::flpexport::Options options;
        lynx::flpexport::Report report;
        return flp::parseProject (lynx::flpexport::exportFlp (s, options, report));
    };
    auto firstLength = [] (const flp::Project& p) -> long long {
        for (const auto& pat : p.patterns)
            for (const auto& n : pat.notes)
                return (long long) n.length;
        return -1;
    };
    auto imported = [] (const flp::Project& p) {
        lynx::FlpImportOptions o;
        o.fileExists = [] (const std::string&) { return true; };
        return lynx::importFlp (p, o);
    };
    // a one-step note: a step on a one-shot Sampler (as before), its length on an enveloped one
    CHECK (firstLength (exported (songWith (0.0f, false))) == 0);
    CHECK (firstLength (exported (songWith (0.05f, false))) > 0);
    // and a LYNX one-step note on an enveloped Sampler comes back one step long, not held
    {
        const auto back = imported (exported (songWith (0.05f, false)));
        CHECK (! back.patterns.empty() && ! back.patterns[0].notes.empty() && ! back.patterns[0].notes[0].held
               && back.patterns[0].notes[0].length == lynx::ticksPerStep);
    }
}
