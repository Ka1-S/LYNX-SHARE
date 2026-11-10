#include "ui/AudioToMidi.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "core/AudioEdit.h"
#include "core/ClipOps.h"
#include "core/StemSplit.h"
#include "ui/I18n.h"
#include "ui/OnnxRuntime.h"

namespace lynx
{
namespace
{
// Spotify's Basic Pitch model (ICASSP 2022, Apache-2.0; third_party/basic-pitch), compiled in: 230 KB, no download.
#include "../../third_party/basic-pitch/nmp.onnx.inc"

juce::String runtimeLoadError()
{
   #if JUCE_MAC
    return tr ("ONNX Runtime could not be loaded. Converting to MIDI needs macOS 13.4 or newer.");
   #elif JUCE_WINDOWS
    return tr ("ONNX Runtime could not be loaded. Install the Microsoft Visual C++ Redistributable (x64) from https://aka.ms/vs/17/release/vc_redist.x64.exe and try again.");
   #else
    return tr ("ONNX Runtime could not be loaded. Converting to MIDI needs the system's libstdc++.");
   #endif
}

bool loadRuntime (onnx::Runtime& runtime, juce::String& error)
{
    return runtime.load (error, tr ("Converting to MIDI needs ONNX Runtime ({0}), which comes with LYNX SHARE's installer and zip, but it is missing next to the program. Install LYNX SHARE with the installer, or unpack the whole zip."),
                         runtimeLoadError());
}

// One Basic Pitch session (released with the job: no thread or memory of it stays behind).
class BasicPitch
{
public:
    ~BasicPitch()
    {
        if (api != nullptr)
        {
            api->ReleaseRunOptions (runOptions);
            api->ReleaseSession (session);
            api->ReleaseMemoryInfo (memory);
            api->ReleaseEnv (env);
        }
    }

    bool open (juce::String& error)
    {
        if (! loadRuntime (runtime, error))
            return false;
        api = runtime.api;
        OrtSessionOptions* options = nullptr;
        const auto ok = check (api->CreateEnv (ORT_LOGGING_LEVEL_ERROR, "LYNX", &env), error)
                     && check (api->CreateCpuMemoryInfo (OrtArenaAllocator, OrtMemTypeDefault, &memory), error)
                     && check (api->CreateRunOptions (&runOptions), error)
                     && check (api->CreateSessionOptions (&options), error)
                     // as the stem separation: cores stay free for the audio callback and the UI, idle workers sleep
                     && check (api->SetIntraOpNumThreads (options, stemsplit::separationThreads (juce::SystemStats::getNumPhysicalCpus(), juce::SystemStats::getNumCpus())), error)
                     && check (api->SetInterOpNumThreads (options, 1), error)
                     && check (api->AddSessionConfigEntry (options, "session.intra_op.allow_spinning", "0"), error)
                     && check (api->AddSessionConfigEntry (options, "session.inter_op.allow_spinning", "0"), error)
                     && check (api->CreateSessionFromArray (env, basicPitchModel, sizeof (basicPitchModel), options, &session), error);
        if (options != nullptr)
            api->ReleaseSessionOptions (options);
        return ok;
    }

    // Any thread: ends a running window early.
    void terminate() { if (runOptions != nullptr) api->ReleaseStatus (api->RunOptionsSetTerminate (runOptions)); }

    // One window: `in` = a2m::windowSamples samples, note / onset = framesPerWindow x a2m::keys each.
    bool run (const float* in, float* note, float* onset, juce::String& error)
    {
        const std::int64_t shape[] { 1, a2m::windowSamples, 1 };
        const char* inputName = "serving_default_input_2:0";
        const char* outputNames[] { "StatefulPartitionedCall:1", "StatefulPartitionedCall:2" }; // (note, onset; :0 = contour)
        OrtValue* input = nullptr;
        OrtValue* outputs[2] { nullptr, nullptr };
        auto ok = check (api->CreateTensorWithDataAsOrtValue (memory, const_cast<float*> (in), sizeof (float) * (std::size_t) a2m::windowSamples, shape, 3,
                                                              ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input), error)
               && check (api->Run (session, runOptions, &inputName, &input, 1, outputNames, 2, outputs), error);
        for (int k = 0; k < 2 && ok; ++k)
        {
            OrtTensorTypeAndShapeInfo* info = nullptr;
            std::size_t count = 0;
            void* data = nullptr;
            ok = check (api->GetTensorTypeAndShape (outputs[k], &info), error)
              && check (api->GetTensorShapeElementCount (info, &count), error)
              && check (api->GetTensorMutableData (outputs[k], &data), error);
            if (info != nullptr)
                api->ReleaseTensorTypeAndShapeInfo (info);
            if (ok && count != (std::size_t) (framesPerWindow * a2m::keys))
            {
                error = tr ("The model returned an unexpected result.");
                ok = false;
            }
            if (ok)
                std::memcpy (k == 0 ? note : onset, data, count * sizeof (float));
        }
        api->ReleaseValue (outputs[0]);
        api->ReleaseValue (outputs[1]);
        api->ReleaseValue (input);
        return ok;
    }

    static constexpr int framesPerWindow = 172; // the graph's output frames per 2 s window

private:
    bool check (OrtStatus* status, juce::String& error)
    {
        if (status == nullptr)
            return true;
        error = juce::String::fromUTF8 (api->GetErrorMessage (status));
        api->ReleaseStatus (status);
        return false;
    }

    onnx::Runtime runtime; // (first member: unloaded last)
    const OrtApi* api = nullptr;
    OrtEnv* env = nullptr;
    OrtMemoryInfo* memory = nullptr;
    OrtRunOptions* runOptions = nullptr;
    OrtSession* session = nullptr;
};

// The notes of mono audio. `model` is used for melody and harmony (may be null for drums). Empty on Cancel or error.
std::vector<a2m::Note> analyse (const std::vector<float>& mono, double rate, a2m::Mode mode, BasicPitch* model,
                                const std::function<bool()>& cancelled, const std::function<void (double)>& progress, juce::String& error)
{
    if (mono.empty() || rate <= 0.0)
        return {};
    if (mode == a2m::Mode::drums)
        return a2m::drumHits (mono.data(), (std::int64_t) mono.size(), rate);
    if (model == nullptr)
        return {};

    // the model hears 22.05 kHz mono
    const auto frames = std::max<std::int64_t> (1, std::llround ((double) mono.size() * a2m::modelRate / rate));
    const auto input = std::abs (rate - a2m::modelRate) < 1.0 ? mono : audioedit::resampleChannel (mono, frames, cancelled);
    if (cancelled() || input.empty())
        return {};
    a2m::Posteriorgram pg;
    if (! a2m::runModel (input, BasicPitch::framesPerWindow,
                         [&] (const float* in, float* note, float* onset) { return ! cancelled() && model->run (in, note, onset, error) && ! cancelled(); },
                         progress, pg))
        return {};
    return a2m::transcribe (pg, mode);
}

// One conversion: the analysis on a low-priority worker, a progress window that never blocks the message thread; Cancel
// closes the window at once and the job winds down behind it (ONNX Runtime's terminate flag ends a running window).
class ConvertJob final : private juce::Thread,
                         private juce::Timer
{
public:
    using Result = std::function<void (std::vector<a2m::Note>)>;

    ConvertJob (std::vector<float> audio, double sampleRate, a2m::Mode m, Result done)
        : juce::Thread ("LYNX audio to MIDI"), mono (std::move (audio)), rate (sampleRate), mode (m), onResult (std::move (done))
    {
        active() = this;
        window = std::make_unique<juce::AlertWindow> (tr ("Convert to MIDI"), tr ("Converting to MIDI..."), juce::MessageBoxIconType::NoIcon);
        window->addProgressBarComponent (shownProgress);
        window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<juce::AlertWindow> alive (window.get());
        window->enterModalState (true, juce::ModalCallbackFunction::create ([this, alive] (int) {
                                     if (alive != nullptr)
                                         cancel();
                                 }), false);
        startThread (juce::Thread::Priority::low);
        startTimer (100);
    }

    ~ConvertJob() override
    {
        stopTimer();
        window.reset();
        active() = nullptr;
    }

    static ConvertJob*& active()
    {
        static ConvertJob* job = nullptr; // (message thread only)
        return job;
    }

    void abandon()
    {
        finishing = true;
        cancel (true);
        waitForThreadToExit (-1);
        delete this;
    }

private:
    void run() override
    {
        BasicPitch local;
        if (mode != a2m::Mode::drums)
        {
            if (! local.open (error))
                return;
            const std::lock_guard<std::mutex> lock (modelLock);
            model = &local;
        }
        notes = analyse (mono, rate, mode, model, [this] { return threadShouldExit(); }, [this] (double p) { progress = p; }, error);
        finished = ! threadShouldExit() && error.isEmpty();
        const std::lock_guard<std::mutex> lock (modelLock);
        model = nullptr;
    }

    void timerCallback() override
    {
        shownProgress = progress.load();
        if (isThreadRunning())
            return;
        stopTimer();
        finishing = true;
        if (window != nullptr)
            window->exitModalState (0);
        window.reset();
        if (! cancelled && finished && onResult)
            onResult (std::move (notes));
        else if (! cancelled && error.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Convert to MIDI"), error);
        delete this; // (a Timer may be deleted in its own callback)
    }

    void cancel (bool wait = false)
    {
        if (finishing && ! wait)
            return;
        cancelled = true;
        if (window != nullptr)
            window->setVisible (false);
        signalThreadShouldExit();
        const std::lock_guard<std::mutex> lock (modelLock);
        if (model != nullptr)
            model->terminate();
    }

    std::vector<float> mono;
    double rate;
    a2m::Mode mode;
    Result onResult;
    std::vector<a2m::Note> notes;
    juce::String error;
    std::atomic<bool> finished { false }, cancelled { false };
    bool finishing = false; // (message thread)
    std::atomic<double> progress { -1.0 };
    double shownProgress = -1.0;
    std::unique_ptr<juce::AlertWindow> window;
    std::mutex modelLock; // guards `model` for cancel()
    BasicPitch* model = nullptr;
};

bool startJob (std::vector<float> mono, double rate, a2m::Mode mode, ConvertJob::Result done)
{
    if (ConvertJob::active() != nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Convert to MIDI"), tr ("Audio is still being converted to MIDI. Try again when that has finished."));
        return false;
    }
    if (mode != a2m::Mode::drums)
    {
        // the runtime first: a computer that can't run it gets the reason at once (unloaded again right away)
        onnx::Runtime runtime;
        juce::String error;
        if (! loadRuntime (runtime, error))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Convert to MIDI"), error);
            return false;
        }
    }
    new ConvertJob (std::move (mono), rate, mode, std::move (done)); // (deletes itself when it ends)
    return true;
}

a2m::Names namesFor (const juce::String& name, a2m::Mode mode)
{
    a2m::Names names;
    const auto pattern = mode == a2m::Mode::melody ? tr ("{0} - Melody", { name })
                       : mode == a2m::Mode::harmony ? tr ("{0} - Harmony", { name })
                                                    : tr ("{0} - Drums", { name });
    names.pattern = names.channel = pattern.toStdString();
    names.kick = tr ("Kick").toStdString();
    names.snare = tr ("Snare").toStdString();
    names.hat = tr ("Hat").toStdString();
    return names;
}

void noNotes()
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Convert to MIDI"), tr ("No notes were found in this audio."));
}
} // namespace

void convertClipToMidi (AudioEngine& engine, int clipIndex, a2m::Mode mode, std::function<void()> done)
{
    const auto& song = engine.getSong();
    if (clipIndex < 0 || clipIndex >= (int) song.playlist.size())
        return;
    const auto clip = song.playlist[(std::size_t) clipIndex];
    if (clip.kind != ClipKind::audio || clip.channel < 0 || clip.channel >= (int) song.channels.size())
        return;
    const auto channel = song.channels[(std::size_t) clip.channel];
    const auto source = juce::String::fromUTF8 (channel.samplePath.c_str());
    const auto sample = engine.getSampleLibrary().load (source);
    if (sample == nullptr || sample->frames() == 0)
        return;

    // what the clip plays: its part of the file, in the file's frames
    const auto span = clipops::clipSpan (channel, sample);
    a2m::ClipTiming timing;
    timing.sampleRate = sample->sampleRate;
    timing.zeroFrame = (double) span.first;
    timing.spanFrames = (double) span.frames;
    timing.framesPerTick = clipops::framesPerTick (channel, clip, (double) span.frames, sample->sampleRate, song.bpm);
    timing.backwards = clipops::playsBackwards (channel, clip);
    const auto range = a2m::clipFrames (clip, timing, (std::int64_t) sample->frames());
    if (range.count <= 0 || timing.framesPerTick <= 0.0)
        return;
    std::vector<float> mono ((std::size_t) range.count);
    const auto* left = sample->leftData();
    const auto* right = sample->isStereo() ? sample->rightData() : nullptr;
    for (std::int64_t i = 0; i < range.count; ++i)
        mono[(std::size_t) i] = right != nullptr ? 0.5f * (left[range.first + i] + right[range.first + i]) : left[range.first + i];

    // the pitch the clip is heard at: its own pitch, plus the channel's (kept-length shift, or the resampling's speed)
    const auto naturalFramesPerTick = 60.0 / (std::max (1.0, song.bpm) * ppq) * sample->sampleRate;
    const auto channelSemitones = channel.sampler.keepPitch ? (double) channel.sampler.pitch + (double) channel.misc.fineTune / 100.0
                                                            : 12.0 * std::log2 (timing.framesPerTick / naturalFramesPerTick);
    const auto transpose = (int) std::lround ((double) clip.pitch + channelSemitones);

    const auto names = namesFor (juce::File (source).getFileNameWithoutExtension(), mode);
    const auto path = channel.samplePath;
    startJob (std::move (mono), sample->sampleRate, mode, [&engine, clipIndex, mode, path, range, timing, transpose, names, done] (std::vector<a2m::Note> notes) {
        if (mode != a2m::Mode::drums)
            for (auto& n : notes)
                n.key = std::clamp (n.key + transpose, 0, 127);
        auto added = -1;
        engine.editSong ([&] (Song& s) {
            added = a2m::addClipPattern (s, clipIndex, path, mode, notes, range.first, timing, names);
            if (added >= 0)
                s.currentPattern = added;
        });
        if (added < 0)
            noNotes();
        else if (done)
            done();
    });
}

void convertAudioToMidi (AudioEngine& engine, std::vector<float> mono, double sampleRate, a2m::Mode mode, const juce::String& name,
                         std::function<void (int)> done)
{
    const auto names = namesFor (name, mode);
    startJob (std::move (mono), sampleRate, mode, [&engine, mode, names, done] (std::vector<a2m::Note> notes) {
        auto added = -1;
        engine.editSong ([&] (Song& s) {
            const auto ticksPerSecond = s.bpm / 60.0 * ppq;
            added = a2m::addPattern (s, mode, notes, [ticksPerSecond] (double seconds) { return seconds * ticksPerSecond; }, 0, names);
            if (added >= 0)
                s.currentPattern = added;
        });
        if (added < 0)
            noNotes();
        if (done)
            done (added < 0 ? 0 : (int) notes.size());
    });
}

void shutdownAudioToMidi()
{
    if (auto* job = ConvertJob::active())
        job->abandon();
}

juce::String convertFileToMidiCsv (AudioEngine& engine, const juce::File& input, a2m::Mode mode, const juce::File& csv)
{
    const auto sample = engine.getSampleLibrary().load (input.getFullPathName());
    if (sample == nullptr || sample->frames() == 0)
        return "can't read " + input.getFullPathName();
    std::vector<float> mono (sample->frames());
    for (std::size_t i = 0; i < mono.size(); ++i)
        mono[i] = sample->isStereo() ? 0.5f * (sample->leftData()[i] + sample->rightData()[i]) : sample->leftData()[i];
    juce::String error;
    BasicPitch model;
    if (mode != a2m::Mode::drums && ! model.open (error))
        return error;
    const auto notes = analyse (mono, sample->sampleRate, mode, &model, [] { return false; }, {}, error);
    if (error.isNotEmpty())
        return error;
    juce::String text ("onset_s,offset_s,pitch,velocity\n");
    for (const auto& n : notes)
        text << juce::String (n.start, 4) << "," << juce::String (n.end, 4) << "," << n.key << "," << (int) std::lround (n.velocity * 127.0f) << "\n";
    return csv.replaceWithText (text) ? juce::String() : "can't write " + csv.getFullPathName();
}
} // namespace lynx
