#pragma once

// song_model.h — Port dari Song.h desktop (LYNX-SHARE) ke Android.
// Struktur data model inti untuk proyek DAW, tanpa dependensi JUCE.
// Fase 3 dari PORTING.md: port model Song.
//
// Hanya struktur yang relevan untuk Android (disederhanakan dari desktop):
//   - Meter, InstrumentKind, EffectKind
//   - Note, Pattern, ChannelDef, MixerTrack, Clip
//   - Song (kontainer utama)
//   - Automation, AutomationTarget
//
// JUCE dependencies yang dihilangkan: juce::String, juce::Array, juce::File,
// AudioProcessor, dll. Semua string menggunakan std::string.

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <optional>
#include <cmath>

// ---------- Meter ----------

struct Meter
{
    int beats = 4;   // beats per bar (numerator)
    int unit = 4;    // beat unit (denominator, 4 = quarter note)
};

// ---------- Instrument types ----------

enum class InstrumentKind : std::uint8_t
{
    kick,
    snare,
    hat,
    clap,
    synth,
    sampler,
    audioClip,
    plugin,
    controller,
    drumsynth,
    minisynth,
    // tambahan akan ditambahkan seiring kebutuhan
};

// ---------- Effect types ----------

enum class EffectKind : std::uint8_t
{
    eq,
    compressor,
    delay,
    reverb,
    limiter,
    filter,
    distortion,
    chorus,
    parametricEq,
    utility,
    // tambahan akan ditambahkan seiring kebutuhan
};

// ---------- Note ----------

struct Note
{
    std::int64_t start = 0;      // ticks from pattern start
    std::int64_t length = 960;   // ticks (1 quarter note at PPQ 960)
    int channel = 0;
    int key = 60;                // MIDI note, 60 = C4
    float velocity = 0.78f;      // 0..1
    float pan = 0.0f;            // -1..1
    float finePitch = 0.0f;      // cents, -120..120
    bool muted = false;
    bool slide = false;
    bool porta = false;
    float release = 0.5f;        // release velocity 0..1
    float modX = 0.5f, modY = 0.5f;
    int colour = 0;              // 0..15
    int group = 0;               // note group
    float chance = 1.0f;         // 0..1
    float velocityRange = 0.0f;  // -1..1
    int chanceGroup = 0;
    bool chanceOne = false;
};

// ---------- Pattern ----------

struct Pattern
{
    std::string name;
    std::vector<Note> notes;     // sorted by start
    std::int64_t lengthTicks = 0; // 0 = automatic (whole bars)
    std::uint32_t colour = 0;    // 0xFFRRGGBB, 0 = automatic
    std::uint64_t uid = 0;       // stable id

    void sortNotes() noexcept {
        std::sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) {
            return a.start < b.start;
        });
    }
};

// ---------- ChannelDef ----------

struct ChannelDef
{
    std::string name;
    InstrumentKind instrument = InstrumentKind::synth;
    float volume = 0.78f;   // 0..1
    float pan = 0.0f;       // -1..1
    bool muted = false;
    std::string samplePath; // used when instrument == sampler
    int mixerTrack = 0;     // 0 = straight to Master
    int maxPolyphony = 0;   // 0 = unlimited
    std::vector<float> modelParams; // instrument-specific parameters
    std::vector<std::uint8_t> pluginState; // raw plugin state
    float swingMix = 1.0f;  // 0..1
    int cutGroup = 0;       // 0 = none
    int cutBy = 0;
    std::uint32_t colour = 0; // 0xFFRRGGBB, 0 = theme default
    int group = -1;         // Channel Rack filter group
    int icon = 0;           // channel icon index
    std::uint64_t uid = 0;  // stable id

    bool hasKeyRegion() const noexcept { return false; } // disederhanakan
};

// ---------- MixerTrack ----------

struct MixerTrack
{
    std::string name;
    float volume = 0.8f;   // 0..1
    float pan = 0.0f;      // -1..1
    bool muted = false;
    bool solo = false;
    std::vector<EffectKind> effects; // effect chain
    bool toMaster = true;
    float masterLevel = 1.0f;
    float stereo = 0.0f;   // -1..1
    std::uint32_t colour = 0;
    bool invertPhase = false;
    bool swapChannels = false;
};

// ---------- Clip ----------

enum class ClipKind : std::uint8_t
{
    pattern,
    audio,
    automation
};

struct Clip
{
    ClipKind kind = ClipKind::pattern;
    std::int64_t start = 0;   // ticks from song start
    std::int64_t length = 0;  // ticks
    int patternId = -1;       // if kind == pattern
    std::string name;
    float volume = 1.0f;
    float pan = 0.0f;
};

// ---------- Automation ----------

enum class AutomationTargetKind : std::uint8_t
{
    none,
    channelVolume,
    channelPan,
    mixerVolume,
    mixerPan,
    effectParam,
    tempo,
    effectMix
};

struct AutomationTarget
{
    AutomationTargetKind kind = AutomationTargetKind::none;
    int index = 0;
    int slot = 0;
    int param = 0;
};

struct AutomationPoint
{
    std::int64_t tick = 0;
    float value = 0.5f;   // 0..1
    float tension = 0.0f; // -1..1
};

struct Automation
{
    std::string name;
    AutomationTarget target;
    std::vector<AutomationPoint> points;
};

// ---------- Song ----------

struct ProjectInfo
{
    std::string title;
    std::string author;
    std::string created;
    std::string modified;
};

struct Song
{
    double bpm = 120.0;
    int beatsPerBar = 4;
    int beatUnit = 4;
    float swing = 0.0f;          // 0..1
    std::uint32_t chanceSeed = 0;
    std::vector<std::string> channelGroups;
    float masterPitch = 0.0f;    // cents, -1200..1200
    ProjectInfo info;
    std::string projectId;
    std::uint64_t nextUid = 0;

    // Core data
    std::vector<ChannelDef> channels;
    std::vector<Pattern> patterns;
    int currentPattern = 0;
    std::vector<Clip> playlist;
    std::vector<MixerTrack> mixer; // [0] = Master
    std::vector<Automation> automations;

    // Playback state
    bool loopPlays = false;
    std::int64_t loopStart = 0;
    std::int64_t loopEnd = 0;

    double tempoFromValue(float v) const noexcept
    {
        constexpr double minTempo = 10.0;
        constexpr double maxTempo = 522.0;
        return minTempo + (maxTempo - minTempo) * (double)v;
    }
    float valueFromTempo(double bpm) const noexcept
    {
        constexpr double minTempo = 10.0;
        constexpr double maxTempo = 522.0;
        return (float)((bpm - minTempo) / (maxTempo - minTempo));
    }
};
