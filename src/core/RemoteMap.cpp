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
        case RemoteAction::launchSelectedClip: return t.track >= 0; // (round 190kc)
        case RemoteAction::crossfaderLeft:
        case RemoteAction::crossfaderCentre:
        case RemoteAction::crossfaderRight:
        case RemoteAction::sceneUp:
        case RemoteAction::sceneDown:
        case RemoteAction::launchSelectedScene:
        case RemoteAction::cancelLaunch:
        case RemoteAction::stopAllClips:
        case RemoteAction::play:
        case RemoteAction::stop:
        case RemoteAction::record:
        case RemoteAction::sessionRecord:
        case RemoteAction::metronome:
        case RemoteAction::overdub: // (round gap190og)
        case RemoteAction::loopSwitch:
        case RemoteAction::punchIn:
        case RemoteAction::punchOut: return true;
    }
    return false;
}

Target targetOf (const RemoteMapping& m)
{
    Target t;
    t.action = m.action;
    if (m.action == RemoteAction::parameter)
        t.param = m.target;
    if (m.action == RemoteAction::clipSlot || m.action == RemoteAction::trackStop || m.action == RemoteAction::launchSelectedClip)
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
    // Live 12.4.16 (live check 190ei): from anywhere but Min the first press goes to Min, then Max, then Min
    return std::abs (current - min) < 1.0e-4f ? clamp01 (max) : clamp01 (min);
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

// --- for every project (round 190hp) ---------------------------------------------------------------------------------------
namespace
{
RemoteMapping fromGlobal (const prefs::GlobalMapping& g)
{
    RemoteMapping m;
    m.source = RemoteMapping::Source::midi;
    m.channel = g.channel;
    m.number = g.number;
    m.port = g.port;
    m.action = (RemoteAction) g.action;
    m.scene = g.action == (int) RemoteAction::sceneLaunch ? g.scene : -1;
    return m;
}
AutomationTarget targetOfGlobal (const prefs::GlobalLink& l)
{
    return { (AutomationTargetKind) l.kind, l.index, l.slot, l.param };
}
bool sceneExists (const Song& song, int scene) noexcept
{
    return scene >= 0 && song.clipGrid && scene < (int) song.clipGrid->scenes.size();
}
// The project maps this control (a link or a MIDI mapping on the same number, channel and port) or this target itself.
bool projectOverrides (const Song& song, const Target& target, int channel, int number, int port)
{
    const auto sameControl = [&] (int ch, int num, int p) { return num == number && p == port && (ch < 0 || channel < 0 || ch == channel); };
    for (const auto& l : song.midiLinks)
        if (sameControl (l.channel, l.cc, l.port) || (target.action == RemoteAction::parameter && l.target == target.param))
            return true;
    for (const auto& m : song.remoteMappings)
        if (m.source == RemoteMapping::Source::midi && (sameControl (m.channel, m.number, m.port) || targetOf (m) == target))
            return true;
    return false;
}
} // namespace

bool canBeGlobal (const Target& t) noexcept
{
    switch (t.action)
    {
        case RemoteAction::parameter: return t.param.kind != AutomationTargetKind::none;
        case RemoteAction::sceneLaunch: return t.scene >= 0;
        case RemoteAction::stopAllClips:
        case RemoteAction::play:
        case RemoteAction::stop:
        case RemoteAction::record:
        case RemoteAction::sessionRecord:
        case RemoteAction::metronome:
        case RemoteAction::crossfaderLeft: // (round 190kc: the crossfader and the relative strip are set-wide controls)
        case RemoteAction::crossfaderCentre:
        case RemoteAction::crossfaderRight:
        case RemoteAction::sceneUp:
        case RemoteAction::sceneDown:
        case RemoteAction::launchSelectedScene:
        case RemoteAction::cancelLaunch:
        case RemoteAction::overdub: // (round gap190og: transport switches, set-wide)
        case RemoteAction::loopSwitch:
        case RemoteAction::punchIn:
        case RemoteAction::punchOut: return true;
        case RemoteAction::clipSlot:
        case RemoteAction::trackStop:
        case RemoteAction::launchSelectedClip: return false;
    }
    return false;
}

std::vector<Row> globalRows (const Song& song, const prefs::MidiPrefs& prefs)
{
    std::vector<Row> out;
    for (int i = 0; i < (int) prefs.globalLinks.size(); ++i)
    {
        const auto& l = prefs.globalLinks[(std::size_t) i];
        Row r;
        r.link = true;
        r.global = true;
        r.index = i;
        r.channel = l.channel;
        r.number = l.cc;
        r.target = Target::parameter (targetOfGlobal (l));
        r.min = l.min;
        r.max = l.max;
        r.hasRange = true;
        r.overridden = projectOverrides (song, r.target, l.channel, l.cc, l.port);
        out.push_back (r);
    }
    for (int i = 0; i < (int) prefs.globalMappings.size(); ++i)
    {
        const auto m = fromGlobal (prefs.globalMappings[(std::size_t) i]);
        Row r;
        r.global = true;
        r.index = i;
        r.channel = m.channel;
        r.number = m.number;
        r.target = targetOf (m);
        r.overridden = projectOverrides (song, r.target, m.channel, m.number, m.port);
        out.push_back (r);
    }
    return out;
}

bool canMove (const Song& song, const prefs::MidiPrefs& prefs, const Row& row, bool toEveryProject)
{
    if (row.index < 0)
        return false;
    if (toEveryProject)
    {
        if (row.global || ! canBeGlobal (row.target))
            return false;
        if (row.link)
            return row.index < (int) song.midiLinks.size();
        return row.index < (int) song.remoteMappings.size() && song.remoteMappings[(std::size_t) row.index].source == RemoteMapping::Source::midi;
    }
    if (! row.global)
        return false;
    if (row.link)
        return row.index < (int) prefs.globalLinks.size();
    if (row.index >= (int) prefs.globalMappings.size())
        return false;
    const auto m = fromGlobal (prefs.globalMappings[(std::size_t) row.index]);
    return m.action != RemoteAction::sceneLaunch || sceneExists (song, m.scene);
}

bool moveToEveryProject (Song& song, prefs::MidiPrefs& prefs, const Row& row)
{
    if (! canMove (song, prefs, row, true))
        return false;
    if (row.link)
    {
        if (row.index < 0 || row.index >= (int) song.midiLinks.size())
            return false;
        const auto l = song.midiLinks[(std::size_t) row.index];
        prefs::GlobalLink g { l.cc, l.channel, (int) l.target.kind, l.target.index, l.target.slot, l.target.param };
        g.port = l.port;
        g.formula = l.formula;
        g.smooth = l.smooth;
        g.smoothing = l.smoothing;
        g.mode = l.mode;
        g.min = l.min;
        g.max = l.max;
        auto& list = prefs.globalLinks;
        list.erase (std::remove_if (list.begin(), list.end(),
                                    [&] (const prefs::GlobalLink& o) {
                                        return targetOfGlobal (o) == l.target || (o.cc == g.cc && o.channel == g.channel && o.port == g.port);
                                    }),
                    list.end());
        list.push_back (std::move (g));
        song.midiLinks.erase (song.midiLinks.begin() + row.index);
        return true;
    }
    if (row.index < 0 || row.index >= (int) song.remoteMappings.size())
        return false;
    const auto m = song.remoteMappings[(std::size_t) row.index];
    if (m.source != RemoteMapping::Source::midi)
        return false;
    prefs::GlobalMapping g { m.channel, m.number, m.port, (int) m.action, m.action == RemoteAction::sceneLaunch ? m.scene : -1 };
    auto& list = prefs.globalMappings;
    list.erase (std::remove_if (list.begin(), list.end(),
                                [&] (const prefs::GlobalMapping& o) {
                                    return targetOf (fromGlobal (o)) == targetOf (m) || (o.number == g.number && o.channel == g.channel && o.port == g.port);
                                }),
                list.end());
    list.push_back (g);
    song.remoteMappings.erase (song.remoteMappings.begin() + row.index);
    return true;
}

bool moveToThisProject (Song& song, prefs::MidiPrefs& prefs, const Row& row)
{
    if (! canMove (song, prefs, row, false))
        return false;
    if (row.link)
    {
        if (row.index < 0 || row.index >= (int) prefs.globalLinks.size())
            return false;
        const auto g = prefs.globalLinks[(std::size_t) row.index];
        MidiLink l { g.cc, g.channel, targetOfGlobal (g) };
        l.port = g.port;
        l.formula = g.formula;
        l.smooth = g.smooth;
        l.smoothing = g.smoothing;
        l.mode = g.mode;
        l.min = g.min;
        l.max = g.max;
        song.midiLinks.erase (std::remove_if (song.midiLinks.begin(), song.midiLinks.end(), [&] (const MidiLink& o) { return o.target == l.target; }),
                              song.midiLinks.end());
        song.midiLinks.push_back (std::move (l));
        prefs.globalLinks.erase (prefs.globalLinks.begin() + row.index);
        return true;
    }
    if (row.index < 0 || row.index >= (int) prefs.globalMappings.size())
        return false;
    const auto m = fromGlobal (prefs.globalMappings[(std::size_t) row.index]);
    if (m.action == RemoteAction::sceneLaunch && ! sceneExists (song, m.scene))
        return false; // (no such scene here)
    unmap (song, targetOf (m), Mode::midi);
    song.remoteMappings.push_back (m);
    prefs.globalMappings.erase (prefs.globalMappings.begin() + row.index);
    return true;
}

bool removeGlobalRow (prefs::MidiPrefs& prefs, const Row& row)
{
    if (! row.global)
        return false;
    if (row.link)
    {
        if (row.index < 0 || row.index >= (int) prefs.globalLinks.size())
            return false;
        prefs.globalLinks.erase (prefs.globalLinks.begin() + row.index);
        return true;
    }
    if (row.index < 0 || row.index >= (int) prefs.globalMappings.size())
        return false;
    prefs.globalMappings.erase (prefs.globalMappings.begin() + row.index);
    return true;
}

bool setGlobalRange (prefs::MidiPrefs& prefs, const Row& row, float min, float max)
{
    if (! row.global || ! row.link || row.index < 0 || row.index >= (int) prefs.globalLinks.size())
        return false;
    auto& l = prefs.globalLinks[(std::size_t) row.index];
    l.min = clamp01 (min);
    l.max = clamp01 (max);
    return true;
}

std::vector<RemoteMapping> activeGlobalMappings (const Song& song, const prefs::MidiPrefs& prefs)
{
    std::vector<RemoteMapping> out;
    for (const auto& g : prefs.globalMappings)
    {
        const auto m = fromGlobal (g);
        if (! valid (targetOf (m)) || ! canBeGlobal (targetOf (m)) || (m.action == RemoteAction::sceneLaunch && ! sceneExists (song, m.scene)))
            continue;
        if (! projectOverrides (song, targetOf (m), m.channel, m.number, m.port))
            out.push_back (m);
    }
    return out;
}

Assigned midiOf (const Song& song, const prefs::MidiPrefs& prefs, const Target& target)
{
    if (const auto own = midiOf (song, target); own.found)
        return own;
    if (target.action == RemoteAction::parameter)
    {
        for (const auto& l : prefs.globalLinks)
            if (targetOfGlobal (l) == target.param && ! projectOverrides (song, target, l.channel, l.cc, l.port))
                return { true, l.channel, l.cc, 0 };
        return {};
    }
    for (const auto& m : activeGlobalMappings (song, prefs))
        if (targetOf (m) == target)
            return { true, m.channel, m.number, 0 };
    return {};
}

std::vector<Fire> midiFires (const Song& song, const std::vector<RemoteMapping>& global, int channel, int number, int value, int previous, int port)
{
    auto out = midiFires (song, channel, number, value, previous, port);
    const auto edge = midiEdge (number, value, previous);
    if (edge == Edge::none)
        return out;
    for (const auto& m : global)
        if (m.action != RemoteAction::parameter && sameMidi (m, channel, number, port))
            out.push_back ({ targetOf (m), edge, m.min, m.max });
    return out;
}

void usedNumbers (const std::vector<RemoteMapping>& global, std::uint64_t ccs[2], std::uint64_t notes[2]) noexcept
{
    for (const auto& m : global)
        if (m.number >= 0 && m.number < 128)
            ccs[m.number >> 6] |= std::uint64_t { 1 } << (m.number & 63);
        else if (isNoteLink (m.number))
            notes[(m.number - linkNoteBase) >> 6] |= std::uint64_t { 1 } << ((m.number - linkNoteBase) & 63);
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
