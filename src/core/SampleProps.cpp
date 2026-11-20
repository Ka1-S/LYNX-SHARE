#include "core/SampleProps.h"

#include <algorithm>
#include <cmath>

namespace lynx::sampleprops
{
namespace
{
// FL's Round (banker's rounding, as cvtsd2si in the default mode)
std::int64_t flRound (double v) noexcept { return (std::int64_t) std::nearbyint (v); }
} // namespace

double beatsOf (double tempo, double frames, double sampleRate) noexcept
{
    return sampleRate > 0.0 ? frames * tempo / (sampleRate * 60.0) : 0.0;
}

double tempoOf (double beats, double frames, double sampleRate) noexcept
{
    return frames > 0.0 ? sampleRate * beats * 60.0 / frames : 0.0;
}

double clampTypedTempo (double tempo) noexcept
{
    return ! (tempo >= 0.0) ? 0.0 : std::min (tempo, 100000.0);
}

int quickBeats (double frames, double sampleRate, double projectTempo, double& tempoOut) noexcept
{
    const auto barFrames = 240000.0 / std::max (1.0e-9, projectTempo) * sampleRate * 0.001; // (240000 / tempo ms: one bar)
    std::int64_t beats = 4;
    if (frames >= 1.0)
    {
        if (frames / barFrames >= 0.75)
            beats = std::min<std::int64_t> (flRound (frames / barFrames) * 4, 256);
        else
            beats = 2;
    }
    tempoOut = tempoOf ((double) beats, frames, sampleRate);
    if (tempoOut < 30.0 || tempoOut > 240.0)
        beats = 0;
    return (int) beats;
}

double limitTempo (double tempo, int low) noexcept
{
    if (tempo > 1.0 && low > 0)
    {
        if (tempo <= (double) (low * 2))
        {
            while (tempo < (double) low)
                tempo *= 2.0;
        }
        else
            while ((double) (low * 2) < tempo)
                tempo /= 2.0;
    }
    return tempo;
}

void roundPitch (int& middleNote, double& fineTune) noexcept
{
    middleNote = (int) std::clamp<std::int64_t> ((std::int64_t) middleNote + flRound (fineTune / 100.0), 0, 127);
    fineTune = 0.0;
}

bool fromDetectedPitch (double semitones, int& middleNote, int& fineCents) noexcept
{
    if (! (semitones >= 0.0))
        return false;
    const auto cents = (int) flRound ((double) ((float) semitones * 100.0f));
    middleNote = std::clamp (cents / 100, 0, 127);
    fineCents = cents % 100;
    return true;
}

Form formOf (const audioedit::Document& doc, const std::string& fileName)
{
    Form f;
    const auto& p = doc.props;
    f.name = p.name;
    f.comments = p.comments;
    f.fileName = fileName;
    f.sampleRate = doc.audio.sampleRate;
    f.int16 = p.int16;
    f.channels = doc.audio.numChannels() == 1 ? Channels::monoMerged : Channels::stereo; // (FL: item 1 for a mono sample)
    f.tempo = p.tempo;
    f.tempoSync = p.tempoSync;
    f.middleNote = p.middleNote;
    f.lowNote = p.lowNote;
    f.highNote = p.highNote;
    f.fineTune = p.fineTune;
    return f;
}

double acceptedRate (double rate) noexcept
{
    return ! (rate >= 1.0) ? 1.0 : std::min (rate, 100000000.0);
}

double acceptedTempo (double tempo) noexcept
{
    return ! (tempo >= 0.0) ? 0.0 : std::min (tempo, 960.0);
}

void toInt16 (audioedit::Document& doc)
{
    float peak = 0.0f;
    for (const auto& c : doc.audio.channels)
        for (const auto v : c)
            peak = std::max (peak, std::abs (v));
    const auto scale = peak > 1.0f ? 1.0f / peak : 1.0f;
    for (auto& c : doc.audio.channels)
        for (auto& v : c)
            v = (float) ((double) flRound (std::clamp ((double) (v * scale), -1.0, 1.0) * 32767.0) / 32767.0);
    doc.props.int16 = true;
}

void setChannels (audioedit::Document& doc, Channels channels)
{
    if (doc.audio.numChannels() < 2)
    {
        if (channels == Channels::stereo)
            audioedit::toStereo (doc);
        return;
    }
    switch (channels)
    {
        case Channels::monoMerged: audioedit::toMono (doc, audioedit::MonoSource::merged); break;
        case Channels::monoLeft: audioedit::toMono (doc, audioedit::MonoSource::left); break;
        case Channels::monoRight: audioedit::toMono (doc, audioedit::MonoSource::right); break;
        case Channels::stereo: break;
    }
}

bool accept (audioedit::Document& doc, const Form& form)
{
    auto& p = doc.props;
    p.name = form.name;
    p.comments = form.comments;
    const auto rate = acceptedRate (form.sampleRate);
    const auto rateChanged = rate != doc.audio.sampleRate;
    if (rateChanged)
    {
        if (form.resample && ! doc.audio.empty())
            audioedit::resample (doc, rate);
        doc.audio.sampleRate = rate;
    }
    setChannels (doc, form.channels);
    if (form.int16 && ! p.int16)
        toInt16 (doc);
    p.int16 = form.int16;
    p.tempo = acceptedTempo (form.tempo);
    p.tempoSync = form.tempoSync;
    p.fineTune = form.fineTune;
    p.middleNote = std::clamp (form.middleNote, 0, 127);
    p.lowNote = std::clamp (form.lowNote, 0, 127);
    p.highNote = std::clamp (form.highNote, 0, 127);
    return rateChanged;
}

std::string noteName (int note)
{
    static const char* const names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    note = std::clamp (note, 0, 127);
    return std::string (names[note % 12]) + std::to_string (note / 12);
}
} // namespace lynx::sampleprops
