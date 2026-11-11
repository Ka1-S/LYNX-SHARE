#pragma once

// The plugin sandbox's wire format (round 176a: split out of PluginSandbox.cpp so the 32-bit bridge, LYNX-bridge32.exe,
// builds the same child side). Every field has a fixed size, so a 64-bit LYNX and a 32-bit child share one layout.

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <algorithm>

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <cstddef>

#if JUCE_WINDOWS
 #include <windows.h>
#elif JUCE_LINUX || JUCE_BSD || JUCE_MAC
 #include <cerrno>
 #include <ctime>
 #include <fcntl.h>
 #include <semaphore.h>
 #include <sys/mman.h>
 #include <unistd.h>
 #if JUCE_MAC
  #include <poll.h>
  #include <sys/event.h>
  #include <sys/stat.h>
  #include <thread>
 #endif
 #if JUCE_LINUX
  #include <poll.h>
  #include <sys/eventfd.h>
  #include <sys/syscall.h>
  #include <thread>
  #include <dlfcn.h>
 #endif
 #define LYNX_POSIX_SANDBOX 1
#endif

namespace lynx::sandboxipc
{
inline int processTag()
{
    static const int tag = juce::Random::getSystemRandom().nextInt (1000000);
    return tag;
}

// diagnostics: LYNX_SANDBOX_LOG=file logs the steps (one file per process)
inline void trace (const juce::String& what)
{
    if (const auto path = juce::SystemStats::getEnvironmentVariable ("LYNX_SANDBOX_LOG", {}); path.isNotEmpty())
        juce::File (path + "." + juce::String (processTag())).appendText (juce::String (juce::Time::getMillisecondCounter()) + " " + what + "\n");
}

#if JUCE_WINDOWS || LYNX_POSIX_SANDBOX
// Block shared between LYNX and the child. The parent writes the input and signals `request`; the child
// processes in place and signals `response`.
struct SharedBlock
{
    static constexpr std::uint32_t magicValue = 0x4C594E58; // "LYNX"
    // (32 channels: a bridged multi-output instrument keeps 16 stereo outputs for the wrapper's Connections, issue #114)
    static constexpr int maxChannels = 32, maxFrames = 4096 /* PluginHost::maxBlockSize */, maxMidi = 1024, maxParams = 512;

    struct Midi
    {
        std::int32_t offset;
        std::uint8_t size, bytes[3];
    };
    struct Param
    {
        std::int32_t index;
        float value;
    };

    std::uint32_t magic;
    std::int32_t frames, channels, midiCount, paramCount, paramOutCount, latency;
    // round 168s (issue #32): before this block, the worker switches the plugin to `mode` (1 real time, 2 offline: an
    // export renders; 0 = unchanged; it is prepared again so a VST3 takes it) and resets it when `resetFirst` is set
    std::int32_t mode, resetFirst;
    // round 176a: the host's position for this block (the plugin's play head in the child: a bridged plugin had none,
    // so a tempo-synced delay ran at 120 BPM and an arpeggiator never saw the transport)
    struct alignas (8) Transport
    {
        enum : std::int32_t { valid = 1, playing = 2, looping = 4, recording = 8, hasPpq = 16, hasBar = 32, hasLoop = 64,
                              hasSamples = 128, hasBpm = 256, hasSignature = 512 };
        double bpm, ppq, barStart, loopStart, loopEnd;
        std::int64_t samples;
        std::int32_t numerator, denominator, flags, spare;
    } transport;
    Midi midi[maxMidi];
    Param params[maxParams], paramsOut[maxParams];
    float audio[maxChannels][maxFrames];
};
// one layout for the 64-bit LYNX and the 32-bit bridge (x86 MinGW aligns doubles to 8 as x64 does; alignas pins it)
static_assert (offsetof (SharedBlock, transport) == 40 && sizeof (SharedBlock::Transport) == 64, "SharedBlock layout");
static_assert (offsetof (SharedBlock, midi) == 104 && sizeof (SharedBlock::Midi) == 8 && sizeof (SharedBlock::Param) == 8, "SharedBlock layout");

// SharedBlock::Transport from a play head's position, and back (the parent writes it, the child's play head reads it)
inline void writeTransport (SharedBlock::Transport& t, const juce::Optional<juce::AudioPlayHead::PositionInfo>& position) noexcept
{
    t = {};
    if (! position.hasValue())
        return;
    using T = SharedBlock::Transport;
    t.flags = T::valid | (position->getIsPlaying() ? T::playing : 0) | (position->getIsLooping() ? T::looping : 0)
            | (position->getIsRecording() ? T::recording : 0);
    if (const auto v = position->getBpm()) { t.bpm = *v; t.flags |= T::hasBpm; }
    if (const auto v = position->getPpqPosition()) { t.ppq = *v; t.flags |= T::hasPpq; }
    if (const auto v = position->getPpqPositionOfLastBarStart()) { t.barStart = *v; t.flags |= T::hasBar; }
    if (const auto v = position->getLoopPoints()) { t.loopStart = v->ppqStart; t.loopEnd = v->ppqEnd; t.flags |= T::hasLoop; }
    if (const auto v = position->getTimeInSamples()) { t.samples = *v; t.flags |= T::hasSamples; }
    if (const auto v = position->getTimeSignature()) { t.numerator = v->numerator; t.denominator = v->denominator; t.flags |= T::hasSignature; }
}

inline juce::Optional<juce::AudioPlayHead::PositionInfo> readTransport (const SharedBlock::Transport& t) noexcept
{
    using T = SharedBlock::Transport;
    if ((t.flags & T::valid) == 0)
        return {};
    juce::AudioPlayHead::PositionInfo p;
    p.setIsPlaying ((t.flags & T::playing) != 0);
    p.setIsLooping ((t.flags & T::looping) != 0);
    p.setIsRecording ((t.flags & T::recording) != 0);
    if ((t.flags & T::hasBpm) != 0) p.setBpm (t.bpm);
    if ((t.flags & T::hasPpq) != 0) p.setPpqPosition (t.ppq);
    if ((t.flags & T::hasBar) != 0) p.setPpqPositionOfLastBarStart (t.barStart);
    if ((t.flags & T::hasLoop) != 0) p.setLoopPoints (juce::AudioPlayHead::LoopPoints { t.loopStart, t.loopEnd });
    if ((t.flags & T::hasSamples) != 0) p.setTimeInSamples (t.samples);
    if ((t.flags & T::hasSignature) != 0) p.setTimeSignature (juce::AudioPlayHead::TimeSignature { t.numerator, t.denominator });
    return p;
}

// The shared block plus the two "doorbells" (request: parent -> child, response: child -> parent).
// Windows: a named file mapping and two auto-reset events. Linux/BSD (round 161): a POSIX shared-memory object
// (/dev/shm) holding the block and two process-shared semaphores. macOS: the same shared memory, but it has no
// process-shared sem_init and no timed sem_wait, so each doorbell is a named FIFO (one byte per ring, poll to wait).
class SandboxChannel
{
public:
    SandboxChannel() = default;
    SandboxChannel (const SandboxChannel&) = delete;
    SandboxChannel& operator= (const SandboxChannel&) = delete;
    ~SandboxChannel() { close(); }

    // parent: creates the objects under `base`
    bool create (const juce::String& base)
    {
        name = base;
       #if JUCE_WINDOWS
        mapping = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD) sizeof (SharedBlock),
                                      wide (objectName ("-mem")).c_str());
        if (mapping != nullptr)
            shared = static_cast<SharedBlock*> (MapViewOfFile (mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (SharedBlock)));
        request = CreateEventW (nullptr, FALSE, FALSE, wide (objectName ("-req")).c_str());
        response = CreateEventW (nullptr, FALSE, FALSE, wide (objectName ("-resp")).c_str());
        if (shared == nullptr || request == nullptr || response == nullptr)
            return false;
        std::memset (shared, 0, sizeof (SharedBlock));
       #else
        const auto path = shmPath();
        ::shm_unlink (path.toRawUTF8()); // a leftover of a crashed LYNX with the same pid
        const int fd = ::shm_open (path.toRawUTF8(), O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd < 0)
            return false;
        owner = true;
        const bool sized = ::ftruncate (fd, (off_t) sizeof (Posix)) == 0;
        void* p = sized ? ::mmap (nullptr, sizeof (Posix), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0) : MAP_FAILED;
        ::close (fd);
        if (p == MAP_FAILED)
            return false;
        posix = static_cast<Posix*> (p);
        std::memset (&posix->block, 0, sizeof (SharedBlock));
       #if JUCE_MAC
        for (const auto* suffix : { "-req", "-resp" })
        {
            const auto fifo = fifoPath (suffix);
            ::unlink (fifo.toRawUTF8()); // (a leftover of a crashed LYNX with the same pid)
            if (::mkfifo (fifo.toRawUTF8(), 0600) != 0)
                return false;
        }
        if (! openFifos())
            return false;
       #else
        if (::sem_init (&posix->request, 1, 0) != 0 || ::sem_init (&posix->response, 1, 0) != 0)
            return false;
        semaphores = true;
       #endif
        shared = &posix->block;
       #endif
        shared->magic = SharedBlock::magicValue;
        return true;
    }

    // child: opens the parent's objects
    bool open (const juce::String& base)
    {
        name = base;
       #if JUCE_WINDOWS
        mapping = OpenFileMappingW (FILE_MAP_ALL_ACCESS, FALSE, wide (objectName ("-mem")).c_str());
        if (mapping != nullptr)
            shared = static_cast<SharedBlock*> (MapViewOfFile (mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (SharedBlock)));
        request = OpenEventW (SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, wide (objectName ("-req")).c_str());
        response = OpenEventW (SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, wide (objectName ("-resp")).c_str());
        if (request == nullptr || response == nullptr)
            return false;
       #else
        const int fd = ::shm_open (shmPath().toRawUTF8(), O_RDWR, 0600);
        if (fd < 0)
            return false;
        void* p = ::mmap (nullptr, sizeof (Posix), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        ::close (fd);
        if (p == MAP_FAILED)
            return false;
        posix = static_cast<Posix*> (p);
        shared = &posix->block;
       #if JUCE_MAC
        if (! openFifos())
            return false;
       #endif
       #endif
        return shared != nullptr && shared->magic == SharedBlock::magicValue;
    }

    // parent, once the child has mapped the block: the name is no longer needed (nothing left in /dev/shm if LYNX dies)
    void unlinkName()
    {
       #if LYNX_POSIX_SANDBOX
        if (owner && ! unlinked)
        {
            unlinked = ::shm_unlink (shmPath().toRawUTF8()) == 0 || errno == ENOENT;
           #if JUCE_MAC
            ::unlink (fifoPath ("-req").toRawUTF8()); // (both sides hold the FIFOs open: the names can go)
            ::unlink (fifoPath ("-resp").toRawUTF8());
           #endif
        }
       #endif
    }

    SharedBlock* block() const noexcept { return shared; }

    void signalRequest() noexcept { post (true); }
    void signalResponse() noexcept { post (false); }
    bool waitRequest (int timeoutMs) noexcept { return wait (true, timeoutMs); }
    bool waitResponse (int timeoutMs) noexcept { return wait (false, timeoutMs); }

private:
   #if JUCE_WINDOWS
    juce::String objectName (const char* suffix) const { return "Local\\" + name + suffix; }
    static std::wstring wide (const juce::String& s) { return std::wstring (s.toWideCharPointer()); }

    void post (bool req) noexcept { SetEvent (req ? request : response); }
    bool wait (bool req, int timeoutMs) noexcept { return WaitForSingleObject (req ? request : response, (DWORD) timeoutMs) == WAIT_OBJECT_0; }

    void close()
    {
        if (shared != nullptr)
            UnmapViewOfFile (shared);
        for (auto* h : { mapping, request, response })
            if (h != nullptr)
                CloseHandle (h);
        shared = nullptr;
        mapping = request = response = nullptr;
    }

    HANDLE mapping = nullptr, request = nullptr, response = nullptr;
   #else
    struct Posix
    {
        sem_t request, response;
        SharedBlock block;
    };

    juce::String shmPath() const { return "/" + name; } // (macOS: at most 31 characters; "/LYNX-sandbox-<pid>-<n>" fits)

   #if JUCE_MAC
    juce::String fifoPath (const char* suffix) const
    {
        return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name + suffix).getFullPathName();
    }

    // O_RDWR: opening never blocks waiting for the other side, and a FIFO with no other writer never reads as closed
    bool openFifos()
    {
        requestFd = ::open (fifoPath ("-req").toRawUTF8(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        responseFd = ::open (fifoPath ("-resp").toRawUTF8(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        return requestFd >= 0 && responseFd >= 0;
    }

    void post (bool req) noexcept
    {
        const char ring = 1;
        juce::ignoreUnused (::write (req ? requestFd : responseFd, &ring, 1));
    }

    bool wait (bool req, int timeoutMs) noexcept
    {
        pollfd p { req ? requestFd : responseFd, POLLIN, 0 };
        for (;;)
        {
            const auto n = ::poll (&p, 1, timeoutMs);
            if (n > 0)
            {
                char ring;
                return ::read (p.fd, &ring, 1) == 1;
            }
            if (n == 0 || errno != EINTR)
                return false;
        }
    }
   #else
    void post (bool req) noexcept { ::sem_post (req ? &posix->request : &posix->response); }

    bool wait (bool req, int timeoutMs) noexcept
    {
        auto* sem = req ? &posix->request : &posix->response;
        timespec until {};
        ::clock_gettime (CLOCK_MONOTONIC, &until);
        until.tv_sec += timeoutMs / 1000;
        until.tv_nsec += (long) (timeoutMs % 1000) * 1000000L;
        if (until.tv_nsec >= 1000000000L)
        {
            ++until.tv_sec;
            until.tv_nsec -= 1000000000L;
        }
        for (;;)
        {
            if (::sem_clockwait (sem, CLOCK_MONOTONIC, &until) == 0)
                return true;
            if (errno != EINTR)
                return false;
        }
    }
   #endif

    void close()
    {
       #if JUCE_MAC
        for (auto* fd : { &requestFd, &responseFd })
            if (*fd >= 0)
            {
                ::close (*fd);
                *fd = -1;
            }
       #endif
        if (posix != nullptr)
        {
           #if ! JUCE_MAC // (deprecated there, and never initialised: the doorbells are FIFOs)
            if (semaphores)
            {
                ::sem_destroy (&posix->request);
                ::sem_destroy (&posix->response);
            }
           #endif
            ::munmap (posix, sizeof (Posix));
        }
        unlinkName();
        posix = nullptr;
        shared = nullptr;
    }

    Posix* posix = nullptr;
    bool owner = false, unlinked = false, semaphores = false;
   #if JUCE_MAC
    int requestFd = -1, responseFd = -1;
   #endif
   #endif
    juce::String name;
    SharedBlock* shared = nullptr;
};


inline juce::MemoryBlock encode (const juce::ValueTree& message)
{
    juce::MemoryOutputStream out;
    message.writeToStream (out);
    return out.getMemoryBlock();
}


// Parameter-change queue with multiple producers (UI and audio thread) and one consumer.
class ParamQueue
{
public:
    void push (int index, float value) noexcept
    {
        const juce::SpinLock::ScopedLockType lock (spin);
        if (count < (int) items.size())
            items[(std::size_t) count++] = { index, value };
    }

    int drain (SharedBlock::Param* out, int max) noexcept
    {
        const juce::SpinLock::ScopedLockType lock (spin);
        const auto n = std::min (count, max);
        std::memcpy (out, items.data(), sizeof (SharedBlock::Param) * (std::size_t) n);
        std::memmove (items.data(), items.data() + n, sizeof (SharedBlock::Param) * (std::size_t) (count - n));
        count -= n;
        return n;
    }

private:
    juce::SpinLock spin;
    std::array<SharedBlock::Param, 4096> items {};
    int count = 0;
};

// ---------------------------------------------------------------------------
// LYNX side: connection to the child, shared memory and events.
#endif
} // namespace lynx::sandboxipc
