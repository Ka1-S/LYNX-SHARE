#include "ui/TypeAhead.h"
#include "ui/PluginPresetMenu.h"
#include "ui/ExternalDrag.h"
#include "ui/GeneratorPresetMenu.h"
#include "ui/FileChoosers.h"
#include "ui/QuickPick.h"
#include "engine/FlStudio.h"
#include "ui/BrowserComponent.h"

#include "core/UserFolders.h"
#include "core/PluginDbFolder.h"

#include "ui/AudioClipboard.h"
#include "ui/SpeechDialog.h"
#include "ui/UiTrace.h"

#include "core/ControlSurface.h"
#include "core/EffectPresets.h"
#include "core/FlEffects.h"
#include "core/PresetCycle.h"
#include "ui/PluginHeader.h"
#include "core/NativeEngines.h"
#include "core/VoltageController.h"
#include "core/FlpImport.h"
#include "core/ProjectContents.h"
#include "core/FactoryContent.h"
#include "core/FactoryLoops.h" // (rollup 0.1.30 step 6: LYNX content in the Loop Browser)
#include "core/LayoutStyle.h"
#include "core/SoundLibrary.h"
#include "core/SearchIndex.h"
#include "core/SearchQuery.h"
#include "core/SearchService.h"
#include "core/SampleFiles.h"
#include "core/SampleSimilarity.h"
#include "engine/SampleLibrary.h"
#include "core/ValueMath.h"
#include "core/Vfx.h"
#include "platform/Paths.h"
#include "platform/CrashGuard.h"
#include "core/FolderWatcher.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#else
 #include <sys/resource.h>
 #include <unistd.h>
 #if JUCE_LINUX
  #include <sys/syscall.h>
 #endif
#endif

#include <deque>
#include <mutex>
#include <unordered_map>
#include "engine/PluginHost.h"
#include "engine/Text.h"
#include "ui/HintBar.h"
#include "ui/PluginWindow.h" // round 190iz (the focused plugin window)
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/PlaylistComponent.h"
#include "ui/Theme.h"
#include "ui/LabelFont.h"
#include "ui/ValueControl.h"
#include "ui/WaveformView.h"
#include "ui/AudioEditor.h"
#include "ui/AutomationUi.h"
#include "ui/ChannelRackComponent.h"
#include "ui/MixerComponent.h"
#include "ui/SoundFontEditor.h"
#include "ui/SamplerImportUi.h"
#include "ui/ToolWindow.h"
#include "ui/WorkflowExtras.h" // round 190iu (issue #191.6)
#include "ui/LoopInfo.h" // round 190kx

namespace lynx
{
juce::ChangeBroadcaster& loopLengthsRead(); // (round 190mf, ui/LoopBrowserParts.inc: the Simple loop rows' lengths read)
namespace
{
bool isAudio (const juce::File& f)
{
    return SampleLibrary::isSampleFile (f);
}

bool isListed (const juce::File& f)
{
    return f.hasFileExtension ("wav;flac;ogg;mp3;aif;aiff;wv;ncw;mid;midi;lynx;flp;als;flm;fst;sf2;sf3;sfz;dwp;dwb;gig");
}

constexpr int maxResults = 500;
constexpr int maxLoops = 20000; // round gaps190kf: the files the Loop Browser describes at most
constexpr int maxSimilar = 100; // "Find similar samples" lists the nearest 100

juce::Array<juce::File> childrenOf (const juce::File& folder)
{
   #if JUCE_MAC
    // JUCE's macOS iterator makes an NSURL of every entry to read its hidden flag, and a name NSString rejects
    // throws an Objective-C exception that ends the app (issue #42). readdir has no such step; names that are
    // not valid UTF-8 are skipped, the rest precomposed like JUCE does.
    // The listing itself is search::listFolderNames (JUCE-free, so the core tests run it on odd names).
    juce::Array<juce::File> children;
    for (const auto& name : search::listFolderNames (folder.getFullPathName().toStdString()))
        children.add (folder.getChildFile (juce::String::fromUTF8 (name.data(), (int) name.size()).convertToPrecomposedUnicode()));
    return children;
   #else
    return folder.findChildFiles (juce::File::findFilesAndDirectories, false, "*");
   #endif
}

search::FileSystem diskFileSystem()
{
    search::FileSystem fs;
    fs.separator = juce::File::getSeparatorChar() == '\\' ? '\\' : '/';
    fs.folderTime = [] (const std::string& path) -> std::int64_t {
        const juce::File folder (juce::String::fromUTF8 (path.c_str()));
        return folder.isDirectory() ? folder.getLastModificationTime().toMilliseconds() : -1;
    };
    fs.list = [] (const std::string& path) {
        search::FolderListing listing;
        const juce::File folder (juce::String::fromUTF8 (path.c_str()));
        if (! folder.isDirectory())
            return listing;
        listing.exists = true;
        listing.time = folder.getLastModificationTime().toMilliseconds();
        auto children = childrenOf (folder);
        children.sort();
        for (const auto& c : children)
        {
            if (c.isDirectory())
            {
                // junctions and links (Windows' "My Music" inside Documents points back to itself) are not followed
                if (! c.isSymbolicLink())
                    listing.subfolders.push_back (c.getFileName().toStdString());
            }
            else if (isListed (c))
                listing.files.push_back (c.getFileName().toStdString());
        }
        return listing;
    };
    return fs;
}

std::vector<std::string> toPaths (const juce::Array<juce::File>& folders)
{
    std::vector<std::string> paths;
    for (const auto& f : folders)
        paths.push_back (f.getFullPathName().toStdString());
    return paths;
}

Icon iconFor (const juce::File& f, bool folder)
{
    if (folder)
        return Icon::folder;
    if (isAudio (f))
        return Icon::audioFile;
    if (f.hasFileExtension ("mid;midi"))
        return Icon::pianoRoll;
    if (f.hasFileExtension ("lynx;flp;als;flm"))
        return Icon::project;
    if (f.hasFileExtension ("fst;sf2;sf3;sfz;lynxpreset;fxp;fxb;vstpreset;adv;adg;dwp;dwb;gig")) // (round 190hr: Live device / rack presets; round 190ii: sampler instruments)
        return Icon::preset;
    return Icon::file;
}

// The hint for a file or folder row: what the mouse can do with it.
juce::String rowHint (const juce::File& f, bool folder, bool expanded)
{
    if (folder)
        return (expanded ? tr ("Folder: [LMB] close") : tr ("Folder: [LMB] open")) + "  " + tr ("[RMB] menu (collapse all, show in the file manager)");
    if (isAudio (f))
        return tr ("Sample: [LMB] preview  [2xLMB] new channel  drag to the Channel Rack or the Playlist  [Enter] into the selected channel  "
                   "[Shift][Up] / [Shift][Down] swap through the files");
    if (f.hasFileExtension ("mid;midi"))
        return tr ("MIDI file: [2xLMB] open it as a project");
    if (f.hasFileExtension ("lynx;flp;als;flm"))
        return tr ("Project: [2xLMB] open it");
    if (f.hasFileExtension ("lynxtrack") || (f.hasFileExtension ("fst") && f.getFullPathName().containsIgnoreCase ("Mixer presets")))
        return tr ("Mixer preset: [2xLMB] load it onto the selected mixer tracks  drag onto a mixer track (onto the empty mixer space: a new track)");
    if (f.hasFileExtension ("fst"))
        return tr ("Preset: [2xLMB] new channel with it (FL synth presets)  drag onto a mixer insert: effect preset");
    if (f.hasFileExtension ("lynxpreset") && f.isAChildOf (genpresetui::root()))
        return tr ("Generator preset: [2xLMB] new channel with it");
    if (f.hasFileExtension ("lynxpreset;fxp;fxb;vstpreset"))
        return tr ("Plugin preset: [2xLMB] load it into its plugin (an open plugin window, or the selected channel's plugin)");
    if (f.hasFileExtension ("sfz"))
        return tr ("SFZ instrument: [LMB] preview  [2xLMB] new LYNX Soundfont channel (or into the selected one)  drag to the Channel Rack");
    if (f.hasFileExtension ("sf2;sf3;sfz"))
        return tr ("SoundFont: [LMB] preview its first preset  [2xLMB] new LYNX Soundfont channel (or into the selected one)  drag to the Channel Rack");
    if (isSamplerInstrumentFile (f))
        return tr ("Sampler instrument (DirectWave, GigaStudio): [2xLMB] new DirectWave channel  [Enter] into the selected DirectWave channel  "
                   "drag to the Channel Rack");
    return {};
}

// A user / channel colour made readable as text and icons on the Browser's dark rows.
juce::Colour readable (juce::Colour c)
{
    return c.withMultipliedSaturation (0.8f).withBrightness (juce::jlimit (0.72f, 0.92f, c.getBrightness() + 0.15f));
}

// The Current project's sections, in FL's order (REA_rodada115).
enum class Category
{
    history,
    patterns,
    effects,
    generators,
    automation,
    initialized, // FL's "Initialized controls" (REA round 146)
    samples
};

constexpr Category categories[] = { Category::history, Category::patterns, Category::effects, Category::generators,
                                     Category::automation, Category::initialized, Category::samples };

juce::String categoryName (Category c)
{
    switch (c)
    {
        case Category::history: return tr ("History");
        case Category::patterns: return tr ("Patterns");
        case Category::effects: return tr ("Effects");
        case Category::generators: return tr ("Generators");
        case Category::automation: return tr ("Automation");
        case Category::initialized: return tr ("Initialized controls");
        case Category::samples: return tr ("Audio files");
    }
    return {};
}

juce::String categoryHint (Category c)
{
    switch (c)
    {
        case Category::history: return tr ("Undo history of this project: [LMB] go back to that step");
        case Category::patterns: return tr ("Patterns of this project, in their colours");
        case Category::effects: return tr ("Effects in use, with the insert they sit on");
        case Category::generators: return tr ("Generators in use (the Channel Rack's channels), with the insert they play into");
        case Category::automation: return tr ("Automation clips of this project");
        case Category::initialized: return tr ("Initialized controls: to be reset when the song starts (a control's menu > Init song with this position)");
        case Category::samples: return tr ("Audio files used by this project (samples, audio clips), in the colour of the channel that plays them");
    }
    return {};
}

Icon categoryIcon (Category c)
{
    switch (c)
    {
        case Category::history: return Icon::undo;
        case Category::patterns: return Icon::pattern;
        case Category::effects: return Icon::mixer;
        case Category::generators: return Icon::keyboard;
        case Category::automation: return Icon::automate;
        case Category::initialized: return Icon::automate;
        case Category::samples: return Icon::audioFile;
    }
    return Icon::folder;
}

// Each top-level folder has its own colour (FL colours its Browser's folders by kind; LYNX uses its own set).
juce::Colour rootColour (int index)
{
    static const juce::Colour colours[] = { juce::Colour (0xff6ea8fe), juce::Colour (0xff3ddc84), juce::Colour (0xffe58fd0),
                                            juce::Colour (0xffb39ddb), juce::Colour (0xffb39ddb), juce::Colour (0xfff5b942),
                                            juce::Colour (0xff4cc9c0), juce::Colour (0xfff2a65a), juce::Colour (0xff9fb7d0) };
    return index >= 0 && index < (int) std::size (colours) ? colours[index] : juce::Colour (0xff8fd18c);
}
} // namespace

namespace
{
// The plugin being dragged from the Plugin database (one drag at a time; message thread).
struct DraggedPlugin
{
    std::function<std::optional<ChannelDef>()> generator;
    std::function<std::optional<EffectSlot>()> effect;
};
DraggedPlugin& draggedPlugin()
{
    static DraggedPlugin d;
    return d;
}
} // namespace

// --- search: the process's search worker ------------------------------------------------------------

// Round 190hd: the indexer process (REA 190hc action 5, parts 2-3, after Live's Indexer: "StartIndex" runs the library
// indexer as a separate process at a low priority and rescans on file-system events). The Browser's index of its
// folders (core/SearchIndex.h) is kept up to date by `LYNX --browser-indexer <pid> <roots file>`: a child at the
// lowest priority (Windows: idle class + background mode, so its disk reads are low priority too; elsewhere nice 19
// and, on Linux, the idle I/O class) that walks the folders whose times changed, writes the index file, tells this
// process, and then sleeps on the system's change notifications for those trees (core/FolderWatcher.h) until the
// next change. It never shares a thread, a lock or a frame with the UI; it ends with its parent. This process then
// saves nothing and only takes in what the child wrote (search::Service::setExternalIndexer / reload). A child that
// cannot start or ends: the in-process pass as before.
namespace
{
juce::File indexFile() { return platform::settingsFile ("browser-index.txt"); }
juce::File indexRootsFile() { return platform::settingsFile ("browser-index-roots.txt"); }

std::string loadIndexText()
{
    const auto f = indexFile();
    juce::MemoryBlock data;
    if (! f.existsAsFile() || f.getSize() > 256 * 1024 * 1024 || ! f.loadFileAsData (data))
        return std::string();
    return std::string (static_cast<const char*> (data.getData()), data.getSize());
}

void saveIndexText (const std::string& text)
{
    const auto f = indexFile();
    const auto temp = f.getSiblingFile (f.getFileName() + ".tmp");
    if (temp.replaceWithData (text.data(), text.size()))
        temp.moveFileTo (f);
}

#if JUCE_WINDOWS
juce::String indexerSignalName (juce::int64 parentPid) { return "Local\\LYNX-browser-index-" + juce::String (parentPid); }
#endif

juce::int64 thisProcessId()
{
#if JUCE_WINDOWS
    return (juce::int64) GetCurrentProcessId();
#else
    return (juce::int64) getpid();
#endif
}

class IndexerProcess
{
public:
    explicit IndexerProcess (search::Service& s) : service (s) {}
    ~IndexerProcess() { stop(); }

    // starts the child for these folders (once; again when they change); false: it could not start
    bool start (const std::vector<std::string>& folders)
    {
        if (watcher.joinable() && folders == roots && child.isRunning())
            return true;
        stop();
        roots = folders;
        juce::StringArray lines;
        for (const auto& r : roots)
            lines.add (juce::String::fromUTF8 (r.c_str()));
        if (! indexRootsFile().replaceWithText (lines.joinIntoString ("\n") + "\n"))
            return false;
       #if JUCE_WINDOWS
        signal = CreateEventW (nullptr, FALSE, FALSE, indexerSignalName (thisProcessId()).toWideCharPointer());
        if (signal == nullptr)
            return false;
       #endif
        const juce::StringArray args { juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName(), "--browser-indexer",
                                       juce::String (thisProcessId()), indexRootsFile().getFullPathName() };
       #if JUCE_WINDOWS
        const auto started = child.start (args, 0);
       #else
        const auto started = child.start (args, juce::ChildProcess::wantStdOut);
       #endif
        if (! started)
        {
            closeSignal();
            return false;
        }
        service.setExternalIndexer (true);
        stopping = false;
        watcher = std::thread ([this] { watch(); });
        return true;
    }

    void stop()
    {
        stopping = true;
        if (child.isRunning())
            child.kill(); // (our own child; a write it was making is a temporary file, replaced by the next)
       #if JUCE_WINDOWS
        if (signal != nullptr)
            SetEvent (signal);
       #endif
        if (watcher.joinable())
            watcher.join();
        closeSignal();
        service.setExternalIndexer (false);
    }

private:
    // (its own thread) the child's news: the index file was written
    void watch()
    {
        for (;;)
        {
           #if JUCE_WINDOWS
            const auto woken = WaitForSingleObject (signal, 2000) == WAIT_OBJECT_0;
            if (stopping)
                return;
            if (woken)
                service.reload();
            if (child.isRunning())
                continue;
           #else
            char c = 0;
            if (child.readProcessOutput (&c, 1) == 1) // (blocks; one byte at a time: a line is a write)
            {
                if (c == '\n')
                    service.reload();
                continue;
            }
            if (stopping)
                return;
           #endif
            // the child ended on its own (a crash): this process indexes again, as before
            service.setExternalIndexer (false);
            service.refresh (roots);
            return;
        }
    }

    void closeSignal()
    {
       #if JUCE_WINDOWS
        if (signal != nullptr)
            CloseHandle (signal);
        signal = nullptr;
       #endif
    }

    search::Service& service;
    juce::ChildProcess child;
    std::vector<std::string> roots;
    std::atomic<bool> stopping { false };
   #if JUCE_WINDOWS
    HANDLE signal = nullptr;
   #endif
    std::thread watcher;
};

// (the child) the lowest priority for the CPU and the disk
void lowerIndexerPriority()
{
#if JUCE_WINDOWS
    // background mode first (low disk and memory priority), then the idle class (the order: setting background mode
    // left the class at Normal, measured)
    SetPriorityClass (GetCurrentProcess(), PROCESS_MODE_BACKGROUND_BEGIN);
    SetPriorityClass (GetCurrentProcess(), IDLE_PRIORITY_CLASS);
#else
    setpriority (PRIO_PROCESS, 0, 19);
 #if JUCE_LINUX
    syscall (SYS_ioprio_set, 1 /* IOPRIO_WHO_PROCESS */, 0, 3 << 13 /* IOPRIO_CLASS_IDLE */);
 #endif
#endif
}

// (the child) returns when the parent process has ended
void waitForParentEnd (juce::int64 parentPid)
{
#if JUCE_WINDOWS
    if (const auto parent = OpenProcess (SYNCHRONIZE, FALSE, (DWORD) parentPid))
    {
        WaitForSingleObject (parent, INFINITE);
        CloseHandle (parent);
    }
#else
    while (getppid() == (pid_t) parentPid)
        std::this_thread::sleep_for (std::chrono::seconds (2));
#endif
}
} // namespace

int BrowserComponent::runIndexerProcess (const juce::StringArray& args)
{
    const auto i = args.indexOf ("--browser-indexer");
    const auto parentPid = args[i + 1].getLargeIntValue();
    const juce::File rootsFile (args[i + 2].unquoted());
    lowerIndexerPriority();
    std::thread ([parentPid] {
        waitForParentEnd (parentPid);
        crashguard::exitNow (0);
    }).detach();

    std::vector<std::string> roots;
    juce::StringArray lines;
    lines.addLines (rootsFile.loadFileAsString());
    for (const auto& line : lines)
        if (line.isNotEmpty() && juce::File::isAbsolutePath (line) && juce::File (line).isDirectory())
            roots.push_back (line.toStdString());

#if JUCE_WINDOWS
    const auto signal = OpenEventW (EVENT_MODIFY_STATE, FALSE, indexerSignalName (parentPid).toWideCharPointer());
#endif
    search::Service::Callbacks cb;
    cb.load = loadIndexText;
    cb.save = saveIndexText;
    cb.indexChanged = [&] { // (the file is written by then)
#if JUCE_WINDOWS
        if (signal != nullptr)
            SetEvent (signal);
#else
        std::fputs ("changed\n", stdout);
        std::fflush (stdout);
#endif
    };
    search::Service service (diskFileSystem(), std::move (cb));
    if (! roots.empty())
        service.refresh (roots);
    // then a pass after each change in those trees (a copy of a whole library: once it has been quiet for 2 s)
    folderwatch::Options options;
    options.quietMs = 2000;
    options.maxDelayMs = 10000;
    options.pollMs = 30000; // (where there are no notifications: a tree's folder times, every 30 s)
    folderwatch::Watcher watcher (roots, [&service, roots] { service.refresh (roots); }, options);
    for (;;)
        std::this_thread::sleep_for (std::chrono::hours (1)); // (ends with the parent: waitForParentEnd)
}

// The Browser's search index and its worker (core/SearchService.h, round 162): one per process while a Browser exists,
// the index kept in the settings folder between sessions. Its news reach the Browsers through two broadcasters (they
// are thread-safe and coalesce); the worker is joined (never killed) before they go.
struct BrowserComponent::SearchShared
{
    juce::ChangeBroadcaster statusChanged, indexChanged;
    std::unique_ptr<search::Service> service; // (last: goes first)
    std::unique_ptr<IndexerProcess> indexer;  // (round 190hd; before the service)

    ~SearchShared()
    {
        indexer.reset();
        service.reset();
    }

    static juce::File file() { return indexFile(); }

    // the background pass over these folders: by the indexer process where it may run, else here (round 190hd)
    void refreshInBackground (std::vector<std::string> roots)
    {
        if (allowIndexerProcess && juce::SystemStats::getEnvironmentVariable ("LYNX_INDEXER_PROCESS", {}) != "0")
        {
            if (indexer == nullptr)
                indexer = std::make_unique<IndexerProcess> (*service);
            if (indexer->start (roots))
                return;
        }
        service->refresh (std::move (roots));
    }

    static std::shared_ptr<SearchShared> get()
    {
        static std::weak_ptr<SearchShared> current; // (message thread)
        if (auto s = current.lock())
            return s;
        auto s = std::make_shared<SearchShared>();
        search::Service::Callbacks cb;
        cb.load = loadIndexText;
        cb.save = saveIndexText;
        cb.statusChanged = [raw = s.get()] { raw->statusChanged.sendChangeMessage(); };
        cb.indexChanged = [raw = s.get()] { raw->indexChanged.sendChangeMessage(); };
        s->service = std::make_unique<search::Service> (diskFileSystem(), std::move (cb));
        current = s;
        return s;
    }
};

// "Find similar samples" (round 190as): the analysis of the Browser's samples and its worker (core/SampleSimilarity.h),
// one per process while a Browser exists, the index kept in the settings folder between sessions. The worker has the
// lowest priority, rests as long as it works and waits while LYNX plays, records or renders.
struct BrowserComponent::SimilarShared
{
    juce::ChangeBroadcaster statusChanged;
    SampleLibrary library;                        // its own readers: nothing goes into the engine's sample cache
    std::unique_ptr<similarity::Service> service; // (last: goes first)
    bool passAsked = false;                       // the background pass was started in this session (message thread)

    ~SimilarShared() { service.reset(); }

    static juce::File file() { return platform::settingsFile ("browser-similar.idx"); }

    static std::shared_ptr<SimilarShared> get (AudioEngine& engine)
    {
        static std::weak_ptr<SimilarShared> current; // (message thread)
        if (auto s = current.lock())
            return s;
        auto s = std::make_shared<SimilarShared>();
        similarity::Service::Callbacks cb;
        cb.fs = diskFileSystem();
        cb.stat = [] (const std::string& path, std::int64_t& size, std::int64_t& time) {
            const juce::File f (juce::String::fromUTF8 (path.c_str()));
            if (! f.existsAsFile())
                return false;
            size = f.getSize();
            time = f.getLastModificationTime().toMilliseconds();
            return true;
        };
        cb.decode = [raw = s.get()] (const std::string& path, double seconds, std::vector<float>& mono, double& rate) {
            return raw->library.readMono (juce::File (juce::String::fromUTF8 (path.c_str())), seconds, mono, rate);
        };
        cb.accepts = [] (const std::string& name) {
            const auto dot = name.find_last_of ('.');
            if (dot == std::string::npos)
                return false;
            const auto extension = name.substr (dot + 1);
            return isSampleExtension (extension) && extension.size() != 6; // (not .speech: it has no audio of its own)
        };
        cb.load = [] {
            const auto f = file();
            juce::MemoryBlock data;
            if (! f.existsAsFile() || f.getSize() > 64 * 1024 * 1024 || ! f.loadFileAsData (data))
                return std::string();
            return std::string (static_cast<const char*> (data.getData()), data.getSize());
        };
        cb.save = [] (const std::string& bytes) {
            const auto f = file();
            const auto temp = f.getSiblingFile (f.getFileName() + ".tmp");
            if (temp.replaceWithData (bytes.data(), bytes.size()))
                temp.moveFileTo (f);
        };
        cb.statusChanged = [raw = s.get()] { raw->statusChanged.sendChangeMessage(); };
        // (the engine outlives every Browser; these are atomics)
        cb.shouldPause = [&engine] { return engine.bounceStillPlaying() || engine.isRecording() || AudioEngine::offlineRendersRunning().load() > 0; };
        s->service = std::make_unique<similarity::Service> (std::move (cb));
        current = s;
        return s;
    }
};

// A click anywhere outside the search box (another panel, the toolbar, the Browser's own list) takes the keyboard
// back from it, so Space plays / stops again (issue #9; FL ends typing in its search field the same way).
class BrowserComponent::FocusWatcher final : public juce::MouseListener
{
public:
    explicit FocusWatcher (BrowserComponent& b) : browser (b) { juce::Desktop::getInstance().addGlobalMouseListener (this); }
    ~FocusWatcher() override { juce::Desktop::getInstance().removeGlobalMouseListener (this); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto* clicked = e.eventComponent;
        if (! browser.isTypingInSearch() || clicked == nullptr)
            return;
        if (clicked == &browser.searchBox || browser.searchBox.isParentOf (clicked))
            return;
        // a menu (the box's own right-click menu: Paste...) keeps it
        if (clicked->getTopLevelComponent() != browser.getTopLevelComponent() && clicked->getTopLevelComponent()->isAlwaysOnTop()
            && dynamic_cast<juce::ResizableWindow*> (clicked->getTopLevelComponent()) == nullptr)
            return;
        browser.leaveSearchBox();
    }

private:
    BrowserComponent& browser;
};

// --- the tree (and, flat, the search results) ---------------------------------------------------------

// round 190iz: the tab icons, the row star, the TAGS button, the tags panel, the Live sidebar
#include "ui/BrowserTagsLive.inc"
#include "ui/LoopBrowserPanel.inc" // round gaps190kf

class BrowserComponent::FileTree final : public juce::Component,
                                         public ui::HintSource,
                                         public juce::FileDragAndDropTarget
{
public:
    // FL layout: 24 px rows (design slice FM-S7 / finding FM-9: FL's tree rows are 23 px with a larger face than LYNX's
    // 22 px ones); the Clip-launch layout's Live-style list: 19 px (Live's row pitch, CL-22). Round 190iz.
    static constexpr int flRowHeight = 24, liveRowHeight = 19;
    int rowHeight = flRowHeight;
    void setLiveRows (bool live)
    {
        const auto h = live ? liveRowHeight : flRowHeight;
        if (h == rowHeight)
            return;
        rowHeight = h;
        updateSize();
        repaint();
    }
    // the hover star's box on a row (FL: "the star that appears at the end of the content"); empty: none there
    juce::Rectangle<int> starBox (int row) const
    {
        if (! juce::isPositiveAndBelow (row, (int) rows.size()) || itemOf (*rows[(std::size_t) row]).empty())
            return {};
        return { getWidth() - 20, row * rowHeight, 16, rowHeight };
    }

    FileTree (BrowserComponent& b, bool isFlat) : browser (b), flat (isFlat)
    {
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
    }

    enum class Kind
    {
        fsFolder, // a folder on disk (top-level ones carry their own name and colour)
        file,     // a file on disk
        project,  // "Current project"
        category, // one of its sections
        item,     // something of the project (pattern, sample, generator, effect, automation clip, undo step)
        plugins,  // the Plugin database and its folders (round 142): built-in, FL engines, installed plugins
        plugin,   // one of its plugins (double-click adds it)
        collection, // a collection's heading in the STARRED tab (round 190ce): its items are its children
        factory     // round 190ke: "LYNX content" and its folders (the Simple layout): LYNX's own sounds, beats, chains
    };

    struct Node
    {
        Kind kind = Kind::file;
        juce::File file;
        juce::Array<juce::File> also; // fsFolder: more folders listed as this one (a merged root and its shared subfolders, round 178a)
        bool folder = false, expanded = false, loaded = false;
        int depth = 0;
        Node* parent = nullptr;
        std::vector<std::unique_ptr<Node>> children;

        juce::String label, detail; // display name (empty: the file's) and the dimmed detail after it
        juce::String loopText; // round 190kx: a loop's bars, tempo and key (the Simple layout's rows; empty: not known)
        bool loopKnown = false; // (the worker answered for this file)
        juce::Colour colour;        // transparent: the default
        Icon icon = Icon::none;     // none: by file type
        Category category = Category::samples;
        projectcontents::Item item; // kind == item (history: item.index = the step)
        bool history = false, missing = false, current = false;
        bool initial = false; // an initialized control (item.index = into Song::initialValues)
        std::function<void()> activate; // kind == plugin: adds it
        // kind == plugin: what it adds (one of them), also when it is dragged (round 162)
        std::function<std::optional<ChannelDef>()> generator;
        std::function<std::optional<EffectSlot>()> effect;
        std::string pluginKey;  // kind == plugin: what a user name is stored under (ui/QuickPick.h, issue #11)
        juce::String original;  // kind == plugin: the name before the user renamed it
        // round 166f (issue #16): kind == plugins — 0 the Effects folder or one of the user's effect folders, 1 the
        // Generators' (-1 the other database folders); userFolder = the user's folder path ("" = Effects / Generators)
        int folderKind = -1;
        std::string userFolder;
        // round 190bj (issue B4): LYNX's own groups under Effects / Generators — the format folder of their Installed
        // entries ("LYNX", "FL engines", "VFX"; "" = Installed itself), core/PluginDbFolder.h groupFolder()
        std::string group;
        bool isGroup = false;
        int collection = -1; // kind == collection: which one (0 = Starred)
    };

    // What a row is to the collections (core/BrowserCollections.h): a file's or folder's path, a Plugin database
    // entry's key; empty = a row that can't be in one (the project's items, headings).
    static std::string itemOf (const Node& n)
    {
        if (n.kind == Kind::plugin)
            return n.pluginKey.empty() ? std::string() : collections::pluginItem (n.pluginKey);
        if ((n.kind == Kind::file || n.kind == Kind::fsFolder) && n.file != juce::File())
            return n.file.getFullPathName().toStdString();
        return {};
    }
    std::string selectedItem() const { return juce::isPositiveAndBelow (selected, (int) rows.size()) ? itemOf (*rows[(std::size_t) selected]) : std::string(); }
    const Node* selectedNode() const { return juce::isPositiveAndBelow (selected, (int) rows.size()) ? rows[(std::size_t) selected] : nullptr; }
    juce::String selectedLabel() const
    {
        const auto* n = selectedNode();
        return n == nullptr ? juce::String() : n->label.isNotEmpty() ? n->label : n->file.getFileName();
    }
    static std::unique_ptr<Node> makeCollection (int collection, int itemCount, bool open)
    {
        auto n = std::make_unique<Node>();
        n->kind = Kind::collection;
        n->collection = collection;
        n->folder = n->loaded = true;
        n->expanded = open;
        n->label = BrowserComponent::collectionName (collection);
        n->detail = juce::String (itemCount);
        n->colour = BrowserComponent::collectionColour (collection);
        return n;
    }
    // A Plugin database entry outside the database's tree (a collection's item): the same row, double-click and drag.
    static std::unique_ptr<Node> makePluginItem (BrowserComponent& browser, const std::string& key)
    {
        if (factorycontent::isKey (key)) // (round 190ke: LYNX's own content in a collection or a tag search)
            return makeFactoryItem (browser, key);
        auto safe = juce::Component::SafePointer<BrowserComponent> (&browser);
        for (int generators = 1; generators >= 0; --generators)
        {
            auto n = std::make_unique<Node>();
            n->kind = Kind::plugin;
            n->pluginKey = key;
            if (generators == 1)
            {
                for (auto& e : quickpick::generatorEntries (browser.engine))
                    if (e.key == key)
                    {
                        n->label = quickpick::shownName (browser.engine, e.key, e.label);
                        n->original = e.label;
                        n->detail = e.detail;
                        n->icon = e.folder == 3 ? Icon::plugin : Icon::keyboard;
                        n->generator = e.make;
                        n->activate = [safe, make = e.make] {
                            if (auto c = make(); c && safe != nullptr && safe->onAddInstrument)
                                safe->onAddInstrument (std::move (*c));
                        };
                        return n;
                    }
            }
            else
                for (auto& e : quickpick::effectEntries (browser.engine))
                    if (e.key == key)
                    {
                        n->label = quickpick::shownName (browser.engine, e.key, e.label);
                        n->original = e.label;
                        n->detail = e.detail;
                        n->icon = e.folder == 3 ? Icon::plugin : Icon::mixer;
                        n->effect = e.make;
                        n->activate = [safe, make = e.make] {
                            if (auto fx = make(); fx && safe != nullptr && safe->onAddEffect)
                                safe->onAddEffect (std::move (*fx));
                        };
                        return n;
                    }
        }
        return nullptr; // (the plugin is not installed any more: the collection leaves it out)
    }

    // Round 190ke (owner decision D2): one item of LYNX's own content, a plugin-like row (double-click / Enter: add it;
    // drag a sound onto the Channel Rack) whose key the collections and the tags store; nullptr for an unknown key
    static std::unique_ptr<Node> makeFactoryItem (BrowserComponent& browser, const std::string& key)
    {
        const auto* item = factorycontent::find (key);
        if (item == nullptr)
            return nullptr;
        auto safe = juce::Component::SafePointer<BrowserComponent> (&browser);
        auto n = std::make_unique<Node>();
        n->kind = Kind::plugin;
        n->pluginKey = key;
        n->label = n->original = juce::String::fromUTF8 (item->name.c_str());
        n->detail = juce::String::fromUTF8 (item->detail.c_str());
        if (item->kind == factorycontent::Kind::sound)
        {
            n->icon = Icon::keyboard;
            const auto index = item->index;
            n->generator = [index]() -> std::optional<ChannelDef> {
                const auto& sounds = soundlib::builtIn();
                return index >= 0 && index < (int) sounds.size() ? soundlib::channelFor (sounds[(std::size_t) index]) : std::nullopt;
            };
            n->activate = [safe, make = n->generator, key] {
                if (safe != nullptr && safe->onFactoryItem) // (the Simple layout: a new instrument track)
                    safe->onFactoryItem (key);
                else if (auto c = make(); c && safe != nullptr && safe->onAddInstrument)
                    safe->onAddInstrument (std::move (*c));
            };
        }
        else
        {
            n->icon = item->kind == factorycontent::Kind::beat ? Icon::note : Icon::mixer;
            n->activate = [safe, key] {
                if (safe != nullptr && safe->onFactoryItem)
                    safe->onFactoryItem (key);
            };
        }
        return n;
    }
    // "LYNX content" with its three folders, each by category / genre (built open or closed as `open` remembers)
    static std::unique_ptr<Node> makeFactoryRoot (BrowserComponent& browser, const std::set<juce::String>& open)
    {
        auto folder = [&open] (Node* parent, const juce::String& label, const juce::String& path) {
            auto n = std::make_unique<Node>();
            n->kind = Kind::factory;
            n->folder = n->loaded = true;
            n->label = label;
            n->icon = Icon::folder;
            n->parent = parent;
            n->depth = parent != nullptr ? parent->depth + 1 : 0;
            n->userFolder = path.toStdString(); // (the key `open` remembers it by)
            n->expanded = open.count (path) > 0;
            return n;
        };
        auto root = folder (nullptr, tr ("LYNX content"), "lynx-content");
        root->colour = theme::colour::accent;
        const std::tuple<factorycontent::Kind, const char*, juce::String> kinds[] { { factorycontent::Kind::sound, "Sounds", tr ("Sounds") },
                                                                                    { factorycontent::Kind::beat, "Beats", tr ("Beats") },
                                                                                    { factorycontent::Kind::chain, "Effect chains", tr ("Effect chains") } };
        int total = 0;
        for (const auto& [kind, id, title] : kinds)
        {
            const auto kindPath = juce::String ("lynx-content/") + id;
            auto group = folder (root.get(), title, kindPath);
            std::map<std::string, std::unique_ptr<Node>> byFolder;
            std::vector<std::string> order;
            for (const auto& item : factorycontent::items())
            {
                if (item.kind != kind)
                    continue;
                auto& sub = byFolder[item.folder];
                if (sub == nullptr)
                {
                    const auto& f = item.folder; // (the words that mean something else elsewhere get their context, as the Simple dock's)
                    const auto shown = f == "Keys" ? trc ("@sound category: Keys") : f == "Bass" ? trc ("@instrument: Bass") : f == "Lead" ? trc ("@sound category: Lead")
                                     : f == "Effects" ? trc ("@sound category: Effects") : tr (juce::String::fromUTF8 (f.c_str()));
                    sub = folder (group.get(), shown, kindPath + "/" + juce::String::fromUTF8 (f.c_str()));
                    order.push_back (item.folder);
                }
                if (auto leaf = makeFactoryItem (browser, item.key))
                {
                    leaf->parent = sub.get();
                    leaf->depth = sub->depth + 1;
                    sub->children.push_back (std::move (leaf));
                }
            }
            int count = 0;
            for (const auto& f : order)
            {
                auto& sub = byFolder[f];
                sub->detail = juce::String ((int) sub->children.size());
                count += (int) sub->children.size();
                group->children.push_back (std::move (sub));
            }
            group->detail = juce::String (count);
            total += count;
            root->children.push_back (std::move (group));
        }
        root->detail = juce::String (total);
        return root;
    }

    // round 190iz: a collection's items (STARRED tab, Live's Collections) and Live's plugin categories as lists
    static std::vector<std::unique_ptr<Node>> collectionNodes (BrowserComponent& browser, int collection);
    static std::vector<std::unique_ptr<Node>> livePluginNodes (BrowserComponent& browser, browserplaces::Category category);

    // tree mode: the given top-level nodes
    void setTopLevel (std::vector<std::unique_ptr<Node>> nodes)
    {
        root = std::make_unique<Node>();
        root->kind = Kind::fsFolder;
        root->folder = root->expanded = root->loaded = true;
        root->depth = -1;
        for (auto& n : nodes)
        {
            n->parent = root.get();
            n->depth = 0;
            if (n->kind == Kind::collection) // (its items came with it)
                for (auto& c : n->children)
                {
                    c->parent = n.get();
                    c->depth = 1;
                }
            if (n->expanded)
                load (*n);
            root->children.push_back (std::move (n));
        }
        selected = hovered = -1;
        rebuild();
    }

    static std::unique_ptr<Node> makeFolder (const juce::File& folder, const juce::String& label, juce::Colour colour)
    {
        auto n = std::make_unique<Node>();
        n->kind = Kind::fsFolder;
        n->file = folder;
        n->folder = true;
        n->label = label;
        n->colour = colour;
        return n;
    }

    // round 166f (issue #16): the user's folders of one kind (FL's Plugin database folders) as nodes under `kindNode`,
    // parents before children; the map gives each path's node
    template <typename MakeFolder>
    static std::map<std::string, Node*> userFolderNodes (Node& kindNode, bool instruments, MakeFolder& folder, AudioEngine& engine)
    {
        std::map<std::string, Node*> out;
        for (const auto& path : engine.getPluginHost().databaseFolders().kind (instruments)) // (a std::set: "A" before "A/B")
        {
            const auto parentPath = plugincatalog::parentFolder (path);
            auto* parent = parentPath.empty() ? &kindNode : (out.count (parentPath) ? out[parentPath] : nullptr);
            if (parent == nullptr) continue;
            auto n = folder (parent, juce::String::fromUTF8 (plugincatalog::folderLeaf (path).c_str()), Icon::folder);
            n->folderKind = instruments ? 1 : 0;
            n->userFolder = path;
            n->colour = theme::colour::info;
            out[path] = n.get();
            parent->children.push_back (std::move (n));
        }
        return out;
    }
    static Node* userFolderOf (const std::map<std::string, Node*>& nodes, AudioEngine& engine, bool instruments, const std::string& key)
    {
        const auto& folders = engine.getPluginHost().databaseFolders();
        const auto it = folders.of.find (key);
        if (it == folders.of.end() || ! folders.kind (instruments).count (it->second)) return nullptr;
        const auto n = nodes.find (it->second);
        return n != nodes.end() ? n->second : nullptr;
    }
    // subfolders first, then the plugins, each A-Z
    static void sortFolder (Node& n)
    {
        std::stable_sort (n.children.begin(), n.children.end(), [] (const auto& a, const auto& b) {
            if (a->folder != b->folder) return a->folder;
            return a->label.compareNatural (b->label) < 0;
        });
    }

    // FL's "Plugin database", interpreted (round 142, issue #5): Generators and Effects, each with LYNX's built-in ones,
    // FL's engines (those the user's FL installation provides) and the installed VST3 / CLAP plugins
    static std::unique_ptr<Node> makePluginDatabase (BrowserComponent& browser, const std::set<juce::String>& open)
    {
        auto folder = [&open] (Node* parent, const juce::String& label, Icon icon) {
            auto n = std::make_unique<Node>();
            n->kind = Kind::plugins;
            n->folder = n->loaded = true;
            n->label = label;
            n->icon = icon;
            n->parent = parent;
            n->depth = parent != nullptr ? parent->depth + 1 : 0;
            n->expanded = open.count ((parent != nullptr && parent->kind == Kind::plugins ? parent->label + "/" : juce::String()) + label) > 0;
            return n;
        };
        auto node = [] (Node& parent, const juce::String& label, const juce::String& detail, Icon icon) -> Node& {
            auto n = std::make_unique<Node>();
            n->kind = Kind::plugin;
            n->label = label;
            n->detail = detail;
            n->icon = icon;
            n->parent = &parent;
            n->depth = parent.depth + 1;
            parent.children.push_back (std::move (n));
            return *parent.children.back();
        };
        quickpick::refreshPluginDatabase (browser.engine); // (the folders as they are on disk now: issue #104)
        auto root = folder (nullptr, tr ("Plugin database"), Icon::plugin);
        root->colour = theme::colour::info;
        auto safe = juce::Component::SafePointer<BrowserComponent> (&browser);
        // a generator / an effect: double-click adds it (to the rack / the selected insert), or drag it there
        auto generatorEntry = [&node, safe] (Node& parent, const juce::String& label, const juce::String& detail, Icon icon,
                                             std::function<std::optional<ChannelDef>()> make) {
            auto& n = node (parent, label, detail, icon);
            n.generator = make;
            n.activate = [safe, make] {
                if (auto c = make(); c && safe != nullptr && safe->onAddInstrument)
                    safe->onAddInstrument (std::move (*c));
            };
        };
        auto effectEntry = [&node, safe] (Node& parent, const juce::String& label, const juce::String& detail, Icon icon,
                                          std::function<std::optional<EffectSlot>()> make) {
            auto& n = node (parent, label, detail, icon);
            n.effect = make;
            n.activate = [safe, make] {
                if (auto fx = make(); fx && safe != nullptr && safe->onAddEffect)
                    safe->onAddEffect (std::move (*fx));
            };
        };
        // the entries (ui/QuickPick.h), each under the name the user gave it (issue #11: FL's renamed plugins, "1_EQ"),
        // sorted by that name in each folder
        auto& engine = browser.engine;
        auto named = [&engine] (const std::string& key, const juce::String& label) { return quickpick::shownName (engine, key, label); };
        auto byLabel = [] (Node& folderNode) {
            std::stable_sort (folderNode.children.begin(), folderNode.children.end(),
                              [] (const auto& a, const auto& b) { return a->label.compareNatural (b->label) < 0; });
        };

        // Generators
        {
            auto gens = folder (root.get(), tr ("Generators"), Icon::keyboard);
            auto lynxGens = folder (gens.get(), "LYNX SHARE", Icon::folder);
            auto flGens = folder (gens.get(), tr ("FL engines"), Icon::folder);
            // FL's VFX note processors (round 164b): channels with no sound, they play other channels / Patcher nodes
            auto vfxGens = folder (gens.get(), tr ("VFX (note effects)"), Icon::folder);
            auto installed = folder (gens.get(), tr ("Installed"), Icon::folder);
            Node* folders[] = { lynxGens.get(), flGens.get(), vfxGens.get(), installed.get() };
            gens->folderKind = 1;
            for (auto [node, group] : { std::pair { folders[0], "LYNX" }, std::pair { folders[1], "FL engines" }, std::pair { folders[2], "VFX" },
                                        std::pair { folders[3], "" } })
            {
                node->group = group;
                node->isGroup = true;
            }
            const auto mine = userFolderNodes (*gens, true, folder, engine); // round 166f: the user's folders first
            for (auto& e : quickpick::generatorEntries (engine))
            {
                auto* target = userFolderOf (mine, engine, true, e.key);
                if (target == nullptr) target = folders[juce::jlimit (0, 3, e.folder)];
                generatorEntry (*target, named (e.key, e.label), e.detail, e.folder == 3 ? Icon::plugin : Icon::keyboard, e.make);
                target->children.back()->pluginKey = e.key;
                target->children.back()->original = e.label;
            }
            for (auto& [path, n] : mine) sortFolder (*n);
            for (auto* f : { &lynxGens, &flGens, &vfxGens, &installed })
                if (! (*f)->children.empty())
                {
                    byLabel (**f);
                    gens->children.push_back (std::move (*f));
                }
            root->children.push_back (std::move (gens));
        }
        // Effects
        {
            auto fxs = folder (root.get(), tr ("Effects"), Icon::mixer);
            auto lynxFx = folder (fxs.get(), "LYNX SHARE", Icon::folder);
            auto flFx = folder (fxs.get(), tr ("FL engines"), Icon::folder);
            auto installed = folder (fxs.get(), tr ("Installed"), Icon::folder);
            Node* folders[] = { lynxFx.get(), flFx.get(), installed.get(), installed.get() };
            fxs->folderKind = 0;
            for (auto [node, group] : { std::pair { folders[0], "LYNX" }, std::pair { folders[1], "FL engines" }, std::pair { folders[2], "" } })
            {
                node->group = group;
                node->isGroup = true;
            }
            const auto mine = userFolderNodes (*fxs, false, folder, engine); // round 166f: the user's folders first
            for (auto& e : quickpick::effectEntries (engine))
            {
                auto* target = userFolderOf (mine, engine, false, e.key);
                if (target == nullptr) target = folders[juce::jlimit (0, 3, e.folder)];
                effectEntry (*target, named (e.key, e.label), e.detail, e.folder == 3 ? Icon::plugin : Icon::mixer, e.make);
                target->children.back()->pluginKey = e.key;
                target->children.back()->original = e.label;
            }
            for (auto& [path, n] : mine) sortFolder (*n);
            for (auto* f : { &lynxFx, &flFx, &installed })
                if (! (*f)->children.empty())
                {
                    byLabel (**f);
                    fxs->children.push_back (std::move (*f));
                }
            root->children.push_back (std::move (fxs));
        }
        // (the counts, dimmed after each folder)
        std::function<int (Node&)> count = [&count] (Node& n) {
            int total = 0;
            for (auto& c : n.children)
                total += c->kind == Kind::plugin ? 1 : count (*c);
            if (n.kind == Kind::plugins)
                n.detail = juce::String (total);
            return total;
        };
        count (*root);
        return root;
    }

    static std::unique_ptr<Node> makeProject (bool open)
    {
        auto n = std::make_unique<Node>();
        n->kind = Kind::project;
        n->folder = true;
        n->expanded = open;
        n->label = tr ("Current project");
        n->icon = Icon::project;
        n->colour = theme::colour::accent;
        return n;
    }

    // flat mode: a list of files with their path relative to the folder searched that holds them
    // (`details`: the dimmed text after each name, e.g. how similar the file is; round 190as)
    void setFiles (const juce::Array<juce::File>& files, const juce::Array<juce::File>& newBases, const juce::String& newStatus,
                   const juce::StringArray& details = {})
    {
        bases = newBases;
        status = newStatus;
        flatNodes.clear();
        for (const auto& f : files)
        {
            auto n = std::make_unique<Node>();
            n->file = f;
            n->detail = details[(int) flatNodes.size()];
            flatNodes.push_back (std::move (n));
        }
        selected = hovered = -1;
        rebuild();
    }

    // flat mode: project items (the PROJECT tab's search)
    void setNodes (std::vector<std::unique_ptr<Node>> nodes, const juce::String& newStatus)
    {
        bases.clear();
        status = newStatus;
        flatNodes = std::move (nodes);
        selected = hovered = -1;
        rebuild();
    }

    // round 190mf: flat mode: changes some rows' dimmed detail in place (the Simple loop list's lengths as they are read)
    void updateDetails (const std::function<bool (Node&)>& change)
    {
        bool any = false;
        for (auto& n : flatNodes)
            any = change (*n) || any;
        if (any)
            repaint();
    }

    void setStatus (const juce::String& text)
    {
        status = text;
        repaint();
    }

    // Removes the folders on disk `drop` takes (round 164: the content filter found them empty), keeping what is open
    // and selected. False when nothing went.
    bool pruneFolders (const std::function<bool (const juce::File&)>& drop)
    {
        if (flat || root == nullptr)
            return false;
        auto* keep = juce::isPositiveAndBelow (selected, (int) rows.size()) ? rows[(std::size_t) selected] : nullptr;
        bool removed = false, lostSelection = false;
        std::function<void (Node&)> walk = [&] (Node& n) {
            for (auto it = n.children.begin(); it != n.children.end();)
            {
                auto& c = **it;
                if (c.kind == Kind::fsFolder && drop (c.file) && std::all_of (c.also.begin(), c.also.end(), drop))
                {
                    for (auto* s = keep; s != nullptr; s = s->parent)
                        lostSelection = lostSelection || s == &c;
                    it = n.children.erase (it);
                    removed = true;
                    continue;
                }
                if (c.kind == Kind::fsFolder && c.loaded)
                    walk (c);
                ++it;
            }
        };
        walk (*root);
        if (! removed)
            return false;
        rebuild();
        selected = -1;
        if (! lostSelection)
            for (int i = 0; i < (int) rows.size(); ++i)
                if (rows[(std::size_t) i] == keep)
                    selected = i;
        hovered = -1;
        return true;
    }

    juce::File selectedFile() const
    {
        return juce::isPositiveAndBelow (selected, (int) rows.size()) ? rows[(std::size_t) selected]->file : juce::File();
    }

    // the top-level folder on disk that holds the selection (none: the selection is not a file)
    juce::File selectedRoot() const
    {
        if (! juce::isPositiveAndBelow (selected, (int) rows.size()))
            return {};
        for (auto* n = rows[(std::size_t) selected]; n != nullptr; n = n->parent)
            if (n->kind == Kind::fsFolder && n->depth == 0)
                return n->file;
        return {};
    }

    // the name a row shows (issue #182's type-to-jump matches it)
    juce::String rowName (int row) const
    {
        if (! juce::isPositiveAndBelow (row, (int) rows.size()))
            return {};
        const auto& n = *rows[(std::size_t) row];
        return n.label.isNotEmpty() ? n.label : n.folder || ! isAudio (n.file) ? n.file.getFileName() : n.file.getFileNameWithoutExtension();
    }

    bool selectedIsFolder() const { return juce::isPositiveAndBelow (selected, (int) rows.size()) && rows[(std::size_t) selected]->folder; }
    int numRows() const { return (int) rows.size(); }
    const Node* nodeAt (int row) const { return juce::isPositiveAndBelow (row, (int) rows.size()) ? rows[(std::size_t) row] : nullptr; } // (round 190kx)
    // round 190ke (--simplegb190keshot): the rows' names as shown
    juce::StringArray rowLabels() const
    {
        juce::StringArray out;
        for (const auto* n : rows)
            out.add (n->label.isNotEmpty() ? n->label : n->file.getFileName());
        return out;
    }
    int getSelectedRow() const { return selected; }

    void select (int row, bool preview)
    {
        row = rows.empty() ? -1 : juce::jlimit (0, (int) rows.size() - 1, row);
        if (row != selected)
        {
            repaintRow (selected);
            selected = row;
            repaintRow (selected);
        }
        scrollToRow (row);
        if (row >= 0)
            browser.fileSelected (rows[(std::size_t) row]->file, preview && ! rows[(std::size_t) row]->folder);
    }

    // Left / Right: close the selected folder (or go to its parent) / open it
    void openSelected (bool open)
    {
        if (! juce::isPositiveAndBelow (selected, (int) rows.size()))
            return;
        auto* n = rows[(std::size_t) selected];

        if (n->folder && n->expanded != open)
        {
            toggle (*n);
            return;
        }

        if (! open && n->parent != nullptr && n->parent != root.get())
            for (int i = 0; i < (int) rows.size(); ++i)
                if (rows[(std::size_t) i] == n->parent)
                {
                    select (i, false);
                    return;
                }
    }

    void collapseAll()
    {
        if (root == nullptr)
            return;
        std::function<void (Node&)> close = [&close] (Node& n) {
            for (auto& c : n.children)
            {
                c->expanded = false;
                close (*c);
            }
        };
        close (*root);
        selected = hovered = -1;
        rebuild();
    }

    void expandAll (Node& n)
    {
        n.expanded = true;
        load (n);
        if (n.kind == Kind::project || n.kind == Kind::category || n.kind == Kind::factory)
            for (auto& c : n.children)
                if (c->folder && c->kind != Kind::fsFolder)
                    expandAll (*c);
        rebuild();
    }

    // FL's folder "Expand all": the folder and its sub-folders open (on disk: down to `depth` levels, so a big
    // library is not scanned at once)
    // --snapshot: opens the top-level folder `f` and its sub-folders (round 164)
    void expandTop (const juce::File& f, int depth)
    {
        if (root != nullptr)
            for (auto& c : root->children)
                if (c->kind == Kind::fsFolder && c->file == f)
                    expandFolder (*c, depth);
    }

    // Opens the folders down to `file` (a file or folder under one of the top-level folders) and returns its row, or
    // -1 when no folder shown here holds it (round 190ce: hot-swap shows the linked sample, "Show in its folder").
    int revealFile (const juce::File& file)
    {
        if (root == nullptr || flat)
            return -1;
        for (auto& top : root->children)
        {
            if (top->kind != Kind::fsFolder)
                continue;
            juce::Array<juce::File> bases { top->file };
            bases.addArray (top->also);
            const auto holds = [&file] (const juce::File& folder) { return folder.isDirectory() && (file == folder || file.isAChildOf (folder)); };
            if (std::none_of (bases.begin(), bases.end(), holds))
                continue;
            auto* node = top.get();
            for (int guard = 0; guard < 64 && node != nullptr; ++guard)
            {
                node->expanded = true;
                load (*node);
                Node* next = nullptr;
                for (auto& c : node->children)
                    if (c->file == file)
                    {
                        rebuild();
                        return rowOfFile (file);
                    }
                    else if (c->folder && file.isAChildOf (c->file))
                        next = c.get();
                node = next;
            }
            rebuild();
        }
        return -1;
    }

    void expandFolder (Node& n, int depth)
    {
        n.expanded = true;
        load (n);
        if (depth > 1)
            for (auto& c : n.children)
                if (c->folder)
                    expandFolder (*c, depth - 1);
        rebuild();
    }

    void rebuild()
    {
        rows.clear();

        if (flat)
            for (auto& n : flatNodes)
                rows.push_back (n.get());
        else if (root != nullptr)
            addRows (*root);
        if (! flat && browser.showsLoopRowInfo())
            askLoopInfo();

        updateSize();
        repaint();
    }

    // round 190kx: the Simple layout's rows of audio files show their bars, tempo and key (BandLab's loop rows): the
    // listed files are analysed once (ui/LoopInfo's worker, cached per file), answers fill the rows as they come
    void askLoopInfo()
    {
        std::vector<std::string> wanted;
        for (const auto* n : rows)
            if (n->kind == Kind::file && ! n->folder && ! n->loopKnown && isAudio (n->file))
                if (wanted.size() < 1000)
                    wanted.push_back (n->file.getFullPathName().toStdString());
        if (wanted.empty())
            return;
        auto& shared = browser.loopInfoShared();
        refreshLoopTexts();
        shared.request (wanted);
    }

    void refreshLoopTexts()
    {
        if (browser.loopInfo == nullptr)
            return;
        const auto meter = browser.engine.getSong().meter();
        bool changed = false;
        for (auto* n : rows)
            if (n->kind == Kind::file && ! n->folder && ! n->loopKnown)
                if (const auto r = browser.loopInfo->known (n->file.getFullPathName().toStdString()))
                {
                    n->loopKnown = true;
                    n->loopText = loopinfo::rowText (*r, meter);
                    changed = true;
                }
        if (changed)
            repaint();
    }

    void updateSize()
    {
        if (auto* parent = getParentComponent())
        {
            auto width = parent->getWidth();
            const auto viewHeight = parent->getParentComponent() != nullptr ? parent->getParentComponent()->getHeight() : parent->getHeight();
            // round gaps190kf: the Loop Browser's list keeps its Tempo / Key column and stars clear of the scroll bar
            if (auto* view = dynamic_cast<juce::Viewport*> (parent->getParentComponent());
                view != nullptr && flat && browser.isLoopsShown() && (int) rows.size() * rowHeight > viewHeight)
                width = view->getWidth() - view->getScrollBarThickness();
            setSize (width, juce::jmax (viewHeight, (int) rows.size() * rowHeight));
        }
    }

    // The project changed: the open sections are listed again (only those whose items changed), keeping what is
    // open and selected.
    void refreshProject()
    {
        if (root == nullptr)
            return;

        bool changed = false;
        const auto key = selectionKey();
        std::function<void (Node&)> visit = [&] (Node& n) {
            if (n.kind == Kind::category)
            {
                Node fresh;
                fresh.kind = Kind::category;
                fresh.category = n.category;
                fresh.depth = n.depth;
                listCategory (fresh, browser.engine.getSong(), &browser.engine, ! n.loaded);
                if (n.detail != fresh.detail)
                {
                    n.detail = fresh.detail;
                    changed = true;
                }
                if (! n.loaded)
                    return;
                if (signature (fresh) != signature (n))
                {
                    n.children = std::move (fresh.children);
                    for (auto& c : n.children)
                        c->parent = &n;
                    changed = true;
                }
                else
                    for (std::size_t i = 0; i < n.children.size(); ++i) // the same items: only the "current" marks
                        n.children[i]->current = fresh.children[i]->current;
                return;
            }
            for (auto& c : n.children)
                if (c->kind == Kind::project || c->kind == Kind::category)
                    visit (*c);
        };
        visit (*root);
        pendingKey = key;

        if (changed)
        {
            rebuild();
            restoreSelection (pendingKey);
        }
        else
            repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme::colour;
        g.fillAll (bg2);
        const auto clip = g.getClipBounds();
        const auto first = juce::jmax (0, clip.getY() / rowHeight), last = juce::jmin ((int) rows.size() - 1, clip.getBottom() / rowHeight);
        const auto previewing = browser.isPreviewPlaying() ? browser.previewedFile : juce::File();
        const auto live = rowHeight == liveRowHeight; // (round 190iz: the Clip-launch layout's Live-style list)
        const auto loopRows = ! flat && browser.showsLoopRowInfo(); // (round 190kx: the Simple layout's loop rows)
        juce::RectangleList<int> guides;              // (round 190iz: FL's dotted tree guides, filled at once below)

        for (int i = first; i <= last; ++i)
        {
            const auto& n = *rows[(std::size_t) i];
            auto r = juce::Rectangle<int> (0, i * rowHeight, getWidth(), rowHeight);
            if (! flat && ! live && n.depth > 0)
                addGuides (guides, n, r);

            // an open project section sits on a slightly raised band (FL draws its open folders on a band)
            if ((n.kind == Kind::project || n.kind == Kind::category) && n.expanded)
            {
                g.setColour (bg3);
                g.fillRect (r);
            }

            if (i == selected)
            {
                g.setColour (accentMuted);
                g.fillRect (r);
                g.setColour (accent);
                g.fillRect (r.withWidth (2));
            }
            else if (i == hovered)
            {
                g.setColour (bg4);
                g.fillRect (r);
            }

            const auto indent = flat ? 6 : 6 + n.depth * 14;
            auto x = (float) indent;

            if (n.folder) // a chevron: right when closed, down when open
            {
                juce::Path p;
                const auto c = juce::Point<float> (x + 5.0f, (float) r.getCentreY());
                if (n.expanded)
                    p.addTriangle (c.x - 3.5f, c.y - 2.0f, c.x + 3.5f, c.y - 2.0f, c.x, c.y + 2.5f);
                else
                    p.addTriangle (c.x - 2.0f, c.y - 3.5f, c.x - 2.0f, c.y + 3.5f, c.x + 2.5f, c.y);
                g.setColour (i == hovered || i == selected ? textPrimary : textSecondary);
                g.fillPath (p);
            }
            if (! flat || n.kind != Kind::item)
                x += 12.0f;

            const auto playing = ! n.folder && n.file != juce::File() && n.file == previewing;
            const auto hasColour = ! n.colour.isTransparent();
            auto iconColour = playing ? accent : hasColour ? readable (n.colour) : n.folder ? warning.withAlpha (0.85f) : textSecondary;
            if (n.missing)
                iconColour = meterClip;
            const auto icon = n.icon != Icon::none ? n.icon : iconFor (n.file, n.folder);
            if (n.kind == Kind::collection) // a collection's heading: its colour as a square
            {
                g.setColour (n.colour);
                g.fillRoundedRectangle (juce::Rectangle<float> (x + 3.0f, (float) r.getY() + 6.0f, 10.0f, 10.0f), 2.0f);
            }
            else
                icons::draw (g, icon, juce::Rectangle<float> (x, (float) r.getY() + 3.0f, 16.0f, 16.0f), iconColour);
            x += 21.0f;

            auto text = r.withLeft ((int) x).withTrimmedRight (6);

            // round 190iz (FL, REA_rodada115 #5): the favourite star at the row's end, on the hovered row and on the
            // starred ones (filled); the Clip-launch layout keeps Live's colour squares only
            const auto rowItem = itemOf (n);
            const auto starred = ! rowItem.empty() && BrowserComponent::collectionStore().has (rowItem, 0);
            if (! live && ! rowItem.empty() && (starred || i == hovered))
            {
                const auto box = starBox (i).toFloat().withSizeKeepingCentre (12.0f, 12.0f);
                const auto overStar = i == hovered && starBox (i).contains (getMouseXYRelative());
                browsertabicons::drawRowStar (g, box, starred, starred ? warning : overStar ? textPrimary : textSecondary);
                text.setRight (starBox (i).getX() - 2);
            }

            // the collections the row is in: one small square per colour at the right edge (round 190ce; round 190iz:
            // Starred is the star in the FL layout)
            if (const auto item = rowItem; ! item.empty())
                if (const auto bits = (std::uint8_t) (BrowserComponent::collectionStore().mask (item) & (live ? 0x7f : 0x7e)); bits != 0)
                {
                    for (int c = collections::count - 1; c >= 0; --c)
                        if ((bits >> c & 1) != 0)
                        {
                            const auto chip = text.removeFromRight (9).withSizeKeepingCentre (7, 7);
                            g.setColour (BrowserComponent::collectionColour (c));
                            g.fillRoundedRectangle (chip.toFloat(), 1.5f);
                        }
                    text.removeFromRight (3);
                }

            const auto flatDetail = flat && n.kind == Kind::file && n.detail.isNotEmpty(); // "87%" of a similar sample
            if (flat && n.kind == Kind::file) // the folder it lives in, on the right (similar samples: how similar)
            {
                const auto where = flatDetail ? n.detail : n.file.getParentDirectory().getRelativePathFrom (baseOf (n.file));
                g.setFont (theme::font (theme::type::caption));
                g.setColour (textDisabled);
                const auto w = juce::jmin (text.getWidth() / 2, juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), where) + 4);
                g.drawText (where, text.removeFromRight (w), juce::Justification::centredRight, true);
            }

            // round 190kx: a loop's bars, tempo and key on the right (the Simple layout, BandLab's loop rows)
            if (n.loopText.isNotEmpty() && loopRows)
            {
                g.setFont (theme::font (theme::type::caption));
                g.setColour (textDisabled);
                const auto w = juce::jmin (text.getWidth() / 2, juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), n.loopText) + 6);
                g.drawText (n.loopText, text.removeFromRight (w), juce::Justification::centredRight, true);
            }

            const auto name = n.label.isNotEmpty() ? n.label
                              : n.folder || ! isAudio (n.file) ? n.file.getFileName()
                                                               : n.file.getFileNameWithoutExtension();
            const auto bold = (n.folder && n.expanded) || n.current || n.kind == Kind::project;
            // round 190iz: FL's tree face one step larger (FM-9); Live's list a step smaller. Round 190je: the FL tree is
            // condensed with FL-style labels on (theme::labelFont = theme::font while the option is off)
            const juce::Font font (live ? theme::font (theme::type::small, bold) : theme::labelFont (theme::type::body + 1.0f, bold));
            g.setFont (font);

            auto nameColour = playing ? accentHover : n.folder || i == hovered || i == selected ? textPrimary : textPrimary.withAlpha (0.86f);
            if (n.kind == Kind::item && hasColour && n.item.kind == projectcontents::Kind::pattern)
                nameColour = readable (n.colour); // FL writes the patterns in their colours
            if (n.missing)
                nameColour = meterClip.withAlpha (0.9f);
            if (n.kind == Kind::item && n.history && ! n.current && n.item.index > browser.engine.getHistoryPosition())
                nameColour = textDisabled; // undone steps (redo)
            g.setColour (nameColour);

            const auto nameWidth = juce::GlyphArrangement::getStringWidthInt (font, name);
            g.drawText (name, text, juce::Justification::centredLeft, true);

            // the detail after the name, dimmed: "(insert)", "3x", "missing", a section's count
            if (n.detail.isNotEmpty() && ! flatDetail && nameWidth + 12 < text.getWidth())
            {
                g.setFont (theme::font (theme::type::small));
                g.setColour (n.missing ? meterClip.withAlpha (0.7f) : textDisabled);
                const auto shown = n.kind == Kind::category || n.kind == Kind::project ? n.detail : "(" + n.detail + ")";
                if (n.kind == Kind::category) // the count on the right
                    g.drawText (shown, text, juce::Justification::centredRight, true);
                else
                    g.drawText (shown, text.withTrimmedLeft (nameWidth + 6), juce::Justification::centredLeft, true);
            }
        }

        if (! guides.isEmpty())
        {
            g.setColour (border.brighter (0.25f));
            g.fillRectList (guides);
        }

        if (juce::isPositiveAndBelow (dropRow, (int) rows.size())) // files from the system would go here
        {
            g.setColour (accent);
            g.drawRect (juce::Rectangle<int> (0, dropRow * rowHeight, getWidth(), rowHeight), 2);
        }

        if (rows.empty() && status.isNotEmpty())
        {
            g.setColour (textDisabled);
            g.setFont (theme::font (theme::type::small));
            g.drawFittedText (status, getLocalBounds().reduced (10).withHeight (60), juce::Justification::centredTop, 3);
        }
    }

    // FL's tree guides (round 190iz, FM-9: "a dotted tree connector"): 1 px dots every 2 px from each open parent down
    // its children, and a short dotted tick into each child row
    void addGuides (juce::RectangleList<int>& out, const Node& n, juce::Rectangle<int> r) const
    {
        const auto column = [] (int depth) { return 6 + depth * 14 + 5; }; // a parent's chevron centre
        const auto mid = r.getCentreY();
        auto vertical = [&out] (int x, int y0, int y1) {
            for (int y = y0 + (y0 & 1); y < y1; y += 2)
                out.addWithoutMerging ({ x, y, 1, 1 });
        };
        const auto* parent = n.parent;
        if (parent == nullptr || parent->depth < 0)
            return;
        const auto isLast = [] (const Node& c) { return c.parent == nullptr || c.parent->children.empty() || c.parent->children.back().get() == &c; };
        // the row's own branch: down to its middle (to the bottom when more siblings follow), then across
        const auto x = column (n.depth - 1);
        vertical (x, r.getY(), isLast (n) ? mid + 1 : r.getBottom());
        for (int xx = x + 2; xx < x + 9; xx += 2)
            out.addWithoutMerging ({ xx, mid, 1, 1 });
        // the lines of the ancestors that still have children below this row
        for (const auto* a = parent; a != nullptr && a->depth > 0; a = a->parent)
            if (! isLast (*a))
                vertical (column (a->depth - 1), r.getY(), r.getBottom());
    }

    int rowAt (int y) const
    {
        const auto row = y / rowHeight;
        return y >= 0 && row < (int) rows.size() ? row : -1;
    }

    // --- files dragged in from the system (issue #12): copied into the folder under the mouse ----------------------
    // The folder row a drop at y lands in: a folder on disk, or a file's own folder; -1 = nowhere to copy to.
    int dropFolderRow (int y) const
    {
        auto row = rowAt (y);
        if (row < 0)
            return -1;
        const Node* n = rows[(std::size_t) row];
        if (n->kind == Kind::file)
            n = n->parent;
        if (n == nullptr || n->kind != Kind::fsFolder || ! n->file.isDirectory())
            return -1;
        const auto it = std::find (rows.begin(), rows.end(), n);
        return it != rows.end() ? (int) (it - rows.begin()) : -1;
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        // the files the Browser lists (samples, MIDI, projects, presets): an other file would not show in the folder
        // (issue #200: folders alone go to the Browser itself, which adds them to its list)
        return ! flat && std::any_of (files.begin(), files.end(), [] (const juce::String& f) {
            const juce::File file (f);
            return file.existsAsFile() && isListed (file);
        });
    }

    void fileDragMove (const juce::StringArray&, int, int y) override
    {
        if (const auto row = dropFolderRow (y); row != dropRow)
        {
            dropRow = row;
            repaint();
        }
    }

    void fileDragExit (const juce::StringArray&) override
    {
        dropRow = -1;
        repaint();
    }

    void filesDropped (const juce::StringArray& files, int, int y) override
    {
        dropRow = -1;
        repaint();
        // issue #200 (round 190ll): folders dropped with the files join the Browser's folder list (FL)
        juce::StringArray folders;
        for (const auto& path : files)
            if (juce::File (path).isDirectory())
                folders.add (path);
        if (! folders.isEmpty() && browser.addUserFolders (folders) > 0)
            return; // (the tree was built again: its rows are gone; the files stay where they are)
        const auto row = dropFolderRow (y);
        if (row < 0)
        {
            browser.showNote (tr ("Drop the files on a folder to copy them there"));
            return;
        }
        auto* folderNode = rows[(std::size_t) row];
        const auto folder = folderNode->file;
        int copied = 0;
        juce::File first;
        for (const auto& path : files)
        {
            const juce::File source (path);
            if (! source.existsAsFile() || ! isListed (source) || source.getParentDirectory() == folder)
                continue;
            // never over a file that is there: "Kick (2).wav"
            auto target = folder.getChildFile (source.getFileName());
            if (target.exists())
                target = folder.getNonexistentChildFile (source.getFileNameWithoutExtension(), source.getFileExtension(), true);
            if (source.copyFileTo (target))
            {
                ++copied;
                if (first == juce::File())
                    first = target;
            }
        }
        if (copied == 0)
        {
            browser.showNote (tr ("Nothing was copied to {0}", { folder.getFileName() }));
            return;
        }
        // the folder lists again, open, with the first copy selected
        folderNode->children.clear();
        folderNode->loaded = false;
        folderNode->expanded = true;
        load (*folderNode);
        rebuild();
        for (int i = 0; i < (int) rows.size(); ++i)
            if (rows[(std::size_t) i]->file == first)
            {
                select (i, false);
                break;
            }
        browser.showNote (copied == 1 ? tr ("Copied 1 file to {0}", { folder.getFileName() })
                                      : tr ("Copied {0} files to {1}", { juce::String (copied), folder.getFileName() }));
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setHovered (rowAt (e.y));
        // round 190iz: the hover star lights under the mouse (only its box repaints)
        if (const auto over = starBox (hovered).contains (e.getPosition()); over != overStar)
        {
            overStar = over;
            repaint (starBox (hovered));
        }
    }
    void mouseExit (const juce::MouseEvent&) override
    {
        setHovered (-1);
        overStar = false;
    }

    // FL's middle-click / "Send to selected channel or focused plugin" on a row (round 190iz)
    void sendRow (const Node& n)
    {
        if (n.kind == Kind::plugin)
            browser.sendPluginToSelected (n.generator, n.effect);
        else if (! n.folder && n.file.existsAsFile() && ! n.missing)
            browser.sendToSelected (n.file);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragStarted = false;
        const auto row = rowAt (e.y);
        pressedRow = row;

        if (row < 0)
            return;

        auto& n = *rows[(std::size_t) row];

        // round 190iz (FL manual browser.htm): the star at the row's end = favourite; the middle button = send to the
        // selected channel; Alt+Click = the whole sample (not FL's first 5 seconds)
        if (e.mods.isMiddleButtonDown())
        {
            select (row, false);
            sendRow (n);
            return;
        }
        if (e.mods.isLeftButtonDown() && rowHeight != liveRowHeight && starBox (row).contains (e.getPosition()))
        {
            browser.toggleStarredItem (itemOf (n));
            repaintRow (row);
            return;
        }
        if (e.mods.isAltDown() && ! n.folder)
            browser.askPreview (0.0, true);

        if (e.mods.isPopupMenu())
        {
            select (row, false);
            showMenu (n);
            return;
        }

        if (n.folder)
        {
            select (row, false);
            if (e.getNumberOfClicks() == 1) // a click opens / closes it (the second click of a double click does not undo it)
                toggle (n);
            return;
        }

        if (n.kind == Kind::item)
        {
            select (row, n.item.kind == projectcontents::Kind::sample && ! n.missing);
            itemClicked (n);
            return;
        }
        if (n.kind == Kind::plugin)
        {
            select (row, false);
            return;
        }

        // a click previews, even when the file is already selected (click again: hear it again)
        select (row, true);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragStarted || e.getDistanceFromDragStart() < 5 || ! juce::isPositiveAndBelow (pressedRow, (int) rows.size()))
            return;

        const auto& n = *rows[(std::size_t) pressedRow];
        auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this);

        if (n.folder || container == nullptr)
            return;

        if (n.kind == Kind::plugin && (n.generator || n.effect)) // a plugin onto the Channel Rack / the Mixer (round 162)
        {
            dragStarted = true;
            draggedPlugin() = { n.generator, n.effect };
            container->startDragging (juce::String (n.generator ? generatorDragDescription : effectDragDescription) + "|" + n.label, this,
                                      juce::ScaledImage (dragImage (n)), true);
            return;
        }

        if (n.kind == Kind::item && n.item.kind == projectcontents::Kind::pattern) // a pattern onto the Playlist
        {
            dragStarted = true;
            container->startDragging (juce::String (patternDragDescription) + "|" + juce::String (n.item.index), this,
                                      juce::ScaledImage (dragImage (n)), true);
            return;
        }

        if (n.file.existsAsFile())
        {
            dragStarted = true;
            container->startDragging (juce::String (dragDescription) + "|" + n.file.getFullPathName(), this, juce::ScaledImage (dragImage (n)),
                                      true);
        }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto row = rowAt (e.y);

        if (row < 0 || rows[(std::size_t) row]->folder || ! e.mods.isLeftButtonDown())
            return;

        const auto& n = *rows[(std::size_t) row];
        if (browser.isHotSwapping() && (n.kind == Kind::plugin || n.kind == Kind::file)) // round 190ce: it replaces the linked device
        {
            select (row, false);
            browser.hotSwapLoadSelected();
            return;
        }
        if (n.kind == Kind::plugin)
        {
            if (n.activate)
                n.activate();
            return;
        }
        if (n.kind != Kind::item)
        {
            browser.openFile (n.file);
            return;
        }

        const auto& item = n.item;
        switch (item.kind)
        {
            case projectcontents::Kind::pattern:
                if (browser.onOpenPattern)
                    browser.onOpenPattern (item.index);
                break;
            case projectcontents::Kind::sample:
                if (! n.missing)
                    browser.openFile (n.file);
                break;
            case projectcontents::Kind::generator:
                if (browser.onOpenChannel)
                    browser.onOpenChannel (item.index);
                break;
            case projectcontents::Kind::effect:
                if (browser.onOpenEffect)
                    browser.onOpenEffect (item.index, item.slot);
                break;
            case projectcontents::Kind::automation:
                if (! n.history && ! n.initial && browser.onOpenAutomation)
                    browser.onOpenAutomation (item.index);
                break;
        }
    }

    ui::Hint hintAt (juce::Point<int> p) override
    {
        ui::Hint h;
        const auto row = rowAt (p.y);

        if (row < 0)
        {
            h.text = flat ? tr ("Search results: files in the current folder and below that match") : tr ("Browser: [LMB] open a folder or preview a sample");
            return h;
        }

        const auto& n = *rows[(std::size_t) row];
        h.hasMenu = true;
        if (starBox (row).contains (p) && rowHeight != liveRowHeight) // round 190iz: FL's favourite star
        {
            h.text = BrowserComponent::collectionStore().has (itemOf (n), 0) ? tr ("Favorite: [LMB] take it out of the STARRED tab")
                                                                              : tr ("Favorite: [LMB] add it to the STARRED tab");
            return h;
        }

        switch (n.kind)
        {
            case Kind::project:
                h.text = tr ("Current project: what this project uses (patterns, effects, generators, automation, audio files)  [LMB] open  [RMB] expand / collapse all");
                return h;
            case Kind::category:
                h.text = categoryHint (n.category) + "  " + (n.expanded ? tr ("[LMB] close") : tr ("[LMB] open"));
                h.value = n.detail;
                return h;
            case Kind::fsFolder:
                if (n.depth == 0 || (n.parent != nullptr && n.parent->kind == Kind::project))
                {
                    h.text = n.label + ": " + n.file.getFullPathName() + "  " + (n.expanded ? tr ("[LMB] close") : tr ("[LMB] open")) + "  " + tr ("[RMB] menu");
                    return h;
                }
                break;
            case Kind::item:
                return itemHint (n);
            case Kind::plugins:
                h.text = tr ("Plugin database: LYNX SHARE's built-in generators and effects, FL's engines and the installed plugins") + "  "
                         + (n.expanded ? tr ("[LMB] close") : tr ("[LMB] open"));
                h.value = n.detail;
                return h;
            case Kind::plugin:
                if (factorycontent::isKey (n.pluginKey) && ! n.generator) // round 190ke: a beat or an effect chain
                {
                    h.text = n.label + " (" + n.detail + ")  " + tr ("[double-click] add it: a beat as a new Drums track at the playhead, a chain on the selected track");
                    h.hasMenu = true;
                    return h;
                }
                h.text = n.label + " (" + n.detail + ")  "
                         + (n.generator ? tr ("[double-click] add it as a new channel, or drag it onto the Channel Rack")
                                        : tr ("[double-click] add it to the selected insert, or drag it onto a mixer insert or an effect slot"));
                h.hasMenu = true;
                return h;
            case Kind::factory:
                h.text = tr ("LYNX content: LYNX's own sounds, beats and effect chains, with tags (the TAGS button filters them)") + "  "
                         + (n.expanded ? tr ("[LMB] close") : tr ("[LMB] open"));
                h.value = n.detail;
                return h;
            case Kind::collection:
                h.text = tr ("Collection: the items you put in it (right-click an item > Collections, or the keys 1-7 on a selected row)") + "  "
                         + (n.expanded ? tr ("[LMB] close") : tr ("[LMB] open")) + "  " + tr ("[RMB] menu");
                h.value = n.detail;
                return h;
            case Kind::file:
                break;
        }

        h.text = rowHint (n.file, n.folder, n.expanded);
        h.value = n.folder ? juce::String() : n.file.getFileExtension().substring (1).toUpperCase();
        return h;
    }

    // for snapshots: the first audio file under the first folders on disk (opening them)
    int firstAudioRow()
    {
        for (int pass = 0; pass < 3; ++pass)
        {
            for (int i = 0; i < (int) rows.size(); ++i)
                if (rows[(std::size_t) i]->kind == Kind::file && isAudio (rows[(std::size_t) i]->file))
                    return i;

            for (auto* n : rows)
                if (n->kind == Kind::fsFolder && ! n->expanded)
                {
                    toggle (*n);
                    break;
                }
        }
        return -1;
    }

    // the first row labelled `label`, or -1
    int rowOfFile (const juce::File& file) const
    {
        for (int i = 0; i < (int) rows.size(); ++i)
            if (rows[(std::size_t) i]->file == file)
                return i;
        return -1;
    }

    int rowLabelled (const juce::String& label) const
    {
        for (int i = 0; i < (int) rows.size(); ++i)
            if (rows[(std::size_t) i]->label == label)
                return i;
        return -1;
    }

    // for snapshots: opens these project sections; returns the first row of a sample item
    void scrollRowIntoView (int row) { scrollToRow (row); }
    bool categoryOpen (Category c) const
    {
        const auto row = categoryRow (c);
        return row >= 0 && rows[(std::size_t) row]->expanded;
    }
    // round 190mr (DF-135): the row of a project section's header (−1: not listed)
    int categoryRow (Category c) const
    {
        for (int i = 0; i < (int) rows.size(); ++i)
            if (rows[(std::size_t) i]->kind == Kind::category && rows[(std::size_t) i]->category == c)
                return i;
        return -1;
    }

    int openProjectSections (std::initializer_list<Category> open)
    {
        for (auto* n : std::vector<Node*> (rows))
            if (n->kind == Kind::category && std::find (open.begin(), open.end(), n->category) != open.end() && ! n->expanded)
            {
                n->expanded = true;
                load (*n);
            }
        rebuild();
        for (int i = 0; i < (int) rows.size(); ++i)
            if (rows[(std::size_t) i]->kind == Kind::item && rows[(std::size_t) i]->item.kind == projectcontents::Kind::sample)
                return i;
        return -1;
    }

    // round 190my (FL's Project picker, right-click on a pattern tile): the pattern's row in Current project › Patterns,
    // selected and in view; −1 when it isn't listed
    int revealPattern (int index)
    {
        openProjectSections ({ Category::patterns });
        for (int i = 0; i < (int) rows.size(); ++i)
            if (rows[(std::size_t) i]->kind == Kind::item && rows[(std::size_t) i]->item.kind == projectcontents::Kind::pattern
                && rows[(std::size_t) i]->item.index == index)
            {
                select (i, false);
                scrollToRow (i);
                return i;
            }
        return -1;
    }

    // round 190iz (FL: "drag animations", details DF-100): the drag picture of a file, for the preview strip's drags too
    juce::Image dragImageForFile (const juce::File& f) const
    {
        Node n;
        n.file = f;
        return dragImage (n);
    }

    // the name row `row` shows (round 190iz: --shots pick rows by it)
    juce::String shownName (int row) const
    {
        if (! juce::isPositiveAndBelow (row, (int) rows.size()))
            return {};
        const auto& n = *rows[(std::size_t) row];
        return n.label.isNotEmpty() ? n.label : n.folder || ! isAudio (n.file) ? n.file.getFileName() : n.file.getFileNameWithoutExtension();
    }

    void setHovered (int row)
    {
        if (row != hovered)
        {
            repaintRow (hovered);
            hovered = row;
            repaintRow (hovered);
        }
    }

    // flat mode, PROJECT tab search: every project item whose name matches
    static std::vector<std::unique_ptr<Node>> projectMatches (const Song& song, const juce::String& query)
    {
        const auto parsed = search::parse (query.toStdString());
        std::vector<std::unique_ptr<Node>> found;
        for (auto c : categories)
        {
            if (c == Category::history)
                continue;
            Node section;
            section.kind = Kind::category;
            section.category = c;
            listCategory (section, song, {});
            for (auto& child : section.children)
                if (search::matches (parsed, child->label.toStdString()))
                {
                    child->parent = nullptr;
                    found.push_back (std::move (child));
                }
        }
        return found;
    }

private:
    juce::File baseOf (const juce::File& f) const
    {
        for (const auto& b : bases)
            if (f.isAChildOf (b))
                return b;
        return f.getParentDirectory();
    }

    void load (Node& n)
    {
        if (n.loaded)
            return;
        n.loaded = true;

        if (n.kind == Kind::project)
        {
            for (auto c : categories)
            {
                auto child = std::make_unique<Node>();
                child->kind = Kind::category;
                child->folder = true;
                child->category = c;
                child->label = categoryName (c);
                child->icon = categoryIcon (c);
                child->depth = n.depth + 1;
                child->parent = &n;
                listCategory (*child, browser.engine.getSong(), &browser.engine, true); // the counts show while closed
                child->children.clear();
                n.children.push_back (std::move (child));
                if (c == Category::initialized && browser.projectFile.existsAsFile()) // then the project's folder (FL's "Project data")
                {
                    auto folder = makeFolder (browser.projectFile.getParentDirectory(), tr ("Project folder"), juce::Colour());
                    folder->icon = Icon::folder;
                    folder->depth = n.depth + 1;
                    folder->parent = &n;
                    n.children.push_back (std::move (folder));
                }
            }
            return;
        }

        if (n.kind == Kind::category)
        {
            loadCategory (n);
            return;
        }

        auto list = [] (const juce::File& folder, juce::Array<juce::File>& folders, juce::Array<juce::File>& files) {
           #if JUCE_MAC
            // not JUCE's iterator on macOS: one name NSString rejects would end the app as the folder opens (issue #42)
            for (const auto& c : childrenOf (folder))
                (c.isDirectory() ? folders : files).add (c);
           #else
            folders = folder.findChildFiles (juce::File::findDirectories, false, "*", juce::File::FollowSymlinks::noCycles);
            files = folder.findChildFiles (juce::File::findFiles, false, "*", juce::File::FollowSymlinks::noCycles);
           #endif
        };
        juce::Array<juce::File> folders, files;
        std::map<juce::String, juce::Array<juce::File>> alsoOf; // a merged subfolder's other copies (by its path)
        if (n.also.isEmpty())
            list (n.file, folders, files);
        else
        {
            // a merged root (FL's Scores, Mixer presets): the union of its folders, the first one's entry on a clash
            juce::Array<juce::File> sources { n.file };
            sources.addArray (n.also);
            std::vector<juce::Array<juce::File>> srcFolders (sources.size()), srcFiles (sources.size());
            std::vector<browserview::Listing> listings (sources.size());
            for (int i = 0; i < sources.size(); ++i)
            {
                list (sources[i], srcFolders[(std::size_t) i], srcFiles[(std::size_t) i]);
                for (const auto& f : srcFolders[(std::size_t) i])
                    listings[(std::size_t) i].folders.push_back (f.getFileName().toStdString());
                for (const auto& f : srcFiles[(std::size_t) i])
                    listings[(std::size_t) i].files.push_back (f.getFileName().toStdString());
            }
            auto find = [] (const juce::Array<juce::File>& in, const std::string& name) {
                for (const auto& f : in)
                    if (f.getFileName().equalsIgnoreCase (juce::String (name)))
                        return f;
                return juce::File();
            };
            const auto merged = browserview::mergeListings (listings);
            for (const auto& e : merged.folders)
            {
                const auto f = find (srcFolders[(std::size_t) e.from], e.name);
                folders.add (f);
                for (const auto a : e.also)
                    alsoOf[f.getFullPathName()].add (find (srcFolders[(std::size_t) a], e.name));
            }
            for (const auto& e : merged.files)
                files.add (find (srcFiles[(std::size_t) e.from], e.name));
        }
        auto byName = [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; };
        std::sort (folders.begin(), folders.end(), byName);
        std::sort (files.begin(), files.end(), byName);

        for (const auto& f : folders)
        {
            if (f.isHidden() || f.getFileName().startsWithChar ('.'))
                continue;
            const auto also = alsoOf.count (f.getFullPathName()) ? alsoOf[f.getFullPathName()] : juce::Array<juce::File>();
            if (browser.filterHides (f) && std::all_of (also.begin(), also.end(), [this] (const juce::File& a) { return browser.filterHides (a); }))
                continue; // nothing in it is listed under the content filter (round 164)
            auto c = std::make_unique<Node>();
            c->kind = Kind::fsFolder;
            c->file = f;
            c->also = also;
            c->folder = true;
            c->depth = n.depth + 1;
            c->parent = &n;
            n.children.push_back (std::move (c));
        }

        for (const auto& f : files)
            if (browser.lists (f))
            {
                auto c = std::make_unique<Node>();
                c->file = f;
                c->depth = n.depth + 1;
                c->parent = &n;
                n.children.push_back (std::move (c));
            }
    }

    void loadCategory (Node& n)
    {
        n.loaded = true;
        n.children.clear();
        listCategory (n, browser.engine.getSong(), &browser.engine);
    }

    // The items of one project section (history needs the engine; the PROJECT search passes none).
    static void listCategory (Node& n, const Song& song, AudioEngine* engine, bool countOnly = false)
    {
        auto add = [&n] (projectcontents::Item item, Icon icon, juce::Colour colour) -> Node& {
            auto c = std::make_unique<Node>();
            c->kind = Kind::item;
            c->label = juce::String::fromUTF8 (item.name.c_str());
            c->detail = juce::String::fromUTF8 (item.detail.c_str());
            c->colour = colour;
            c->icon = icon;
            c->depth = n.depth + 1;
            c->parent = &n;
            c->item = std::move (item);
            n.children.push_back (std::move (c));
            return *n.children.back();
        };

        switch (n.category)
        {
            case Category::history:
                if (engine != nullptr)
                {
                    // each label compares two whole states: only listed while the section is open, the last 30
                    const auto size = engine->getHistorySize(), at = engine->getHistoryPosition();
                    for (int i = size - 1; ! countOnly && i >= juce::jmax (0, size - 30); --i) // newest first, as FL lists it
                    {
                        const auto l = engine->getHistoryLabel (i);
                        projectcontents::Item item;
                        item.kind = projectcontents::Kind::automation; // (unused for history rows)
                        item.index = i;
                        item.name = tr (l.text, { juce::String::fromUTF8 (l.arg.c_str()) }).toStdString();
                        auto& node = add (std::move (item), Icon::undo, juce::Colour());
                        node.history = true;
                        node.current = i == at;
                    }
                    n.detail = juce::String (size);
                }
                break;
            case Category::patterns:
                for (auto& item : projectcontents::patterns (song))
                {
                    const auto colour = PlaylistComponent::patternColour (song, item.index);
                    const auto current = item.index == song.currentPattern;
                    if (item.uses == 0)
                        item.detail = tr ("not in the Playlist").toStdString();
                    add (std::move (item), Icon::pattern, colour).current = current;
                }
                break;
            case Category::effects:
                for (auto& item : projectcontents::effects (song))
                    add (item, item.plugin ? Icon::plugin : Icon::mixer, item.colour != 0 ? juce::Colour (item.colour) : juce::Colour());
                break;
            case Category::generators:
                for (auto& item : projectcontents::generators (song))
                {
                    const auto& channel = song.channels[(std::size_t) item.index];
                    const auto icon = item.plugin ? Icon::plugin
                                      : projectcontents::playsSampleFile (channel.instrument) ? Icon::audioFile
                                                                                               : Icon::keyboard;
                    const auto colour = channel.instrument == InstrumentKind::audioClip ? PlaylistComponent::audioClipColour (channel)
                                        : item.colour != 0                               ? juce::Colour (item.colour)
                                                                                         : juce::Colour();
                    const auto selected = engine != nullptr && engine->getSelectedChannel() == item.index;
                    add (std::move (item), icon, colour).current = selected;
                }
                break;
            case Category::automation:
                for (auto& item : projectcontents::automations (song))
                    add (item, Icon::automate, item.colour != 0 ? juce::Colour (item.colour) : theme::colour::automation);
                break;
            case Category::initialized:
                for (int i = 0; i < (int) song.initialValues.size(); ++i)
                {
                    const auto& v = song.initialValues[(std::size_t) i];
                    projectcontents::Item item;
                    item.kind = projectcontents::Kind::automation;
                    item.index = i;
                    item.name = (engine != nullptr ? describeAutomationTarget (*engine, v.target) : juce::String ("?")).toStdString();
                    item.detail = std::to_string ((int) std::lround (v.value * 100.0f)) + "%";
                    add (std::move (item), Icon::automate, theme::colour::automation).initial = true;
                }
                break;
            case Category::samples:
                for (auto& item : projectcontents::samples (song))
                {
                    const juce::String path = juce::String::fromUTF8 (item.path.c_str());
                    const juce::File file (juce::File::isAbsolutePath (path) ? path : juce::String());
                    auto colour = item.colour != 0 ? juce::Colour (item.colour) : theme::colour::audioClip;
                    if (item.index >= 0 && song.channels[(std::size_t) item.index].instrument == InstrumentKind::audioClip)
                        colour = PlaylistComponent::audioClipColour (song.channels[(std::size_t) item.index]);
                    auto& node = add (std::move (item), Icon::audioFile, colour);
                    node.file = file;
                    node.missing = ! countOnly && ! file.existsAsFile();
                    if (node.missing)
                        node.detail = tr ("missing");
                }
                break;
        }

        if (n.category != Category::history)
            n.detail = juce::String ((int) n.children.size());
    }

    static juce::String signature (const Node& n)
    {
        juce::String s;
        for (const auto& c : n.children)
            s << c->label << '|' << c->detail << '|' << (int) c->colour.getARGB() << '|' << (int) c->missing << '|' << c->item.index << '|'
              << c->item.slot << '\n';
        return s;
    }

    juce::String selectionKey() const
    {
        if (! juce::isPositiveAndBelow (selected, (int) rows.size()))
            return {};
        const auto& n = *rows[(std::size_t) selected];
        if (n.kind != Kind::item)
            return "f:" + n.file.getFullPathName() + n.label;
        return "i:" + juce::String ((int) n.item.kind) + ":" + juce::String (n.item.index) + ":" + juce::String (n.item.slot) + ":"
               + juce::String::fromUTF8 (n.item.path.c_str()) + (n.history ? "h" : "");
    }

    void restoreSelection (const juce::String& key)
    {
        if (key.isEmpty())
            return;
        const auto keep = selected;
        selected = -1;
        for (int i = 0; i < (int) rows.size(); ++i)
        {
            selected = i;
            if (selectionKey() == key)
                return;
        }
        selected = -1;
        juce::ignoreUnused (keep);
    }

    void itemClicked (Node& n)
    {
        auto& engine = browser.engine;
        const auto& item = n.item;

        if (n.history)
        {
            engine.jumpToHistory (item.index);
            return;
        }
        if (n.initial)
            return;

        switch (item.kind)
        {
            case projectcontents::Kind::pattern: // FL: clicking a pattern makes it the current one
                if (item.index != engine.getSong().currentPattern && item.index < (int) engine.getSong().patterns.size())
                    engine.editSong ([index = item.index] (Song& s) { s.currentPattern = index; }, false);
                break;
            case projectcontents::Kind::generator:
                if (item.index < (int) engine.getSong().channels.size())
                    engine.setSelectedChannel (item.index);
                break;
            case projectcontents::Kind::sample:
            case projectcontents::Kind::effect:
            case projectcontents::Kind::automation:
                break;
        }
    }

    ui::Hint itemHint (const Node& n) const
    {
        ui::Hint h;
        h.hasMenu = true;
        const auto& item = n.item;

        if (n.initial)
        {
            h.text = n.label + ": " + tr ("reset to this value when the song starts  [RMB] delete the initial value");
            h.value = n.detail;
            return h;
        }
        if (n.history)
        {
            h.text = tr ("Undo history: [LMB] go back to this step");
            h.value = tr ("Level {0}/{1}", { juce::String (item.index + 1), juce::String (browser.engine.getHistorySize()) });
            return h;
        }

        switch (item.kind)
        {
            case projectcontents::Kind::pattern:
                h.text = n.label + ": " + tr ("[LMB] make it the current pattern  [2xLMB] open in the Piano Roll  drag into the Playlist  [RMB] pattern menu");
                h.value = item.uses == 0 ? tr ("not in the Playlist") : tr ("{0} clips", { juce::String (item.uses) });
                break;
            case projectcontents::Kind::sample:
                h.text = juce::String::fromUTF8 (item.path.c_str()) + "  "
                         + (n.missing ? tr ("(missing: File > Missing files... finds it)")
                                      : tr ("[LMB] preview  [2xLMB] new channel  drag to the Playlist or the Channel Rack  [RMB] menu"));
                h.value = tr ("used {0}x", { juce::String (item.uses) });
                break;
            case projectcontents::Kind::generator:
                h.text = n.label + (n.detail.isNotEmpty() ? " (" + n.detail + ")" : juce::String()) + ": "
                         + tr ("[LMB] select the channel  [2xLMB] open its settings  [RMB] menu");
                break;
            case projectcontents::Kind::effect:
                h.text = n.label + " (" + n.detail + "): " + tr ("[2xLMB] open its editor  [RMB] menu");
                h.value = tr ("slot {0}", { juce::String (item.slot + 1) });
                break;
            case projectcontents::Kind::automation:
                h.text = n.label + ": " + tr ("[2xLMB] open the automation editor");
                h.value = item.uses == 0 ? tr ("not in the Playlist") : tr ("{0} clips", { juce::String (item.uses) });
                break;
        }
        return h;
    }

    // --- round 166f (issue #16): the user's Plugin database folders (FL's folders, shown as headings in the slot menus)
    void setPluginFolder (const std::string& key, bool instruments, const std::string& path)
    {
        auto& host = browser.engine.getPluginHost();
        auto folders = host.databaseFolders();
        plugincatalog::moveToFolder (folders, instruments, key, path);
        host.setDatabaseFolders (folders);
        browser.rebuildTree();
    }

    void askFolderName (const juce::String& title, const juce::String& initial, std::function<void (const std::string&)> done)
    {
        auto* window = new juce::AlertWindow (title, tr ("Folders hold plugins in the Plugin database; the mixer slot and the Channel Rack menus list each folder under its own heading. A / makes a subfolder (\"Stock/EQ\")."),
                                              juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("name", initial);
        window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([window, done] (int result) {
                                     const auto name = plugincatalog::cleanFolder (window->getTextEditorContents ("name").toStdString());
                                     if (result == 1 && ! name.empty() && done) done (name);
                                 }),
                                 true);
    }

    void addFolderItems (juce::PopupMenu& menu, Node& n)
    {
        auto safe = juce::Component::SafePointer<FileTree> (this);
        const bool instruments = n.folderKind == 1;
        const auto path = n.userFolder;
        auto update = [safe, instruments] (std::function<void (plugincatalog::Folders&)> change) {
            if (safe == nullptr) return;
            auto& host = safe->browser.engine.getPluginHost();
            auto folders = host.databaseFolders();
            change (folders);
            host.setDatabaseFolders (folders);
            safe->browser.rebuildTree();
        };
        menu.addSeparator();
        menu.addItem (path.empty() ? tr ("New folder...") : tr ("New subfolder..."), [safe, update, instruments, path] {
            if (safe != nullptr)
                safe->askFolderName (tr ("New folder"), {}, [update, instruments, path] (const std::string& name) {
                    update ([=] (plugincatalog::Folders& f) { plugincatalog::addFolder (f, instruments, path.empty() ? name : path + "/" + name); });
                });
        });
        if (! path.empty())
        {
            menu.addItem (tr ("Rename folder..."), [safe, update, instruments, path] {
                if (safe != nullptr)
                    safe->askFolderName (tr ("Rename folder"), juce::String::fromUTF8 (path.c_str()), [update, instruments, path] (const std::string& name) {
                        update ([=] (plugincatalog::Folders& f) { plugincatalog::renameFolder (f, instruments, path, name); });
                    });
            });
            menu.addItem (tr ("Delete folder (its plugins go back to their usual folder)"), [safe, update, instruments, path] {
                if (safe == nullptr) return;
                std::vector<std::string> keys;
                if (instruments) for (const auto& e : quickpick::generatorEntries (safe->browser.engine)) keys.push_back (e.key);
                else for (const auto& e : quickpick::effectEntries (safe->browser.engine)) keys.push_back (e.key);
                update ([=] (plugincatalog::Folders& f) { plugincatalog::removeFolder (f, instruments, path, keys); });
            });
        }
        else
        {
            // FL's own arrangement: the folders of FL Studio's Plugin database (Effects / Generators), when FL is here
            const auto fl = findFlStudio();
            const auto base = fl.userData.getChildFile ("Presets").getChildFile ("Plugin database").getChildFile (instruments ? "Generators" : "Effects");
            menu.addItem (tr ("Folders from FL Studio's Plugin database"), base.isDirectory(), false, [safe, update, instruments, base] {
                if (safe == nullptr) return;
                std::vector<std::string> files;
                for (const auto& entry : juce::RangedDirectoryIterator (base, true, "*.fst", juce::File::findFiles))
                    files.push_back (entry.getFile().getRelativePathFrom (base).replaceCharacter ('\\', '/').toStdString());
                std::vector<std::pair<std::string, std::string>> names;
                auto& engine = safe->browser.engine;
                if (instruments) for (const auto& e : quickpick::generatorEntries (engine)) names.push_back ({ e.key, e.label.toStdString() });
                else for (const auto& e : quickpick::effectEntries (engine)) names.push_back ({ e.key, e.label.toStdString() });
                int placed = 0;
                update ([&] (plugincatalog::Folders& f) { placed = plugincatalog::importFlFolders (f, instruments, files, names); });
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Plugin database"),
                                                        tr ("{0} plugins placed in the folders of FL Studio's Plugin database.", { juce::String (placed) }));
            });
        }
    }

    // FL's "Delete file..." on a Plugin database entry (round 190bf): to the recycle bin after asking; the plugin leaves
    // the menus' folders and the picker (it stays installed), as when the file is deleted in the file manager
    void deleteDatabaseEntry (const juce::File& entry)
    {
        auto safe = juce::Component::SafePointer<FileTree> (this);
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, tr ("Delete file"),
                                            tr ("Move \"{0}\" to the recycle bin?", { entry.getFileName() }), tr ("Delete"), tr ("Cancel"), nullptr,
                                            juce::ModalCallbackFunction::create ([safe, entry] (int result) {
                                                if (result == 0 || safe == nullptr)
                                                    return;
                                                if (! entry.moveToTrash())
                                                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Delete file"),
                                                                                            tr ("The file could not be deleted."));
                                                quickpick::refreshPluginDatabase (safe->browser.engine, true);
                                                safe->browser.rebuildTree();
                                            }));
    }

public:
    void renamePlugin (const std::string& key, const juce::String& original)
    {
        auto safe = juce::Component::SafePointer<FileTree> (this);
        quickpick::rename (browser.engine, key, original, [safe] {
            if (safe != nullptr)
                safe->browser.rebuildTree();
        });
    }

    bool renameSelectedPlugin()
    {
        if (! juce::isPositiveAndBelow (selected, (int) rows.size()))
            return false;
        const auto& n = *rows[(std::size_t) selected];
        if (n.kind != Kind::plugin || n.pluginKey.empty())
            return false;
        renamePlugin (n.pluginKey, n.original);
        return true;
    }

    // --snapshot (round 190bf): the menu a right-click on that row shows (the Plugin database's), without showing it
    juce::PopupMenu menuForSnapshot (int row)
    {
        juce::PopupMenu out;
        if (! juce::isPositiveAndBelow (row, (int) rows.size()))
            return out;
        captured = &out;
        showMenu (*rows[(std::size_t) row]);
        captured = nullptr;
        return out;
    }
    int firstRowOfKind (bool plugin) const
    {
        for (int i = 0; i < (int) rows.size(); ++i)
            if (rows[(std::size_t) i]->kind == (plugin ? Kind::plugin : Kind::plugins) && (plugin || rows[(std::size_t) i]->folderKind >= 0))
                return i;
        return -1;
    }

private:
    juce::PopupMenu* captured = nullptr;
    void present (juce::PopupMenu& menu)
    {
        if (captured != nullptr)
            *captured = menu;
        else
            menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
    }

    void showMenu (Node& n)
    {
        auto& engine = browser.engine;
        juce::PopupMenu menu;
        auto safe = juce::Component::SafePointer<FileTree> (this);
        auto expandItems = [&menu, safe, &n] {
            Node* target = &n;
            while (target != nullptr && target->kind == Kind::item)
                target = target->parent;
            menu.addItem (tr ("Expand all"), [safe, target] {
                if (safe != nullptr && target != nullptr)
                    safe->expandAll (*target);
            });
            menu.addItem (tr ("Collapse all"), [safe] {
                if (safe != nullptr)
                    safe->collapseAll();
            });
        };

        // (issue #104: the items below act on the Plugin database folder as it is on disk now)
        if (n.kind == Kind::plugin || n.kind == Kind::plugins)
            quickpick::refreshPluginDatabase (engine, true);
        if (n.kind == Kind::plugin && factorycontent::isKey (n.pluginKey)) // round 190ke: LYNX's own content
        {
            menu.addSectionHeader (n.label);
            menu.addItem (tr ("Add"), [act = n.activate] {
                if (act)
                    act();
            });
            if (const auto item = itemOf (n); ! item.empty())
                browser.addCollectionItems (menu, item);
            present (menu);
            return;
        }
        if (n.kind == Kind::plugin)
        {
            menu.addSectionHeader (n.label);
            // round 190iz (REA round 143 B5: FL's first item on a plugin): a generator replaces the selected channel's
            // (FL's Enter), an effect goes into the selected insert
            menu.addItem (tr ("Send to selected channel or focused plugin"), [safe, generator = n.generator, effect = n.effect] {
                if (safe != nullptr)
                    safe->browser.sendPluginToSelected (generator, effect);
            });
            menu.addItem (tr ("Add"), [act = n.activate] {
                if (act)
                    act();
            });
            if (! n.pluginKey.empty()) // FL's renamed plugins (issue #11): "1_EQ" is then picked by typing 1 on an empty slot
            {
                menu.addSeparator();
                menu.addItem (tr ("Rename...") + "\tF2", [safe, key = n.pluginKey, original = n.original] {
                    if (safe != nullptr)
                        safe->renamePlugin (key, original);
                });
                menu.addItem (tr ("Original name"), n.label != n.original, false, [safe, key = n.pluginKey] {
                    if (safe != nullptr)
                    {
                        safe->browser.engine.getPluginHost().setPluginName (key, {});
                        safe->browser.rebuildTree();
                    }
                });
                // round 166f (issue #16): FL's Plugin database folders — the slot menus list each folder under a heading
                const bool instruments = (bool) n.generator;
                const auto& folders = engine.getPluginHost().databaseFolders();
                const auto it = folders.of.find (n.pluginKey);
                const auto now = it != folders.of.end() && folders.kind (instruments).count (it->second) ? it->second : std::string();
                juce::PopupMenu move;
                for (const auto& path : folders.kind (instruments))
                    move.addItem (juce::String::fromUTF8 (plugincatalog::groupHeading (path).c_str()), true, path == now, [safe, key = n.pluginKey, instruments, path] {
                        if (safe != nullptr) safe->setPluginFolder (key, instruments, path);
                    });
                if (! folders.kind (instruments).empty()) move.addSeparator();
                move.addItem (tr ("New folder..."), [safe, key = n.pluginKey, instruments] {
                    if (safe != nullptr)
                        safe->askFolderName (tr ("New folder"), {}, [safe, key, instruments] (const std::string& name) {
                            if (safe != nullptr) safe->setPluginFolder (key, instruments, name);
                        });
                });
                move.addItem (tr ("No folder"), ! now.empty(), now.empty(), [safe, key = n.pluginKey, instruments] {
                    if (safe != nullptr) safe->setPluginFolder (key, instruments, {});
                });
                menu.addSubMenu (tr ("Move to folder"), move);
            }
            if (const auto item = itemOf (n); ! item.empty()) // round 190ce: the collections take Plugin database entries too
                browser.addCollectionItems (menu, item);
            // FL's "System" group on an entry (round 190bf, fl190be q1-entry-menu.png): its file in the Plugin database
            // folder (the Installed one for a plugin in no folder). FL's "Windows shell menu" is left out: LYNX has no
            // shell context menu of its own.
            {
                const auto entry = n.pluginKey.empty() ? juce::File() : quickpick::pluginDatabaseEntry (n.pluginKey, (bool) n.generator);
                const auto ok = entry.existsAsFile();
                menu.addSectionHeader (tr ("System"));
                menu.addItem (tr ("Open parent folder"), ok, false, [entry] { entry.getParentDirectory().startAsProcess(); });
                menu.addItem (tr ("Locate file"), ok, false, [entry] { entry.revealToUser(); });
                menu.addItem (tr ("Delete file..."), ok, false, [safe, entry] {
                    if (safe != nullptr)
                        safe->deleteDatabaseEntry (entry);
                });
            }
            present (menu);
            return;
        }
        if (n.kind == Kind::collection)
        {
            auto safeBrowser = juce::Component::SafePointer<BrowserComponent> (&browser);
            ui::addItemHeader (menu, tr ("Collection"), n.label, n.colour);
            menu.addItem (tr ("Rename..."), [safeBrowser, c = n.collection] {
                if (safeBrowser != nullptr)
                    safeBrowser->renameCollection (c);
            });
            menu.addItem (tr ("Hide this collection"), n.collection != 0 || BrowserComponent::collectionStore().shown().size() > 1, false, [safeBrowser, c = n.collection] {
                if (safeBrowser != nullptr)
                    safeBrowser->setCollectionShown (c, false);
            });
            // the collections the STARRED tab lists (round 190cy: here, as Live's Collections › Edit, since the
            // Clip-launch workflow's row menu has only the labels)
            juce::PopupMenu show;
            for (int c = 0; c < collections::count; ++c)
                show.addItem (BrowserComponent::collectionName (c), true, BrowserComponent::collectionStore().isShown (c), [safeBrowser, c] {
                    if (safeBrowser != nullptr)
                        safeBrowser->setCollectionShown (c, ! BrowserComponent::collectionStore().isShown (c));
                });
            menu.addSubMenu (tr ("Show in the STARRED tab"), show);
            present (menu);
            return;
        }
        if (n.kind == Kind::project || n.kind == Kind::category || n.kind == Kind::plugins || n.kind == Kind::factory)
        {
            menu.addSectionHeader (n.label);
            expandItems();
            juce::File databaseDir; // (a Plugin database folder: its place on disk)
            if (n.kind == Kind::plugins && n.folderKind >= 0) // round 166f (issue #16): the user's folders
                addFolderItems (menu, n);
            if (n.kind == Kind::plugins)
            {
                // round 190ba (issue #104): the Plugin database is a folder the user can arrange in the file manager, as
                // FL's (its Browser opens the folder from this menu too). The database itself, Effects / Generators, one
                // of the user's folders; LYNX's own groups (LYNX SHARE, FL engines, Installed) open the Installed entries
                juce::File dir = quickpick::pluginDatabaseFolder();
                if (n.folderKind >= 0)
                    dir = quickpick::pluginDatabaseFolder (n.folderKind == 1, n.userFolder);
                else if (n.isGroup && n.parent != nullptr && n.parent->kind == Kind::plugins && n.parent->folderKind >= 0)
                    dir = juce::File (juce::String::fromUTF8 (plugindb::groupFolder (dir.getFullPathName().toStdString(), n.parent->folderKind == 1, n.group).c_str()));
                databaseDir = dir;
            }
            if ((n.kind == Kind::project || n.kind == Kind::plugins) && n.parent == nullptr && browser.shownTab() == allTab) // round 163
            {
                menu.addSeparator();
                browser.addHideItem (menu, n.kind == Kind::project ? browserview::projectRootId : browserview::pluginsRootId, n.label);
            }
            if (n.kind == Kind::plugins)
            {
                // FL's "System" group at the end of a folder's menu (round 190bf, fl190be q1-folder-menu.png): "Open"
                // (FL's "Windows shell menu" is left out: LYNX has no shell context menu of its own)
                menu.addSectionHeader (tr ("System"));
                menu.addItem (tr ("Open"), databaseDir.isDirectory(), false, [dir = databaseDir] { dir.startAsProcess(); });
            }
            present (menu);
            return;
        }

        if (n.kind != Kind::item)
        {
            if (n.folder)
                browser.showFolderMenu (n.file, [safe, node = &n] {
                    if (safe != nullptr)
                        safe->expandFolder (*node, 3);
                });
            else
                browser.showFileMenu (n.file);
            return;
        }

        const auto item = n.item;
        if (n.history)
            return;
        if (n.initial)
        {
            const auto& values = engine.getSong().initialValues;
            if (! juce::isPositiveAndBelow (item.index, (int) values.size()))
                return;
            menu.addSectionHeader (n.label + ": " + n.detail);
            menu.addItem (tr ("Delete initial value"), [&engine, target = values[(std::size_t) item.index].target] {
                engine.editSong ([target] (Song& s) { s.removeInitialValue (target); });
            });
            menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
            return;
        }

        switch (item.kind)
        {
            case projectcontents::Kind::pattern: // FL: the right-click makes it the current pattern, then its menu
                itemClicked (n);
                if (browser.onPatternMenu)
                    browser.onPatternMenu (item.index);
                return;
            case projectcontents::Kind::sample:
            {
                const auto file = n.file;
                const auto ok = ! n.missing;
                ui::addItemHeader (menu, tr ("Audio file"), n.label, n.colour);
                menu.addItem (tr ("Send to the selected channel"), ok && browser.onLoadIntoChannel != nullptr, false, [safe, file] {
                    if (safe != nullptr && safe->browser.onLoadIntoChannel)
                        safe->browser.onLoadIntoChannel (file);
                });
                menu.addItem (tr ("Open in a new channel"), ok, false, [safe, file] {
                    if (safe != nullptr)
                        safe->browser.openFile (file);
                });
                menu.addItem (tr ("Send to the Playlist as an audio clip"), ok && browser.onSendToPlaylist != nullptr, false, [safe, file] {
                    if (safe != nullptr && safe->browser.onSendToPlaylist)
                        safe->browser.onSendToPlaylist (file);
                });
                menu.addSeparator();
                menu.addItem (tr ("Edit in the audio editor..."), ok, false, [&engine, file] { openAudioEditor (engine, file); });
                if (item.index >= 0)
                    menu.addItem (tr ("Show its channel"), [safe, channel = item.index] {
                        if (safe != nullptr && channel < (int) safe->browser.engine.getSong().channels.size())
                            safe->browser.engine.setSelectedChannel (channel);
                    });
                menu.addSeparator();
                menu.addItem (tr ("Show in the file manager"), ok, false, [file] { file.revealToUser(); });
                workflowextras::addBrowserDeleteItem (menu, engine, false, item.index); // round 190iu (issue #191.6): a take
                break;
            }
            case projectcontents::Kind::generator:
                ui::addItemHeader (menu, tr ("Generator"), n.label, n.colour);
                menu.addItem (tr ("Open settings"), [safe, channel = item.index] {
                    if (safe != nullptr && safe->browser.onOpenChannel)
                        safe->browser.onOpenChannel (channel);
                });
                if (engine.getSong().channels[(std::size_t) item.index].mixerTrack > 0)
                    menu.addItem (tr ("Show its mixer insert"), [safe, track = engine.getSong().channels[(std::size_t) item.index].mixerTrack] {
                        if (safe != nullptr && safe->browser.onShowInsert)
                            safe->browser.onShowInsert (track);
                    });
                break;
            case projectcontents::Kind::effect:
                ui::addItemHeader (menu, tr ("Effect") + " - " + n.detail, n.label, n.colour);
                menu.addItem (tr ("View editor"), [safe, track = item.index, slot = item.slot] {
                    if (safe != nullptr && safe->browser.onOpenEffect)
                        safe->browser.onOpenEffect (track, slot);
                });
                menu.addItem (tr ("Show its mixer insert"), [safe, track = item.index] {
                    if (safe != nullptr && safe->browser.onShowInsert)
                        safe->browser.onShowInsert (track);
                });
                break;
            case projectcontents::Kind::automation:
                ui::addItemHeader (menu, tr ("Automation clip"), n.label, n.colour);
                menu.addItem (tr ("Open the automation editor"), [safe, index = item.index] {
                    if (safe != nullptr && safe->browser.onOpenAutomation)
                        safe->browser.onOpenAutomation (index);
                });
                workflowextras::addBrowserDeleteItem (menu, engine, true, item.index); // round 190iu (issue #191.6)
                break;
        }

        menu.addSeparator();
        expandItems();
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
    }

    void addRows (Node& n)
    {
        for (auto& c : n.children)
        {
            rows.push_back (c.get());
            if (c->folder && c->expanded)
                addRows (*c);
        }
    }

    void toggle (Node& n)
    {
        const auto keep = selectionKey();
        n.expanded = ! n.expanded;
        if (n.kind == Kind::plugins) // (remembered: the database is built again with the tree)
        {
            const auto key = (n.parent != nullptr && n.parent->kind == Kind::plugins ? n.parent->label + "/" : juce::String()) + n.label;
            if (n.expanded)
                browser.openPluginFolders.insert (key);
            else
                browser.openPluginFolders.erase (key);
        }
        if (n.kind == Kind::factory) // round 190ke (remembered: the folders are built again with the tree)
        {
            const auto key = juce::String::fromUTF8 (n.userFolder.c_str());
            if (n.expanded)
                browser.openFactoryFolders.insert (key);
            else
                browser.openFactoryFolders.erase (key);
        }
        if (n.kind == Kind::collection)
        {
            if (n.expanded)
                browser.closedCollections.erase (n.collection);
            else
                browser.closedCollections.insert (n.collection);
        }
        if (n.expanded)
            load (n);
        if (n.expanded && n.file.isDirectory()) // round 190lr (DF-96): an opened folder is a place in the history
            browser.noteLocation (n.file);
        rebuild();
        restoreSelection (keep);

        if (n.expanded) // show what opened
            for (int i = 0; i < (int) rows.size(); ++i)
                if (rows[(std::size_t) i] == &n)
                    scrollToRow (juce::jmin ((int) rows.size() - 1, i + juce::jmin ((int) n.children.size(), 6)));
    }

    void scrollToRow (int row)
    {
        if (row < 0)
            return;
        if (auto* view = findParentComponentOfClass<juce::Viewport>())
        {
            const auto top = row * rowHeight, bottom = top + rowHeight;
            const auto y = view->getViewPositionY(), h = view->getViewHeight();
            if (top < y)
                view->setViewPosition (0, top);
            else if (bottom > y + h)
                view->setViewPosition (0, bottom - h);
        }
    }

    void repaintRow (int row)
    {
        if (row >= 0)
            repaint (0, row * rowHeight, getWidth(), rowHeight);
    }

    juce::Image dragImage (const Node& n) const
    {
        const auto name = n.label.isNotEmpty() ? n.label : n.file.getFileNameWithoutExtension();
        const auto font = juce::Font (theme::font (theme::type::body));
        const auto width = juce::jlimit (80, 260, juce::GlyphArrangement::getStringWidthInt (font, name) + 36);
        const auto colour = n.colour.isTransparent() ? theme::colour::accent : readable (n.colour);
        juce::Image image (juce::Image::ARGB, width, 24, true);
        juce::Graphics g (image);
        g.setColour (theme::colour::bg4.withAlpha (0.92f));
        g.fillRoundedRectangle (0.5f, 0.5f, (float) width - 1.0f, 23.0f, 5.0f);
        g.setColour (colour);
        g.drawRoundedRectangle (0.5f, 0.5f, (float) width - 1.0f, 23.0f, 5.0f, 1.0f);
        icons::draw (g, n.icon != Icon::none ? n.icon : iconFor (n.file, false), { 6.0f, 4.0f, 16.0f, 16.0f }, colour);
        g.setColour (theme::colour::textPrimary);
        g.setFont (font);
        g.drawText (name, 28, 0, width - 32, 24, juce::Justification::centredLeft, true);
        return image;
    }

    BrowserComponent& browser;
    const bool flat;
    std::unique_ptr<Node> root;
    std::vector<std::unique_ptr<Node>> flatNodes;
    std::vector<Node*> rows;
    juce::Array<juce::File> bases;
    juce::String status, pendingKey;
    int selected = -1, hovered = -1, pressedRow = -1;
    int dropRow = -1; // the folder files dragged in from the system would be copied to
    bool dragStarted = false;
    bool overStar = false; // (round 190iz: the mouse is on the hovered row's star)
};

// --- the tabs: FL 2026's six icon tabs (round 190iz; before: ALL / PROJECT / STARRED as text) ----------------------
// FL (fl-playlist.png 0,104,205,140): six icons spread over the width, the selected one in an outlined box; design finding
// FM-8: 30x30 boxes, the selected one with a 1.5 px accent outline (radius 4), the others textSecondary, hover bg5. The
// names are in the hint bar and the tooltip, as FL's icon tabs.

class BrowserComponent::TabBar final : public juce::Component,
                                       public ui::HintSource,
                                       public juce::TooltipClient
{
public:
    explicit TabBar (BrowserComponent& b) : browser (b) { setRepaintsOnMouseActivity (true); }

    static constexpr int preferredHeight = 32;

    // the slot (left to right) of a tab, and its box
    juce::Rectangle<int> tabBounds (int t) const
    {
        const auto slot = browserplaces::slotOf ((browserplaces::Tab) t);
        const auto w = getWidth() / tabCount;
        const auto cell = juce::Rectangle<int> (slot * w, 0, slot == tabCount - 1 ? getWidth() - slot * w : w, getHeight());
        const auto side = juce::jmin (30, cell.getWidth() - 2, getHeight() - 2);
        return cell.withSizeKeepingCentre (side, side);
    }
    int tabAt (juce::Point<int> p) const
    {
        const auto slot = juce::jlimit (0, tabCount - 1, p.x * tabCount / juce::jmax (1, getWidth()));
        return (int) browserplaces::tabOrder[(std::size_t) slot];
    }

    static juce::String name (int t)
    {
        switch ((browserplaces::Tab) t)
        {
            case browserplaces::Tab::all: return tr ("ALL");
            case browserplaces::Tab::project: return tr ("PROJECT");
            case browserplaces::Tab::plugins: return tr ("PLUGINS");
            case browserplaces::Tab::presets: return tr ("PRESETS");
            case browserplaces::Tab::sounds: return tr ("SOUNDS");
            case browserplaces::Tab::starred: return tr ("STARRED");
        }
        return {};
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        const auto mouse = isMouseOver() ? getMouseXYRelative() : juce::Point<int> (-1, -1);
        const auto hoverTab = mouse.x >= 0 ? tabAt (mouse) : -1;
        for (int t = 0; t < tabCount; ++t)
        {
            const auto r = tabBounds (t).toFloat();
            const auto on = browser.tab == t;
            if (! on && t == hoverTab)
            {
                g.setColour (colour::bg5);
                g.fillRoundedRectangle (r, 4.0f);
            }
            if (on)
            {
                g.setColour (colour::bg4);
                g.fillRoundedRectangle (r, 4.0f);
                g.setColour (colour::accent);
                g.drawRoundedRectangle (r.reduced (0.75f), 4.0f, 1.5f);
            }
            const auto icon = r.withSizeKeepingCentre (18.0f, 18.0f);
            browsertabicons::draw (g, (browserplaces::Tab) t, icon.toNearestInt().toFloat(), on ? colour::textPrimary : colour::textSecondary);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override { browser.setTab (tabAt (e.getPosition())); }

    juce::String getTooltip() override { return isMouseOver() ? name (tabAt (getMouseXYRelative())) : juce::String(); }

    ui::Hint hintAt (juce::Point<int> p) override
    {
        ui::Hint h;
        switch ((browserplaces::Tab) tabAt (p))
        {
            case browserplaces::Tab::all: h.text = tr ("Browser: ALL - the current project and every sample folder"); break;
            case browserplaces::Tab::project:
                h.text = tr ("Browser: PROJECT - only what the current project uses (patterns, effects, generators, audio files...)");
                break;
            case browserplaces::Tab::plugins: h.text = tr ("Browser: PLUGINS - the Plugin database (generators and effects)"); break;
            case browserplaces::Tab::presets: h.text = tr ("Browser: PRESETS - the preset folders (synth, channel, plugin and mixer presets)"); break;
            case browserplaces::Tab::sounds: h.text = tr ("Browser: SOUNDS - the sample folders, audio files only"); break;
            case browserplaces::Tab::starred: h.text = tr ("Browser: STARRED - the files and folders you starred (the star at a row's end)"); break;
        }
        return h;
    }

private:
    BrowserComponent& browser;
};

// --- the preview strip: waveform of the selected sample ----------------------------------------------

class BrowserComponent::PreviewStrip final : public juce::Component,
                                             public ui::HintSource,
                                             private juce::Timer,
                                             private juce::ChangeListener
{
public:
    explicit PreviewStrip (BrowserComponent& b) : browser (b) { ui::WaveformCache::getInstance()->addChangeListener (this); }

    ~PreviewStrip() override
    {
        if (auto* cache = ui::WaveformCache::getInstanceWithoutCreating())
            cache->removeChangeListener (this);
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); } // the spectral waveform is ready

    // FL 2026's preview strip (audit round 165z-a): over the waveform, bottom left, a Play / Stop toggle and the preview
    // volume slider; they show while the mouse is over the strip or a sample plays
    void adoptControls()
    {
        addChildComponent (browser.stopButton);
        addChildComponent (browser.previewVolume);
        updateControls();
    }

    void updateControls()
    {
        const auto playing = isPlaying();
        const auto show = isAudio (file) && (playing || isMouseOver (true));
        if (playing != shownPlaying) // (only on a change: this runs on every timer tick)
        {
            shownPlaying = playing;
            browser.stopButton.setIcon (playing ? Icon::stop : Icon::play);
            browser.stopButton.setPrimary (playing);
            const auto text = playing ? tr ("Stop the sample preview") : tr ("Play the sample preview");
            browser.stopButton.setTooltip (text);
            ui::setHint (browser.stopButton, text, true);
        }
        if (browser.stopButton.isVisible() != show)
        {
            browser.stopButton.setVisible (show);
            browser.previewVolume.setVisible (show);
        }
        if (show && ! isTimerRunning())
            startTimerHz (30); // (hides them again when the mouse leaves)
    }

    void resized() override
    {
        auto row = getLocalBounds().reduced (8, 6).removeFromBottom (14);
        browser.stopButton.setBounds (row.removeFromLeft (20));
        row.removeFromLeft (6);
        browser.previewVolume.setBounds (row.removeFromLeft (juce::jmin (90, juce::jmax (40, row.getWidth() / 2))));
    }

    void mouseEnter (const juce::MouseEvent&) override { updateControls(); }
    void mouseExit (const juce::MouseEvent&) override { updateControls(); }

    void togglePlay()
    {
        if (isPlaying())
            browser.stopPreview();
        else if (isAudio (file))
        {
            browser.askPreview (0.0, true); // (FL's Preview Player: Play plays the whole sample)
            browser.fileSelected (file, true);
        }
    }

    // round 190iz: what the preview plays (FL's 5 s from the start or from a Shift+Click, or all of it)
    void show (const juce::File& f, bool playing, double fromFraction, bool full)
    {
        startFraction = juce::jlimit (0.0, 1.0, fromFraction);
        playFull = full;
        show (f, playing);
    }

    void show (const juce::File& f, bool playing)
    {
        if (f != file)
        {
            file = f;
            peaks.clear();
            seconds = 0.0;
            info.clear();

            if (isAudio (f))
                if (const auto sample = browser.engine.getSampleLibrary().load (f.getFullPathName()); sample != nullptr && sample->frames() > 0)
                {
                    // 256 peaks over the whole file
                    const auto frames = sample->frames();
                    constexpr std::size_t count = 256;
                    peaks.assign (count, 0.0f);
                    const auto* l = sample->leftData();
                    const auto* r = sample->rightData();
                    const auto stride = std::max<std::size_t> (1, frames / (count * 64)); // look at up to 64 points per peak

                    for (std::size_t i = 0; i < count; ++i)
                    {
                        const auto from = frames * i / count, to = juce::jmax (from + 1, frames * (i + 1) / count);
                        float m = 0.0f;
                        for (auto k = from; k < to && k < frames; k += stride)
                            m = juce::jmax (m, std::abs (l[k]), std::abs (r[k]));
                        peaks[i] = m;
                    }

                    const auto top = *std::max_element (peaks.begin(), peaks.end());
                    if (top > 1.0e-6f) // normalised: quiet files are still readable
                        for (auto& p : peaks)
                            p /= top;

                    seconds = (double) frames / juce::jmax (1.0, sample->sampleRate);
                    info = juce::String (seconds, seconds < 10.0 ? 2 : 1) + " s  " + juce::String (juce::roundToInt (sample->sampleRate / 100.0) / 10.0, 1)
                           + " kHz  " + (sample->isStereo() ? tr ("stereo") : tr ("mono"));
                }
        }

        if (playing && seconds > 0.0)
        {
            startedAt = juce::Time::getMillisecondCounterHiRes();
            startTimerHz (30);
        }
        updateControls();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme::colour;
        g.fillAll (bg1);
        g.setColour (border);
        g.fillRect (0, 0, getWidth(), 1);
        auto area = getLocalBounds().reduced (6, 4).withTrimmedTop (1);

        if (file == juce::File() || ! isAudio (file))
        {
            g.setColour (textDisabled);
            g.setFont (theme::font (theme::type::small));
            g.drawFittedText (tr ("Click a sample to hear it; its waveform shows here"), area, juce::Justification::centred, 2);
            return;
        }

        auto top = area.removeFromTop (14);
        g.setFont (theme::font (theme::type::caption, true));
        g.setColour (textPrimary);
        g.drawText (file.getFileNameWithoutExtension(), top, juce::Justification::centredLeft, true);
        g.setFont (theme::font (theme::type::caption));
        g.setColour (textSecondary);
        g.drawText (info + tempoText(), top, juce::Justification::centredRight, true);
        area.removeFromTop (2);

        g.setColour (bg0);
        g.fillRoundedRectangle (area.toFloat(), theme::radius::sm);

        if (peaks.empty())
            return;

        const auto wave = area.toFloat().reduced (2.0f, 2.0f);
        const auto mid = wave.getCentreY();
        const auto progress = position();
        const auto playedX = wave.getX() + wave.getWidth() * (float) progress;
        const auto fromX = wave.getX() + wave.getWidth() * (float) (progress > 0.0 ? startFraction : 0.0); // (round 190iz)

        // the detailed, frequency-coloured waveform (ui/WaveformView) once analysed; the quick peaks until then
        const auto path = file.getFullPathName();
        if (const auto summary = ui::WaveformCache::getInstance()->find (path, [this, path] { return browser.engine.getSampleLibrary().load (path); }))
        {
            ui::paintWaveform (g, wave, *summary, 0.0, (double) summary->frames / juce::jmax (1.0f, wave.getWidth()),
                               ui::currentWaveStyle (textSecondary, ui::normalisingGain (*summary)));
            if (progress > 0.0)
            {
                g.setColour (accent.withAlpha (0.22f)); // the part already heard
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (fromX, wave.getY(), juce::jmax (fromX, playedX), wave.getBottom()));
                g.setColour (textPrimary);
                g.fillRect (playedX, wave.getY(), 1.0f, wave.getHeight());
            }
            return;
        }

        for (int x = 0; x < (int) wave.getWidth(); ++x)
        {
            const auto p = peaks[(std::size_t) (x * (int) peaks.size() / juce::jmax (1, (int) wave.getWidth()))];
            const auto h = juce::jmax (0.5f, p * wave.getHeight() * 0.5f);
            const auto px = wave.getX() + (float) x;
            g.setColour (progress > 0.0 && px <= playedX && px >= fromX ? accent : textSecondary.withAlpha (0.75f));
            g.fillRect (px, mid - h, 1.0f, h * 2.0f);
        }

        if (progress > 0.0)
        {
            g.setColour (textPrimary);
            g.fillRect (playedX, wave.getY(), 1.0f, wave.getHeight());
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! isAudio (file) || ! e.mods.isLeftButtonDown())
            return;
        // round 190iz: FL's Shift+Click (and a click in the Clip-launch layout, Live's scrub area) plays from the mouse
        // position; Alt+Click plays to the end; a plain click hears it again from the start
        const auto wave = getLocalBounds().reduced (6, 4).withTrimmedTop (17).toFloat().reduced (2.0f, 2.0f);
        const auto at = wave.getWidth() > 1.0f ? juce::jlimit (0.0, 1.0, (double) ((float) e.x - wave.getX()) / (double) wave.getWidth()) : 0.0;
        browser.askPreview (e.mods.isShiftDown() || browser.isLiveLayout() ? at : 0.0, e.mods.isAltDown());
        browser.fileSelected (file, true);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging && isAudio (file) && e.getDistanceFromDragStart() > 5)
            if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
            {
                dragging = true;
                container->startDragging (juce::String (dragDescription) + "|" + file.getFullPathName(), this,
                                          juce::ScaledImage (browser.tree->dragImageForFile (file)), true); // (round 190iz: the drag ghost)
            }
    }

    void mouseUp (const juce::MouseEvent&) override { dragging = false; }

    ui::Hint hintAt (juce::Point<int>) override
    {
        ui::Hint h;
        h.text = isAudio (file) ? (browser.isLiveLayout() ? tr ("Sample preview: [LMB] play from there  [Alt+LMB] to the end  drag it like a file in the list")
                                                          : tr ("Sample preview: [LMB] hear it again  [Shift+LMB] from there  [Alt+LMB] to the end  drag it like a file in the list"))
                                : tr ("Sample preview");
        h.value = info;
        return h;
    }

    bool isPlaying() const { return position() > 0.0 && position() < 1.0; }

    // "Preview in time" (round 190aq): the tempo found in the file and the project's, after the file's details
    juce::String tempoText() const
    {
        const auto& timing = browser.engine.getPreviewTiming();
        if (! browser.engine.isPreviewInTime() || timing.path != file.getFullPathName() || ! (timing.fileBpm > 0.0))
            return {};
        auto bpm = [] (double v) { return juce::String (v, std::abs (v - std::round (v)) < 0.05 ? 0 : 1); };
        // "120 BPM" at the project's tempo already, else "120 → 140 BPM" (what it is stretched to)
        return "  " + tr ("{0} BPM", { timing.ratio == 1.0 ? bpm (timing.fileBpm)
                                                             : bpm (timing.fileBpm) + juce::String::fromUTF8 (" \xe2\x86\x92 ") + bpm (timing.fileBpm / timing.ratio) });
    }

    void stop()
    {
        startedAt = 0.0;
        if (! isMouseOver (true))
            stopTimer();
        updateControls();
        repaint();
    }

private:
    double position() const
    {
        if (startedAt <= 0.0 || seconds <= 0.0)
            return 0.0;
        // "Preview in time" (round 190aq): the stretched preview starts later (its stretch, then its beat) and lasts
        // another time, so the engine says where it is
        if (const auto& timing = browser.engine.getPreviewTiming(); timing.path == file.getFullPathName() && (timing.pending || timing.inTime))
        {
            const auto p = browser.engine.getPreviewInTimeProgress();
            return p < 0.0 ? 1.0 : juce::jlimit (1.0e-6, 1.0 - 1.0e-6, p);
        }
        // round 190iz: from where it started, for FL's 5 seconds unless it plays to the end
        const auto elapsed = (juce::Time::getMillisecondCounterHiRes() - startedAt) / 1000.0;
        const auto limit = playFull || browser.engine.isFullSamplePreview() ? seconds : juce::jmin (seconds, (double) browserpreview::defaultSeconds + startFraction * seconds);
        if (startFraction * seconds + elapsed >= limit)
            return 1.0;
        return juce::jlimit (0.0, 1.0, startFraction + elapsed / seconds);
    }

    void timerCallback() override
    {
        if (startedAt > 0.0 && position() >= 1.0)
        {
            stop();
            browser.tree->repaint(); // the "playing" colour goes
            browser.results->repaint();
        }
        updateControls();
        if (startedAt <= 0.0 && ! browser.stopButton.isVisible())
            stopTimer();
        repaint();
    }

    BrowserComponent& browser;
    juce::File file;
    std::vector<float> peaks;
    double seconds = 0.0, startedAt = 0.0;
    double startFraction = 0.0; // round 190iz: where the preview started (0..1)
    bool playFull = false;      // ... and whether it plays to the end
    juce::String info;
    bool dragging = false;
    std::optional<bool> shownPlaying; // the Play / Stop state the button shows
};

// --- the browser -------------------------------------------------------------------------------------

bool BrowserComponent::isPreviewPlaying() const
{
    return strip != nullptr && strip->isPlaying();
}

// FL's Browser details box (REA round 134/139): what is selected — a column of labels (Date, Size, Version for a
// project FL saved, Path) and their values, the path wrapping over the last lines. Hidden while nothing is selected.
class BrowserComponent::DetailsBox final : public juce::Component,
                                           public ui::HintSource
{
public:
    static constexpr int preferredHeight = 70;

    void show (const juce::File& f)
    {
        file = f;
        rows.clear();
        if (file != juce::File())
        {
            rows.push_back ({ tr ("Date"), file.getLastModificationTime().formatted ("%Y-%m-%d %H:%M:%S") });
            if (file.existsAsFile())
            {
                rows.push_back ({ tr ("Size"), juce::File::descriptionOfSizeInBytes (file.getSize()) });
                if (file.hasFileExtension (".flp"))
                {
                    juce::MemoryBlock head;
                    if (juce::FileInputStream in (file); in.openedOk())
                        in.readIntoMemoryBlock (head, 4096);
                    const auto version = flp::readSavedVersion ({ (const std::uint8_t*) head.getData(), head.getSize() });
                    if (! version.empty())
                        rows.push_back ({ tr ("Version"), juce::String (version) });
                }
            }
            rows.push_back ({ tr ("Path"), file.getFullPathName() });
        }
        repaint();
    }

    bool hasFile() const noexcept { return file != juce::File(); }

    ui::Hint hintAt (juce::Point<int>) override
    {
        ui::Hint h;
        if (file != juce::File())
            h.text = tr ("Selected: {0}", { file.getFullPathName() });
        return h;
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme::colour;
        g.fillAll (bg1);
        g.setColour (border);
        g.fillRect (0, 0, getWidth(), 1);
        auto area = getLocalBounds().reduced (6, 3).withTrimmedTop (1);
        g.setFont (theme::font (theme::type::caption));
        constexpr int line = 13, labelWidth = 48;
        for (std::size_t i = 0; i < rows.size() && area.getHeight() >= line; ++i)
        {
            const auto last = i + 1 == rows.size();
            // the path (last row) takes the lines left and wraps, as FL's box does
            auto r = last ? area : area.removeFromTop (line);
            g.setColour (textDisabled);
            g.drawText (rows[i].first, r.removeFromLeft (labelWidth).withHeight (line), juce::Justification::centredLeft, false);
            g.setColour (textSecondary);
            if (last)
                g.drawFittedText (rows[i].second, r, juce::Justification::topLeft, juce::jmax (1, r.getHeight() / line), 1.0f);
            else
                g.drawText (rows[i].second, r, juce::Justification::centredLeft, true);
        }
    }

private:
    juce::File file;
    std::vector<std::pair<juce::String, juce::String>> rows;
};

// --- Hot-swap (round 190ce): the bar -------------------------------------------------------------------
// The bar over the tabs while the Browser is linked to a device: what it is linked to, and an X that ends the link
// (the reference shows a coloured banner at the top of its browser with a close button).
class BrowserComponent::HotSwapBar final : public juce::Component, public ui::HintSource
{
public:
    explicit HotSwapBar (BrowserComponent& b) : browser (b) { setRepaintsOnMouseActivity (true); }

    static constexpr int height = 22;

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        g.setColour (colour::accent);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), radius::md);
        auto r = getLocalBounds().reduced (6, 0);
        const auto x = closeBounds();
        r.setRight (x.getX() - 4);
        g.setColour (juce::Colours::white);
        g.setFont (font (type::caption, true));
        g.drawText (tr ("Swapping {0}", { browser.hotSwapTargetName() }), r, juce::Justification::centredLeft, true); // (the reference: "Swapping Instrument")
        const auto over = isMouseOver() && x.contains (getMouseXYRelative());
        if (over)
        {
            g.setColour (juce::Colours::white.withAlpha (0.22f));
            g.fillRoundedRectangle (x.toFloat(), 3.0f);
        }
        g.setColour (juce::Colours::white);
        const auto c = x.toFloat().reduced (5.0f);
        g.drawLine (c.getX(), c.getY(), c.getRight(), c.getBottom(), 1.6f);
        g.drawLine (c.getX(), c.getBottom(), c.getRight(), c.getY(), 1.6f);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (closeBounds().contains (e.getPosition()))
            browser.endHotSwap();
    }

    ui::Hint hintAt (juce::Point<int> p) override
    {
        if (closeBounds().contains (p))
            return { tr ("End hot-swap (Esc)"), {}, false, false };
        return { tr ("Hot-swap: Enter or a double-click loads the selected plugin, preset or sample in place of this device"), browser.hotSwapTargetName(), false, false };
    }

private:
    juce::Rectangle<int> closeBounds() const { return getLocalBounds().removeFromRight (height).reduced (2); }
    BrowserComponent& browser;
};

BrowserComponent::BrowserComponent (AudioEngine& e, Role r)
    : engine (e), role (r)
{
    tabs = std::make_unique<TabBar> (*this);
    addAndMakeVisible (*tabs);
    // round 190iz: FL's TAGS button and tags panel, Live's sidebar (the Clip-launch layout shows it)
    tagsButton = std::make_unique<TagsButton> (*this);
    addAndMakeVisible (*tagsButton);
    tagsPanel = std::make_unique<TagsPanel> (*this);
    addChildComponent (*tagsPanel);
    // round gaps190kf: the Loop Browser (the Sample browser window's: LYNX's own window, so FL's Browser panel keeps FL's row)
    loopsButton = std::make_unique<LoopsButton> (*this);
    addChildComponent (*loopsButton);
    loopsButton->setVisible (role == Role::samples);
    loopPanel = std::make_unique<LoopPanel> (*this);
    addChildComponent (*loopPanel);
    sidebar = std::make_unique<LiveSidebar> (*this);
    sidebarView = std::make_unique<juce::Viewport>();
    sidebarView->setViewedComponent (sidebar.get(), false);
    sidebarView->setScrollBarsShown (true, false);
    sidebarView->setScrollBarThickness (8);
    sidebarView->setWantsKeyboardFocus (false);
    addChildComponent (*sidebarView);
    loadTags();
    loadPrefs();
    swapBar = std::make_unique<HotSwapBar> (*this);
    addChildComponent (*swapBar);
    liveBrowsers().push_back (this);

    // the search index (round 162): a background pass over every folder a few seconds after start (FL's automatic
    // indexing: only the folders that changed since the last session are listed again)
    searcher = SearchShared::get();
    searcher->statusChanged.addChangeListener (this);
    searcher->indexChanged.addChangeListener (this);
    loopLengthsRead().addChangeListener (this); // (round 190mf: the Simple loop rows' lengths)
    juce::Timer::callAfterDelay (4000, [safe = juce::Component::SafePointer<BrowserComponent> (this)] {
        if (safe != nullptr && safe->role == Role::main && ! safe->allRoots.isEmpty())
            safe->searcher->refreshInBackground (toPaths (safe->withMerged (safe->allRoots))); // (round 190hd: the indexer process)
    });
    // "Find similar samples" (round 190as): once it was used (its index file exists), the folders are checked for new
    // samples in the background a while after start; before that nothing is analysed
    similar = SimilarShared::get (engine);
    similar->statusChanged.addChangeListener (this);
    juce::Timer::callAfterDelay (20000, [safe = juce::Component::SafePointer<BrowserComponent> (this)] {
        if (safe != nullptr && safe->role == Role::main && ! safe->allRoots.isEmpty() && SimilarShared::file().existsAsFile()
            && ! std::exchange (safe->similar->passAsked, true))
            safe->similar->service->refresh (toPaths (safe->withMerged (safe->allRoots)));
    });
    focusWatcher = std::make_unique<FocusWatcher> (*this);

    searchBox.setTextToShowWhenEmpty (tr ("Search (Ctrl+F): kick -loop, snare OR clap, hat*.wav"), theme::colour::textDisabled);
    searchBox.addListener (this);
    searchBox.setEscapeAndReturnKeysConsumed (true);
    ui::setHint (searchBox, tr ("Search the selected folder (or every folder) and below; in PROJECT, the project's items: words must all match; "
                                "OR, -word (not), \"exact phrase\", * and ? wildcards  [Enter] to the results  [Esc] clear"));
    addAndMakeVisible (searchBox);

    // FL's Browser has a way to silence the preview too (issue #9): a Play / Stop toggle on the preview strip, beside
    // the preview volume, as in FL 2026 (audit round 165z-a); the strip adopts both below
    stopButton.setFlat (true);
    stopButton.setWantsKeyboardFocus (false);
    stopButton.setMouseClickGrabsKeyboardFocus (false);
    stopButton.onClick = [this] {
        if (strip != nullptr)
            strip->togglePlay();
    };

    // what this Browser shows (round 163): content filter, folders shown, the Sample browser window
    viewButton.setFlat (true);
    viewButton.setWantsKeyboardFocus (false);
    viewButton.setMouseClickGrabsKeyboardFocus (false);
    viewButton.setClickingTogglesState (false);
    viewButton.onClick = [this] { showViewMenu(); };
    addAndMakeVisible (viewButton);

    // the sample preview volume (FL's Browser has one too): a slider on the preview strip, as in FL 2026
    previewVolume.setSliderStyle (juce::Slider::LinearHorizontal);
    previewVolume.setRange (0.0, controls::maxUtilityVolume, 0.01);
    previewVolume.setDoubleClickReturnValue (true, 1.0);
    previewVolume.textFromValueFunction = [] (double v) { return ui::utilityVolumeText (v); };
    previewVolume.valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("0123456789.").getDoubleValue() / 100.0; };
    previewVolume.setValue (engine.getPreviewVolume(), juce::dontSendNotification);
    previewVolume.setMouseClickGrabsKeyboardFocus (false);
    previewVolume.setWantsKeyboardFocus (false);
    previewVolume.onValueChange = [this] {
        engine.setPreviewVolume (previewVolume.getValue());
        refreshPreviewVolume();
        if (onPreviewVolumeChanged != nullptr)
            onPreviewVolumeChanged();
    };
    refreshPreviewVolume();

    tree = std::make_unique<FileTree> (*this, false);
    results = std::make_unique<FileTree> (*this, true);

    for (auto [view, list] : { std::pair { &treeView, tree.get() }, std::pair { &resultsView, results.get() } })
    {
        *view = std::make_unique<juce::Viewport>();
        (*view)->setViewedComponent (list, false);
        (*view)->setScrollBarsShown (true, false);
        (*view)->setScrollBarThickness (10);
        (*view)->setWantsKeyboardFocus (false);
    }

    addAndMakeVisible (*treeView);
    addChildComponent (*resultsView);

    strip = std::make_unique<PreviewStrip> (*this);
    addAndMakeVisible (*strip);
    strip->adoptControls();
    details = std::make_unique<DetailsBox>();
    addChildComponent (*details);

    browserStartupMark ("  browser: search index, widgets");
    if (tagsPanelShown)
        tagsPanel->setVisible (true);
    rebuildRoots();
    browserStartupMark ("  browser: roots and tree");
    engine.addChangeListener (this);
    constructing = false;
}

void BrowserComponent::browserStartupMark (const char* step) const
{
    if (constructing && role == Role::main && startupMark != nullptr)
        startupMark (step);
}

juce::File BrowserComponent::userFoldersFile()
{
    return PluginHost::settingsFolder().getChildFile ("browser-folders.txt");
}

juce::File BrowserComponent::starredFile()
{
    return PluginHost::settingsFolder().getChildFile ("browser-starred.txt");
}

void BrowserComponent::setStarred (const juce::File& file, bool on)
{
    setInCollection (file.getFullPathName().toStdString(), 0, on);
}

// --- Collections (round 190ce) ------------------------------------------------------------------------

juce::File BrowserComponent::collectionsFile()
{
    return PluginHost::settingsFolder().getChildFile ("browser-collections.txt");
}

collections::Store& BrowserComponent::collectionStore()
{
    static collections::Store store;
    return store;
}

std::vector<BrowserComponent*>& BrowserComponent::liveBrowsers()
{
    static std::vector<BrowserComponent*> all;
    return all;
}

void BrowserComponent::loadCollections()
{
    if (const auto file = collectionsFile(); file.existsAsFile())
    {
        collectionStore() = collections::Store::fromText (file.loadFileAsString().toStdString());
        return;
    }
    // before round 190ce: one starred path per line
    std::vector<std::string> paths;
    for (const auto& line : juce::StringArray::fromLines (starredFile().loadFileAsString()))
        if (line.trim().isNotEmpty() && juce::File::isAbsolutePath (line.trim()))
            paths.push_back (line.trim().toStdString());
    collectionStore() = collections::Store::fromStarred (paths);
}

void BrowserComponent::saveCollections()
{
    const auto& store = collectionStore();
    collectionsFile().getParentDirectory().createDirectory();
    collectionsFile().replaceWithText (juce::String::fromUTF8 (store.toText().c_str()));
    juce::String starredText; // (an older LYNX SHARE reads this one)
    for (const auto& path : store.starredPaths())
        starredText << juce::String::fromUTF8 (path.c_str()) << "\n";
    starredFile().replaceWithText (starredText);
}

juce::String BrowserComponent::collectionName (int collection)
{
    const auto& store = collectionStore();
    if (store.hasOwnName (collection))
        return juce::String::fromUTF8 (store.name (collection).c_str());
    switch (collection)
    {
        case 0: return tr ("Starred");
        case 1: return tr ("Orange");
        case 2: return tr ("Yellow");
        case 3: return tr ("Green");
        case 4: return tr ("Blue");
        case 5: return tr ("Purple");
        case 6: return tr ("Gray");
        default: return {};
    }
}

juce::Colour BrowserComponent::collectionColour (int collection)
{
    return juce::Colour (collections::colourArgb (collection));
}

void BrowserComponent::collectionsChanged()
{
    for (auto* b : liveBrowsers())
    {
        if (b->liveLayout) // round 190iz: Live's sidebar lists the shown collections
            b->sidebar->refresh();
        if (b->liveLayout ? b->liveSelection.category == browserplaces::Category::collection : b->tab == starredTab)
        {
            const auto row = b->tree->getSelectedRow();
            const auto scroll = b->treeView->getViewPosition();
            b->rebuildTree();
            if (row >= 0 && b->tree->numRows() > 0)
                b->tree->select (juce::jmin (row, b->tree->numRows() - 1), false);
            b->treeView->setViewPosition (scroll);
        }
        b->tree->repaint();
        b->results->repaint();
        if (b->loopsShown && b->loopFilterState.favouritesOnly) // (round gaps190kf: the Favorites button's list)
            b->applyLoopFilter();
    }
}

void BrowserComponent::setInCollection (const std::string& item, int collection, bool on)
{
    if (! collectionStore().label (item, collection, on)) // (a hidden collection shows itself: round 190cy)
        return;
    saveCollections();
    collectionsChanged();
}

void BrowserComponent::toggleCollectionOnSelection (int collection)
{
    auto& list = activeList();
    const auto item = list.selectedItem();
    if (item.empty())
    {
        ui::postHint ({ tr ("Select a file, a folder or a plugin in the Browser first"), {} }, 2500);
        return;
    }
    const auto label = list.selectedLabel();
    const auto on = ! collectionStore().has (item, collection);
    setInCollection (item, collection, on);
    ui::postHint ({ on ? tr ("Added to the collection {0}", { collectionName (collection) }) : tr ("Removed from the collection {0}", { collectionName (collection) }), label }, 2500);
}

void BrowserComponent::clearCollectionsOnSelection()
{
    auto& list = activeList();
    const auto item = list.selectedItem();
    if (item.empty())
        return;
    const auto label = list.selectedLabel();
    if (! collectionStore().clear (item))
        return;
    saveCollections();
    collectionsChanged();
    ui::postHint ({ tr ("Removed from every collection"), label }, 2500);
}

void BrowserComponent::setCollectionShown (int collection, bool shown)
{
    auto& store = collectionStore();
    store.setShown (collection, shown);
    if (store.shown().empty()) // (one stays: Starred)
        store.setShown (0, true);
    saveCollections();
    collectionsChanged();
}

void BrowserComponent::renameCollection (int collection)
{
    auto* window = new juce::AlertWindow (tr ("Rename collection"), tr ("Name of the collection (empty: its default name):"), juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", juce::String::fromUTF8 (collectionStore().labels[(std::size_t) juce::jlimit (0, collections::count - 1, collection)].name.c_str()));
    window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<BrowserComponent> safe (this);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([window, safe, collection] (int result) {
        if (result != 1 || safe == nullptr)
            return;
        collectionStore().rename (collection, window->getTextEditorContents ("name").toStdString());
        saveCollections();
        safe->collectionsChanged();
    }), true);
}

// The collections in a row's menu. Clip-launch workflow (round 190cy, Live 12.4.16 live check 190cw shot 05): a flat
// list in the menu itself, "Clear all colours" (0) then the seven with their keys (1-7), ticked when the item is in
// one; renaming and showing / hiding are on a collection's heading. Pattern workflow: as round 190ce built it, a
// "Collections" sub-menu with the seven, "Remove from all", which collections the STARRED tab lists and their names.
void BrowserComponent::addCollectionItems (juce::PopupMenu& menu, const std::string& item)
{
    if (item.empty())
        return;
    juce::Component::SafePointer<BrowserComponent> safe (this);
    const auto& store = collectionStore();
    if (liveItemMenus)
    {
        menu.addSeparator();
        menu.addItem (tr ("Clear all colours") + "\t0", store.mask (item) != 0, false, [safe, item] {
            if (safe == nullptr || ! collectionStore().clear (item))
                return;
            saveCollections();
            safe->collectionsChanged();
        });
        for (int c = 0; c < collections::count; ++c)
        {
            juce::PopupMenu::Item entry (collectionName (c) + "\t" + juce::String (c + 1));
            entry.setTicked (store.has (item, c));
            entry.setColour (collectionColour (c).brighter (0.25f));
            entry.setAction ([safe, item, c] {
                if (safe != nullptr)
                    safe->setInCollection (item, c, ! collectionStore().has (item, c));
            });
            menu.addItem (entry);
        }
        menu.addSeparator();
        addTagItems (menu, item); // round 190iz
        return;
    }
    juce::PopupMenu sub;
    for (int c = 0; c < collections::count; ++c)
    {
        juce::PopupMenu::Item entry (collectionName (c) + "\t" + juce::String (c + 1));
        entry.setTicked (store.has (item, c));
        entry.setColour (collectionColour (c).brighter (0.25f));
        entry.setAction ([safe, item, c] {
            if (safe != nullptr)
                safe->setInCollection (item, c, ! collectionStore().has (item, c));
        });
        sub.addItem (entry);
    }
    sub.addSeparator();
    sub.addItem (tr ("Remove from all") + "\t0", store.mask (item) != 0, false, [safe, item] {
        if (safe == nullptr || ! collectionStore().clear (item))
            return;
        saveCollections();
        safe->collectionsChanged();
    });
    sub.addSeparator();
    juce::PopupMenu show, rename;
    for (int c = 0; c < collections::count; ++c)
    {
        show.addItem (collectionName (c), true, store.isShown (c), [safe, c] {
            if (safe != nullptr)
                safe->setCollectionShown (c, ! collectionStore().isShown (c));
        });
        rename.addItem (collectionName (c), [safe, c] {
            if (safe != nullptr)
                safe->renameCollection (c);
        });
    }
    sub.addSubMenu (tr ("Show in the STARRED tab"), show);
    sub.addSubMenu (tr ("Rename"), rename);
    menu.addSubMenu (tr ("Collections"), sub);
    addTagItems (menu, item); // round 190iz: FL's Tags section (Tags ▸, Add tag..., Edit tags...)
}

juce::PopupMenu BrowserComponent::collectionsMenuForSnapshot (const std::string& item)
{
    juce::PopupMenu menu;
    addCollectionItems (menu, item);
    // (the shot shows the sub-menu's own items: a picture of a menu does not open its sub-menus)
    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        if (it.getItem().subMenu != nullptr)
            return *it.getItem().subMenu;
    return menu;
}

void BrowserComponent::selectRowForSnapshot (const juce::String& label)
{
    auto& list = activeList();
    for (int i = 0; i < list.numRows(); ++i)
    {
        list.select (i, false);
        if (list.selectedLabel() == label)
            return;
    }
}

// --- Hot-swap (round 190ce) ---------------------------------------------------------------------------

juce::String BrowserComponent::hotSwapTargetName() const
{
    const auto& song = engine.getSong();
    const auto& t = swapSession.target;
    if (t.kind == hotswap::Kind::effect)
    {
        if (const auto at = hotswap::findEffect (song, t.instance); at.found())
            return quickpick::slotName (engine, song.mixer[(std::size_t) at.track].effects[(std::size_t) at.slot]);
    }
    else if (t.kind == hotswap::Kind::channel)
    {
        if (const auto c = hotswap::findChannel (song, t.instance); c >= 0)
            return juce::String::fromUTF8 (song.channels[(std::size_t) c].name.c_str());
    }
    return {};
}

void BrowserComponent::beginHotSwap (const hotswap::Target& target)
{
    const auto& song = engine.getSong();
    if (! hotswap::exists (song, target))
        return;
    swapSession.begin (target, tab, searchBox.getText().toStdString());
    if (searchBox.getText().isNotEmpty())
    {
        searchBox.setText ({}, false);
        runSearch();
    }
    // the Browser goes to what can take the device's place: a sample channel's own sample in its folder, else the
    // Plugin database's effects or generators
    bool shown = false;
    if (target.kind == hotswap::Kind::channel)
        if (const auto c = hotswap::findChannel (song, target.instance); c >= 0 && hotswap::playsSample (song.channels[(std::size_t) c]))
        {
            const juce::File sample (juce::String::fromUTF8 (song.channels[(std::size_t) c].samplePath.c_str()));
            setTab (allTab);
            if (sample.existsAsFile())
                if (const auto row = tree->revealFile (sample); row >= 0)
                {
                    tree->select (row, false);
                    shown = true;
                }
            shown = shown || sample == juce::File() || ! sample.existsAsFile(); // (no sample yet: the folders as they are)
        }
    // an effect with a preset: that preset, as Live starts hot-swap on the device's current preset (round 190cy)
    if (! shown && target.kind == hotswap::Kind::effect)
        shown = revealCurrentPreset (target);
    if (! shown)
        showPluginDatabase ({}, target.kind == hotswap::Kind::channel ? 1 : 0);
    swapBar->setVisible (true);
    swapBar->repaint();
    resized();
    if (onHotSwapChanged != nullptr)
        onHotSwapChanged();
}

bool BrowserComponent::revealCurrentPreset (const hotswap::Target& target)
{
    const auto& song = engine.getSong();
    const auto at = hotswap::findEffect (song, target.instance);
    if (! at.found())
        return false;
    const auto& fx = song.mixer[(std::size_t) at.track].effects[(std::size_t) at.slot];
    if (fx.presetName.empty())
        return false;
    // the files the effect's presets come from: FL's preset folders of the plugin it stands for (the editor's preset
    // list, round 168r) and LYNX's own saved presets of the effect
    juce::Array<juce::File> files, folders;
    if (const auto roots = findFlStudioRoots(); ! roots.data.empty())
        for (const auto& plugin : flPluginsForEffect (fx.kind))
        {
            const auto paths = presetcycle::pluginPresetFolders (roots, false, plugin);
            for (const auto& folder : paths)
                folders.addIfNotAlreadyThere (juce::File (juce::String::fromUTF8 (folder.c_str())));
            for (const auto& p : presetcycle::listPresets (paths))
                files.add (juce::File (juce::String::fromUTF8 (p.path.c_str())));
        }
    folders.add (pluginheader::userFolder (fx.kind));
    files.addArray (pluginheader::userPresets (fx.kind));
    const auto name = juce::String::fromUTF8 (fx.presetName.c_str());
    const auto current = std::find_if (files.begin(), files.end(), [&name] (const juce::File& f) { return f.getFileNameWithoutExtension() == name; });
    if (current == files.end())
        return false;
    const auto file = *current;
    setTab (allTab);
    if (const auto row = tree->revealFile (file); row >= 0)
    {
        tree->select (row, false);
        return true;
    }
    resultsView->setVisible (true);
    treeView->setVisible (false);
    results->setFiles (files, folders, tr ("Presets of {0}", { hotSwapTargetName() }));
    results->select (files.indexOf (file), false);
    presetsListed = true;
    return true;
}

void BrowserComponent::endHotSwap()
{
    if (! swapSession.active())
        return;
    const auto back = swapSession.end();
    swapBar->setVisible (false);
    setTab (back.tab);
    if (std::exchange (presetsListed, false) || searchBox.getText() != juce::String::fromUTF8 (back.search.c_str()))
    {
        searchBox.setText (juce::String::fromUTF8 (back.search.c_str()), false);
        runSearch();
    }
    resized();
    if (onHotSwapChanged != nullptr)
        onHotSwapChanged();
}

void BrowserComponent::hotSwapRefused()
{
    ui::postHint ({ tr ("Hot-swap: this can't take the place of {0}", { hotSwapTargetName() }), {} }, 3000);
}

bool BrowserComponent::hotSwapLoadSelected()
{
    if (! swapSession.active())
        return false;
    const auto* n = activeList().selectedNode();
    if (n == nullptr || n->folder)
        return true; // (the mode keeps Enter: nothing is added to the project while it is on)
    if (n->kind == FileTree::Kind::plugin)
    {
        if (! hotSwapLoadPlugin (n->generator, n->effect))
            hotSwapRefused();
        return true;
    }
    if (n->kind == FileTree::Kind::file && n->file.existsAsFile())
    {
        if (! hotSwapLoadFile (n->file))
            hotSwapRefused();
        return true;
    }
    hotSwapRefused();
    return true;
}

bool BrowserComponent::hotSwapLoadPlugin (const std::function<std::optional<ChannelDef>()>& generator, const std::function<std::optional<EffectSlot>()>& effect)
{
    const auto target = swapSession.target;
    hotswap::Target now;
    if (target.kind == hotswap::Kind::effect && effect)
    {
        auto fx = effect();
        if (! fx)
            return false;
        engine.editSong ([&now, target, fx = std::move (*fx)] (Song& song) mutable { now = hotswap::swapEffect (song, target, std::move (fx)); });
    }
    else if (target.kind == hotswap::Kind::channel && generator)
    {
        auto c = generator();
        if (! c)
            return false;
        if (const auto channel = hotswap::findChannel (engine.getSong(), target.instance); channel >= 0 && onHotSwapWillReplaceInstrument != nullptr)
            onHotSwapWillReplaceInstrument (channel);
        engine.editSong ([&now, target, c = std::move (*c)] (Song& song) mutable { now = hotswap::swapInstrument (song, target, std::move (c)); });
    }
    else
        return false;
    if (! now.active())
        return false;
    swapSession.retarget (now);
    swapBar->repaint();
    if (onHotSwapChanged != nullptr)
        onHotSwapChanged();
    return true;
}

bool BrowserComponent::hotSwapLoadFile (const juce::File& file)
{
    const auto target = swapSession.target;
    const auto& song = engine.getSong();
    const auto loaded = [this] {
        swapSession.loaded();
        swapBar->repaint();
        return true;
    };

    // a hosted plugin's own preset into the linked plugin (as its window's preset menu loads it)
    if (pluginpresetui::isPresetFile (file) && (! file.hasFileExtension ("lynxpreset") || pluginpresetui::readLynxPreset (file).has_value()))
    {
        juce::AudioPluginInstance* plugin = nullptr;
        if (target.kind == hotswap::Kind::effect)
        {
            if (const auto at = hotswap::findEffect (song, target.instance); at.found())
                if (auto* p = dynamic_cast<PluginEffect*> (engine.getEffect (at.track, at.slot).get()))
                    plugin = &p->getInstance();
        }
        else if (const auto c = hotswap::findChannel (song, target.instance); c >= 0)
            if (auto* p = dynamic_cast<PluginInstrument*> (engine.getChannelInstrument (c).get()))
                plugin = &p->getInstance();
        juce::String error;
        if (plugin == nullptr || ! pluginpresetui::load (*plugin, file, error))
            return false;
        engine.markModified();
        // the device comes in on, as the preset loads it (round 190cy, Live 12.4.16)
        if (const auto at = hotswap::findEffect (song, target.instance); at.found() && ! song.mixer[(std::size_t) at.track].effects[(std::size_t) at.slot].enabled)
            engine.editSong ([target] (Song& s) { hotswap::presetLoadedInto (s, target); });
        return loaded();
    }

    switch (hotswap::candidateFor (song, target, file.getFileName().toStdString()))
    {
        case hotswap::Candidate::effectPreset:
        {
            const auto at = hotswap::findEffect (song, target.instance);
            if (file.hasFileExtension ("lynxpreset")) // one of LYNX's presets of this very effect: its settings only
            {
                const auto kind = song.mixer[(std::size_t) at.track].effects[(std::size_t) at.slot].kind;
                const auto preset = presets::fromText (kind, file.loadFileAsString().toStdString());
                if (! preset)
                    return false;
                const auto name = preset->name.empty() ? file.getFileNameWithoutExtension().toStdString() : preset->name;
                bool ok = false;
                engine.editSong ([&ok, target, kind, &preset, &name] (Song& s) {
                    ok = hotswap::swapEffectPreset (s, target, kind, preset->params, preset->hasState ? &preset->state : nullptr, name);
                });
                return ok && loaded();
            }
            juce::MemoryBlock data;
            if (! file.loadFileAsData (data))
                return false;
            const auto* bytes = static_cast<const std::uint8_t*> (data.getData());
            auto fx = effectFromFlPreset (std::vector<std::uint8_t> (bytes, bytes + data.getSize()), song.bpm, natives::flDataReader (flStudioAccess()));
            if (! fx)
                return false;
            hotswap::Target now;
            engine.editSong ([&now, target, fx = std::move (*fx)] (Song& s) mutable { now = hotswap::swapEffect (s, target, std::move (fx)); });
            if (! now.active())
                return false;
            swapSession.retarget (now);
            swapBar->repaint();
            if (onHotSwapChanged != nullptr)
                onHotSwapChanged();
            return true;
        }
        case hotswap::Candidate::synthPreset:
        {
            juce::MemoryBlock data;
            if (! file.loadFileAsData (data))
                return false;
            const auto* bytes = static_cast<const std::uint8_t*> (data.getData());
            const auto synth = synthFromFlPreset (std::vector<std::uint8_t> (bytes, bytes + data.getSize()));
            if (! synth)
                return false;
            ChannelDef c;
            c.name = file.getFileNameWithoutExtension().toStdString();
            c.instrument = InstrumentKind::synth;
            c.synth = *synth;
            return hotSwapLoadPlugin ([c] { return std::optional<ChannelDef> (c); }, {});
        }
        case hotswap::Candidate::sample:
        {
            const auto channel = hotswap::findChannel (song, target.instance);
            if (channel < 0 || onHotSwapSample == nullptr)
                return false;
            onHotSwapSample (channel, file); // (the Channel Rack's own sample load: .speech files ask first)
            return loaded();
        }
        default: break;
    }
    return false;
}

// --- what this Browser shows (round 163) ------------------------------------------------------------

juce::File BrowserComponent::viewFile (Role r)
{
    return PluginHost::settingsFolder().getChildFile (r == Role::samples ? "sample-browser-view.txt" : "browser-view.txt");
}

bool BrowserComponent::sampleBrowserWasOpen()
{
    return browserview::View::fromText (viewFile (Role::samples).loadFileAsString().toStdString()).open;
}

juce::Rectangle<int> BrowserComponent::sampleBrowserBounds()
{
    const auto v = browserview::View::fromText (viewFile (Role::samples).loadFileAsString().toStdString());
    return { v.x, v.y, v.width, v.height };
}

void BrowserComponent::loadView()
{
    viewLoaded = true;
    const auto file = viewFile (role);
    viewFromFile = file.existsAsFile();
    if (viewFromFile)
    {
        view = browserview::View::fromText (file.loadFileAsString().toStdString());
        return;
    }
    view = {};
    if (role == Role::samples)
    {
        // the Sample browser starts with only the places samples live, listing samples and SoundFonts
        view.filter = browserview::ContentFilter::samples;
        view.setShown (browserview::projectRootId, false);
        view.setShown (browserview::pluginsRootId, false);
        for (int i = 0; i < allRoots.size(); ++i)
            if (! browserview::sampleBrowserShowsByDefault (allRootKinds[(std::size_t) i]))
                view.setShown (allRoots[i].getFullPathName().toStdString(), false);
    }
}

void BrowserComponent::saveView() const
{
    if (! viewLoaded)
        return;
    const auto file = viewFile (role);
    auto v = view;
    if (role == Role::samples) // the window's open state and bounds are the window's to write
    {
        const auto old = browserview::View::fromText (file.loadFileAsString().toStdString());
        v.open = old.open;
        v.x = old.x, v.y = old.y, v.width = old.width, v.height = old.height;
    }
    file.getParentDirectory().createDirectory();
    file.replaceWithText (juce::String::fromUTF8 (v.toText().c_str()));
}

bool BrowserComponent::lists (const juce::File& file) const
{
    // (round 190iz: under the tab's / Live entry's own filter: PRESETS, SOUNDS, Samples...)
    return browserview::listed (file.getFileName().toStdString(), activeFilter()) || file.isDirectory();
}

std::shared_ptr<const search::FolderMatches> BrowserComponent::filterMatches() const
{
    const auto filter = activeFilter(); // (round 190iz)
    if (filter == browserview::ContentFilter::all || searcher == nullptr)
        return nullptr;
    return searcher->service->folderMatches ((int) filter, [filter] (const std::string& name) { return browserview::listed (name, filter); });
}

bool BrowserComponent::filterHides (const juce::File& folder) const
{
    // from the search index (cached; never waits for it): a folder it doesn't know yet stays listed
    const auto matches = filterMatches();
    return matches != nullptr && matches->of (folder.getFullPathName().toStdString()) == search::FolderMatches::Contents::none;
}

void BrowserComponent::applyFilterToTree()
{
    const auto matches = filterMatches();
    if (matches == nullptr || matches->generation == prunedGeneration)
        return;
    prunedGeneration = matches->generation;
    if (tree->pruneFolders ([this] (const juce::File& f) { return filterHides (f); }))
        treeView->setViewPosition (treeView->getViewPosition()); // (keeps the scroll inside the shorter list)
}

void BrowserComponent::setContentFilter (browserview::ContentFilter filter)
{
    if (filter == view.filter)
        return;
    view.filter = filter;
    saveView();
    refreshViewButton();
    prunedGeneration = 0;
    rebuildTree();
    refreshFoldersPanel();
    if (searchBox.getText().trim().isNotEmpty())
        runSearch();
}

void BrowserComponent::setRootShown (const juce::String& id, bool shown)
{
    if (view.shows (id.toStdString()) == shown)
        return;
    view.setShown (id.toStdString(), shown);
    saveView();
    rebuildRoots();
    if (searchBox.getText().trim().isNotEmpty())
        runSearch();
}

void BrowserComponent::showAllRoots()
{
    view.hidden.clear();
    view.excluded.clear(); // (and every folder of the merged roots, round 178a)
    saveView();
    rebuildRoots();
}

juce::String BrowserComponent::rootId (const juce::File& folder) const
{
    return allRoots.contains (folder) ? folder.getFullPathName() : juce::String();
}

void BrowserComponent::refreshViewButton()
{
    const auto filtered = view.filter != browserview::ContentFilter::all;
    int hidden = 0;
    for (const auto& id : view.hidden)
        hidden += id == browserview::projectRootId || id == browserview::pluginsRootId
                          || allRoots.contains (juce::File (juce::String::fromUTF8 (id.c_str())))
                      ? 1 : 0;
    viewButton.setToggleState (filtered || hidden > 0, juce::dontSendNotification);
    juce::String text = tr ("Browser options: what to list, which folders to show");
    if (filtered)
        text << "  (" << tr (browserview::contentFilterLabel (view.filter)) << ")";
    if (hidden > 0)
        text << "  (" << (hidden == 1 ? tr ("1 folder hidden") : tr ("{0} folders hidden", { juce::String (hidden) })) << ")";
    viewButton.setTooltip (text);
    ui::setHint (viewButton, text, true);
}

void BrowserComponent::addHideItem (juce::PopupMenu& menu, const juce::String& id, const juce::String& label)
{
    menu.addItem (tr ("Hide \"{0}\" from this Browser", { label }), [safe = juce::Component::SafePointer<BrowserComponent> (this), id] {
        if (safe != nullptr)
            safe->setRootShown (id, false);
    });
}

void BrowserComponent::addViewItems (juce::PopupMenu& menu)
{
    juce::Component::SafePointer<BrowserComponent> safe (this);
    // what is listed (folders are always listed)
    juce::PopupMenu filters;
    for (int i = 0; i < browserview::contentFilterCount; ++i)
    {
        const auto f = (browserview::ContentFilter) i;
        filters.addItem (tr (browserview::contentFilterLabel (f)), true, view.filter == f, [safe, f] {
            if (safe != nullptr)
                safe->setContentFilter (f);
        });
    }
    menu.addSubMenu (tr ("Show") + ": " + tr (browserview::contentFilterLabel (view.filter)), filters);

    // which top-level entries are shown and the user's own folders: a window (round 164; a checklist sub-menu before)
    menu.addItem (tr ("Show folders..."), [safe] {
        if (safe != nullptr)
            safe->showFoldersDialog();
    });
}

// --- the "Show folders" window (round 164) --------------------------------------------------------------

// Which top-level entries this Browser shows, as a checklist (ticks apply at once), the user's own folders with a
// Remove button, Add folder..., Show all. Each entry says where it is and, under a content filter, whether it holds
// anything to list (those folders are left out of the tree while the filter is on).
class BrowserComponent::FoldersPanel final : public juce::Component
{
public:
    using Entry = FolderEntry;

    explicit FoldersPanel (BrowserComponent* b) : browser (b)
    {
        title.setFont (theme::font (theme::type::body, true));
        title.setColour (juce::Label::textColourId, theme::colour::textPrimary);
        addAndMakeVisible (title);
        note.setFont (theme::font (theme::type::small));
        note.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        note.setJustificationType (juce::Justification::topLeft);
        addAndMakeVisible (note);
        list.setViewedComponent (&rows, false);
        list.setScrollBarsShown (true, false);
        list.setScrollBarThickness (10);
        addAndMakeVisible (list);

        addFolder.setButtonText (tr ("Add folder..."));
        addFolder.setTooltip (tr ("Add one of your own folders to the Browsers (samples, projects, SoundFonts...)"));
        addFolder.onClick = [this] { if (browser != nullptr) browser->addUserFolder(); };
        showAll.setButtonText (tr ("Show all"));
        showAll.setTooltip (tr ("Tick every folder"));
        showAll.onClick = [this] { if (browser != nullptr) browser->showAllRoots(); };
        close.setButtonText (tr ("Close"));
        close.setColour (juce::TextButton::buttonColourId, theme::colour::accent);
        close.setColour (juce::TextButton::textColourOffId, theme::colour::onAccent);
        close.onClick = [this] { closeToolWindowOf (*this); };
        for (auto* button : { &addFolder, &showAll, &close })
            addAndMakeVisible (button);
        refresh();
        setSize (520, 470);
    }

    // the browser's entries again (a tick, an added / removed folder, the filter changed)
    void refresh()
    {
        if (browser == nullptr)
            return;
        setEntries (browser->folderEntries(), browser->role == Role::samples,
                    browser->view.filter == browserview::ContentFilter::all ? juce::String() : tr (browserview::contentFilterLabel (browser->view.filter)));
    }

    void setEntries (std::vector<Entry> newEntries, bool sampleBrowser, const juce::String& filterName)
    {
        entries = std::move (newEntries);
        title.setText (sampleBrowser ? tr ("Folders shown in the Sample browser") : tr ("Folders shown in the Browser"), juce::dontSendNotification);
        auto text = tr ("Untick what you don't use: it leaves this Browser and its searches. Nothing is deleted.");
        if (filterName.isNotEmpty())
            text << "\n" << tr ("Listing \"{0}\": folders with nothing of that kind are left out of the tree.", { filterName });
        note.setText (text, juce::dontSendNotification);
        int shown = 0;
        for (const auto& e : entries)
            shown += e.shown ? 1 : 0;
        showAll.setEnabled (shown < (int) entries.size());

        rows.deleteAllChildren();
        for (int i = 0; i < (int) entries.size(); ++i)
            rows.addAndMakeVisible (new Row (*this, i));
        layoutRows();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (14, 12);
        title.setBounds (area.removeFromTop (22));
        note.setBounds (area.removeFromTop (note.getText().containsChar ('\n') ? 36 : 20));
        area.removeFromTop (6);
        auto bottom = area.removeFromBottom (28);
        close.setBounds (bottom.removeFromRight (90));
        addFolder.setBounds (bottom.removeFromLeft (120));
        bottom.removeFromLeft (8);
        showAll.setBounds (bottom.removeFromLeft (90));
        area.removeFromBottom (10);
        list.setBounds (area);
        layoutRows();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg2);
        g.setColour (theme::colour::border);
        g.drawRect (list.getBounds().expanded (1), 1);
    }

    static constexpr int rowHeight = 34;

private:
    struct Row final : public juce::Component
    {
        Row (FoldersPanel& p, int i) : panel (p), index (i)
        {
            const auto& e = panel.entries[(std::size_t) i];
            tick.setToggleState (e.shown, juce::dontSendNotification);
            tick.setTooltip (e.shown ? tr ("Shown: untick to hide it from this Browser") : tr ("Hidden: tick to show it"));
            tick.onClick = [this] { panel.toggle (index, tick.getToggleState()); };
            addAndMakeVisible (tick);
            if (e.user)
            {
                remove.setTooltip (tr ("Remove this folder from the Browsers (the folder itself stays on the disk)"));
                remove.setFlat (true);
                remove.onClick = [this] { panel.remove (index); };
                addAndMakeVisible (remove);
            }
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (6, 0);
            tick.setBounds (area.removeFromLeft (26).withSizeKeepingCentre (24, 24));
            if (remove.isVisible())
                remove.setBounds (area.removeFromRight (26).withSizeKeepingCentre (22, 22));
        }

        void paint (juce::Graphics& g) override
        {
            const auto& e = panel.entries[(std::size_t) index];
            if (index % 2 == 1)
                g.fillAll (theme::colour::bg2.brighter (0.06f));
            auto area = getLocalBounds().reduced (6, 2).withTrimmedLeft (32).withTrimmedRight (remove.isVisible() ? 30 : 0);
            auto top = area.removeFromTop (area.getHeight() / 2 + 1);
            g.setFont (theme::font (theme::type::body, e.shown));
            g.setColour (e.shown ? theme::colour::textPrimary : theme::colour::textDisabled);
            juce::String name = e.name;
            if (e.user)
                name << "  " << tr ("(your folder)");
            g.drawText (name, top, juce::Justification::bottomLeft, true);
            g.setFont (theme::font (theme::type::small));
            g.setColour (theme::colour::textDisabled);
            auto detail = e.path;
            if (e.empty)
                detail = tr ("nothing to list under the filter") + "  ·  " + detail;
            g.drawFittedText (detail, area, juce::Justification::topLeft, 1, 0.9f);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mouseWasClicked() && ! e.mods.isPopupMenu())
                tick.triggerClick(); // the whole row ticks
        }

        FoldersPanel& panel;
        int index;
        juce::ToggleButton tick;
        IconButton remove { "remove folder", Icon::close };
    };

    void layoutRows()
    {
        const auto width = list.getWidth() - (entries.size() * (std::size_t) rowHeight > (std::size_t) list.getHeight() ? 10 : 0);
        rows.setSize (juce::jmax (0, width), (int) entries.size() * rowHeight);
        for (int i = 0; i < rows.getNumChildComponents(); ++i)
            rows.getChildComponent (i)->setBounds (0, i * rowHeight, rows.getWidth(), rowHeight);
    }

    void toggle (int index, bool shown)
    {
        if (browser != nullptr && juce::isPositiveAndBelow (index, (int) entries.size()))
        {
            const auto id = entries[(std::size_t) index].id;
            juce::MessageManager::callAsync ([safe = browser, id, shown] { // (rebuilds the rows: not inside a row's click)
                if (safe != nullptr)
                    safe->setRootShown (id, shown);
            });
        }
    }

    void remove (int index)
    {
        if (browser != nullptr && juce::isPositiveAndBelow (index, (int) entries.size()))
        {
            const juce::File folder (entries[(std::size_t) index].path);
            juce::MessageManager::callAsync ([safe = browser, folder] {
                if (safe != nullptr)
                    safe->removeUserFolder (folder);
            });
        }
    }

    juce::Component::SafePointer<BrowserComponent> browser;
    std::vector<Entry> entries;
    juce::Label title, note;
    juce::Viewport list;
    juce::Component rows;
    juce::TextButton addFolder, showAll, close;
};

std::vector<BrowserComponent::FolderEntry> BrowserComponent::folderEntries() const
{
    std::vector<FolderEntry> out;
    out.push_back ({ browserview::projectRootId, tr ("Current project"), tr ("the project's patterns, channels, samples and history"),
                     view.shows (browserview::projectRootId), false, false });
    out.push_back ({ browserview::pluginsRootId, tr ("Plugin database"), tr ("LYNX SHARE's, FL's and your installed plugins"),
                     view.shows (browserview::pluginsRootId), false, false });
    for (int i = 0; i < allRoots.size(); ++i)
    {
        FolderEntry e;
        e.id = e.path = allRoots[i].getFullPathName();
        e.name = allRootNames[i];
        e.shown = view.shows (e.id.toStdString());
        e.user = allRootKinds[(std::size_t) i] == browserview::RootKind::user;
        const auto merged = withMerged ({ allRoots[i] });
        e.empty = std::all_of (merged.begin(), merged.end(), [this] (const juce::File& f) { return filterHides (f); });
        out.push_back (std::move (e));
    }
    return out;
}

void BrowserComponent::refreshFoldersPanel()
{
    if (auto* panel = dynamic_cast<FoldersPanel*> (foldersPanel.getComponent()))
        panel->refresh();
}

void BrowserComponent::showFoldersDialog()
{
    auto panel = std::make_unique<FoldersPanel> (this);
    foldersPanel = panel.get();
    openToolWindow (std::move (panel), role == Role::samples ? tr ("Sample browser folders") : tr ("Browser folders"),
                    role == Role::samples ? "sample-browser-folders" : "browser-folders");
}

std::unique_ptr<juce::Component> BrowserComponent::makeFoldersPanelForSnapshot()
{
    auto panel = std::make_unique<FoldersPanel> (this);
    // a user folder (with its Remove button) and one hidden entry, whatever this machine has
    auto entries = folderEntries();
    entries.push_back ({ "D:\\Samples\\Drums", "Drums", "D:\\Samples\\Drums", true, true, false });
    if (entries.size() > 3)
        entries[2].shown = false;
    panel->setEntries (std::move (entries), false, {});
    return panel;
}

void BrowserComponent::showViewMenu()
{
    buildViewMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&viewButton));
}

juce::PopupMenu BrowserComponent::buildViewMenu()
{
    juce::PopupMenu menu;
    addViewItems (menu);
    juce::Component::SafePointer<BrowserComponent> safe (this);
    menu.addSeparator();
    // round 190ce (the reference's preview switch): off = a selected file is shown, not played; Play on the strip plays it
    menu.addItem (tr ("Preview files when they are selected"), true, autoPreview, [safe] {
        autoPreview = ! autoPreview;
        if (! autoPreview && safe != nullptr)
            safe->stopPreview();
        if (safe != nullptr && safe->onPreviewVolumeChanged != nullptr)
            safe->onPreviewVolumeChanged(); // (saved with the preview volume)
    });
    // FL's Browser menu › View "Auto-hide" (round 190mv, DF-107; capture 190mv A2: only a floating Browser hides)
    if (role != Role::samples && onAutoHideChanged != nullptr)
        menu.addItem (tr ("Auto-hide"), true, autoHide, [safe] {
            autoHide = ! autoHide;
            if (safe != nullptr && safe->onAutoHideChanged != nullptr)
                safe->onAutoHideChanged();
        });
    // FL's Browser menu › View "Full sample preview" (round 190iz; off: a click plays the first 5 seconds, as FL)
    menu.addItem (tr ("Full sample preview"), true, engine.isFullSamplePreview(), [safe] {
        if (safe == nullptr)
            return;
        safe->engine.setFullSamplePreview (! safe->engine.isFullSamplePreview());
        safe->savePrefs();
    });
    // LYNX's own (round 190aq, off by default): loops are auditioned at the project's tempo, starting on the beat
    menu.addItem (tr ("Preview in time (loops play at the project's tempo, on the beat)"), true, engine.isPreviewInTime(), [safe] {
        if (safe == nullptr)
            return;
        safe->engine.setPreviewInTime (! safe->engine.isPreviewInTime());
        if (safe->onPreviewVolumeChanged != nullptr)
            safe->onPreviewVolumeChanged(); // (saved with the preview volume)
    });
    // round 190kx (Simple layout, as BandLab): dropped loops take the tempo and key found in their audio
    if (showsLoopRowInfo())
        menu.addItem (tr ("Dropped loops take the tempo and key found in their audio"), true, loopanalysis::dropDetects().load(),
                      [] { workflowextras::setExtra ("simple.loopDetect", ! loopanalysis::dropDetects().load()); });
    menu.addSeparator();
    menu.addItem (tr ("Refresh"), [safe] { if (safe != nullptr) safe->rebuildRoots(); });
    menu.addItem (tr ("Refresh search index"), [safe] { if (safe != nullptr) safe->refreshSearchIndex (false); });
    if (onToggleSampleBrowser != nullptr)
    {
        menu.addSeparator();
        menu.addItem (role == Role::samples ? tr ("Close the Sample browser") : tr ("Sample browser (separate window)"),
                      [safe] { if (safe != nullptr && safe->onToggleSampleBrowser != nullptr) safe->onToggleSampleBrowser(); });
    }
    return menu;
}

namespace
{
// The Sample browser window's content: a drag container (samples dragged out of this window reach the Channel Rack,
// the Playlist and the Mixer of the main window) holding the Browser; it keeps the window's bounds and whether the
// user closed it.
class SampleBrowserHost final : public juce::Component,
                                public juce::DragAndDropContainer
{
public:
    SampleBrowserHost (AudioEngine& engine, const std::function<void (BrowserComponent&)>& wire)
        : browser (engine, BrowserComponent::Role::samples)
    {
        if (wire != nullptr)
            wire (browser);
        addAndMakeVisible (browser);
        setSize (340, 620);
        if (std::exchange (BrowserComponent::openLoopsNext, false)) // (round gaps190kf: the Simple layout's Loops tile)
            browser.setLoopsShown (true);
    }

    ~SampleBrowserHost() override
    {
        // a window the user closed is hidden first (ToolWindow::closeSoon); one deleted while visible goes with LYNX
        // and opens again at the next start
        auto* window = getTopLevelComponent();
        auto v = browser.getView();
        v.open = window != this && window->isVisible();
        if (window != this)
        {
            const auto b = window->getBounds();
            v.x = b.getX();
            v.y = b.getY();
            v.width = b.getWidth();
            v.height = b.getHeight();
        }
        file().getParentDirectory().createDirectory();
        file().replaceWithText (juce::String::fromUTF8 (v.toText().c_str()));
    }

    void resized() override { browser.setBounds (getLocalBounds()); }

    // issue #174: a sample dragged from this window onto a plugin's own editor (or out of LYNX) goes on as a file drag
    bool shouldDropFilesWhenDraggedExternally (const juce::DragAndDropTarget::SourceDetails& details, juce::StringArray& files,
                                               bool& canMoveFiles) override
    {
        return externaldrag::shouldDropFiles (details, nullptr, files, canMoveFiles);
    }

    static juce::File file() { return PluginHost::settingsFolder().getChildFile ("sample-browser-view.txt"); }

    BrowserComponent browser;
};
} // namespace

namespace
{
// Round 190lx (DS-42): the Simple layout's Loops dock (BrowserComponent::makeLoopsDock)
class LoopsDock final : public juce::Component
{
public:
    LoopsDock (AudioEngine& engine, const std::function<void (BrowserComponent&)>& wire, std::function<void()> closed)
        : browser (engine, BrowserComponent::Role::samples), onClose (std::move (closed))
    {
        if (wire != nullptr)
            wire (browser);
        title.setText (tr ("Loops"), juce::dontSendNotification);
        title.setFont (theme::font (theme::type::title, true));
        title.setColour (juce::Label::textColourId, theme::colour::textPrimary);
        title.setInterceptsMouseClicks (false, false);
        closeButton.setButtonText (juce::String::fromUTF8 ("\xc3\x97"));
        closeButton.setTooltip (tr ("Close"));
        closeButton.onClick = [this] {
            if (onClose != nullptr)
                onClose();
        };
        addAndMakeVisible (title);
        addAndMakeVisible (closeButton);
        addAndMakeVisible (browser);
        browser.setLoopsShown (true);
        setTitle (tr ("Loops"));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg2);
        g.setColour (theme::colour::border); // (BandLab: a 1 px edge on the left)
        g.drawVerticalLine (0, 0.0f, (float) getHeight());
    }

    void resized() override
    {
        auto r = getLocalBounds().withTrimmedLeft (1);
        auto head = r.removeFromTop (36).reduced (12, 4);
        closeButton.setBounds (head.removeFromRight (28));
        title.setBounds (head);
        browser.setBounds (r);
    }

    BrowserComponent browser;

private:
    juce::Label title;
    juce::TextButton closeButton;
    std::function<void()> onClose;
};
} // namespace

std::unique_ptr<juce::Component> BrowserComponent::makeLoopsDock (AudioEngine& engine, const std::function<void (BrowserComponent&)>& wire,
                                                                  std::function<void()> onClose)
{
    return std::make_unique<LoopsDock> (engine, wire, std::move (onClose));
}

BrowserComponent* BrowserComponent::browserOfLoopsDock (juce::Component* dock)
{
    auto* d = dynamic_cast<LoopsDock*> (dock);
    return d != nullptr ? &d->browser : nullptr;
}

std::unique_ptr<juce::Component> BrowserComponent::makeSampleBrowserContent (AudioEngine& engine, const std::function<void (BrowserComponent&)>& wire)
{
    auto host = std::make_unique<SampleBrowserHost> (engine, wire);
    auto v = host->browser.getView();
    v.open = true; // (written at once: a crash keeps it open next time too)
    SampleBrowserHost::file().getParentDirectory().createDirectory();
    SampleBrowserHost::file().replaceWithText (juce::String::fromUTF8 (v.toText().c_str()));
    return host;
}

juce::File BrowserComponent::firstSampleFolder() const
{
    using browserview::RootKind;
    for (auto wanted : { RootKind::flSamples, RootKind::user, RootKind::music })
        for (int i = 0; i < allRoots.size() && i < (int) allRootKinds.size(); ++i)
            if (allRootKinds[(std::size_t) i] == wanted && allRoots[i].isDirectory())
                return allRoots[i];
    return {};
}

void BrowserComponent::rebuildRoots()
{
    roots.clear();
    rootNames.clear();
    allRoots.clear();
    allRootNames.clear();
    allRootKinds.clear();
    rootAlso.clear();
    using browserview::RootKind;

    // FL folders (if installed) and user folders. Round 190bj (QA: the FL folders were listed with LYNX_NO_FL=1): the
    // installation comes from findFlStudio, as everywhere else (FL's own install path, Program Files\Image-Line, Wine,
    // LYNX_FL_INSTALL; none with LYNX_NO_FL)
    const auto flInstall = findFlStudio();
    const auto fl = flInstall.installDir;

    auto add = [this] (const juce::String& name, const juce::File& folder, RootKind kind) {
        if (folder.isDirectory() && ! allRoots.contains (folder))
        {
            allRoots.add (folder);
            allRootNames.add (name);
            allRootKinds.push_back (kind);
        }
    };
    // one root for several folders, as FL's Browser shows its Scores and Mixer presets (the user's Presets folder and
    // its own Data\\Patches one, round 178a): the first folder that exists is the root, the others are merged into it
    auto addMerged = [this, &add] (const juce::String& name, juce::Array<juce::File> folders, RootKind kind) {
        folders.removeIf ([] (const juce::File& f) { return ! f.isDirectory(); });
        if (folders.isEmpty())
            return;
        const auto root = folders.removeAndReturn (0);
        add (name, root, kind);
        if (! folders.isEmpty() && allRoots.contains (root))
            rootAlso[root.getFullPathName()] = folders;
    };

    const auto data = fl.getChildFile ("Data");
    add (tr ("FL samples (Packs)"), data.getChildFile ("Patches").getChildFile ("Packs"), RootKind::flSamples);
    add (tr ("FL demo projects"), data.getChildFile ("Demo projects"), RootKind::flDemos);
    const auto userData = flInstall.userData;
    auto userPresets = [&userData] (const char* folder) {
        return userData.isDirectory() ? userData.getChildFile ("Presets").getChildFile (folder) : juce::File();
    };
    addMerged (tr ("Scores"), { userPresets ("Scores"), data.getChildFile ("Patches").getChildFile ("Scores") }, RootKind::flMidi);
    add (tr ("3x Osc presets (FL)"), data.getChildFile ("Patches").getChildFile ("Channel presets").getChildFile ("3x Osc"), RootKind::flPresets);
    add (tr ("Synth presets (FL)"), data.getChildFile ("Patches").getChildFile ("Plugin presets").getChildFile ("Generators"), RootKind::flPresets);
    add (tr ("Speech (FL)"), data.getChildFile ("Patches").getChildFile ("Speech"), RootKind::speech);
    speechui::userSpeechFolder().createDirectory(); // (the Speech properties save there; round 168b)
    add (tr ("Speech"), speechui::userSpeechFolder(), RootKind::speech);
    // FL's Browser folder "Mixer presets" (REA round 167q, FUN_009db5b0: the user's and the installation's, one root)
    addMerged (tr ("Mixer presets"), { userPresets ("Mixer presets"), data.getChildFile ("Patches").getChildFile ("Mixer presets") },
               RootKind::flPresets);
    pluginpresetui::presetsRoot().createDirectory(); // (the plugin window saves presets there; round 162d)
    add (tr ("Plugin presets"), pluginpresetui::presetsRoot(), RootKind::pluginPresets);
    audioclipboard::folder().createDirectory(); // (Tools › Clipboard saves there; round 181b)
    add (tr ("Clipboard files"), audioclipboard::folder(), RootKind::clipboardFiles);
    add (tr ("Documents"), juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), RootKind::documents);
    add (tr ("Music"), juce::File::getSpecialLocation (juce::File::userMusicDirectory), RootKind::music);
    add (tr ("Desktop"), juce::File::getSpecialLocation (juce::File::userDesktopDirectory), RootKind::desktop);
    add (tr ("Downloads"), juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads"), RootKind::downloads);

    for (const auto& line : juce::StringArray::fromLines (userFoldersFile().loadFileAsString()))
        if (line.trim().isNotEmpty())
            add (juce::File (line.trim()).getFileName(), juce::File (line.trim()), RootKind::user);

    if (! viewLoaded)
        loadView();
    bool migrated = false; // (round 178a: the old "FL MIDI" / "Mixer presets (FL)" roots' hidden state)
    for (const auto& [rootPath, folders] : rootAlso)
        migrated = browserview::migrateMergedRoot (view, rootPath.toStdString(), toPaths (folders)) || migrated;
    if (migrated && viewFromFile)
        saveView();
    for (int i = 0; i < allRoots.size(); ++i)
        if (view.shows (allRoots[i].getFullPathName().toStdString()))
        {
            roots.add (allRoots[i]);
            rootNames.add (allRootNames[i]);
        }
    refreshViewButton();
    refreshFoldersPanel();

    loadCollections();
    if (liveLayout) // round 190iz: Live's Places follow the folders
        sidebar->refresh();

    rebuildTree();
}

// A collection's items for the STARRED tab and Live's Collections (round 190iz: from the STARRED tab's code of round 190ce):
// folders first (each opens like the ALL tab's), then files, then Plugin database entries, each by name; missing files
// and plugins that are gone are left out.
std::vector<std::unique_ptr<BrowserComponent::FileTree::Node>> BrowserComponent::FileTree::collectionNodes (BrowserComponent& browser, int collection)
{
    const auto& store = collectionStore();
    std::vector<std::unique_ptr<FileTree::Node>> out;
    juce::Array<juce::File> folders, files;
    std::vector<std::unique_ptr<FileTree::Node>> plugins;
    for (const auto& item : store.itemsOf (collection))
    {
        if (collections::isPluginItem (item))
        {
            if (auto n = FileTree::makePluginItem (browser, collections::pluginKeyOf (item)))
                plugins.push_back (std::move (n));
            continue;
        }
        if (const juce::File f (juce::String::fromUTF8 (item.c_str())); f.isDirectory())
            folders.add (f);
        else if (f.existsAsFile())
            files.add (f);
    }
    const auto byName = [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; };
    std::sort (folders.begin(), folders.end(), byName);
    std::sort (files.begin(), files.end(), byName);
    std::sort (plugins.begin(), plugins.end(), [] (const auto& a, const auto& b) { return a->label.compareNatural (b->label) < 0; });
    for (const auto& f : folders)
        out.push_back (FileTree::makeFolder (f, f.getFileName(), collectionColour (collection)));
    for (const auto& f : files)
    {
        auto n = std::make_unique<FileTree::Node>();
        n->file = f;
        out.push_back (std::move (n));
    }
    for (auto& n : plugins)
        out.push_back (std::move (n));
    return out;
}

// Round 190iz: Live's Instruments / Audio Effects / MIDI Effects / Plug-Ins / Modulators: the Plugin database entries
// of that kind as one list, by name (the same rows, double-click and drag as in the database's tree)
std::vector<std::unique_ptr<BrowserComponent::FileTree::Node>> BrowserComponent::FileTree::livePluginNodes (BrowserComponent& browser, browserplaces::Category category)
{
    std::vector<std::unique_ptr<FileTree::Node>> out;
    auto safe = juce::Component::SafePointer<BrowserComponent> (&browser);
    auto& engine = browser.engine;
    const auto wanted = browserplaces::wantsPlugins (category);
    if (wanted != 0)
        for (auto& e : quickpick::generatorEntries (engine))
        {
            const char* groups[] = { "LYNX", "FL engines", "VFX", "" };
            if (! browserplaces::keepsPlugin (category, true, groups[juce::jlimit (0, 3, e.folder)], e.label.toStdString()))
                continue;
            auto n = std::make_unique<FileTree::Node>();
            n->kind = FileTree::Kind::plugin;
            n->pluginKey = e.key;
            n->label = quickpick::shownName (engine, e.key, e.label);
            n->original = e.label;
            n->detail = e.detail;
            n->icon = e.folder == 3 ? Icon::plugin : Icon::keyboard;
            n->generator = e.make;
            n->activate = [safe, make = e.make] {
                if (auto c = make(); c && safe != nullptr && safe->onAddInstrument)
                    safe->onAddInstrument (std::move (*c));
            };
            out.push_back (std::move (n));
        }
    if (wanted != 1)
        for (auto& e : quickpick::effectEntries (engine))
        {
            const char* groups[] = { "LYNX", "FL engines", "", "" };
            if (! browserplaces::keepsPlugin (category, false, groups[juce::jlimit (0, 3, e.folder)], e.label.toStdString()))
                continue;
            auto n = std::make_unique<FileTree::Node>();
            n->kind = FileTree::Kind::plugin;
            n->pluginKey = e.key;
            n->label = quickpick::shownName (engine, e.key, e.label);
            n->original = e.label;
            n->detail = e.detail;
            n->icon = e.folder == 3 ? Icon::plugin : Icon::mixer;
            n->effect = e.make;
            n->activate = [safe, make = e.make] {
                if (auto fx = make(); fx && safe != nullptr && safe->onAddEffect)
                    safe->onAddEffect (std::move (*fx));
            };
            out.push_back (std::move (n));
        }
    std::stable_sort (out.begin(), out.end(), [] (const auto& a, const auto& b) { return a->label.compareNatural (b->label) < 0; });
    return out;
}

bool BrowserComponent::showsRoot (int i) const
{
    if (! juce::isPositiveAndBelow (i, allRoots.size()) || ! roots.contains (allRoots[i]))
        return false; // hidden from this Browser (round 163)
    const auto kind = allRootKinds[(std::size_t) i];
    if (liveLayout)
    {
        if (liveSelection.category == browserplaces::Category::folder)
            return allRoots[i].getFullPathName().toStdString() == liveSelection.path;
        return browserplaces::categoryShowsRoot (liveSelection.category, kind);
    }
    return browserplaces::tabShowsRoot ((browserplaces::Tab) tab, kind);
}

browserview::ContentFilter BrowserComponent::activeFilter() const
{
    if (liveLayout)
        return browserplaces::categoryFilter (liveSelection.category, view.filter);
    return browserplaces::tabFilter ((browserplaces::Tab) tab, view.filter);
}

void BrowserComponent::rebuildTree()
{
    using browserplaces::Category;
    std::vector<std::unique_ptr<FileTree::Node>> top;
    const auto cat = liveSelection.category;
    categoryQuery = {};
    auto finish = [this, &top] {
        tree->setTopLevel (std::move (top));
        treeView->setViewPosition (0, 0);
    };
    if (liveLayout && cat == Category::collection) // round 190iz: Live's Collections
    {
        top = FileTree::collectionNodes (*this, liveSelection.collection);
        finish();
        return;
    }
    if (! liveLayout && tab == starredTab)
    {
        // Only Starred switched on (as it is at first): its items alone, the tab as it always was. More than one (or
        // another one): each collection under its heading.
        const auto shown = collectionStore().shown();
        if (shown.size() == 1 && shown.front() == 0)
            top = FileTree::collectionNodes (*this, 0);
        else
            for (const auto c : shown)
            {
                auto items = FileTree::collectionNodes (*this, c);
                auto heading = FileTree::makeCollection (c, (int) items.size(), closedCollections.count (c) == 0);
                heading->children = std::move (items);
                top.push_back (std::move (heading));
            }
        finish();
        return;
    }
    // round 190iz: FL's PLUGINS tab (the Plugin database, opened) and Live's plugin categories (one list each)
    if (liveLayout ? (browserplaces::wantsPlugins (cat) >= 0 && cat != Category::all) : tab == pluginsTab)
    {
        if (liveLayout)
            top = FileTree::livePluginNodes (*this, cat);
        else
        {
            auto db = FileTree::makePluginDatabase (*this, openPluginFolders);
            db->expanded = true;
            top.push_back (std::move (db));
        }
        finish();
        return;
    }
    const auto allLike = liveLayout ? cat == Category::all : tab == allTab;
    if (liveLayout ? (cat == Category::currentProject || (allLike && view.shows (browserview::projectRootId)))
                   : (tab == projectTab || (allLike && view.shows (browserview::projectRootId))))
        top.push_back (FileTree::makeProject (shownTab() == projectTab));
    // FL's Plugin database, under the project (round 142); always while hot-swap is on (round 190ce: it is what the
    // linked device is replaced from, also when the user's view leaves it out)
    if (allLike && (view.shows (browserview::pluginsRootId) || swapSession.active()))
    {
        browserStartupMark ("  browser: roots (FL folders, user folders, view, collections)");
        top.push_back (FileTree::makePluginDatabase (*this, openPluginFolders));
        browserStartupMark ("  browser: the Plugin database tree");
    }
    // round 190ke (owner decision D2): LYNX's own content with its tags, in the Simple layout
    if (! liveLayout && layoutstyle::current() == layoutstyle::Style::simple && (allLike || tab == presetsTab || tab == soundsTab))
        top.push_back (FileTree::makeFactoryRoot (*this, openFactoryFolders));

    if (liveLayout && cat == Category::templates) // round 190iz: Live's Templates: the user's project templates
    {
        if (const auto folder = userTemplatesFolder(); folder.isDirectory())
        {
            auto n = FileTree::makeFolder (folder, tr ("Templates"), rootColour (99));
            n->expanded = true;
            top.push_back (std::move (n));
        }
    }
    else if (liveLayout || allLike || tab == presetsTab || tab == soundsTab)
        for (int i = 0; i < allRoots.size(); ++i)
        {
            if (! showsRoot (i))
                continue;
            if (const auto merged = withMerged ({ allRoots[i] });
                std::all_of (merged.begin(), merged.end(), [this] (const juce::File& f) { return filterHides (f); }))
                continue; // nothing in it is listed under the content filter (round 164)
            // the FL folders keep their colour by position; the user's own folders share one
            auto colour = rootColour (i < 9 ? i : 99);
            auto n = FileTree::makeFolder (allRoots[i], allRootNames[i], colour);
            n->also = mergedInto (allRoots[i]);
            if (allRootKinds[(std::size_t) i] == browserview::RootKind::flMidi)
                n->icon = Icon::note; // (FL's Scores folder has a note)
            if (liveLayout && cat == Category::folder) // (Live's place: its content at once)
                n->expanded = true;
            top.push_back (std::move (n));
        }

    finish();
    // Live's Drums (round 190iz): the sample places searched for drum words, in the results list
    if (liveLayout && cat == Category::drums)
    {
        categoryQuery = browserplaces::drumsQuery();
        runSearch();
    }
}

void BrowserComponent::setTab (int newTab)
{
    newTab = juce::jlimit (0, tabCount - 1, newTab);
    if (liveLayout) // round 190iz: no tabs in the Clip-launch layout: Live's All / Current Project / Favorites instead
    {
        browserplaces::Entry e;
        if (newTab == projectTab)
            e.category = browserplaces::Category::currentProject;
        else if (newTab == starredTab)
        {
            e.group = browserplaces::Group::collections;
            e.category = browserplaces::Category::collection;
            e.collection = 0;
        }
        if (e.category != liveSelection.category || e.collection != liveSelection.collection || ! liveSelection.path.empty())
            showLiveEntry (e);
        return;
    }
    if (newTab == tab)
        return;
    tab = newTab;
    rebuildTree();
    tabs->repaint();
    if (searchBox.getText().isNotEmpty() || tagFilterActive()) // (round 190iz: the tag filter too)
        runSearch();
    savePrefs(); // round 190iz: FL remembers the last tab (LastTabIndex)
}

void BrowserComponent::setProjectFile (const juce::File& file)
{
    if (file == projectFile)
        return;
    projectFile = file;
    rebuildTree(); // the "Project folder" entry follows the project
}

// round 190kx: the loop analysis' worker, made on first use
loopinfo::Shared& BrowserComponent::loopInfoShared()
{
    if (loopInfo == nullptr)
    {
        loopInfo = loopinfo::Shared::get (engine);
        loopInfo->addChangeListener (this);
    }
    return *loopInfo;
}

bool BrowserComponent::showsLoopRowInfo() const { return ! liveLayout && layoutstyle::current() == layoutstyle::Style::simple; }

void BrowserComponent::loopInfoChanged()
{
    tree->refreshLoopTexts();
    // the Loop Browser lists again with the new tempos and keys (at most twice a second: its rows are rebuilt)
    if (loopsShown && ! std::exchange (loopFilterAgainAsked, true))
        juce::Timer::callAfterDelay (500, [safe = juce::Component::SafePointer<BrowserComponent> (this)] {
            if (safe == nullptr)
                return;
            safe->loopFilterAgainAsked = false;
            if (safe->loopsShown)
                safe->applyLoopFilter();
        });
}

void BrowserComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (loopInfo != nullptr && source == loopInfo.get())
    {
        loopInfoChanged();
        return;
    }
    if (source == &loopLengthsRead())
    {
        loopLengthsArrived();
        return;
    }
    if (source == &searcher->statusChanged)
    {
        indexStatusChanged();
        return;
    }
    if (source == &similar->statusChanged)
    {
        similarStatusChanged();
        return;
    }
    if (source == &searcher->indexChanged)
    {
        applyFilterToTree();
        if (loopsShown) // (round gaps190kf: the loops are listed again, in place)
        {
            runLoopSearch();
            return;
        }
        // the background check found changed folders: the results shown are searched again, in place
        if (resultsView->isVisible() && shownTab() != projectTab && searchBox.getText().trim().isNotEmpty())
            runSearch (true);
        return;
    }

    // round 190ce: the device the Browser is linked to was deleted (or the project replaced): the link ends
    if (swapSession.active())
    {
        if (! swapSession.stillValid (engine.getSong()))
            endHotSwap();
        else
            swapBar->repaint();
    }
    // the project changed: its sections are listed again a moment later (not on every knob step)
    projectDirty = true;
    if (! isTimerRunning())
        startTimer (300);
}

void BrowserComponent::addUserFolder()
{
    chooser = lynx::ui::makeFileChooser (tr ("Add a folder to the Browser"), juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc) {
        const auto folder = fc.getResult();
        if (! folder.isDirectory())
            return;

        auto lines = juce::StringArray::fromLines (userFoldersFile().loadFileAsString());
        lines.removeEmptyStrings();
        lines.addIfNotAlreadyThere (folder.getFullPathName());
        userFoldersFile().replaceWithText (lines.joinIntoString ("\n"));
        setTab (allTab);
        rebuildRoots();
    });
}

int BrowserComponent::addUserFolders (const juce::StringArray& paths)
{
    std::vector<std::string> dropped;
    juce::StringArray names;
    for (const auto& path : paths)
        if (const juce::File folder (path); folder.isDirectory())
            dropped.push_back (folder.getFullPathName().toStdString());
    if (dropped.empty())
        return 0;
    std::vector<std::string> list;
    for (const auto& line : juce::StringArray::fromLines (userFoldersFile().loadFileAsString()))
        list.push_back (line.toStdString());
    const auto added = userfolders::add (list, dropped, ! juce::File::areFileNamesCaseSensitive());
    if (added.empty())
    {
        showNote (dropped.size() == 1 ? tr ("{0} is in the Browser already", { juce::File (juce::String::fromUTF8 (dropped.front().c_str())).getFileName() })
                                      : tr ("These folders are in the Browser already"));
        return 0;
    }
    juce::StringArray lines;
    for (const auto& l : list)
        lines.add (juce::String::fromUTF8 (l.c_str()));
    userFoldersFile().getParentDirectory().createDirectory();
    userFoldersFile().replaceWithText (lines.joinIntoString ("\n"));
    setTab (allTab);
    rebuildRoots();
    for (auto* other : liveBrowsers()) // (the other Browsers list the same folders)
        if (other != this)
            other->reloadUserFolders();
    showNote (added.size() == 1 ? tr ("Added {0} to the Browser", { juce::File (juce::String::fromUTF8 (added.front().c_str())).getFileName() })
                                : tr ("Added {0} folders to the Browser", { juce::String ((int) added.size()) }));
    return (int) added.size();
}

bool BrowserComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    return std::any_of (files.begin(), files.end(), [] (const juce::String& f) { return juce::File (f).isDirectory(); });
}

void BrowserComponent::fileDragEnter (const juce::StringArray&, int, int)
{
    folderDragOver = true;
    repaint();
}

void BrowserComponent::fileDragExit (const juce::StringArray&)
{
    folderDragOver = false;
    repaint();
}

void BrowserComponent::filesDropped (const juce::StringArray& files, int, int)
{
    folderDragOver = false;
    repaint();
    addUserFolders (files);
}

void BrowserComponent::paintOverChildren (juce::Graphics& g)
{
    if (! folderDragOver)
        return;
    g.setColour (theme::colour::accent);
    g.drawRect (getLocalBounds(), 2);
}

void BrowserComponent::removeUserFolder (const juce::File& folder)
{
    auto lines = juce::StringArray::fromLines (userFoldersFile().loadFileAsString());
    lines.removeEmptyStrings();
    for (int i = lines.size(); --i >= 0;)
        if (juce::File (lines[i].trim()) == folder)
            lines.remove (i);
    userFoldersFile().replaceWithText (lines.joinIntoString ("\n"));
    if (view.hidden.erase (folder.getFullPathName().toStdString()) > 0) // (a hidden mark of a folder no longer listed)
        saveView();
    rebuildRoots();
}

BrowserComponent::~BrowserComponent()
{
    std::erase (liveBrowsers(), this);
    engine.removeChangeListener (this);
    searcher->statusChanged.removeChangeListener (this);
    searcher->indexChanged.removeChangeListener (this);
    loopLengthsRead().removeChangeListener (this);
    searcher->service->cancelSearch();
    similar->statusChanged.removeChangeListener (this);
    if (similarId != 0)
        similar->service->cancel();
    if (loopInfo != nullptr)
        loopInfo->removeChangeListener (this);
    focusWatcher.reset();
    engine.previewFile ({});
}

void BrowserComponent::waitForListing (int)
{
    // the tree lists folders synchronously when they open
}

void BrowserComponent::resized()
{
    auto area = getLocalBounds().reduced (4);
    if (swapBar->isVisible()) // round 190ce: the hot-swap bar over the tabs
    {
        swapBar->setBounds (area.removeFromTop (HotSwapBar::height));
        area.removeFromTop (3);
    }
    // round 190iz: FL's six icon tabs (Pattern / Simple layouts); the Clip-launch layout has Live's sidebar instead
    tabs->setVisible (! liveLayout);
    if (! liveLayout)
    {
        tabs->setBounds (area.removeFromTop (TabBar::preferredHeight));
        area.removeFromTop (2);
    }
    {
        auto row = area.removeFromTop (24);
        viewButton.setBounds (row.removeFromRight (22));
        row.removeFromRight (4);
        tagsButton->setBounds (row.removeFromRight (TagsButton::width)); // (FL's TAGS button in the search row)
        row.removeFromRight (4);
        if (loopsButton->isVisible()) // (round gaps190kf: the Sample browser window's LOOPS)
        {
            loopsButton->setBounds (row.removeFromRight (LoopsButton::width));
            row.removeFromRight (4);
        }
        searchBox.setBounds (row);
    }
    // indexing progress / what the search found (round 162)
    statusArea = statusLine.isNotEmpty() ? area.removeFromTop (16).withTrimmedLeft (2) : juce::Rectangle<int>();
    area.removeFromTop (4);
    strip->setBounds (getLocalBounds().removeFromBottom (juce::jmin (64, getHeight() / 4)));
    area.setBottom (strip->getY() - 2);
    // FL's details box (date, size / version, folder) just above the preview strip, while something is selected
    details->setVisible (details->hasFile() && area.getHeight() > 120);
    if (details->isVisible())
        details->setBounds (area.removeFromBottom (DetailsBox::preferredHeight).withX (0).withWidth (getWidth()));

    // Live's two panes: the sidebar (about a third, 84-120 px) beside the content, whose top holds the tag chips and
    // the "Name" column title
    sidebarView->setVisible (liveLayout);
    if (liveLayout)
    {
        const auto sideWidth = juce::jlimit (84, 120, area.getWidth() / 3);
        sidebarView->setBounds (area.removeFromLeft (sideWidth));
        sidebar->setSize (sideWidth - (sidebar->getHeight() > sidebarView->getHeight() ? 8 : 0), sidebar->getHeight());
        area.removeFromLeft (1);
    }
    tagsPanel->setVisible (tagsPanelShown);
    if (tagsPanelShown)
        tagsPanel->setBounds (area.removeFromTop (tagsPanel->heightFor (area.getWidth())));
    loopPanel->setVisible (loopsShown);
    if (loopsShown) // (round gaps190kf: the grid takes at most about half the height; its rows scroll past that)
        loopPanel->setBounds (area.removeFromTop (loopPanel->heightFor (area.getWidth(), juce::jmax (140, area.getHeight() / 2 + 40))));
    nameHeader = liveLayout ? area.removeFromTop (18) : juce::Rectangle<int>();
    treeView->setBounds (area);
    resultsView->setBounds (area);

    for (auto* list : { tree.get(), results.get() })
    {
        list->setLiveRows (liveLayout);
        list->setSize (area.getWidth() - 10, list->getHeight());
        list->updateSize();
    }
}

void BrowserComponent::refreshPreviewVolume()
{
    previewVolume.setValue (engine.getPreviewVolume(), juce::dontSendNotification);
    const auto text = tr ("Sample preview volume: {0}", { ui::utilityVolumeText (engine.getPreviewVolume()) });
    previewVolume.setTooltip (text);
    ui::setHint (previewVolume, text + "  " + tr ("[LMB] drag  [Wheel] step  [2xLMB] 100%  [RMB] type a value"), true);
}

void BrowserComponent::paint (juce::Graphics& g)
{
    const uitrace::Scope trace ("BrowserComponent paint", true, g.getClipBounds());
    g.fillAll (theme::colour::bg2);
    if (auto header = nameHeader; ! header.isEmpty()) // round 190iz: Live's list title ("Name")
    {
        g.setColour (theme::colour::bg3);
        g.fillRect (header);
        g.setColour (theme::colour::border);
        g.fillRect (header.removeFromBottom (1));
        g.setFont (theme::font (theme::type::caption, true));
        g.setColour (theme::colour::textSecondary);
        g.drawText (tr ("Name"), header.withTrimmedLeft (8), juce::Justification::centredLeft, false);
    }
    if (! statusArea.isEmpty())
    {
        g.setFont (theme::font (theme::type::caption));
        g.setColour (theme::colour::textDisabled);
        g.drawText (statusLine, statusArea, juce::Justification::centredLeft, true);
    }
}

void BrowserComponent::showPreviewForSnapshot (const juce::File& file)
{
    strip->show (file, false);
}

void BrowserComponent::fileSelected (const juce::File& file, bool preview)
{
    // round 190iz: what was asked for this preview (Shift+Click / Alt+Click / Play), then plain again
    const auto startFraction = std::exchange (previewStartAsked, 0.0);
    const auto full = std::exchange (previewFullAsked, false);
    preview = previewWanted (preview); // (round 190ce: the automatic preview can be switched off)
    if (details != nullptr && file != juce::File())
    {
        const auto wasVisible = details->hasFile();
        details->show (file);
        if (! wasVisible)
            resized();
    }

    if (preview && file.hasFileExtension ("sf2;sf3;sfz") && file.existsAsFile())
    {
        int bank = 0, program = 0;
        if (firstSoundFontPreset (file.getFullPathName().toStdString(), bank, program))
            previewSoundFontPreset (engine, file.getFullPathName().toStdString(), bank, program);
        return;
    }
    if (! isAudio (file))
        return;

    if (preview && file.existsAsFile())
    {
        engine.previewFile (file, startFraction, full); // (round 190iz: FL's 5 seconds unless full)
        previewedFile = file;
    }

    strip->show (file, preview, startFraction, full);
    tree->repaint();
    results->repaint();
}

// FL's Browser menus (REA round 143b, [EXEC] FL 2026: right-click on a sample, a folder, a plugin): the file's actions
// first, then a "System" section (open the parent folder, locate the file, delete it) and a "Tags" section (Favorite);
// a folder: Expand all / Collapse all, then System (Open). LYNX keeps its own "Add folder..." and "Refresh".
// Round 163 (user feedback: "this sort of UI has a ton of wasted space"): no "System" / "Tags" titles any more; the
// rarely used system actions sit in one "File" sub-menu, Starred stays one click away.
void BrowserComponent::addSystemItems (juce::PopupMenu& menu, const juce::File& file)
{
    menu.addSeparator();
    menu.addItem (tr ("Starred"), true, isStarred (file), [this, file] { setStarred (file, ! isStarred (file)); });
    addCollectionItems (menu, file.getFullPathName().toStdString());
    if (resultsView->isVisible() && shownTab() != projectTab) // round 190ce: from a search result to its place in the folders
        menu.addItem (tr ("Show in its folder"), [safe = juce::Component::SafePointer<BrowserComponent> (this), file] {
            if (safe == nullptr)
                return;
            safe->searchBox.setText ({}, false);
            safe->runSearch();
            if (safe->liveLayout) // (round 190iz: Live's All shows every place)
                safe->showLiveEntry ({});
            else
                safe->setTab (allTab);
            if (const auto row = safe->tree->revealFile (file); row >= 0)
                safe->tree->select (row, false);
            else
                safe->showNote (tr ("This file is not in a folder the Browser shows"));
        });
    juce::PopupMenu system;
    system.addItem (tr ("Open parent folder"), [file] { file.getParentDirectory().startAsProcess(); });
    system.addItem (tr ("Locate file"), [file] { file.revealToUser(); });
    system.addItem (tr ("Copy the file path"), [file] { juce::SystemClipboard::copyTextToClipboard (file.getFullPathName()); });
    system.addSeparator();
    system.addItem (tr ("Delete file..."), file.existsAsFile(), false, [this, file] { confirmDeleteFile (file); });
    menu.addSubMenu (tr ("File"), system);
}

void BrowserComponent::showNote (const juce::String& text)
{
    const auto relayout = text.isEmpty() != statusLine.isEmpty();
    statusLine = text;
    if (relayout)
        resized();
    repaint();
    // it goes away by itself (unless something else wrote there meanwhile)
    juce::Timer::callAfterDelay (5000, [safe = juce::Component::SafePointer<BrowserComponent> (this), text] {
        if (safe != nullptr && safe->statusLine == text)
            safe->showNote ({});
    });
}

void BrowserComponent::confirmDeleteFile (const juce::File& file)
{
    juce::Component::SafePointer<BrowserComponent> safe (this);
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, tr ("Delete file"),
                                        tr ("Move \"{0}\" to the recycle bin?", { file.getFileName() }), tr ("Delete"), tr ("Cancel"), nullptr,
                                        juce::ModalCallbackFunction::create ([safe, file] (int result) {
                                            if (result == 0 || safe == nullptr)
                                                return;
                                            if (! file.moveToTrash())
                                                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Delete file"),
                                                                                        tr ("The file could not be deleted."));
                                            safe->rebuildRoots();
                                        }));
}

void BrowserComponent::showFileMenu (const juce::File& file)
{
    buildFileMenu (file).showMenuAsync (juce::PopupMenu::Options().withMousePosition()); // (round 190iz: built apart for --shots)
}

juce::PopupMenu BrowserComponent::buildFileMenu (const juce::File& file)
{
    juce::PopupMenu menu;
    menu.addSectionHeader (file.getFileName());

    if (isAudio (file))
    {
        // FL's first item (REA round 143 B3; round 190iz: FL's own words and its focused-plugin rule, core/BrowserSend.h)
        menu.addItem (tr ("Send to selected channel or focused plugin"), [this, file] { sendToSelected (file); });
        menu.addItem (tr ("Open in a new channel"), [this, file] { openFile (file); });
        menu.addSeparator();
        menu.addItem (tr ("Send to the Playlist as an audio clip"), onSendToPlaylist != nullptr, false, [this, file] { onSendToPlaylist (file); });
        menu.addSeparator();
        menu.addItem (tr ("Edit in the audio editor..."), [this, file] { openAudioEditor (engine, file); });
        // FL's Sounds tab menu: "Open/Edit in...", then "Find similar samples" (FL asks FL Cloud; LYNX looks through
        // the Browser's own folders, round 190as)
        if (! file.hasFileExtension ("speech"))
            menu.addItem (tr ("Find similar samples"), [this, file] { findSimilar (file); });
    }
    else if (file.hasFileExtension ("lynx;flp;als;flm"))
        menu.addItem (tr ("Open project"), onOpenProject != nullptr, false, [this, file] { onOpenProject (file); });
    else if (file.hasFileExtension ("mid;midi"))
        menu.addItem (tr ("Open MIDI file"), onOpenProject != nullptr, false, [this, file] { onOpenProject (file); });
    else if (file.hasFileExtension ("fst;gmsynth") || genpresetui::isPresetFile (file))
    {
        if (pluginpresetui::isFlPresetOfPlugin (file)) // round 190iz: an FL preset of a hosted plugin
            menu.addItem (tr ("Send to selected channel or focused plugin"), [this, file] { sendToSelected (file); });
        menu.addItem (tr ("Open in a new channel"), [this, file] { openFile (file); });
    }
    else if (pluginpresetui::isPresetFile (file)) // round 190iz: a hosted plugin's preset (.lynxpreset, .fxp, .vstpreset)
        menu.addItem (tr ("Send to selected channel or focused plugin"), [this, file] { sendToSelected (file); });
    else if (file.hasFileExtension ("sf2;sf3;sfz"))
    {
        const auto selected = engine.getSelectedChannel();
        const auto& song = engine.getSong();
        const auto selectedIsFont = selected >= 0 && selected < (int) song.channels.size()
                                    && song.channels[(std::size_t) selected].instrument == InstrumentKind::soundfont;
        menu.addItem (tr ("Open in a new channel"), [this, file] { openSoundFontFile (engine, file); });
        menu.addItem (tr ("Send to the selected channel"), selectedIsFont, false, [this, file, selected] { openSoundFontFile (engine, file, selected); });
    }
    else if (isSamplerInstrumentFile (file))
    {
        const auto selected = engine.getSelectedChannel();
        const auto& song = engine.getSong();
        const auto selectedIsDw = selected >= 0 && selected < (int) song.channels.size()
                                  && song.channels[(std::size_t) selected].instrument == InstrumentKind::directwave;
        menu.addItem (tr ("Open in a new channel"), [this, file] { openSamplerInstrumentFile (engine, file); });
        menu.addItem (tr ("Send to the selected channel"), selectedIsDw, false, [this, file, selected] { openSamplerInstrumentFile (engine, file, selected); });
    }

    addSystemItems (menu, file);
    menu.addItem (tr ("Collapse all folders"), [this] { tree->collapseAll(); });
    juce::PopupMenu browserMenu;
    addViewItems (browserMenu);
    menu.addSubMenu (tr ("Browser"), browserMenu);
    return menu;
}

void BrowserComponent::showFolderMenu (const juce::File& folder, std::function<void()> expandAll)
{
    // compact (round 163): the folder's name as the only title, its actions, then the Browser's own options
    juce::PopupMenu menu;
    menu.addSectionHeader (folder.getFileName());
    menu.addItem (tr ("Expand all"), expandAll != nullptr, false, [expandAll] { expandAll(); });
    menu.addItem (tr ("Collapse all"), [this] { tree->collapseAll(); });
    menu.addItem (tr ("Starred"), folder.isDirectory(), isStarred (folder), [this, folder] { setStarred (folder, ! isStarred (folder)); });
    if (folder.isDirectory())
        addCollectionItems (menu, folder.getFullPathName().toStdString());
    menu.addItem (tr ("Open in the file manager"), [folder] { folder.startAsProcess(); });
    menu.addItem (tr ("Copy the file path"), [folder] { juce::SystemClipboard::copyTextToClipboard (folder.getFullPathName()); });
    menu.addSeparator();
    if (const auto id = rootId (folder); id.isNotEmpty() && shownTab() == allTab)
    {
        addHideItem (menu, id, allRootNames[allRoots.indexOf (folder)]);
        if (allRootKinds[(std::size_t) allRoots.indexOf (folder)] == browserview::RootKind::user)
            menu.addItem (tr ("Remove this folder from the Browsers"), [safe = juce::Component::SafePointer<BrowserComponent> (this), folder] {
                if (safe != nullptr)
                    safe->removeUserFolder (folder);
            });
        if (const auto merged = rootAlso.find (folder.getFullPathName()); merged != rootAlso.end())
        {
            // a LYNX extra (FL has no exclusion; round 178a): the folders this root merges, each one ticked in or out;
            // the root's own folder stays (Hide... hides the root)
            juce::PopupMenu folders;
            folders.addItem (folder.getFullPathName(), false, true, nullptr);
            for (const auto& f : merged->second)
            {
                const auto path = f.getFullPathName().toStdString();
                folders.addItem (f.getFullPathName(), true, view.excluded.count (path) == 0,
                                 [safe = juce::Component::SafePointer<BrowserComponent> (this), path] {
                                     if (safe == nullptr)
                                         return;
                                     if (safe->view.excluded.erase (path) == 0)
                                         safe->view.excluded.insert (path);
                                     safe->saveView();
                                     safe->rebuildRoots();
                                     safe->searcher->service->refresh (toPaths (safe->withMerged (safe->allRoots)));
                                     if (safe->searchBox.getText().trim().isNotEmpty())
                                         safe->runSearch();
                                 });
            }
            menu.addSubMenu (tr ("Folders"), folders);
        }
    }
    juce::PopupMenu browserMenu;
    addViewItems (browserMenu);
    browserMenu.addSeparator();
    browserMenu.addItem (tr ("Refresh"), [this] { rebuildRoots(); });
    browserMenu.addItem (tr ("Refresh search index"), [this] { refreshSearchIndex (false); });
    menu.addSubMenu (tr ("Browser"), browserMenu);
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

BrowserComponent::FileTree& BrowserComponent::activeList() const
{
    return resultsView->isVisible() ? *results : *tree;
}

// --- search ------------------------------------------------------------------------------------------

void BrowserComponent::focusSearch()
{
    searchBox.grabKeyboardFocus();
    searchBox.selectAll();
}

juce::Array<juce::File> BrowserComponent::searchRoots() const
{
    // the top-level folder holding the selection; nothing selected there: every folder
    if (const auto root = tree->selectedRoot(); root.isDirectory())
        return withMerged ({ root });
    if (liveLayout && liveSelection.category == browserplaces::Category::collection) // round 190iz: Live's collection
    {
        juce::Array<juce::File> folders;
        for (const auto& item : collectionStore().itemsOf (liveSelection.collection))
            if (const juce::File f (juce::String::fromUTF8 (item.c_str())); ! collections::isPluginItem (item) && f.isDirectory())
                folders.addIfNotAlreadyThere (f);
        return folders;
    }
    if (liveLayout || tab == presetsTab || tab == soundsTab) // round 190iz: the places the tab / Live entry lists
    {
        juce::Array<juce::File> shown;
        for (int i = 0; i < allRoots.size(); ++i)
            if (showsRoot (i))
                shown.add (allRoots[i]);
        if (liveLayout && liveSelection.category == browserplaces::Category::templates)
            shown.add (userTemplatesFolder());
        return withMerged (shown);
    }
    if (tab == starredTab) // the folders of the collections shown
    {
        juce::Array<juce::File> folders;
        for (const auto c : collectionStore().shown())
            for (const auto& item : collectionStore().itemsOf (c))
                if (const juce::File f (juce::String::fromUTF8 (item.c_str())); ! collections::isPluginItem (item) && f.isDirectory())
                    folders.addIfNotAlreadyThere (f);
        return folders;
    }
    return withMerged (roots);
}

juce::Array<juce::File> BrowserComponent::mergedInto (const juce::File& root) const
{
    const auto it = rootAlso.find (root.getFullPathName());
    if (it == rootAlso.end())
        return {};
    auto folders = it->second; // (less the folders this view leaves out: a hidden "FL MIDI" before round 178a)
    folders.removeIf ([this] (const juce::File& f) { return view.excluded.count (f.getFullPathName().toStdString()) > 0; });
    return folders;
}

juce::Array<juce::File> BrowserComponent::withMerged (const juce::Array<juce::File>& folders) const
{
    auto out = folders;
    for (const auto& f : folders)
        out.addArray (mergedInto (f));
    return out;
}

void BrowserComponent::setSearchForSnapshot (const juce::String& text)
{
    // synchronous: the snapshot is drawn before any message is dispatched
    searchBox.setText (text, juce::dontSendNotification);
    searcher->service->cancelSearch();
    resultsView->setVisible (text.isNotEmpty());
    treeView->setVisible (text.isEmpty());

    if (text.isEmpty() || roots.isEmpty())
        return;

    const juce::Array<juce::File> scope { roots[0] }; // the FL samples: the same results on every machine with FL
    auto answer = std::make_shared<std::pair<std::mutex, search::SearchResult>>();
    searcher->service->search (text.toStdString(), toPaths (scope), (std::size_t) maxResults, [answer] (const search::SearchResult& r) {
        const std::lock_guard<std::mutex> guard (answer->first);
        answer->second = r;
    });
    searcher->service->waitIdle (120000);
    auto found = collectionMatches (text); // (round 190ce: as searchAnswered, the collections' names first)
    {
        const std::lock_guard<std::mutex> guard (answer->first);
        for (const auto& path : answer->second.paths)
            found.addIfNotAlreadyThere (juce::File (juce::String::fromUTF8 (path.c_str())));
    }
    results->setFiles (found, scope, found.isEmpty() ? tr ("Nothing found for \"{0}\".", { text }) : juce::String());
    if (! found.isEmpty())
        results->select (0, false);
}

void BrowserComponent::selectFirstSampleForSnapshot()
{
    if (const auto row = tree->firstAudioRow(); row >= 0)
    {
        tree->select (row, false);
        strip->show (tree->selectedFile(), false);
        tree->setHovered (row + 1 < tree->numRows() ? row + 1 : -1);
    }
}

void BrowserComponent::showFilterForSnapshot (const juce::File& folder, browserview::ContentFilter filter)
{
    // a folder of the user's (not saved) listed under `filter` once the index knows it (nothing saved either)
    if (! allRoots.contains (folder))
    {
        allRoots.add (folder);
        allRootNames.add (folder.getFileName());
        allRootKinds.push_back (browserview::RootKind::user);
    }
    roots.clear();
    roots.add (folder);
    view.filter = filter;
    view.setShown (browserview::projectRootId, false);
    view.setShown (browserview::pluginsRootId, false);
    searcher->service->refresh (toPaths ({ folder }));
    searcher->service->waitIdle (60000);
    prunedGeneration = 0;
    setTab (allTab);
    rebuildTree();
    tree->expandTop (folder, 3);
    refreshViewButton();
}

void BrowserComponent::resetViewForSnapshot()
{
    viewLoaded = false; // (read again: the snapshot's changes were never saved)
    prunedGeneration = 0;
    rebuildRoots();
}

std::vector<juce::PopupMenu> BrowserComponent::pluginDatabaseMenusForSnapshot()
{
    // a folder of the user's (else Effects / Generators) and the first plugin entry shown, as a right-click opens them
    showPluginDatabaseForSnapshot();
    std::vector<juce::PopupMenu> out;
    out.push_back (tree->menuForSnapshot (tree->firstRowOfKind (false)));
    out.push_back (tree->menuForSnapshot (tree->firstRowOfKind (true)));
    return out;
}

void BrowserComponent::showPluginDatabaseForSnapshot()
{
    openPluginFolders = { tr ("Plugin database"), tr ("Plugin database") + "/" + tr ("Generators"), tr ("Generators") + "/" + tr ("FL engines"),
                          tr ("Plugin database") + "/" + tr ("Effects"), tr ("Effects") + "/" + tr ("FL engines") };
    setTab (allTab);
    rebuildTree();
}

void BrowserComponent::showPluginDatabase (const juce::String& folder, int generators)
{
    const auto db = tr ("Plugin database");
    openPluginFolders.insert (db);
    openPluginFolders.insert (db + "/" + tr ("Generators"));
    openPluginFolders.insert (db + "/" + tr ("Effects"));
    // the Plugin picker's tab clicked (round 171c, FL opens its Browser on that category): that folder of the user's,
    // else the Generators / Effects heading
    const auto kind = generators > 0 ? tr ("Generators") : tr ("Effects");
    if (generators >= 0 && folder.isNotEmpty())
        openPluginFolders.insert (kind + "/" + folder);
    setTab (allTab);
    rebuildTree();
    auto row = generators >= 0 && folder.isNotEmpty() ? tree->rowLabelled (folder) : -1;
    if (row < 0)
        row = tree->rowLabelled (generators >= 0 ? kind : db);
    if (row >= 0)
    {
        tree->select (row, false);
        tree->grabKeyboardFocus();
    }
}

bool BrowserComponent::locateFile (const juce::File& file)
{
    if (searchBox.getText().isNotEmpty())
    {
        searchBox.setText ({}, false);
        runSearch();
    }
    if (liveLayout) // (round 190iz: Live's All shows every place)
        showLiveEntry ({});
    else
        setTab (allTab);
    if (const auto row = tree->revealFile (file); row >= 0)
    {
        tree->select (row, false);
        tree->grabKeyboardFocus();
        return true;
    }
    showNote (tr ("This file is not in a folder the Browser shows"));
    return false;
}

juce::Rectangle<int> BrowserComponent::selectedRowScreenBounds() const
{
    if (tree == nullptr || ! tree->isShowing() || tree->getSelectedRow() < 0)
        return {};
    const auto row = tree->getSelectedRow();
    return tree->localAreaToGlobal (juce::Rectangle<int> (0, row * tree->rowHeight, tree->getWidth(), tree->rowHeight));
}

void BrowserComponent::showScores (const juce::String& subfolder)
{
    // the Piano roll's File › Browse scores (round 168g; FL: FUN_0123a580 opens its browser at the Scores folder): the
    // "Scores" root shown, opened and selected. With a subfolder (the Event editor's File › Browse automations: FL
    // opens Scores\\Automation, the user's when there is one; REA round 178a) that folder's row is selected
    for (int i = 0; i < allRoots.size(); ++i)
        if (i < (int) allRootKinds.size() && allRootKinds[(std::size_t) i] == browserview::RootKind::flMidi)
        {
            const auto folder = allRoots[i];
            if (const auto id = rootId (folder); id.isNotEmpty())
                setRootShown (id, true);
            setTab (allTab);
            rebuildTree();
            tree->expandTop (folder, 1);
            int row = -1;
            if (subfolder.isNotEmpty())
                for (const auto& f : withMerged ({ folder })) // (the merged row is the first folder's that has it)
                    if (row < 0)
                        row = tree->rowOfFile (f.getChildFile (subfolder));
            if (row < 0)
                row = tree->rowLabelled (allRootNames[i]);
            if (row >= 0)
            {
                tree->select (row, false);
                tree->grabKeyboardFocus();
            }
            return;
        }
    showNote (tr ("FL Studio's scores were not found (FL Studio is not installed)"));
}

void BrowserComponent::showFactoryContentForSnapshot (const juce::StringArray& openFolders)
{
    openFactoryFolders.clear();
    for (const auto& f : openFolders)
        openFactoryFolders.insert (f);
    rebuildTree();
}

juce::StringArray BrowserComponent::rowLabelsForCheck() const { return tree->rowLabels(); }

// round 190kx (--loopinfo190kxshot): waits for the loop analysis of what is listed, then fills the rows / Loop Browser
bool BrowserComponent::waitLoopInfoForSnapshot (int timeoutMs)
{
    if (loopInfo == nullptr)
        return true;
    const auto idle = loopInfo->service().waitIdle (timeoutMs);
    tree->refreshLoopTexts();
    if (loopsShown)
        applyLoopFilter();
    return idle;
}

std::size_t BrowserComponent::loopInfoQueuedForCheck() { return loopInfo != nullptr ? loopInfo->service().queued() : 0; }

juce::StringArray BrowserComponent::loopRowTextsForCheck() const
{
    juce::StringArray out;
    for (int i = 0; i < tree->numRows(); ++i)
        if (const auto* n = tree->nodeAt (i); n != nullptr && n->loopText.isNotEmpty())
            out.add (n->file.getFileNameWithoutExtension() + ": " + n->loopText);
    return out;
}
juce::StringArray BrowserComponent::resultLabelsForCheck() const { return results->rowLabels(); }

// FL's General › Undo history › "Show undo history in the browser when undoing changes" (round 190mr, DF-135): the
// Browser's Current project › History opens, its header scrolled into view
void BrowserComponent::showHistory()
{
    setTab (projectTab);
    tree->openProjectSections ({ Category::history });
    tree->scrollRowIntoView (tree->categoryRow (Category::history));
}

bool BrowserComponent::historyShownForCheck() const
{
    return shownTab() == projectTab && tree->categoryOpen (Category::history);
}

bool BrowserComponent::revealProjectPattern (int pattern)
{
    setTab (projectTab);
    return tree->revealPattern (pattern) >= 0;
}

void BrowserComponent::showProjectForSnapshot()
{
    setTab (projectTab);
    if (const auto row = tree->openProjectSections ({ Category::patterns, Category::generators, Category::samples }); row >= 0)
    {
        tree->select (row, false);
        strip->show (tree->selectedFile(), false);
        tree->setHovered (row + 1 < tree->numRows() ? row + 1 : -1);
    }
}

void BrowserComponent::textEditorTextChanged (juce::TextEditor&)
{
    searchPending = true;
    startTimer (250); // search once typing pauses
}

void BrowserComponent::textEditorEscapeKeyPressed (juce::TextEditor&)
{
    // Esc: clears the search and gives the keyboard back (Space plays again)
    searchPending = false;
    searchBox.clear();
    runSearch();
    leaveSearchBox();
}

void BrowserComponent::textEditorReturnKeyPressed (juce::TextEditor&)
{
    // Enter: searches now and leaves the box, so Up / Down step through the results (and Space plays)
    searchPending = false;
    runSearch();
    leaveSearchBox();
}

bool BrowserComponent::isTypingInSearch() const
{
    return searchBox.hasKeyboardFocus (true);
}

void BrowserComponent::leaveSearchBox()
{
    if (! isTypingInSearch())
        return;
    // the window's first ancestor that takes keys (the main window's content: the shortcuts, the transport) gets them
    for (auto* p = getParentComponent(); p != nullptr; p = p->getParentComponent())
        if (p->getWantsKeyboardFocus() && p->isShowing())
        {
            p->grabKeyboardFocus();
            if (! isTypingInSearch())
                return;
        }
    searchBox.giveAwayKeyboardFocus();
}

void BrowserComponent::timerCallback()
{
    stopTimer();

    if (std::exchange (searchPending, false))
        runSearch();

    if (std::exchange (projectDirty, false))
    {
        tree->refreshProject();
        if (shownTab() == projectTab && resultsView->isVisible())
            runSearch();
    }
}

void BrowserComponent::runSearch (bool quiet)
{
    searcher->service->cancelSearch();
    searchId = 0;
    if (similarTo != juce::File())
    {
        // similar samples are listed while the box still says so; any other text is an ordinary search again
        if (shownTab() != projectTab && searchBox.getText() == similarLabel())
        {
            runSimilar (quiet);
            return;
        }
        similarTo = juce::File();
        similarPartial = false;
        if (std::exchange (similarId, 0) != 0)
            similar->service->cancel();
    }
    if (loopsShown) // round gaps190kf: the Loop Browser's list (the box's text narrows it by name)
    {
        if (loopInfos.empty() && ! loopsAnswer)
            runLoopSearch();
        else
            applyLoopFilter();
        return;
    }
    if (tagFilterActive()) // round 190iz: tags picked in the tags panel, or "#tag" in the box
    {
        runTagFilter();
        return;
    }
    const auto query = effectiveQuery(); // (round 190iz: Live's Drums searches when the box is empty)
    resultsView->setVisible (query.isNotEmpty());
    treeView->setVisible (query.isEmpty());

    if (query.isEmpty())
    {
        searchPartial = false;
        indexStatusChanged();
        return;
    }

    if (shownTab() == projectTab) // the project's own items, at once
    {
        auto found = FileTree::projectMatches (engine.getSong(), query);
        const auto none = found.empty();
        results->setNodes (std::move (found), none ? tr ("Nothing found for \"{0}\".", { query }) : juce::String());
        return;
    }

    const auto scope = searchRoots();
    if (scope.isEmpty())
    {
        // round 190ce: no folder to search, but the collections are still searched by their names
        const auto tagged = collectionMatches (query);
        results->setFiles (tagged, {}, tagged.isEmpty() ? tr ("Nothing found for \"{0}\".", { query }) : juce::String());
        return;
    }

    if (! quiet)
    {
        results->setFiles ({}, scope, tr ("Searching {0}...", { scope.size() == 1 ? scope[0].getFullPathName() : tr ("every folder") }));
        resultsView->setViewPosition (0, 0);
    }

    // the worker answers on its thread: the answer comes to the message thread (and is dropped if this Browser went)
    searchId = searcher->service->search (query.toStdString(), toPaths (scope), (std::size_t) maxResults,
                                          [safe = juce::Component::SafePointer<BrowserComponent> (this), query, scope] (const search::SearchResult& r) {
                                              juce::MessageManager::callAsync ([safe, query, scope, r] {
                                                  if (safe != nullptr)
                                                      safe->searchAnswered (r, query, scope);
                                              });
                                          });
}

juce::Array<juce::File> BrowserComponent::collectionMatches (const juce::String& query) const
{
    std::array<std::string, collections::count> names;
    for (int c = 0; c < collections::count; ++c)
        names[(std::size_t) c] = collectionName (c).toStdString();
    juce::Array<juce::File> out;
    for (const auto& item : collectionStore().search (query.toStdString(), &names))
        if (! collections::isPluginItem (item))
            if (const juce::File f (juce::String::fromUTF8 (item.c_str())); f.existsAsFile() && (activeFilter() == browserview::ContentFilter::all || lists (f)))
                out.add (f);
    return out;
}

void BrowserComponent::searchAnswered (const search::SearchResult& r, const juce::String& query, const juce::Array<juce::File>& scope)
{
    if (r.id != searchId || effectiveQuery() != query)
        return;

    // round 190ce: first the files found through the name of a collection they are in ("starred kick"), then the
    // files found by their own names
    auto found = collectionMatches (query);
    for (const auto& path : r.paths)
        if (juce::File f (juce::String::fromUTF8 (path.c_str())); activeFilter() == browserview::ContentFilter::all || lists (f))
            found.addIfNotAlreadyThere (f);

    // (partial answers and re-searches keep the selection and the scroll position)
    const auto selected = results->selectedFile();
    const auto scroll = resultsView->getViewPosition();
    results->setFiles (found, scope,
                       found.isEmpty() ? (r.partial ? tr ("Indexing the folders... nothing found for \"{0}\" yet.", { query })
                                                    : tr ("Nothing found for \"{0}\".", { query }))
                                       : juce::String());
    if (selected != juce::File())
        if (const auto row = found.indexOf (selected); row >= 0)
            results->select (row, false);
    resultsView->setViewPosition (scroll);

    searchPartial = r.partial;
    if (! r.partial && ! found.isEmpty())
        // FL's hint bar after a search: "Found 51 files" (REA round 134)
        ui::postHint ({ r.truncated ? tr ("Found more than {0} files", { juce::String (found.size()) })
                                    : found.size() == 1 ? tr ("Found 1 file") : tr ("Found {0} files", { juce::String (found.size()) }),
                        {}, false, false },
                      4000);

    // the line under the search box
    juce::String line;
    if (r.partial)
        line = tr ("Indexing... {0} found so far", { juce::String (found.size()) });
    else if (r.truncated)
        line = tr ("Showing the first {0} results.", { juce::String (maxResults) });
    else
        line = found.size() == 1 ? tr ("Found 1 file") : tr ("Found {0} files", { juce::String (found.size()) });
    const auto st = searcher->service->status();
    if (st.busy && ! r.partial)
        line << "  ·  " << tr ("checking the index...");
    if (line != statusLine)
    {
        const auto relayout = line.isEmpty() != statusLine.isEmpty();
        statusLine = line;
        if (relayout)
            resized();
        repaint (statusArea);
    }
}

void BrowserComponent::indexStatusChanged()
{
    applyFilterToTree(); // the index changed: folders found empty under the content filter go (round 164)
    if (similarTo != juce::File() && resultsView->isVisible())
        return; // similar samples are listed: their own line stays
    const auto st = searcher->service->status();
    juce::String line;
    if (resultsView->isVisible() && shownTab() != projectTab && searchBox.getText().trim().isNotEmpty())
    {
        // a search is shown: its line stays; while the folders are indexed, the progress is added
        line = statusLine.upToFirstOccurrenceOf ("  ·  ", false, false);
        if (st.busy)
            line << "  ·  " << (searchPartial ? tr ("{0} folders indexed", { juce::String ((int) st.listed) }) : tr ("checking the index..."));
    }
    else if (st.busy && (st.rebuilding || st.listed > 0))
        line = tr ("Indexing the Browser's folders for search: {0} folders...", { juce::String ((int) st.listed) });

    if (line != statusLine)
    {
        const auto relayout = line.isEmpty() != statusLine.isEmpty();
        statusLine = line;
        if (relayout)
            resized();
        repaint();
    }
}

// --- "Find similar samples" (round 190as) ------------------------------------------------------------------------

juce::String BrowserComponent::similarLabel() const
{
    return tr ("Similar to {0}", { similarTo.getFileNameWithoutExtension() });
}

juce::Array<juce::File> BrowserComponent::similarScope() const
{
    return withMerged (roots); // every folder this Browser shows, wherever the sample itself is
}

void BrowserComponent::findSimilar (const juce::File& file)
{
    if (! file.existsAsFile())
        return;
    if (shownTab() == projectTab)
        setTab (allTab);
    similarTo = file;
    searchPending = false;
    searchBox.setText (similarLabel(), juce::dontSendNotification);
    // the first use starts the analysis of the Browser's folders (the clicked file is analysed first, at once)
    if (! allRoots.isEmpty() && ! std::exchange (similar->passAsked, true))
        similar->service->refresh (toPaths (withMerged (allRoots)));
    runSearch();
}

void BrowserComponent::findSimilarSamples (const juce::File& file, std::function<void (juce::Array<juce::File>)> done)
{
    BrowserComponent* main = nullptr;
    for (auto* b : liveBrowsers())
        if (b->role == Role::main)
            main = b;
    if (main == nullptr || main->similar == nullptr || ! file.existsAsFile())
    {
        if (done)
            juce::MessageManager::callAsync ([done] { done ({}); });
        return;
    }
    if (! main->allRoots.isEmpty() && ! std::exchange (main->similar->passAsked, true))
        main->similar->service->refresh (toPaths (main->withMerged (main->allRoots)));
    main->similar->service->findSimilar (file.getFullPathName().toStdString(), toPaths (main->similarScope()), (std::size_t) maxSimilar,
                                         [done] (const similarity::Result& r) {
                                             juce::Array<juce::File> found;
                                             for (const auto& m : r.matches)
                                                 found.add (juce::File (juce::String::fromUTF8 (m.path.c_str())));
                                             juce::MessageManager::callAsync ([done, found] {
                                                 if (done)
                                                     done (found);
                                             });
                                         });
}

void BrowserComponent::runSimilar (bool quiet)
{
    resultsView->setVisible (true);
    treeView->setVisible (false);
    const auto scope = similarScope();
    if (! quiet)
    {
        results->setFiles ({}, scope, tr ("Listening to \"{0}\"...", { similarTo.getFileName() }));
        resultsView->setViewPosition (0, 0);
    }
    similarAskedAt = juce::Time::getMillisecondCounter();
    // the worker answers on its thread: the answer comes to the message thread (and is dropped if this Browser went)
    similarId = similar->service->findSimilar (similarTo.getFullPathName().toStdString(), toPaths (scope), (std::size_t) maxSimilar,
                                               [safe = juce::Component::SafePointer<BrowserComponent> (this)] (const similarity::Result& r) {
                                                   juce::MessageManager::callAsync ([safe, r] {
                                                       if (safe != nullptr)
                                                           safe->similarAnswered (r);
                                                   });
                                               });
}

void BrowserComponent::similarAnswered (const similarity::Result& r)
{
    if (r.id != similarId || similarTo == juce::File() || r.query != similarTo.getFullPathName().toStdString())
        return;

    juce::Array<juce::File> found;
    juce::StringArray details;
    for (const auto& m : r.matches)
        if (juce::File f (juce::String::fromUTF8 (m.path.c_str())); activeFilter() == browserview::ContentFilter::all || lists (f))
        {
            found.add (f);
            details.add (juce::String (similarity::percent (m.distance)) + "%");
        }

    const auto name = similarTo.getFileName();
    // (partial answers keep the selection and the scroll position)
    const auto selected = results->selectedFile();
    const auto scroll = resultsView->getViewPosition();
    results->setFiles (found, similarScope(),
                       r.failed ? tr ("\"{0}\" can't be compared: it is silent or can't be read.", { name })
                       : found.isEmpty() ? (r.partial ? tr ("Listening to the Browser's samples... nothing like \"{0}\" yet.", { name })
                                                      : tr ("No other samples to compare \"{0}\" with.", { name }))
                                         : juce::String(),
                       details);
    if (selected != juce::File())
        if (const auto row = found.indexOf (selected); row >= 0)
            results->select (row, false);
    resultsView->setViewPosition (scroll);

    similarPartial = r.partial && ! r.failed;
    juce::String line;
    if (! r.failed)
    {
        line = found.size() == 1 ? tr ("1 similar sample, most similar first") : tr ("{0} similar samples, most similar first", { juce::String (found.size()) });
        if (r.partial)
            line << "  ·  " << tr ("{0} samples analysed so far", { juce::String ((int) r.searched) });
        else if (! found.isEmpty())
            ui::postHint ({ tr ("Compared with {0} samples", { juce::String ((int) r.searched) }), {}, false, false }, 4000);
    }
    if (line != statusLine)
    {
        const auto relayout = line.isEmpty() != statusLine.isEmpty();
        statusLine = line;
        if (relayout)
            resized();
        repaint (statusArea);
    }
}

void BrowserComponent::similarStatusChanged()
{
    // the background analysis moved on: a list still marked "so far" is asked again (not more than every 3 s; the
    // last time when the pass ends)
    if (similarTo == juce::File() || ! similarPartial || ! resultsView->isVisible())
        return;
    const auto busy = similar->service->status().busy;
    if (! busy || juce::Time::getMillisecondCounter() - similarAskedAt > 3000)
        runSimilar (true);
}

void BrowserComponent::findSimilarForSnapshot (const juce::File& file, const juce::File& folder)
{
    similar->passAsked = true;
    similar->service->refresh (toPaths ({ folder }));
    similar->service->waitIdle (300000);
    if (shownTab() == projectTab)
        setTab (allTab);
    similarTo = file;
    searchBox.setText (similarLabel(), juce::dontSendNotification);
    resultsView->setVisible (true);
    treeView->setVisible (false);
    auto answer = std::make_shared<std::pair<std::mutex, similarity::Result>>();
    similarId = similar->service->findSimilar (file.getFullPathName().toStdString(), toPaths ({ folder }), (std::size_t) maxSimilar,
                                               [answer] (const similarity::Result& r) {
                                                   const std::lock_guard<std::mutex> hold (answer->first);
                                                   answer->second = r;
                                               });
    similar->service->waitIdle (120000);
    const std::lock_guard<std::mutex> hold (answer->first);
    similarAnswered (answer->second);
}

void BrowserComponent::refreshSearchIndex (bool rebuild)
{
    // the index is shared by every Browser: it covers every folder, hidden ones too
    if (! allRoots.isEmpty())
        searcher->service->refresh (toPaths (withMerged (allRoots)), rebuild);
    ui::postHint ({ rebuild ? tr ("Rebuilding the Browser's search index in the background...")
                            : tr ("Checking the Browser's search index in the background (only changed folders are read again)..."),
                    {}, false, false },
                  4000);
}

juce::String BrowserComponent::searchIndexStatus() const
{
    const auto st = searcher->service->status();
    if (! st.loaded)
        return tr ("Search index: loading...");
    auto text = tr ("Search index: {0} files in {1} folders", { juce::String ((juce::int64) st.files), juce::String ((juce::int64) st.folders) });
    if (st.busy)
        text << " (" << tr ("updating: {0} folders read", { juce::String ((int) st.listed) }) << ")";
    else if (st.checkedAt > 0)
        text << " (" << tr ("checked at {0}", { juce::Time (st.checkedAt).formatted ("%H:%M") }) << ")";
    return text;
}

bool BrowserComponent::renameSelectedPlugin()
{
    return activeList().renameSelectedPlugin();
}

void BrowserComponent::stopPreview()
{
    engine.previewFile ({});
    if (strip != nullptr)
        strip->stop();
    tree->repaint();
    results->repaint();
}

// --- keys --------------------------------------------------------------------------------------------

juce::File BrowserComponent::getSelectedFile() const
{
    return activeList().selectedFile();
}

void BrowserComponent::step (int delta, bool load)
{
    auto& list = activeList();
    const auto count = list.numRows();

    if (count == 0)
        return;

    // the next row in that direction, as in FL: Up / Down walk every row (folders too: Right opens one, Left closes
    // it), previewing each sample; Shift+Up / Down (loading each file into the channel) skips the folders
    auto row = list.getSelectedRow();
    for (int guard = 0; guard < count; ++guard)
    {
        row = row < 0 ? (delta > 0 ? 0 : count - 1) : row + delta;
        if (row < 0 || row >= count)
            return;
        list.select (row, false);
        if (! load || ! list.selectedIsFolder())
            break;
    }

    const auto file = list.selectedFile();

    if (file.existsAsFile())
        fileSelected (file, true);

    if (load && file.existsAsFile() && onLoadIntoChannel)
        onLoadIntoChannel (file);
}

// Round 190lr (DF-96, FL's Browser "Navigation history": Backspace / Shift+Backspace = previous / next location; DL-102,
// Live's Ctrl+[ / Ctrl+]): the folders opened, in order; going back or forward selects and reveals that folder again
void BrowserComponent::noteLocation (const juce::File& folder)
{
    if (walkingHistory || folder == juce::File())
        return;
    if (historyAt >= 0 && historyAt < (int) locations.size() && locations[(std::size_t) historyAt] == folder)
        return;
    locations.resize ((std::size_t) juce::jmax (0, historyAt + 1)); // (a new place drops the forward part)
    locations.push_back (folder);
    if (locations.size() > 64)
        locations.erase (locations.begin());
    historyAt = (int) locations.size() - 1;
}

bool BrowserComponent::walkHistory (int delta)
{
    const auto to = historyAt + delta;
    if (to < 0 || to >= (int) locations.size())
        return false;
    historyAt = to;
    const juce::ScopedValueSetter<bool> walking (walkingHistory, true);
    return locateFile (locations[(std::size_t) to]);
}

// Round 190lr (DF-97, FL's Browser: "Use (F3) to skip to the next matching item"): with a search typed, the next (−1:
// previous) row of the results, round to the first after the last; a file is previewed as Up / Down do
void BrowserComponent::findMatch (int delta)
{
    if (searchBox.getText().trim().isEmpty())
        return;
    auto& list = activeList();
    const auto count = list.numRows();
    if (count == 0)
        return;
    const auto at = list.getSelectedRow();
    const auto row = at < 0 ? (delta > 0 ? 0 : count - 1) : ((at + delta) % count + count) % count;
    list.select (row, false);
    if (const auto file = list.selectedFile(); file.existsAsFile())
        fileSelected (file, true);
}

bool BrowserComponent::typeToJump (const juce::KeyPress& key)
{
    auto& list = activeList();
    const auto row = ui::typeAheadRow (&list, key, list.getSelectedRow(), list.numRows(), [&list] (int r) { return list.rowName (r); });
    if (row < 0)
        return false;
    list.select (row, false);
    return true;
}

void BrowserComponent::openFolder (bool open)
{
    if (! resultsView->isVisible())
        tree->openSelected (open);
}

bool BrowserComponent::previewSelected()
{
    const auto file = getSelectedFile();
    if (! file.existsAsFile())
        return false;
    const juce::ScopedValueSetter<bool> force (forcePreview, true);
    fileSelected (file, true);
    return true;
}

void BrowserComponent::loadSelected()
{
    if (hotSwapLoadSelected()) // round 190ce: while linked, Enter replaces the linked device
        return;
    const auto file = getSelectedFile();

    if (! file.existsAsFile())
        return;

    if (isSamplerInstrumentFile (file))
    {
        // Enter: into the selected DirectWave channel, else a new one (round 190ii)
        openSamplerInstrumentFile (engine, file, engine.getSelectedChannel());
        return;
    }
    if (file.hasFileExtension ("sf2;sf3;sfz"))
    {
        // Enter: into the selected LYNX Soundfont channel, else a new one
        if (! openSoundFontFile (engine, file, engine.getSelectedChannel()))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Load SoundFont"), tr ("This file is not a SoundFont LYNX SHARE can read."));
        return;
    }
    if (! onLoadIntoChannel || ! onLoadIntoChannel (file))
        openFile (file);
}

bool BrowserComponent::isAudioFile (const juce::File& file)
{
    return isAudio (file);
}

bool BrowserComponent::isBrowserDrag (const juce::DragAndDropTarget::SourceDetails& details)
{
    const auto text = details.description.toString();
    return text == dragDescription || text.startsWith (juce::String (dragDescription) + "|");
}

juce::File BrowserComponent::draggedFile (const juce::DragAndDropTarget::SourceDetails& details)
{
    const auto text = details.description.toString();

    if (text.startsWith (juce::String (dragDescription) + "|"))
        return juce::File (text.fromFirstOccurrenceOf ("|", false, false));

    return {};
}

bool BrowserComponent::startPluginDrag (juce::Component& source, std::function<std::optional<ChannelDef>()> generator,
                                        std::function<std::optional<EffectSlot>()> effect, const juce::String& label, const juce::Image& image,
                                        juce::Component* containerOwner)
{
    // (`containerOwner`: the Plugin picker is its own window, outside the main window's drag container)
    // (findParentDragContainerFor looks from the parent up, so the owner itself is tried first)
    auto* container = dynamic_cast<juce::DragAndDropContainer*> (containerOwner);
    if (container == nullptr)
        container = juce::DragAndDropContainer::findParentDragContainerFor (containerOwner != nullptr ? containerOwner : &source);
    if (container == nullptr || (generator == nullptr && effect == nullptr))
        return false;
    const auto isGenerator = generator != nullptr;
    draggedPlugin() = { std::move (generator), std::move (effect) };
    container->startDragging (juce::String (isGenerator ? generatorDragDescription : effectDragDescription) + "|" + label, &source,
                              juce::ScaledImage (image), true);
    return true;
}

bool BrowserComponent::isGeneratorDrag (const juce::DragAndDropTarget::SourceDetails& details)
{
    return details.description.toString().startsWith (juce::String (generatorDragDescription) + "|") && draggedPlugin().generator != nullptr;
}

bool BrowserComponent::isEffectDrag (const juce::DragAndDropTarget::SourceDetails& details)
{
    return details.description.toString().startsWith (juce::String (effectDragDescription) + "|") && draggedPlugin().effect != nullptr;
}

std::optional<ChannelDef> BrowserComponent::draggedGenerator (const juce::DragAndDropTarget::SourceDetails& details)
{
    return isGeneratorDrag (details) ? draggedPlugin().generator() : std::nullopt;
}

std::optional<EffectSlot> BrowserComponent::draggedEffect (const juce::DragAndDropTarget::SourceDetails& details)
{
    return isEffectDrag (details) ? draggedPlugin().effect() : std::nullopt;
}

int BrowserComponent::draggedPattern (const juce::DragAndDropTarget::SourceDetails& details)
{
    const auto text = details.description.toString();
    if (text.startsWith (juce::String (patternDragDescription) + "|"))
        return text.fromFirstOccurrenceOf ("|", false, false).getIntValue();
    return -1;
}

void BrowserComponent::newSampleChannel (const juce::File& file)
{
    const auto path = file.getFullPathName().toStdString();
    const auto name = file.getFileNameWithoutExtension().toStdString();
    engine.editSong ([&] (Song& s) {
        ChannelDef c;
        c.name = name;
        c.instrument = InstrumentKind::sampler;
        c.samplePath = path;
        c.mixerTrack = s.claimFreeInsert();
        s.channels.push_back (c);
    });
    engine.setSelectedChannel ((int) engine.getSong().channels.size() - 1);
}

void BrowserComponent::openFile (const juce::File& file)
{
    // a user preset of one of LYNX's generators (round 190fs): a new channel with that generator and sound
    if (genpresetui::isPresetFile (file))
    {
        juce::String error;
        if (! genpresetui::addChannel (engine, file, error) && error.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Preset"), error);
        return;
    }
    // a hosted plugin's preset (round 162d, issue #8): into the plugin it belongs to
    if (pluginpresetui::isPresetFile (file) && (! file.hasFileExtension ("lynxpreset") || pluginpresetui::readLynxPreset (file).has_value()))
    {
        juce::String message;
        if (! pluginpresetui::applyFromBrowser (engine, file, message) && message.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Plugin preset"), message);
        return;
    }
    if (file.hasFileExtension ("sf2;sf3;sfz"))
    {
        if (! openSoundFontFile (engine, file))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Load SoundFont"), tr ("This file is not a SoundFont LYNX SHARE can read."));
        return;
    }
    if (isSamplerInstrumentFile (file))
    {
        openSamplerInstrumentFile (engine, file); // (round 190ii, issue #149: a new DirectWave channel)
        return;
    }
    if (isAudio (file) && file.hasFileExtension ("speech"))
    {
        // FL's .speech: the Speech properties first, then a new channel with the file it returns (round 168b)
        speechui::editThenLoad (engine, file, [safe = juce::Component::SafePointer<BrowserComponent> (this)] (const juce::File& f) {
            if (safe != nullptr && f.existsAsFile())
            {
                safe->newSampleChannel (f);
                safe->rebuildTree(); // (an edited copy appears in the Speech folder)
            }
        });
        return;
    }
    if (isAudio (file))
    {
        newSampleChannel (file);
        return;
    }

    // FL's mixer preset (REA round 167q) or LYNX's mixer track state: onto the selected mixer tracks
    if (file.hasFileExtension ("fst;lynxtrack") && onOpenMixerPreset && onOpenMixerPreset (file))
        return;

    // preset of an FL synth that LYNX converts (3x Osc, Sytrus, GMS...): new channel with the built-in synth
    if (file.hasFileExtension ("fst;gmsynth"))
    {
        // FL's preset of a third-party plugin (round 190fs): into that plugin (an open window, or a new channel)
        if (pluginpresetui::isFlPresetOfPlugin (file))
        {
            juce::String message;
            if (! pluginpresetui::applyFlPresetFromBrowser (engine, file, message) && message.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Preset"), message);
            return;
        }
        juce::MemoryBlock data;

        if (! file.loadFileAsData (data))
            return;

        const auto* bytes = static_cast<const std::uint8_t*> (data.getData());

        if (auto synth = synthFromFlPreset (std::vector<std::uint8_t> (bytes, bytes + data.getSize())))
        {
            const auto name = file.getFileNameWithoutExtension().toStdString();
            engine.editSong ([&] (Song& s) {
                ChannelDef c;
                c.name = name;
                c.instrument = InstrumentKind::synth;
                c.synth = *synth;
                c.mixerTrack = s.claimFreeInsert();
                s.channels.push_back (c);
            });
            engine.setSelectedChannel ((int) engine.getSong().channels.size() - 1);
        }
        else
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Preset"),
                                                    tr ("LYNX SHARE reads presets from 3x Osc, Sytrus, Harmor, GMS, Harmless, MiniSynth, DX10, Fruit Kick, Ogun and Sakura."));
        return;
    }

    if (file.hasFileExtension ("lynx;flp;als;flm;mid;midi") && onOpenProject)
        onOpenProject (file);
}

// --- Round 190iz: tags, Live's browser, send to selected, prefs ----------------------------------------------------------

browsertags::Store& BrowserComponent::tagStore()
{
    static browsertags::Store store;
    return store;
}

browsertags::Store BrowserComponent::shownTags()
{
    if (layoutstyle::current() != layoutstyle::Style::simple)
        return tagStore();
    return factorycontent::merged (tagStore());
}

void BrowserComponent::layoutStyleChanged()
{
    static bool wasSimple = false; // (only a move into or out of the Simple layout changes what is listed)
    const auto simple = layoutstyle::current() == layoutstyle::Style::simple;
    if (std::exchange (wasSimple, simple) == simple)
        return;
    for (auto* b : liveBrowsers())
    {
        b->rebuildTree();
        if (b->tagsPanelShown)
        {
            b->tagsPanel->refresh();
            b->resized();
        }
        if (b->tagFilterActive())
            b->runSearch (true);
    }
}

juce::File BrowserComponent::tagsFile()
{
    // the user's tag store (FL: one tag database of the user; not in the project)
    return PluginHost::settingsFolder().getChildFile ("browser-tags.txt");
}

void BrowserComponent::loadTags()
{
    static bool loaded = false; // (once per process: every Browser shares the store)
    if (std::exchange (loaded, true))
        return;
    if (const auto file = tagsFile(); file.existsAsFile())
        tagStore() = browsertags::Store::fromText (file.loadFileAsString().toStdString());
}

void BrowserComponent::saveTags()
{
    tagsFile().getParentDirectory().createDirectory();
    tagsFile().replaceWithText (juce::String::fromUTF8 (tagStore().toText().c_str()));
}

void BrowserComponent::tagsChanged()
{
    for (auto* b : liveBrowsers())
    {
        // picked tags that were deleted go
        const auto shown = shownTags();
        std::erase_if (b->picked, [&shown] (const std::string& t) { return ! shown.exists (t); });
        if (b->tagsPanelShown)
        {
            b->tagsPanel->refresh();
            b->resized(); // (the panel's height follows its chips)
        }
        b->tagsButton->repaint();
        if (b->loopsShown) // (round gaps190kf: tags that name a keyword give it)
            b->runLoopSearch();
        else if (b->tagFilterActive() || b->resultsView->isVisible())
            b->runSearch (true);
        b->tree->repaint();
        b->results->repaint();
    }
}

void BrowserComponent::setTagged (const std::string& item, const juce::String& tag, bool on)
{
    if (tagStore().set (item, tag.toStdString(), on))
    {
        saveTags();
        tagsChanged();
    }
}

void BrowserComponent::addTagItems (juce::PopupMenu& menu, const std::string& item)
{
    if (item.empty())
        return;
    juce::Component::SafePointer<BrowserComponent> safe (this);
    // FL's Tags section: Favorite (LYNX's Starred / the collections, above), Tags ▸, Add tag, Edit tags
    juce::PopupMenu tags;
    const auto shown = shownTags(); // (round 190ke: with LYNX content's tags in the Simple layout)
    for (const auto& t : shown.all())
    {
        const auto name = juce::String::fromUTF8 (t.name.c_str());
        tags.addItem (name, true, shown.has (item, t.name), [safe, item, name] {
            if (safe != nullptr)
                safe->setTagged (item, name, ! tagStore().has (item, name.toStdString()));
        });
    }
    if (tags.getNumItems() == 0)
        tags.addItem (tr ("(no tags yet)"), false, false, nullptr);
    menu.addSubMenu (tr ("Tags"), tags);
    menu.addItem (tr ("Add tag..."), [safe, item] {
        if (safe != nullptr)
            safe->askAddTag (item);
    });
    menu.addItem (tr ("Edit tags..."), ! tagStore().names.empty(), false, [safe] {
        if (safe != nullptr)
            safe->showEditTags();
    });
}

void BrowserComponent::askAddTag (const std::string& item)
{
    auto* window = new juce::AlertWindow (tr ("Add tag"), tr ("Type a tag to add to the item. Separate several with commas: Kick,808,C# gives it three tags."),
                                          juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("tags", {});
    window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<BrowserComponent> safe (this);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([window, safe, item] (int result) {
                                 const auto text = window->getTextEditorContents ("tags").toStdString();
                                 if (result != 1 || safe == nullptr)
                                     return;
                                 if (tagStore().add ({ item }, text) > 0)
                                 {
                                     saveTags();
                                     safe->tagsChanged();
                                 }
                             }),
                             true);
}

void BrowserComponent::showEditTags()
{
    // FL's "Edit tags": each tag with its count; pick one to rename or delete it everywhere
    juce::PopupMenu menu;
    menu.addSectionHeader (tr ("Edit tags"));
    juce::Component::SafePointer<BrowserComponent> safe (this);
    for (const auto& t : tagStore().all())
    {
        juce::PopupMenu one;
        const auto tag = t.name;
        one.addItem (tr ("Rename..."), [safe, tag] {
            if (safe == nullptr)
                return;
            auto* window = new juce::AlertWindow (tr ("Rename tag"), tr ("The tag's new name, on every item that has it:"), juce::MessageBoxIconType::NoIcon);
            window->addTextEditor ("name", juce::String::fromUTF8 (tag.c_str()));
            window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
            window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
            window->enterModalState (true, juce::ModalCallbackFunction::create ([window, safe, tag] (int result) {
                                         const auto name = window->getTextEditorContents ("name").toStdString();
                                         if (result == 1 && safe != nullptr && tagStore().rename (tag, name))
                                         {
                                             for (auto& p : safe->picked)
                                                 if (browsertags::Store::key (p) == browsertags::Store::key (tag))
                                                     p = browsertags::cleanTag (name);
                                             saveTags();
                                             safe->tagsChanged();
                                         }
                                     }),
                                     true);
        });
        one.addItem (tr ("Delete tag"), [safe, tag] {
            if (safe == nullptr)
                return;
            tagStore().remove (tag);
            saveTags();
            safe->tagsChanged();
        });
        menu.addSubMenu (juce::String::fromUTF8 (tag.c_str()) + "  (" + juce::String (t.items) + ")", one);
    }
    if (tagStore().names.empty())
        menu.addItem (tr ("(no tags yet)"), false, false, nullptr);
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void BrowserComponent::setTagsPanelShown (bool shown)
{
    if (shown == tagsPanelShown)
        return;
    tagsPanelShown = shown;
    if (shown)
        tagsPanel->refresh();
    if (shown && loopsShown) // (round gaps190kf: one filter panel at a time)
    {
        setLoopsShown (false);
        return;
    }
    resized();
    tagsButton->repaint();
    savePrefs();
}

void BrowserComponent::setPickedTags (std::vector<std::string> tags)
{
    picked = std::move (tags);
    if (layoutstyle::current() == layoutstyle::Style::simple && tagsPanelShown) // (round 190ke: the picked chips come first)
        tagsPanel->refresh();
    else
        tagsPanel->repaint();
    tagsButton->repaint();
    runSearch();
    savePrefs();
}

void BrowserComponent::setTagMatch (browsertags::Match match)
{
    if (match == matchMode)
        return;
    matchMode = match;
    tagsPanel->repaint();
    if (tagFilterActive())
        runSearch();
    savePrefs();
}

bool BrowserComponent::tagFilterActive() const
{
    return ! picked.empty() || ! browsertags::parseQuery (searchBox.getText().trim().toStdString()).tags.empty();
}

void BrowserComponent::runTagFilter()
{
    // FL: the picked tags filter the content (Any / All); Live: "#tag" in the search; the rest of the text must be in
    // the item's name
    const auto query = browsertags::parseQuery (searchBox.getText().trim().toStdString());
    const auto store = shownTags(); // (round 190ke: LYNX content's tags too, in the Simple layout)
    std::vector<std::string> items;
    if (! picked.empty())
        items = store.filter (picked, matchMode);
    if (! query.tags.empty())
    {
        const auto hashed = store.filter (query.tags, browsertags::Match::all);
        if (picked.empty())
            items = hashed;
        else
            std::erase_if (items, [&hashed] (const std::string& i) { return std::find (hashed.begin(), hashed.end(), i) == hashed.end(); });
    }
    std::vector<std::unique_ptr<FileTree::Node>> nodes;
    for (const auto& item : items)
    {
        if (collections::isPluginItem (item))
        {
            if (auto n = FileTree::makePluginItem (*this, collections::pluginKeyOf (item)); n != nullptr && browsertags::nameHasWords (n->label.toStdString(), query.rest))
                nodes.push_back (std::move (n));
            continue;
        }
        const juce::File f (juce::String::fromUTF8 (item.c_str()));
        if (! browsertags::nameHasWords (f.getFileName().toStdString(), query.rest))
            continue;
        if (f.isDirectory())
            nodes.push_back (FileTree::makeFolder (f, f.getFileName(), juce::Colour()));
        else if (f.existsAsFile() && lists (f))
        {
            auto n = std::make_unique<FileTree::Node>();
            n->file = f;
            n->detail = f.getParentDirectory().getFileName(); // (the folder it is in, on the right)
            nodes.push_back (std::move (n));
        }
    }
    std::stable_sort (nodes.begin(), nodes.end(), [] (const auto& a, const auto& b) {
        const auto na = a->label.isNotEmpty() ? a->label : a->file.getFileName();
        const auto nb = b->label.isNotEmpty() ? b->label : b->file.getFileName();
        return na.compareNatural (nb) < 0;
    });
    const auto count = (int) nodes.size();
    resultsView->setVisible (true);
    treeView->setVisible (false);
    results->setNodes (std::move (nodes), count == 0 ? tr ("No item has these tags.") : juce::String());
    resultsView->setViewPosition (0, 0);
    const auto line = count == 1 ? tr ("1 tagged item") : tr ("{0} tagged items", { juce::String (count) });
    if (line != statusLine)
    {
        const auto relayout = statusLine.isEmpty();
        statusLine = line;
        if (relayout)
            resized();
        repaint();
    }
}

void BrowserComponent::setLiveLayout (bool on)
{
    if (on == liveLayout)
        return;
    liveLayout = on;
    if (on)
        sidebar->refresh();
    resized();
    rebuildTree();
    if (searchBox.getText().isNotEmpty() || tagFilterActive())
        runSearch();
}

void BrowserComponent::showLiveEntry (const browserplaces::Entry& entry)
{
    if (! entry.available)
        return;
    if (entry.category == browserplaces::Category::addFolder) // Live's "Add Folder...": a place, not a view
    {
        addUserFolder();
        return;
    }
    liveSelection = entry;
    sidebar->repaint();
    rebuildTree();
    if (searchBox.getText().isNotEmpty() || tagFilterActive())
        runSearch();
    else if (categoryQuery.isEmpty())
    {
        resultsView->setVisible (false);
        treeView->setVisible (true);
    }
    savePrefs();
}

browsersend::Channel BrowserComponent::selectedChannelKind() const
{
    const auto row = engine.getSelectedChannel();
    const auto& song = engine.getSong();
    if (row < 0 || row >= (int) song.channels.size())
        return browsersend::Channel::none;
    const auto kind = song.channels[(std::size_t) row].instrument;
    if (kind == InstrumentKind::sampler || kind == InstrumentKind::kick || kind == InstrumentKind::snare || kind == InstrumentKind::hat
        || kind == InstrumentKind::clap || kind == InstrumentKind::granulizer || kind == InstrumentKind::wavetraveller)
        return browsersend::Channel::playsSample;
    if (kind == InstrumentKind::soundfont)
        return browsersend::Channel::soundFont;
    if (kind == InstrumentKind::plugin)
        return browsersend::Channel::plugin;
    return browsersend::Channel::other;
}

void BrowserComponent::sendToSelected (const juce::File& file)
{
    using namespace browsersend;
    const auto item = isAudio (file) ? Item::sample
                      : file.hasFileExtension ("sf2;sf3;sfz") ? Item::soundFont
                      : pluginpresetui::isFlPresetOfPlugin (file) || pluginpresetui::isPresetFile (file) ? Item::pluginPreset
                      : genpresetui::isPresetFile (file) || file.hasFileExtension ("fst;gmsynth") ? Item::generatorPreset
                                                                                                    : Item::other;
    const auto focused = ! PluginWindows::openPlugins().empty();
    switch (decide (item, selectedChannelKind(), focused))
    {
        case Action::sampleIntoSelected:
            if (onLoadIntoChannel == nullptr || ! onLoadIntoChannel (file))
                openFile (file);
            return;
        case Action::fontIntoSelected:
            openSoundFontFile (engine, file, engine.getSelectedChannel());
            return;
        case Action::presetToFocusedPlugin:
        {
            juce::String message;
            const auto ok = pluginpresetui::isFlPresetOfPlugin (file) ? pluginpresetui::applyFlPresetFromBrowser (engine, file, message)
                                                                      : pluginpresetui::applyFromBrowser (engine, file, message);
            if (! ok && message.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Plugin preset"), message);
            return;
        }
        case Action::sampleNewChannel:
        case Action::fontNewChannel:
        case Action::generatorPresetChannel:
        case Action::none: // (a plugin preset with no plugin to take it: what a double-click does, which says what to open)
            if (item != Item::other)
                openFile (file);
            else
                showNote (tr ("Select a channel or open a plugin window that can take this file"));
            return;
        case Action::replaceSelected:
        case Action::newChannel:
        case Action::effectToInsert: return; // (Plugin database entries: sendPluginToSelected)
    }
}

void BrowserComponent::sendPluginToSelected (const std::function<std::optional<ChannelDef>()>& generator,
                                             const std::function<std::optional<EffectSlot>()>& effect)
{
    using namespace browsersend;
    const auto action = decide (generator ? Item::generator : Item::effect, selectedChannelKind(), ! PluginWindows::openPlugins().empty());
    if (action == Action::replaceSelected && generator)
    {
        // FL's Enter on a generator: it replaces the selected channel's (as hot-swap does, one undo step)
        auto c = generator();
        if (! c)
            return;
        const auto channel = engine.getSelectedChannel();
        const auto target = hotswap::targetOfChannel (engine.getSong(), channel);
        if (! target.active())
            return;
        if (onHotSwapWillReplaceInstrument != nullptr)
            onHotSwapWillReplaceInstrument (channel);
        engine.editSong ([target, c = std::move (*c)] (Song& song) mutable { hotswap::swapInstrument (song, target, std::move (c)); });
        return;
    }
    if (action == Action::newChannel && generator)
    {
        if (auto c = generator(); c && onAddInstrument)
            onAddInstrument (std::move (*c));
        return;
    }
    if (effect)
        if (auto fx = effect(); fx && onAddEffect)
            onAddEffect (std::move (*fx));
}

void BrowserComponent::hoverRowForSnapshot (const juce::String& label)
{
    auto& list = activeList();
    for (int i = 0; i < list.numRows(); ++i)
        if (list.shownName (i) == label)
        {
            list.setHovered (i);
            return;
        }
}

void BrowserComponent::showPreviewPlayedForSnapshot (const juce::File& file, double fromFraction)
{
    strip->show (file, true, fromFraction, false);
}

void BrowserComponent::toggleStarredItem (const std::string& item)
{
    if (! item.empty())
        setInCollection (item, 0, ! collectionStore().has (item, 0));
}

juce::File BrowserComponent::prefsFile() const
{
    return PluginHost::settingsFolder().getChildFile (role == Role::main ? "browser-prefs.txt" : "browser-prefs-samples.txt");
}

void BrowserComponent::loadPrefs()
{
    if (! readPrefs)
        return;
    for (const auto& line : juce::StringArray::fromLines (prefsFile().loadFileAsString()))
    {
        const auto key = line.upToFirstOccurrenceOf ("=", false, false).trim();
        const auto value = line.fromFirstOccurrenceOf ("=", false, false).trim();
        if (key == "tab")
            tab = juce::jlimit (0, tabCount - 1, value.getIntValue());
        else if (key == "fullSamplePreview" && role == Role::main)
            engine.setFullSamplePreview (value == "1");
        else if (key == "tagsPanel")
            tagsPanelShown = value == "1";
        else if (key == "tagMatch")
            matchMode = value == "all" ? browsertags::Match::all : browsertags::Match::any;
        else if (key == "tag" && value.isNotEmpty())
            picked.push_back (value.toStdString());
        else if (key == "liveCategory")
            liveSelection.category = (browserplaces::Category) juce::jlimit (0, (int) browserplaces::Category::collection, value.getIntValue());
        else if (key == "liveCollection")
            liveSelection.collection = value.getIntValue();
        else if (key == "livePath")
            liveSelection.path = value.toStdString();
        else if (key == "loopsPanel") // (round gaps190kf)
            loopsShown = value == "1" && role == Role::samples;
        else if (key == "loopCategories")
            loopCategoryMask = juce::jlimit (1, 7, value.getIntValue());
        else if (key == "loopLayout")
            loopLayoutState = loopkeys::Layout::fromText (value.replaceCharacter ('|', '\n').toStdString());
    }
    {
        const auto shown = shownTags();
        std::erase_if (picked, [&shown] (const std::string& t) { return ! shown.exists (t); });
    }
    if (! browserplaces::libraryAvailable (liveSelection.category) || liveSelection.category == browserplaces::Category::addFolder)
        liveSelection = {};
}

void BrowserComponent::savePrefs() const
{
    if (! readPrefs) // (--shots leave the user's file alone)
        return;
    juce::String text;
    text << "tab=" << tab << "\n"
         << "fullSamplePreview=" << (engine.isFullSamplePreview() ? 1 : 0) << "\n"
         << "tagsPanel=" << (tagsPanelShown ? 1 : 0) << "\n"
         << "tagMatch=" << (matchMode == browsertags::Match::all ? "all" : "any") << "\n"
         << "liveCategory=" << (int) liveSelection.category << "\n"
         << "liveCollection=" << liveSelection.collection << "\n"
         << "livePath=" << juce::String::fromUTF8 (liveSelection.path.c_str()) << "\n"
         << "loopsPanel=" << (loopsShown ? 1 : 0) << "\n"
         << "loopCategories=" << loopCategoryMask << "\n";
    if (loopLayoutState != loopkeys::Layout::defaults())
        text << "loopLayout=" << juce::String::fromUTF8 (loopLayoutState.toText().c_str()).trimEnd().replaceCharacter ('\n', '|') << "\n";
    for (const auto& t : picked)
        text << "tag=" << juce::String::fromUTF8 (t.c_str()) << "\n";
    prefsFile().getParentDirectory().createDirectory();
    prefsFile().replaceWithText (text);
}
#include "ui/LoopBrowserParts.inc" // round gaps190kf
} // namespace lynx
