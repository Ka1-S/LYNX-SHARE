#pragma once

#include <cmath>

#include <atomic>

// Built-in instruments: synthesized drums and synth, and a sampler. They let the
// DAW make sound before plugin hosting exists and play imported projects.
// Everything has fixed capacity: noteOn/render never allocate.

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/Song.h"

namespace lynx
{
struct LiveInput;
struct HardwareOutput;
}

namespace lynx
{
namespace vfx
{
class Processor;
}

// Gains a sampler's voices take from outside, per sample (round 190z: FPC's pad envelopes, layer pan and FPC's choke,
// which FL steps on the voice's own tick grid). `voice` is the sampler's voice slot (0…15). Audio thread, no
// allocation, no lock.
class VoiceShaper
{
public:
    virtual ~VoiceShaper() = default;
    virtual void voiceStarted (int voice) noexcept = 0;  // a note started in the slot (a stolen slot starts again)
    virtual void voiceReleased (int voice) noexcept = 0; // its note ended
    // the left / right gain of the voice's next sample; false: the voice is over (it stops before this sample)
    virtual bool voiceGains (int voice, float& left, float& right) noexcept = 0;
};

class Instrument
{
public:
    virtual ~Instrument() = default;

    virtual void prepare (double sampleRate) noexcept = 0;
    virtual void noteOn (int key, float velocity) noexcept = 0;
    // Starts the note already `startSeconds` in (only meaningful for samples; others ignore it).
    virtual void noteOnAt (int key, float velocity, double /*startSeconds*/) noexcept { noteOn (key, velocity); }
    virtual void noteOff (int key) noexcept = 0;
    virtual void allNotesOff() noexcept = 0;
    // A cut group's "cut" (another note of the group starts): the sound stops; a Patcher's VFX nodes keep the notes
    // they hold, as FL's do (round 165d: FL's Arpeggiator script on a "cut itself" Patcher keeps arpeggiating a chord
    // whose notes start apart [EXEC: FL render, REA_rodada165d §4])
    virtual void cut() noexcept { allNotesOff(); }
    // FL's Options › Project › "Fast declick for cut groups" (REA round 167h §3): a cut voice fades out in 2 ms
    virtual void setFastDeclick (bool /*fast*/) noexcept {}
    // the project's panning law (Song::panTriangular, FL's Options › Project › Panning law; REA round 167j §1)
    virtual void setPanTriangular (bool /*triangular*/) noexcept {}
    // Song::flPluginLevels (round 190ad): a hosted plugin sends its notes' velocities as FL's wrapper does
    virtual void setFlWrapperVelocity (bool /*on*/) noexcept {}
    // A note on a given MIDI channel (0…15), from a MIDI Out channel routed to this one's MIDI input port. Only
    // hosted plugins see the channel; the others play it as a plain note.
    virtual void noteOnMidi (int key, float velocity, int /*midiChannel*/) noexcept { noteOn (key, velocity); }
    virtual void noteOffMidi (int key, int /*midiChannel*/) noexcept { noteOff (key); }
    // what arrives on MIDI port `port` (a MIDI Out on that port): a plugin takes it as noteOnMidi; a Patcher sends it
    // to the nodes cabled to its "MIDI port N" pin (round 162)
    virtual void noteOnPort (int /*port*/, int key, float velocity, int midiChannel) noexcept { noteOnMidi (key, velocity, midiChannel); }
    virtual void noteOffPort (int /*port*/, int key, int midiChannel) noexcept { noteOffMidi (key, midiChannel); }
    // A short MIDI message (program change, bank select…) for the next block. Only hosted plugins take it.
    virtual void midiMessage (const std::uint8_t* /*bytes*/, int /*size*/) noexcept {}
    // A MIDI controller's pitch wheel on this channel (round 162, issue #9), already scaled by the channel's pitch
    // range (FL's Misc › Pitch range, ±2 semitones by default): every sounding and new note is bent by `semitones`
    // until the wheel moves again. Audio thread. Hosted plugins get the raw MIDI pitch bend instead (midiMessage).
    // Round 173d: FL's native generators have no wheel input of their own; the wheel moves the channel's pitch (the
    // voice's Final.Pitch, REA_rodada164e / 167a) and each generator follows it as it follows the PITCH knob (its own
    // "Bend range" control does not scale it: FL renders bit-identical with it at max). So by default the rack keeps
    // the wheel here (wheelCents) and adds it to the channel's pitch; engines with a wheel of their own override this.
    virtual void pitchBend (float semitones) noexcept { wheel = std::isfinite (semitones) ? semitones * 100.0f : 0.0f; }
    // the wheel the rack adds to this channel's pitch (cents; 0 for an engine that bends by itself)
    float wheelCents() const noexcept { return wheel; }
    // The channel's pitch as it stands now, in cents: its PITCH knob + fine tune + the main pitch (issue #14). Audio
    // thread, before every render. Native engines take these at note on (configure / setNextNoteExpression) and ignore
    // it; a hosted plugin turns it into MIDI pitch bend (FL's wrapper: pitch bend range).
    virtual void setLivePitch (float /*cents*/) noexcept {}
    // The same, split: `knob` = the PITCH knob (cents, what configure() gave the engine as the channel's pitch), `rest` =
    // fine tune + main pitch (what setNextNoteExpression adds to each note). FL's generators follow both on their
    // sounding notes (round 167a, REA_rodada167a §1: a +200 knob or main pitch move retunes a held note at once); the
    // native engines that do override this, the others (and hosted plugins) get setLivePitch (knob + rest).
    virtual void setChannelPitchNow (float knob, float rest) noexcept { setLivePitch (knob + rest); }
    // A controller (CC 0…127, `value` 0..1) played live into a native instrument (the mod wheel is CC 1). Ignored by
    // engines without MIDI controllers; hosted plugins get the raw message (midiMessage).
    virtual void controlChange (int /*cc*/, float /*value*/) noexcept {}
    // UI thread, before the instrument reaches the audio thread: a sampler whose sample is stretched keeping its pitch
    // allocates its per-voice pitch shifters (keys transpose it without changing its length, like FL's Stretch modes).
    virtual void prepareKeyShift (double /*sampleRate*/) {}

    // The channel's pan (−1..1) and linear volume behind the gains of the next render() calls. Audio thread, before
    // every chunk. For an instrument that pans its parts around the channel's pan (FPC's pads, round 190q).
    virtual void setChannelLevels (float /*pan*/, float /*volume*/) noexcept {}
    // The sampler's voices take their gains from `shaper` too (null = none; the shaper outlives the instrument's use
    // of it). Before the first note. Other instruments ignore it.
    virtual void setVoiceShaper (VoiceShaper* /*shaper*/) noexcept {}
    // Adds (does not overwrite) n samples into left/right, with the gains already applied.
    virtual void render (float* left, float* right, int n, float gainLeft, float gainRight) noexcept = 0;
    // Automated parameter (plugins). 0..1. Audio thread.
    virtual void setParameter (int /*index*/, float /*value*/) noexcept {}
    // A hosted plugin's parameter value now (0..1; −1 = unknown), for a Patcher parameter output (round 171b). Audio thread.
    virtual float parameterValue (int /*index*/) const noexcept { return -1.0f; }
    // Latency the instrument introduces (plugins), in samples. For PDC.
    virtual int latencySamples() const noexcept { return 0; }
    // "Slide" note: sounding notes glide to `key` over `samples`, without retriggering. Ignored if unsupported.
    virtual void slideTo (int /*key*/, float /*cents*/, float /*samples*/) noexcept {}
    // FL's channel Porta / Mono for this instrument (round 168l, core/ChannelPoly.h): whether the rack's porta can start
    // its voices from the previous note's pitch (its voices take HostSlide::pendingGlide() at note on) and whether its
    // sounding voice can follow a Mono channel's notes (slideTo). Engines that do neither: the rack retriggers instead.
    virtual bool followsChannelPorta() const noexcept { return false; }
    virtual bool followsSlides() const noexcept { return followsChannelPorta(); }
    // Round 190w (FL 2026's playback locator in the channel settings' sample display, What's New 21138): where the
    // newest sounding voice reads, as a fraction 0..1 of its played region in play order (a reversed voice from the
    // region's end); -1 when no voice sounds. Any thread. Only LYNX's Sampler reports it.
    virtual float playPosition() const noexcept { return -1.0f; }
    // Audio clip: fades and in-clip position of the next note (before noteOn), in samples.
    // FL 2026's clip pitch (round 167k): the next audio clip note's own transposition, shifted keeping its length;
    // setClipShift (UI thread, before prepareKeyShift) asks for the pitch shifters even without "keep pitch"
    virtual void setNextClipPitch (float /*semitones*/) noexcept {}
    // the next audio clip's stretch (FL 2026's clip stretch, round 167l): its audio lasts that many times longer
    virtual void setNextClipStretch (float /*stretch*/) noexcept {}
    virtual void setClipShift (bool) {}
    virtual void setNextClipFades (float /*elapsed*/, float /*remaining*/, float /*fadeIn*/, float /*fadeOut*/, std::uint16_t /*shape*/ = 0,
                                  float /*inTension*/ = 0.0f, float /*outTension*/ = 0.0f) noexcept {}
    // Pan, fine tuning and "porta" (inverts the channel's porta) of the next note (called right before
    // noteOn). Ignored if unsupported.
    virtual void setNextNoteExpression (float /*pan*/, float /*cents*/, bool /*porta*/ = false) noexcept {}
    // The next note's volume factor (FL's echo delay, REA round 146b); false when the instrument does not scale its
    // voices (the caller then scales the velocity).
    virtual bool setNextNoteGain (float /*gain*/) noexcept { return false; }
    // The next note's Mod X / Y (0..1, 0.5 = none; the note's own, moved by the echo delay and the key / velocity
    // tracking, REA round 151), called right before noteOn; and the release velocity of the next noteOff (0..1),
    // right before it. Ignored if unsupported (hosted plugins: FL's wrapper MIDI options).
    virtual void setNextNoteModulation (float /*modX*/, float /*modY*/) noexcept {}
    virtual void setNextNoteRelease (float /*velocity*/) noexcept {}
    // The next note's own fine pitch (cents, the piano roll's, without the channel's tuning; issue #63), called right
    // before noteOn. Hosted plugins send it as FL's wrapper does (core/HostNotePitch.h: the key it rounds to, the rest as
    // the VST3 note's tuning); native engines take it with the channel's in setNextNoteExpression.
    virtual void setNextNoteFinePitch (float /*cents*/) noexcept {}
    // Playback stopped: the held notes stop (hosted plugins may send "All Notes Off" or only those notes' note offs:
    // FL's wrapper option). The panic button: everything stops (a hosted plugin may also be reset).
    virtual void transportStopped() noexcept { allNotesOff(); }
    // Playback started (FL resets its generators: a MIDI Out sends its bank, patch and controls again).
    virtual void transportStarted() noexcept {}
    virtual void panic() noexcept { allNotesOff(); }
    // FL's "Reset plugins on transport" (round 167k: FL resets every plugin it registered, generators included —
    // FUN_012b8620 adds each opened plugin to the list FUN_012b5210 resets). The Sampler / audio clip channels are not
    // plugins in FL and keep their voices; a plugin generator stops its voices, a hosted plugin is also reset (with its
    // wrapper's "Reset plugin when FL Studio resets").
    virtual void transportReset() noexcept { panic(); }
    // Channel settings (sampler, synth). UI thread, on every snapshot; the instrument stores them
    // so the audio thread can read without waiting.
    virtual void configure (const ChannelDef&, double /*bpm*/) noexcept {}
    // A new instrument, configured and not yet playing (UI thread): it may take its settings now, so the first
    // block or note does not build them on the audio thread.
    virtual void settleNew() {}
    // Control output ("controller" instruments), 0..1. Audio thread.
    virtual float controlOutput (int /*output*/) const noexcept { return 0.0f; }
    // the program the instrument plays when a note can select it (Transistor Bass's sequencer: the note's key), −1
    // otherwise; the editor follows it as FL's does (round 169g2)
    virtual int selectedProgram() const noexcept { return -1; }
    // Song position (ticks) at the start of the block, for the global LFOs. Audio thread.
    virtual void setSongPosition (double /*ticks*/) noexcept {}
    // No voice sounding: render would only produce silence (the mixer skips the channel until a note arrives).
    virtual bool idle() const noexcept { return false; }
    // The notes it sent out in its renders since the last call (a hosted plugin's MIDI output, FL's wrapper "Output
    // port", round 149): fills `out` with up to `max` and returns how many.
    struct MidiOutNote
    {
        int key = 60;
        float velocity = 0.0f; // 0 = note off
    };
    virtual int takeMidiOutput (MidiOutNote* /*out*/, int /*max*/) noexcept { return 0; }
    // Short MIDI messages it sends on its MIDI port (a Dashboard's controls and program, round 165): audio thread;
    // each one packed as status | data1 << 8 | data2 << 16 (a 0xC0 / 0xD0 status has no data 2). Fills `out` with up
    // to `max` and returns how many; the rest wait for the next call.
    virtual int takeMidiMessages (std::uint32_t* /*out*/, int /*max*/) noexcept { return 0; }
    // Extra outputs to other mixer inserts (a generator Patcher's extra "To LYNX" outputs, REA_rodada141): the rack
    // calls beginChunk (n) before a chunk's render() calls, which then also add each extra output's audio (gains
    // applied) into auxOutputData (k, side) from the chunk's start; auxOutputTrack (k) = the insert it feeds.
    virtual void beginChunk (int /*n*/) noexcept {}
    // A generator Patcher holding a Hardware Insert (round 165c): the device's buses for this chunk (null: offline) and
    // where the chunk starts in the callback piece. The rack calls it after beginChunk.
    virtual void setHardwareIo (const LiveInput* /*in*/, HardwareOutput* /*out*/, int /*offset*/) noexcept {}
    virtual int auxOutputCount() const noexcept { return 0; }
    virtual int auxOutputTrack (int /*k*/) const noexcept { return -1; }
    virtual const float* auxOutputData (int /*k*/, int /*side*/) const noexcept { return nullptr; }
    // A VFX channel / node (round 164b, core/Vfx.h): its note processor (null for every other instrument)
    virtual vfx::Processor* noteProcessor() noexcept { return nullptr; }
    // The next note's colour group (0…15, FL's note colours; called right before noteOn): a Patcher passes it to its
    // VFX nodes (VFX Color Mapper, VFX Envelope). Ignored by the others.
    virtual void setNextNoteColour (int /*colour*/) noexcept {}
    // A property of the sounding voices of `key` changes while they sound (VFX Envelope, round 164b): `prop` is a
    // vfx::Prop (0 velocity 0..1 — the voice's volume relative to its note-on velocity is value / velocity at
    // note-on —, 1 pitch in cents from the key, 2 pan −1..1, 3 Mod X, 4 Mod Y). Ignored if unsupported.
    virtual void voiceProperty (int /*key*/, int /*prop*/, float /*value*/) noexcept {}
    // Issue #108 (MPE, core/Mpe.h): a played note's own expression changing while it sounds — `prop` a vfx::Prop
    // (pitch: the note's own cents, `baseCents` the channel's tuning on top; modX = timbre, modY = pressure, 0..1).
    // Native engines take it as a voice property; a hosted plugin sends it on the note's member channel.
    virtual void noteExpression (int key, int prop, float value, float baseCents) noexcept
    {
        voiceProperty (key, prop, prop == 1 /* vfx::Prop::pitch */ ? value + baseCents : value);
    }
    // The same for a voice that arrived on MIDI port `port` (a Channel Rack VFX's output, round 165c): a Patcher passes
    // it to what that port reaches; hosted plugins cannot take it (MIDI has no per-voice properties).
    virtual void voicePropertyPort (int /*port*/, int /*key*/, int /*prop*/, float /*value*/, int /*midiChannel*/) noexcept {}
    // What a generator Patcher's VFX nodes send to its "To LYNX" MIDI ports (round 165c): note ons / offs and voice
    // properties, played on those ports at the next chunk's start. Audio thread.
    struct PortEvent
    {
        int port = 0, key = 60, channel = 0;
        int prop = -1;        // ≥ 0: a voice property (vfx::Prop) with `value`; −1: a note on (value = velocity > 0) or off
        float value = 0.0f;
        float pan = 0.0f, cents = 0.0f, modX = 0.5f, modY = 0.5f, release = 0.5f;
    };
    virtual int takePortEvents (PortEvent* /*out*/, int /*max*/) noexcept { return 0; }

    // Note identity (round 190ej, NoteEvent::noteId; 0 = none): the note the next noteOn / noteOnAt starts and the note
    // the next noteOff ends. The rack sets each right before the call and clears it right after, so an instrument's own
    // calls (allNotesOff's loops, a note on releasing its key first) never see one. FL gives every note its own voice:
    // of two overlapping notes of one key on one channel, the first note's end ends only its own voice (FL 2026 [EXEC:
    // /R renders of 21 generators, round 190ej]). An instrument that keeps voices by note stores startingNoteId() in
    // the voice and ends, at a note off, only the voices endsVoice() accepts; an off without an id (a live key, the
    // rack's Mono / Max polyphony releases) ends every voice of its key, as before.
    void setNextNoteId (std::uint32_t id) noexcept { onNoteId = id; }
    void setNextNoteOffId (std::uint32_t id) noexcept { offNoteId = id; }

protected:
    std::uint32_t startingNoteId() const noexcept { return onNoteId; }
    std::uint32_t endingNoteId() const noexcept { return offNoteId; }
    bool endsVoice (int voiceKey, std::uint32_t voiceNoteId, int key) const noexcept
    {
        return voiceKey == key && (offNoteId == 0 || voiceNoteId == offNoteId);
    }

private:
    float wheel = 0.0f; // (pitchBend's default)
    std::uint32_t onNoteId = 0, offNoteId = 0;
};

// The FL generators that keep one sounding note per key (FL 2026 [EXEC: /R renders, round 190ej]: Kepler, Kepler Exo,
// MiniSynth, PoiZone, Sawer): a second note of a held key takes the key over, and a note off ends the key's sound
// only when it is the end of the note that took the key last (the earlier note's end does nothing). Per key: the
// note that holds it. Audio thread, fixed storage.
class KeyOwners
{
public:
    void start (int key, std::uint32_t id) noexcept
    {
        if (key >= 0 && key < 128)
            owner[(std::size_t) key] = id;
    }
    // a note off of `key` with note id `id` (0 = none: always) ends the key's sound
    bool ends (int key, std::uint32_t id) const noexcept
    {
        return id == 0 || key < 0 || key >= 128 || owner[(std::size_t) key] == 0 || owner[(std::size_t) key] == id;
    }

private:
    std::array<std::uint32_t, 128> owner {};
};

// Audio of an already-decoded sample (loaded off the audio thread). Short samples
// live in the vectors; long ones live in a memory-mapped cache file (the OS reads
// from disk on demand) and use the `mapped*` pointers.
struct SampleData
{
    std::vector<float> left, right; // empty right = mono
    double sampleRate = 44100.0;
    // the file's own loop (WAV smpl chunk, first loop): frames [loopStart, loopEnd), −1 = none
    std::int64_t loopStart = -1, loopEnd = -1;
    // a WAV's cue points / regions (their starts, ascending) and ACID beat count (0 = none): what FL hands a plugin as
    // the sample's regions and beats (Fruity Granulizer's slices and tempo sync)
    std::vector<std::int64_t> markerStarts;
    float acidBeats = 0.0f;
    // round 190ey (live check 190ew SMP-1 / SMP-7): the file's slice markers (FL's ACID "strc" list, its entries after the
    // first; 0 = none) and its sample format (bits per sample, 0 = unknown; float or integer), as FL's Sampler page shows them
    int sliceMarkers = 0;
    int bitsPerSample = 0;
    bool floatData = false;
    // a sample FL's precomputed effects were applied to (core/SamplePrecompute.h): it holds only the played region,
    // which was [cropFrom, cropTo] of the file (fractions); the sampler maps its Start / End knobs into it
    bool cropped = false;
    double cropFrom = 0.0, cropTo = 1.0;

    const float* mappedLeft = nullptr;
    const float* mappedRight = nullptr; // nullptr = mono
    std::size_t mappedFrames = 0;
    std::shared_ptr<const void> mapping; // owner of the mapping

    bool isMapped() const noexcept { return mappedLeft != nullptr; }
    std::size_t frames() const noexcept { return isMapped() ? mappedFrames : left.size(); }
    const float* leftData() const noexcept { return isMapped() ? mappedLeft : left.data(); }
    const float* rightData() const noexcept
    {
        if (isMapped())
            return mappedRight != nullptr ? mappedRight : mappedLeft;
        return right.empty() ? left.data() : right.data();
    }
    bool isStereo() const noexcept { return isMapped() ? mappedRight != nullptr : ! right.empty(); }
};

// Offline rendering (export) in progress. Instruments that, like FL's plugins, have separate render-quality settings
// switch to them while it is set (Sytrus: oversampling and envelope accuracy, REA_rodada31 §1.2); they read it in
// prepare()/configure(), so set it before preparing the graph for the render and clear it afterwards.
void setOfflineRendering (bool on) noexcept;
bool offlineRendering() noexcept;
// An export is rendering, at whatever quality (issue #138): setOfflineRendering() is only on with the Export window's
// "HQ for all plugins", so an instrument that must wait for data another thread is still loading (LYNX Soundfont's
// font) asks this instead. Set for the whole of an offline render; never while playing live.
void setRenderInProgress (bool on) noexcept;
bool renderInProgress() noexcept;
// FL's "HQ for all plugins" during an export (REA round 134): effects with an HQ switch use HQ whatever it is set to.
void setHqForAllPlugins (bool on) noexcept;
bool hqForAllPlugins() noexcept;
// The built-in synth renders up to four voices side by side (round 190gp; the same samples as one voice at a time,
// which `false` restores: for tests and A/B runs).
void setSynthVoiceLanes (bool on) noexcept;
bool synthVoiceLanes() noexcept;

// Where the song is, for hosted plugins (tempo-synced effects, LFO/volume shapers, arpeggiators read the host's
// transport): the renderer publishes it at the start of each chunk it renders.
struct HostTimeline
{
    std::atomic<double> tick { 0.0 };          // song position in LYNX ticks (ppq per quarter note)
    std::atomic<double> loopStart { 0.0 }, loopEnd { 0.0 }; // the playback loop in LYNX ticks (round 151: "Send loop position")
    std::atomic<bool> looping { false };
    std::atomic<double> bpm { 120.0 };
    std::atomic<double> sampleRate { 44100.0 };
    std::atomic<bool> playing { false };
    std::atomic<int> beatsPerBar { 4 };
    // round 169c (issue #32): the chunk's tempo in ticks per sample (0 = stopped) and a count raised for each chunk.
    // A generator is called once per stretch between its notes, so one chunk can be several plugin blocks: each block
    // reports the position where it starts (tickAt), not the chunk's start again. Before, every block of a chunk said
    // the same position, so a synced sequencer / arpeggiator / LFO saw the song stand still and jump back (it went
    // out of time or restarted its step).
    std::atomic<double> ticksPerSample { 0.0 };
    std::atomic<std::uint64_t> chunk { 0 };
    double tickAt (int sampleOffset) const noexcept
    {
        return tick.load (std::memory_order_relaxed) + (double) sampleOffset * ticksPerSample.load (std::memory_order_relaxed);
    }
};
HostTimeline& hostTimeline() noexcept;
// Where in the current chunk the plugin block this thread is about to process starts (samples). Set by the hosted
// generator around its blocks and read by its play head; 0 everywhere else (effects get the whole chunk).
void setHostBlockOffset (int samples) noexcept;
int hostBlockOffset() noexcept;

// A hosted generator's sample position within the renderer's chunk (round 169c): render() is called for consecutive
// stretches of the chunk, so the offset of each stretch is the sum of the ones before it in the same chunk.
struct HostBlockClock
{
    std::uint64_t seenChunk = ~std::uint64_t { 0 };
    int offset = 0;
    int start (std::uint64_t chunk) noexcept
    {
        if (chunk != seenChunk)
        {
            seenChunk = chunk;
            offset = 0;
        }
        return offset;
    }
    void advance (int samples) noexcept { offset += samples; }
};

// The keys a hosted plugin was sent a note on for, per MIDI channel (round 169c, issue #32). Stopping or jumping sends
// each of them a note off: a VST3 plugin never gets All Notes Off (CC 123) or All Sound Off (CC 120), because VST3 has
// no MIDI controller events (a CC reaches a VST3 plugin only as a parameter the plugin maps it to, and most synths
// map neither), so the notes sounding before a jump kept playing under the ones started after it.
struct HostedKeys
{
    std::array<std::array<std::uint64_t, 2>, 16> bits {};
    void set (int channel, int key, bool on) noexcept
    {
        if (channel < 0 || channel > 15 || key < 0 || key > 127)
            return;
        auto& word = bits[(std::size_t) channel][(std::size_t) (key >> 6)];
        const auto bit = std::uint64_t { 1 } << (key & 63);
        word = on ? (word | bit) : (word & ~bit);
    }
    bool held (int channel, int key) const noexcept
    {
        return channel >= 0 && channel < 16 && key >= 0 && key < 128
               && ((bits[(std::size_t) channel][(std::size_t) (key >> 6)] >> (key & 63)) & 1u) != 0;
    }
    bool any() const noexcept
    {
        for (const auto& c : bits)
            if (c[0] != 0 || c[1] != 0)
                return true;
        return false;
    }
    // calls noteOff (channel 0..15, key) for every held key, then forgets them
    template <typename NoteOff>
    void releaseAll (NoteOff&& noteOff) noexcept
    {
        for (int ch = 0; ch < 16; ++ch)
            for (int key = 0; key < 128; ++key)
                if (held (ch, key))
                    noteOff (ch, key);
        bits = {};
    }
};

// `sample` is only used by the sampler; without a sample the sampler is silent.
// The Sampler's Gain knob (SamplerSettings::gainDb) as a factor: dB clamped to -24..+24, exactly 1 at 0 dB.
float samplerGain (float db) noexcept;

std::unique_ptr<Instrument> makeInstrument (InstrumentKind kind, std::shared_ptr<const SampleData> sample = nullptr);

// Multi-sample sampler: each zone with its sample already loaded (nullptr = missing file).
struct LoadedZone
{
    SampleZone zone;
    std::shared_ptr<const SampleData> sample;
};
std::unique_ptr<Instrument> makeZonedSampler (std::vector<LoadedZone> zones);

// Cheap, deterministic white noise (xorshift), identical on every platform.
struct Noise
{
    std::uint32_t state = 0x12345678u;

    float next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float) (std::int32_t) state * (1.0f / 2147483648.0f);
    }
};
} // namespace lynx
