// M4A export on macOS (owner decision D5, round als190ka-export): AudioToolbox's ExtAudioFile writes an MPEG-4 audio
// file (kAudioFileM4AType) through Apple's AAC encoder (kAudioFormatMPEG4AAC, AAC-LC), both part of macOS (see
// engine/AacEncoder.h for why LYNX bundles no encoder). Plain C API: no Objective-C. Linked with AudioToolbox and
// CoreFoundation (CMakeLists.txt; JUCE's audio modules link them too).

#include "engine/AacEncoder.h"

#if defined(__APPLE__)

#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace lynx::aac
{
namespace
{
std::string failed (const char* what, OSStatus status)
{
    char text[160];
    std::snprintf (text, sizeof (text), "M4A export: %s failed (OSStatus %d)", what, (int) status);
    return text;
}

bool findEncoder()
{
    UInt32 format = kAudioFormatMPEG4AAC, size = 0;
    return AudioFormatGetPropertyInfo (kAudioFormatProperty_Encoders, sizeof (format), &format, &size) == noErr && size > 0;
}
} // namespace

bool available()
{
    static const bool found = findEncoder();
    return found;
}

std::string unavailableReason()
{
    return available() ? std::string() : std::string ("M4A (AAC) export needs macOS' AAC encoder, which was not found");
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

    CFURLRef url = CFURLCreateFromFileSystemRepresentation (kCFAllocatorDefault, reinterpret_cast<const UInt8*> (utf8Path.c_str()),
                                                            (CFIndex) utf8Path.size(), false);
    if (url == nullptr)
    {
        error = "M4A export: bad file name";
        return false;
    }

    AudioStreamBasicDescription fileFormat {};
    fileFormat.mSampleRate = sampleRate;
    fileFormat.mFormatID = kAudioFormatMPEG4AAC;
    fileFormat.mChannelsPerFrame = (UInt32) channels;
    fileFormat.mFramesPerPacket = 1024;

    ExtAudioFileRef file = nullptr;
    auto status = ExtAudioFileCreateWithURL (url, kAudioFileM4AType, &fileFormat, nullptr, kAudioFileFlags_EraseFile, &file);
    CFRelease (url);
    if (status != noErr || file == nullptr)
    {
        error = failed ("creating the file", status);
        return false;
    }

    // what LYNX hands over: interleaved 32-bit float (the converter makes AAC of it)
    AudioStreamBasicDescription client {};
    client.mSampleRate = sampleRate;
    client.mFormatID = kAudioFormatLinearPCM;
    client.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    client.mBitsPerChannel = 32;
    client.mChannelsPerFrame = (UInt32) channels;
    client.mFramesPerPacket = 1;
    client.mBytesPerFrame = (UInt32) (4 * channels);
    client.mBytesPerPacket = client.mBytesPerFrame;
    status = ExtAudioFileSetProperty (file, kExtAudioFileProperty_ClientDataFormat, sizeof (client), &client);
    if (status == noErr)
    {
        // the bitrate is the converter's; ExtAudioFile picks the change up when its converter configuration is reset
        AudioConverterRef converter = nullptr;
        UInt32 size = sizeof (converter);
        if (ExtAudioFileGetProperty (file, kExtAudioFileProperty_AudioConverter, &size, &converter) == noErr && converter != nullptr)
        {
            UInt32 bitrate = (UInt32) kbps * 1000u;
            AudioConverterSetProperty (converter, kAudioConverterEncodeBitRate, sizeof (bitrate), &bitrate);
            CFArrayRef config = nullptr;
            ExtAudioFileSetProperty (file, kExtAudioFileProperty_ConverterConfig, sizeof (config), &config);
        }
    }
    if (status != noErr)
    {
        ExtAudioFileDispose (file);
        error = failed ("the encoder's input format", status);
        return false;
    }

    constexpr long long block = 8192;
    std::vector<float> interleaved ((std::size_t) (block * channels));
    for (long long done = 0; done < frames && status == noErr; done += block)
    {
        const auto n = (UInt32) std::min (block, frames - done);
        for (UInt32 i = 0; i < n; ++i)
            for (int c = 0; c < channels; ++c)
                interleaved[(std::size_t) i * (std::size_t) channels + (std::size_t) c] = std::clamp ((c == 0 ? left : right)[done + i], -1.0f, 1.0f);
        AudioBufferList list;
        list.mNumberBuffers = 1;
        list.mBuffers[0].mNumberChannels = (UInt32) channels;
        list.mBuffers[0].mDataByteSize = n * client.mBytesPerFrame;
        list.mBuffers[0].mData = interleaved.data();
        status = ExtAudioFileWrite (file, n, &list);
    }
    const auto closed = ExtAudioFileDispose (file);
    if (status != noErr || closed != noErr)
    {
        error = failed (status != noErr ? "encoding (ExtAudioFileWrite)" : "finishing the file", status != noErr ? status : closed);
        return false;
    }
    return true;
}
} // namespace lynx::aac

#endif
