#include "engine/JsfxFormat.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <cmath>
#include <filesystem>
#include <mutex>

#include <ysfx.h>

#include "core/Inflate.h"
#include "core/Jsfx.h"
#include "engine/JsfxPackData.h"
#include "engine/JsfxReference.h"
#include "engine/PluginHost.h"
#include "engine/ProjectDowngrade.h"
#include "platform/Paths.h"
#include "ui/JsfxPanel.h"

namespace lynx
{
namespace
{
#if JUCE_WINDOWS || JUCE_MAC
constexpr bool caseInsensitivePaths = true;
#else
constexpr bool caseInsensitivePaths = false;
#endif

std::string utf8 (const juce::String& s) { return s.toStdString(); }
juce::String fromUtf8 (const char* s) { return s != nullptr ? juce::String::fromUTF8 (s) : juce::String(); }

// --- logs -------------------------------------------------------------------------------------------------------------
// ysfx reports through its config's reporter; LYNX keeps the errors of a compile (they become the failure message)
struct Log
{
    juce::StringArray errors, warnings;
};
void report (intptr_t userdata, ysfx_log_level level, const char* message)
{
    if (auto* log = reinterpret_cast<Log*> (userdata))
    {
        if (level == ysfx_log_error)
            log->errors.add (fromUtf8 (message));
        else if (level == ysfx_log_warning)
            log->warnings.add (fromUtf8 (message));
    }
}

struct ConfigDeleter { void operator() (ysfx_config_t* c) const noexcept { ysfx_config_free (c); } };
struct FxDeleter { void operator() (ysfx_t* f) const noexcept { ysfx_free (f); } };
struct StateDeleter { void operator() (ysfx_state_t* s) const noexcept { ysfx_state_free (s); } };
struct BankDeleter { void operator() (ysfx_bank_t* b) const noexcept { ysfx_bank_free (b); } };
using ConfigPtr = std::unique_ptr<ysfx_config_t, ConfigDeleter>;
using FxPtr = std::unique_ptr<ysfx_t, FxDeleter>;
using StatePtr = std::unique_ptr<ysfx_state_t, StateDeleter>;
using BankPtr = std::unique_ptr<ysfx_bank_t, BankDeleter>;

// --- roots and the file policy -------------------------------------------------------------------------------------
juce::File settingsList() { return platform::settingsFile ("jsfx-folders.txt"); }

std::mutex& policyLock()
{
    static std::mutex m;
    return m;
}
std::vector<std::string>& allowedFolders() // canonical, under policyLock
{
    static std::vector<std::string> folders;
    return folders;
}
juce::File& projectFolderNow()
{
    static juce::File folder;
    return folder;
}

std::string canonical (const juce::File& file)
{
    std::error_code ec;
    const auto p = std::filesystem::weakly_canonical (std::filesystem::path ((const char8_t*) file.getFullPathName().toRawUTF8()), ec);
    if (ec)
        return jsfx::normalise (utf8 (file.getFullPathName()));
    const auto s = p.generic_u8string();
    return jsfx::normalise (std::string ((const char*) s.data(), s.size()));
}

// The folders scripts may read below: every root, the Data folders REAPER's layout puts beside / inside them, and the
// open project's folder
void refreshPolicy()
{
    std::vector<std::string> folders;
    const auto roots = jsfx::roots();
    for (int i = 0; i < roots.getNumPaths(); ++i)
    {
        folders.push_back (canonical (roots[i]));
        folders.push_back (canonical (roots[i].getChildFile ("Data")));
        folders.push_back (canonical (roots[i].getSiblingFile ("Data")));
    }
    juce::File project;
    {
        const std::scoped_lock lock (policyLock());
        project = projectFolderNow();
    }
    if (project.isDirectory())
        folders.push_back (canonical (project));
    const std::scoped_lock lock (policyLock());
    allowedFolders() = std::move (folders);
}

bool policyAllows (const char* path)
{
    if (path == nullptr)
        return false;
    const auto file = canonical (juce::File (fromUtf8 (path)));
    const std::scoped_lock lock (policyLock());
    for (const auto& folder : allowedFolders())
        if (jsfx::isInside (file, folder, caseInsensitivePaths))
            return true;
    return false;
}

void installPolicyOnce()
{
    static std::once_flag once;
    std::call_once (once, [] {
        refreshPolicy();
        ysfx_lynx_set_path_filter (&policyAllows);
    });
}

std::vector<std::string> rootStrings()
{
    std::vector<std::string> out;
    const auto r = jsfx::roots();
    for (int i = 0; i < r.getNumPaths(); ++i)
        out.push_back (utf8 (r[i].getFullPathName()));
    return out;
}

// The root a script lies in and its id there; outside every root the id is its file name
jsfx::Located locateScript (const juce::File& file)
{
    auto located = jsfx::locate (utf8 (file.getFullPathName()), rootStrings(), caseInsensitivePaths);
    if (located.root < 0)
        located.relative = utf8 (file.getFileName());
    return located;
}

// --- a script's header ----------------------------------------------------------------------------------------------
struct Header
{
    bool ok = false;
    juce::String name, author;
    std::vector<std::string> tags;
    int inputs = 0, outputs = 0;
};

Header readHeader (const juce::File& file)
{
    Header h;
    ConfigPtr config (ysfx_config_new());
    Log log;
    ysfx_set_log_reporter (config.get(), &report);
    ysfx_set_user_data (config.get(), (intptr_t) &log);
    FxPtr fx (ysfx_new (config.get()));
    if (! ysfx_load_file (fx.get(), file.getFullPathName().toRawUTF8(), ysfx_load_ignoring_imports))
        return h;
    h.ok = true;
    h.name = fromUtf8 (ysfx_get_name (fx.get())).trim();
    if (h.name.isEmpty())
        h.name = file.getFileNameWithoutExtension();
    h.author = fromUtf8 (ysfx_get_author (fx.get())).trim();
    const auto count = ysfx_get_tags (fx.get(), nullptr, 0);
    for (std::uint32_t i = 0; i < count; ++i)
        h.tags.push_back (ysfx_get_tag (fx.get(), i));
    h.inputs = (int) ysfx_get_num_inputs (fx.get());
    h.outputs = (int) ysfx_get_num_outputs (fx.get());
    ysfx_set_user_data (config.get(), 0);
    return h;
}

bool hasJsfxExtension (const juce::File& f) { return f.hasFileExtension ("jsfx"); }

int lowestBit (std::uint64_t bits) noexcept
{
    auto n = 0;
    while ((bits & 1) == 0 && n < 64)
        bits >>= 1, ++n;
    return n;
}

std::uint64_t hashOfFile (const juce::File& f)
{
    juce::MemoryBlock data;
    if (! f.loadFileAsData (data))
        return 0;
    return jsfx::sourceHash (std::string_view ((const char*) data.getData(), data.getSize()));
}

bool extensionless (const juce::File& f)
{
    const auto name = f.getFileName();
    return ! name.containsChar ('.') && ! name.startsWithChar ('.');
}

bool headLooksLikeJsfx (const juce::File& f)
{
    juce::FileInputStream in (f);
    if (! in.openedOk())
        return false;
    char buffer[4096];
    const auto n = in.read (buffer, (int) sizeof buffer);
    return n > 0 && jsfx::looksLikeJsfx (std::string_view (buffer, (std::size_t) n));
}
} // namespace

// --- public helpers --------------------------------------------------------------------------------------------------
namespace jsfx
{
juce::File reaperEffectsFolder()
{
   #if JUCE_WINDOWS
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("REAPER").getChildFile ("Effects");
   #elif JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Application Support/REAPER/Effects");
   #else
    const auto xdg = juce::SystemStats::getEnvironmentVariable ("XDG_CONFIG_HOME", {});
    const auto config = xdg.isNotEmpty() && juce::File::isAbsolutePath (xdg) ? juce::File (xdg)
                                                                              : juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".config");
    return config.getChildFile ("REAPER").getChildFile ("Effects");
   #endif
}

juce::File userFolder() { return platform::documentsFolder().getChildFile ("LYNX").getChildFile (userFolderName); }
juce::File packFolder() { return platform::settingsFolder().getChildFile (packFolderName); }

juce::StringArray userFolders()
{
    juce::StringArray lines;
    lines.addLines (settingsList().loadFileAsString());
    juce::StringArray out;
    for (const auto& l : lines)
        if (juce::File::isAbsolutePath (l.trim()))
            out.addIfNotAlreadyThere (l.trim());
    return out;
}

void setUserFolders (const juce::StringArray& folders)
{
    settingsList().replaceWithText (folders.joinIntoString ("\n") + (folders.isEmpty() ? "" : "\n"));
    refreshPolicy();
}

juce::FileSearchPath roots()
{
    juce::FileSearchPath path;
    path.add (userFolder());
    path.add (packFolder());
    if (const auto reaper = reaperEffectsFolder(); reaper.isDirectory())
        path.addIfNotAlreadyThere (reaper);
    for (const auto& f : userFolders())
        if (juce::File (f).isDirectory())
            path.addIfNotAlreadyThere (juce::File (f));
    return path;
}

int installPack()
{
    static std::mutex installing;
    const std::scoped_lock lock (installing);
    const auto folder = packFolder();
    const auto stamp = folder.getChildFile (".lynx-pack");
    if (stamp.loadFileAsString().trim() == jsfxpack::hash)
        return 0;
    // what an earlier pack wrote and this one has no more is removed (only those files: the user's own stay)
    juce::StringArray before;
    before.addLines (folder.getChildFile (".lynx-pack-files").loadFileAsString());
    juce::StringArray now;
    auto written = 0;
    for (const auto& f : jsfxpack::files)
    {
        const auto data = inflateZlib (f.zlib, f.zlibSize);
        if (! data || data->size() != f.size)
            continue;
        const auto target = folder.getChildFile (fromUtf8 (f.path));
        target.getParentDirectory().createDirectory();
        if (target.replaceWithData (data->data(), data->size()))
            ++written;
        now.add (fromUtf8 (f.path));
    }
    for (const auto& old : before)
        if (old.isNotEmpty() && ! now.contains (old) && ! old.contains (".."))
            folder.getChildFile (old).deleteFile();
    folder.getChildFile (".lynx-pack-files").replaceWithText (now.joinIntoString ("\n") + "\n");
    stamp.replaceWithText (juce::String (jsfxpack::hash) + "\n");
    return written;
}

void setProjectFolder (const juce::File& folder)
{
    {
        const std::scoped_lock lock (policyLock());
        if (projectFolderNow() == folder)
            return;
        projectFolderNow() = folder;
    }
    refreshPolicy();
}

bool mayRead (const juce::File& path)
{
    installPolicyOnce();
    return policyAllows (path.getFullPathName().toRawUTF8());
}

juce::String relativeIdOf (const juce::PluginDescription& d)
{
    if (d.pluginFormatName != JsfxPluginFormat::formatName)
        return {};
    return fromUtf8 (locateScript (juce::File (d.fileOrIdentifier)).relative.c_str());
}

std::unique_ptr<juce::PluginDescription> findPortable (const juce::KnownPluginList& known, const juce::String& identifier)
{
    if (! identifier.startsWith (juce::String (JsfxPluginFormat::formatName) + "-"))
        return nullptr;
    // "<format>-<name>-<hash of the path>-<unique id>" (JUCE's createIdentifierString): the last part is the id, a hash of
    // the script's relative path and role (core/Jsfx.h), the same on every computer
    const auto uid = (int) (juce::uint32) identifier.fromLastOccurrenceOf ("-", false, false).getHexValue64();
    std::unique_ptr<juce::PluginDescription> best;
    auto bestRoot = 0;
    for (const auto& d : known.getTypes()) // (the same script in two roots: the first root wins, as roots() orders them)
        if (d.pluginFormatName == JsfxPluginFormat::formatName && d.uniqueId == uid)
        {
            const auto root = locateScript (juce::File (d.fileOrIdentifier)).root;
            const auto rank = root < 0 ? 1 << 20 : root;
            if (best == nullptr || rank < bestRoot)
                best = std::make_unique<juce::PluginDescription> (d), bestRoot = rank;
        }
    return best;
}
} // namespace jsfx

// --- the instance ---------------------------------------------------------------------------------------------------
namespace jsfx
{
class SliderParameter;

class Instance final : public juce::AudioPluginInstance, private juce::AsyncUpdater
{
public:
    Instance (const juce::PluginDescription& d, std::unique_ptr<Log> l, ConfigPtr c, FxPtr f, std::uint64_t source, juce::String relative)
        : AudioPluginInstance (busesFor (d)),
          description (d),
          log (std::move (l)),
          config (std::move (c)),
          fx (std::move (f)),
          sourceHash (source),
          relativeId (std::move (relative))
    {
        gfx = ysfx_has_section (fx.get(), ysfx_section_gfx);
        ysfx_set_midi_capacity (fx.get(), 4096, false); // (fixed: no allocation on the audio thread)
        makeParameters();
        if (const auto* bankPath = ysfx_get_bank_path (fx.get()); bankPath != nullptr && *bankPath != 0)
            bank.reset (ysfx_load_bank (bankPath));
    }

    ~Instance() override { cancelPendingUpdate(); }

    // --- AudioProcessor ---------------------------------------------------------------------------------------------
    const juce::String getName() const override { return description.name; }
    void fillInPluginDescription (juce::PluginDescription& d) const override { d = description; }

    void prepareToPlay (double sampleRate, int blockSize) override
    {
        const juce::ScopedLock lock (getCallbackLock());
        ysfx_set_sample_rate (fx.get(), sampleRate);
        ysfx_set_block_size (fx.get(), (std::uint32_t) juce::jmax (1, blockSize));
        applyPendingSliders();
        ysfx_init (fx.get()); // @init, here and not on the audio thread
        readBackSliders (false);
        const auto pdc = juce::roundToInt (ysfx_get_pdc_delay (fx.get()));
        latencyNow.store (pdc);
        setLatencySamples (pdc);
        prepared = true;
    }

    void releaseResources() override {}
    void reset() override { ysfx_delayed_init (fx.get()); }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        auto* f = fx.get();
        const auto frames = buffer.getNumSamples();
        applyPendingSliders();
        updateTimeInfo();
        for (const auto m : midi)
        {
            ysfx_midi_event_t event {};
            event.bus = 0;
            event.offset = (std::uint32_t) juce::jlimit (0, juce::jmax (0, frames - 1), m.samplePosition);
            event.size = (std::uint32_t) m.numBytes;
            event.data = m.data;
            ysfx_send_midi (f, &event);
        }
        const auto ins = (std::uint32_t) juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels());
        const auto outs = (std::uint32_t) juce::jmin (getTotalNumOutputChannels(), buffer.getNumChannels());
        // (in place: ysfx reads a frame's inputs before it writes that frame's outputs)
        ysfx_process_float (f, buffer.getArrayOfReadPointers(), buffer.getArrayOfWritePointers(), ins, outs, (std::uint32_t) frames);
        for (auto ch = (int) outs; ch < buffer.getNumChannels(); ++ch)
            buffer.clear (ch, 0, frames);
        midi.clear();
        ysfx_midi_event_t event {};
        while (ysfx_receive_midi (f, &event))
            if (event.bus == 0)
                midi.addEvent (event.data, (int) event.size, (int) juce::jlimit (0u, (std::uint32_t) juce::jmax (0, frames - 1), event.offset));
        readBackSliders (true);
        const auto pdc = juce::roundToInt (ysfx_get_pdc_delay (f));
        if (pdc != latencyNow.load (std::memory_order_relaxed))
        {
            latencyNow.store (pdc);
            triggerAsyncUpdate(); // (setLatencySamples tells the listeners: on the message thread)
        }
    }

    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override { return makeJsfxPanel (*this); }
    bool hasEditor() const override { return true; }

    // .rpl banks beside the script: its programs
    int getNumPrograms() override { return bank != nullptr ? juce::jmax (1, (int) bank->preset_count) : 1; }
    int getCurrentProgram() override { return program; }
    void setCurrentProgram (int index) override
    {
        if (bank == nullptr || index < 0 || index >= (int) bank->preset_count)
            return;
        {
            const juce::ScopedLock lock (getCallbackLock());
            ysfx_load_state (fx.get(), bank->presets[index].state);
            readBackSliders (false);
        }
        program = index;
        notifyAllParameters();
    }
    const juce::String getProgramName (int index) override
    {
        if (bank == nullptr || index < 0 || index >= (int) bank->preset_count)
            return {};
        return fromUtf8 (bank->presets[index].name);
    }
    void changeProgramName (int, const juce::String&) override {}

    // The state (core/Jsfx.h SavedState): under the callback lock, as ysfx's own plugin does (@serialize is script code
    // and must not run beside @sample); a block that meets it plays dry (PluginEffect::process)
    void getStateInformation (juce::MemoryBlock& data) override
    {
        SavedState s;
        s.relative = utf8 (relativeId);
        s.source = sourceHash;
        {
            const juce::ScopedLock lock (getCallbackLock());
            applyPendingSliders();
            StatePtr state (ysfx_save_state (fx.get()));
            if (state != nullptr)
            {
                for (std::uint32_t i = 0; i < state->slider_count; ++i)
                    s.sliders.emplace_back (state->sliders[i].index, state->sliders[i].value);
                s.serialized.assign ((const char*) state->data, state->data_size);
            }
        }
        const auto bytes = encodeState (s);
        data.replaceAll (bytes.data(), bytes.size());
    }

    void setStateInformation (const void* data, int size) override
    {
        SavedState s;
        if (! decodeState (data, (std::size_t) juce::jmax (0, size), s))
            return;
        changed = s.source != 0 && sourceHash != 0 && s.source != sourceHash;
        std::vector<ysfx_state_slider_t> sliderValues;
        for (const auto& [index, value] : s.sliders)
            if (index < (std::uint32_t) ysfx_max_sliders)
                sliderValues.push_back ({ index, value });
        ysfx_state_t state {};
        state.sliders = sliderValues.data();
        state.slider_count = (std::uint32_t) sliderValues.size();
        state.data = (std::uint8_t*) s.serialized.data();
        state.data_size = s.serialized.size();
        {
            const juce::ScopedLock lock (getCallbackLock());
            for (auto& p : pending)
                p.store (0);
            ysfx_load_state (fx.get(), &state);
            readBackSliders (false);
        }
        notifyAllParameters();
    }

    // --- what the panel and the tests read ---------------------------------------------------------------------------
    const juce::PluginDescription description;
    std::unique_ptr<Log> log;
    ConfigPtr config;
    FxPtr fx;
    const std::uint64_t sourceHash;
    const juce::String relativeId;
    bool gfx = false;
    std::atomic<bool> changed { false };
    std::atomic<int> latencyNow { 0 };
    std::vector<Slider> sliderList;            // the sliders the script has, in order (fixed after compiling)
    std::array<SliderParameter*, ysfx_max_sliders> bySlider {};
    std::array<std::atomic<double>, ysfx_max_sliders> values {}; // the script's slider values after the last block

    // a slider edit from the host or the UI: applied by the next block (or a state / prepare call) on its thread
    void queueSlider (int index, double value) noexcept
    {
        if (index < 0 || index >= ysfx_max_sliders)
            return;
        queued[(std::size_t) index].store (value, std::memory_order_relaxed);
        values[(std::size_t) index].store (value, std::memory_order_relaxed);
        pending[(std::size_t) index / 64].fetch_or (std::uint64_t (1) << (index % 64), std::memory_order_release);
    }

private:
    std::array<std::atomic<std::uint64_t>, ysfx_max_slider_groups> pending {}, automated {};
    std::array<std::atomic<double>, ysfx_max_sliders> queued {};
    BankPtr bank;
    int program = 0;
    bool prepared = false;
    std::int64_t lastPosition = -1;

    static BusesProperties busesFor (const juce::PluginDescription& d)
    {
        BusesProperties buses;
        if (! d.isInstrument)
            buses.addBus (true, "Input", juce::AudioChannelSet::stereo(), true);
        buses.addBus (false, "Output", juce::AudioChannelSet::stereo(), true);
        return buses;
    }

    void makeParameters();
    void notifyAllParameters();
    void handleAsyncUpdate() override;

    void applyPendingSliders() noexcept
    {
        for (int g = 0; g < ysfx_max_slider_groups; ++g)
        {
            auto bits = pending[(std::size_t) g].exchange (0, std::memory_order_acquire);
            while (bits != 0)
            {
                const auto bit = lowestBit (bits);
                bits &= bits - 1;
                const auto index = g * 64 + bit;
                ysfx_slider_set_value (fx.get(), (std::uint32_t) index, queued[(std::size_t) index].load (std::memory_order_relaxed), true);
            }
        }
    }

    // the script's own slider values back into the parameters (it may set them in @block / @sample); automation it
    // asks for (slider_automate) goes to the host from the message thread
    void readBackSliders (bool fromAudio) noexcept
    {
        for (const auto& s : sliderList)
            values[(std::size_t) s.index].store (ysfx_slider_get_value (fx.get(), (std::uint32_t) s.index), std::memory_order_relaxed);
        if (! fromAudio)
            return;
        auto any = false;
        for (std::uint8_t g = 0; g < ysfx_max_slider_groups; ++g)
            if (const auto bits = ysfx_fetch_slider_automations (fx.get(), g); bits != 0)
            {
                automated[g].fetch_or (bits, std::memory_order_relaxed);
                any = true;
            }
        if (any)
            triggerAsyncUpdate();
    }

    void updateTimeInfo() noexcept
    {
        ysfx_time_info_t info {};
        info.tempo = 120.0;
        info.time_signature[0] = 4;
        info.time_signature[1] = 4;
        info.playback_state = ysfx_playback_stopped;
        if (auto* head = getPlayHead())
            if (const auto position = head->getPosition())
            {
                const auto samples = position->getTimeInSamples();
                info.playback_state = position->getIsRecording() ? ysfx_playback_recording
                                      : position->getIsPlaying() ? ysfx_playback_playing
                                      : samples.hasValue() && *samples != lastPosition ? ysfx_playback_paused
                                                                                       : ysfx_playback_stopped;
                if (samples.hasValue())
                    lastPosition = *samples;
                if (const auto bpm = position->getBpm())
                    info.tempo = *bpm;
                if (const auto seconds = position->getTimeInSeconds())
                    info.time_position = *seconds;
                if (const auto ppq = position->getPpqPosition())
                    info.beat_position = *ppq;
                if (const auto sig = position->getTimeSignature())
                {
                    info.time_signature[0] = (std::uint32_t) juce::jmax (1, sig->numerator);
                    info.time_signature[1] = (std::uint32_t) juce::jmax (1, sig->denominator);
                }
            }
        ysfx_set_time_info (fx.get(), &info);
    }
};

// One slider as a host parameter: normalised through ysfx's own curves (linear / log / sqr), enum and file sliders as
// choices
class SliderParameter final : public juce::HostedAudioProcessorParameter
{
public:
    SliderParameter (Instance& o, const Slider& s, ysfx_slider_curve_t c) : owner (o), slider (s), curve (c) {}

    juce::String getParameterID() const override { return "slider" + juce::String (slider.index + 1); }
    float getValue() const override { return toNormalised (owner.values[(std::size_t) slider.index].load (std::memory_order_relaxed)); }
    void setValue (float v) override { owner.queueSlider (slider.index, toPlain (v)); }
    float getDefaultValue() const override { return toNormalised (slider.defaultValue); }
    juce::String getName (int maximumLength) const override { return slider.name.substring (0, maximumLength); }
    juce::String getLabel() const override { return {}; }
    juce::String getText (float v, int) const override { return textOf (toPlain (v)); }
    float getValueForText (const juce::String& text) const override
    {
        if (slider.isEnum || slider.isFile)
            if (const auto i = slider.choices.indexOf (text.trim(), true); i >= 0)
                return toNormalised (slider.minimum + i);
        return toNormalised (text.retainCharacters ("0123456789.-+eE").getDoubleValue());
    }
    bool isAutomatable() const override { return true; }
    bool isDiscrete() const override { return slider.isEnum || slider.isFile; }
    int getNumSteps() const override
    {
        if (isDiscrete())
            return juce::jmax (2, slider.choices.size());
        if (slider.step > 0 && slider.maximum > slider.minimum)
        {
            const auto steps = (slider.maximum - slider.minimum) / slider.step + 1;
            if (steps >= 2 && steps <= 4096)
                return (int) std::lround (steps);
        }
        return AudioProcessorParameter::getNumSteps();
    }
    juce::StringArray getAllValueStrings() const override { return isDiscrete() ? slider.choices : juce::StringArray(); }

    double toPlain (float v) const noexcept
    {
        auto value = ysfx_normalized_to_ysfx_value (juce::jlimit (0.0, 1.0, (double) v), &curve);
        const auto rounded = std::round (value); // (as ysfx's plugin: integers stay exact)
        if (std::abs (rounded - value) < 1.0e-5)
            value = rounded > -0.1 ? std::abs (rounded) : rounded;
        return value;
    }
    float toNormalised (double plain) const noexcept
    {
        if (std::abs (curve.max - curve.min) < 1.0e-12)
            return 0.0f;
        return (float) juce::jlimit (0.0, 1.0, ysfx_ysfx_value_to_normalized (plain, &curve));
    }
    juce::String textOf (double value) const
    {
        if (slider.isEnum || slider.isFile)
        {
            const auto i = (int) std::lround (value - slider.minimum);
            return juce::isPositiveAndBelow (i, slider.choices.size()) ? slider.choices[i] : juce::String (value);
        }
        auto decimals = 2;
        if (slider.step >= 1.0)
            decimals = 0;
        else if (slider.step > 0)
            decimals = juce::jlimit (1, 4, (int) std::ceil (-std::log10 (slider.step) - 1.0e-9));
        return juce::String (value, decimals);
    }

    Instance& owner;
    const Slider slider;
    const ysfx_slider_curve_t curve;
};

void Instance::makeParameters()
{
    for (std::uint32_t i = 0; i < (std::uint32_t) ysfx_max_sliders; ++i)
    {
        if (! ysfx_slider_exists (fx.get(), i))
            continue;
        Slider s;
        s.index = (int) i;
        s.name = fromUtf8 (ysfx_slider_get_name (fx.get(), i));
        if (s.name.isEmpty())
            s.name = "Slider " + juce::String ((int) i + 1);
        ysfx_slider_range_t range {};
        ysfx_slider_get_range (fx.get(), i, &range);
        s.minimum = range.min, s.maximum = range.max, s.step = range.inc, s.defaultValue = range.def;
        s.isEnum = ysfx_slider_is_enum (fx.get(), i);
        s.isFile = ysfx_slider_is_path (fx.get(), i);
        if (s.isEnum || s.isFile)
        {
            const auto count = ysfx_slider_get_enum_names (fx.get(), i, nullptr, 0);
            for (std::uint32_t k = 0; k < count; ++k)
                s.choices.add (fromUtf8 (ysfx_slider_get_enum_name (fx.get(), i, k)));
            if (s.isFile && s.maximum <= s.minimum)
                s.maximum = s.minimum + juce::jmax (0, s.choices.size() - 1);
        }
        ysfx_slider_curve_t curve {};
        ysfx_slider_get_curve (fx.get(), i, &curve);
        if ((s.isEnum || s.isFile) && curve.max <= curve.min)
            curve.max = curve.min + juce::jmax (1, s.choices.size() - 1);
        s.parameter = (int) sliderList.size();
        values[i].store (ysfx_slider_get_value (fx.get(), i));
        sliderList.push_back (s);
        auto p = std::make_unique<SliderParameter> (*this, s, curve);
        bySlider[i] = p.get();
        addHostedParameter (std::move (p));
    }
}

void Instance::notifyAllParameters()
{
    for (const auto& s : sliderList)
        if (auto* p = bySlider[(std::size_t) s.index])
            p->sendValueChangedMessageToListeners (p->getValue());
}

void Instance::handleAsyncUpdate()
{
    setLatencySamples (latencyNow.load());
    for (int g = 0; g < ysfx_max_slider_groups; ++g)
    {
        auto bits = automated[(std::size_t) g].exchange (0);
        while (bits != 0)
        {
            const auto bit = lowestBit (bits);
            bits &= bits - 1;
            if (auto* p = bySlider[(std::size_t) (g * 64 + bit)])
                p->sendValueChangedMessageToListeners (p->getValue()); // (slider_automate: it records as an automation move)
        }
    }
}

// --- the panel's view of an instance ---------------------------------------------------------------------------------
Instance* asJsfx (juce::AudioProcessor* plugin) { return dynamic_cast<Instance*> (plugin); }
std::vector<Slider> sliders (Instance& i) { return i.sliderList; }
bool sliderVisible (Instance& i, int index)
{
    if (index < 0 || index >= ysfx_max_sliders)
        return false;
    const auto mask = ysfx_get_slider_visibility (i.fx.get(), (std::uint8_t) (index / 64));
    return (mask >> (index % 64)) & 1;
}
double sliderValue (Instance& i, int index) { return juce::isPositiveAndBelow (index, (int) ysfx_max_sliders) ? i.values[(std::size_t) index].load() : 0.0; }
void setSliderValue (Instance& i, int index, double value)
{
    if (! juce::isPositiveAndBelow (index, (int) ysfx_max_sliders))
        return;
    if (auto* p = i.bySlider[(std::size_t) index])
        p->setValueNotifyingHost (p->toNormalised (value));
}
juce::String scriptName (Instance& i) { return i.description.name; }
juce::String scriptAuthor (Instance& i) { return fromUtf8 (ysfx_get_author (i.fx.get())); }
juce::String scriptFile (Instance& i) { return i.description.fileOrIdentifier; }
juce::String relativeId (Instance& i) { return i.relativeId; }
bool hasGfx (Instance& i) { return i.gfx; }
bool scriptChanged (Instance& i) { return i.changed.load(); }
int latency (Instance& i) { return i.latencyNow.load(); }
} // namespace jsfx

// --- the format ----------------------------------------------------------------------------------------------------
void JsfxPluginFormat::findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results, const juce::String& path)
{
    if (! fileMightContainThisPluginType (path))
        return;
    const juce::File file (path);
    const auto h = readHeader (file);
    if (! h.ok)
        return;
    const auto located = locateScript (file);
    const auto make = [&] (bool instrument) {
        auto d = std::make_unique<juce::PluginDescription>();
        d->name = h.name;
        d->descriptiveName = h.name;
        d->pluginFormatName = formatName;
        d->category = h.tags.empty() ? file.getParentDirectory().getFileName() : fromUtf8 (h.tags.front().c_str());
        d->manufacturerName = h.author.isNotEmpty() ? h.author : juce::String ("JSFX");
        d->fileOrIdentifier = file.getFullPathName();
        d->lastFileModTime = file.getLastModificationTime();
        d->lastInfoUpdateTime = juce::Time::getCurrentTime();
        d->uniqueId = d->deprecatedUid = jsfx::uniqueIdOf (located.relative, instrument);
        d->isInstrument = instrument;
        d->numInputChannels = instrument ? 0 : 2;
        d->numOutputChannels = 2;
        results.add (std::move (d));
    };
    if (jsfx::listAsEffect (h.inputs, h.outputs))
        make (false);
    if (jsfx::listAsGenerator (h.inputs, h.tags, located.relative))
        make (true);
}

bool JsfxPluginFormat::fileMightContainThisPluginType (const juce::String& path)
{
    if (! juce::File::isAbsolutePath (path))
        return false;
    const juce::File f (path);
    if (! f.existsAsFile())
        return false;
    if (hasJsfxExtension (f))
        return true;
    // REAPER's own effects have no extension (and their .jsfx-inc / .txt / .rpl files are not effects)
    return extensionless (f) && f.getSize() < 8 * 1024 * 1024 && headLooksLikeJsfx (f);
}

juce::String JsfxPluginFormat::getNameOfPluginFromIdentifier (const juce::String& path)
{
    const auto h = readHeader (juce::File (path));
    return h.ok ? h.name : juce::File (path).getFileNameWithoutExtension();
}

bool JsfxPluginFormat::pluginNeedsRescanning (const juce::PluginDescription& d)
{
    return juce::File (d.fileOrIdentifier).getLastModificationTime() != d.lastFileModTime;
}

bool JsfxPluginFormat::doesPluginStillExist (const juce::PluginDescription& d)
{
    return juce::File (d.fileOrIdentifier).existsAsFile();
}

juce::StringArray JsfxPluginFormat::searchPathsForPlugins (const juce::FileSearchPath& directories, bool recursive, bool)
{
    juce::StringArray files;
    for (int i = 0; i < directories.getNumPaths(); ++i)
    {
        const auto dir = directories[i];
        if (! dir.isDirectory())
            continue;
        for (const auto& entry : juce::RangedDirectoryIterator (dir, recursive, "*", juce::File::findFiles, juce::File::FollowSymlinks::noCycles))
        {
            const auto& f = entry.getFile();
            if (f.getFileName().startsWithChar ('.') || entry.isHidden())
                continue;
            if (hasJsfxExtension (f) || (extensionless (f) && headLooksLikeJsfx (f)))
                files.addIfNotAlreadyThere (f.getFullPathName());
        }
    }
    return files;
}

juce::FileSearchPath JsfxPluginFormat::getDefaultLocationsToSearch() { return jsfx::roots(); }

void JsfxPluginFormat::createPluginInstance (const juce::PluginDescription& d, double sampleRate, int blockSize, PluginCreationCallback callback)
{
    installPolicyOnce();
    const juce::File file (d.fileOrIdentifier);
    if (! file.existsAsFile())
    {
        callback (nullptr, juce::String ("JSFX script not found: ") + d.fileOrIdentifier);
        return;
    }
    const auto located = locateScript (file);
    auto log = std::make_unique<Log>();
    ConfigPtr config (ysfx_config_new());
    ysfx_set_log_reporter (config.get(), &report);
    ysfx_set_user_data (config.get(), (intptr_t) log.get());
    ysfx_register_builtin_audio_formats (config.get());
    // import / data roots: REAPER's layout (Effects + Data beside it) when the script is in one, else its JSFX root
    // and that root's Data folder
    ysfx_guess_file_roots (config.get(), file.getFullPathName().toRawUTF8());
    if (fromUtf8 (ysfx_get_import_root (config.get())).isEmpty())
    {
        const auto roots = jsfx::roots();
        const auto root = located.root >= 0 && located.root < roots.getNumPaths() ? roots[located.root] : file.getParentDirectory();
        ysfx_set_import_root (config.get(), root.getFullPathName().toRawUTF8());
        ysfx_set_data_root (config.get(), root.getChildFile ("Data").getFullPathName().toRawUTF8());
    }
    FxPtr fx (ysfx_new (config.get()));
    if (! ysfx_load_file (fx.get(), file.getFullPathName().toRawUTF8(), 0) || ! ysfx_compile (fx.get(), ysfx_compile_no_gfx))
    {
        const auto why = log->errors.isEmpty() ? juce::String ("the script did not compile") : log->errors[0];
        callback (nullptr, "JSFX " + d.name + ": " + why);
        return;
    }
    const auto source = hashOfFile (file);
    auto instance = std::make_unique<jsfx::Instance> (d, std::move (log), std::move (config), std::move (fx), source,
                                                      fromUtf8 (located.relative.c_str()));
    instance->setRateAndBufferSizeDetails (sampleRate, blockSize);
    callback (std::move (instance), {});
}
// --- --selftest / --jsfxbench --------------------------------------------------------------------------------------
namespace jsfx
{
namespace
{
// The reference render's input (tools/jsfx_ref.cpp makes the same): two sines left, a sine and noise right
void testInput (float* l, float* r, int frames, std::uint32_t& lcg, long start)
{
    for (int i = 0; i < frames; ++i)
    {
        const double n = (double) (start + i);
        lcg = lcg * 1664525u + 1013904223u;
        const double noise = (double) (lcg >> 8) / 16777216.0 * 0.2 - 0.1;
        l[i] = (float) (0.6 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * n / 48000.0)
                        + 0.3 * std::sin (2.0 * juce::MathConstants<double>::pi * 3317.0 * n / 48000.0));
        r[i] = (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * n / 48000.0 + 0.3) + noise);
    }
}

std::uint64_t hashFloats (std::uint64_t h, float v)
{
    std::uint32_t bits;
    std::memcpy (&bits, &v, 4);
    for (int k = 0; k < 4; ++k)
    {
        h ^= (bits >> (8 * k)) & 0xff;
        h *= 1099511628211ull;
    }
    return h;
}

struct Failure : std::runtime_error
{
    using std::runtime_error::runtime_error;
};
void require (bool ok, const juce::String& what)
{
    if (! ok)
        throw Failure (what.toStdString());
}

std::unique_ptr<juce::AudioPluginInstance> make (const juce::PluginDescription& d, double rate, int block, juce::String& error)
{
    JsfxPluginFormat format;
    auto instance = format.createInstanceFromDescription (d, rate, block, error);
    if (instance != nullptr)
        preparePlugin (*instance, rate, block);
    return instance;
}

juce::PluginDescription describe (const juce::File& file, bool instrument)
{
    JsfxPluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, file.getFullPathName());
    for (auto* d : found)
        if (d->isInstrument == instrument)
            return *d;
    throw Failure ("not listed as " + std::string (instrument ? "a generator: " : "an effect: ") + file.getFullPathName().toStdString());
}

// LYNX's own test scripts (written for round 190od's probe; plain EEL2, no third-party code)
const char* gainScript = R"(desc:LYNX test - gain + one-pole lowpass
tags: utility test
slider1:gain_db=0<-60,24,0.1>Gain (dB)
slider2:cut=1000<20,20000,1:log>Cutoff (Hz)
slider3:mode=0<0,2,1{Off,Lowpass,Highpass}>Mode
slider4:hid=0<0,1,1>-Hidden
import lynx_test_lib.jsfx-inc
in_pin:left input
in_pin:right input
out_pin:left output
out_pin:right output
@init
pdc_delay = 32;
memo = 0;
sl = sr = 0;
@slider
g = lynx_db2lin(gain_db);
a = exp(-2*$pi*cut/srate);
memo += 1;
@sample
mode == 0 ? (
  spl0 *= g; spl1 *= g;
) : (
  sl = (1-a)*spl0 + a*sl;
  sr = (1-a)*spl1 + a*sr;
  mode == 1 ? (spl0 = sl*g; spl1 = sr*g) : (spl0 = (spl0-sl)*g; spl1 = (spl1-sr)*g);
);
@serialize
file_var(0, memo);
)";
const char* libScript = R"(@init
function lynx_db2lin(db) ( exp(db*log(10)/20) );
)";
const char* synthScript = R"(desc:LYNX test - MIDI sine synth
tags: instrument test
slider1:vol=0.5<0,1,0.01>Volume
in_pin:none
out_pin:left output
out_pin:right output
@init
freq = 0; phase = 0; env = 0; gate = 0; note = -1;
@block
while (midirecv(ofs, m1, m2, m3)) (
  st = m1 & 0xF0;
  (st == 0x90 && m3 > 0) ? (
    note = m2; freq = 440*2^((m2-69)/12); gate = m3/127;
  ) : ((st == 0x80 || (st == 0x90 && m3 == 0)) && m2 == note) ? (
    gate = 0;
  );
  midisend(ofs, m1, m2 + 12, m3);
);
@sample
env = gate > 0 ? gate : env*0.999;
s = sin(phase) * env * vol;
phase += 2*$pi*freq/srate; phase > 2*$pi ? phase -= 2*$pi;
spl0 = s; spl1 = s;
)";
// reads one file (the path is put in below) and says whether it could
const char* readerScript = R"(desc:LYNX test - file policy
in_pin:left input
out_pin:left output
@init
h = file_open("%PATH%");
got = h >= 0 ? 1 : 0;
h >= 0 ? file_close(h);
@sample
spl0 = spl0;
)";

double readVar (juce::AudioPluginInstance& p, const char* name)
{
    auto* i = asJsfx (&p);
    return i != nullptr ? ysfx_read_var (i->fx.get(), name) : -1.0;
}
} // namespace

juce::String selfTest (const juce::File& reportFolder)
{
    juce::StringArray notes;
    const auto temp = juce::File::createTempFile ("lynx-jsfx-test");
    try
    {
        installPolicyOnce();
        temp.createDirectory();
        const juce::ScopeGuard cleanup { [&temp] { temp.deleteRecursively(); } };

        // the bundled pack: written to the settings folder, MIT scripts with their licence
        installPack();
        const auto clipper = packFolder().getChildFile ("chokehold/Clipper/soft_clipper.jsfx");
        require (clipper.existsAsFile() && packFolder().getChildFile ("chokehold/LICENSE.txt").existsAsFile(), "the bundled pack was not written");
        require (installPack() == 0, "the pack was written again although it is current");

        // the scan: the pack's scripts are found in the roots; REAPER-style extensionless headers are recognised
        JsfxPluginFormat format;
        const auto files = format.searchPathsForPlugins (roots(), true, false);
        require (files.contains (clipper.getFullPathName()), "the scan did not find the bundled soft_clipper.jsfx");
        const auto plain = temp.getChildFile ("volume");
        plain.replaceWithText ("// a REAPER effect without an extension\ndesc:Plain volume\nslider1:v=0<-12,12,1>Volume\n@sample\nspl0=spl0;\n");
        require (format.fileMightContainThisPluginType (plain.getFullPathName()), "an extensionless JSFX was not recognised");
        temp.getChildFile ("README").replaceWithText ("not a script\n");
        require (! format.fileMightContainThisPluginType (temp.getChildFile ("README").getFullPathName()), "a text file was taken for a JSFX");
        const auto fx = describe (clipper, false);
        require (fx.uniqueId == uniqueIdOf ("chokehold/Clipper/soft_clipper.jsfx", false) && fx.pluginFormatName == "JSFX", "the soft clipper's id");
        {
            juce::OwnedArray<juce::PluginDescription> found;
            format.findAllTypesForFile (found, packFolder().getChildFile ("chokehold/MIDI/midi_chord_trigger.jsfx").getFullPathName());
            require (found.size() == 2 && found[0]->isInstrument != found[1]->isInstrument, "the MIDI chord trigger is listed as an effect and a generator");
            found.clear();
            format.findAllTypesForFile (found, packFolder().getChildFile ("chokehold/Generator/test_signals.jsfx").getFullPathName());
            require (found.size() == 1 && found[0]->isInstrument, "the test signal generator is a generator only");
        }
        // another computer's identifier (another absolute path, so another path hash) finds the same script
        {
            juce::KnownPluginList list;
            list.addType (fx);
            auto other = fx;
            other.fileOrIdentifier = "/elsewhere/JSFX pack/chokehold/Clipper/soft_clipper.jsfx";
            require (list.getTypeForIdentifierString (other.createIdentifierString()) == nullptr, "(test) the other identifier matched directly");
            auto found = findPortable (list, other.createIdentifierString());
            require (found != nullptr && found->fileOrIdentifier == fx.fileOrIdentifier, "a script was not found by its portable id");
        }

        // reference render: the slot (PluginEffect, LYNX's host path) against ysfx run directly, and against the
        // upstream library's own output (tools/jsfx_ref.cpp)
        constexpr int block = 512, blocks = 64;
        juce::String error;
        auto instance = make (fx, 48000.0, PluginHost::maxBlockSize, error);
        require (instance != nullptr, "the soft clipper did not load: " + error);
        require (instance->getParameters().size() == 6, "the soft clipper has 6 sliders");
        require (instance->getParameters()[2]->getText (instance->getParameters()[2]->getValue(), 32) == "2x", "an enum slider's text");
        require (instance->getParameters()[1]->getName (64).contains ("Boost"), "a slider's name");
        auto* boost = instance->getParameters()[1];
        PluginEffect slot (std::move (instance), PluginHost::maxBlockSize);
        ConfigPtr config (ysfx_config_new());
        FxPtr direct (ysfx_new (config.get()));
        require (ysfx_load_file (direct.get(), clipper.getFullPathName().toRawUTF8(), 0) && ysfx_compile (direct.get(), ysfx_compile_no_gfx), "direct compile");
        ysfx_set_sample_rate (direct.get(), 48000.0);
        ysfx_set_block_size (direct.get(), (std::uint32_t) PluginHost::maxBlockSize);
        ysfx_init (direct.get());
        std::vector<float> l (block), r (block), dl (block), dr (block);
        std::uint32_t lcg = 12345u, lcg2 = 12345u;
        auto hash = 14695981039346656037ull;
        auto maxDirect = 0.0f, maxUpstream = 0.0f;
        long frame = 0;
        std::size_t point = 0;
        EffectParams params {};
        for (int b = 0; b < blocks; ++b)
        {
            if (b == 32)
            {
                slot.setParameter (1, 0.25f); // Boost: 12 dB of 0 … 48
                ysfx_slider_set_value (direct.get(), 1, 12.0, true);
            }
            testInput (l.data(), r.data(), block, lcg, frame);
            testInput (dl.data(), dr.data(), block, lcg2, frame);
            slot.process (l.data(), r.data(), block, params, 120.0);
            ysfx_time_info_t info {};
            info.tempo = 120.0;
            info.time_signature[0] = info.time_signature[1] = 4;
            ysfx_set_time_info (direct.get(), &info);
            const float* ins[2] = { dl.data(), dr.data() };
            float* outs[2] = { dl.data(), dr.data() };
            ysfx_process_float (direct.get(), ins, outs, 2, 2, (std::uint32_t) block);
            for (int i = 0; i < block; ++i, ++frame)
            {
                maxDirect = juce::jmax (maxDirect, std::abs (l[(std::size_t) i] - dl[(std::size_t) i]), std::abs (r[(std::size_t) i] - dr[(std::size_t) i]));
                hash = hashFloats (hashFloats (hash, l[(std::size_t) i]), r[(std::size_t) i]);
                if (frame % 61 == 0 && point < std::size (jsfxref::softClipper))
                {
                    maxUpstream = juce::jmax (maxUpstream, std::abs (l[(std::size_t) i] - jsfxref::softClipper[point][0]),
                                              std::abs (r[(std::size_t) i] - jsfxref::softClipper[point][1]));
                    ++point;
                }
            }
        }
        require (std::abs (boost->getValue() - 0.25f) < 1.0e-6f, "the Boost parameter reads back");
        require (maxDirect == 0.0f, "the slot differs from ysfx run directly (max " + juce::String (maxDirect) + ")");
        require (point == std::size (jsfxref::softClipper), "reference points");
        require (maxUpstream <= 1.0e-5f, "the slot differs from upstream ysfx's reference (max " + juce::String (maxUpstream) + ")");
        notes.add ("reference: " + juce::String ((int) point) + " points, max |slot - upstream| " + juce::String (maxUpstream, 9)
                   + (hash == jsfxref::softClipperHash ? ", all 32768 frames bit-identical" : ", hash differs (another libm)"));

        // state: what the slot saves loads into a new instance with the same sound; the relative id and the source hash
        // travel with it
        const auto saved = slot.captureState();
        require (saved.has_value(), "the state was not captured");
        {
            juce::MemoryBlock bytes;
            bytes.fromBase64Encoding (juce::String (*saved));
            SavedState st;
            require (decodeState (bytes.getData(), bytes.getSize(), st) && st.relative == "chokehold/Clipper/soft_clipper.jsfx" && st.source != 0,
                     "the saved state's id / hash");
            require (st.sliders.size() == 6 && std::abs (st.sliders[1].second - 12.0) < 1.0e-9, "the saved sliders");
        }
        auto again = make (fx, 48000.0, PluginHost::maxBlockSize, error);
        require (again != nullptr, "second instance: " + error);
        PluginEffect restored (std::move (again), PluginHost::maxBlockSize);
        require (restored.restoreState (*saved), "the state was not restored");
        require (std::abs (restored.getInstance().getParameters()[1]->getValue() - 0.25f) < 1.0e-6f, "a restored slider");
        require (! scriptChanged (*asJsfx (&restored.getInstance())), "the restored script counts as changed");

        // PDC from pdc_delay, @serialize, import, a log slider, a hidden slider (own test script)
        const auto gainFile = temp.getChildFile ("lynx_test_gain.jsfx");
        gainFile.replaceWithText (gainScript);
        temp.getChildFile ("lynx_test_lib.jsfx-inc").replaceWithText (libScript);
        auto gain = make (describe (gainFile, false), 48000.0, PluginHost::maxBlockSize, error);
        require (gain != nullptr, "the gain test script did not load: " + error);
        require (gain->getLatencySamples() == 32, "pdc_delay 32 is the plugin's latency (" + juce::String (gain->getLatencySamples()) + ")");
        require (gain->getParameters().size() == 4 && ! sliderVisible (*asJsfx (gain.get()), 3) && sliderVisible (*asJsfx (gain.get()), 0),
                 "a '-' slider is hidden but kept");
        {
            auto* cut = gain->getParameters()[1];
            cut->setValueNotifyingHost (0.5f); // log 20 … 20000: the middle is ~632 Hz
            juce::AudioBuffer<float> buffer (2, block);
            juce::MidiBuffer midi;
            buffer.clear();
            gain->processBlock (buffer, midi);
            const auto hz = sliderValue (*asJsfx (gain.get()), 1);
            require (hz > 600.0 && hz < 660.0, "a log slider's middle (" + juce::String (hz) + " Hz)");
        }
        PluginEffect gainSlot (std::move (gain), PluginHost::maxBlockSize);
        require (gainSlot.latencySamples() == 32, "the slot's latency");
        {
            const auto st = gainSlot.captureState();
            juce::MemoryBlock bytes;
            bytes.fromBase64Encoding (juce::String (*st));
            SavedState decoded;
            require (decodeState (bytes.getData(), bytes.getSize(), decoded) && decoded.serialized.size() == 4, "@serialize data (file_var: 4 bytes)");
        }
        // a script changed after the project was saved: it still loads, and the panel says so
        {
            const auto st = gainSlot.captureState();
            gainFile.replaceWithText (juce::String (gainScript) + "// edited\n");
            auto edited = make (describe (gainFile, false), 48000.0, PluginHost::maxBlockSize, error);
            require (edited != nullptr, "the edited script: " + error);
            juce::MemoryBlock bytes;
            bytes.fromBase64Encoding (juce::String (*st));
            edited->setStateInformation (bytes.getData(), (int) bytes.getSize());
            require (scriptChanged (*asJsfx (edited.get())), "a changed script is not reported");
        }

        // MIDI in and out: a generator plays the note it gets (440 Hz for A4) and sends it on an octave up
        const auto synthFile = temp.getChildFile ("lynx_test_synth.jsfx");
        synthFile.replaceWithText (synthScript);
        auto synth = make (describe (synthFile, true), 48000.0, 256, error);
        require (synth != nullptr, "the synth test script did not load: " + error);
        {
            PluginInstrument channel (std::move (synth), 256);
            channel.noteOn (69, 1.0f);
            std::vector<float> sl (256), sr (256);
            int crossings = 0;
            float last = 0.0f, peak = 0.0f;
            std::vector<Instrument::MidiOutNote> sent (16);
            int sentCount = 0;
            for (int b = 0; b < 188; ++b) // 1 s
            {
                std::fill (sl.begin(), sl.end(), 0.0f);
                std::fill (sr.begin(), sr.end(), 0.0f);
                channel.render (sl.data(), sr.data(), 256, 1.0f, 1.0f);
                sentCount += channel.takeMidiOutput (sent.data() + sentCount, (int) sent.size() - sentCount);
                for (const auto v : sl)
                {
                    if (b > 0 && last <= 0.0f && v > 0.0f)
                        ++crossings;
                    last = v;
                    peak = juce::jmax (peak, std::abs (v));
                }
            }
            require (crossings >= 436 && crossings <= 442, "the generator's pitch (" + juce::String (crossings) + " cycles in a second, 440 expected)");
            require (peak > 0.3f, "the generator's level");
            require (sentCount >= 1 && sent[0].key == 81 && sent[0].velocity > 0.0f, "the script's MIDI out (note 81 expected)");
        }

        // the file policy: a file outside the roots is refused, the same file is allowed once its folder is the
        // open project's
        const auto secretFolder = temp.getChildFile ("elsewhere");
        secretFolder.createDirectory();
        const auto secret = secretFolder.getChildFile ("secret.txt");
        secret.replaceWithText ("not for scripts\n");
        const auto readerFile = temp.getChildFile ("lynx_test_reader.jsfx");
        readerFile.replaceWithText (juce::String (readerScript).replace ("%PATH%", secret.getFullPathName().replaceCharacter ('\\', '/')));
        require (mayRead (clipper) && ! mayRead (secret), "the policy's folders");
        {
            auto reader = make (describe (readerFile, false), 48000.0, 512, error);
            require (reader != nullptr, "the reader test script: " + error);
            require (readVar (*reader, "got") == 0.0, "a script read a file outside the JSFX folders");
        }
        setProjectFolder (secretFolder);
        {
            auto reader = make (describe (readerFile, false), 48000.0, 512, error);
            require (reader != nullptr && readVar (*reader, "got") == 1.0, "a script could not read a file in the project's folder");
        }
        setProjectFolder ({});
        require (! mayRead (secret), "the project folder stayed allowed");

        // a script that does not compile fails with the compiler's message
        {
            const auto broken = temp.getChildFile ("lynx_test_broken.jsfx");
            broken.replaceWithText ("desc:LYNX test - broken\n@sample\nspl0 = (;\n");
            auto none = make (describe (broken, false), 48000.0, 512, error);
            require (none == nullptr && error.startsWith ("JSFX"), "a broken script: " + error);
        }

        // Save a copy for 0.1.30 / 0.1.28: the JSFX slots become empty, with one line
        {
            Song song;
            song.mixer.resize (2);
            EffectSlot jsfxSlot;
            jsfxSlot.kind = EffectKind::plugin;
            jsfxSlot.plugin.id = fx.createIdentifierString().toStdString();
            jsfxSlot.plugin.name = "Soft Clipper";
            song.mixer[1].effects.push_back (jsfxSlot);
            ChannelDef c;
            c.instrument = InstrumentKind::plugin;
            c.plugin.id = describe (synthFile, true).createIdentifierString().toStdString();
            c.plugin.name = "Synth";
            song.channels.push_back (c);
            for (const auto* release : { "0.1.30", "0.1.28" })
            {
                const auto copy = olderlynx::forRelease (song, release);
                require (copy.ok, "the copy for " + juce::String (release));
                const auto json = juce::JSON::parse (copy.json);
                require (json["mixer"][1]["effects"][0]["kind"].toString() == "utility" && ! (bool) json["mixer"][1]["effects"][0]["on"],
                         "the JSFX insert of the copy for " + juce::String (release));
                require (json["channels"][0]["instrument"].toString() == "sampler", "the JSFX channel of the copy for " + juce::String (release));
                const auto lines = std::count_if (copy.lost.begin(), copy.lost.end(), [] (const olderlynx::Loss& l) { return juce::String (l.text).contains ("JSFX"); });
                require (lines == 1, "one line about JSFX in the copy for " + juce::String (release));
            }
        }
    }
    catch (const std::exception& e)
    {
        temp.deleteRecursively();
        return "ERROR " + juce::String::fromUTF8 (e.what());
    }
    if (reportFolder != juce::File())
        reportFolder.getChildFile ("jsfx-selftest.txt").replaceWithText (notes.joinIntoString ("\n") + "\n");
    return "OK (" + notes.joinIntoString ("; ") + ")";
}

juce::String bench (const juce::File& output)
{
    installPolicyOnce();
    installPack();
    constexpr int block = 512, blocks = 400;
    constexpr double rate = 48000.0;
    juce::StringArray lines;
    lines.add ("# JSFX CPU per instance (round 190pj): ns per stereo frame at 48 kHz, 512-frame blocks, " + juce::String (blocks)
               + " blocks of the test signal; share = % of one core in real time");
    lines.add ("script\tns_per_frame\tshare_pct");
    std::vector<float> l (block), r (block);
    // the floor: a plain copy of the same buffers
    {
        std::uint32_t lcg = 1;
        const auto t0 = std::chrono::steady_clock::now();
        float sink = 0;
        for (int b = 0; b < blocks; ++b)
        {
            testInput (l.data(), r.data(), block, lcg, (long) b * block);
            for (int i = 0; i < block; ++i)
                sink += l[(std::size_t) i] * 0.5f + r[(std::size_t) i] * 0.5f;
        }
        const auto ns = (double) std::chrono::duration_cast<std::chrono::nanoseconds> (std::chrono::steady_clock::now() - t0).count();
        lines.add ("(test signal + copy)\t" + juce::String (ns / (blocks * block), 2) + "\t" + juce::String (ns / (blocks * block) * rate / 1.0e7, 3)
                   + (sink == 12345.0f ? " " : ""));
    }
    JsfxPluginFormat format;
    auto files = format.searchPathsForPlugins (juce::FileSearchPath (packFolder().getFullPathName()), true, false);
    files.sort (true);
    for (const auto& path : files)
    {
        juce::OwnedArray<juce::PluginDescription> found;
        format.findAllTypesForFile (found, path);
        if (found.isEmpty())
            continue;
        juce::String error;
        auto p = make (*found[0], rate, block, error);
        const auto name = locateScript (juce::File (path)).relative;
        if (p == nullptr)
        {
            lines.add (fromUtf8 (name.c_str()) + "\tERROR " + error);
            continue;
        }
        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;
        std::uint32_t lcg = 1;
        for (int b = 0; b < 20; ++b) // (warm up: lazy memory, first @block)
        {
            testInput (buffer.getWritePointer (0), buffer.getWritePointer (1), block, lcg, (long) b * block);
            p->processBlock (buffer, midi);
        }
        double ns = 0;
        for (int b = 0; b < blocks; ++b)
        {
            testInput (buffer.getWritePointer (0), buffer.getWritePointer (1), block, lcg, (long) b * block);
            midi.clear();
            const auto t0 = std::chrono::steady_clock::now();
            p->processBlock (buffer, midi);
            ns += (double) std::chrono::duration_cast<std::chrono::nanoseconds> (std::chrono::steady_clock::now() - t0).count();
        }
        const auto perFrame = ns / (blocks * block);
        lines.add (fromUtf8 (name.c_str()) + "\t" + juce::String (perFrame, 1) + "\t" + juce::String (perFrame * rate / 1.0e7, 3));
    }
    output.replaceWithText (lines.joinIntoString ("\n") + "\n");
    return "OK " + juce::String (files.size()) + " scripts";
}
} // namespace jsfx
} // namespace lynx
