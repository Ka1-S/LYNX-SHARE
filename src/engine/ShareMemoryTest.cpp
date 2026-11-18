// Round 190ks (Discord: "is it possible for plugins of the same type to share resources?"). `LYNX --sharetest <folder>
// <case> report.txt` measures what a project of 8 identical channels costs in memory: the process's resident memory
// before and after the project is loaded into the live engine (its rack built, its fonts loaded), with what the sample
// cache holds and how many distinct SoundFont banks the channels play. Cases:
//   sf2-same     8 LYNX Soundfont channels on one .sf2 file          wav-same     8 Sampler channels on one .wav file
//   sf2-copies   8 LYNX Soundfont channels, each on its own copy      wav-copies   8 Sampler channels, each on its own copy
// The files are made in <folder> (a ~30 MB bank, a 20 s stereo sample). Run once with LYNX_CONTENT_SHARE=0 (the copies
// held separately, as before round 190ks) and once without (byte-identical files share one copy).

#include <set>

#include "core/SoundFont.h"
#include "engine/AudioEngine.h"
#include "engine/ChannelRackProcessor.h"
#include "engine/SampleLibrary.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #define PSAPI_VERSION 2 // GetProcessMemoryInfo from kernel32 (K32GetProcessMemoryInfo): no extra library
 #include <windows.h>
 #include <psapi.h>
#endif

namespace lynx
{
namespace
{
double residentMb()
{
#if JUCE_WINDOWS
    PROCESS_MEMORY_COUNTERS_EX c {};
    if (GetProcessMemoryInfo (GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*) &c, sizeof (c)))
        return (double) c.PrivateUsage / 1048576.0;
    return -1.0;
#elif JUCE_LINUX
    const auto status = juce::File ("/proc/self/status").loadFileAsString();
    for (const auto& line : juce::StringArray::fromLines (status))
        if (line.startsWith ("VmRSS:"))
            return line.fromFirstOccurrenceOf (":", false, false).trim().getDoubleValue() / 1024.0; // (kB)
    return -1.0;
#else
    return -1.0;
#endif
}

bool writeWav (const juce::File& file, double seconds)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> out (file.createOutputStream().release());
    if (out == nullptr)
        return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (out.get(), 44100.0, 2, 16, {}, 0));
    if (writer == nullptr)
        return false;
    out.release();
    const auto frames = (int) (seconds * 44100.0);
    juce::AudioBuffer<float> b (2, frames);
    juce::Random random (190);
    for (int i = 0; i < frames; ++i)
    {
        b.setSample (0, i, 0.3f * std::sin (0.031f * (float) i) + 0.05f * (random.nextFloat() - 0.5f));
        b.setSample (1, i, 0.3f * std::sin (0.029f * (float) i) + 0.05f * (random.nextFloat() - 0.5f));
    }
    return writer->writeFromAudioSampleBuffer (b, 0, frames);
}
} // namespace

juce::String AudioEngine::shareMemorySelfTest (const juce::File& folder, const juce::String& which)
{
    folder.createDirectory();
    const auto sf2 = which.startsWith ("sf2");
    const auto copies = which.endsWith ("copies");
    if (! sf2 && ! which.startsWith ("wav"))
        return "ERROR unknown case " + which;
    // the files: one original and 7 byte copies
    std::vector<juce::File> files;
    const auto original = folder.getChildFile (sf2 ? "bank.sf2" : "sample.wav");
    if (! original.existsAsFile())
    {
        if (sf2)
        {
            std::vector<sf::TestPreset> presets;
            for (int p = 0; p < 40; ++p)
                presets.push_back ({ "Preset " + std::to_string (p), 0, p });
            const auto bytes = sf::makeTestFont (presets, 8, 96000);
            if (! original.replaceWithData (bytes.data(), bytes.size()))
                return "ERROR can't write " + original.getFullPathName();
        }
        else if (! writeWav (original, 20.0))
            return "ERROR can't write " + original.getFullPathName();
    }
    for (int k = 0; k < 8; ++k)
    {
        auto f = original;
        if (copies && k > 0)
        {
            f = folder.getChildFile ((sf2 ? "bank-copy" : "sample-copy") + juce::String (k) + (sf2 ? ".sf2" : ".wav"));
            if (! f.existsAsFile() || f.getSize() != original.getSize())
                original.copyFileTo (f);
        }
        files.push_back (f);
    }

    AudioEngine live (false);
    if (const auto e = live.openStandInDevice (48000.0, 256); e.isNotEmpty())
        return "ERROR open the stand-in device: " + e;
    for (int i = 0; i < 10; ++i)
    {
        live.timerCallback();
        juce::Thread::sleep (20);
    }
    const auto before = residentMb();

    Song song;
    song.bpm = 120.0;
    song.mixer = Song::defaultMixer (2);
    for (int k = 0; k < 8; ++k)
    {
        ChannelDef c;
        c.name = (sf2 ? "Soundfont " : "Sampler ") + std::to_string (k + 1);
        c.instrument = sf2 ? InstrumentKind::soundfont : InstrumentKind::sampler;
        c.samplePath = files[(std::size_t) k].getFullPathName().toStdString();
        song.channels.push_back (c);
    }
    song.patterns.emplace_back();
    song.patterns[0].lengthTicks = 4 * ppq;
    const auto started = juce::Time::getMillisecondCounterHiRes();
    live.setSong (song);
    for (int i = 0; i < 100; ++i) // (the rack is built by the engine's timer; fonts load on their own threads)
    {
        live.timerCallback();
        juce::Thread::sleep (20);
    }
    std::set<const sf::Font*> fonts;
    if (sf2)
        for (const auto& f : files)
        {
            auto handle = sf::FontLibrary::instance().getNow (f.getFullPathName().toStdString());
            if (handle != nullptr && handle->font() != nullptr)
                fonts.insert (handle->font());
        }
    const auto loadMs = juce::Time::getMillisecondCounterHiRes() - started;
    juce::Thread::sleep (300);
    const auto after = residentMb();
    const auto use = SampleLibrary::memoryUse();
    juce::String report;
    report << "case " << which << " (content share " << (std::getenv ("LYNX_CONTENT_SHARE") != nullptr && std::getenv ("LYNX_CONTENT_SHARE")[0] == '0' ? "off" : "on")
           << "), file " << juce::String ((double) original.getSize() / 1048576.0, 1) << " MB\n";
    report << "resident before " << juce::String (before, 1) << " MB, after " << juce::String (after, 1) << " MB, project costs "
           << juce::String (after - before, 1) << " MB (load " << juce::String (loadMs, 0) << " ms incl. 2 s of timer)\n";
    report << "sample cache: " << use.samples << " decoded sample(s), " << juce::String ((double) use.sampleBytes / 1048576.0, 1) << " MB\n";
    if (sf2)
        report << "distinct banks played by the 8 channels: " << (int) fonts.size() << "\n";
    live.closeStandInDevice();
    return "OK\n" + report;
}
} // namespace lynx
