#include "flm_importer.h"

#include <algorithm>
#include <map>
#include <cmath>

namespace lynx::flm_android
{

// ---------- Helper: MIDI note to frequency ----------

static double midiToFreq(int note)
{
    return 440.0 * std::pow(2.0, (note - 69) / 12.0);
}

// ---------- Helper: Resolve FLM paths (@L@, @R@) ----------

std::string resolveFlmPath(const std::string& flmPath, const FlmImportOptions& options)
{
    if (flmPath.empty()) return "";
    
    std::string path = flmPath;
    
    // Replace @L@ with project folder
    size_t pos = path.find("@L@");
    if (pos != std::string::npos && !options.projectFolder.empty())
    {
        path.replace(pos, 3, options.projectFolder);
    }
    
    // Replace @R@ with resource root
    pos = path.find("@R@");
    if (pos != std::string::npos && !options.resourceRoot.empty())
    {
        path.replace(pos, 3, options.resourceRoot);
    }
    
    // Try user resource roots
    if (path.find("@R@") != std::string::npos)
    {
        for (const auto& root : options.userResourceRoots)
        {
            std::string testPath = path;
            size_t rpos = testPath.find("@R@");
            if (rpos != std::string::npos)
            {
                testPath.replace(rpos, 3, root);
                if (options.fileExists && options.fileExists(testPath))
                {
                    return testPath;
                }
            }
        }
    }
    
    // Normalize path separators
    std::replace(path.begin(), path.end(), '\\', '/');
    
    return path;
}

// ---------- Check if FLM data ----------

bool isFlmData(const std::uint8_t* data, std::size_t size)
{
    if (size < 4) return false;
    return data[0] == 0x31 && data[1] == 0x30 && data[2] == 0x4C && data[3] == 0x46; // "10LF"
}

// ---------- Convert FLM Module Type to InstrumentKind ----------

static InstrumentKind moduleTypeToInstrumentKind(std::uint32_t type)
{
    using namespace module;
    switch (type)
    {
        case module::miniSynth: return InstrumentKind::synth;
        case module::dwSampler: return InstrumentKind::sampler;
        case module::gmSynth: return InstrumentKind::synth;
        case module::transistorBass: return InstrumentKind::synth;
        case module::superSaw: return InstrumentKind::synth;
        case module::slicer: return InstrumentKind::sampler;
        case module::threeOsc: return InstrumentKind::synth;
        case module::soundFont: return InstrumentKind::sampler;
        default: return InstrumentKind::synth;
    }
}

// ---------- Convert FLM Module to ChannelDef ----------

static ChannelDef moduleToChannel(const flm_android::Module& mod, const std::string& name, int mixerTrack)
{
    ChannelDef ch;
    ch.name = name.empty() ? "Module " + std::to_string(mod.instanceId) : name;
    ch.instrument = moduleTypeToInstrumentKind(mod.type);
    ch.mixerTrack = mixerTrack;
    ch.volume = 0.78f;
    ch.pan = 0.0f;
    
    // Apply module params to channel (simplified)
    if (!mod.params.empty())
    {
        // First param usually volume/level
        if (mod.params.size() > 0)
            ch.volume = std::clamp(mod.params[0], 0.0f, 1.0f);
    }
    
    // Sample path for samplers
    if (!mod.presetFile.empty())
    {
        ch.samplePath = mod.presetFile;
    }
    
    return ch;
}

// ---------- Convert FLM Event to Note ----------

static Note eventToNote(const Event& evt, double ticksPerBeat)
{
    Note note;
    note.start = static_cast<std::int64_t>(evt.tick);
    note.length = static_cast<std::int64_t>(evt.length * ticksPerBeat);
    note.key = evt.key;
    note.velocity = evt.velocity / 255.0f;
    note.pan = (evt.pan - 127) / 127.0f; // -1..1
    return note;
}

// ---------- Main Import Function ----------

std::optional<Song> importFlmProject(const Project& flmProject, const FlmImportOptions& options, FlmImportReport* report)
{
    if (report)
    {
        report->converted.clear();
        report->approximated.clear();
        report->unsupported.clear();
        report->droppedEffects.clear();
        report->partialEffects.clear();
        report->missingSamples = 0;
        report->relocatedSamples = 0;
    }

    Song song;
    song.bpm = flmProject.tempo;
    song.beatsPerBar = flmProject.numerator;
    song.beatUnit = flmProject.denominator;
    song.info.title = flmProject.title;
    song.info.author = flmProject.author;
    song.info.created = flmProject.created;
    song.info.modified = flmProject.saved;
    song.projectId = flmProject.originalName;
    
    // FLM uses 128 ticks per beat (FlmFormat.h: ticksPerBeat = 128)
    const double flmTicksPerBeat = 128.0;
    const double lynxTicksPerBeat = 960.0; // PPQ 960
    const double tickScale = lynxTicksPerBeat / flmTicksPerBeat; // 960/128 = 7.5

    int nextChannelId = 1; // 0 = Master
    std::map<std::uint32_t, int> flmChannelToMixerTrack;

    // Process each FLM channel/rack pair
    for (size_t i = 0; i < flmProject.channels.size() && i < flmProject.racks.size(); ++i)
    {
        const auto& flmChannel = flmProject.channels[i];
        const auto& flmRack = flmProject.racks[i];
        
        int mixerTrack = nextChannelId++;
        flmChannelToMixerTrack[flmChannel.channelId] = mixerTrack;
        
        // Create mixer track for this channel
        MixerTrack mt;
        mt.name = flmChannel.name;
        mt.volume = 0.8f;
        mt.pan = 0.0f;
        
        // Apply rack params to mixer track
        if (flmRack.params.size() > 0) mt.volume = std::clamp(flmRack.params[0], 0.0f, 1.0f); // level
        if (flmRack.params.size() > 1) mt.pan = std::clamp(flmRack.params[1] - 0.5f, -1.0f, 1.0f); // pan
        if (flmRack.params.size() > 2) mt.muted = flmRack.params[2] > 0.5f; // mute
        if (flmRack.params.size() > 3) mt.solo = flmRack.params[3] > 0.5f; // solo
        
        mt.colour = static_cast<std::uint32_t>(flmChannel.hue * 0xFFFFFF);
        song.mixer.push_back(mt);
        
        // Process rack modules as instruments/effects
        for (const auto& module : flmRack.modules)
        {
            if (isGenerator(module.type))
            {
                // Generator -> Channel
                ChannelDef ch = moduleToChannel(module, flmChannel.name, mixerTrack);
                
                // Handle sampler sample
                if (module.type == module::dwSampler && !module.presetFile.empty())
                {
                    std::string resolved = resolveFlmPath(module.presetFile, {});
                    if (options.fileExists && !options.fileExists(resolved))
                    {
                        if (report) report->missingSamples++;
                    }
                    else
                    {
                        ch.samplePath = resolved;
                    }
                }
                
                // Apply module params
                if (!module.params.empty())
                {
                    if (module.params.size() > 0) ch.volume = std::clamp(module.params[0], 0.0f, 1.0f);
                }
                
                song.channels.push_back(ch);
                
                if (report) report->converted[moduleName(module.type) ?: "Unknown Generator"]++;
            }
            else
            {
                // Effect module -> add to mixer track effects
                // Simplified: map common FLM effects to our effect kinds
                EffectKind ek = EffectKind::eq;
                // Map based on module type
                // (Full mapping would need full FLM module registry)
                
                if (!song.mixer.empty())
                {
                    song.mixer.back().effects.push_back(ek);
                    if (report) report->approximated[moduleName(module.type) ?: "Unknown Effect"]++;
                }
            }
        }
        
        // Process channel lanes -> patterns
        for (const auto& lane : flmChannel.lanes)
        {
            LaneKind kind = lane.kind();
            
            if (kind == LaneKind::notes || kind == LaneKind::drums)
            {
                // Collect notes from all clips in this lane
                std::map<std::uint32_t, Pattern> patternsByContentId;
                
                for (const auto& clip : lane.clips)
                {
                    Pattern& pattern = patternsByContentId[clip.contentId];
                    if (pattern.name.empty())
                    {
                        pattern.name = lane.name.empty() ? "Pattern" : lane.name;
                    }
                    pattern.lengthTicks = static_cast<std::int64_t>(clip.timelineLength() * 960.0 / 4.0); // Convert beats to ticks
                    
                    for (const auto& evt : clip.events)
                    {
                        Note note = eventToNote(evt, 128.0);
                        // Scale ticks: FLM 128 PPQ -> LYNX 960 PPQ
                        note.start = static_cast<std::int64_t>(note.start * 7.5);
                        note.length = static_cast<std::int64_t>(note.length * 7.5);
                        pattern.notes.push_back(note);
                    }
                }
                
                for (auto& [_, pattern] : patternsByContentId)
                {
                    pattern.sortNotes();
                    pattern.uid = song.patterns.size() + 1;
                    song.patterns.push_back(std::move(pattern));
                }
            }
            else if (kind == LaneKind::audio)
            {
                // Audio clips -> audio clip channels
                for (const auto& clip : lane.clips)
                {
                    if (clip.sample)
                    {
                        std::string samplePath = resolveFlmPath(clip.sample->path, {});
                        if (options.fileExists && !options.fileExists(samplePath))
                        {
                            if (report) report->missingSamples++;
                        }
                        else
                        {
                            // Create audio clip channel
                            ChannelDef ch;
                            ch.name = clip.sample->name.empty() ? "Audio Clip" : clip.sample->name;
                            ch.instrument = InstrumentKind::audioClip;
                            ch.samplePath = samplePath;
                            ch.mixerTrack = flmChannelToMixerTrack[lane.targetModule];
                            song.channels.push_back(ch);
                            
                            // Create playlist clip
                            Clip cl;
                            cl.kind = ClipKind::audio;
                            cl.start = static_cast<std::int64_t>(clip.startTick * 7.5);
                            cl.length = static_cast<std::int64_t>(clip.timelineLength() * 960.0 / 4.0);
                            cl.name = ch.name;
                            song.playlist.push_back(cl);
                        }
                    }
                }
            }
            else if (kind == LaneKind::automation)
            {
                // Automation lanes -> Automation
                // Simplified: create basic automation
            }
        }
    }
    
    // Sort patterns by UID
    for (auto& pattern : song.patterns)
    {
        pattern.sortNotes();
    }
    
    // Set default master track if no mixer tracks
    if (song.mixer.empty())
    {
        MixerTrack master;
        master.name = "Master";
        master.volume = 0.8f;
        song.mixer.push_back(master);
    }
    
    // Set current pattern
    song.currentPattern = song.patterns.empty() ? 0 : song.patterns[0].uid;
    
    return song;
}

} // namespace lynx::flm_android