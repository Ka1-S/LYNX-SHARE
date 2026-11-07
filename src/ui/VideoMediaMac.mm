// macOS's own video decoder for ui/VideoMedia (round 174d): AVFoundation. Decodes what macOS reads (MOV / MP4 / M4V
// with H.264, HEVC, ProRes, MPEG-4, MJPEG; AAC / ALAC / AC-3 / PCM sound) as 32-bit BGRA frames and float PCM, with
// the system's hardware decoders. AVAssetReader cannot seek: a seek starts a new reader at that time.

#include "ui/VideoMedia.h"

#if JUCE_MAC

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations" // (the synchronous AVAsset properties: we run on our own thread)

namespace lynx::media
{
namespace
{
AVURLAsset* assetFor (const juce::File& file)
{
    if (! file.existsAsFile())
        return nil;
    NSURL* url = [NSURL fileURLWithPath: [NSString stringWithUTF8String: file.getFullPathName().toRawUTF8()]];
    NSDictionary* options = @{ AVURLAssetPreferPreciseDurationAndTimingKey: @YES };
    return [AVURLAsset URLAssetWithURL: url options: options];
}

class AvReader final : public Reader
{
public:
    ~AvReader() override { close(); }

    bool open (const juce::File& file)
    {
        @autoreleasepool
        {
            asset = [assetFor (file) retain];
            if (asset == nil)
                return false;
            const auto d = CMTimeGetSeconds (asset.duration);
            inf.duration = std::isfinite (d) ? d : 0.0;
            inf.hasAudio = [[asset tracksWithMediaType: AVMediaTypeAudio] count] > 0;
            NSArray* videos = [asset tracksWithMediaType: AVMediaTypeVideo];
            if ([videos count] == 0)
                return inf.hasAudio;
            track = [[videos objectAtIndex: 0] retain];
            const auto size = CGSizeApplyAffineTransform (track.naturalSize, CGAffineTransformIdentity);
            inf.width = (int) std::lround (std::abs (size.width));
            inf.height = (int) std::lround (std::abs (size.height));
            inf.fps = track.nominalFrameRate > 0.0f ? (double) track.nominalFrameRate : 30.0;
            inf.hasVideo = inf.width > 0 && inf.height > 0 && start (0.0);
            return inf.hasVideo || inf.hasAudio;
        }
    }

    video::Info info() const override { return inf; }

    bool seek (double seconds) override
    {
        if (! inf.hasVideo)
            return false;
        @autoreleasepool
        {
            return start (seconds);
        }
    }

    bool next (std::vector<std::uint8_t>& bgra, int& w, int& h, double& time) override
    {
        if (output == nil)
            return false;
        @autoreleasepool
        {
            for (int tries = 0; tries < 64; ++tries)
            {
                if (reader.status != AVAssetReaderStatusReading)
                    return false;
                CMSampleBufferRef sample = [output copyNextSampleBuffer];
                if (sample == nullptr)
                    return false;
                CVImageBufferRef image = CMSampleBufferGetImageBuffer (sample);
                if (image == nullptr)
                {
                    CFRelease (sample);
                    continue;
                }
                time = CMTimeGetSeconds (CMSampleBufferGetPresentationTimeStamp (sample));
                CVPixelBufferLockBaseAddress (image, kCVPixelBufferLock_ReadOnly);
                w = (int) CVPixelBufferGetWidth (image);
                h = (int) CVPixelBufferGetHeight (image);
                const auto pitch = CVPixelBufferGetBytesPerRow (image);
                const auto* base = static_cast<const std::uint8_t*> (CVPixelBufferGetBaseAddress (image));
                bgra.resize ((std::size_t) w * (std::size_t) h * 4);
                for (int y = 0; y < h && base != nullptr; ++y)
                    std::memcpy (bgra.data() + (std::size_t) y * (std::size_t) w * 4, base + (std::size_t) y * pitch, (std::size_t) w * 4);
                CVPixelBufferUnlockBaseAddress (image, kCVPixelBufferLock_ReadOnly);
                CFRelease (sample);
                return base != nullptr;
            }
            return false;
        }
    }

private:
    bool start (double seconds)
    {
        closeReader();
        NSError* error = nil;
        reader = [[AVAssetReader alloc] initWithAsset: asset error: &error];
        if (reader == nil)
            return false;
        NSDictionary* settings = @{ (id) kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA) };
        output = [[AVAssetReaderTrackOutput alloc] initWithTrack: track outputSettings: settings];
        output.alwaysCopiesSampleData = NO;
        if (! [reader canAddOutput: output])
            return false;
        [reader addOutput: output];
        const auto from = CMTimeMakeWithSeconds (std::max (0.0, seconds), 600);
        reader.timeRange = CMTimeRangeMake (from, kCMTimePositiveInfinity);
        return [reader startReading];
    }

    void closeReader()
    {
        if (reader != nil)
        {
            [reader cancelReading];
            [reader release];
            reader = nil;
        }
        if (output != nil)
        {
            [output release];
            output = nil;
        }
    }

    void close()
    {
        closeReader();
        if (track != nil)
            [track release];
        if (asset != nil)
            [asset release];
        track = nil;
        asset = nil;
    }

    AVURLAsset* asset = nil;
    AVAssetTrack* track = nil;
    AVAssetReader* reader = nil;
    AVAssetReaderTrackOutput* output = nil;
    video::Info inf;
};
} // namespace

std::unique_ptr<Reader> openNative (const juce::File& file)
{
    auto r = std::make_unique<AvReader>();
    if (! r->open (file))
        return nullptr;
    return r;
}

juce::String nativeBackendName() { return "AVFoundation"; }

std::shared_ptr<const video::AudioTrack> decodeNativeAudio (const juce::File& file, double rate)
{
    @autoreleasepool
    {
        AVURLAsset* asset = assetFor (file);
        if (asset == nil)
            return nullptr;
        NSArray* audio = [asset tracksWithMediaType: AVMediaTypeAudio];
        if ([audio count] == 0)
            return nullptr;
        NSError* error = nil;
        AVAssetReader* reader = [[[AVAssetReader alloc] initWithAsset: asset error: &error] autorelease];
        if (reader == nil)
            return nullptr;
        // interleaved stereo 16-bit at `rate`: AVFoundation decodes, mixes the tracks down and resamples
        NSDictionary* settings = @{ AVFormatIDKey: @(kAudioFormatLinearPCM), AVSampleRateKey: @(rate), AVNumberOfChannelsKey: @2,
                                    AVLinearPCMBitDepthKey: @16, AVLinearPCMIsFloatKey: @NO, AVLinearPCMIsBigEndianKey: @NO,
                                    AVLinearPCMIsNonInterleaved: @NO };
        AVAssetReaderAudioMixOutput* output = [AVAssetReaderAudioMixOutput assetReaderAudioMixOutputWithAudioTracks: audio audioSettings: settings];
        if (! [reader canAddOutput: output])
            return nullptr;
        [reader addOutput: output];
        if (! [reader startReading])
            return nullptr;
        auto track = std::make_shared<video::AudioTrack>();
        track->rate = rate;
        for (;;)
        {
            CMSampleBufferRef sample = [output copyNextSampleBuffer];
            if (sample == nullptr)
                break;
            if (CMBlockBufferRef block = CMSampleBufferGetDataBuffer (sample))
            {
                const auto length = CMBlockBufferGetDataLength (block);
                const auto at = track->samples.size();
                track->samples.resize (at + length / 2);
                CMBlockBufferCopyDataBytes (block, 0, (length / 2) * 2, track->samples.data() + at);
            }
            CFRelease (sample);
        }
        if (track->samples.size() % 2 != 0)
            track->samples.pop_back();
        if (track->samples.empty())
            return nullptr;
        return track;
    }
}
} // namespace lynx::media

#pragma clang diagnostic pop

#endif
