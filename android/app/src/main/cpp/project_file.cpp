#include "project_file.h"

#include <cstring>
#include <fstream>
#include <vector>

const char kLynxProjectMagic[4] = {'L', 'Y', 'N', 'X'};
constexpr std::uint32_t kLynxProjectVersion = 2; // Fase 3: Song model

namespace {

void writeBytes(std::ofstream& out, const void* data, std::size_t size) {
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
}

template <typename T>
void writePOD(std::ofstream& out, const T& value) {
    writeBytes(out, &value, sizeof(T));
}

bool readBytes(std::ifstream& in, void* data, std::size_t size) {
    in.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
    return in.gcount() == static_cast<std::streamsize>(size);
}

template <typename T>
bool readPOD(std::ifstream& in, T& value) {
    return readBytes(in, &value, sizeof(T));
}

std::string readString(std::ifstream& in) {
    std::uint32_t len = 0;
    if (!readPOD(in, len)) return "";
    std::string str;
    str.resize(len);
    if (len > 0) {
        readBytes(in, str.data(), len);
    }
    return str;
}

void writeString(std::ofstream& out, const std::string& str) {
    std::uint32_t len = static_cast<std::uint32_t>(str.size());
    writePOD(out, len);
    if (len > 0) {
        writeBytes(out, str.data(), len);
    }
}

// ---------- Note ----------

void writeNote(std::ofstream& out, const Note& n) {
    writePOD(out, n.start);
    writePOD(out, n.length);
    writePOD(out, n.channel);
    writePOD(out, n.key);
    writePOD(out, n.velocity);
    writePOD(out, n.pan);
    writePOD(out, n.finePitch);
    writePOD(out, n.muted);
    writePOD(out, n.slide);
    writePOD(out, n.porta);
    writePOD(out, n.release);
    writePOD(out, n.modX);
    writePOD(out, n.modY);
    writePOD(out, n.colour);
    writePOD(out, n.group);
    writePOD(out, n.chance);
    writePOD(out, n.velocityRange);
    writePOD(out, n.chanceGroup);
    writePOD(out, n.chanceOne);
}

bool readNote(std::ifstream& in, Note& n) {
    return readPOD(in, n.start) && readPOD(in, n.length) && readPOD(in, n.channel) &&
           readPOD(in, n.key) && readPOD(in, n.velocity) && readPOD(in, n.pan) &&
           readPOD(in, n.finePitch) && readPOD(in, n.muted) && readPOD(in, n.slide) &&
           readPOD(in, n.porta) && readPOD(in, n.release) && readPOD(in, n.modX) &&
           readPOD(in, n.modY) && readPOD(in, n.colour) && readPOD(in, n.group) &&
           readPOD(in, n.chance) && readPOD(in, n.velocityRange) &&
           readPOD(in, n.chanceGroup) && readPOD(in, n.chanceOne);
}

// ---------- Pattern ----------

void writePattern(std::ofstream& out, const Pattern& p) {
    writeString(out, p.name);
    std::uint32_t numNotes = static_cast<std::uint32_t>(p.notes.size());
    writePOD(out, numNotes);
    for (const auto& n : p.notes) {
        writeNote(out, n);
    }
    writePOD(out, p.lengthTicks);
    writePOD(out, p.colour);
    writePOD(out, p.uid);
}

bool readPattern(std::ifstream& in, Pattern& p) {
    p.name = readString(in);
    std::uint32_t numNotes = 0;
    if (!readPOD(in, numNotes)) return false;
    p.notes.resize(numNotes);
    for (auto& n : p.notes) {
        if (!readNote(in, n)) return false;
    }
    return readPOD(in, p.lengthTicks) && readPOD(in, p.colour) && readPOD(in, p.uid);
}

// ---------- ChannelDef ----------

void writeChannel(std::ofstream& out, const ChannelDef& c) {
    writeString(out, c.name);
    writePOD(out, static_cast<std::uint8_t>(c.instrument));
    writePOD(out, c.volume);
    writePOD(out, c.pan);
    writePOD(out, c.muted);
    writeString(out, c.samplePath);
    writePOD(out, c.mixerTrack);
    writePOD(out, c.maxPolyphony);
    // modelParams
    std::uint32_t numParams = static_cast<std::uint32_t>(c.modelParams.size());
    writePOD(out, numParams);
    for (float p : c.modelParams) {
        writePOD(out, p);
    }
    // pluginState
    std::uint32_t numState = static_cast<std::uint32_t>(c.pluginState.size());
    writePOD(out, numState);
    if (numState > 0) {
        writeBytes(out, c.pluginState.data(), numState);
    }
    writePOD(out, c.swingMix);
    writePOD(out, c.cutGroup);
    writePOD(out, c.cutBy);
    writePOD(out, c.colour);
    writePOD(out, c.group);
    writePOD(out, c.icon);
    writePOD(out, c.uid);
}

bool readChannel(std::ifstream& in, ChannelDef& c) {
    c.name = readString(in);
    std::uint8_t kind = 0;
    if (!readPOD(in, kind)) return false;
    c.instrument = static_cast<InstrumentKind>(kind);
    if (!readPOD(in, c.volume) || !readPOD(in, c.pan) || !readPOD(in, c.muted)) return false;
    c.samplePath = readString(in);
    if (!readPOD(in, c.mixerTrack) || !readPOD(in, c.maxPolyphony)) return false;
    std::uint32_t numParams = 0;
    if (!readPOD(in, numParams)) return false;
    c.modelParams.resize(numParams);
    for (float& p : c.modelParams) {
        if (!readPOD(in, p)) return false;
    }
    std::uint32_t numState = 0;
    if (!readPOD(in, numState)) return false;
    c.pluginState.resize(numState);
    if (numState > 0) {
        readBytes(in, c.pluginState.data(), numState);
    }
    return readPOD(in, c.swingMix) && readPOD(in, c.cutGroup) && readPOD(in, c.cutBy) &&
           readPOD(in, c.colour) && readPOD(in, c.group) && readPOD(in, c.icon) &&
           readPOD(in, c.uid);
}

// ---------- MixerTrack ----------

void writeMixerTrack(std::ofstream& out, const MixerTrack& t) {
    writeString(out, t.name);
    writePOD(out, t.volume);
    writePOD(out, t.pan);
    writePOD(out, t.muted);
    writePOD(out, t.solo);
    std::uint32_t numEffects = static_cast<std::uint32_t>(t.effects.size());
    writePOD(out, numEffects);
    for (EffectKind e : t.effects) {
        writePOD(out, static_cast<std::uint8_t>(e));
    }
    writePOD(out, t.toMaster);
    writePOD(out, t.masterLevel);
    writePOD(out, t.stereo);
    writePOD(out, t.colour);
    writePOD(out, t.invertPhase);
    writePOD(out, t.swapChannels);
}

bool readMixerTrack(std::ifstream& in, MixerTrack& t) {
    t.name = readString(in);
    if (!readPOD(in, t.volume) || !readPOD(in, t.pan) || !readPOD(in, t.muted) ||
        !readPOD(in, t.solo)) return false;
    std::uint32_t numEffects = 0;
    if (!readPOD(in, numEffects)) return false;
    t.effects.resize(numEffects);
    for (auto& e : t.effects) {
        std::uint8_t kind = 0;
        if (!readPOD(in, kind)) return false;
        e = static_cast<EffectKind>(kind);
    }
    return readPOD(in, t.toMaster) && readPOD(in, t.masterLevel) && readPOD(in, t.stereo) &&
           readPOD(in, t.colour) && readPOD(in, t.invertPhase) && readPOD(in, t.swapChannels);
}

// ---------- Clip ----------

void writeClip(std::ofstream& out, const Clip& c) {
    writePOD(out, static_cast<std::uint8_t>(c.kind));
    writePOD(out, c.start);
    writePOD(out, c.length);
    writePOD(out, c.patternId);
    writeString(out, c.name);
    writePOD(out, c.volume);
    writePOD(out, c.pan);
}

bool readClip(std::ifstream& in, Clip& c) {
    std::uint8_t kind = 0;
    if (!readPOD(in, kind)) return false;
    c.kind = static_cast<ClipKind>(kind);
    return readPOD(in, c.start) && readPOD(in, c.length) && readPOD(in, c.patternId) &&
           (c.name = readString(in), true) && readPOD(in, c.volume) && readPOD(in, c.pan);
}

// ---------- Automation ----------

void writeAutomation(std::ofstream& out, const Automation& a) {
    writeString(out, a.name);
    writePOD(out, static_cast<std::uint8_t>(a.target.kind));
    writePOD(out, a.target.index);
    writePOD(out, a.target.slot);
    writePOD(out, a.target.param);
    std::uint32_t numPoints = static_cast<std::uint32_t>(a.points.size());
    writePOD(out, numPoints);
    for (const auto& p : a.points) {
        writePOD(out, p.tick);
        writePOD(out, p.value);
        writePOD(out, p.tension);
    }
}

bool readAutomation(std::ifstream& in, Automation& a) {
    a.name = readString(in);
    std::uint8_t kind = 0;
    if (!readPOD(in, kind)) return false;
    a.target.kind = static_cast<AutomationTargetKind>(kind);
    if (!readPOD(in, a.target.index) || !readPOD(in, a.target.slot) || !readPOD(in, a.target.param)) return false;
    std::uint32_t numPoints = 0;
    if (!readPOD(in, numPoints)) return false;
    a.points.resize(numPoints);
    for (auto& p : a.points) {
        if (!readPOD(in, p.tick) || !readPOD(in, p.value) || !readPOD(in, p.tension)) return false;
    }
    return true;
}

} // namespace

bool saveProject(const std::string& path, const Song& song) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;

    // Header: magic + version
    writeBytes(out, kLynxProjectMagic, 4);
    writePOD(out, kLynxProjectVersion);

    // Section: Song metadata
    writePOD(out, song.bpm);
    writePOD(out, song.beatsPerBar);
    writePOD(out, song.beatUnit);
    writePOD(out, song.swing);
    writePOD(out, song.chanceSeed);
    writeString(out, song.info.title);
    writeString(out, song.info.author);
    writeString(out, song.info.created);
    writeString(out, song.info.modified);
    writeString(out, song.projectId);
    writePOD(out, song.nextUid);

    // Section: Channel groups
    std::uint32_t numGroups = static_cast<std::uint32_t>(song.channelGroups.size());
    writePOD(out, numGroups);
    for (const auto& g : song.channelGroups) {
        writeString(out, g);
    }

    // Section: Channels
    std::uint32_t numChannels = static_cast<std::uint32_t>(song.channels.size());
    writePOD(out, numChannels);
    for (const auto& c : song.channels) {
        writeChannel(out, c);
    }

    // Section: Patterns
    std::uint32_t numPatterns = static_cast<std::uint32_t>(song.patterns.size());
    writePOD(out, numPatterns);
    for (const auto& p : song.patterns) {
        writePattern(out, p);
    }

    // Section: currentPattern
    writePOD(out, song.currentPattern);

    // Section: Playlist
    std::uint32_t numClips = static_cast<std::uint32_t>(song.playlist.size());
    writePOD(out, numClips);
    for (const auto& c : song.playlist) {
        writeClip(out, c);
    }

    // Section: Mixer
    std::uint32_t numTracks = static_cast<std::uint32_t>(song.mixer.size());
    writePOD(out, numTracks);
    for (const auto& t : song.mixer) {
        writeMixerTrack(out, t);
    }

    // Section: Automations
    std::uint32_t numAutomations = static_cast<std::uint32_t>(song.automations.size());
    writePOD(out, numAutomations);
    for (const auto& a : song.automations) {
        writeAutomation(out, a);
    }

    // Section: Loop
    writePOD(out, song.loopPlays);
    writePOD(out, song.loopStart);
    writePOD(out, song.loopEnd);

    return out.good();
}

std::optional<Song> loadProject(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;

    // Header
    char magic[4] = {0};
    if (!readBytes(in, magic, 4)) return std::nullopt;
    if (std::memcmp(magic, kLynxProjectMagic, 4) != 0) return std::nullopt;

    std::uint32_t version = 0;
    if (!readPOD(in, version)) return std::nullopt;
    if (version != kLynxProjectVersion) return std::nullopt;

    Song song;

    // Song metadata
    if (!readPOD(in, song.bpm) || !readPOD(in, song.beatsPerBar) || !readPOD(in, song.beatUnit) ||
        !readPOD(in, song.swing) || !readPOD(in, song.chanceSeed)) return std::nullopt;
    song.info.title = readString(in);
    song.info.author = readString(in);
    song.info.created = readString(in);
    song.info.modified = readString(in);
    song.projectId = readString(in);
    if (!readPOD(in, song.nextUid)) return std::nullopt;

    // Channel groups
    std::uint32_t numGroups = 0;
    if (!readPOD(in, numGroups)) return std::nullopt;
    song.channelGroups.resize(numGroups);
    for (auto& g : song.channelGroups) {
        g = readString(in);
    }

    // Channels
    std::uint32_t numChannels = 0;
    if (!readPOD(in, numChannels)) return std::nullopt;
    song.channels.resize(numChannels);
    for (auto& c : song.channels) {
        if (!readChannel(in, c)) return std::nullopt;
    }

    // Patterns
    std::uint32_t numPatterns = 0;
    if (!readPOD(in, numPatterns)) return std::nullopt;
    song.patterns.resize(numPatterns);
    for (auto& p : song.patterns) {
        if (!readPattern(in, p)) return std::nullopt;
    }

    // currentPattern
    if (!readPOD(in, song.currentPattern)) return std::nullopt;

    // Playlist
    std::uint32_t numClips = 0;
    if (!readPOD(in, numClips)) return std::nullopt;
    song.playlist.resize(numClips);
    for (auto& c : song.playlist) {
        if (!readClip(in, c)) return std::nullopt;
    }

    // Mixer
    std::uint32_t numTracks = 0;
    if (!readPOD(in, numTracks)) return std::nullopt;
    song.mixer.resize(numTracks);
    for (auto& t : song.mixer) {
        if (!readMixerTrack(in, t)) return std::nullopt;
    }

    // Automations
    std::uint32_t numAutomations = 0;
    if (!readPOD(in, numAutomations)) return std::nullopt;
    song.automations.resize(numAutomations);
    for (auto& a : song.automations) {
        if (!readAutomation(in, a)) return std::nullopt;
    }

    // Loop
    if (!readPOD(in, song.loopPlays) || !readPOD(in, song.loopStart) || !readPOD(in, song.loopEnd)) return std::nullopt;

    return song;
}
