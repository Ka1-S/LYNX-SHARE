#include "ui/AudioToMidi.h"
#include "ui/BasicPitchSession.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <optional>

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

    // `count` windows back to back: `in` = count x a2m::windowSamples samples, note / onset = count x framesPerWindow x
    // a2m::keys each, contour (null: not asked for) count x framesPerWindow x a2m::contourBins.
    bool run (const float* in, int count, float* note, float* onset, float* contour, juce::String& error)
    {
        const std::int64_t shape[] { count, a2m::windowSamples, 1 };
        const char* inputName = "serving_default_input_2:0";
        const char* outputNames[] { "StatefulPartitionedCall:1", "StatefulPartitionedCall:2", "StatefulPartitionedCall:0" }; // (note, onset, contour)
        const auto outputCount = contour != nullptr ? 3 : 2;
        float* targets[] { note, onset, contour };
        const std::size_t sizes[] { (std::size_t) count * framesPerWindow * a2m::keys, (std::size_t) count * framesPerWindow * a2m::keys,
                                    (std::size_t) count * framesPerWindow * a2m::contourBins };
        OrtValue* input = nullptr;
        OrtValue* outputs[3] { nullptr, nullptr, nullptr };
        auto ok = check (api->CreateTensorWithDataAsOrtValue (memory, const_cast<float*> (in), sizeof (float) * (std::size_t) a2m::windowSamples * (std::size_t) count,
                                                              shape, 3, ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input), error)
               && check (api->Run (session, runOptions, &inputName, &input, 1, outputNames, (std::size_t) outputCount, outputs), error);
        for (int k = 0; k < outputCount && ok; ++k)
        {
            OrtTensorTypeAndShapeInfo* info = nullptr;
            std::size_t values = 0;
            void* data = nullptr;
            ok = check (api->GetTensorTypeAndShape (outputs[k], &info), error)
              && check (api->GetTensorShapeElementCount (info, &values), error)
              && check (api->GetTensorMutableData (outputs[k], &data), error);
            if (info != nullptr)
                api->ReleaseTensorTypeAndShapeInfo (info);
            if (ok && values != sizes[k])
            {
                error = tr ("The model returned an unexpected result.");
                ok = false;
            }
            if (ok)
                std::memcpy (targets[k], data, values * sizeof (float));
        }
        for (auto* o : outputs)
            api->ReleaseValue (o);
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

} // namespace

// --- basicpitch::Session (ui/BasicPitchSession.h) ------------------------------------------------------------------
struct basicpitch::Session::Impl
{
    BasicPitch model;
};

basicpitch::Session::Session() : impl (std::make_unique<Impl>()) {}
basicpitch::Session::~Session() = default;
bool basicpitch::Session::open (juce::String& error) { return impl->model.open (error); }
void basicpitch::Session::terminate() { impl->model.terminate(); }
bool basicpitch::Session::run (const float* in, int count, float* note, float* onset, float* contour, juce::String& error)
{
    return impl->model.run (in, count, note, onset, contour, error);
}

bool basicpitch::posteriorgram (Session& session, const std::vector<float>& mono, double rate, bool withContour,
                                const std::function<bool()>& cancelled, const std::function<void (double)>& progress, a2m::Posteriorgram& out,
                                juce::String& error, double* seconds)
{
    // windows per ONNX Runtime run: one (round 190lj measured 1 / 2 / 4 on a 3-minute mix: the same notes, no CPU or
    // wall time gained, +15 / +41 MB peak memory for the bigger runs)
    constexpr int windowsPerRun = 1;
    const auto stop = [&cancelled] { return cancelled && cancelled(); };
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    const auto input = a2m::toModelRate (mono, rate, stop); // the model hears 22.05 kHz mono
    if (stop() || input.empty())
        return false;
    const auto t1 = juce::Time::getMillisecondCounterHiRes();
    const auto ok = a2m::runModelBatched (input, Session::framesPerWindow, windowsPerRun, withContour,
                                          [&] (const float* in, int count, float* note, float* onset, float* contour) {
                                              return ! stop() && session.run (in, count, note, onset, contour, error) && ! stop();
                                          },
                                          progress, out);
    if (seconds != nullptr)
    {
        seconds[0] = (t1 - t0) / 1000.0;
        seconds[1] = (juce::Time::getMillisecondCounterHiRes() - t1) / 1000.0;
    }
    return ok;
}

namespace
{
// What a conversion does: one of the three menu modes, or (round 190lj) the options dialog's settings.
struct Request
{
    a2m::Mode mode = a2m::Mode::harmony;
    std::optional<a2m::ConvertOptions> options;
};

Request requestOf (const a2m::ConvertOptions& options)
{
    return { options.polyphonic ? a2m::Mode::harmony : a2m::Mode::melody, options };
}

// The notes of mono audio. `model` is used for melody and harmony (may be null for drums). Empty on Cancel or error.
// `timing` (may be null) gets the seconds spent resampling, in the model and making the notes.
std::vector<a2m::Note> analyse (const std::vector<float>& mono, double rate, const Request& request, basicpitch::Session* model,
                                const std::function<bool()>& cancelled, const std::function<void (double)>& progress, juce::String& error,
                                double* timing = nullptr)
{
    if (mono.empty() || rate <= 0.0)
        return {};
    if (request.mode == a2m::Mode::drums)
        return a2m::drumHits (mono.data(), (std::int64_t) mono.size(), rate);
    if (model == nullptr)
        return {};

    const auto bends = request.options && request.options->pitchBends;
    a2m::Posteriorgram pg;
    double seconds[2] {};
    if (! basicpitch::posteriorgram (*model, mono, rate, bends, cancelled, progress, pg, error, seconds))
        return {};
    const auto t2 = juce::Time::getMillisecondCounterHiRes();
    auto notes = request.options ? a2m::transcribe (pg, *request.options) : a2m::transcribe (pg, request.mode);
    if (timing != nullptr)
    {
        timing[0] = seconds[0];
        timing[1] = seconds[1];
        timing[2] = (juce::Time::getMillisecondCounterHiRes() - t2) / 1000.0;
    }
    return notes;
}

// One conversion: the analysis on a low-priority worker, a progress window that never blocks the message thread; Cancel
// closes the window at once and the job winds down behind it (ONNX Runtime's terminate flag ends a running window).
class ConvertJob final : private juce::Thread,
                         private juce::Timer
{
public:
    using Result = std::function<void (std::vector<a2m::Note>)>;

    ConvertJob (std::vector<float> audio, double sampleRate, Request r, Result done)
        : juce::Thread ("LYNX audio to MIDI"), mono (std::move (audio)), rate (sampleRate), request (std::move (r)), onResult (std::move (done))
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
        basicpitch::Session local;
        if (request.mode != a2m::Mode::drums)
        {
            if (! local.open (error))
                return;
            const std::lock_guard<std::mutex> lock (modelLock);
            model = &local;
        }
        notes = analyse (mono, rate, request, model, [this] { return threadShouldExit(); }, [this] (double p) { progress = p; }, error);
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
    Request request;
    Result onResult;
    std::vector<a2m::Note> notes;
    juce::String error;
    std::atomic<bool> finished { false }, cancelled { false };
    bool finishing = false; // (message thread)
    std::atomic<double> progress { -1.0 };
    double shownProgress = -1.0;
    std::unique_ptr<juce::AlertWindow> window;
    std::mutex modelLock; // guards `model` for cancel()
    basicpitch::Session* model = nullptr;
};

bool startJob (std::vector<float> mono, double rate, const Request& request, ConvertJob::Result done)
{
    const auto mode = request.mode;
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
    new ConvertJob (std::move (mono), rate, request, std::move (done)); // (deletes itself when it ends)
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

namespace
{
// What a clip conversion needs, read on the message thread before the analysis starts.
struct ClipWork
{
    std::vector<float> mono;
    double rate = 44100.0;
    a2m::FrameRange range;
    a2m::ClipTiming timing;
    int transpose = 0;
    a2m::Names names;
    std::string path;
    a2m::Placement place;
};

std::optional<ClipWork> prepareClip (AudioEngine& engine, int clipIndex, const Request& request)
{
    const auto mode = request.mode;
    const auto& song = engine.getSong();
    if (clipIndex < 0 || clipIndex >= (int) song.playlist.size())
        return {};
    const auto clip = song.playlist[(std::size_t) clipIndex];
    if (clip.kind != ClipKind::audio || clip.channel < 0 || clip.channel >= (int) song.channels.size())
        return {};
    const auto channel = song.channels[(std::size_t) clip.channel];
    const auto source = juce::String::fromUTF8 (channel.samplePath.c_str());
    const auto sample = engine.getSampleLibrary().load (source);
    if (sample == nullptr || sample->frames() == 0)
        return {};

    ClipWork w;
    // what the clip plays: its part of the file, in the file's frames
    const auto span = clipops::clipSpan (channel, sample);
    w.timing.sampleRate = sample->sampleRate;
    w.timing.zeroFrame = (double) span.first;
    w.timing.spanFrames = (double) span.frames;
    w.timing.framesPerTick = clipops::framesPerTick (channel, clip, (double) span.frames, sample->sampleRate, song.bpm);
    w.timing.backwards = clipops::playsBackwards (channel, clip);
    w.range = a2m::clipFrames (clip, w.timing, (std::int64_t) sample->frames());
    if (w.range.count <= 0 || w.timing.framesPerTick <= 0.0)
        return {};
    w.mono.resize ((std::size_t) w.range.count);
    const auto* left = sample->leftData();
    const auto* right = sample->isStereo() ? sample->rightData() : nullptr;
    for (std::int64_t i = 0; i < w.range.count; ++i)
        w.mono[(std::size_t) i] = right != nullptr ? 0.5f * (left[w.range.first + i] + right[w.range.first + i]) : left[w.range.first + i];
    w.rate = sample->sampleRate;

    // the pitch the clip is heard at: its own pitch, plus the channel's (kept-length shift, or the resampling's speed)
    const auto naturalFramesPerTick = 60.0 / (std::max (1.0, song.bpm) * ppq) * sample->sampleRate;
    const auto channelSemitones = channel.sampler.keepPitch ? (double) channel.sampler.pitch + (double) channel.misc.fineTune / 100.0
                                                            : 12.0 * std::log2 (w.timing.framesPerTick / naturalFramesPerTick);
    w.transpose = (int) std::lround ((double) clip.pitch + channelSemitones);
    w.names = namesFor (juce::File (source).getFileNameWithoutExtension(), mode);
    w.path = channel.samplePath;
    if (request.options)
    {
        w.place.channel = request.options->channel;
        w.place.quantizeTicks = request.options->quantize ? request.options->gridTicks : 0;
    }
    return w;
}

// The notes into the song (one undo step): the new pattern's index, or -1
int applyClip (AudioEngine& engine, int clipIndex, a2m::Mode mode, const ClipWork& w, std::vector<a2m::Note> notes)
{
    if (mode != a2m::Mode::drums)
        for (auto& n : notes)
            n.key = std::clamp (n.key + w.transpose, 0, 127);
    auto added = -1;
    engine.editSong ([&] (Song& s) {
        added = a2m::addClipPattern (s, clipIndex, w.path, mode, notes, w.range.first, w.timing, w.names, w.place);
        if (added >= 0)
            s.currentPattern = added;
    });
    return added;
}

void convertClip (AudioEngine& engine, int clipIndex, const Request& request, std::function<void()> done)
{
    auto work = prepareClip (engine, clipIndex, request);
    if (! work)
        return;
    auto mono = std::move (work->mono);
    const auto rate = work->rate;
    const auto mode = request.mode;
    startJob (std::move (mono), rate, request, [&engine, clipIndex, mode, w = std::move (*work), done] (std::vector<a2m::Note> notes) {
        if (applyClip (engine, clipIndex, mode, w, std::move (notes)) < 0)
            noNotes();
        else if (done)
            done();
    });
}

void convertAudio (AudioEngine& engine, std::vector<float> mono, double sampleRate, const Request& request, const juce::String& name,
                   std::function<void (int)> done)
{
    const auto mode = request.mode;
    const auto names = namesFor (name, mode);
    a2m::Placement place;
    if (request.options)
    {
        place.channel = request.options->channel;
        place.quantizeTicks = request.options->quantize ? request.options->gridTicks : 0;
    }
    startJob (std::move (mono), sampleRate, request, [&engine, mode, names, place, done] (std::vector<a2m::Note> notes) {
        auto added = -1;
        engine.editSong ([&] (Song& s) {
            const auto ticksPerSecond = s.bpm / 60.0 * ppq;
            added = a2m::addPattern (s, mode, notes, [ticksPerSecond] (double seconds) { return seconds * ticksPerSecond; }, 0, names, place);
            if (added >= 0)
                s.currentPattern = added;
        });
        if (added < 0)
            noNotes();
        if (done)
            done (added < 0 ? 0 : (int) notes.size());
    });
}

// --- the options dialog (round 190lj) -----------------------------------------------------------------------------
// FL-style key names (C5 = MIDI 60, as LYNX's Piano roll shows them)
juce::String keyName (int key)
{
    return juce::MidiMessage::getMidiNoteName (key, true, true, 5);
}

// a 0..100 % slider with its name above it, as the AlertWindow names its boxes (it has no slider of its own)
struct SliderRow final : juce::Component
{
    SliderRow (const juce::String& text, float value, const juce::String& tip)
    {
        label.setText (text, juce::dontSendNotification);
        label.setBorderSize ({});
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 22);
        slider.setRange (0.0, 100.0, 1.0);
        slider.setTextValueSuffix (" %");
        slider.setValue (std::round (value * 100.0f), juce::dontSendNotification);
        slider.setTooltip (tip);
        label.setTooltip (tip);
        addAndMakeVisible (label);
        addAndMakeVisible (slider);
        setSize (380, 46);
    }
    void lookAndFeelChanged() override
    {
        label.setFont (getLookAndFeel().getAlertWindowFont());
        label.setColour (juce::Label::textColourId, findColour (juce::AlertWindow::textColourId));
    }
    void parentHierarchyChanged() override { lookAndFeelChanged(); }
    void resized() override
    {
        label.setBounds (0, 0, getWidth(), 18);
        slider.setBounds (0, 20, getWidth(), 24);
    }
    float value() const { return (float) slider.getValue() / 100.0f; }
    juce::Label label;
    juce::Slider slider;
};

struct ToggleRow final : juce::Component
{
    ToggleRow (const juce::String& text, bool on, const juce::String& tip)
    {
        toggle.setButtonText (text);
        toggle.setToggleState (on, juce::dontSendNotification);
        toggle.setTooltip (tip);
        addAndMakeVisible (toggle);
        setSize (380, 26);
    }
    void resized() override { toggle.setBounds (0, 2, getWidth(), 22); }
    juce::ToggleButton toggle;
};

// The dialog: an AlertWindow with its own rows (it owns them, so it can be shown or pictured alone).
class ConvertOptionsWindow final : public juce::AlertWindow
{
public:
    ConvertOptionsWindow (AudioEngine& engine, juce::Component* owner, std::int64_t grid)
        : juce::AlertWindow (tr ("Convert to MIDI (polyphonic)"),
                             tr ("Finds the notes of the audio, chords too (Basic Pitch), and puts them in a new pattern."),
                             juce::MessageBoxIconType::NoIcon, owner),
          gridTicks (grid)
    {
        const auto& o = lastConvertOptions();
        addComboBox ("notes", { tr ("Polyphonic (chords)"), tr ("One note at a time (melody)") }, tr ("Notes"));
        getComboBoxComponent ("notes")->setSelectedItemIndex (o.polyphonic ? 0 : 1, juce::dontSendNotification);

        note = std::make_unique<SliderRow> (tr ("Note sensitivity"), o.noteSensitivity,
                                            tr ("Higher finds quieter notes (and more false ones). Basic Pitch's default: 70 %."));
        split = std::make_unique<SliderRow> (tr ("Split sensitivity"), o.splitSensitivity,
                                             tr ("Higher splits a held sound into more repeated notes. Basic Pitch's default: 50 %."));
        addCustomComponent (note.get());
        addCustomComponent (split.get());
        addTextEditor ("minimum", juce::String (juce::roundToInt (o.minNoteMs)), tr ("Shortest note (ms)"));

        juce::StringArray keys;
        for (int k = a2m::lowestKey; k < a2m::lowestKey + a2m::keys; ++k)
            keys.add (keyName (k));
        addComboBox ("low", keys, tr ("Lowest note"));
        addComboBox ("high", keys, tr ("Highest note"));
        getComboBoxComponent ("low")->setSelectedItemIndex (std::clamp (o.minKey - a2m::lowestKey, 0, a2m::keys - 1), juce::dontSendNotification);
        getComboBoxComponent ("high")->setSelectedItemIndex (std::clamp (o.maxKey - a2m::lowestKey, 0, a2m::keys - 1), juce::dontSendNotification);

        bends = std::make_unique<ToggleRow> (tr ("Pitch bends"), o.pitchBends,
                                             tr ("Each note follows the pitch it was sung or played at (its pitch curve in the Piano roll)."));
        quantize = std::make_unique<ToggleRow> (tr ("Quantize to the snap grid"), o.quantize && gridTicks > 0,
                                                tr ("Note starts and ends move to the nearest grid line."));
        quantize->toggle.setEnabled (gridTicks > 0);
        addCustomComponent (bends.get());
        addCustomComponent (quantize.get());

        // the channel that plays them: a new Synth channel (as the three menu items), or one of the song's instruments
        juce::StringArray names { tr ("New channel (Synth)") };
        const auto& song = engine.getSong();
        for (int c = 0; c < (int) song.channels.size(); ++c)
            if (a2m::canPlayNotes (song.channels[(std::size_t) c]))
            {
                channels.push_back (c);
                names.add (juce::String (c + 1) + "  " + juce::String::fromUTF8 (song.channels[(std::size_t) c].name.c_str()));
            }
        addComboBox ("channel", names, tr ("Play them on"));
        getComboBoxComponent ("channel")->setSelectedItemIndex (0, juce::dontSendNotification);

        // the shortest note follows the decoder's own default when it was left there (Basic Pitch 128 ms, melody 50 ms)
        getComboBoxComponent ("notes")->onChange = [this] {
            const auto poly = getComboBoxComponent ("notes")->getSelectedItemIndex() == 0;
            auto* minimum = getTextEditor ("minimum");
            const auto now = minimum->getText().trim().getIntValue();
            if (now == (poly ? 50 : 128))
                minimum->setText (poly ? "128" : "50", juce::dontSendNotification);
        };

        addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    }

    a2m::ConvertOptions read()
    {
        a2m::ConvertOptions r;
        r.polyphonic = getComboBoxComponent ("notes")->getSelectedItemIndex() == 0;
        r.noteSensitivity = note->value();
        r.splitSensitivity = split->value();
        r.minNoteMs = juce::jlimit (0.0, 5000.0, getTextEditorContents ("minimum").trim().getDoubleValue());
        r.minKey = a2m::lowestKey + std::max (0, getComboBoxComponent ("low")->getSelectedItemIndex());
        r.maxKey = a2m::lowestKey + std::max (0, getComboBoxComponent ("high")->getSelectedItemIndex());
        if (r.minKey > r.maxKey)
            std::swap (r.minKey, r.maxKey);
        r.pitchBends = bends->toggle.getToggleState();
        r.quantize = gridTicks > 0 && quantize->toggle.getToggleState();
        r.gridTicks = gridTicks;
        const auto pick = getComboBoxComponent ("channel")->getSelectedItemIndex();
        r.channel = pick >= 1 && pick - 1 < (int) channels.size() ? channels[(std::size_t) (pick - 1)] : -1;
        return r;
    }

private:
    std::int64_t gridTicks;
    std::unique_ptr<SliderRow> note, split;
    std::unique_ptr<ToggleRow> bends, quantize;
    std::vector<int> channels; // the channel combo's items after the first: song channel indices
};
} // namespace

a2m::ConvertOptions& lastConvertOptions()
{
    static a2m::ConvertOptions options; // (message thread)
    return options;
}

void showConvertOptions (AudioEngine& engine, juce::Component* owner, std::int64_t gridTicks, std::function<void (const a2m::ConvertOptions&)> go)
{
    auto* box = new ConvertOptionsWindow (engine, owner, gridTicks);
    box->enterModalState (true, juce::ModalCallbackFunction::create ([box, go] (int result) {
                              if (result != 1)
                                  return;
                              const auto r = box->read();
                              auto kept = r;
                              kept.channel = -1; // (channel numbers change: not kept)
                              lastConvertOptions() = kept;
                              if (go)
                                  go (r);
                          }),
                          true);
}

int convertClipToMidiNow (AudioEngine& engine, int clipIndex, const a2m::ConvertOptions& options, juce::String& error)
{
    const auto request = requestOf (options);
    const auto work = prepareClip (engine, clipIndex, request);
    if (! work)
    {
        error = "not an audio clip";
        return -1;
    }
    basicpitch::Session model;
    if (! model.open (error))
        return -1;
    auto notes = analyse (work->mono, work->rate, request, &model, [] { return false; }, {}, error);
    return error.isEmpty() ? applyClip (engine, clipIndex, request.mode, *work, std::move (notes)) : -1;
}

std::unique_ptr<juce::Component> makeConvertOptionsForSnapshot (AudioEngine& engine, std::int64_t gridTicks)
{
    return std::make_unique<ConvertOptionsWindow> (engine, nullptr, gridTicks);
}

void convertClipToMidi (AudioEngine& engine, int clipIndex, a2m::Mode mode, std::function<void()> done)
{
    convertClip (engine, clipIndex, Request { mode, std::nullopt }, std::move (done));
}

void convertClipToMidi (AudioEngine& engine, int clipIndex, const a2m::ConvertOptions& options, std::function<void()> done)
{
    convertClip (engine, clipIndex, requestOf (options), std::move (done));
}

void convertAudioToMidi (AudioEngine& engine, std::vector<float> mono, double sampleRate, a2m::Mode mode, const juce::String& name,
                         std::function<void (int)> done)
{
    convertAudio (engine, std::move (mono), sampleRate, Request { mode, std::nullopt }, name, std::move (done));
}

void convertAudioToMidi (AudioEngine& engine, std::vector<float> mono, double sampleRate, const a2m::ConvertOptions& options,
                         const juce::String& name, std::function<void (int)> done)
{
    convertAudio (engine, std::move (mono), sampleRate, requestOf (options), name, std::move (done));
}

void shutdownAudioToMidi()
{
    if (auto* job = ConvertJob::active())
        job->abandon();
}

namespace
{
juce::String fileToCsv (AudioEngine& engine, const juce::File& input, const Request& request, const juce::File& csv, juce::String* timingText)
{
    const auto sample = engine.getSampleLibrary().load (input.getFullPathName());
    if (sample == nullptr || sample->frames() == 0)
        return "can't read " + input.getFullPathName();
    std::vector<float> mono (sample->frames());
    for (std::size_t i = 0; i < mono.size(); ++i)
        mono[i] = sample->isStereo() ? 0.5f * (sample->leftData()[i] + sample->rightData()[i]) : sample->leftData()[i];
    juce::String error;
    basicpitch::Session model;
    if (request.mode != a2m::Mode::drums && ! model.open (error))
        return error;
    double timing[3] {};
    const auto notes = analyse (mono, sample->sampleRate, request, &model, [] { return false; }, {}, error, timing);
    if (error.isNotEmpty())
        return error;
    if (timingText != nullptr)
        *timingText = juce::String (timing[0], 4) + "," + juce::String (timing[1], 4) + "," + juce::String (timing[2], 4);
    juce::String text ("onset_s,offset_s,pitch,velocity\n");
    for (const auto& n : notes)
        text << juce::String (n.start, 4) << "," << juce::String (n.end, 4) << "," << n.key << "," << (int) std::lround (n.velocity * 127.0f) << "\n";
    for (std::size_t i = 0; i < notes.size(); ++i)
        for (const auto& b : notes[i].bends)
            text << "bend," << (int) i << "," << juce::String (b.seconds, 4) << "," << juce::String (b.cents, 1) << "\n";
    return csv.replaceWithText (text) ? juce::String() : "can't write " + csv.getFullPathName();
}
} // namespace

juce::String convertFileToMidiCsv (AudioEngine& engine, const juce::File& input, a2m::Mode mode, const juce::File& csv)
{
    return fileToCsv (engine, input, Request { mode, std::nullopt }, csv, nullptr);
}

juce::String convertFileToMidiCsv (AudioEngine& engine, const juce::File& input, const a2m::ConvertOptions& options, const juce::File& csv,
                                   juce::String* timing)
{
    return fileToCsv (engine, input, requestOf (options), csv, timing);
}
} // namespace lynx
