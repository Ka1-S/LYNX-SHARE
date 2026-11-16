#include "ui/StemExtract.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "core/AudioEdit.h"
#include "core/StemSplit.h"
#include "core/UpdateCheck.h"
#include "platform/Paths.h"
#include "ui/I18n.h"
#include "ui/OnnxRuntime.h"
#include "ui/Theme.h"
#include "ui/WindowOwner.h"

namespace lynx
{
namespace
{
// Fixed revisions and hashes (core/StemSplit.h modelFile): what the user agreed to is what runs.
constexpr const char* modelPage = "https://huggingface.co/adowu/htdemucs-onnx";
constexpr const char* modelPage6 = "https://huggingface.co/StemSplitio/htdemucs-6s-onnx";
constexpr const char* modelPageFine = "https://huggingface.co/StemSplitio/htdemucs-ft-onnx";

// Not the settings folder (round 167x): on Windows that is the roaming %APPDATA%, no place for 166 MB.
juce::File modelFolder()
{
    if (platform::profileOverride() != juce::File())
        return platform::settingsFile ("models"); // (a test profile keeps everything inside it)
   #if JUCE_WINDOWS
    return juce::File::getSpecialLocation (juce::File::windowsLocalAppData).getChildFile ("LYNX").getChildFile ("models");
   #elif JUCE_MAC
    return platform::settingsFile ("models"); // ~/Library/Application Support/LYNX (Caches may be purged)
   #else
    const auto xdg = juce::SystemStats::getEnvironmentVariable ("XDG_DATA_HOME", {});
    const auto data = xdg.startsWithChar ('/') ? juce::File (xdg) : juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".local/share");
    return data.getChildFile ("LYNX").getChildFile ("models");
   #endif
}

juce::File fileOf (const stemsplit::ModelFile& m) { return modelFolder().getChildFile (m.file); }

// (a truncated or damaged copy counts as missing: it is downloaded again, after the same question)
bool installed (const stemsplit::ModelFile& m) { return fileOf (m).getSize() == m.bytes; }

// the options of the last extraction (round 190jr): kept in the settings folder
juce::File optionsFile() { return platform::settingsFile ("stem-options.txt"); }
stemsplit::Options savedOptions() { return stemsplit::optionsFromText (optionsFile().loadFileAsString().trim().toStdString()); }
void saveOptions (const stemsplit::Options& o) { optionsFile().replaceWithText (juce::String (stemsplit::toText (o)) + "\n"); }

juce::String runtimeLoadError()
{
   #if JUCE_MAC
    return tr ("ONNX Runtime could not be loaded. Stem extraction needs macOS 13.4 or newer.");
   #elif JUCE_WINDOWS
    return tr ("ONNX Runtime could not be loaded. Install the Microsoft Visual C++ Redistributable (x64) from https://aka.ms/vs/17/release/vc_redist.x64.exe and try again.");
   #else
    return tr ("ONNX Runtime could not be loaded. Stem extraction needs the system's libstdc++.");
   #endif
}

// ONNX Runtime (ui/OnnxRuntime), with this feature's messages.
struct Runtime : onnx::Runtime
{
    bool load (juce::String& error)
    {
        return onnx::Runtime::load (error, tr ("Stem extraction needs ONNX Runtime ({0}), which comes with LYNX SHARE's installer and zip, but it is missing next to the program. Install LYNX SHARE with the installer, or unpack the whole zip."),
                                    runtimeLoadError());
    }
};

// One loaded model.
class StemModel
{
public:
    ~StemModel()
    {
        if (api != nullptr)
        {
            api->ReleaseRunOptions (runOptions);
            api->ReleaseSession (session);
            api->ReleaseMemoryInfo (memory);
            api->ReleaseEnv (env); // (its threads end here: nothing keeps running after the job)
        }
    }

    bool open (const juce::File& model, juce::String& error, int threads, bool safeMode, int graphOutputs)
    {
        outputs = graphOutputs;
        if (! runtime.load (error))
            return false;
        api = runtime.api;

        OrtSessionOptions* options = nullptr;
        auto ok = check (api->CreateEnv (ORT_LOGGING_LEVEL_ERROR, "LYNX", &env), error)
               && check (api->CreateCpuMemoryInfo (OrtArenaAllocator, OrtMemTypeDefault, &memory), error)
               && check (api->CreateRunOptions (&runOptions), error)
               && check (api->CreateSessionOptions (&options), error)
               // the project may be playing: cores stay free for the audio callback and the UI, and idle workers
               // sleep instead of spinning (ONNX Runtime's default keeps every worker busy between operators)
               && check (api->SetIntraOpNumThreads (options, threads), error)
               // Safe mode (FL's troubleshooting switch, round 190jr): no graph optimisation, operators one after another
               && (! safeMode || (check (api->SetSessionGraphOptimizationLevel (options, ORT_DISABLE_ALL), error)
                                  && check (api->SetSessionExecutionMode (options, ORT_SEQUENTIAL), error)))
               && check (api->SetInterOpNumThreads (options, 1), error)
               // no arena: one 7.8 s segment needs ~4 GB of tensors for a moment; the arena kept that (and more, it
               // grows by doubling) until the job ended, without it the RAM goes back between segments (same speed)
               && check (api->DisableCpuMemArena (options), error)
               && check (api->AddSessionConfigEntry (options, "session.intra_op.allow_spinning", "0"), error)
               && check (api->AddSessionConfigEntry (options, "session.inter_op.allow_spinning", "0"), error);
       #if JUCE_WINDOWS
        ok = ok && check (api->CreateSession (env, model.getFullPathName().toWideCharPointer(), options, &session), error);
       #else
        ok = ok && check (api->CreateSession (env, model.getFullPathName().toRawUTF8(), options, &session), error);
       #endif
        if (options != nullptr)
            api->ReleaseSessionOptions (options);
        if (! ok && session == nullptr && error.isNotEmpty())
            error = tr ("could not read {0}", { model.getFullPathName() }) + "\n\n" + error;
        return ok;
    }

    // Any thread: ends a running segment early.
    void terminate() { if (runOptions != nullptr) api->ReleaseStatus (api->RunOptionsSetTerminate (runOptions)); }

    bool run (const float* in, float* out, juce::String& error)
    {
        using namespace stemsplit;
        const std::int64_t shape[] { 1, 2, segmentFrames };
        OrtValue* input = nullptr;
        OrtValue* output = nullptr;
        OrtTensorTypeAndShapeInfo* info = nullptr;
        const char* inputName = "mix";
        const char* outputName = "stems";
        void* data = nullptr;
        std::size_t count = 0;
        auto ok = check (api->CreateTensorWithDataAsOrtValue (memory, const_cast<float*> (in), (std::size_t) (2 * segmentFrames) * sizeof (float), shape, 3,
                                                              ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input), error)
               && check (api->Run (session, runOptions, &inputName, &input, 1, &outputName, 1, &output), error)
               && check (api->GetTensorTypeAndShape (output, &info), error)
               && check (api->GetTensorShapeElementCount (info, &count), error)
               && check (api->GetTensorMutableData (output, &data), error);
        const auto expected = (std::size_t) (outputs * 2 * segmentFrames);
        if (ok && count != expected)
        {
            error = tr ("The model returned an unexpected result.");
            ok = false;
        }
        if (ok)
            std::memcpy (out, data, expected * sizeof (float));
        if (info != nullptr)
            api->ReleaseTensorTypeAndShapeInfo (info);
        api->ReleaseValue (output);
        api->ReleaseValue (input);
        return ok;
    }

private:
    bool check (OrtStatus* status, juce::String& error)
    {
        if (status == nullptr)
            return true;
        error = juce::String::fromUTF8 (api->GetErrorMessage (status));
        api->ReleaseStatus (status);
        return false;
    }

    Runtime runtime; // (first member: unloaded last)
    const OrtApi* api = nullptr;
    OrtEnv* env = nullptr;
    OrtMemoryInfo* memory = nullptr;
    OrtRunOptions* runOptions = nullptr;
    OrtSession* session = nullptr;
    int outputs = stemsplit::numStems;
};

bool writeWav (const juce::File& file, const std::array<std::vector<float>, 2>& channels)
{
    juce::WavAudioFormat wav;
    auto stream = file.createOutputStream();
    if (stream == nullptr)
        return false;
    // 32-bit float: a stem of a loud master can peak above 0 dB
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), stemsplit::sampleRate, 2, 32, {}, 0));
    if (writer == nullptr)
        return false;
    stream.release(); // (the writer owns it now)
    const float* data[] { channels[0].data(), channels[1].data() };
    return writer->writeFromFloatArrays (data, 2, (int) channels[0].size()) && writer->flush();
}

// Separates a sample with the options' passes and writes "<name> - <stem>.wav" into `folder` (round 190jr: only the
// stems asked for, from the model asked for); nothing stays behind on failure or Cancel. `files` gets (stem, file).
bool separateToFiles (const stemsplit::Options& options, const SampleData& sample, const juce::String& name, const juce::File& folder,
                      const std::function<bool()>& cancelled, const std::function<void (double)>& progress,
                      std::vector<std::pair<int, juce::File>>& files, const std::function<void (const juce::String&)>& status, juce::String& error,
                      const std::function<void (StemModel*)>& active = {})
{
    using namespace stemsplit;
    const auto frames = (std::int64_t) sample.frames();
    if (frames <= 0 || sample.sampleRate <= 0.0)
    {
        error = tr ("The sample is empty.");
        return false;
    }
    const auto passes = passesFor (options);
    if (passes.empty())
    {
        error = tr ("No stem is ticked.");
        return false;
    }
    // the model works in stereo at 44.1 kHz; the clip plays each file at its own rate, so it stays aligned
    const auto outFrames = juce::jmax<std::int64_t> (1, std::llround ((double) frames * sampleRate / sample.sampleRate));
    std::vector<float> left (sample.leftData(), sample.leftData() + frames);
    std::vector<float> right = sample.isStereo() ? std::vector<float> (sample.rightData(), sample.rightData() + frames) : left;
    left = audioedit::resampleChannel (left, outFrames, cancelled); // (a long file takes a while: Cancel works here too)
    right = audioedit::resampleChannel (right, outFrames, cancelled);
    if (cancelled())
        return false;
    if (! folder.createDirectory())
    {
        error = tr ("can't write {0}", { folder.getFullPathName() });
        return false;
    }
    auto fail = [&files] {
        for (auto& f : files)
            f.second.deleteFile();
        files.clear();
        return false;
    };
    const auto threads = separationThreads (juce::SystemStats::getNumPhysicalCpus(), juce::SystemStats::getNumCpus(), options);
    for (std::size_t p = 0; p < passes.size(); ++p)
    {
        const auto& pass = passes[p];
        if (passes.size() > 1 && status)
            status (tr ("Extracting stems... ({0} of {1})", { juce::String ((int) p + 1), juce::String ((int) passes.size()) }));
        StemModel model;
        if (! model.open (fileOf (*pass.model), error, threads, options.safeMode, pass.model->outputs))
            return fail();
        struct Running // (Cancel can end the running segment of this pass at once)
        {
            const std::function<void (StemModel*)>& set;
            Running (const std::function<void (StemModel*)>& f, StemModel* m) : set (f) { if (set) set (m); }
            ~Running() { if (set) set (nullptr); }
        } running { active, &model };
        std::vector<std::array<std::vector<float>, 2>> stems ((std::size_t) pass.model->outputs);
        const auto base = (double) p / (double) passes.size();
        if (! separateInto (left, right, pass.model->outputs, [&] (const float* in, float* out) { return ! cancelled() && model.run (in, out, error) && ! cancelled(); },
                            [&] (double v) { if (progress) progress (base + v / (double) passes.size()); }, stems))
            return fail();
        for (const auto s : pass.take)
        {
            const auto file = folder.getNonexistentChildFile (name + " - " + allStemNames[(std::size_t) s], ".wav", false);
            files.push_back ({ s, file });
            if (cancelled() || ! writeWav (file, stems[(std::size_t) s]))
            {
                if (! cancelled())
                    error = tr ("can't write {0}", { file.getFullPathName() });
                return fail();
            }
        }
    } // (each pass's session, its threads and its tensors go before the next one opens)
    return true;
}

// One "Extract stems" job (round 167x): the download, the separation and the WAVs on a low-priority worker, a progress
// window that never blocks the message thread. Cancel closes the window at once and the job winds down behind it
// (ONNX Runtime's terminate flag ends a running segment; a thread is never killed). The result lands as one undo step.
class StemJob final : private juce::Thread,
                      private juce::Timer
{
public:
    StemJob (AudioEngine& e, int clip, std::string path, std::string sampleName, juce::File targetFolder, std::shared_ptr<const SampleData> data,
             stemsplit::Options chosen, std::function<void()> done)
        : juce::Thread ("LYNX stem extraction"),
          engine (e), clipIndex (clip), sourcePath (std::move (path)), name (std::move (sampleName)), folder (std::move (targetFolder)), sample (std::move (data)),
          onDone (std::move (done)), options (chosen)
    {
        active() = this;
        window = std::make_unique<juce::AlertWindow> (tr ("Extract stems from sample"), tr ("Extracting stems..."), juce::MessageBoxIconType::NoIcon);
        window->addProgressBarComponent (shownProgress);
        window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        // modal, as the song must not change under the job. The callback comes asynchronously, also after finish()
        // closed the window: it only acts while the window (owned by this job) still exists
        juce::Component::SafePointer<juce::AlertWindow> alive (window.get());
        window->enterModalState (true, juce::ModalCallbackFunction::create ([this, alive] (int) {
                                     if (alive != nullptr)
                                         cancel();
                                 }), false);
        startThread (juce::Thread::Priority::low);
        startTimer (100);
    }

    ~StemJob() override
    {
        stopTimer();
        window.reset();
        active() = nullptr;
    }

    static StemJob*& active()
    {
        static StemJob* job = nullptr; // (message thread only)
        return job;
    }

    // App shutdown: stops at the next check (ONNX Runtime's terminate, the download's cancel) and waits for it.
    void abandon()
    {
        finishing = true;
        cancel (true);
        waitForThreadToExit (-1);
        deleteFiles();
        delete this;
    }

private:
    void run() override
    {
        // every graph the options need, downloaded once each (round 190jr: the 6-stem and fine-tuned models too)
        std::vector<const stemsplit::ModelFile*> needed;
        for (const auto& pass : stemsplit::passesFor (options))
            if (! installed (*pass.model) && std::find (needed.begin(), needed.end(), pass.model) == needed.end())
                needed.push_back (pass.model);
        for (std::size_t i = 0; i < needed.size(); ++i)
            if (threadShouldExit() || ! downloadModel (*needed[i], (int) i + 1, (int) needed.size()))
                return;
        if (threadShouldExit())
            return;
        setStatus (tr ("Extracting stems..."));
        progress = -1.0;
        finished = separateToFiles (options, *sample, juce::String::fromUTF8 (name.c_str()), folder, [this] { return threadShouldExit(); },
                                    [this] (double p) { progress = p; }, files, [this] (const juce::String& t) { setStatus (t); }, error,
                                    [this] (StemModel* m) {
                                        const std::lock_guard<std::mutex> lock (active_);
                                        model = m;
                                        if (m != nullptr && threadShouldExit())
                                            m->terminate();
                                    });
    } // (the sessions, their threads and the library are released as each pass ends: RAM goes back once the job ends)

    void timerCallback() override
    {
        shownProgress = progress.load();
        {
            const std::lock_guard<std::mutex> lock (statusLock);
            if (window != nullptr && status != shownStatus)
                window->setMessage (shownStatus = status);
        }
        if (isThreadRunning())
            return;
        finish();
    }

    void finish()
    {
        stopTimer();
        finishing = true;
        if (window != nullptr)
            window->exitModalState (0);
        window.reset();

        if (cancelled || ! finished)
        {
            deleteFiles();
            if (! cancelled && error.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Extract stems from sample"), error);
        }
        else
        {
            std::vector<stemsplit::StemFile> stems;
            for (const auto& [stem, file] : files)
                stems.push_back ({ stemsplit::allStemNames[(std::size_t) stem], file.getFullPathName().toStdString() });
            stemsplit::Placement place;
            place.newInserts = options.newInserts;
            place.groupTracks = options.groupTracks;
            place.whenDone = options.whenDone;
            bool added = false;
            engine.editSong ([&] (Song& s) { added = stemsplit::addStemClips (s, clipIndex, sourcePath, stems, place); });
            if (added && onDone)
                onDone();
        }
        delete this; // (a Timer may be deleted in its own callback)
    }

    void cancel (bool wait = false)
    {
        if (finishing && ! wait)
            return; // (finish() closing the window)
        cancelled = true;
        if (window != nullptr)
            window->setVisible (false); // the window goes at once; the worker stops behind it
        signalThreadShouldExit();
        const std::lock_guard<std::mutex> lock (active_);
        if (model != nullptr)
            model->terminate();
       #if ! (JUCE_LINUX || JUCE_BSD)
        if (stream != nullptr)
            stream->cancel();
       #endif
    }

    void deleteFiles()
    {
        if (finished)
            for (auto& f : files)
                f.second.deleteFile();
    }

    void setStatus (const juce::String& text)
    {
        const std::lock_guard<std::mutex> lock (statusLock);
        status = text;
    }

    bool downloadModel (const stemsplit::ModelFile& model, int index, int count)
    {
        setStatus (count > 1 ? tr ("Downloading the stem models... ({0} of {1})", { juce::String (index), juce::String (count) }) : tr ("Downloading the stem model..."));
        progress = 0.0;
        const auto target = fileOf (model);
        const auto part = target.withFileExtension ("part");
        target.getParentDirectory().createDirectory();
        target.deleteFile(); // (a damaged copy of the wrong size)
        const auto ok = download (part, model) && verify (part, model) && part.moveFileTo (target);
        if (! ok)
        {
            part.deleteFile();
            if (error.isEmpty() && ! threadShouldExit())
                error = tr ("can't write {0}", { target.getFullPathName() });
        }
        return ok;
    }

    bool verify (const juce::File& file, const stemsplit::ModelFile& model)
    {
        setStatus (tr ("Checking the stem model..."));
        progress = -1.0;
        juce::FileInputStream in (file);
        update::Sha256Stream hash;
        juce::HeapBlock<char> buffer (65536);
        while (in.openedOk() && ! in.isExhausted() && ! threadShouldExit())
        {
            const auto n = in.read (buffer, 65536);
            if (n <= 0)
                break;
            hash.add (buffer, (std::size_t) n);
        }
        if (threadShouldExit())
            return false;
        if (hash.finish() == model.sha256)
            return true;
        error = tr ("The downloaded model failed its checksum, so it was not used.");
        return false;
    }

    bool download (const juce::File& dest, const stemsplit::ModelFile& model)
    {
        const auto modelBytes = model.bytes;
        const auto* modelUrl = model.url;
       #if JUCE_LINUX || JUCE_BSD
        // (LYNX is built without libcurl, JUCE_USE_CURL=0: the curl program fetches it, as in the updater)
        juce::ChildProcess child;
        if (! child.start ({ "curl", "-fsSL", "--connect-timeout", "8", "--speed-limit", "1", "--speed-time", "30",
                             "--max-filesize", juce::String (modelBytes), "-o", dest.getFullPathName(), modelUrl }, 0))
        {
            error = tr ("the curl program is missing");
            return false;
        }
        while (child.isRunning())
        {
            if (threadShouldExit())
            {
                child.kill();
                return false;
            }
            progress = (double) dest.getSize() / (double) modelBytes;
            juce::Thread::sleep (150);
        }
        if (child.getExitCode() != 0)
        {
            error = tr ("no connection");
            return false;
        }
        return true;
       #else
        juce::WebInputStream web (juce::URL (modelUrl), false);
        web.withConnectionTimeout (20000).withNumRedirectsToFollow (5);
        {
            const std::lock_guard<std::mutex> lock (active_);
            stream = threadShouldExit() ? nullptr : &web;
        }
        struct Clear
        {
            StemJob& t;
            ~Clear()
            {
                const std::lock_guard<std::mutex> lock (t.active_);
                t.stream = nullptr;
            }
        } clear { *this };
        if (threadShouldExit())
            return false;
        if (! web.connect (nullptr) || web.getStatusCode() != 200)
        {
            if (! threadShouldExit())
                error = web.getStatusCode() > 0 ? "HTTP " + juce::String (web.getStatusCode()) : tr ("no connection");
            return false;
        }
        dest.deleteFile(); // (a stream appends to what is there)
        juce::FileOutputStream out (dest);
        if (! out.openedOk())
        {
            error = tr ("can't write {0}", { dest.getFullPathName() });
            return false;
        }
        juce::HeapBlock<char> buffer (65536);
        std::int64_t got = 0;
        while (! web.isExhausted())
        {
            if (threadShouldExit())
                return false;
            const auto n = web.read (buffer, 65536);
            if (n < 0 || (n == 0 && web.isError()))
            {
                error = tr ("the connection was lost");
                return false;
            }
            if (n > 0 && ! out.write (buffer, (std::size_t) n))
            {
                error = tr ("can't write {0}", { dest.getFullPathName() });
                return false;
            }
            got += n;
            if (got > modelBytes)
            {
                error = tr ("The downloaded model failed its checksum, so it was not used."); // (more than the pinned file)
                return false;
            }
            progress = (double) got / (double) modelBytes;
        }
        out.flush();
        return out.getStatus().wasOk();
       #endif
    }

    AudioEngine& engine;
    int clipIndex;
    std::string sourcePath, name;
    juce::File folder;
    std::shared_ptr<const SampleData> sample;
    std::function<void()> onDone;
    stemsplit::Options options;
    std::vector<std::pair<int, juce::File>> files;
    juce::String error;
    std::atomic<bool> finished { false };
    std::atomic<bool> cancelled { false };
    bool finishing = false; // (message thread)
    std::atomic<double> progress { -1.0 };
    double shownProgress = -1.0; // (message thread: the progress bar reads it)
    std::mutex statusLock;
    juce::String status, shownStatus;
    std::unique_ptr<juce::AlertWindow> window;
    std::mutex active_; // guards `model` and `stream` for cancel()
    StemModel* model = nullptr;
    juce::WebInputStream* stream = nullptr;
};
} // namespace

// --- the options form (round 190jr, BUILD-LIST 14): FL's TDEMUCSFORM, then LYNX's model rows ---------------------------
namespace
{
class StemOptionsPanel final : public juce::Component
{
public:
    StemOptionsPanel (stemsplit::Options start, std::function<void (stemsplit::Options)> whenExtract) : options (start), onExtract (std::move (whenExtract))
    {
        // FL's controls and captions, in FL's places (the form's OptionsPanel, 287 x 176 at 100 %)
        const char* names[] = { TR_NOOP ("Drums"), TR_NOOP ("Bass"), TR_NOOP ("Instruments"), TR_NOOP ("Vocals"), TR_NOOP ("Guitar"), TR_NOOP ("Piano") };
        const char* tips[] = { TR_NOOP ("Extract drums"), TR_NOOP ("Extract bass"), TR_NOOP ("Extract other instruments"), TR_NOOP ("Extract vocals"),
                               TR_NOOP ("Extract guitar (6 stems model)"), TR_NOOP ("Extract piano (6 stems model)") };
        for (int s = 0; s < stemsplit::maxStems; ++s)
        {
            auto& b = stems[(std::size_t) s];
            b.setButtonText (tr (names[s]));
            b.setTooltip (tr (tips[s]));
            b.setToggleState (options.stems[(std::size_t) s], juce::dontSendNotification);
            b.onClick = [this] { refresh(); };
            addAndMakeVisible (b);
        }
        limitCpu.setButtonText (tr ("Limit CPU usage"));
        limitCpu.setTooltip (tr ("Limit separation to fewer CPU cores"));
        limitCpu.setToggleState (options.limitCpu, juce::dontSendNotification);
        safeMode.setButtonText (tr ("Safe mode"));
        safeMode.setTooltip (tr ("Use this for troubleshooting."));
        safeMode.setToggleState (options.safeMode, juce::dontSendNotification);
        whenDone.addItem (tr ("Mute source clip"), 1);
        whenDone.addItem (tr ("Mute source track"), 2);
        whenDone.addItem (tr ("Do nothing"), 3);
        whenDone.setSelectedId ((int) options.whenDone + 1, juce::dontSendNotification);
        whenDone.setTooltip (tr ("Mute source clip, track or nothing after separation"));
        whenDoneLabel.setText (tr ("When done"), juce::dontSendNotification);
        groupTracks.setButtonText (tr ("Group playlist tracks"));
        groupTracks.setTooltip (tr ("Group the playlist tracks for the separated stems"));
        groupTracks.setToggleState (options.groupTracks, juce::dontSendNotification);
        newInserts.setButtonText (tr ("Assign to new mixer tracks"));
        newInserts.setTooltip (tr ("Assign each stem to a different mixer track"));
        newInserts.setToggleState (options.newInserts, juce::dontSendNotification);
        // LYNX's model row (BandLab's Splitter offers 4 and 6 stems; the fine-tuned models are the best of HT-Demucs)
        modelLabel.setText (tr ("Model"), juce::dontSendNotification);
        model.addItem (tr ("Standard (4 stems)"), 1);
        model.addItem (tr ("6 stems (adds guitar and piano)"), 2);
        model.addItem (tr ("Fine-tuned (best, slower)"), 3);
        model.setSelectedId ((int) options.model + 1, juce::dontSendNotification);
        model.setTooltip (tr ("Which separation model: the fine-tuned one separates best and takes one pass per stem"));
        model.onChange = [this] { refresh(); };
        cancel.setButtonText (tr ("Cancel"));
        cancel.onClick = [this] { close(); };
        extractButton.setButtonText (tr ("Extract"));
        extractButton.setTooltip (tr ("Extract stems responsibly"));
        extractButton.onClick = [this] { extract(); };
        for (juce::Component* c : std::initializer_list<juce::Component*> { &limitCpu, &safeMode, &whenDone, &whenDoneLabel, &groupTracks, &newInserts, &modelLabel,
                                                                            &model, &note, &cancel, &extractButton })
            addAndMakeVisible (c);
        note.setFont (theme::font (theme::type::small));
        note.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        note.setJustificationType (juce::Justification::topLeft);
        setSize (289, 300);
        refresh();
    }

    stemsplit::Options current() const
    {
        auto o = options;
        for (int s = 0; s < stemsplit::maxStems; ++s)
            o.stems[(std::size_t) s] = stems[(std::size_t) s].getToggleState();
        o.limitCpu = limitCpu.getToggleState();
        o.safeMode = safeMode.getToggleState();
        o.whenDone = (stemsplit::WhenDone) juce::jlimit (0, 2, whenDone.getSelectedId() - 1);
        o.groupTracks = groupTracks.getToggleState();
        o.newInserts = newInserts.getToggleState();
        o.model = (stemsplit::Model) juce::jlimit (0, 2, model.getSelectedId() - 1);
        return o;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg2);
        g.setColour (theme::colour::bg3);
        g.fillRect (getLocalBounds().withHeight (176).reduced (1, 0)); // (FL's OptionsPanel plate)
        g.fillRect (juce::Rectangle<int> (1, 180, getWidth() - 2, 82));  // LYNX's model rows
    }

    void resized() override
    {
        // FL's DFM coordinates (TDEMUCSFORM, 100 %)
        stems[0].setBounds (12, 11, 130, 21);
        stems[1].setBounds (143, 11, 130, 21);
        stems[2].setBounds (12, 32, 130, 21);
        stems[3].setBounds (143, 32, 130, 21);
        limitCpu.setBounds (12, 63, 114, 21);
        safeMode.setBounds (143, 63, 114, 21);
        whenDone.setBounds (12, 94, 123, 23);
        whenDoneLabel.setBounds (141, 94, 80, 23);
        groupTracks.setBounds (12, 123, 233, 21);
        newInserts.setBounds (12, 150, 233, 21);
        // LYNX: the model and the two stems only the 6-stem model has
        modelLabel.setBounds (12, 186, 50, 23);
        model.setBounds (64, 186, 211, 23);
        stems[4].setBounds (12, 213, 130, 21);
        stems[5].setBounds (143, 213, 130, 21);
        note.setBounds (12, 236, 265, 24);
        cancel.setBounds (0, getHeight() - 29, 60, 29);
        extractButton.setBounds (getWidth() - 61, getHeight() - 29, 60, 29);
    }

    void pressExtractForSnapshot() { extract(); }

private:
    void refresh()
    {
        const auto o = current();
        const auto six = o.model == stemsplit::Model::sixStems;
        stems[4].setEnabled (six);
        stems[5].setEnabled (six);
        const auto passes = stemsplit::passesFor (o);
        extractButton.setEnabled (! passes.empty());
        const auto missing = stemsplit::downloadBytes (passes, [] (const stemsplit::ModelFile& m) { return installed (m); });
        juce::String text;
        if (passes.empty())
            text = tr ("Tick at least one stem.");
        else if (missing > 0)
            text = tr ("Downloads {0} MB the first time.", { juce::String ((int) (missing / 1000000)) });
        if (o.model == stemsplit::Model::fineTuned && passes.size() > 1)
            text << (text.isEmpty() ? "" : " ") << tr ("{0} passes.", { juce::String ((int) passes.size()) });
        note.setText (text, juce::dontSendNotification);
    }

    void close()
    {
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->exitModalState (0);
    }

    void extract()
    {
        const auto o = current();
        if (stemsplit::passesFor (o).empty())
            return;
        auto then = onExtract;
        close();
        if (then)
            then (o);
    }

    stemsplit::Options options;
    std::function<void (stemsplit::Options)> onExtract;
    std::array<juce::ToggleButton, stemsplit::maxStems> stems;
    juce::ToggleButton limitCpu, safeMode, groupTracks, newInserts;
    juce::ComboBox whenDone, model;
    juce::Label whenDoneLabel, modelLabel, note;
    juce::TextButton cancel, extractButton;
};
} // namespace

std::unique_ptr<juce::Component> makeStemOptionsPanel (const stemsplit::Options& start, std::function<void (stemsplit::Options)> extract)
{
    return std::make_unique<StemOptionsPanel> (start, std::move (extract));
}

void extractStemsFromClip (AudioEngine& engine, int clipIndex, std::function<void()> done)
{
    if (StemJob::active() != nullptr)
    {
        // (a cancelled job still winding down: one separation at a time)
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Extract stems from sample"), tr ("Stems are still being extracted. Try again when that has finished."));
        return;
    }
    const auto& song = engine.getSong();
    if (clipIndex < 0 || clipIndex >= (int) song.playlist.size())
        return;
    const auto& clip = song.playlist[(std::size_t) clipIndex];
    if (clip.kind != ClipKind::audio || clip.channel < 0 || clip.channel >= (int) song.channels.size())
        return;
    const auto path = song.channels[(std::size_t) clip.channel].samplePath;
    const auto source = juce::String::fromUTF8 (path.c_str());
    const auto sample = engine.getSampleLibrary().load (source);
    if (sample == nullptr)
        return;

    // the runtime first: a computer that can't run it gets the reason before any download (unloaded again right away)
    {
        Runtime runtime;
        juce::String error;
        if (! runtime.load (error))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Extract stems from sample"), error);
            return;
        }
    }

    // next to the project: <project>/Audio/Recorded/Stems (the global recordings folder for an unsaved project)
    const auto folder = engine.getRecordingFolder().getChildFile ("Stems");
    auto start = [&engine, clipIndex, path, name = juce::File (source).getFileNameWithoutExtension().toStdString(), folder, sample, done] (stemsplit::Options o) {
        if (StemJob::active() == nullptr)
            new StemJob (engine, clipIndex, path, name, folder, sample, o, done); // (deletes itself when it ends)
    };
    // FL's form first (round 190jr): the stems, the model and what happens after
    auto chosen = [start] (stemsplit::Options o) {
        saveOptions (o);
        const auto passes = stemsplit::passesFor (o);
        const auto missing = stemsplit::downloadBytes (passes, [] (const stemsplit::ModelFile& m) { return installed (m); });
        if (missing <= 0)
        {
            start (o);
            return;
        }
        // a model not downloaded yet: the source and the licence, before anything is downloaded
        const auto page = o.model == stemsplit::Model::sixStems ? modelPage6 : o.model == stemsplit::Model::fineTuned ? modelPageFine : modelPage;
        const auto text = tr ("Extracting stems needs the HT-Demucs model. It isn't part of LYNX SHARE: it is downloaded once ({0} MB) and kept in {1}.", { juce::String ((int) (missing / 1000000)), modelFolder().getFullPathName() })
                        + "\n\n" + tr ("Source: {0} (ONNX export of Demucs v4 by Meta AI Research)", { juce::String (page) })
                        + "\n\n" + tr ("Licence: the Demucs code is MIT. The licence of the trained weights is not clear, as the authors say: the training data includes MUSDB18-HQ, parts of which were for research use only. Decide for yourself whether that fits your use, commercial use above all.");
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, tr ("Extract stems from sample"), text, tr ("Download"), tr ("Cancel"), nullptr,
                                            juce::ModalCallbackFunction::create ([start, o] (int result) {
                                                if (result != 0)
                                                    start (o);
                                            }));
    };
    juce::DialogWindow::LaunchOptions dialog;
    dialog.dialogTitle = tr ("Extract stems from sample");
    dialog.content.setOwned (makeStemOptionsPanel (savedOptions(), chosen).release());
    dialog.dialogBackgroundColour = theme::colour::bg2;
    dialog.escapeKeyTriggersCloseButton = true;
    dialog.useNativeTitleBar = true;
    dialog.resizable = false;
    if (auto* w = dialog.launchAsync())
        keepAboveMainWindow (*w);
}

void shutdownStemExtraction()
{
    if (auto* job = StemJob::active())
        job->abandon();
}

juce::String extractStemsToFolder (AudioEngine& engine, const juce::File& input, const juce::File& folder, const stemsplit::Options& options)
{
    const auto sample = engine.getSampleLibrary().load (input.getFullPathName());
    if (sample == nullptr)
        return "can't read " + input.getFullPathName();
    for (const auto& pass : stemsplit::passesFor (options))
        if (! installed (*pass.model))
            return "no model at " + fileOf (*pass.model).getFullPathName();
    juce::String error;
    std::vector<std::pair<int, juce::File>> files;
    separateToFiles (options, *sample, input.getFileNameWithoutExtension(), folder, [] { return false; }, {}, files, {}, error);
    return error;
}
} // namespace lynx
