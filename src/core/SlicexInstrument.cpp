#include "core/SlicexInstrument.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

#include "core/FlResampler.h"
#include "core/FlSettings.h"
#include "core/HostSlide.h"
#include "core/SlicexEngine.h"

namespace lynx
{
namespace slicex
{
const std::string& deckBPath (const ChannelDef& channel) noexcept
{
    static const std::string none;
    return channel.zones.empty() ? none : channel.zones.front().path;
}

double sampleBeats (const SampleData& sample) noexcept
{
    if (sample.frames() == 0)
        return 0.0;
    const auto seconds = (double) sample.frames() / (sample.sampleRate > 0.0 ? sample.sampleRate : 44100.0);
    return sample.acidBeats > 0.0f ? (double) sample.acidBeats : guessBeats (seconds);
}

void setDeckBPath (ChannelDef& channel, const std::string& path)
{
    channel.zones.clear();
    if (! path.empty())
    {
        SampleZone zone;
        zone.path = path;
        channel.zones.push_back (std::move (zone));
    }
}

bool importChannel (ChannelDef& def, const std::vector<std::uint8_t>& bytes,
                    const std::function<std::string (const std::string&)>& existing,
                    const std::function<std::string (const std::vector<std::uint8_t>&, const std::string&)>& saveEmbedded)
{
    const auto state = parse (bytes);
    if (! state)
        return false;
    auto locate = [&] (const Deck& deck, const std::string& name) {
        std::string path;
        if (! deck.wav.empty() && saveEmbedded) // audio that only exists inside the project
            path = saveEmbedded (deck.wav, name);
        if (path.empty() && ! deck.path.empty() && existing)
            path = existing (deck.path);
        return path;
    };
    const auto a = locate (state->decks[0], def.name);
    const auto b = locate (state->decks[1], def.name + " (deck B)");
    if (a.empty() && b.empty())
        return false;
    def.instrument = InstrumentKind::slicex;
    def.pluginState = bytes;
    def.samplePath = a;
    setDeckBPath (def, b);
    return true;
}
} // namespace slicex

namespace
{
// A deck for the engine: the sample's frames (kept alive by the sample) and its regions. A deck whose WAV is in the
// state has FL's regions (cue points with their lengths, names and keys); a deck that names a file has the cue points
// the sample loader read from it.
slicex::DeckAudio deckAudio (const std::shared_ptr<const SampleData>& sample, const slicex::Deck& deck)
{
    slicex::DeckAudio a;
    if (sample == nullptr || sample->frames() == 0)
        return a;
    a.owner = sample;
    a.left = sample->leftData();
    a.right = sample->isStereo() ? sample->rightData() : nullptr;
    a.frames = (std::int64_t) sample->frames();
    a.sampleRate = sample->sampleRate > 0.0 ? sample->sampleRate : 44100.0;
    a.beats = slicex::sampleBeats (*sample);
    std::int64_t frames = 0;
    if (! deck.wav.empty())
        a.regions = slicex::wavRegions (deck.wav, frames);
    if (a.regions.empty() || frames != a.frames) // (not the state's own WAV: the file's cue points)
        a.regions = slicex::regionsFromMarkers (sample->markerStarts, a.frames);
    return a;
}

// The channel's Slicex. configure() (UI thread) parses the state, builds the program and hands it over; the audio
// thread takes it at its next note or render and gives the old one back to be freed on the UI thread (as Wave
// Traveller does).
class SlicexInstrument final : public Instrument
{
public:
    SlicexInstrument (std::shared_ptr<const SampleData> a, std::shared_ptr<const SampleData> b)
        : samples { std::move (a), std::move (b) }
    {
        engine.setTempo (140.0f);
        for (auto& n : notes)
            n.id = -1;
    }

    ~SlicexInstrument() override
    {
        delete incoming.exchange (nullptr);
        delete retired.exchange (nullptr);
    }

    void prepare (double sr) noexcept override
    {
        take();
        engine.killAll();
        // FL tells the plugin when it renders (the sample-rate message's flag) and reads the samples with its render
        // interpolation then (REA_rodada190h Part 3)
        const auto rendering = offlineRendering();
        sampleRate = sr > 0.0 ? sr : 44100.0;
        engine.setRate ((int) std::lround (sr), rendering);
        // (playback: Settings › Audio › "Resampling quality", round 190bx; FL tells its plugins the method when it
        // changes, FUN_01366a70. Here it is taken when the engine is prepared.)
        engine.setInterpolation (rendering ? flrs::renderQuality : flsettings::options().interpolation.load (std::memory_order_relaxed));
    }

    void configure (const ChannelDef& channel, double bpm) noexcept override
    {
        delete retired.exchange (nullptr);
        pitchCents.store (channel.sampler.pitch * 100.0f);
        const auto tempo = (float) (bpm > 0.0 ? bpm : 140.0);
        if (configured && tempo == lastTempo && channel.pluginState == lastState)
            return;
        auto s = std::make_unique<Setup>();
        s->tempo = tempo;
        if (! configured || channel.pluginState != lastState)
        {
            auto state = slicex::parse (channel.pluginState);
            if (! state)
                state = slicex::fresh();
            s->velocityToVolume = state->optionByte (slicex::oVelocityToVolume) != 0;
            s->program = slicex::makeProgram (*state, deckAudio (samples[0], state->decks[0]), deckAudio (samples[1], state->decks[1]));
            s->newProgram = true;
            lastVelocityToVolume = s->velocityToVolume;
        }
        else
            s->velocityToVolume = lastVelocityToVolume;
        lastState = channel.pluginState, lastTempo = tempo, configured = true;
        // (a set-up the audio thread has not taken yet: its program, if it had one, must not be lost)
        if (auto* waiting = incoming.exchange (nullptr))
        {
            if (! s->newProgram && waiting->newProgram)
            {
                s->program = std::move (waiting->program);
                s->newProgram = true;
            }
            delete waiting;
        }
        incoming.store (s.release());
    }

    void settleNew() override { take(); }

    void setNextNoteExpression (float pan, float cents, bool) noexcept override
    {
        nextPan = pan;
        nextCents = cents;
    }

    void setNextNoteModulation (float modX, float modY) noexcept override
    {
        nextModX = std::clamp ((modX - 0.5f) * 2.0f, -1.0f, 1.0f);
        nextModY = std::clamp ((modY - 0.5f) * 2.0f, -1.0f, 1.0f);
    }

    void setNextNoteColour (int colour) noexcept override { nextColour = colour & 15; }

    void setSongPosition (double ticks) noexcept override
    {
        songTicks = ticks;
        sinceBlock = 0;
    }

    void noteOn (int key, float velocity) noexcept override
    {
        take();
        const auto vel = std::clamp ((double) velocity, 0.0, 1.0);
        // FL's host hands the plugin the note's level (Init.Vol); "Link velocity to volume" off: every note at 100 %
        const auto initVol = velocityToVolume ? (float) ((std::pow (11.0, 1.28 * vel) - 1.0) / 10.0) : 1.0f;
        const auto k = std::clamp (key, 0, 127);
        slicex::Note n;
        n.key = k;
        n.velocity = (float) vel;
        n.colour = nextColour;
        n.vol = (float) (channelVol * (double) initVol);
        n.pan = (float) std::clamp ((double) nextPan + channelPan, -1.0, 1.0);
        n.pitchCents = (float) (100.0 * (k - 60) + (double) pitchCents.load() + (double) nextCents);
        n.modX = nextModX;
        n.modY = nextModY;
        // where the song is at this note (the block's position and what was rendered since): the LFOs with a global
        // phase and the "song time" layering
        const auto beats = std::max (0.0, songTicks / (double) ppq + (double) sinceBlock * (double) tempo / (60.0 * sampleRate));
        engine.setSongPosition (beats, beats * 60.0 / (double) tempo);
        const auto id = engine.trigger (n);
        if (id >= 0)
        {
            auto& slot = freeSlot();
            slot.id = id;
            slot.key = k;
            slot.noteId = startingNoteId();
            slot.volFactor = initVol;
            slot.pan = nextPan;
            slot.cents = nextCents;
            // (FL's channel porta: the voice glides from the previous note's pitch, HostSlide::pendingGlide)
            slot.slide.anchor (NAN);
        }
        nextPan = 0.0f, nextCents = 0.0f, nextModX = 0.0f, nextModY = 0.0f, nextColour = 0;
    }

    void noteOff (int key) noexcept override
    {
        // (round 190ej: the note's own voice — FL hands Slicex the ending note's voice, Voice_Release 0x883970 releases that
        // voice only: REA_rodada190h §6 / VMT slot 10)
        for (auto& n : notes)
            if (n.id >= 0 && endsVoice (n.key, n.noteId, key) && ! n.released)
            {
                engine.release (n.id);
                n.released = true;
            }
    }

    // Slide notes and the channel's porta (#157). Slicex reads its voices' Final.Pitch from the host at every render
    // call and adds it to the region's pitch (REA_rodada190h Part 3, Gen_Render 0x880740: cents = region pitch +
    // (60 − key) · 100 + FinalLevels.Pitch + master pitch), so FL's slide notes and porta, which move that pitch, bend
    // a sounding slice; its key (the region it plays) stays the note's own (round 190hg, REA_rodada190hg §1).
    // FL's renders of slide notes on Slicex (§1.1): the host moves the pitch once per tick of the project (1/96 beat), in
    // n + 1 equal steps for a slide n ticks long, the first one at the slide's start, the last one at its end.
    bool followsChannelPorta() const noexcept override { return true; }
    void slideTo (int key, float cents, float samples) noexcept override
    {
        const auto to = 100.0 * (std::clamp (key, 0, 127) - 60) + (double) pitchCents.load() + (double) cents;
        const auto tick = sampleRate * 60.0 / ((double) std::max (1.0f, tempo) * (double) flTicksPerBeat);
        const auto ticks = std::max (1, (int) std::lround ((double) samples / tick));
        // the steps fall on the song's tick grid, each at the frame nearest to its tick (FL's renders: §1.1)
        const auto now = songTicks / (double) ppq * 60.0 / (double) std::max (1.0f, tempo) * sampleRate + (double) sinceBlock;
        const auto firstTick = std::llround (now / tick);
        const auto nowFrame = std::llround (now);
        for (auto& n : notes)
            if (n.id >= 0 && ! n.released)
            {
                auto& st = n.stepped;
                st.from = st.cents;
                st.to = (float) (to - (100.0 * (n.key - 60) + (double) pitchCents.load() + (double) n.cents)) - n.slide.target; // (the porta glide ends at its target)
                st.steps = ticks + 1;
                st.done = 0;
                st.tick = tick;
                st.firstTick = firstTick;
                st.startFrame = nowFrame;
                st.sinceStart = 0.0;
            }
    }

    void allNotesOff() noexcept override
    {
        engine.releaseAll();
        for (auto& n : notes)
            if (n.id >= 0)
                n.released = true;
    }

    void panic() noexcept override
    {
        engine.killAll();
        for (auto& n : notes)
            n.id = -1;
    }

    bool idle() const noexcept override { return engine.idle(); }
    // the editor's play line (round 190jd)
    slicex::Engine::PlayMark playMark() const noexcept { return engine.playMark(); }

    void render (float* left, float* right, int n, float gl, float gr) noexcept override
    {
        take();
        // FL's channel volume / pan from LYNX's gains (as Wave Traveller, the Granulizer and 3x Osc)
        const auto vol = std::sqrt (((double) gl * gl + (double) gr * gr) * 0.5);
        auto pan = vol > 0.0 ? std::atan2 ((double) gr, (double) gl) * (4.0 / 3.141592653589793) - 1.0 : 0.0;
        if (std::fabs (pan) < 1e-9)
            pan = 0.0;
        const auto pitch = (double) pitchCents.load();
        if (vol != channelVol || pan != channelPan || pitch != channelPitch)
        {
            channelVol = vol, channelPan = pan, channelPitch = pitch;
            for (auto& note : notes)
                if (note.id >= 0)
                {
                    if (! engine.noteActive (note.id))
                    {
                        note.id = -1;
                        continue;
                    }
                    setLevels (note);
                }
        }
        sinceBlock += n;
        if (engine.idle())
        {
            engine.clearPlayMark();
            return;
        }
        if (! std::any_of (notes.begin(), notes.end(), [] (const Held& h) { return h.id >= 0 && (h.slide.moving() || h.stepped.active()); }))
        {
            engine.render (left, right, n);
            return;
        }
        // a slide or porta glide is moving: the host's Final.Pitch reaches the voices before the frames it covers (a
        // render call of FL's own) — a slide note's next step at its tick, a porta glide every slideChunk frames
        for (int done = 0; done < n;)
        {
            int len = std::min (slideChunk, n - done);
            for (auto& note : notes)
                if (note.id >= 0 && note.stepped.active())
                {
                    if (note.stepped.due())
                    {
                        note.stepped.step();
                        setLevels (note);
                    }
                    if (note.stepped.active())
                        len = std::min (len, std::max (1, note.stepped.untilNext()));
                }
            for (auto& note : notes)
                if (note.id >= 0 && note.slide.moving())
                {
                    note.slide.advance ((float) len);
                    setLevels (note);
                }
            engine.render (left + done, right + done, len);
            for (auto& note : notes)
                if (note.id >= 0 && note.stepped.active())
                    note.stepped.sinceStart += len;
            done += len;
        }
    }

private:
    struct Setup
    {
        std::shared_ptr<slicex::Program> program;
        bool newProgram = false;
        bool velocityToVolume = true;
        float tempo = 140.0f;
    };

    struct Held
    {
        int id = -1, key = 60;
        bool released = false;
        float volFactor = 1.0f, pan = 0.0f, cents = 0.0f;
        std::uint32_t noteId = 0; // the LYNX note that started it (round 190ej)
        HostSlide slide {};       // the channel's porta moving the note's Final.Pitch (round 190hg)
        // a slide note moving it: FL's host steps it once per tick (round 190hg, REA_rodada190hg §1.1)
        struct Stepped
        {
            float from = 0.0f, to = 0.0f, cents = 0.0f; // (cents from the note's own pitch)
            int steps = 0, done = 0;                    // n + 1 steps for a slide of n ticks; how many are applied
            double tick = 1.0, sinceStart = 0.0;        // samples a tick, samples since the slide started
            std::int64_t firstTick = 0, startFrame = 0; // the song's tick of the first step, the song's frame at the start
            bool active() const noexcept { return done < steps; }
            // the frame of step k from the slide's start: tick firstTick + k of the song, to the nearest frame
            double at (int k) const noexcept { return (double) (std::llround ((double) (firstTick + k) * tick) - startFrame); }
            bool due() const noexcept { return active() && sinceStart >= at (done); }
            int untilNext() const noexcept { return (int) std::ceil (at (done) - sinceStart); }
            void step() noexcept
            {
                ++done;
                cents = from + (to - from) * (float) done / (float) steps;
            }
        } stepped {};
    };

    static constexpr int slideChunk = 256;
    static constexpr int flTicksPerBeat = 96; // (FL's default project PPQ; the slide steps run on its ticks)

    void setLevels (const Held& note) noexcept
    {
        engine.setNoteLevels (note.id, (float) (channelVol * (double) note.volFactor),
                              (float) std::clamp ((double) note.pan + channelPan, -1.0, 1.0),
                              (float) (100.0 * (note.key - 60) + channelPitch + (double) note.cents + (double) note.slide.cents
                                       + (double) note.stepped.cents));
    }

    void take() noexcept
    {
        if (retired.load() != nullptr)
            return;
        if (auto* s = incoming.exchange (nullptr))
        {
            if (s->newProgram)
            {
                s->program = engine.setProgram (std::move (s->program)); // the old program leaves with `s`, freed on the UI thread
                for (auto& n : notes)
                    n.id = -1;
            }
            velocityToVolume = s->velocityToVolume;
            tempo = s->tempo > 0.0f ? s->tempo : 140.0f;
            engine.setTempo (s->tempo);
            retired.store (s);
        }
    }

    Held& freeSlot() noexcept
    {
        for (auto& n : notes)
            if (n.id < 0)
                return n = Held {};
        for (auto& n : notes)
            if (! engine.noteActive (n.id))
                return n = Held {};
        return notes[0] = Held {}; // (every slot holds a sounding note: the oldest slot is given up; its voices play on)
    }

    slicex::Engine engine;
    std::array<std::shared_ptr<const SampleData>, 2> samples;
    std::atomic<Setup*> incoming { nullptr }, retired { nullptr };
    std::atomic<float> pitchCents { 0.0f };
    // (UI thread)
    std::vector<std::uint8_t> lastState;
    float lastTempo = -1.0f;
    bool configured = false, lastVelocityToVolume = true;
    // (audio thread)
    std::array<Held, slicex::Engine::maxVoices> notes {};
    bool velocityToVolume = true;
    float tempo = 140.0f;
    double sampleRate = 44100.0, songTicks = 0.0;
    std::int64_t sinceBlock = 0;
    double channelVol = 1.0, channelPan = 0.0, channelPitch = 0.0;
    float nextPan = 0.0f, nextCents = 0.0f, nextModX = 0.0f, nextModY = 0.0f;
    int nextColour = 0;
};
} // namespace

std::unique_ptr<Instrument> makeSlicexInstrument (std::shared_ptr<const SampleData> deckA, std::shared_ptr<const SampleData> deckB)
{
    return std::make_unique<SlicexInstrument> (std::move (deckA), std::move (deckB));
}

namespace slicex
{
PlayLine playLine (const Instrument* instrument) noexcept
{
    const auto* x = dynamic_cast<const SlicexInstrument*> (instrument);
    if (x == nullptr)
        return {};
    const auto m = x->playMark();
    return { m.deck, m.frame };
}
} // namespace slicex
} // namespace lynx
