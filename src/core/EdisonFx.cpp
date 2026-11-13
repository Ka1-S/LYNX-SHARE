#include "core/EdisonFx.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <unordered_map>

#include "core/Instruments.h"
#include "core/Song.h"

namespace lynx::edisonfx
{
namespace ae = audioedit;

// --- parameters ----------------------------------------------------------------------------------------------------
const EffectInfo& info()
{
    static const EffectInfo i { "Edison",
                                { { "Record mode", 1.0f / 3.0f },
                                  { "Append", 1.0f },
                                  { "Mute input", 0.0f },
                                  { "Slave playback to host", 0.0f },
                                  { "Loop", 0.0f },
                                  { "Maximum recording time", 5.0f / 6.0f }, // 30 min (live FL check 190cq, shot edison190cd-05e)
                                  { "Noise threshold", defaultNoiseThreshold },
                                  { "Ignore own selection", 0.0f } } };
    return i;
}

const char* modeName (Mode m) noexcept
{
    switch (m)
    {
        case Mode::now: return "Now";
        case Mode::onInput: return "On input";
        case Mode::input: return "Input";
        case Mode::onPlay: return "On play";
    }
    return "";
}

const char* modeHint (Mode m) noexcept
{
    switch (m)
    {
        case Mode::now: return "Recording starts at once";
        case Mode::onInput: return "Recording starts with the first sound above the threshold";
        case Mode::input: return "Records only while there is sound above the threshold";
        case Mode::onPlay: return "Records only while the song is playing";
    }
    return "";
}

Mode modeOf (const EffectParams& params) noexcept
{
    return (Mode) std::clamp ((int) std::lround (params[recordMode] * 3.0f), 0, modeCount - 1);
}

float modeValue (Mode m) noexcept { return (float) (int) m / 3.0f; }

void setOpenAndRecord (EffectParams& params) noexcept
{
    params[recordMode] = modeValue (Mode::onPlay);
    params[maxTime] = 3.0f / (float) (maxTimeCount - 1); // 5 min
    params[append] = 0.0f;
    params[slave] = 1.0f;
}

int maxTimeIndexOf (const EffectParams& params) noexcept
{
    return std::clamp ((int) std::lround (params[maxTime] * (float) (maxTimeCount - 1)), 0, maxTimeCount - 1);
}

std::int64_t maxFramesOf (const EffectParams& params, double sampleRate) noexcept
{
    return (std::int64_t) std::llround ((double) maxMinutes[maxTimeIndexOf (params)] * 60.0 * sampleRate);
}

std::string paramText (int param, float value)
{
    EffectParams p {};
    switch (param)
    {
        case recordMode:
            p[recordMode] = value;
            return modeName (modeOf (p));
        case maxTime:
        {
            p[maxTime] = value;
            const auto minutes = maxMinutes[maxTimeIndexOf (p)];
            return minutes == 0 ? std::string ("Forever") : std::to_string (minutes) + " min";
        }
        case noiseThreshold:
        {
            if (value <= 0.0f)
                return "-inf dB";
            char text[32];
            std::snprintf (text, sizeof (text), "%.1f dB", 20.0 * std::log10 ((double) value));
            return text;
        }
        case append:
        case muteInput:
        case slave:
        case loopPlay:
        case ignoreOwnSelection:
            return value >= 0.5f ? "on" : "off";
        default: break;
    }
    return {};
}

// --- the record-mode state machine ---------------------------------------------------------------------------------
void Gate::prepare (double sampleRate) noexcept
{
    holdLength = std::max (1, (int) std::lround (sampleRate * holdSeconds));
    preRollLength = std::max (1, (int) std::lround (sampleRate * preRollSeconds));
}

void Gate::arm (Mode mode) noexcept
{
    current = mode;
    hold = 0;
    jumpPending = false;
    state = mode == Mode::now ? Phase::recording : Phase::waiting;
}

void Gate::disarm() noexcept
{
    state = Phase::off;
    hold = 0;
    jumpPending = false;
}

Gate::Decision Gate::block (float peak, int frames, bool hostPlaying, bool hostJumped, float threshold) noexcept
{
    Decision d;
    if (state == Phase::off)
        return d;

    switch (current)
    {
        case Mode::now:
            d.write = true;
            break;

        case Mode::onInput:
        case Mode::input:
        {
            const auto before = hold;
            if (peak > threshold)
                hold = holdLength; // heard: the hold starts again
            else if (current == Mode::input)
                hold = std::max (0, hold - frames); // only "Input" runs out; "On input" goes on once it started
            d.write = hold > 0;
            d.preRoll = d.write && before <= 0;
            break;
        }

        case Mode::onPlay:
        {
            // FL writes a "Song jump" marker where each stretch of playback starts in the take (frame 0 of a new take,
            // the join when it is appended, every restart of the song while armed) and at every jump while playing
            // (live FL check 190cq, shot edison190cd-01-02)
            const auto starts = hostPlaying && state != Phase::recording;
            if ((hostJumped || starts) && hostPlaying)
                jumpPending = true;
            d.write = hostPlaying;
            if (d.write && jumpPending)
            {
                d.jumpMarker = true;
                jumpPending = false;
            }
            break;
        }
    }
    state = d.write ? Phase::recording : Phase::waiting;
    return d;
}

// --- the effect ------------------------------------------------------------------------------------------------------
Processor::Processor() : ringL (ringCapacity), ringR (ringCapacity)
{
    prepare (44100.0);
}

Processor::~Processor() = default;

void Processor::prepare (double sampleRate) noexcept
{
    rate.store (sampleRate > 0.0 ? sampleRate : 44100.0, std::memory_order_relaxed);
    gate.prepare (rate.load (std::memory_order_relaxed));
    preL.assign ((std::size_t) gate.preRollFrames(), 0.0f);
    preR.assign ((std::size_t) gate.preRollFrames(), 0.0f);
    prePos = preCount = 0;
}

void Processor::setRecording (bool on) noexcept
{
    if (on)
    {
        // what an earlier take left behind is dropped (the audio thread is not writing: Record is off)
        readIndex.store (writeIndex.load (std::memory_order_acquire), std::memory_order_release);
        jumpRead.store (jumpWrite.load (std::memory_order_acquire), std::memory_order_release);
        dropped.store (0, std::memory_order_relaxed);
        limitHit.store (false, std::memory_order_relaxed);
    }
    wantRecord.store (on, std::memory_order_release);
    if (! on)
        shownPhase.store ((int) Phase::off, std::memory_order_relaxed);
}

void Processor::write (const float* l, const float* r, int n) noexcept
{
    const auto w = writeIndex.load (std::memory_order_relaxed);
    const auto rd = readIndex.load (std::memory_order_acquire);
    const auto space = (std::int64_t) ringCapacity - (std::int64_t) (w - rd);
    const auto count = (int) std::min<std::int64_t> (n, space);
    const auto mask = (std::uint64_t) ringCapacity - 1;
    for (int i = 0; i < count; ++i)
    {
        ringL[(std::size_t) ((w + (std::uint64_t) i) & mask)] = l[i];
        ringR[(std::size_t) ((w + (std::uint64_t) i) & mask)] = r[i];
    }
    writeIndex.store (w + (std::uint64_t) count, std::memory_order_release);
    written += count;
    if (count < n)
        dropped.fetch_add (n - count, std::memory_order_relaxed);
}

void Processor::keepPreRoll (const float* l, const float* r, int n) noexcept
{
    const auto size = (int) preL.size();
    if (size == 0)
        return;
    const auto from = std::max (0, n - size); // only the last `size` frames of a long block matter
    for (int i = from; i < n; ++i)
    {
        preL[(std::size_t) prePos] = l[i];
        preR[(std::size_t) prePos] = r[i];
        prePos = prePos + 1 == size ? 0 : prePos + 1;
    }
    preCount = std::min (size, preCount + (n - from));
}

void Processor::writePreRoll() noexcept
{
    const auto size = (int) preL.size();
    auto at = prePos - preCount;
    if (at < 0)
        at += size;
    while (preCount > 0)
    {
        const auto run = std::min (preCount, size - at);
        write (preL.data() + at, preR.data() + at, run);
        at = at + run == size ? 0 : at + run;
        preCount -= run;
    }
    prePos = 0;
}

int Processor::drain (std::vector<float>& l, std::vector<float>& r, std::vector<std::int64_t>& jumps, std::int64_t takeFrames)
{
    const auto w = writeIndex.load (std::memory_order_acquire);
    const auto rd = readIndex.load (std::memory_order_relaxed);
    const auto n = (int) (w - rd);
    const auto mask = (std::uint64_t) ringCapacity - 1;
    l.reserve (l.size() + (std::size_t) n);
    r.reserve (r.size() + (std::size_t) n);
    for (int i = 0; i < n; ++i)
    {
        l.push_back (ringL[(std::size_t) ((rd + (std::uint64_t) i) & mask)]);
        r.push_back (ringR[(std::size_t) ((rd + (std::uint64_t) i) & mask)]);
    }
    readIndex.store (w, std::memory_order_release);

    // (a jump is stored as the take frame it happened at, counted by the audio thread from the start of the take)
    const auto jw = jumpWrite.load (std::memory_order_acquire);
    auto jr = jumpRead.load (std::memory_order_relaxed);
    for (; jr != jw; ++jr)
        jumps.push_back (std::min<std::int64_t> (jumpAt[jr % jumpSlots], takeFrames + n));
    jumpRead.store (jr, std::memory_order_release);
    return n;
}

void Processor::play (std::shared_ptr<const ae::Audio> newClip, bool loop, float gain)
{
    const auto stops = newClip == nullptr;
    Source old; // (what the audio thread let go of is freed here)
    while (! tryLock()) {} // (the audio thread holds it for a few assignments)
    old = std::move (retiredClip);
    retiredClip = {};
    pendingClip.audio = std::move (newClip);
    pendingClip.loop = loop;
    pendingClip.gain = gain;
    hasPendingClip = true;
    unlock();
    if (stops)
        shownPosition.store (-1.0, std::memory_order_relaxed);
}

void Processor::setContent (std::shared_ptr<const ae::Audio> newContent, std::int64_t offset)
{
    Source old;
    while (! tryLock()) {}
    old = std::move (retiredContent);
    retiredContent = {};
    pendingContent.audio = std::move (newContent);
    pendingContent.offset = offset;
    hasPendingContent = true;
    unlock();
}

// Adds `s` from `position` (source frames) to the block; 4-point Hermite between frames, the frames themselves when
// the position is whole and the rates match.
void Processor::render (const Source& s, double& position, double step, float* l, float* r, int n, bool stopAtEnd) noexcept
{
    const auto& a = *s.audio;
    const auto frames = a.frames();
    if (frames < 1)
        return;
    const auto* sl = a.channels[0].data();
    const auto* sr = a.numChannels() > 1 ? a.channels[1].data() : sl;
    const auto last = frames - 1;
    auto at = [&] (const float* d, std::int64_t i) noexcept -> float {
        if (s.loop && ! stopAtEnd)
            i = ((i % frames) + frames) % frames;
        return i < 0 || i > last ? 0.0f : d[i];
    };
    for (int i = 0; i < n; ++i)
    {
        if (position >= (double) frames)
        {
            if (stopAtEnd || ! s.loop)
                break;
            position = std::fmod (position, (double) frames);
        }
        if (position >= 0.0)
        {
            const auto whole = (std::int64_t) position;
            const auto frac = (float) (position - (double) whole);
            float yl, yr;
            if (frac == 0.0f)
            {
                yl = sl[whole];
                yr = sr[whole];
            }
            else
            {
                auto hermite = [frac] (float xm1, float x0, float x1, float x2) noexcept {
                    const auto c = (x1 - xm1) * 0.5f;
                    const auto v = x0 - x1;
                    const auto w = c + v;
                    const auto a2 = w + v + (x2 - x0) * 0.5f;
                    const auto b = w + a2;
                    return ((a2 * frac - b) * frac + c) * frac + x0;
                };
                yl = hermite (at (sl, whole - 1), at (sl, whole), at (sl, whole + 1), at (sl, whole + 2));
                yr = hermite (at (sr, whole - 1), at (sr, whole), at (sr, whole + 1), at (sr, whole + 2));
            }
            l[i] += s.gain * yl;
            r[i] += s.gain * yr;
        }
        position += step;
    }
}

void Processor::process (float* left, float* right, int n, const EffectParams& params, double bpm) noexcept
{
    const auto sr = rate.load (std::memory_order_relaxed);
    const auto ticksPerSample = std::max (1.0, bpm) / 60.0 * (double) ppq / sr;
    const auto drift = std::abs (songTicks - expectedTicks);
    const auto continuing = hostPlaying && wasPlaying;
    const auto reseek = ! continuing || drift > 1.0;       // slave playback follows the song position
    const auto jumped = continuing && drift > ppq * 0.25;  // a jump worth a marker (a loop, a click on the timeline)
    expectedTicks = songTicks + (double) n * ticksPerSample;
    wasPlaying = hostPlaying;

    // --- recording: the slot's input, before anything else touches it ---
    const auto want = wantRecord.load (std::memory_order_acquire);
    if (want != recording)
    {
        recording = want;
        if (want)
        {
            gate.arm (modeOf (params));
            written = 0;
            prePos = preCount = 0;
        }
        else
            gate.disarm();
    }
    if (recording)
    {
        if (gate.mode() != modeOf (params))
            gate.arm (modeOf (params));
        float peak = 0.0f;
        for (int i = 0; i < n; ++i)
            peak = std::max (peak, std::max (std::abs (left[i]), std::abs (right[i])));
        if (peak > meter.load (std::memory_order_relaxed))
            meter.store (peak, std::memory_order_relaxed);

        const auto d = gate.block (peak, n, hostPlaying, jumped, std::max (0.0f, params[noiseThreshold]));
        if (d.write)
        {
            if (d.preRoll)
                writePreRoll();
            if (d.jumpMarker)
            {
                const auto jw = jumpWrite.load (std::memory_order_relaxed);
                if (jw - jumpRead.load (std::memory_order_acquire) < (std::uint32_t) jumpSlots)
                {
                    jumpAt[jw % jumpSlots] = written;
                    jumpWrite.store (jw + 1, std::memory_order_release);
                }
            }
            write (left, right, n);
            const auto limit = maxFramesOf (params, sr);
            if (limit > 0 && written >= limit)
            {
                limitHit.store (true, std::memory_order_relaxed);
                wantRecord.store (false, std::memory_order_release);
                recording = false;
                gate.disarm();
            }
        }
        else if (gate.mode() == Mode::onInput || gate.mode() == Mode::input)
            keepPreRoll (left, right, n);
        shownPhase.store ((int) gate.phase(), std::memory_order_relaxed);
    }

    // --- the input goes on, or not ---
    if (params[muteInput] >= 0.5f)
    {
        std::fill (left, left + n, 0.0f);
        std::fill (right, right + n, 0.0f);
    }

    // --- playback, added to it ---
    if (tryLock())
    {
        if (hasPendingClip && retiredClip.audio == nullptr)
        {
            retiredClip = std::move (clip);
            clip = std::move (pendingClip);
            pendingClip = {};
            hasPendingClip = false;
            clipPosition = 0.0;
        }
        if (hasPendingContent && retiredContent.audio == nullptr)
        {
            retiredContent = std::move (content);
            content = std::move (pendingContent);
            pendingContent = {};
            hasPendingContent = false;
            slaveEngaged = false;
        }
        unlock();
    }

    const auto slaved = params[slave] >= 0.5f && hostPlaying && content.audio != nullptr && content.audio->frames() > 0;
    if (slaved)
    {
        const auto& a = *content.audio;
        if (! slaveEngaged || reseek)
            slavePosition = songTicks / ((double) ppq * std::max (1.0, bpm) / 60.0) * a.sampleRate
                            + (params[ignoreOwnSelection] >= 0.5f ? 0.0 : (double) content.offset);
        slaveEngaged = true;
        render (content, slavePosition, a.sampleRate / sr, left, right, n, true);
        shownPosition.store (slavePosition < (double) a.frames() ? slavePosition : -1.0, std::memory_order_relaxed);
        return;
    }
    slaveEngaged = false;

    if (clip.audio != nullptr && clip.audio->frames() > 0)
    {
        const auto frames = (double) clip.audio->frames();
        if (clip.loop || clipPosition < frames)
        {
            render (clip, clipPosition, clip.audio->sampleRate / sr, left, right, n, false);
            shownPosition.store (clip.loop || clipPosition < frames ? clipPosition : -1.0, std::memory_order_relaxed);
            return;
        }
    }
    shownPosition.store (-1.0, std::memory_order_relaxed);
}

// --- the slot's document -------------------------------------------------------------------------------------------
ae::Document withTake (const ae::Document& base, const ae::Audio& take, const std::vector<std::int64_t>& jumps, bool appendTake)
{
    ae::Document d;
    std::int64_t at = 0;
    double scale = 1.0;
    if (appendTake && ! base.audio.empty())
    {
        d = base;
        at = d.audio.frames();
        scale = base.audio.sampleRate / std::max (1.0, take.sampleRate);
        ae::insert (d, at, take); // (converted to the document's format)
    }
    else
    {
        d.title = base.title;
        d.audio = take;
    }
    for (const auto j : jumps)
        ae::addMarker (d, std::clamp<std::int64_t> (at + (std::int64_t) std::llround ((double) j * scale), 0, d.audio.frames()), "Song jump");
    return d;
}

namespace
{
std::mutex& registryMutex()
{
    static std::mutex m;
    return m;
}

std::unordered_map<std::uint64_t, std::weak_ptr<Session>>& registry()
{
    static std::unordered_map<std::uint64_t, std::weak_ptr<Session>> map;
    return map;
}

std::shared_ptr<const ae::Audio> audioOf (const std::shared_ptr<const ae::Document>& doc)
{
    return std::shared_ptr<const ae::Audio> (doc, &doc->audio); // (shares the document: nothing is copied)
}
} // namespace

Session::Session (std::uint64_t instanceId) : instance (instanceId), doc (std::make_shared<const ae::Document>()) {}

void Session::changedDocument()
{
    ++rev;
    const std::lock_guard<std::mutex> guard (registryMutex());
    players.erase (std::remove_if (players.begin(), players.end(), [] (const std::weak_ptr<Processor>& p) { return p.expired(); }), players.end());
    for (const auto& weak : players)
        if (const auto p = weak.lock())
            p->setContent (audioOf (doc), slaveOffset);
}

void Session::setDocument (ae::Document d)
{
    doc = std::make_shared<const ae::Document> (std::move (d));
    changed = true;
    changedDocument();
}

void Session::setSlaveOffset (std::int64_t selectionStart)
{
    const auto offset = std::max<std::int64_t> (0, selectionStart);
    if (offset == slaveOffset)
        return;
    slaveOffset = offset;
    const std::lock_guard<std::mutex> guard (registryMutex());
    for (const auto& weak : players)
        if (const auto p = weak.lock())
            p->setContent (audioOf (doc), slaveOffset);
}

void Session::attach (const std::shared_ptr<Processor>& p)
{
    const std::lock_guard<std::mutex> guard (registryMutex());
    players.push_back (p);
    p->setContent (audioOf (doc), slaveOffset);
}

void Session::load (const std::string& path, const std::function<std::shared_ptr<const SampleData> (const std::string&)>& fallback)
{
    ae::Document d;
    if (! readWavFile (path, d))
    {
        d = {};
        if (const auto sample = fallback ? fallback (path) : nullptr; sample != nullptr && ! sample->left.empty())
        {
            d.audio.sampleRate = sample->sampleRate;
            d.audio.channels.push_back (sample->left);
            if (sample->right.size() == sample->left.size())
                d.audio.channels.push_back (sample->right);
            d.loopStart = sample->loopStart;
            d.loopEnd = sample->loopEnd;
        }
    }
    doc = std::make_shared<const ae::Document> (std::move (d));
    file = path;
    changed = false;
    changedDocument();
}

void Session::loadFlState (const std::vector<std::uint8_t>& state)
{
    ae::Document d;
    if (const auto parsed = parseFlState (state); parsed && parsed->wavSize > 0)
        if (! ae::decodeWav (state.data() + parsed->wavOffset, parsed->wavSize, d))
            d = {};
    doc = std::make_shared<const ae::Document> (std::move (d));
    file.clear();
    changed = false;
    changedDocument();
}

bool Session::save (const std::string& path)
{
    if (doc->audio.empty())
    {
        file.clear();
        changed = false;
        return true;
    }
    if (! writeWavFile (*doc, path))
        return false;
    file = path;
    changed = false;
    return true;
}

bool Session::start (const std::shared_ptr<Processor>& p, bool appendTake)
{
    if (p == nullptr || active)
        return false;
    recorder = p;
    appending = appendTake;
    takeRate = p->sampleRate();
    takeL.clear();
    takeR.clear();
    jumps.clear();
    lost = 0;
    active = true;
    p->play (nullptr, false);
    p->setRecording (true);
    return true;
}

bool Session::pump()
{
    if (! active)
        return false;
    const auto p = recorder.lock();
    if (p == nullptr)
    {
        stop();
        return false;
    }
    p->drain (takeL, takeR, jumps, takeFrames());
    lost = p->droppedFrames();
    if (! p->isRecording()) // (the maximum recording time)
    {
        stop();
        return false;
    }
    return true;
}

void Session::stop()
{
    if (! active)
        return;
    active = false;
    if (const auto p = recorder.lock())
    {
        p->setRecording (false);
        p->drain (takeL, takeR, jumps, takeFrames());
        lost = p->droppedFrames();
    }
    recorder.reset();
    if (! takeL.empty())
    {
        doc = std::make_shared<const ae::Document> (withTake (*doc, take(), jumps, appending));
        changed = true;
        changedDocument();
    }
    takeL.clear();
    takeR.clear();
    jumps.clear();
}

Phase Session::phase() const
{
    if (! active)
        return Phase::off;
    const auto p = recorder.lock();
    return p != nullptr ? p->phase() : Phase::off;
}

ae::Audio Session::take() const
{
    ae::Audio a;
    a.sampleRate = takeRate;
    a.channels = { takeL, takeR };
    return a;
}

ae::Document Session::preview() const
{
    return active && ! takeL.empty() ? withTake (*doc, take(), jumps, appending) : *doc;
}

std::int64_t Session::takeStart() const noexcept
{
    return active && appending ? doc->audio.frames() : 0;
}

std::shared_ptr<Session> findSession (std::uint64_t instanceId)
{
    const std::lock_guard<std::mutex> guard (registryMutex());
    const auto it = registry().find (instanceId);
    return it != registry().end() ? it->second.lock() : nullptr;
}

std::shared_ptr<Session> sessionFor (const EffectSlot& slot, const std::function<std::shared_ptr<const SampleData> (const std::string&)>& fallback)
{
    if (auto found = findSession (slot.instance.id))
        return found;
    auto made = std::make_shared<Session> (slot.instance.id);
    if (! slot.samplePath.empty())
        made->load (slot.samplePath, fallback);
    else if (! slot.state.empty())
        made->loadFlState (slot.state);
    const std::lock_guard<std::mutex> guard (registryMutex());
    auto& entry = registry()[slot.instance.id];
    if (auto raced = entry.lock())
        return raced;
    entry = made;
    return made;
}

std::vector<std::shared_ptr<Session>> sessions()
{
    std::vector<std::shared_ptr<Session>> out;
    const std::lock_guard<std::mutex> guard (registryMutex());
    for (auto it = registry().begin(); it != registry().end();)
    {
        if (auto s = it->second.lock())
        {
            out.push_back (std::move (s));
            ++it;
        }
        else
            it = registry().erase (it);
    }
    return out;
}

void followPath (const EffectSlot& slot, const std::function<std::shared_ptr<const SampleData> (const std::string&)>& fallback)
{
    const auto s = findSession (slot.instance.id);
    if (s == nullptr || s->path() == slot.samplePath || s->dirty() || s->recording())
        return;
    // An editor that holds audio is the truth while it lives: a song undo that goes back past a recording, or a path
    // that cannot be read, must not take the audio out of the open editor. The slot is pointed at the session's file
    // again by the next flush (AudioEngine::flushEdisonAudio). Only an empty session reads what the slot names
    // (a relocated file, an undo that brings a recording back).
    if (! s->document().audio.empty())
        return;
    if (! slot.samplePath.empty())
        s->load (slot.samplePath, fallback);
    else if (! slot.state.empty())
        s->loadFlState (slot.state);
    else
    {
        s->setDocument ({});
        s->save ({}); // (an empty document: nothing to keep, nothing unsaved)
    }
}

std::shared_ptr<Effect> make (const EffectSlot& slot, double sampleRate, const std::function<std::shared_ptr<const SampleData> (const std::string&)>& fallback)
{
    // the processor keeps its session alive: the audio of a slot lives as long as an effect or a window of it does
    struct Holder final
    {
        Processor processor;
        std::shared_ptr<Session> session;
    };
    auto holder = std::make_shared<Holder>();
    holder->processor.prepare (sampleRate);
    holder->session = sessionFor (slot, fallback);
    auto processor = std::shared_ptr<Processor> (holder, &holder->processor);
    holder->session->attach (processor);
    return processor;
}

// --- files -----------------------------------------------------------------------------------------------------------
bool writeWavFile (const ae::Document& doc, const std::string& path)
{
    const auto bytes = ae::encodeWav (doc, ae::WavFormat::float32);
    std::ofstream out (path, std::ios::binary | std::ios::trunc);
    if (! out)
        return false;
    out.write ((const char*) bytes.data(), (std::streamsize) bytes.size());
    return out.good();
}

bool readWavFile (const std::string& path, ae::Document& out)
{
    std::ifstream in (path, std::ios::binary | std::ios::ate);
    if (! in)
        return false;
    const auto size = (std::size_t) in.tellg();
    if (size < 44)
        return false;
    std::vector<std::uint8_t> bytes (size);
    in.seekg (0);
    in.read ((char*) bytes.data(), (std::streamsize) size);
    return in.good() && ae::decodeWav (bytes.data(), bytes.size(), out);
}

// --- FL's saved state ------------------------------------------------------------------------------------------------
std::optional<FlState> parseFlState (const std::vector<std::uint8_t>& state)
{
    auto u32 = [&state] (std::size_t at) {
        std::uint32_t v = 0;
        std::memcpy (&v, state.data() + at, 4);
        return v;
    };
    if (state.size() < 4 + 8 + 4)
        return std::nullopt;
    FlState s;
    auto version = u32 (0);
    if (version >= 0xd0004u)
        return std::nullopt; // newer than the layout read here
    if (version > 0xffffu)
        version >>= 16;
    s.version = (int) version;

    std::size_t p = 4 + (version < 5 ? 8u : 12u);
    std::uint64_t size = 0;
    if (version < 13)
    {
        if (p + 4 > state.size())
            return std::nullopt;
        size = u32 (p);
        p += 4;
    }
    else
    {
        if (p + 8 > state.size())
            return std::nullopt;
        std::memcpy (&size, state.data() + p, 8);
        p += 8;
    }
    if (size > state.size() - p)
        return std::nullopt;
    if (size >= 12 && std::memcmp (state.data() + p, "RIFF", 4) == 0)
    {
        s.wavOffset = p;
        s.wavSize = (std::size_t) size;
    }
    p += (std::size_t) size;

    // the sample's name, then selection (2 doubles), view (2 doubles) and the noise threshold: read when the name is
    // empty (one zero byte) — how a longer name is stored was not established
    if (version >= 7 && p + 1 + 36 <= state.size() && state[p] == 0)
    {
        float threshold = 0.0f;
        std::memcpy (&threshold, state.data() + p + 1 + 32, 4);
        if (std::isfinite (threshold) && threshold >= 0.0f && threshold <= 1.0f)
            s.noiseThreshold = threshold;
    }

    // the switches are the last bytes of the state: 20 since version 6, 12 before (8 in version 1)
    const std::size_t tail = version < 2 ? 8u : version < 6 ? 12u : 20u;
    if (state.size() < p + tail)
        return s;
    const auto t = state.size() - tail;
    s.mode = (Mode) std::clamp ((int) u32 (t), 0, modeCount - 1);
    s.newProject = state[t + 4] != 0;
    s.loop = state[t + 5] != 0;
    s.muteInput = state[t + 6] != 0;
    s.noAutoScroll = state[t + 7] != 0;
    if (tail >= 12)
    {
        auto index = (int) u32 (t + 8);
        if (version < 11) // minutes then: the first choice that long
        {
            const auto minutes = index;
            index = maxTimeCount - 1;
            for (int i = maxTimeCount - 1; i >= 0; --i)
                if (minutes <= maxMinutes[i])
                    index = i;
        }
        s.maxTime = std::clamp (index, 0, maxTimeCount - 1);
    }
    if (tail >= 20)
    {
        s.slave = state[t + 16] != 0;
        s.ignoreHostSelection = state[t + 17] != 0;
        s.ignoreOwnSelection = state[t + 18] != 0;
    }
    return s;
}

void toParams (const FlState& s, EffectParams& params)
{
    params[recordMode] = modeValue (s.mode);
    params[append] = s.newProject ? 0.0f : 1.0f;
    params[muteInput] = s.muteInput ? 1.0f : 0.0f;
    params[slave] = s.slave ? 1.0f : 0.0f;
    params[loopPlay] = s.loop ? 1.0f : 0.0f;
    params[maxTime] = (float) s.maxTime / (float) (maxTimeCount - 1);
    params[noiseThreshold] = s.noiseThreshold;
    params[ignoreOwnSelection] = s.ignoreOwnSelection ? 1.0f : 0.0f;
}
} // namespace lynx::edisonfx
