#pragma once

// Round 190rm (0.1.32 import / export sweep): one checked way to write a file.
//
// JUCE's File::replaceWithData and File::replaceWithText write a temporary file, IGNORE whether that write worked, and
// then move it over the target (juce_File.cpp: appendData / appendText's result is dropped). On a full disk, a quota, a
// failing or disconnected drive they return true and leave a truncated file where the good one was: File › Save of a
// 784 KB project on a full disk left 64 KB of broken JSON in its place (0.1.30, 0.1.31, 0.1.31.2 alike), and the .flp,
// FL zip, DAWproject, zipped package and FLAC / MP3 / AIFF exports reported success on cut files.
//
// These functions write a temporary file beside the target, check every write, flush it to the disk (fsync /
// FlushFileBuffers, which is FileOutputStream::flush), check the stream's status and the size on disk, and only then move
// it over the target. On any failure the target is not touched, the temporary file is deleted and `error` says why in
// the user's words ("the disk is full", "the folder can't be written to", or the system's text).
//
// The byte content is what JUCE's calls write (the same writeText line-ending conversion), so files stay identical.

#include <juce_core/juce_core.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>

namespace lynx::safewrite
{
// The reason of a failed write, for a message (English; the UI translates the fixed parts with tr()).
inline juce::String reasonOf (const juce::Result& status)
{
    const auto text = status.getErrorMessage();
    const auto lower = text.toLowerCase();
    if (lower.contains ("no space") || lower.contains ("not enough space") || lower.contains ("disk full") || lower.contains ("disk is full")
        || lower.contains ("too large") || lower.contains ("quota"))
        return "the disk is full";
    if (lower.contains ("permission") || lower.contains ("access is denied") || lower.contains ("read-only") || lower.contains ("read only"))
        return "the folder can't be written to";
    return text.isNotEmpty() ? text : juce::String ("the file could not be written");
}

// A write that stopped short without an error code (a short write(): the disk or the quota ran out mid-buffer): the
// disk is full when the volume has less free space than a few buffers
inline juce::String reasonOfShortWrite (const juce::File& file, const juce::Result& status)
{
    if (status.failed())
        return reasonOf (status);
    const auto free = file.getBytesFreeOnVolume();
    return free >= 0 && free < (juce::int64) 1 << 20 ? juce::String ("the disk is full") : juce::String ("the file could not be written");
}

// Writes `target` through `write` (which writes to the stream and returns false on its own failure): temporary file,
// checks, flush, size check, then the move. False with `error` = the reason; the target is then untouched.
inline bool writeWith (const juce::File& target, const std::function<bool (juce::OutputStream&)>& write, juce::String& error)
{
    if (! target.getParentDirectory().isDirectory() && ! target.getParentDirectory().createDirectory())
    {
        error = "the folder can't be written to";
        return false;
    }
    juce::TemporaryFile temp (target, juce::TemporaryFile::useHiddenFile);
    juce::int64 written = 0;
    {
        juce::FileOutputStream out (temp.getFile());
        if (out.failedToOpen())
        {
            error = reasonOf (out.getStatus());
            return false;
        }
        const auto ok = write (out);
        out.flush();
        if (! ok || out.getStatus().failed())
        {
            error = reasonOfShortWrite (temp.getFile(), out.getStatus());
            return false;
        }
        written = out.getPosition();
    }
    if (temp.getFile().getSize() != written)
    {
        error = "the disk is full";
        return false;
    }
    if (! temp.overwriteTargetFileWithTemporary())
    {
        error = "the file could not be replaced (in use by another program, or the folder can't be written to)";
        return false;
    }
    return true;
}

// writeWith for a writer that takes its stream and deletes it (juce::AudioFormatWriter, the zip builder; rollup 0.1.32
// step 6): `write` gets `stream` to hand over (release() it once the writer owns it) and must have destroyed that writer
// before it returns. The furthest byte written is the file's end (a WAV writer seeks back to its header when it closes).
inline bool writeWithHandedStream (const juce::File& target, const std::function<bool (std::unique_ptr<juce::OutputStream>& stream)>& write,
                                   juce::String& error)
{
    return writeWith (target, [&write] (juce::OutputStream& out) {
        struct Borrowed final : juce::OutputStream
        {
            Borrowed (juce::OutputStream& o, juce::int64& e) : to (o), end (e) {}
            void flush() override { to.flush(); }
            bool setPosition (juce::int64 pos) override { return to.setPosition (pos); }
            juce::int64 getPosition() override { return to.getPosition(); }
            bool write (const void* data, size_t n) override
            {
                const auto ok = to.write (data, n);
                end = std::max (end, to.getPosition());
                return ok;
            }
            juce::OutputStream& to;
            juce::int64& end;
        };
        juce::int64 end = out.getPosition();
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<Borrowed> (out, end);
        const auto ok = write (stream);
        stream.reset();
        return ok && out.setPosition (end);
    }, error);
}

inline bool writeData (const juce::File& target, const void* data, size_t size, juce::String& error)
{
    return writeWith (target, [data, size] (juce::OutputStream& out) { return size == 0 || out.write (data, size); }, error);
}

// File::replaceWithText's arguments and bytes (JUCE's defaults: no BOM, "\r\n" line endings; nullptr = as they are)
inline bool writeText (const juce::File& target, const juce::String& text, juce::String& error, bool asUnicode = false, bool writeHeaderBytes = false,
                       const char* lineFeed = "\r\n")
{
    return writeWith (target, [&] (juce::OutputStream& out) { return out.writeText (text, asUnicode, writeHeaderBytes, lineFeed); }, error);
}

// Drop-ins for juce::File::replaceWithText / replaceWithData where only true / false is used (settings files, lists)
inline bool replaceWithText (const juce::File& target, const juce::String& text, bool asUnicode = false, bool writeHeaderBytes = false,
                             const char* lineFeed = "\r\n")
{
    juce::String error;
    return writeText (target, text, error, asUnicode, writeHeaderBytes, lineFeed);
}

inline bool replaceWithData (const juce::File& target, const void* data, size_t size)
{
    juce::String error;
    return writeData (target, data, size, error);
}

// A copy of `source` (File::copyFileTo checks nothing either: it uses replaceWithData on Linux / macOS)
inline bool copyFile (const juce::File& source, const juce::File& target, juce::String& error)
{
    juce::FileInputStream in (source);
    if (in.failedToOpen())
    {
        error = reasonOf (in.getStatus());
        return false;
    }
    return writeWith (target, [&in] (juce::OutputStream& out) {
        const auto total = in.getTotalLength();
        return out.writeFromInputStream (in, -1) == total;
    }, error);
}

// For a writer that owns its stream (juce::AudioFormatWriter, the zip builder...): a file stream that keeps its own
// failure in `state`, which outlives it, so the caller can tell after the writer is gone whether every byte got out.
struct StreamState
{
    std::atomic<bool> failed { false };
    juce::String reason;
};

class CheckedFileStream final : public juce::OutputStream
{
public:
    CheckedFileStream (const juce::File& file, std::shared_ptr<StreamState> s) : out (file), path (file), state (std::move (s))
    {
        if (out.failedToOpen())
            fail();
    }
    ~CheckedFileStream() override
    {
        out.flush();
        if (out.getStatus().failed())
            fail();
    }
    bool openedOk() const noexcept { return out.openedOk(); }
    void flush() override
    {
        out.flush();
        if (out.getStatus().failed())
            fail();
    }
    bool setPosition (juce::int64 pos) override { return out.setPosition (pos) || (fail(), false); }
    juce::int64 getPosition() override { return out.getPosition(); }
    bool write (const void* data, size_t n) override { return out.write (data, n) || (fail(), false); }

private:
    void fail()
    {
        if (! state->failed.exchange (true))
            state->reason = reasonOfShortWrite (path, out.getStatus());
    }
    juce::FileOutputStream out;
    juce::File path;
    std::shared_ptr<StreamState> state;
};
} // namespace lynx::safewrite
