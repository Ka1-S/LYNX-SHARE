#include "core/RemoteMap.h"

#include <algorithm>
#include <cmath>

namespace lynx::remotemap
{
namespace
{
bool sameMidi (const RemoteMapping& m, int channel, int number, int port) noexcept
{
    return m.source == RemoteMapping::Source::midi && m.number == number && (m.channel < 0 || m.channel == channel) && m.port == port;
}
bool sameControl (const RemoteMapping& m, const MidiControl& c) noexcept
{
    return m.source == RemoteMapping::Source::midi && m.number == c.number && m.channel == c.channel && m.port == c.port;
}
bool isFor (const RemoteMapping& m, const Target& t)
{
    return targetOf (m) == t;
}
float clamp01 (float v) noexcept
{
    return std::clamp (std::isfinite (v) ? v : 0.0f, 0.0f, 1.0f);
}
} // namespace

bool valid (const Target& t) noexcept
{
    switch (t.action)
    {
        case RemoteAction::parameter: return t.param.kind != AutomationTargetKind::none;
        case RemoteAction::clipSlot: return t.track >= 0 && t.scene >= 0;
        case RemoteAction::sceneLaunch: return t.scene >= 0;
        case RemoteAction::trackStop: return t.track >= 0;
        case RemoteAction::stopAllClips:
        case RemoteAction::play:
        case RemoteAction::stop:
        case RemoteAction::record:
        case RemoteAction::sessionRecord:
        case RemoteAction::metronome: return true;
    }
    return false;
}

Target targetOf (const RemoteMapping& m)
{
    Target t;
    t.action = m.action;
    if (m.action == RemoteAction::parameter)
        t.param = m.target;
    if (m.action == RemoteAction::clipSlot || m.action == RemoteAction::trackStop)
        t.track = m.track;
    if (m.action == RemoteAction::clipSlot || m.action == RemoteAction::sceneLaunch)
        t.scene = m.scene;
    return t;
}

float applyRange (float x, float min, float max) noexcept
{
    return clamp01 (min + (max - min) * clamp01 (x));
}

float keyToggle (float current, float min, float max) noexcept
{
    return std::abs (current - max) < 1.0e-4f ? clamp01 (min) : clamp01 (max);
}

Edge midiEdge (int number, int value, int previous) noexcept
{
    if (isNoteLink (number))
        return value > 0 ? Edge::press : Edge::release;
    const auto wasDown = previous >= 64, isDown = value >= 64;
    if (isDown == wasDown)
        return Edge::none;
    return isDown ? Edge::press : Edge::release;
}

Refusal assignMidi (Song& song, const Target& target, MidiControl control)
{
    if (! valid (target) || control.number < 0 || ! (control.number < 128 || isNoteLink (control.number)))
        return Refusal::invalid;
    if (target.action == RemoteAction::parameter)
    {
        song.midiLinks.erase (std::remove_if (song.midiLinks.begin(), song.midiLinks.end(), [&] (const MidiLink& l) { return l.target == target.param; }),
                              song.midiLinks.end());
        MidiLink link { control.number, control.channel, target.param };
        link.port = control.port;
        song.midiLinks.push_back (std::move (link));
        return Refusal::none;
    }
    // the lesson's two rules for clip slots and scenes
    for (const auto& m : song.remoteMappings)
    {
        if (! sameControl (m, control) || isFor (m, target))
            continue;
        if (target.action == RemoteAction::clipSlot && m.action == RemoteAction::clipSlot && m.track == target.track)
            return Refusal::sameTrack;
        if (target.action == RemoteAction::sceneLaunch && m.action == RemoteAction::clipSlot)
            return Refusal::usedBySlot;
    }
    unmap (song, target, Mode::midi);
    RemoteMapping m;
    m.source = RemoteMapping::Source::midi;
    m.channel = control.channel;
    m.number = control.number;
    m.port = control.port;
    m.action = target.action;
    m.track = target.track;
    m.scene = target.scene;
    song.remoteMappings.push_back (m);
    return Refusal::none;
}

Refusal assignKey (Song& song, const Target& target, int key)
{
    if (! valid (target) || key == 0)
        return Refusal::invalid;
    for (const auto& m : song.remoteMappings)
    {
        if (m.source != RemoteMapping::Source::key || m.key != key || isFor (m, target))
            continue;
        if (target.action == RemoteAction::clipSlot && m.action == RemoteAction::clipSlot && m.track == target.track)
            return Refusal::sameTrack;
        if (target.action == RemoteAction::sceneLaunch && m.action == RemoteAction::clipSlot)
            return Refusal::usedBySlot;
    }
    unmap (song, target, Mode::key);
    RemoteMapping m;
    m.source = RemoteMapping::Source::key;
    m.key = key;
    m.action = target.action;
    if (target.action == RemoteAction::parameter)
        m.target = target.param;
    m.track = target.track;
    m.scene = target.scene;
    song.remoteMappings.push_back (m);
    return Refusal::none;
}

int unmap (Song& song, const Target& target, Mode kind)
{
    if (kind == Mode::off)
        return 0;
    const auto source = kind == Mode::midi ? RemoteMapping::Source::midi : RemoteMapping::Source::key;
    const auto before = song.remoteMappings.size() + song.midiLinks.size();
    song.remoteMappings.erase (std::remove_if (song.remoteMappings.begin(), song.remoteMappings.end(),
                                               [&] (const RemoteMapping& m) { return m.source == source && isFor (m, target); }),
                               song.remoteMappings.end());
    if (kind == Mode::midi && target.action == RemoteAction::parameter)
        song.midiLinks.erase (std::remove_if (song.midiLinks.begin(), song.midiLinks.end(), [&] (const MidiLink& l) { return l.target == target.param; }),
                              song.midiLinks.end());
    return (int) (before - song.remoteMappings.size() - song.midiLinks.size());
}

Assigned midiOf (const Song& song, const Target& target)
{
    if (target.action == RemoteAction::parameter)
    {
        for (const auto& l : song.midiLinks)
            if (l.target == target.param)
                return { true, l.channel, l.cc, 0 };
        return {};
    }
    for (const auto& m : song.remoteMappings)
        if (m.source == RemoteMapping::Source::midi && isFor (m, target))
            return { true, m.channel, m.number, 0 };
    return {};
}

Assigned keyOf (const Song& song, const Target& target)
{
    for (const auto& m : song.remoteMappings)
        if (m.source == RemoteMapping::Source::key && isFor (m, target))
            return { true, -1, -1, m.key };
    return {};
}

std::vector<Row> rows (const Song& song, Mode kind)
{
    std::vector<Row> out;
    if (kind == Mode::midi)
        for (int i = 0; i < (int) song.midiLinks.size(); ++i)
        {
            const auto& l = song.midiLinks[(std::size_t) i];
            Row r;
            r.link = true;
            r.index = i;
            r.channel = l.channel;
            r.number = l.cc;
            r.target = Target::parameter (l.target);
            r.min = l.min;
            r.max = l.max;
            r.hasRange = true;
            out.push_back (r);
        }
    const auto source = kind == Mode::midi ? RemoteMapping::Source::midi : RemoteMapping::Source::key;
    if (kind != Mode::off)
        for (int i = 0; i < (int) song.remoteMappings.size(); ++i)
        {
            const auto& m = song.remoteMappings[(std::size_t) i];
            if (m.source != source)
                continue;
            Row r;
            r.index = i;
            r.channel = m.channel;
            r.number = m.number;
            r.key = m.key;
            r.target = targetOf (m);
            r.min = m.min;
            r.max = m.max;
            r.hasRange = m.action == RemoteAction::parameter;
            out.push_back (r);
        }
    return out;
}

bool setRange (Song& song, const Row& row, float min, float max)
{
    if (! row.hasRange)
        return false;
    min = clamp01 (min);
    max = clamp01 (max);
    if (row.link)
    {
        if (row.index < 0 || row.index >= (int) song.midiLinks.size())
            return false;
        auto& l = song.midiLinks[(std::size_t) row.index];
        l.min = min;
        l.max = max;
        return true;
    }
    if (row.index < 0 || row.index >= (int) song.remoteMappings.size())
        return false;
    auto& m = song.remoteMappings[(std::size_t) row.index];
    m.min = min;
    m.max = max;
    return true;
}

bool invertRange (Song& song, const Row& row)
{
    return setRange (song, row, row.max, row.min);
}

bool removeRow (Song& song, const Row& row)
{
    if (row.link)
    {
        if (row.index < 0 || row.index >= (int) song.midiLinks.size())
            return false;
        song.midiLinks.erase (song.midiLinks.begin() + row.index);
        return true;
    }
    if (row.index < 0 || row.index >= (int) song.remoteMappings.size())
        return false;
    song.remoteMappings.erase (song.remoteMappings.begin() + row.index);
    return true;
}

std::string noteText (int note)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    note = std::clamp (note, 0, 127);
    return std::string (names[note % 12]) + std::to_string (note / 12 - 2);
}

std::string controlText (int number)
{
    if (isNoteLink (number))
        return noteText (number - linkNoteBase);
    if (number >= 0 && number < 128)
        return "CC " + std::to_string (number);
    return {};
}

void scenesInserted (Song& song, int at) noexcept
{
    for (auto& m : song.remoteMappings)
        if ((m.action == RemoteAction::clipSlot || m.action == RemoteAction::sceneLaunch) && m.scene >= at)
            ++m.scene;
}

void sceneRemoved (Song& song, int scene)
{
    auto& list = song.remoteMappings;
    list.erase (std::remove_if (list.begin(), list.end(),
                                [scene] (const RemoteMapping& m) {
                                    return (m.action == RemoteAction::clipSlot || m.action == RemoteAction::sceneLaunch) && m.scene == scene;
                                }),
                list.end());
    for (auto& m : list)
        if ((m.action == RemoteAction::clipSlot || m.action == RemoteAction::sceneLaunch) && m.scene > scene)
            --m.scene;
}

std::vector<Fire> midiFires (const Song& song, int channel, int number, int value, int previous, int port)
{
    std::vector<Fire> out;
    const auto edge = midiEdge (number, value, previous);
    if (edge == Edge::none)
        return out;
    for (const auto& m : song.remoteMappings)
        if (m.action != RemoteAction::parameter && sameMidi (m, channel, number, port))
            out.push_back ({ targetOf (m), edge, m.min, m.max });
    return out;
}

std::vector<Fire> keyFires (const Song& song, int key, bool down)
{
    std::vector<Fire> out;
    for (const auto& m : song.remoteMappings)
        if (m.source == RemoteMapping::Source::key && m.key == key)
            out.push_back ({ targetOf (m), down ? Edge::press : Edge::release, m.min, m.max });
    return out;
}

void usedNumbers (const Song& song, std::uint64_t ccs[2], std::uint64_t notes[2]) noexcept
{
    for (const auto& m : song.remoteMappings)
    {
        if (m.source != RemoteMapping::Source::midi)
            continue;
        if (m.number >= 0 && m.number < 128)
            ccs[m.number >> 6] |= std::uint64_t { 1 } << (m.number & 63);
        else if (isNoteLink (m.number))
            notes[(m.number - linkNoteBase) >> 6] |= std::uint64_t { 1 } << ((m.number - linkNoteBase) & 63);
    }
}
} // namespace lynx::remotemap
