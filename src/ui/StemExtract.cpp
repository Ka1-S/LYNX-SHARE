#include "ui/StemExtract.h"

#include <array>
#include <cmath>
#include <cstring>
#include <mutex>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <onnxruntime_c_api.h>

#include "core/AudioEdit.h"
#include "core/StemSplit.h"
#include "core/UpdateCheck.h"
#include "platform/Paths.h"
#include "ui/I18n.h"

namespace lynx
{
namespace
{
// A fixed revision and hash: what the user agreed to is what runs.
constexpr const char* modelPage = "https://huggingface.co/adowu/htdemucs-onnx";
constexpr const char* modelUrl = "https://huggingface.co/adowu/htdemucs-onnx/resolve/e6b688a9a9f977e2efaba79d0f18b9b0cfc606ea/htdemucs_fp16weights.onnx";
constexpr const char* modelSha256 = "d05c269d0178d2a72ad484b10b11dd370193fc923201c3b27a99f848745db70a";
constexpr std::int64_t modelBytes = 165612636;

juce::File modelFile() { return platform::settingsFile ("models").getChildFile ("htdemucs_fp16weights.onnx"); }

// ONNX Runtime, shipped next to LYNX and loaded by its full path (never a copy from the system's search path).
juce::File runtimeFile()
{
    const auto folder = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
   #if JUCE_MAC
    return folder.getSiblingFile ("Frameworks").getChildFile ("libonnxruntime.1.23.2.dylib");
   #elif JUCE_WINDOWS
    return folder.getChildFile ("onnxruntime.dll");
   #else
    return folder.getChildFile ("libonnxruntime.so.1.23.2");
   #endif
}

juce::String runtimeLoadError()
{
   #if JUCE_MAC
    return tr ("ONNX Runtime could not be loaded. Stem extraction needs macOS 13.4 or newer.");
   #elif JUCE_WINDOWS
    return tr ("ONNX Runtime could not be loaded. Install the Microsoft Visual C++ Redistributable (x64) and try again.");
   #else
    return tr ("ONNX Runtime could not be loaded. Stem extraction needs the system's libstdc++.");
   #endif
}

// One loaded model. Its C API comes from the library at run time: the Windows build is cross-compiled with MinGW and
// can't link Microsoft's import library.
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
            api->ReleaseEnv (env);
        }
    }

    bool open (const juce::File& model, juce::String& error)
    {
        const auto path = runtimeFile();
        const auto getBase = library.open (path.getFullPathName()) ? (const OrtApiBase* (ORT_API_CALL*) (void) noexcept) library.getFunction ("OrtGetApiBase") : nullptr;
        api = getBase != nullptr ? getBase()->GetApi (ORT_API_VERSION) : nullptr;
        if (api == nullptr)
        {
            error = runtimeLoadError();
            return false;
        }

        OrtSessionOptions* options = nullptr;
        auto ok = check (api->CreateEnv (ORT_LOGGING_LEVEL_ERROR, "LYNX", &env), error)
               && check (api->CreateCpuMemoryInfo (OrtArenaAllocator, OrtMemTypeDefault, &memory), error)
               && check (api->CreateRunOptions (&runOptions), error)
               && check (api->CreateSessionOptions (&options), error)
               // one core stays free for the audio thread: the project may be playing
               && check (api->SetIntraOpNumThreads (options, juce::jmax (1, juce::SystemStats::getNumCpus() - 1)), error);
       #if JUCE_WINDOWS
        ok = ok && check (api->CreateSession (env, model.getFullPathName().toWideCharPointer(), options, &session), error);
       #else
        ok = ok && check (api->CreateSession (env, model.getFullPathName().toRawUTF8(), options, &session), error);
       #endif
        if (options != nullptr)
            api->ReleaseSessionOptions (options);
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

    juce::DynamicLibrary library; // (first member: unloaded last)
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
    left = audioedit::resampleChannel (left, outFrames);
    right = audioedit::resampleChannel (right, outFrames);

    Stems stems;
    if (! separate (left, right, [&] (const float* in, float* out) { return ! cancelled() && model.run (in, out, error) && ! cancelled(); }, progress, stems))
        return false;

    if (! folder.createDirectory())
    {
        error = tr ("can't write {0}", { folder.getFullPathName() });
        return false;
    }
    for (int s = 0; s < numStems; ++s)
    {
        auto& file = files[(std::size_t) s];
        file = folder.getNonexistentChildFile (name + " - " + stemNames[(std::size_t) s], ".wav", false);
        if (! writeWav (file, stems[(std::size_t) s]))
        {
            error = tr ("can't write {0}", { file.getFullPathName() });
            for (int k = 0; k <= s; ++k)
                files[(std::size_t) k].deleteFile();
            return false;
        }
    }
    return true;
}

class StemThread final : public juce::ThreadWithProgressWindow,
                         private juce::Thread::Listener
{
public:
    StemThread (AudioEngine& e, int clip, std::string path, std::string sampleName, juce::File targetFolder, std::shared_ptr<const SampleData> data, std::function<void()> done)
        : juce::ThreadWithProgressWindow (tr ("Extracting stems..."), true, true, 30000),
          engine (e), clipIndex (clip), sourcePath (std::move (path)), name (std::move (sampleName)), folder (std::move (targetFolder)), sample (std::move (data)), onDone (std::move (done))
    {
        addListener (this);
    }

    ~StemThread() override { removeListener (this); }

    void run() override
    {
        if (! modelFile().existsAsFile() && ! downloadModel())
            return;
        setStatusMessage (tr ("Extracting stems..."));
        setProgress (-1.0);
        StemModel local;
        if (! local.open (modelFile(), error))
            return;
        {
            const std::lock_guard<std::mutex> lock (active);
            model = &local;
        }
        finished = separateToFiles (local, *sample, juce::String::fromUTF8 (name.c_str()), folder, [this] { return threadShouldExit(); },
                                    [this] (double p) { setProgress (p); }, files, error);
        const std::lock_guard<std::mutex> lock (active);
        model = nullptr;
    }

    void threadComplete (bool cancelled) override
    {
        std::array<std::string, stemsplit::numStems> paths;
        for (std::size_t s = 0; s < paths.size(); ++s)
            paths[s] = files[s].getFullPathName().toStdString();

        if (cancelled || ! finished)
        {
            if (finished) // cancelled after the last segment
                for (auto& f : files)
                    f.deleteFile();
            if (! cancelled && error.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Extract stems from sample"), error);
        }
        else
        {
            bool added = false;
            engine.editSong ([&] (Song& s) { added = stemsplit::addStemClips (s, clipIndex, sourcePath, paths); });
            if (added && onDone)
                onDone();
        }
        juce::MessageManager::callAsync ([this] { delete this; });
    }

private:
    void exitSignalSent() override
    {
        const std::lock_guard<std::mutex> lock (active);
        if (model != nullptr)
            model->terminate();
       #if ! (JUCE_LINUX || JUCE_BSD)
        if (stream != nullptr)
            stream->cancel();
       #endif
    }

    bool downloadModel()
    {
        setStatusMessage (tr ("Downloading the stem model..."));
        const auto target = modelFile();
        const auto part = target.withFileExtension ("part");
        target.getParentDirectory().createDirectory();
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
        setStatusMessage (tr ("Checking the stem model..."));
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
        if (! child.start ({ "curl", "-fsSL", "--connect-timeout", "8", "--speed-limit", "1", "--speed-time", "30", "-o", dest.getFullPathName(), modelUrl }, 0))
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
            setProgress ((double) dest.getSize() / (double) modelBytes);
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
            const std::lock_guard<std::mutex> lock (active);
            stream = threadShouldExit() ? nullptr : &web;
        }
        struct Clear
        {
            StemThread& t;
            ~Clear()
            {
                const std::lock_guard<std::mutex> lock (t.active);
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
            setProgress ((double) got / (double) modelBytes);
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
    bool finished = false;
    std::mutex active; // guards `model` and `stream` for exitSignalSent
    StemModel* model = nullptr;
    juce::WebInputStream* stream = nullptr;
};
} // namespace

void extractStemsFromClip (AudioEngine& engine, int clipIndex, std::function<void()> done)
{
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

    // next to the project: <project>/Audio/Recorded/Stems (the global recordings folder for an unsaved project)
    const auto folder = engine.getRecordingFolder().getChildFile ("Stems");
    auto start = [&engine, clipIndex, path, name = juce::File (source).getFileNameWithoutExtension().toStdString(), folder, sample, done] {
        (new StemThread (engine, clipIndex, path, name, folder, sample, done))->launchThread();
    };
    if (modelFile().existsAsFile())
    {
        start();
        return;
    }

    // the first time: the source and the licence, before anything is downloaded
    const auto text = tr ("Extracting stems needs the HT-Demucs model. It isn't part of LYNX SHARE: it is downloaded once ({0} MB) and kept in the settings folder.", { juce::String (modelBytes / 1000000) })
                    + "\n\n" + tr ("Source: {0} (ONNX export of Demucs v4 by Meta AI Research)", { juce::String (modelPage) })
                    + "\n\n" + tr ("Licence: the Demucs code is MIT. The licence of the trained weights is not clear, as the authors say: the training data includes MUSDB18-HQ, parts of which were for research use only. Decide for yourself whether that fits your use, commercial use above all.");
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, tr ("Extract stems from sample"), text, tr ("Download"), tr ("Cancel"), nullptr,
                                        juce::ModalCallbackFunction::create ([start] (int result) {
                                            if (result != 0)
                                                start();
                                        }));
}

juce::String extractStemsToFolder (AudioEngine& engine, const juce::File& input, const juce::File& folder)
{
    const auto sample = engine.getSampleLibrary().load (input.getFullPathName());
    if (sample == nullptr)
        return "can't read " + input.getFullPathName();
    if (! modelFile().existsAsFile())
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
