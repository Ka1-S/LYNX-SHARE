// Windows' own video decoder for ui/VideoMedia (round 174d): Media Foundation's Source Reader. Decodes what Windows
// reads (MP4 / MOV / M4V / AVI / WMV / MKV with H.264, HEVC with the system's extension, MPEG-4, MJPEG; AAC / AC-3 /
// MP3 / PCM sound), converted to RGB32 by the reader's video processing and to float PCM for the sound.

#include "ui/VideoMedia.h"

#if JUCE_WINDOWS

#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(_MSC_VER)
 #pragma comment(lib, "mfplat.lib")
 #pragma comment(lib, "mfreadwrite.lib")
 #pragma comment(lib, "mfuuid.lib")
 #pragma comment(lib, "ole32.lib")
 #pragma comment(lib, "propsys.lib")
#endif

namespace lynx::media
{
namespace
{
template <typename T>
struct Com
{
    T* p = nullptr;
    Com() = default;
    Com (const Com&) = delete;
    Com& operator= (const Com&) = delete;
    ~Com() { reset(); }
    void reset()
    {
        if (p != nullptr)
            p->Release();
        p = nullptr;
    }
    T** put()
    {
        reset();
        return &p;
    }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

// COM on the calling thread (once per thread) and Media Foundation (once per process)
bool startMediaFoundation()
{
    thread_local bool com = [] {
        const auto hr = CoInitializeEx (nullptr, COINIT_MULTITHREADED);
        return SUCCEEDED (hr) || hr == RPC_E_CHANGED_MODE; // (the message thread is already an apartment)
    }();
    static const bool mf = SUCCEEDED (MFStartup (MF_VERSION, MFSTARTUP_LITE));
    return com && mf;
}

bool createReader (const juce::File& file, bool videoProcessing, Com<IMFSourceReader>& reader)
{
    if (! startMediaFoundation() || ! file.existsAsFile())
        return false;
    Com<IMFAttributes> attrs;
    if (FAILED (MFCreateAttributes (attrs.put(), 2)))
        return false;
    if (videoProcessing)
        attrs->SetUINT32 (MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    const auto path = file.getFullPathName();
    return SUCCEEDED (MFCreateSourceReaderFromURL (path.toWideCharPointer(), attrs.p, reader.put())) && reader;
}

double durationOf (IMFSourceReader* reader)
{
    PROPVARIANT var;
    PropVariantInit (&var);
    double d = 0.0;
    if (SUCCEEDED (reader->GetPresentationAttribute ((DWORD) MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &var)) && var.vt == VT_UI8)
        d = (double) var.uhVal.QuadPart / 1.0e7;
    PropVariantClear (&var);
    return d;
}

class MfReader final : public Reader
{
public:
    bool open (const juce::File& file)
    {
        if (! createReader (file, true, reader))
            return false;
        reader->SetStreamSelection ((DWORD) MF_SOURCE_READER_ALL_STREAMS, FALSE);
        inf.duration = durationOf (reader.p);
        {
            Com<IMFMediaType> native;
            if (SUCCEEDED (reader->GetNativeMediaType ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, native.put())))
                inf.hasAudio = true;
        }
        Com<IMFMediaType> native;
        if (FAILED (reader->GetNativeMediaType ((DWORD) MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, native.put())))
            return inf.hasAudio; // (sound only: no picture)
        UINT32 num = 0, den = 0;
        if (SUCCEEDED (MFGetAttributeRatio (native.p, MF_MT_FRAME_RATE, &num, &den)) && num > 0 && den > 0)
            inf.fps = (double) num / (double) den;
        reader->SetStreamSelection ((DWORD) MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
        Com<IMFMediaType> type;
        if (FAILED (MFCreateMediaType (type.put())))
            return false;
        type->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Video);
        type->SetGUID (MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (FAILED (reader->SetCurrentMediaType ((DWORD) MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, type.p)))
            return inf.hasAudio;
        if (! readFormat())
            return inf.hasAudio;
        inf.hasVideo = true;
        // Media Foundation ignores the MP4 edit list: a B-frame stream written by ffmpeg / most encoders reports its first
        // frame at the edit's media time (0.08 s for two B-frames at 25 fps) where every other player shows 0. Time 0 is
        // the first frame here too (round 174d: the frame-accuracy check found the 2-frame shift)
        {
            DWORD stream = 0, flags = 0;
            LONGLONG ts = 0;
            Com<IMFSample> sample;
            for (int tries = 0; tries < 8 && ! sample; ++tries)
                if (FAILED (reader->ReadSample ((DWORD) MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &stream, &flags, &ts, sample.put()))
                    || (flags & (MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR)) != 0)
                    break;
            if (sample && ts > 0 && ts < (LONGLONG) 100000000) // (< 10 s: a real offset, not a stream that starts late)
                origin = (double) ts / 1.0e7;
            if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) != 0)
                readFormat();
            seek (0.0);
        }
        if (inf.fps <= 0.0)
            inf.fps = 30.0;
        return true;
    }

    video::Info info() const override { return inf; }

    bool seek (double seconds) override
    {
        if (! inf.hasVideo)
            return false;
        PROPVARIANT var;
        PropVariantInit (&var);
        var.vt = VT_I8;
        var.hVal.QuadPart = (LONGLONG) std::llround ((std::max (0.0, seconds) + origin) * 1.0e7);
        const auto ok = SUCCEEDED (reader->SetCurrentPosition (GUID_NULL, var));
        PropVariantClear (&var);
        return ok;
    }

    bool next (std::vector<std::uint8_t>& bgra, int& w, int& h, double& time) override
    {
        if (! inf.hasVideo)
            return false;
        for (int tries = 0; tries < 64; ++tries)
        {
            DWORD stream = 0, flags = 0;
            LONGLONG ts = 0;
            Com<IMFSample> sample;
            if (FAILED (reader->ReadSample ((DWORD) MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &stream, &flags, &ts, sample.put())))
                return false;
            if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0 || (flags & MF_SOURCE_READERF_ERROR) != 0)
                return false;
            if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) != 0 && ! readFormat())
                return false;
            if (! sample)
                continue; // (a gap / a format change without a frame)
            Com<IMFMediaBuffer> buffer;
            if (FAILED (sample->ConvertToContiguousBuffer (buffer.put())))
                return false;
            w = width;
            h = height;
            bgra.resize ((std::size_t) w * (std::size_t) h * 4);
            BYTE* base = nullptr;
            LONG pitch = 0;
            Com<IMF2DBuffer> twoD;
            const bool locked2D = SUCCEEDED (buffer->QueryInterface (IID_PPV_ARGS (twoD.put()))) && SUCCEEDED (twoD->Lock2D (&base, &pitch));
            DWORD length = 0;
            if (! locked2D)
            {
                if (FAILED (buffer->Lock (&base, nullptr, &length)))
                    return false;
                pitch = stride;
                if (pitch < 0)
                    base += (std::size_t) (-pitch) * (std::size_t) (paddedHeight - 1); // bottom-up: the first row is last
            }
            for (int y = 0; y < h; ++y)
            {
                const auto* row = base + (std::ptrdiff_t) (y + top) * pitch + (std::ptrdiff_t) left * 4;
                auto* d = bgra.data() + (std::size_t) y * (std::size_t) w * 4;
                std::memcpy (d, row, (std::size_t) w * 4);
                for (int x = 0; x < w; ++x)
                    d[x * 4 + 3] = 255; // (RGB32: the fourth byte is undefined)
            }
            if (locked2D)
                twoD->Unlock2D();
            else
                buffer->Unlock();
            time = (double) ts / 1.0e7 - origin;
            return true;
        }
        return false;
    }

private:
    bool readFormat()
    {
        Com<IMFMediaType> current;
        if (FAILED (reader->GetCurrentMediaType ((DWORD) MF_SOURCE_READER_FIRST_VIDEO_STREAM, current.put())))
            return false;
        UINT32 fw = 0, fh = 0;
        if (FAILED (MFGetAttributeSize (current.p, MF_MT_FRAME_SIZE, &fw, &fh)) || fw == 0 || fh == 0)
            return false;
        paddedHeight = (int) fh;
        UINT32 s = 0;
        stride = SUCCEEDED (current->GetUINT32 (MF_MT_DEFAULT_STRIDE, &s)) ? (LONG) (INT32) s : (LONG) fw * 4;
        // the picture inside the coded frame (1920 × 1080 in a 1920 × 1088 frame)
        MFVideoArea area {};
        UINT32 size = 0;
        left = top = 0;
        width = (int) fw;
        height = (int) fh;
        if (SUCCEEDED (current->GetBlob (MF_MT_MINIMUM_DISPLAY_APERTURE, (UINT8*) &area, sizeof (area), &size)) && size == sizeof (area))
        {
            left = std::clamp ((int) area.OffsetX.value, 0, (int) fw - 1);
            top = std::clamp ((int) area.OffsetY.value, 0, (int) fh - 1);
            width = std::clamp ((int) area.Area.cx, 1, (int) fw - left);
            height = std::clamp ((int) area.Area.cy, 1, (int) fh - top);
        }
        if (! inf.hasVideo || inf.width == 0)
        {
            inf.width = width;
            inf.height = height;
        }
        return true;
    }

    Com<IMFSourceReader> reader;
    video::Info inf;
    double origin = 0.0; // the first frame's own timestamp (seconds): time 0
    int width = 0, height = 0, left = 0, top = 0, paddedHeight = 0;
    LONG stride = 0;
};
} // namespace

std::unique_ptr<Reader> openNative (const juce::File& file)
{
    auto r = std::make_unique<MfReader>();
    if (! r->open (file))
        return nullptr;
    return r;
}

juce::String nativeBackendName() { return "Media Foundation"; }

std::shared_ptr<const video::AudioTrack> decodeNativeAudio (const juce::File& file, double rate)
{
    Com<IMFSourceReader> reader;
    if (! createReader (file, false, reader))
        return nullptr;
    reader->SetStreamSelection ((DWORD) MF_SOURCE_READER_ALL_STREAMS, FALSE);
    if (FAILED (reader->SetStreamSelection ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE)))
        return nullptr;
    // float PCM, stereo at `rate` (the reader converts and resamples); else float at the file's own rate
    auto setType = [&] (bool withLayout) {
        Com<IMFMediaType> type;
        if (FAILED (MFCreateMediaType (type.put())))
            return false;
        type->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        type->SetGUID (MF_MT_SUBTYPE, MFAudioFormat_Float);
        if (withLayout)
        {
            type->SetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, 2);
            type->SetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32) rate);
            type->SetUINT32 (MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
            type->SetUINT32 (MF_MT_AUDIO_BLOCK_ALIGNMENT, 8);
            type->SetUINT32 (MF_MT_AUDIO_AVG_BYTES_PER_SECOND, (UINT32) rate * 8);
        }
        return SUCCEEDED (reader->SetCurrentMediaType ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, type.p));
    };
    if (! setType (true) && ! setType (false))
        return nullptr;
    Com<IMFMediaType> current;
    if (FAILED (reader->GetCurrentMediaType ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, current.put())))
        return nullptr;
    const auto channels = (int) MFGetAttributeUINT32 (current.p, MF_MT_AUDIO_NUM_CHANNELS, 0);
    const auto fileRate = (double) MFGetAttributeUINT32 (current.p, MF_MT_AUDIO_SAMPLES_PER_SECOND, 0);
    if (channels <= 0 || fileRate <= 0.0)
        return nullptr;
    std::vector<float> stereo; // L R … at fileRate
    if (const auto d = durationOf (reader.p); d > 0.0)
        stereo.reserve ((std::size_t) (d * fileRate * 2.0) + 4096);
    for (;;)
    {
        DWORD stream = 0, flags = 0;
        LONGLONG ts = 0;
        Com<IMFSample> sample;
        if (FAILED (reader->ReadSample ((DWORD) MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &stream, &flags, &ts, sample.put())))
            break;
        if ((flags & (MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR)) != 0)
            break;
        if (! sample)
            continue;
        Com<IMFMediaBuffer> buffer;
        if (FAILED (sample->ConvertToContiguousBuffer (buffer.put())))
            break;
        BYTE* data = nullptr;
        DWORD length = 0;
        if (FAILED (buffer->Lock (&data, nullptr, &length)))
            break;
        const auto* f = reinterpret_cast<const float*> (data);
        const auto frames = (std::size_t) length / sizeof (float) / (std::size_t) channels;
        for (std::size_t i = 0; i < frames; ++i)
        {
            const auto* fr = f + i * (std::size_t) channels;
            stereo.push_back (fr[0]);
            stereo.push_back (channels > 1 ? fr[1] : fr[0]);
        }
        buffer->Unlock();
    }
    if (stereo.empty())
        return nullptr;
    auto track = std::make_shared<video::AudioTrack>();
    track->rate = rate;
    const auto inFrames = stereo.size() / 2;
    const auto ratio = fileRate / rate;
    const auto outFrames = (std::size_t) std::floor ((double) inFrames / ratio);
    track->samples.resize (outFrames * 2);
    auto toInt = [] (float v) { return (std::int16_t) std::lround (std::clamp (v, -1.0f, 1.0f) * 32767.0f); };
    for (std::size_t o = 0; o < outFrames; ++o)
    {
        // (linear interpolation when the reader did not resample)
        const auto pos = (double) o * ratio;
        const auto i = (std::size_t) pos;
        const auto frac = (float) (pos - (double) i);
        const auto j = std::min (i + 1, inFrames - 1);
        for (int c = 0; c < 2; ++c)
            track->samples[o * 2 + (std::size_t) c] = toInt (stereo[i * 2 + (std::size_t) c] * (1.0f - frac) + stereo[j * 2 + (std::size_t) c] * frac);
    }
    return track;
}
} // namespace lynx::media

#endif
