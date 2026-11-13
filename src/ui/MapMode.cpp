#include "ui/MapMode.h"

#include <algorithm>
#include <cmath>

#include "core/Effects.h"
#include "engine/AudioEngine.h"
#include "ui/AutomationUi.h"
#include "ui/I18n.h"
#include "ui/Theme.h"

namespace lynx::mapmode
{
namespace
{
constexpr const char* tagProperty = "lynxMapTarget";
constexpr int headerHeight = 24, rowHeight = 20, titleHeight = 26;

juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

juce::String trackName (const Song& song, int track)
{
    if (track >= 0 && track < (int) song.playlistTracks.size() && ! song.playlistTracks[(std::size_t) track].name.empty())
        return utf8 (song.playlistTracks[(std::size_t) track].name);
    return tr ("Track {0}", { juce::String (track + 1) });
}

// a target as one var (the component property)
juce::var encode (const remotemap::Target& t)
{
    return juce::Array<juce::var> { (int) t.action, (int) t.param.kind, t.param.index, t.param.slot, t.param.param, t.track, t.scene };
}
std::optional<remotemap::Target> decode (const juce::var& v)
{
    const auto* a = v.getArray();
    if (a == nullptr || a->size() != 7)
        return std::nullopt;
    remotemap::Target t;
    t.action = (RemoteAction) (int) (*a)[0];
    t.param = { (AutomationTargetKind) (int) (*a)[1], (int) (*a)[2], (int) (*a)[3], (int) (*a)[4] };
    t.track = (*a)[5];
    t.scene = (*a)[6];
    return t;
}
} // namespace

void tag (juce::Component& c, const remotemap::Target& target)
{
    c.getProperties().set (tagProperty, encode (target));
}

std::optional<remotemap::Target> tagOf (const juce::Component& c)
{
    return c.getProperties().contains (tagProperty) ? decode (c.getProperties()[tagProperty]) : std::nullopt;
}

std::pair<juce::String, juce::String> describe (AudioEngine& engine, const remotemap::Target& t)
{
    const auto& song = engine.getSong();
    switch (t.action)
    {
        case RemoteAction::parameter:
        {
            // round 190em: Path "<track> | <device>" and the parameter's name, as Live's list shows them (live check
            // 190ei: "T1 | Mixer", "Track Volume")
            const auto text = describeAutomationTarget (engine, t.param);
            const auto cut = text.lastIndexOf (" - ");
            const auto owner = cut > 0 ? text.substring (0, cut) : juce::String();
            const auto name = cut > 0 ? text.substring (cut + 3) : text;
            const auto mixerPath = (t.param.kind == AutomationTargetKind::mixerSend ? owner.upToFirstOccurrenceOf (juce::String::fromUTF8 (" \xe2\x86\x92 "), false, false) : owner)
                                   + " | " + tr ("Mixer");
            switch (t.param.kind)
            {
                case AutomationTargetKind::mixerVolume: return { mixerPath, tr ("Track Volume") };
                case AutomationTargetKind::mixerPan: return { mixerPath, tr ("Track Panning") };
                case AutomationTargetKind::mixerSend: return { mixerPath, name.isNotEmpty() && owner.contains (juce::String::fromUTF8 ("\xe2\x86\x92"))
                                                                             ? tr ("Send to {0}", { owner.fromFirstOccurrenceOf (juce::String::fromUTF8 (" \xe2\x86\x92 "), false, false) })
                                                                             : name };
                case AutomationTargetKind::effectParam:
                {
                    // the device: the effect in that insert slot
                    const auto& mixer = song.mixer;
                    if (t.param.index >= 0 && t.param.index < (int) mixer.size() && t.param.slot >= 0
                        && t.param.slot < (int) mixer[(std::size_t) t.param.index].effects.size())
                    {
                        const auto& fx = mixer[(std::size_t) t.param.index].effects[(std::size_t) t.param.slot];
                        auto device = fx.kind == EffectKind::plugin && ! fx.plugin.name.empty() ? utf8 (fx.plugin.name) : utf8 (effectInfo (fx.kind).name);
                        if (device.isNotEmpty())
                            return { owner + " | " + device, name };
                    }
                    return { owner, name };
                }
                default: return { owner, name };
            }
        }
        case RemoteAction::clipSlot: return { trackName (song, t.track), tr ("Slot {0}", { juce::String (t.scene + 1) }) };
        case RemoteAction::trackStop: return { trackName (song, t.track), tr ("Clip stop") };
        case RemoteAction::sceneLaunch:
        {
            const auto* grid = song.clipGrid ? &*song.clipGrid : nullptr;
            const auto name = grid != nullptr && t.scene < (int) grid->scenes.size() ? utf8 (grid->scenes[(std::size_t) t.scene].name) : juce::String();
            return { tr ("Scenes"), name.isNotEmpty() ? name : tr ("Scene {0}", { juce::String (t.scene + 1) }) };
        }
        case RemoteAction::stopAllClips: return { tr ("Clip grid"), tr ("Stop all clips") };
        case RemoteAction::play: return { tr ("Transport"), tr ("Play") };
        case RemoteAction::stop: return { tr ("Transport"), tr ("Stop") };
        case RemoteAction::record: return { tr ("Transport"), tr ("Record") };
        case RemoteAction::sessionRecord: return { tr ("Clip grid"), tr ("Session Record") };
        case RemoteAction::metronome: return { tr ("Transport"), tr ("Metronome") };
    }
    return {};
}

juce::String keyText (int key)
{
    if (key > 32 && key < 127)
        return juce::String::charToString ((juce::juce_wchar) key).toUpperCase();
    return juce::KeyPress (key).getTextDescription();
}

juce::String listKeyText (int key)
{
    // round 190em: the Key Mappings list as Live writes it ("Key w"; live check 190ei); letters in lower case
    const auto text = key > 32 && key < 127 ? juce::String::charToString ((juce::juce_wchar) key).toLowerCase() : keyText (key);
    return trc ("@computer key: Key") + " " + text; // (the computer key's word, not the musical key)
}

juce::String assignedText (const remotemap::Assigned& a, remotemap::Mode mode)
{
    if (! a.found)
        return {};
    if (mode == remotemap::Mode::key)
        return keyText (a.key);
    const auto channel = a.channel < 0 ? juce::String ("*") : juce::String (a.channel + 1);
    return channel + "/" + (isNoteLink (a.number) ? juce::String (remotemap::noteText (a.number - linkNoteBase)) : juce::String (a.number));
}

juce::String rangeText (AudioEngine& engine, const remotemap::Target& target, float value)
{
    if (target.action != RemoteAction::parameter)
        return {};
    // round 190em: in the parameter's own unit, as Live's list ("-∞ dB", "6.0 dB", "50L"; live check 190ei)
    auto text = targetValueTextAt (engine, target.param, value);
    if (text.isEmpty())
        return automationPointValueText (target.param, value);
    if (text.startsWith ("-inf"))
        return juce::String::fromUTF8 ("\xe2\x88\x92\xe2\x88\x9e") + text.substring (4);
    const auto pan = target.param.kind == AutomationTargetKind::mixerPan || target.param.kind == AutomationTargetKind::channelPan;
    if (pan && (text.startsWith ("L") || text.startsWith ("R")))
        return text.substring (1) + text.substring (0, 1); // (Live writes the side after the number)
    return text;
}

std::optional<float> rangeValueOf (AudioEngine& engine, const remotemap::Target& target, const juce::String& typed)
{
    const auto t = typed.trim().toUpperCase();
    if (target.action != RemoteAction::parameter || t.isEmpty())
        return std::nullopt;
    const auto& p = target.param;
    if (p.kind == AutomationTargetKind::mixerPan || p.kind == AutomationTargetKind::channelPan)
    {
        if (t.startsWith ("C"))
            return 0.5f;
        const auto amount = juce::jlimit (0.0f, 100.0f, t.retainCharacters ("0123456789.").getFloatValue()) / 100.0f;
        const auto left = t.containsChar ('L') || t.startsWith ("-");
        return 0.5f + (left ? -0.5f : 0.5f) * amount;
    }
    if (p.kind == AutomationTargetKind::tempo)
        return juce::jlimit (0.0f, 1.0f, valueFromTempo (t.retainCharacters ("0123456789.").getDoubleValue()));
    if (t.startsWith ("-INF") || t.startsWith (juce::String::fromUTF8 ("\xe2\x88\x92\xe2\x88\x9e")))
        return 0.0f;
    // any other unit: the value whose text shows the number closest to the one typed
    const auto number = t.replace (juce::String::fromUTF8 ("\xe2\x88\x92"), "-").retainCharacters ("0123456789.-");
    if (! number.containsAnyOf ("0123456789"))
        return std::nullopt;
    const auto wanted = number.getDoubleValue();
    auto best = -1.0f;
    auto bestGap = 1.0e300;
    for (int i = 0; i <= 1000; ++i)
    {
        const auto v = (float) i / 1000.0f;
        const auto shown = targetValueTextAt (engine, p, v).replace (juce::String::fromUTF8 ("\xe2\x88\x92"), "-");
        if (! shown.containsAnyOf ("0123456789"))
            continue;
        const auto gap = std::abs (shown.retainCharacters ("0123456789.-").getDoubleValue() - wanted);
        if (gap < bestGap)
        {
            bestGap = gap;
            best = v;
        }
    }
    if (best < 0.0f)
        return juce::jlimit (0.0f, 1.0f, (float) wanted / 100.0f); // (no unit: percent)
    return best;
}

juce::Colour modeColour (remotemap::Mode mode)
{
    return mode == remotemap::Mode::key ? theme::colour::warning : theme::colour::info;
}

// --- the overlay ------------------------------------------------------------------------------------------------------

Overlay::Overlay (AudioEngine& e, juce::Component& r) : engine (e), root (r)
{
    setTitle (tr ("Map mode"));
    setWantsKeyboardFocus (false);
    setAlwaysOnTop (true);
}

Overlay::~Overlay() { stopTimer(); }

void Overlay::setMode (remotemap::Mode m)
{
    mode = m;
    selected.reset();
    if (mode == remotemap::Mode::off)
        stopTimer();
    else
        startTimerHz (15);
    refresh();
}

void Overlay::select (const std::optional<remotemap::Target>& target)
{
    selected = target;
    repaint();
}

void Overlay::tell (const juce::String& text)
{
    if (onMessage != nullptr)
        onMessage (text);
}

juce::String Overlay::refusalText (remotemap::Refusal r) const
{
    switch (r)
    {
        case remotemap::Refusal::sameTrack: return tr ("This control already launches another clip slot of the same track");
        case remotemap::Refusal::usedBySlot: return tr ("This control already launches a clip slot: it can't launch a scene too");
        case remotemap::Refusal::invalid: return tr ("Nothing to map here");
        case remotemap::Refusal::none: break;
    }
    return {};
}

bool Overlay::assignKey (int key)
{
    if (mode != remotemap::Mode::key || ! selected)
        return false;
    auto result = remotemap::Refusal::none;
    engine.editSong ([&, t = *selected] (Song& s) { result = remotemap::assignKey (s, t, key); });
    if (result != remotemap::Refusal::none)
    {
        tell (refusalText (result));
        return false;
    }
    tell (tr ("Key {0} mapped to {1}", { keyText (key), describe (engine, *selected).second }));
    repaint();
    return true;
}

bool Overlay::assignMidi (const remotemap::MidiControl& control)
{
    if (mode != remotemap::Mode::midi || ! selected)
        return false;
    auto result = remotemap::Refusal::none;
    engine.editSong ([&, t = *selected] (Song& s) { result = remotemap::assignMidi (s, t, control); });
    if (result != remotemap::Refusal::none)
    {
        tell (refusalText (result));
        return false;
    }
    tell (tr ("{0} mapped to {1}", { assignedText ({ true, control.channel, control.number, 0 }, mode), describe (engine, *selected).second }));
    repaint();
    return true;
}

bool Overlay::unmapSelected()
{
    if (mode == remotemap::Mode::off || ! selected)
        return false;
    auto removed = 0;
    engine.editSong ([&, t = *selected, m = mode] (Song& s) { removed = remotemap::unmap (s, t, m); });
    repaint();
    return removed > 0;
}

void Overlay::collect (juce::Component& c, std::vector<Spot>& out)
{
    // (visibility is walked down from the root, so this works off-screen too, where nothing "is showing")
    if (! c.isVisible() || c.getWidth() <= 0 || c.getHeight() <= 0 || &c == this || dynamic_cast<MappingBrowser*> (&c) != nullptr)
        return;
    if (auto* source = dynamic_cast<SpotSource*> (&c); source != nullptr)
    {
        std::vector<Spot> own;
        source->mapSpots (own);
        const auto visible = getLocalArea (&c, c.getLocalBounds());
        for (auto& s : own)
            if (const auto area = getLocalArea (&c, s.area).getIntersection (visible); ! area.isEmpty() && remotemap::valid (s.target))
            {
                out.push_back ({ area, s.target });
                owners.push_back (&c);
            }
    }
    else if (const auto t = tagOf (c); t && remotemap::valid (*t))
    {
        out.push_back ({ getLocalArea (&c, c.getLocalBounds()), *t });
        owners.push_back (&c);
    }
    for (auto* child : c.getChildren())
        collect (*child, out);
}

void Overlay::refresh()
{
    found.clear();
    owners.clear();
    if (mode != remotemap::Mode::off)
    {
        std::vector<Spot> all;
        for (auto* child : root.getChildren())
            collect (*child, all);
        // only what is not covered: the component under the element's middle must be its own (a floating panel in front
        // of the grid hides the slots behind it)
        collecting = true;
        for (std::size_t i = 0; i < all.size(); ++i)
        {
            const auto centre = all[i].area.getCentre();
            const auto* top = root.getComponentAt (root.getLocalPoint (this, centre));
            if (top == owners[i] || (top != nullptr && owners[i]->isParentOf (top)))
                found.push_back (all[i]);
        }
        collecting = false;
        owners.clear();
    }
    // a selection whose element went away (deleted track, scrolled out) stays selected; it just isn't drawn
    repaint();
}

void Overlay::timerCallback()
{
    if (mode == remotemap::Mode::midi)
        if (const auto caught = engine.takeMapCapture())
        {
            if (selected)
                assignMidi (*caught);
            else
                tell (tr ("MIDI Map Mode: click an element first, then move a control on your MIDI device"));
        }
    refresh();
}

int Overlay::spotAt (juce::Point<int> p) const
{
    for (int i = (int) found.size() - 1; i >= 0; --i) // (the innermost element was added last)
        if (found[(std::size_t) i].area.contains (p))
            return i;
    return -1;
}

bool Overlay::hitTest (int x, int y)
{
    return ! collecting && mode != remotemap::Mode::off && spotAt ({ x, y }) >= 0;
}

void Overlay::mouseDown (const juce::MouseEvent& e)
{
    const auto i = spotAt (e.getPosition());
    if (i < 0)
        return;
    selected = found[(std::size_t) i].target;
    const auto name = describe (engine, *selected);
    tell (mode == remotemap::Mode::midi ? tr ("MIDI Map Mode: move a control or press a key on your MIDI device to map {0}", { name.second })
                                        : tr ("Key Map Mode: press a computer key to map {0}", { name.second }));
    repaint();
}

void Overlay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // (a scroll over a highlighted element still scrolls what is under it)
    setInterceptsMouseClicks (false, false);
    if (auto* under = root.getComponentAt (root.getLocalPoint (this, e.getPosition())); under != nullptr && under != this)
        under->mouseWheelMove (e.getEventRelativeTo (under), wheel);
    setInterceptsMouseClicks (true, false);
}

void Overlay::paint (juce::Graphics& g)
{
    if (mode == remotemap::Mode::off)
        return;
    const auto colour = modeColour (mode);
    const auto& song = engine.getSong();
    g.setFont (theme::font (theme::type::caption, true));
    for (const auto& s : found)
    {
        const auto r = s.area.toFloat().reduced (0.5f);
        g.setColour (colour.withAlpha (0.30f));
        g.fillRect (r);
        g.setColour (colour.withAlpha (0.85f));
        g.drawRect (r, 1.0f);
        const auto a = mode == remotemap::Mode::key ? remotemap::keyOf (song, s.target) : remotemap::midiOf (song, s.target);
        if (a.found)
        {
            const auto text = assignedText (a, mode);
            const auto w = juce::jmin ((float) s.area.getWidth(), (float) text.length() * 6.5f + 6.0f);
            const juce::Rectangle<float> box (r.getX(), r.getY(), w, juce::jmin (13.0f, r.getHeight()));
            g.setColour (colour);
            g.fillRect (box);
            g.setColour (theme::colour::bg0);
            g.drawText (text, box, juce::Justification::centred, false);
        }
        if (selected && *selected == s.target)
        {
            g.setColour (theme::colour::textPrimary);
            g.drawRect (r.expanded (1.0f), 2.0f);
        }
    }
}

// --- the Mapping Browser ----------------------------------------------------------------------------------------------

MappingBrowser::MappingBrowser (AudioEngine& e) : engine (e)
{
    setTitle (tr ("Mappings"));
    setWantsKeyboardFocus (true);
    startTimerHz (5);
}

MappingBrowser::~MappingBrowser() { stopTimer(); }

void MappingBrowser::setMode (remotemap::Mode m)
{
    mode = m == remotemap::Mode::off ? remotemap::Mode::midi : m;
    selectedRows.clear();
    reload();
}

void MappingBrowser::selectRow (int row)
{
    selectedRows = { row };
    repaint();
}

void MappingBrowser::reload()
{
    auto now = remotemap::rows (engine.getSong(), mode);
    if (now != list)
    {
        list = std::move (now);
        selectedRows.erase (std::remove_if (selectedRows.begin(), selectedRows.end(), [this] (int r) { return r >= (int) list.size(); }), selectedRows.end());
        repaint();
    }
}

void MappingBrowser::timerCallback()
{
    if (dragRow < 0)
        reload();
}

std::vector<int> MappingBrowser::columnWidths() const
{
    // MIDI: Channel, Note/Control, Path, Name, Min, Max; Key: Key, Path, Name, Min, Max (the lesson's picture)
    // (narrow fixed columns, so Path and Name get what is left; the Browser is often ~250 px wide)
    const auto w = getWidth();
    const auto range = juce::jlimit (40, 64, w / 8);
    if (mode == remotemap::Mode::key)
    {
        // (round 190em: room for "Key w")
        const auto key = juce::jlimit (50, 72, w / 5), rest = juce::jmax (40, w - key - 2 * range);
        return { key, rest * 2 / 5, rest - rest * 2 / 5, range, range };
    }
    const auto channel = juce::jlimit (22, 56, w / 12), control = juce::jlimit (40, 80, w / 9), rest = juce::jmax (40, w - channel - control - 2 * range);
    return { channel, control, rest * 2 / 5, rest - rest * 2 / 5, range, range };
}

bool MappingBrowser::isMinMaxColumn (int column, bool& isMax) const
{
    const auto count = (int) columnWidths().size();
    isMax = column == count - 1;
    return column >= count - 2;
}

int MappingBrowser::columnAt (int x) const
{
    auto left = 0, i = 0;
    for (const auto w : columnWidths())
    {
        if (x >= left && x < left + w)
            return i;
        left += w;
        ++i;
    }
    return -1;
}

int MappingBrowser::rowAt (int y) const
{
    const auto r = (y - titleHeight - headerHeight) / rowHeight;
    return y >= titleHeight + headerHeight && r >= 0 && r < (int) list.size() ? r : -1;
}

void MappingBrowser::paint (juce::Graphics& g)
{
    namespace c = theme::colour;
    g.fillAll (c::bg2);
    auto area = getLocalBounds();
    // title band ("MIDI Mappings" / "Key Mappings")
    const auto title = area.removeFromTop (titleHeight);
    g.setColour (c::bg3);
    g.fillRect (title);
    g.setColour (c::textPrimary);
    g.setFont (theme::font (theme::type::body, true));
    g.drawText (mode == remotemap::Mode::key ? tr ("Key Mappings") : tr ("MIDI Mappings"), title.withTrimmedLeft (8), juce::Justification::centredLeft);
    g.setColour (modeColour (mode));
    g.fillRect (title.getX(), title.getBottom() - 2, title.getWidth(), 2);

    const auto widths = columnWidths();
    const juce::StringArray heads = mode == remotemap::Mode::key
                                        ? juce::StringArray { trc ("@computer key: Key"), tr ("Path"), tr ("Name"), tr ("Min"), tr ("Max") }
                                        : juce::StringArray { widths[0] < 44 ? juce::String ("Ch") : tr ("Channel"), tr ("Note/Control"), tr ("Path"), tr ("Name"), tr ("Min"), tr ("Max") };
    auto header = area.removeFromTop (headerHeight);
    g.setColour (c::bg4);
    g.fillRect (header);
    g.setFont (theme::font (theme::type::small, true));
    {
        auto x = 0;
        for (int i = 0; i < heads.size(); ++i)
        {
            g.setColour (c::textSecondary);
            g.drawText (heads[i], x + 5, header.getY(), widths[(std::size_t) i] - 8, header.getHeight(), juce::Justification::centredLeft, true);
            g.setColour (c::border);
            g.fillRect (x + widths[(std::size_t) i] - 1, header.getY(), 1, header.getHeight());
            x += widths[(std::size_t) i];
        }
    }

    g.setFont (theme::font (theme::type::small));
    for (int r = 0; r < (int) list.size(); ++r)
    {
        const auto& row = list[(std::size_t) r];
        const juce::Rectangle<int> line (0, titleHeight + headerHeight + r * rowHeight, getWidth(), rowHeight);
        if (line.getY() > getHeight())
            break;
        const auto chosen = std::find (selectedRows.begin(), selectedRows.end(), r) != selectedRows.end();
        g.setColour (chosen ? c::accentMuted : (r % 2 == 0 ? c::bg2 : c::bg1));
        g.fillRect (line);
        const auto [path, name] = describe (engine, row.target);
        juce::StringArray cells;
        if (mode == remotemap::Mode::key)
            cells = { listKeyText (row.key), path, name };
        else
            cells = { row.channel < 0 ? tr ("All") : juce::String (row.channel + 1), juce::String (remotemap::controlText (row.number)), path, name };
        auto x = 0;
        for (int i = 0; i < (int) widths.size(); ++i)
        {
            const auto w = widths[(std::size_t) i];
            const juce::Rectangle<int> cell (x, line.getY(), w, rowHeight);
            bool isMax = false;
            if (isMinMaxColumn (i, isMax))
            {
                if (row.hasRange)
                {
                    const auto box = cell.reduced (3, 2);
                    g.setColour (isMax ? modeColour (mode).withAlpha (0.55f) : c::bg5);
                    g.fillRect (box);
                    g.setColour (c::textPrimary);
                    g.drawText (rangeText (engine, row.target, isMax ? row.max : row.min), box, juce::Justification::centred, true);
                }
            }
            else
            {
                g.setColour (c::textPrimary);
                g.drawText (cells[i], cell.reduced (5, 0), juce::Justification::centredLeft, true);
            }
            x += w;
        }
    }
    if (list.empty())
    {
        g.setColour (c::textSecondary);
        g.drawFittedText (mode == remotemap::Mode::key ? tr ("Click an element, then press a key to map it.")
                                                       : tr ("Click an element, then move a control on your MIDI device to map it."),
                          area.reduced (10).withHeight (60), juce::Justification::centredTop, 3);
    }
}

void MappingBrowser::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto r = rowAt (e.y);
    if (r < 0)
    {
        selectedRows.clear();
        repaint();
        return;
    }
    if (e.mods.isPopupMenu())
    {
        if (std::find (selectedRows.begin(), selectedRows.end(), r) == selectedRows.end())
            selectedRows = { r };
        repaint();
        rowMenu (r).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
        return;
    }
    if (e.mods.isCommandDown() || e.mods.isShiftDown())
    {
        if (const auto it = std::find (selectedRows.begin(), selectedRows.end(), r); it != selectedRows.end())
            selectedRows.erase (it);
        else
            selectedRows.push_back (r);
    }
    else
        selectedRows = { r };
    bool isMax = false;
    if (isMinMaxColumn (columnAt (e.x), isMax) && list[(std::size_t) r].hasRange)
    {
        dragRow = r;
        dragMax = isMax;
        dragStart = isMax ? list[(std::size_t) r].max : list[(std::size_t) r].min;
    }
    repaint();
}

void MappingBrowser::mouseDrag (const juce::MouseEvent& e)
{
    if (dragRow < 0 || dragRow >= (int) list.size())
        return;
    const auto fine = e.mods.isCommandDown() ? 0.1f : 1.0f;
    const auto v = juce::jlimit (0.0f, 1.0f, dragStart - (float) e.getDistanceFromDragStartY() / 200.0f * fine);
    editRange (dragRow, dragMax, v);
}

void MappingBrowser::mouseUp (const juce::MouseEvent&)
{
    dragRow = -1;
}

void MappingBrowser::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto r = rowAt (e.y);
    bool isMax = false;
    if (r >= 0 && isMinMaxColumn (columnAt (e.x), isMax) && list[(std::size_t) r].hasRange)
        typeRange (r, isMax);
}

void MappingBrowser::editRange (int r, bool isMax, float value)
{
    auto row = list[(std::size_t) r];
    const auto min = isMax ? row.min : value, max = isMax ? value : row.max;
    engine.editSong ([row, min, max] (Song& s) { remotemap::setRange (s, row, min, max); });
    list[(std::size_t) r].min = min;
    list[(std::size_t) r].max = max;
    repaint();
}

void MappingBrowser::typeRange (int r, bool isMax)
{
    const auto row = list[(std::size_t) r];
    auto* window = new juce::AlertWindow (isMax ? tr ("Max") : tr ("Min"), describe (engine, row.target).second, juce::MessageBoxIconType::NoIcon, this);
    const auto now = isMax ? row.max : row.min;
    window->addTextEditor ("value", rangeText (engine, row.target, now)); // (round 190em: in the parameter's unit)
    window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<MappingBrowser> self (this);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([self, window, r, isMax] (int result) {
        const std::unique_ptr<juce::AlertWindow> owned (window);
        if (result != 1 || self == nullptr || r >= (int) self->list.size())
            return;
        if (const auto v = rangeValueOf (self->engine, self->list[(std::size_t) r].target, owned->getTextEditorContents ("value")))
            self->editRange (r, isMax, juce::jlimit (0.0f, 1.0f, *v));
    }), false);
}

juce::PopupMenu MappingBrowser::rowMenu (int r)
{
    juce::PopupMenu menu;
    const auto has = r >= 0 && r < (int) list.size();
    menu.addItem (tr ("Invert Range"), has && list[(std::size_t) r].hasRange, false, [this, r] {
        if (r >= (int) list.size())
            return;
        const auto row = list[(std::size_t) r];
        engine.editSong ([row] (Song& s) { remotemap::invertRange (s, row); });
        reload();
    });
    menu.addItem (tr ("Delete") + "\t" + tr ("Del"), has, false, [this] { removeSelected(); });
    return menu;
}

void MappingBrowser::removeSelected()
{
    if (selectedRows.empty())
        return;
    // from the last row up, so the earlier rows keep their places
    auto rows = selectedRows;
    std::sort (rows.begin(), rows.end(), [this] (int a, int b) {
        const auto& x = list[(std::size_t) a];
        const auto& y = list[(std::size_t) b];
        return x.link != y.link ? x.link < y.link : x.index > y.index;
    });
    std::vector<remotemap::Row> gone;
    for (const auto r : rows)
        if (r >= 0 && r < (int) list.size())
            gone.push_back (list[(std::size_t) r]);
    engine.editSong ([gone] (Song& s) {
        for (const auto& row : gone)
            remotemap::removeRow (s, row);
    });
    selectedRows.clear();
    reload();
}

bool MappingBrowser::keyPressed (const juce::KeyPress& key)
{
    if (key.isKeyCode (juce::KeyPress::deleteKey) || key.isKeyCode (juce::KeyPress::backspaceKey))
    {
        removeSelected();
        return true;
    }
    return false;
}
} // namespace lynx::mapmode
