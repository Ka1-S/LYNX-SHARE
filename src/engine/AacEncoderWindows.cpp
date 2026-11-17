// M4A export on Windows (owner decision D5, round als190ka-export): Media Foundation's Sink Writer with the MPEG-4 file
// sink and Microsoft's AAC encoder MFT, both part of Windows (see engine/AacEncoder.h for why LYNX bundles no encoder).
// The encoder takes 16-bit PCM, mono or stereo, at 44.1 or 48 kHz and writes AAC-LC at 96 / 128 / 160 / 192 kbps
// (MF_MT_AUDIO_AVG_BYTES_PER_SECOND 12000 / 16000 / 20000 / 24000; "AAC Encoder", Microsoft Learn).

#include "engine/AacEncoder.h"

#if defined(_WIN32)

#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#if defined(_MSC_VER)
 #pragma comment(lib, "mfplat.lib")
 #pragma comment(lib, "mfreadwrite.lib")
 #pragma comment(lib, "mfuuid.lib")
 #pragma comment(lib, "ole32.lib")
#endif

namespace lynx::aac
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

// The GUIDs beyond the ones ui/VideoMediaWindows.cpp already links, defined here (the MinGW import libraries the Zig
// build links do not carry all of them); values from the Windows SDK's mfapi.h / mfidl.h / mfreadwrite.h.
constexpr GUID kAudioFormatAac { 0x00001610, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } }; // MFAudioFormat_AAC
constexpr GUID kAudioFormatPcm { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } }; // MFAudioFormat_PCM
constexpr GUID kAacProfileLevel { 0x7632f0e6, 0x9538, 0x4d61, { 0xac, 0xda, 0xea, 0x29, 0xc8, 0xc1, 0x44, 0x56 } }; // MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION
constexpr GUID kAacPayloadType { 0xbfbabe79, 0x7434, 0x4d1c, { 0x94, 0xf0, 0x72, 0xa3, 0xb9, 0xe1, 0x71, 0x88 } }; // MF_MT_AAC_PAYLOAD_TYPE
constexpr GUID kContainerType { 0x150ff23f, 0x4abc, 0x478b, { 0xac, 0x4f, 0xe1, 0x91, 0x6f, 0xba, 0x1c, 0xca } }; // MF_TRANSCODE_CONTAINERTYPE
constexpr GUID kContainerMpeg4 { 0xdc6cd05d, 0xb9d0, 0x40ef, { 0xbd, 0x35, 0xfa, 0x62, 0x2c, 0x1a, 0xb2, 0x8a } }; // MFTranscodeContainerType_MPEG4
constexpr GUID kDisableThrottling { 0x08b845d8, 0x2b74, 0x4afe, { 0x9d, 0x53, 0xbe, 0x16, 0xd2, 0xd5, 0xae, 0x4f } }; // MF_SINK_WRITER_DISABLE_THROTTLING
constexpr GUID kAudioEncoderCategory { 0x91c64bd0, 0xf91e, 0x4d8c, { 0x92, 0x76, 0xdb, 0x24, 0x82, 0x79, 0xd9, 0x75 } }; // MFT_CATEGORY_AUDIO_ENCODER

// COM on the calling thread (once per thread) and Media Foundation (once per process), as ui/VideoMediaWindows.cpp
bool startMediaFoundation()
{
    thread_local bool com = [] {
        const auto hr = CoInitializeEx (nullptr, COINIT_MULTITHREADED);
        return SUCCEEDED (hr) || hr == RPC_E_CHANGED_MODE; // (the message thread is already an apartment)
    }();
    static const bool mf = SUCCEEDED (MFStartup (MF_VERSION, MFSTARTUP_LITE));
    return com && mf;
}

std::string failed (const char* what, HRESULT hr)
{
    char text[160];
    std::snprintf (text, sizeof (text), "M4A export: %s failed (0x%08lX)", what, (unsigned long) hr);
    return text;
}

std::wstring wide (const std::string& utf8)
{
    if (utf8.empty())
        return {};
    const auto n = MultiByteToWideChar (CP_UTF8, 0, utf8.data(), (int) utf8.size(), nullptr, 0);
    std::wstring out ((std::size_t) std::max (0, n), L'\0');
    if (n > 0)
        MultiByteToWideChar (CP_UTF8, 0, utf8.data(), (int) utf8.size(), out.data(), n);
    return out;
}

bool findEncoder()
{
    if (! startMediaFoundation())
        return false;
    MFT_REGISTER_TYPE_INFO output { MFMediaType_Audio, kAudioFormatAac };
    IMFActivate** activates = nullptr;
    UINT32 count = 0;
    const auto hr = MFTEnumEx (kAudioEncoderCategory, MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_ASYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER,
                               nullptr, &output, &activates, &count);
    for (UINT32 i = 0; i < count; ++i)
        activates[i]->Release();
    CoTaskMemFree (activates);
    return SUCCEEDED (hr) && count > 0;
}
} // namespace

bool available()
{
    static const bool found = findEncoder();
    return found;
}

std::string unavailableReason()
{
    return available() ? std::string()
                       : std::string ("M4A (AAC) export needs Windows' AAC encoder (Media Foundation), which this Windows does not have "
                                      "(an \"N\" edition needs the Media Feature Pack)");
}

bool encode (const std::string& utf8Path, const float* left, const float* right, long long frames, int channels, int sampleRate,
             int kbps, std::string& error)
{
    if (channels != 1 && channels != 2)
        channels = 2;
    if (right == nullptr)
        right = left;
    if (sampleRate != 44100 && sampleRate != 48000)
    {
        error = "M4A export: the AAC encoder takes 44100 or 48000 Hz";
        return false;
    }
    kbps = kbps <= 112 ? 96 : kbps <= 144 ? 128 : kbps <= 176 ? 160 : 192;
    if (frames <= 0 || left == nullptr)
    {
        error = "M4A export: nothing to write";
        return false;
    }
    if (! available())
    {
        error = unavailableReason();
        return false;
    }

    Com<IMFAttributes> attributes;
    auto hr = MFCreateAttributes (attributes.put(), 2);
    if (FAILED (hr))
        return error = failed ("MFCreateAttributes", hr), false;
    attributes->SetGUID (kContainerType, kContainerMpeg4); // (whatever the file's extension: a temporary name is written first)
    attributes->SetUINT32 (kDisableThrottling, TRUE);       // (offline: as fast as the encoder goes)

    Com<IMFSinkWriter> writer;
    const auto path = wide (utf8Path);
    hr = MFCreateSinkWriterFromURL (path.c_str(), nullptr, attributes.p, writer.put());
    if (FAILED (hr))
        return error = failed ("creating the file", hr), false;

    Com<IMFMediaType> out;
    if (FAILED (hr = MFCreateMediaType (out.put())))
        return error = failed ("MFCreateMediaType", hr), false;
    out->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    out->SetGUID (MF_MT_SUBTYPE, kAudioFormatAac);
    out->SetUINT32 (MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    out->SetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32) sampleRate);
    out->SetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, (UINT32) channels);
    out->SetUINT32 (MF_MT_AUDIO_AVG_BYTES_PER_SECOND, (UINT32) (kbps * 125));
    out->SetUINT32 (kAacPayloadType, 0);          // raw AAC (the MPEG-4 sink writes the AudioSpecificConfig)
    out->SetUINT32 (kAacProfileLevel, 0x29);      // AAC Profile, level 2 (AAC-LC, stereo up to 48 kHz)
    DWORD stream = 0;
    if (FAILED (hr = writer->AddStream (out.p, &stream)))
        return error = failed ("the AAC stream (AddStream)", hr), false;

    Com<IMFMediaType> in;
    if (FAILED (hr = MFCreateMediaType (in.put())))
        return error = failed ("MFCreateMediaType", hr), false;
    const auto blockAlign = (UINT32) (2 * channels);
    in->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    in->SetGUID (MF_MT_SUBTYPE, kAudioFormatPcm);
    in->SetUINT32 (MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    in->SetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32) sampleRate);
    in->SetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, (UINT32) channels);
    in->SetUINT32 (MF_MT_AUDIO_BLOCK_ALIGNMENT, blockAlign);
    in->SetUINT32 (MF_MT_AUDIO_AVG_BYTES_PER_SECOND, blockAlign * (UINT32) sampleRate);
    if (FAILED (hr = writer->SetInputMediaType (stream, in.p, nullptr)))
        return error = failed ("the encoder's input (SetInputMediaType)", hr), false;
    if (FAILED (hr = writer->BeginWriting()))
        return error = failed ("BeginWriting", hr), false;

    constexpr long long block = 8192;
    for (long long done = 0; done < frames; done += block)
    {
        const auto n = (DWORD) std::min (block, frames - done);
        Com<IMFMediaBuffer> buffer;
        if (FAILED (hr = MFCreateMemoryBuffer (n * blockAlign, buffer.put())))
            return error = failed ("MFCreateMemoryBuffer", hr), false;
        BYTE* bytes = nullptr;
        if (FAILED (hr = buffer->Lock (&bytes, nullptr, nullptr)))
            return error = failed ("locking a buffer", hr), false;
        auto* pcm = reinterpret_cast<std::int16_t*> (bytes);
        for (DWORD i = 0; i < n; ++i)
            for (int c = 0; c < channels; ++c)
            {
                const auto v = (c == 0 ? left : right)[done + i];
                pcm[i * (DWORD) channels + (DWORD) c] = (std::int16_t) std::lrint (std::clamp (v, -1.0f, 1.0f) * 32767.0f);
            }
        buffer->Unlock();
        buffer->SetCurrentLength (n * blockAlign);

        Com<IMFSample> sample;
        if (FAILED (hr = MFCreateSample (sample.put())))
            return error = failed ("MFCreateSample", hr), false;
        sample->AddBuffer (buffer.p);
        // (100 ns units, from the frame count so no rounding piles up)
        const auto start = (LONGLONG) (done * 10000000LL / sampleRate);
        const auto end = (LONGLONG) ((done + (long long) n) * 10000000LL / sampleRate);
        sample->SetSampleTime (start);
        sample->SetSampleDuration (end - start);
        if (FAILED (hr = writer->WriteSample (stream, sample.p)))
            return error = failed ("encoding (WriteSample)", hr), false;
    }
    if (FAILED (hr = writer->Finalize()))
        return error = failed ("finishing the file (Finalize)", hr), false;
    return true;
}
} // namespace lynx::aac

#endif
