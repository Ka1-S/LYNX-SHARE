// ARA 2 hosting (round 190kx): see AraHost.h, core/AraLink.h, core/AraRegions.h.
//
// Written from Celemony's ARA SDK 2.1 documentation (ARA_Library/html_docs: the model graph, "ARA2HowTo_Host1/2",
// "ARA2HowTo_Threads") and JUCE 8's ARA hosting classes (juce_ARAHosting.h). Rules it keeps:
//  - every model edit happens on the message thread between beginEditing / endEditing (ARAEditGuard);
//  - playback regions are added to or removed from a plugin instance's renderers only while that instance is not
//    prepared for playback: the instance is released (releaseResources) and prepared again around it, under its callback
//    lock, so the audio thread meanwhile renders silence for that channel (its render() only try-locks);
//  - plugin extension instances (our bound plugin instances) go before their document controller, and the model objects
//    before the document controller: everything that refers to a document controller holds it shared;
//  - the document's archive is given back after the model objects it describes exist again (same persistent ids), in the
//    same edit (restoreObjectsFromArchive, ARA 2.0 Final).

#include "engine/AraHost.h"

#include "core/AraRegions.h"
#include "core/Instruments.h"
#include "core/Song.h"
#include "engine/PluginHost.h"
#include "platform/Paths.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <set>

#if JUCE_PLUGINHOST_ARA && (JUCE_PLUGINHOST_VST3 || JUCE_PLUGINHOST_AU) && (JUCE_MAC || JUCE_WINDOWS || JUCE_LINUX)
 #define LYNX_ARA 1
 #include <ARA_Library/Dispatch/ARAHostDispatch.h>
#else
 #define LYNX_ARA 0
#endif

namespace lynx
{
bool AraHost::isAra (const juce::PluginDescription& d) noexcept
{
#if LYNX_ARA
    return d.hasARAExtension;
#else
    juce::ignoreUnused (d);
    return false;
#endif
}

std::vector<juce::PluginDescription> AraHost::araPlugins (PluginHost& plugins)
{
    std::vector<juce::PluginDescription> out;
    for (const auto& d : plugins.getKnownPlugins().getTypes())
        if (isAra (d))
            out.push_back (d);
    std::sort (out.begin(), out.end(), [] (const auto& a, const auto& b) { return a.name.compareNatural (b.name) < 0; });
    return out;
}

#if ! LYNX_ARA
// ---- a build without the ARA SDK: nothing is hosted -----------------------------------------------------------------
struct AraHost::Impl
{
};
AraHost::AraHost (PluginHost&, SampleProvider) {}
AraHost::~AraHost() = default;
bool AraHost::available() noexcept { return false; }
bool AraHost::isAraInstrument (const Instrument*) noexcept { return false; }
void AraHost::sync (const Song&, double) {}
void AraHost::reset() {}
std::shared_ptr<Instrument> AraHost::instrumentFor (const ChannelDef&, double) { return nullptr; }
juce::AudioPluginInstance* AraHost::pluginOf (std::uint64_t) { return nullptr; }
std::shared_ptr<void> AraHost::ownerOf (std::uint64_t) { return nullptr; }
void AraHost::showInEditor (std::uint64_t, const std::vector<std::uint64_t>&) {}
std::vector<AraDocumentArchive> AraHost::archives() { return {}; }
bool AraHost::analysing() { return false; }
int AraHost::documentCount() const { return 0; }
int AraHost::instanceCount() const { return 0; }
#else

namespace
{
namespace HM = juce::ARAHostModel;

// what the host did and why something failed, one line each, in the settings folder's ara.log (diagnostics; only written
// while ARA is in use)
void logLine (const juce::String& line)
{
    platform::settingsFile ("ara.log").appendText (juce::Time::getCurrentTime().toString (true, true, true, true) + " " + line + "\n");
}

// ---- what the host refs point to --------------------------------------------------------------------------------------
struct SourceData // an AudioSource's host ref: the sample it reads (immutable, shared with its readers)
{
    std::shared_ptr<const SampleData> sample;
    std::atomic<int> analysing { 0 }; // 1 while the plugin reports analysis in progress
};

struct Reader
{
    std::shared_ptr<const SampleData> sample;
    bool use64 = false;
};

struct ContentReader
{
    ARA::ARAContentType type {};
    std::vector<ARA::ARAContentTempoEntry> tempos;
    std::vector<ARA::ARAContentBarSignature> bars;
};

// ---- the host side interfaces ------------------------------------------------------------------------------------------
class AudioAccess final : public ARA::Host::AudioAccessControllerInterface
{
public:
    ARA::ARAAudioReaderHostRef createAudioReaderForSource (ARA::ARAAudioSourceHostRef source, bool use64BitSamples) noexcept override
    {
        auto* data = reinterpret_cast<SourceData*> (source);
        auto* reader = new Reader { data != nullptr ? data->sample : nullptr, use64BitSamples };
        return reinterpret_cast<ARA::ARAAudioReaderHostRef> (reader);
    }

    // Any thread (a plugin's analysis threads read at the same time): each reader reads its own immutable sample.
    bool readAudioSamples (ARA::ARAAudioReaderHostRef ref, ARA::ARASamplePosition position, ARA::ARASampleCount count,
                           void* const buffers[]) noexcept override
    {
        const auto* reader = reinterpret_cast<const Reader*> (ref);
        if (reader == nullptr || reader->sample == nullptr || count < 0 || position < 0)
            return false;
        const auto& s = *reader->sample;
        const auto frames = (ARA::ARASamplePosition) s.frames();
        const auto channels = s.isStereo() ? 2 : 1;
        const float* src[2] = { s.leftData(), s.rightData() };
        for (int ch = 0; ch < channels; ++ch)
        {
            for (ARA::ARASampleCount i = 0; i < count; ++i)
            {
                const auto at = position + i;
                const auto v = at < frames ? src[ch][at] : 0.0f;
                if (reader->use64)
                    static_cast<double*> (buffers[ch])[i] = (double) v;
                else
                    static_cast<float*> (buffers[ch])[i] = v;
            }
        }
        return true;
    }

    void destroyAudioReader (ARA::ARAAudioReaderHostRef ref) noexcept override { delete reinterpret_cast<Reader*> (ref); }
};

// An archive being read: its bytes and the document archive id it was written with (ARA wants it back: a plugin asks
// for it to check the archive's version; Melodyne reads it, so it must never be null)
struct ArchiveIn
{
    juce::MemoryBlock data;
    std::string archiveId;
};

class Archiving final : public ARA::Host::ArchivingControllerInterface
{
public:
    ARA::ARASize getArchiveSize (ARA::ARAArchiveReaderHostRef ref) noexcept override
    {
        return (ARA::ARASize) reinterpret_cast<const ArchiveIn*> (ref)->data.getSize();
    }
    bool readBytesFromArchive (ARA::ARAArchiveReaderHostRef ref, ARA::ARASize position, ARA::ARASize length, ARA::ARAByte buffer[]) noexcept override
    {
        const auto* block = &reinterpret_cast<const ArchiveIn*> (ref)->data;
        if (position + length > block->getSize())
            return false;
        std::memcpy (buffer, static_cast<const char*> (block->getData()) + position, length);
        return true;
    }
    bool writeBytesToArchive (ARA::ARAArchiveWriterHostRef ref, ARA::ARASize position, ARA::ARASize length, const ARA::ARAByte buffer[]) noexcept override
    {
        auto* out = reinterpret_cast<juce::MemoryOutputStream*> (ref);
        return out->setPosition ((juce::int64) position) && out->write (buffer, length);
    }
    void notifyDocumentArchivingProgress (float) noexcept override {}
    void notifyDocumentUnarchivingProgress (float) noexcept override {}
    ARA::ARAPersistentID getDocumentArchiveID (ARA::ARAArchiveReaderHostRef ref) noexcept override
    {
        return reinterpret_cast<const ArchiveIn*> (ref)->archiveId.c_str();
    }
};

// the song's tempo and meter, as the musical context's content (set by sync before the plugin reads it)
struct Timeline
{
    std::mutex lock;
    std::vector<ara::TempoEntry> tempos { { 0.0, 0.0 }, { 2.0, 4.0 } };
    int beats = 4, unit = 4;
};

class ContentAccess final : public ARA::Host::ContentAccessControllerInterface
{
public:
    explicit ContentAccess (std::shared_ptr<Timeline> t) : timeline (std::move (t)) {}

    bool isMusicalContextContentAvailable (ARA::ARAMusicalContextHostRef, ARA::ARAContentType type) noexcept override
    {
        return type == ARA::kARAContentTypeTempoEntries || type == ARA::kARAContentTypeBarSignatures;
    }
    ARA::ARAContentGrade getMusicalContextContentGrade (ARA::ARAMusicalContextHostRef, ARA::ARAContentType) noexcept override
    {
        return ARA::kARAContentGradeApproved; // (the user's own tempo and meter)
    }
    ARA::ARAContentReaderHostRef createMusicalContextContentReader (ARA::ARAMusicalContextHostRef, ARA::ARAContentType type,
                                                                    const ARA::ARAContentTimeRange*) noexcept override
    {
        auto* r = new ContentReader();
        r->type = type;
        const std::lock_guard<std::mutex> hold (timeline->lock);
        if (type == ARA::kARAContentTypeTempoEntries)
            for (const auto& e : timeline->tempos)
                r->tempos.push_back ({ e.seconds, e.quarters });
        else if (type == ARA::kARAContentTypeBarSignatures)
            r->bars.push_back ({ timeline->beats, timeline->unit, 0.0 });
        return reinterpret_cast<ARA::ARAContentReaderHostRef> (r);
    }
    bool isAudioSourceContentAvailable (ARA::ARAAudioSourceHostRef, ARA::ARAContentType) noexcept override { return false; }
    ARA::ARAContentGrade getAudioSourceContentGrade (ARA::ARAAudioSourceHostRef, ARA::ARAContentType) noexcept override { return ARA::kARAContentGradeInitial; }
    ARA::ARAContentReaderHostRef createAudioSourceContentReader (ARA::ARAAudioSourceHostRef, ARA::ARAContentType, const ARA::ARAContentTimeRange*) noexcept override
    {
        return nullptr;
    }
    ARA::ARAInt32 getContentReaderEventCount (ARA::ARAContentReaderHostRef ref) noexcept override
    {
        const auto* r = reinterpret_cast<const ContentReader*> (ref);
        return r == nullptr ? 0 : (ARA::ARAInt32) (r->type == ARA::kARAContentTypeTempoEntries ? r->tempos.size() : r->bars.size());
    }
    const void* getContentReaderDataForEvent (ARA::ARAContentReaderHostRef ref, ARA::ARAInt32 index) noexcept override
    {
        const auto* r = reinterpret_cast<const ContentReader*> (ref);
        if (r == nullptr || index < 0)
            return nullptr;
        if (r->type == ARA::kARAContentTypeTempoEntries)
            return (std::size_t) index < r->tempos.size() ? &r->tempos[(std::size_t) index] : nullptr;
        return (std::size_t) index < r->bars.size() ? &r->bars[(std::size_t) index] : nullptr;
    }
    void destroyContentReader (ARA::ARAContentReaderHostRef ref) noexcept override { delete reinterpret_cast<ContentReader*> (ref); }

private:
    std::shared_ptr<Timeline> timeline;
};

class ModelUpdates final : public ARA::Host::ModelUpdateControllerInterface
{
public:
    void notifyAudioSourceAnalysisProgress (ARA::ARAAudioSourceHostRef ref, ARA::ARAAnalysisProgressState state, float) noexcept override
    {
        if (auto* data = reinterpret_cast<SourceData*> (ref))
            data->analysing.store (state == ARA::kARAAnalysisProgressCompleted ? 0 : 1, std::memory_order_relaxed);
    }
    void notifyAudioSourceContentChanged (ARA::ARAAudioSourceHostRef, const ARA::ARAContentTimeRange*, ARA::ContentUpdateScopes) noexcept override {}
    void notifyAudioModificationContentChanged (ARA::ARAAudioModificationHostRef, const ARA::ARAContentTimeRange*, ARA::ContentUpdateScopes) noexcept override
    {
        edits.fetch_add (1, std::memory_order_relaxed);
    }
    void notifyPlaybackRegionContentChanged (ARA::ARAPlaybackRegionHostRef, const ARA::ARAContentTimeRange*, ARA::ContentUpdateScopes) noexcept override {}
    std::atomic<std::uint64_t> edits { 0 };
};

class Playback final : public ARA::Host::PlaybackControllerInterface
{
public:
    explicit Playback (std::function<void (int, double)>* hook) : request (hook) {}
    void requestStartPlayback() noexcept override { post (0, 0.0); }
    void requestStopPlayback() noexcept override { post (1, 0.0); }
    void requestSetPlaybackPosition (ARA::ARATimePosition seconds) noexcept override { post (2, seconds); }
    void requestSetCycleRange (ARA::ARATimePosition, ARA::ARATimeDuration) noexcept override {}
    void requestEnableCycle (bool) noexcept override {}

private:
    void post (int what, double seconds)
    {
        auto* hook = request;
        juce::MessageManager::callAsync ([alive = alive, hook, what, seconds] {
            if (*alive && hook != nullptr && *hook)
                (*hook) (what, seconds);
        });
    }

public:
    std::function<void (int, double)>* request = nullptr;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};

// ---- the document controller, shared by everything that refers to it ---------------------------------------------------
// It also keeps the plugin's module loaded until the controller is gone: the ARA factory is released (uninitialised,
// its COM object released) after the controller, by code in the plugin's module, which unloads with its last instance.
// So the instance the factory was taken from stays, unbound and never processed, until then (round 190kx: without it,
// the last channel's instance going first unloaded Melodyne's module and the factory's release crashed).
struct Controller
{
    std::unique_ptr<juce::AudioPluginInstance> keeper;
    std::unique_ptr<juce::ARAHostDocumentController> dc;
    ARA::Host::DocumentController& get() const { return dc->getDocumentController(); }
    std::shared_ptr<bool> playbackAlive;
    ~Controller()
    {
        if (playbackAlive != nullptr)
            *playbackAlive = false;
        dc.reset();     // (the document controller and, with it, the factory)
        keeper.reset(); // (then the module may unload)
    }
};

// One bound plugin instance (an ARA channel's): the channel's instrument renders with it, its window shows its editor.
// It keeps the document controller alive until it is gone itself.
struct Binding
{
    std::shared_ptr<Controller> controller;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    HM::PlaybackRendererInterface playback;
    HM::EditorRendererInterface editor;
    const ARA::ARAPlugInExtensionInstance* raw = nullptr;
    double sampleRate = 0.0;
    std::atomic<bool> crashed { false };
    std::atomic<bool> ready { false }; // prepared for playback (false while its regions change)
    std::atomic<bool> editorOpen { false }; // its editor is open (the heartbeat looks, on the message thread)
    std::string name;

    ~Binding()
    {
        // the renderers' registries first (they remove their regions from the instance), then the instance, then (with
        // the last reference) the document controller
        ready = false;
        if (plugin != nullptr)
            plugin->releaseResources();
        playback = {};
        editor = {};
        plugin.reset();
    }

    // Region set changes: the instance unprepared around `fn`, the audio thread kept out by the callback lock
    template <typename Fn>
    void unprepared (Fn&& fn)
    {
        const juce::ScopedLock hold (plugin->getCallbackLock());
        ready = false;
        plugin->releaseResources();
        fn();
        preparePlugin (*plugin, sampleRate, PluginHost::maxBlockSize);
        ready = true;
    }
};

// The channel's instrument: the bound instance's playback renderer, played from the song's timeline (the play head).
class AraInstrument final : public Instrument
{
public:
    explicit AraInstrument (std::shared_ptr<Binding> b) : binding (std::move (b))
    {
        buffer.setSize (juce::jmax (2, binding->plugin->getTotalNumOutputChannels(), binding->plugin->getTotalNumInputChannels()),
                        PluginHost::maxBlockSize);
        midi.ensureSize (64);
    }

    void prepare (double) noexcept override {}
    void noteOn (int, float) noexcept override {}   // (the clips' notes: the plugin plays its regions by itself)
    void noteOff (int) noexcept override {}
    void allNotesOff() noexcept override {}
    // Rendered while the song plays, or while its editor is open (it previews the notes clicked there); otherwise idle: a
    // stopped song with ARA channels costs no plugin calls (round 190kx: +3.4 % of a core with Melodyne before)
    bool idle() const noexcept override
    {
        return ! hostTimeline().playing.load (std::memory_order_relaxed) && ! binding->editorOpen.load (std::memory_order_relaxed);
    }
    int latencySamples() const noexcept override { return binding->plugin->getLatencySamples(); }
    void transportReset() noexcept override {}

    void render (float* left, float* right, int n, float gl, float gr) noexcept override
    {
        const auto offset = clock.start (hostTimeline().chunk.load (std::memory_order_relaxed));
        clock.advance (n);
        if (n <= 0 || binding->crashed.load (std::memory_order_relaxed))
            return;
        auto& plugin = *binding->plugin;
        const juce::CriticalSection::ScopedTryLockType lock (plugin.getCallbackLock());
        if (! lock.isLocked() || ! binding->ready.load (std::memory_order_acquire))
            return; // (its regions are being changed: silent for this block)
        // (an audio clip plays its file at unity at FL's default channel volume, as the channel's sampler does:
        // core/Instruments.cpp audioClipUnity)
        static const float unity = 1.0f / channelGain (audioClipUnityVolume);
        gl *= unity;
        gr *= unity;
        for (int at = 0; at < n;)
        {
            const auto count = juce::jmin (n - at, buffer.getNumSamples());
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), count);
            view.clear();
            midi.clear();
            setHostBlockOffset (offset + at);
            const auto code = processGuarded (plugin, view, midi, binding->crashed, binding->name);
            setHostBlockOffset (0);
            if (code != 0)
                return;
            const auto* l = view.getReadPointer (0);
            const auto* r = view.getNumChannels() > 1 && plugin.getTotalNumOutputChannels() > 1 ? view.getReadPointer (1) : l;
            for (int i = 0; i < count; ++i)
            {
                left[at + i] += l[i] * gl;
                right[at + i] += r[i] * gr;
            }
            at += count;
        }
    }

    std::shared_ptr<Binding> binding;

private:
    juce::AudioBuffer<float> buffer;
    juce::MidiBuffer midi;
    HostBlockClock clock;
};

// One ARA channel's model objects
struct Chan
{
    std::uint64_t uid = 0;
    std::string path;
    std::unique_ptr<SourceData> data;
    std::unique_ptr<HM::AudioSource> source;
    std::unique_ptr<HM::AudioModification> modification;
    std::unique_ptr<HM::RegionSequence> sequence;
    std::map<std::uint64_t, std::unique_ptr<HM::PlaybackRegion>> regions; // by clip uid
    std::map<std::uint64_t, ara::Region> placed;
    std::shared_ptr<Binding> binding;
    std::shared_ptr<Instrument> instrument; // (the same one for every rack build)
    std::string name;
    int order = 0;
    bool stretchOk = false; // the plugin time-stretches (ARA transformation flag)
    bool seen = false;      // (sync's mark)
};

// One plugin's document
struct Doc
{
    std::string pluginId;
    juce::PluginDescription description;
    std::shared_ptr<Controller> controller;
    std::shared_ptr<Timeline> timeline;
    std::unique_ptr<HM::MusicalContext> context;
    std::map<std::uint64_t, std::unique_ptr<Chan>> channels;
    ModelUpdates* updates = nullptr; // (owned by the controller)
    ARA::ARAPlaybackTransformationFlags transformations = 0;
    std::vector<ARA::ARAContentType> analyzeable; // what the plugin analyses (its factory says)
    std::string archiveId;                         // the factory's document archive id (what its archives are written as)

    ~Doc()
    {
        // (the plugin's transport requests stop here: the controller may outlive the host, held by a playing instance)
        if (controller != nullptr && controller->playbackAlive != nullptr)
            *controller->playbackAlive = false;
        // the model objects before the controller (the bindings, held by the rack too, keep it as long as they live)
        if (controller != nullptr && controller->dc != nullptr)
        {
            const juce::ARAEditGuard guard (controller->get());
            for (auto& [uid, c] : channels)
            {
                if (c->binding != nullptr)
                    c->binding->unprepared ([&c] { c->regions.clear(); });
                c->regions.clear();
                c->sequence.reset();
                c->modification.reset();
                c->source.reset();
            }
            context.reset();
        }
        channels.clear();
    }
};

ARA::ARAPlaybackRegionProperties regionProps (const ara::Region& r, const Doc& doc, const Chan& c, const char* name)
{
    auto p = HM::PlaybackRegion::getEmptyProperties();
    const auto stretch = r.stretched() && c.stretchOk;
    p.transformationFlags = stretch ? ARA::kARAPlaybackTransformationTimestretch : ARA::kARAPlaybackTransformationNoChanges;
    p.startInModificationTime = r.sourceStart;
    p.durationInModificationTime = stretch ? r.sourceDuration : r.playDuration;
    p.startInPlaybackTime = r.playStart;
    p.durationInPlaybackTime = r.playDuration;
    p.musicalContextRef = doc.context->getPluginRef();
    p.regionSequenceRef = c.sequence->getPluginRef();
    p.name = name;
    p.color = nullptr;
    return p;
}

// ARA wants a PlugInExtensionInstance's raw pointer for the editor view (JUCE's wrapper keeps it private and holds
// nothing else)
const ARA::ARAPlugInExtensionInstance* rawOf (const HM::PlugInExtensionInstance& e)
{
    static_assert (sizeof (HM::PlugInExtensionInstance) == sizeof (const ARA::ARAPlugInExtensionInstance*));
    static_assert (std::is_trivially_copyable_v<HM::PlugInExtensionInstance>);
    const ARA::ARAPlugInExtensionInstance* raw = nullptr;
    std::memcpy (&raw, &e, sizeof (raw));
    return raw;
}
} // namespace

struct AraHost::Impl : private juce::Timer
{
    // The model's heartbeat (ARA: the host calls notifyModelUpdates on the message thread for the plugin to report its
    // analysis and edits), 10 times a second while a document exists, and whether any source is still being analysed
    void timerCallback() override
    {
        if (docs.empty())
        {
            stopTimer();
            busy = false;
            return;
        }
        busy = poll();
    }
    bool poll()
    {
        bool any = false;
        for (auto& [id, doc] : docs)
        {
            auto& dc = doc->controller->get();
            dc.notifyModelUpdates();
            for (auto& [uid, c] : doc->channels)
            {
                if (c->binding != nullptr)
                    c->binding->editorOpen.store (c->binding->plugin->getActiveEditor() != nullptr, std::memory_order_relaxed);
                if (c->source == nullptr || c->data == nullptr)
                    continue;
                bool incomplete = false;
                for (const auto type : doc->analyzeable)
                    incomplete = incomplete || dc.isAudioSourceContentAnalysisIncomplete (c->source->getPluginRef(), type);
                c->data->analysing.store (incomplete ? 1 : 0, std::memory_order_relaxed);
                any = any || incomplete;
            }
        }
        return any;
    }
    void heartbeat()
    {
        if (! docs.empty() && ! isTimerRunning())
            startTimerHz (10);
    }
    std::atomic<bool> busy { false };

    PluginHost& plugins;
    SampleProvider samples;
    std::function<void (int, double)>* playbackHook = nullptr;
    std::map<std::string, std::unique_ptr<Doc>> docs; // by plugin id
    std::set<std::string> failed;                     // plugin ids that could not be hosted (not tried again)
    std::vector<AraDocumentArchive> pendingArchives;  // the song's archives, for documents not made yet
    double sampleRate = 44100.0;
    juce::String error;

    Impl (PluginHost& p, SampleProvider s) : plugins (p), samples (std::move (s)) {}

    std::unique_ptr<juce::AudioPluginInstance> createInstance (const juce::PluginDescription& d, juce::String& why)
    {
        auto instance = createRightInstance (plugins.getFormats(), d, sampleRate, PluginHost::maxBlockSize, why);
        if (instance == nullptr && why.isEmpty())
            why = "could not be loaded";
        return instance;
    }

    // (takes `first`: the instance the factory comes from stays with the controller, see Controller)
    Doc* docFor (const std::string& pluginId, const juce::PluginDescription& d, std::unique_ptr<juce::AudioPluginInstance>& first)
    {
        if (auto it = docs.find (pluginId); it != docs.end())
            return it->second.get();
        juce::ARAFactoryWrapper factory;
        juce::createARAFactoryAsync (*first, [&factory] (juce::ARAFactoryWrapper f) { factory = std::move (f); });
        if (factory.get() == nullptr)
        {
            error = d.name + ": no ARA";
            return nullptr;
        }
        auto doc = std::make_unique<Doc>();
        doc->pluginId = pluginId;
        doc->description = d;
        doc->timeline = std::make_shared<Timeline>();
        doc->transformations = factory.get()->supportedPlaybackTransformationFlags;
        if (factory.get()->documentArchiveID != nullptr)
            doc->archiveId = factory.get()->documentArchiveID;
        for (ARA::ARASize k = 0; k < factory.get()->analyzeableContentTypesCount; ++k)
            doc->analyzeable.push_back (factory.get()->analyzeableContentTypes[k]);
        auto updates = std::make_unique<ModelUpdates>();
        doc->updates = updates.get();
        auto playback = std::make_unique<Playback> (playbackHook);
        auto alive = playback->alive;
        doc->controller = std::make_shared<Controller>();
        doc->controller->keeper = std::move (first);
        doc->controller->playbackAlive = alive;
        doc->controller->dc = juce::ARAHostDocumentController::create (std::move (factory), "LYNX", std::make_unique<AudioAccess>(),
                                                                         std::make_unique<Archiving>(),
                                                                         std::make_unique<ContentAccess> (doc->timeline), std::move (updates),
                                                                         std::move (playback));
        if (doc->controller->dc == nullptr)
        {
            error = d.name + ": its ARA document could not be made";
            return nullptr;
        }
        {
            auto props = HM::MusicalContext::getEmptyProperties();
            props.name = "LYNX";
            props.orderIndex = 0;
            props.color = nullptr;
            doc->context = std::make_unique<HM::MusicalContext> (reinterpret_cast<ARA::ARAMusicalContextHostRef> (doc.get()), doc->controller->get(), props);
        }
        auto* raw = doc.get();
        docs[pluginId] = std::move (doc);
        return raw;
    }

    void setTimeline (Doc& doc, const Song& song)
    {
        bool changed = false;
        {
            const std::lock_guard<std::mutex> hold (doc.timeline->lock);
            const auto tempos = ara::tempoEntries (song);
            const auto m = song.meter();
            changed = m.beats != doc.timeline->beats || m.unit != doc.timeline->unit || tempos.size() != doc.timeline->tempos.size()
                      || tempos[1].seconds != doc.timeline->tempos[1].seconds;
            doc.timeline->tempos = tempos;
            doc.timeline->beats = m.beats;
            doc.timeline->unit = m.unit;
        }
        if (changed)
            doc.controller->get().updateMusicalContextContent (doc.context->getPluginRef(), nullptr, ARA::ContentUpdateScopes::timelineIsAffected());
    }

    // the channel's model objects (in an edit); false: its sample is not there
    bool makeChannel (Doc& doc, Chan& c, const ChannelDef& def, int order)
    {
        auto& dc = doc.controller->get();
        c.uid = def.uid;
        c.path = def.samplePath;
        c.name = def.name;
        c.order = order;
        c.stretchOk = (doc.transformations & ARA::kARAPlaybackTransformationTimestretch) != 0;
        auto sample = samples ? samples (def.samplePath) : nullptr;
        if (sample == nullptr || sample->frames() == 0)
            return false;
        c.data = std::make_unique<SourceData>();
        c.data->sample = sample;
        const auto sourceId = ara::sourceId (def), modId = ara::modificationId (def);
        auto sp = HM::AudioSource::getEmptyProperties();
        sp.name = def.name.c_str();
        sp.persistentID = sourceId.c_str();
        sp.sampleCount = (ARA::ARASampleCount) sample->frames();
        sp.sampleRate = sample->sampleRate;
        sp.channelCount = sample->isStereo() ? 2 : 1;
        sp.merits64BitSamples = false;
        c.source = std::make_unique<HM::AudioSource> (reinterpret_cast<ARA::ARAAudioSourceHostRef> (c.data.get()), dc, sp);
        c.source->enableAudioSourceSamplesAccess (true);
        auto mp = HM::AudioModification::getEmptyProperties();
        mp.name = def.name.c_str();
        mp.persistentID = modId.c_str();
        c.modification = std::make_unique<HM::AudioModification> (reinterpret_cast<ARA::ARAAudioModificationHostRef> (&c), dc, *c.source, mp);
        auto qp = HM::RegionSequence::getEmptyProperties();
        qp.name = def.name.c_str();
        qp.orderIndex = order;
        qp.musicalContextRef = doc.context->getPluginRef();
        qp.color = nullptr;
        c.sequence = std::make_unique<HM::RegionSequence> (reinterpret_cast<ARA::ARARegionSequenceHostRef> (&c), dc, qp);
        return true;
    }

    // the channel's regions as `wanted`; returns whether the set of regions changed (then the renderers must follow)
    bool placeRegions (Doc& doc, Chan& c, const std::vector<ara::Region>& wanted, std::vector<HM::PlaybackRegion*>& added,
                       std::vector<std::uint64_t>& removed)
    {
        auto& dc = doc.controller->get();
        std::set<std::uint64_t> keep;
        for (const auto& r : wanted)
        {
            keep.insert (r.clipUid);
            const auto props = regionProps (r, doc, c, c.name.c_str());
            if (auto it = c.regions.find (r.clipUid); it != c.regions.end())
            {
                if (c.placed[r.clipUid] != r)
                    it->second->update (props);
            }
            else
            {
                c.regions[r.clipUid] = std::make_unique<HM::PlaybackRegion> (reinterpret_cast<ARA::ARAPlaybackRegionHostRef> (&c.placed[r.clipUid]), dc, *c.modification, props);
                added.push_back (c.regions[r.clipUid].get());
            }
            c.placed[r.clipUid] = r;
        }
        for (const auto& [uid, region] : c.regions)
            if (! keep.contains (uid))
                removed.push_back (uid);
        return ! added.empty() || ! removed.empty();
    }

    std::shared_ptr<Binding> bind (Doc& doc, Chan& c, std::unique_ptr<juce::AudioPluginInstance> instance)
    {
        auto b = std::make_shared<Binding>();
        b->controller = doc.controller;
        b->name = doc.description.name.toStdString();
        b->sampleRate = sampleRate;
        const auto roles = ARA::kARAPlaybackRendererRole | ARA::kARAEditorRendererRole | ARA::kARAEditorViewRole;
        auto extension = doc.controller->dc->bindDocumentToPluginInstance (*instance, roles, roles);
        if (! extension.isValid())
        {
            error = doc.description.name + ": it refused the ARA document";
            return nullptr;
        }
        b->raw = rawOf (extension);
        b->playback = extension.getPlaybackRendererInterface();
        b->editor = extension.getEditorRendererInterface();
        b->plugin = std::move (instance);
        b->plugin->setPlayHead (&songPlayHead());
        for (auto& [uid, region] : c.regions)
        {
            b->playback.add (*region);
            b->editor.add (*region);
        }
        preparePlugin (*b->plugin, sampleRate, PluginHost::maxBlockSize);
        b->ready = true;
        return b;
    }

    std::vector<AraDocumentArchive> archives()
    {
        std::vector<AraDocumentArchive> out;
        for (auto& [id, doc] : docs)
        {
            juce::MemoryOutputStream stream;
            const auto ok = doc->controller->get().storeObjectsToArchive (reinterpret_cast<ARA::ARAArchiveWriterHostRef> (&stream), nullptr);
            if (ok)
                out.push_back ({ id, juce::Base64::toBase64 (stream.getData(), stream.getDataSize()).toStdString(), doc->archiveId });
        }
        return out;
    }
};

AraHost::AraHost (PluginHost& plugins, SampleProvider samples) : impl (std::make_unique<Impl> (plugins, std::move (samples)))
{
    impl->playbackHook = &onPlaybackRequest;
}

AraHost::~AraHost() { reset(); }

bool AraHost::available() noexcept { return true; }

bool AraHost::isAraInstrument (const Instrument* instrument) noexcept { return dynamic_cast<const AraInstrument*> (instrument) != nullptr; }

void AraHost::reset()
{
    impl->docs.clear();
    impl->failed.clear();
    impl->pendingArchives.clear();
}

void AraHost::sync (const Song& song, double sampleRate)
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (sampleRate > 0.0)
        impl->sampleRate = sampleRate;
    // the archives of documents not made yet (a project just opened); a made document keeps its own state
    for (const auto& a : song.araDocuments)
        if (! impl->docs.contains (a.pluginId))
        {
            auto it = std::find_if (impl->pendingArchives.begin(), impl->pendingArchives.end(), [&a] (const auto& p) { return p.pluginId == a.pluginId; });
            if (it == impl->pendingArchives.end())
                impl->pendingArchives.push_back (a);
            else
                *it = a;
        }

    for (auto& [id, doc] : impl->docs)
        for (auto& [uid, c] : doc->channels)
            c->seen = false;

    // the ARA channels by plugin
    std::map<std::string, std::vector<int>> byPlugin;
    for (int i = 0; i < (int) song.channels.size(); ++i)
        if (ara::isAraChannel (song, i) && ! impl->failed.contains (song.channels[(std::size_t) i].ara.pluginId))
            byPlugin[song.channels[(std::size_t) i].ara.pluginId].push_back (i);

    for (const auto& [pluginId, indices] : byPlugin)
    {
        auto description = impl->plugins.find (juce::String::fromUTF8 (pluginId.c_str()));
        if (description == nullptr)
        {
            // (not failed for good: the plugin list may still be loading, or a scan may add it; asked again next time)
            error = juce::String::fromUTF8 (song.channels[(std::size_t) indices.front()].ara.pluginName.c_str()) + ": not installed";
            logLine (error);
            continue;
        }
        Doc* doc = nullptr;
        std::unique_ptr<juce::AudioPluginInstance> first;
        bool fresh = false; // (the document is made in this sync: the song's archive of it is given back)
        if (auto it = impl->docs.find (pluginId); it != impl->docs.end())
            doc = it->second.get();
        else
        {
            fresh = true;
            juce::String why;
            first = impl->createInstance (*description, why);
            if (first == nullptr || (doc = impl->docFor (pluginId, *description, first)) == nullptr)
            {
                error = impl->error.isNotEmpty() ? impl->error : description->name + ": " + why;
                logLine (error);
                impl->failed.insert (pluginId);
                continue;
            }
        }
        impl->setTimeline (*doc, song);

        // the model: channels and their regions, in one edit (with the archive given back in it for a fresh document)
        struct Pending
        {
            Chan* chan;
            std::vector<HM::PlaybackRegion*> added;
            std::vector<std::uint64_t> removed;
        };
        std::vector<Pending> changes;
        std::vector<Chan*> unbound;
        {
            const juce::ARAEditGuard guard (doc->controller->get());
            int order = 0;
            for (const auto i : indices)
            {
                const auto& def = song.channels[(std::size_t) i];
                auto& slot = doc->channels[def.uid];
                if (slot != nullptr && slot->path != def.samplePath)
                {
                    // another sample: the channel starts over (its edits were for the old audio)
                    if (slot->binding != nullptr)
                        slot->binding->unprepared ([&slot] { slot->regions.clear(); });
                    slot.reset();
                }
                if (slot == nullptr)
                {
                    slot = std::make_unique<Chan>();
                    if (! impl->makeChannel (*doc, *slot, def, order))
                    {
                        logLine ("channel " + juce::String::fromUTF8 (def.name.c_str()) + ": its sample is not loaded (" + juce::String::fromUTF8 (def.samplePath.c_str()) + ")");
                        doc->channels.erase (def.uid);
                        continue;
                    }
                }
                auto& c = *slot;
                c.seen = true;
                const auto seconds = c.data->sample->sampleRate > 0.0 ? (double) c.data->sample->frames() / c.data->sample->sampleRate : 0.0;
                Pending p { &c, {}, {} };
                if (impl->placeRegions (*doc, c, ara::regionsOf (song, i, seconds), p.added, p.removed))
                    changes.push_back (std::move (p));
                if (c.binding == nullptr)
                    unbound.push_back (&c);
                ++order;
            }
            if (fresh)
                for (auto it = impl->pendingArchives.begin(); it != impl->pendingArchives.end(); ++it)
                    if (it->pluginId == pluginId)
                    {
                        juce::MemoryOutputStream decoded;
                        if (juce::Base64::convertFromBase64 (decoded, juce::String::fromUTF8 (it->archive.c_str())))
                        {
                            ArchiveIn block;
                            block.data = juce::MemoryBlock (decoded.getData(), decoded.getDataSize());
                            // (an archive saved without its id: the plugin's own, the only one it can be from)
                            block.archiveId = ! it->archiveId.empty() ? it->archiveId : doc->archiveId;
                            if (! doc->controller->get().restoreObjectsFromArchive (reinterpret_cast<ARA::ARAArchiveReaderHostRef> (&block), nullptr))
                                error = description->name + ": its saved edits could not be read";
                        }
                        impl->pendingArchives.erase (it);
                        break;
                    }
        }

        // the renderers follow the region sets (each bound instance unprepared around its own change)
        for (auto& p : changes)
        {
            auto& c = *p.chan;
            const auto apply = [&] {
                for (auto* r : p.added)
                    if (c.binding != nullptr)
                    {
                        c.binding->playback.add (*r);
                        c.binding->editor.add (*r);
                    }
                {
                    const juce::ARAEditGuard guard (doc->controller->get());
                    for (const auto uid : p.removed)
                    {
                        c.regions.erase (uid); // (its registries take it out of the renderers as it goes)
                        c.placed.erase (uid);
                    }
                }
            };
            if (c.binding != nullptr)
                c.binding->unprepared (apply);
            else
                apply();
        }

        // an instance for each channel that has none yet (the first one made for the document is used first)
        for (auto* c : unbound)
        {
            auto instance = std::move (first);
            if (instance == nullptr)
            {
                juce::String why;
                instance = impl->createInstance (*description, why);
                if (instance == nullptr)
                {
                    error = description->name + ": " + why;
                    continue;
                }
            }
            c->binding = impl->bind (*doc, *c, std::move (instance));
            logLine (description->name + " bound to channel " + juce::String::fromUTF8 (c->name.c_str()) + (c->binding != nullptr ? ": ok" : ": " + impl->error)
                     + " (" + juce::String ((int) c->regions.size()) + " region(s))");
        }
    }

    // channels that are no longer ARA channels (or gone), then documents without channels
    for (auto it = impl->docs.begin(); it != impl->docs.end();)
    {
        auto& doc = *it->second;
        for (auto c = doc.channels.begin(); c != doc.channels.end();)
        {
            if (c->second->seen)
            {
                ++c;
                continue;
            }
            {
                const juce::ARAEditGuard guard (doc.controller->get());
                auto& chan = *c->second;
                if (chan.binding != nullptr)
                    chan.binding->unprepared ([&chan] { chan.regions.clear(); });
                chan.regions.clear();
                chan.sequence.reset();
                chan.modification.reset();
                chan.source.reset();
            }
            c = doc.channels.erase (c);
        }
        if (doc.channels.empty())
            it = impl->docs.erase (it);
        else
            ++it;
    }
    impl->heartbeat();
}

std::shared_ptr<Instrument> AraHost::instrumentFor (const ChannelDef& channel, double sampleRate)
{
    for (auto& [id, doc] : impl->docs)
        if (id == channel.ara.pluginId)
            if (auto it = doc->channels.find (channel.uid); it != doc->channels.end() && it->second->binding != nullptr)
            {
                auto b = it->second->binding;
                if (sampleRate > 0.0 && b->sampleRate != sampleRate)
                {
                    b->sampleRate = sampleRate;
                    b->unprepared ([] {});
                }
                auto& chan = *it->second;
                if (chan.instrument == nullptr || static_cast<AraInstrument*> (chan.instrument.get())->binding != b)
                    chan.instrument = std::make_shared<AraInstrument> (b);
                return chan.instrument;
            }
    return nullptr;
}

juce::AudioPluginInstance* AraHost::pluginOf (std::uint64_t channelUid)
{
    for (auto& [id, doc] : impl->docs)
        if (auto it = doc->channels.find (channelUid); it != doc->channels.end() && it->second->binding != nullptr)
            return it->second->binding->plugin.get();
    return nullptr;
}

std::shared_ptr<void> AraHost::ownerOf (std::uint64_t channelUid)
{
    for (auto& [id, doc] : impl->docs)
        if (auto it = doc->channels.find (channelUid); it != doc->channels.end() && it->second->binding != nullptr)
            return it->second->binding;
    return nullptr;
}

void AraHost::showInEditor (std::uint64_t channelUid, const std::vector<std::uint64_t>& clipUids)
{
    for (auto& [id, doc] : impl->docs)
        if (auto it = doc->channels.find (channelUid); it != doc->channels.end() && it->second->binding != nullptr)
        {
            auto& c = *it->second;
            const auto* raw = c.binding->raw;
            if (raw == nullptr || raw->editorViewInterface == nullptr || raw->editorViewRef == nullptr)
                return;
            std::vector<ARA::ARAPlaybackRegionRef> refs;
            for (const auto& [uid, region] : c.regions)
                if (clipUids.empty() || std::find (clipUids.begin(), clipUids.end(), uid) != clipUids.end())
                    refs.push_back (region->getPluginRef());
            const ARA::ARARegionSequenceRef sequence = c.sequence->getPluginRef();
            ARA::ARAViewSelection selection {};
            selection.structSize = ARA::kARAViewSelectionMinSize;
            selection.playbackRegionRefsCount = refs.size();
            selection.playbackRegionRefs = refs.data();
            selection.regionSequenceRefsCount = 1;
            selection.regionSequenceRefs = &sequence;
            selection.timeRange = nullptr;
            raw->editorViewInterface->notifySelection (raw->editorViewRef, &selection);
            return;
        }
}

std::vector<AraDocumentArchive> AraHost::archives()
{
    auto out = impl->archives();
    for (const auto& p : impl->pendingArchives) // (a document not made again keeps what the project had)
        if (std::none_of (out.begin(), out.end(), [&p] (const auto& a) { return a.pluginId == p.pluginId; }))
            out.push_back (p);
    return out;
}

bool AraHost::analysing()
{
    // (on the message thread it asks the plugins now; elsewhere it reads what the heartbeat last found)
    if (juce::MessageManager::existsAndIsCurrentThread())
        return impl->busy = impl->poll();
    return impl->busy.load();
}

int AraHost::documentCount() const { return (int) impl->docs.size(); }

int AraHost::instanceCount() const
{
    int n = 0;
    for (auto& [id, doc] : impl->docs)
        for (auto& [uid, c] : doc->channels)
            n += c->binding != nullptr ? 1 : 0;
    return n;
}
#endif
} // namespace lynx
