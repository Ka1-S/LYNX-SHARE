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

namespace lynx
{
namespace
{
// A fixed revision and hash: what the user agreed to is what runs.
constexpr const char* modelPage = "https://huggingface.co/adowu/htdemucs-onnx";
constexpr const char* modelUrl = "https://huggingface.co/adowu/htdemucs-onnx/resolve/e6b688a9a9f977e2efaba79d0f18b9b0cfc606ea/htdemucs_fp16weights.onnx";
constexpr const char* modelSha256 = "d05c269d0178d2a72ad484b10b11dd370193fc923201c3b27a99f848745db70a";
constexpr std::int64_t modelBytes = 165612636;

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

juce::File modelFile() { return modelFolder().getChildFile ("htdemucs_fp16weights.onnx"); }

// (a truncated or damaged copy counts as missing: it is downloaded again, after the same question)
bool modelInstalled() { return modelFile().getSize() == modelBytes; }

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

    bool open (const juce::File& model, juce::String& error)
    {
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
               && check (api->SetIntraOpNumThreads (options, stemsplit::separationThreads (juce::SystemStats::getNumPhysicalCpus(), juce::SystemStats::getNumCpus())), error)
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
        const auto expected = (std::size_t) (numStems * 2 * segmentFrames);
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

// Separates a sample and writes "<name> - <stem>.wav" into `folder`; nothing stays behind on failure or Cancel.
bool separateToFiles (StemModel& model, const SampleData& sample, const juce::String& name, const juce::File& folder,
                      const std::function<bool()>& cancelled, const std::function<void (double)>& progress,
                      std::array<juce::File, stemsplit::numStems>& files, juce::String& error)
{
    using namespace stemsplit;
    const auto frames = (std::int64_t) sample.frames();
    if (frames <= 0 || sample.sampleRate <= 0.0)
    {
        error = tr ("The sample is empty.");
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

    Stems stems;
    if (! separate (left, right, [&] (const float* in, float* out) { return ! cancelled() && model.run (in, out, error) && ! cancelled(); }, progress, stems))
        return false;
    left = {}; // (the mix is no longer needed while the stems are written)
    right = {};

    if (! folder.createDirectory())
    {
        error = tr ("can't write {0}", { folder.getFullPathName() });
        return false;
    }
    for (int s = 0; s < numStems; ++s)
    {
        auto& file = files[(std::size_t) s];
        file = folder.getNonexistentChildFile (name + " - " + stemNames[(std::size_t) s], ".wav", false);
        if (cancelled() || ! writeWav (file, stems[(std::size_t) s]))
        {
            if (! cancelled())
                error = tr ("can't write {0}", { file.getFullPathName() });
            for (int k = 0; k <= s; ++k)
                files[(std::size_t) k].deleteFile();
            return false;
        }
    }
    return true;
}

// One "Extract stems" job (round 167x): the download, the separation and the WAVs on a low-priority worker, a progress
// window that never blocks the message thread. Cancel closes the window at once and the job winds down behind it
// (ONNX Runtime's terminate flag ends a running segment; a thread is never killed). The result lands as one undo step.
class StemJob final : private juce::Thread,
                      private juce::Timer
{
public:
    StemJob (AudioEngine& e, int clip, std::string path, std::string sampleName, juce::File targetFolder, std::shared_ptr<const SampleData> data, std::function<void()> done)
        : juce::Thread ("LYNX stem extraction"),
          engine (e), clipIndex (clip), sourcePath (std::move (path)), name (std::move (sampleName)), folder (std::move (targetFolder)), sample (std::move (data)), onDone (std::move (done))
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
        if (! modelInstalled() && ! downloadModel())
            return;
        if (threadShouldExit())
            return;
        setStatus (tr ("Extracting stems..."));
        progress = -1.0;
        StemModel local;
        if (! local.open (modelFile(), error))
            return;
        {
            const std::lock_guard<std::mutex> lock (active_);
            model = &local;
        }
        if (! threadShouldExit())
            finished = separateToFiles (local, *sample, juce::String::fromUTF8 (name.c_str()), folder, [this] { return threadShouldExit(); },
                                        [this] (double p) { progress = p; }, files, error);
        const std::lock_guard<std::mutex> lock (active_);
        model = nullptr;
    } // (the session, its threads and the library are released here: RAM goes back once the job ends)

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
            std::array<std::string, stemsplit::numStems> paths;
            for (std::size_t s = 0; s < paths.size(); ++s)
                paths[s] = files[s].getFullPathName().toStdString();
            bool added = false;
            engine.editSong ([&] (Song& s) { added = stemsplit::addStemClips (s, clipIndex, sourcePath, paths); });
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
                f.deleteFile();
    }

    void setStatus (const juce::String& text)
    {
        const std::lock_guard<std::mutex> lock (statusLock);
        status = text;
    }

    bool downloadModel()
    {
        setStatus (tr ("Downloading the stem model..."));
        progress = 0.0;
        const auto target = modelFile();
        const auto part = target.withFileExtension ("part");
        target.getParentDirectory().createDirectory();
        target.deleteFile(); // (a damaged copy of the wrong size)
        const auto ok = download (part) && verify (part) && part.moveFileTo (target);
        if (! ok)
        {
            part.deleteFile();
            if (error.isEmpty() && ! threadShouldExit())
                error = tr ("can't write {0}", { target.getFullPathName() });
        }
        return ok;
    }

    bool verify (const juce::File& file)
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
        if (hash.finish() == modelSha256)
            return true;
        error = tr ("The downloaded model failed its checksum, so it was not used.");
        return false;
    }

    bool download (const juce::File& dest)
    {
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
    std::array<juce::File, stemsplit::numStems> files;
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
    auto start = [&engine, clipIndex, path, name = juce::File (source).getFileNameWithoutExtension().toStdString(), folder, sample, done] {
        if (StemJob::active() == nullptr)
            new StemJob (engine, clipIndex, path, name, folder, sample, done); // (deletes itself when it ends)
    };
    if (modelInstalled())
    {
        start();
        return;
    }

    // the first time: the source and the licence, before anything is downloaded
    const auto text = tr ("Extracting stems needs the HT-Demucs model. It isn't part of LYNX SHARE: it is downloaded once ({0} MB) and kept in {1}.", { juce::String (modelBytes / 1000000), modelFolder().getFullPathName() })
                    + "\n\n" + tr ("Source: {0} (ONNX export of Demucs v4 by Meta AI Research)", { juce::String (modelPage) })
                    + "\n\n" + tr ("Licence: the Demucs code is MIT. The licence of the trained weights is not clear, as the authors say: the training data includes MUSDB18-HQ, parts of which were for research use only. Decide for yourself whether that fits your use, commercial use above all.");
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, tr ("Extract stems from sample"), text, tr ("Download"), tr ("Cancel"), nullptr,
                                        juce::ModalCallbackFunction::create ([start] (int result) {
                                            if (result != 0)
                                                start();
                                        }));
}

void shutdownStemExtraction()
{
    if (auto* job = StemJob::active())
        job->abandon();
}

juce::String extractStemsToFolder (AudioEngine& engine, const juce::File& input, const juce::File& folder)
{
    const auto sample = engine.getSampleLibrary().load (input.getFullPathName());
    if (sample == nullptr)
        return "can't read " + input.getFullPathName();
    if (! modelInstalled())
        return "no model at " + modelFile().getFullPathName();
    juce::String error;
    StemModel model;
    if (! model.open (modelFile(), error))
        return error;
    std::array<juce::File, stemsplit::numStems> files;
    separateToFiles (model, *sample, input.getFileNameWithoutExtension(), folder, [] { return false; }, {}, files, error);
    return error;
}
} // namespace lynx
