#pragma once

// LYNX's video decoding (round 174d, docs/analise/REA_rodada174d.md §3), shared by the Video Player (round 165g) and
// the video tracks. The decoders, tried in order:
//   1. the operating system's own (built in, nothing to install): Windows Media Foundation (ui/VideoMediaWindows.cpp),
//      macOS AVFoundation (ui/VideoMediaMac.mm);
//   2. the ffmpeg the user installed (the program, found as for the video export: ui/VisualEditors findFfmpeg), for
//      Linux and for the formats the system cannot read (DNxHR, MXF …).
// Threads: frames are decoded on a FrameDecoder's own thread, the sound tracks on a pool thread; the audio thread never
// waits for any of it, and a decoder that falls behind drops frames instead of stalling.

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/VideoPlayer.h"

namespace lynx::media
{
// --- the native decoders -----------------------------------------------------------------------------------------
// One open file, decoded on one thread at a time.
class Reader
{
public:
    virtual ~Reader() = default;
    virtual video::Info info() const = 0;
    // the next frame read is the one at or before `seconds` (or a key frame before it: next() then skips forward)
    virtual bool seek (double seconds) = 0;
    // decodes the next frame: BGRA, rows top-down, w × h (its own size, which may change), and its time in seconds
    virtual bool next (std::vector<std::uint8_t>& bgra, int& w, int& h, double& time) = 0;
};

// The system's decoder for a file (nullptr: none on this system, or it cannot read the file)
std::unique_ptr<Reader> openNative (const juce::File& file);
// The system's decoding of the file's sound track: interleaved stereo int16 at `rate` (nullptr: none / no sound)
std::shared_ptr<const video::AudioTrack> decodeNativeAudio (const juce::File& file, double rate);
// "Media Foundation", "AVFoundation", or empty when this system has none
juce::String nativeBackendName();

// --- what a file holds ---------------------------------------------------------------------------------------------
struct Probe
{
    juce::String report;  // ffmpeg's report (empty when the system's decoder read the file)
    video::Info info;
    juce::String backend; // the decoder that reads it: nativeBackendName(), "ffmpeg", or empty (none)
};
Probe probe (const juce::String& path); // cached per path
// Forgets the probes and decoded sound tracks (after locating ffmpeg)
void clearCaches();
// Whether any decoder can be used at all on this machine (the system's, or ffmpeg)
bool canDecodeVideo();

// How to install ffmpeg on this system (Windows / macOS / the Linux distributions), several lines, translated
juce::String ffmpegInstallHelp (bool forExport = false); // (forExport: the first line asks to export again)
// The message for a file that no decoder could read (no ffmpeg / ffmpeg could not read it / the system could not)
juce::String cannotReadMessage (const juce::String& path);

// --- the sound tracks (for the Video Player's instrument and the video tracks) --------------------------------------
constexpr double trackRate = 48000.0;
// The track of `path` (UTF-8): decoded once in the background (`wait` = now, for an export); `ready` runs on the
// message thread when a background decode finishes
std::shared_ptr<const video::AudioTrack> provideTrack (const std::string& path, bool wait, std::function<void()> ready = {});
// Writes the file's sound track to a 16-bit stereo WAV (blocking; any thread). False: no sound / no decoder.
bool extractAudioToWav (const juce::File& video, const juce::File& wav);

// --- the frames ------------------------------------------------------------------------------------------------------
// Decodes the frame the transport is at, at the shown size, on its own thread. The system decoder seeks to the frame
// (scrubbing) and decodes ahead while playing (late frames are skipped); with ffmpeg one process per stretch of
// playback and one per still frame.
class FrameDecoder final : public juce::Thread
{
public:
    FrameDecoder();
    ~FrameDecoder() override;

    void setSource (const juce::String& path, const video::Info& info);
    void setSize (int w, int h);                // the shown size in pixels (≤ 3840 × 2160)
    void request (double seconds, bool moving); // the time to show and whether it moves
    juce::Image frame (double& time);           // the latest frame (UI thread) and its time
    int frameCount() const noexcept { return frames.load(); }
    int droppedCount() const noexcept { return dropped.load(); }

    void run() override;

private:
    void runNative (Reader& reader, const juce::String& p);
    void runFfmpeg (const juce::String& entry);
    void startProcess (const juce::String& p, double t, int w, int h, double fps, bool moving);
    void stopProcess();
    bool readFrame (std::vector<std::uint8_t>& bytes);
    void publish (const std::uint8_t* bgra, int srcW, int srcH, int w, int h, double t);

    std::mutex want, out, procLock;
    juce::String path;
    video::Info info;
    int width = 0, height = 0;
    double wantTime = 0.0;
    bool wantMoving = false, restart = false;
    std::unique_ptr<juce::ChildProcess> process;
    juce::Image shown;
    std::array<juce::Image, 3> pool;
    double shownTime = -1.0, lastShown = -1e9;
    std::atomic<int> frames { 0 }, dropped { 0 };
};

// Scales BGRA rows (area average when shrinking, nearest when growing); dst is w × h × 4
void scaleBgra (const std::uint8_t* src, int srcW, int srcH, int srcStride, std::uint8_t* dst, int w, int h, int dstStride) noexcept;
} // namespace lynx::media
