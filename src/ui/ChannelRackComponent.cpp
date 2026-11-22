#include "core/SelectionRename.h"
#include "core/FlSettings.h"
#include <unordered_map>
#include "ui/Motion.h"
#include "core/RackLayout.h"
#include "core/RackFilter.h"
#include "ui/FlMods.h"
#include "ui/EditorTools.h"
#include "core/FruityDance.h"
#include "core/VideoPlayer.h"
#include "ui/ColourPicker.h"
#include "ui/FileChoosers.h"
#include "ui/TrackIcons.h"
#include "core/TrackIcons.h"
#include "ui/PatcherEditor.h"
#include "ui/UiTrace.h"
#include "ui/TextCache.h"
#include "ui/ChannelRackComponent.h"
#include "core/NoteRepeat.h"
#include "ui/PaintAB.h"
#include "ui/ImageCaches.h"
#include "ui/PluginOffTag.h"
#include "ui/SpeechDialog.h"
#include "core/Patcherize.h"
#include "ui/QuickPick.h"
#include "core/ControlSurface.h"
#include "core/VoltageController.h"

#include "core/MixerOps.h"
#include "core/PlaylistOps.h"

#include "ui/AutomationUi.h"
#include "core/NoteNames.h"
#include "core/NoteTools.h"
#include "core/NoteEdit.h"
#include "core/FlSynths.h"
#include "core/MidiControls.h"
#include "ui/ToolWindow.h"
#include "ui/DialogFrame.h"
#include "ui/LoopStarterWindow.h"
#include "core/Rack.h"
#include "core/Slicer.h"
#include "ui/ChannelSettings.h"

#include "engine/Text.h"
#include "engine/ProjectFile.h"
#include "platform/Paths.h"
#include "ui/Dialogs.h"
#include "ui/HintBar.h"
#include "ui/ExportDialog.h"
#include "core/StandIns.h"
#include "ui/I18n.h"
#include "ui/SamplerImportUi.h"
#include "ui/LiveRemoteUi.h"
#include "ui/BrowserComponent.h"

#include "ui/PluginWindow.h"
#include "ui/Theme.h"
#include "ui/AudioEditor.h"
#include "ui/AutoSamplerDialog.h"
#include "core/RackOps.h"
#include "core/TransportEdit.h"
#include "core/ChannelBurn.h"
#include "core/ChannelLoops.h"
#include "core/SoundFont.h"
#include "core/Vfx.h"
#include "core/CrossPaste.h"   // issue #232 stage 2 (round 190nz)
#include "core/LayoutStyle.h"
#include "core/TrackStates.h" // round 190op (P4)
#include "ui/WindowClipboard.h"
#include "ui/SoundFontEditor.h"

namespace lynx
{
namespace
{
void sliceChannelToDrumRack (AudioEngine& engine, int channel); // (round 190ec, below)

// English name (used for new channel names stored in the song)
juce::String instrumentName (InstrumentKind k)
{
    switch (k)
    {
        case InstrumentKind::kick: return "Kick";
        case InstrumentKind::snare: return "Snare";
        case InstrumentKind::hat: return "Hat";
        case InstrumentKind::clap: return "Clap";
        case InstrumentKind::synth: return "Synth";
        case InstrumentKind::sampler: return "Sampler";
        case InstrumentKind::audioClip: return "Audio clip";
        case InstrumentKind::plugin: return "Plugin";
        case InstrumentKind::controller: return "Controller";
        case InstrumentKind::sakura: return "Sakura";
        case InstrumentKind::drumpad: return "Drumpad";
        case InstrumentKind::boobass: return "BooBass";
        case InstrumentKind::drumsynth: return "DrumSynth Live";
        case InstrumentKind::harmless: return "Harmless";
        case InstrumentKind::sytrus: return "Sytrus";
        case InstrumentKind::harmor: return "Harmor";
        case InstrumentKind::gms: return "GMS";
        case InstrumentKind::morphine: return "Morphine";
        case InstrumentKind::ogun: return "Ogun";
        case InstrumentKind::minisynth: return "MiniSynth";
        case InstrumentKind::dx10: return "Fruity DX10";
        case InstrumentKind::kepler: return "Kepler";
        case InstrumentKind::keplerExo: return "Kepler Exo";
        case InstrumentKind::poizone: return "PoiZone";
        case InstrumentKind::fruitkick: return "Fruit Kick";
        case InstrumentKind::bassdrum: return "BassDrum";
        case InstrumentKind::toxic: return "Toxic Biohazard";
        case InstrumentKind::threeosc: return "3x Osc";
        case InstrumentKind::autogun: return "Autogun";
        case InstrumentKind::transistorbass: return "Transistor Bass";
        case InstrumentKind::simsynth: return "SimSynth";
        case InstrumentKind::wasp: return "Wasp";
        case InstrumentKind::plucked: return "Plucked!";
        case InstrumentKind::beepmap: return "BeepMap";
        case InstrumentKind::sawer: return "Sawer";
        case InstrumentKind::flkeys: return "FL Keys";
        case InstrumentKind::granulizer: return "Fruity Granulizer";
        case InstrumentKind::drumaxx: return "Drumaxx";
        case InstrumentKind::slayer: return "FL Slayer";
        case InstrumentKind::fruitydance: return "Fruity Dance";
        case InstrumentKind::directwave: return "DirectWave";
        case InstrumentKind::midiOut: return "MIDI Out";
        case InstrumentKind::patcher: return "Patcher";
        case InstrumentKind::soundfont: return "LYNX Soundfont";
        case InstrumentKind::layer: return "Layer";
        case InstrumentKind::dashboard: return "Dashboard";
        case InstrumentKind::voltage: return "Voltage Controller";
        case InstrumentKind::vfx: return "VFX";
        case InstrumentKind::videoplayer: return "Fruity Video Player";
        case InstrumentKind::wavetraveller: return "Wave Traveller";
        case InstrumentKind::fruityslicer: return "Fruity Slicer";
        case InstrumentKind::slicex: return "Slicex";
        case InstrumentKind::fpc: return "FPC";
        case InstrumentKind::pocketSuperSaw: return "Pocket SuperSaw";
        case InstrumentKind::tetra: return "Tetra";
        case InstrumentKind::helix: return "Helix";
        case InstrumentKind::braid: return "Braid";
        case InstrumentKind::lynxModel: return "LYNX Model";
    }
    return "?";
}

// name shown in menus (translated)
juce::String instrumentLabel (InstrumentKind k)
{
    switch (k)
    {
        case InstrumentKind::kick: return tr ("Kick");
        case InstrumentKind::snare: return tr ("Snare");
        case InstrumentKind::hat: return tr ("Hat");
        case InstrumentKind::clap: return tr ("Clap");
        case InstrumentKind::synth: return tr ("Synth");
        case InstrumentKind::sampler: return tr ("Sampler");
        case InstrumentKind::audioClip: return tr ("Audio clip");
        case InstrumentKind::controller: return tr ("Envelope Controller");
        case InstrumentKind::midiOut: return tr ("MIDI Out");
        case InstrumentKind::layer: return tr ("Layer");
        case InstrumentKind::dashboard: return "Dashboard";
        case InstrumentKind::voltage: return "Voltage Controller";
        default: return instrumentName (k);
    }
}

void setStep (Pattern& p, int channel, int step, bool on, bool muteRemoved = false)
{
    if (p.hasStep (channel, step) != on)
        p.toggleStep (channel, step, muteRemoved);
}

juce::String channelName (const Song& song, int row)
{
    return row >= 0 && row < (int) song.channels.size() ? juce::String::fromUTF8 (song.channels[(std::size_t) row].name.c_str()) : juce::String();
}
} // namespace

bool ChannelRackComponent::keepEditorsOpen = false;
bool ChannelRackComponent::selectFirstChannelWithNotes = false;
bool ChannelRackComponent::autoSelectLinked = true;
bool ChannelRackComponent::muteRemovedSteps = false;
bool ChannelRackComponent::nameAlignLeft = false;

ChannelRackComponent::ChannelRackComponent (AudioEngine& e)
    : engine (e)
{
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
    // paints every pixel (paint() starts with an opaque fillAll): the panel behind it is no longer filled under it on every
    // frame (round 190ms, perf: the window frame's fill under the rack was ~2.5 M instructions a whole frame on JUCE's
    // software renderer). Linux only: on Direct2D the frame's clip then becomes a rectangle list (a geometry layer), and
    // the rack's frame got 2-9 % slower (round 190ms, Windows --paintbench); macOS was not measured.
   #if JUCE_LINUX || JUCE_BSD
    setOpaque (true);
   #endif
    engine.addChangeListener (this);
    engine.getLevelsBroadcaster().addChangeListener (this);
    previews.setSender ([this] (int channel, int key, float velocity, bool on) { engine.sendLiveNote (channel, key, velocity, on); });
    updateSize();
    showingTimer = std::make_unique<ui::TimerWhileShowing> (*this, static_cast<juce::Timer&> (*this), 30); // (stopped while hidden; round 174f)
}

ChannelRackComponent::~ChannelRackComponent()
{
    previews.releaseAll();
    engine.getLevelsBroadcaster().removeChangeListener (this);
    engine.removeChangeListener (this);
}

void ChannelRackComponent::moved()
{
    // the viewport scrolled the rack sideways: the controls column follows the visible left edge
    const auto x = juce::jmax (0, -getX());

    if (x != stickyX)
    {
        stickyX = x;
        repaint();
    }
}

void ChannelRackComponent::visibilityChanged()
{
    if (! isShowing())
        previews.releaseAll(); // hidden mid-gesture: nothing keeps sounding
}

int ChannelRackComponent::numSteps() const
{
    const auto& song = engine.getSong();

    if (song.patterns.empty())
        return 16;

    const auto& p = song.activePattern();
    const auto steps = (int) (p.effectiveLength (song.meter()) / ticksPerStep);
    return juce::jlimit (16, 512, steps);
}

PreviewSpan ChannelRackComponent::previewSpan() const
{
    auto visible = 0;
    if (const auto* holder = getParentComponent())
        visible = holder->getWidth() - gridLeft() - 8 - (advancedLoopShown() ? loopColumnWidth : 0);
    // (at least 8 steps wide: a rack squeezed narrower than that scrolls its miniatures too)
    return notePreviewSpan (numSteps() * stepWidth, juce::jmax (visible, 8 * stepWidth), stickyX, completePreview.load (std::memory_order_relaxed));
}

int ChannelRackComponent::shownSteps() const
{
    return juce::jmin (512, numSteps() + ghostSteps);
}

bool ChannelRackComponent::isShown (int row) const
{
    return rackfilter::shows (engine.getSong(), row, filterGroup);
}

int ChannelRackComponent::visibleIndex (int row) const
{
    if (filterGroup == rackfilter::all)
        return row;

    int index = 0;
    for (int r = 0; r < row && r < (int) engine.getSong().channels.size(); ++r)
        index += isShown (r) ? 1 : 0;
    return index + juce::jmax (0, row - (int) engine.getSong().channels.size());
}

int ChannelRackComponent::shownCount() const
{
    return visibleIndex ((int) engine.getSong().channels.size());
}

// round 190ld (DE-37): FL's Page Up / Page Down step the filter groups in the menu's order (All, Unsorted, Audio clips,
// then the project's groups), wrapping round
void ChannelRackComponent::stepFilterGroup (int direction)
{
    const auto& song = engine.getSong();
    std::vector<int> order { rackfilter::all, rackfilter::unsorted, rackfilter::audioClips };
    for (int g = 0; g < (int) song.channelGroups.size(); ++g)
        if (! rackfilter::isBuiltinName (song.channelGroups[(std::size_t) g]))
            order.push_back (g);
    const auto at = std::find (order.begin(), order.end(), filterGroup);
    const auto i = at == order.end() ? 0 : (int) (at - order.begin());
    const auto n = (int) order.size();
    setFilterGroup (order[(std::size_t) (((i + (direction < 0 ? -1 : 1)) % n + n) % n)]);
}

void ChannelRackComponent::setFilterGroup (int group)
{
    group = rackfilter::clamp (engine.getSong(), group);

    if (group == filterGroup)
        return;

    filterGroup = group;
    updateSize();
    repaint();

    if (onFilterChanged)
        onFilterChanged();
}

juce::String ChannelRackComponent::filterName() const
{
    const auto& groups = engine.getSong().channelGroups;
    if (filterGroup == rackfilter::audioClips)
        return tr ("Audio");
    if (filterGroup == rackfilter::unsorted)
        return tr ("Unsorted");
    return filterGroup >= 0 && filterGroup < (int) groups.size() ? juce::String::fromUTF8 (rackfilter::shownName (groups[(std::size_t) filterGroup]).c_str()) : tr ("All");
}

void ChannelRackComponent::showFilterMenu (juce::Component* target)
{
    const auto& song = engine.getSong();
    juce::PopupMenu menu;
    menu.addItem (tr ("All"), true, filterGroup == rackfilter::all, [this] { setFilterGroup (rackfilter::all); });
    // FL 2026's list (round 190oq, issue #246; REA_rodada190oq §3): "All", a line, then the groups A–Z — its built-in
    // "Audio" (the audio clip channels) and "Unsorted" (the channels in no group) among them, as LYNX's two views
    // (round 168t: offered for every project), then the project's own groups
    struct Entry
    {
        juce::String name;
        int filter;
    };
    std::vector<Entry> entries { { tr ("Audio"), rackfilter::audioClips }, { tr ("Unsorted"), rackfilter::unsorted } };
    for (int g = 0; g < (int) song.channelGroups.size(); ++g)
        if (! rackfilter::isBuiltinName (song.channelGroups[(std::size_t) g])) // (FL's own "Audio" / "Unsorted": the views above)
            entries.push_back ({ juce::String::fromUTF8 (rackfilter::shownName (song.channelGroups[(std::size_t) g]).c_str()), g });
    std::stable_sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    menu.addSeparator();
    for (const auto& e : entries)
        menu.addItem (e.name, true, filterGroup == e.filter, [this, f = e.filter] { setFilterGroup (f); });

    menu.addSeparator();
    menu.addItem (tr ("Group selected...") + "\tAlt+G", [this] { groupSelected(); });
    {
        juce::PopupMenu move;
        move.addItem (tr ("(no group)"), [this] { moveSelectedToGroup (-1); });
        for (int g = 0; g < (int) song.channelGroups.size(); ++g)
            move.addItem (juce::String::fromUTF8 (song.channelGroups[(std::size_t) g].c_str()), [this, g] { moveSelectedToGroup (g); });
        menu.addSubMenu (tr ("Move selected channels to"), move, ! engine.getSelectedChannels().empty());
    }
    menu.addItem (tr ("Rename this group..."), filterGroup >= 0, false, [this] {
        const auto group = filterGroup;
        auto* window = new juce::AlertWindow (tr ("Filter group name"), {}, juce::MessageBoxIconType::NoIcon);
        window->addTextEditor ("name", filterName());
        window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([this, group, window] (int result) {
                                     const auto name = window->getTextEditorContents ("name").trim().toStdString();
                                     if (result != 1 || name.empty())
                                         return;
                                     engine.editSong ([group, name] (Song& s) {
                                         if (group < (int) s.channelGroups.size())
                                             s.channelGroups[(std::size_t) group] = name;
                                     });
                                     if (onFilterChanged)
                                         onFilterChanged();
                                 }),
                                 true);
    });
    menu.addItem (tr ("Delete this group (keeps its channels)"), filterGroup >= 0, false, [this] {
        const auto group = filterGroup;
        setFilterGroup (-1);
        engine.editSong ([group] (Song& s) {
            if (group >= (int) s.channelGroups.size())
                return;
            s.channelGroups.erase (s.channelGroups.begin() + group);
            for (auto& c : s.channels)
                c.group = c.group == group ? -1 : c.group > group ? c.group - 1 : c.group;
        });
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target));
}

void ChannelRackComponent::moveSelectedToGroup (int group)
{
    engine.editSong ([group, rows = engine.getSelectedChannels()] (Song& s) {
        for (auto r : rows)
            if (r >= 0 && r < (int) s.channels.size())
                s.channels[(std::size_t) r].group = group >= 0 && group < (int) s.channelGroups.size() ? group : -1;
    });
    updateSize();
    repaint();
}

void ChannelRackComponent::groupSelected()
{
    // FL: Alt+G asks for the "Filter group name"; the selected channels move into the new group, which is shown
    auto* window = new juce::AlertWindow (tr ("Filter group name"), {}, juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", tr ("Group {0}", { juce::String ((int) engine.getSong().channelGroups.size() + 1) }));
    window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window] (int result) {
                                 const auto name = window->getTextEditorContents ("name").trim().toStdString();
                                 if (result != 1 || name.empty())
                                     return;
                                 const auto group = (int) engine.getSong().channelGroups.size();
                                 engine.editSong ([name, rows = engine.getSelectedChannels()] (Song& s) {
                                     s.channelGroups.push_back (name);
                                     for (auto r : rows)
                                         if (r >= 0 && r < (int) s.channels.size())
                                             s.channels[(std::size_t) r].group = (int) s.channelGroups.size() - 1;
                                 });
                                 setFilterGroup (group);
                             }),
                             true);
}

void ChannelRackComponent::updateSize()
{
    refreshZipLayout(); // (round 190mk: zipped channels' strips)
    refreshAutomationRows(); // (round 190oo, PAT-7: the automation clip rows, after the strips they keep whole)
    const auto rows = shownCount() + 1; // +1 = "add channel" row
    // FL's ghost steps: when the rack is wider than the pattern, the free width shows faint steps after its end;
    // clicking one adds a step there and makes the pattern longer (issue #11)
    ghostSteps = 0;
    const auto loopColumn = advancedLoopShown() ? loopColumnWidth : 0; // (FL's advanced looping controls, at the right)
    if (const auto* holder = getParentComponent())
        ghostSteps = juce::jmax (0, (holder->getWidth() - gridLeft() - 8 - loopColumn) / stepWidth - numSteps());
    setSize (gridLeft() + shownSteps() * stepWidth + 8 + loopColumn,
             (zipLayout.top.empty() ? rows * rowHeight : zipLayout.total) + (int) autoRows.size() * rowHeight + 8 + (graphRow() >= 0 ? laneHeight : 0));
}

// --- FL's Channel button width (round 190mk, DE-39) ---------------------------------------------------------------------
void ChannelRackComponent::setButtonWidth (int percent)
{
    buttonWidthPercent = percent <= 0 ? 0 : juce::jlimit (100, 400, percent);
    if (buttonWidthPercent > 0)
        nameWidth = widthForPercent (buttonWidthPercent);
    else // Size to fit: the longest name with the button's margins, between 100 % and 400 %
    {
        const auto font = theme::font (theme::type::body);
        float widest = 0.0f;
        for (const auto& c : engine.getSong().channels)
            widest = juce::jmax (widest, juce::GlyphArrangement::getStringWidth (font, juce::String::fromUTF8 (c.name.c_str())));
        nameWidth = juce::jlimit (widthForPercent (100), widthForPercent (400), (int) std::ceil (widest) + 34);
    }
    updateSize();
    repaint();
}

// --- FL's zipped channels (round 190mk, DE-38; capture 190mf A4, core/RackZip.h) ----------------------------------------

void ChannelRackComponent::refreshZipLayout()
{
    const auto& channels = engine.getSong().channels;
    if (std::none_of (channels.begin(), channels.end(), [] (const ChannelDef& c) { return c.zipped; }))
    {
        zipLayout = {}; // (no zipped channel: FL's fixed rows, the code paths as before)
        return;
    }
    std::vector<bool> zipped, shown;
    zipped.reserve (channels.size());
    shown.reserve (channels.size());
    for (int r = 0; r < (int) channels.size(); ++r)
    {
        zipped.push_back (channels[(std::size_t) r].zipped);
        shown.push_back (isShown (r));
    }
    zipLayout = rackzip::layout (zipped, shown, rowHeight);
}

juce::Rectangle<int> ChannelRackComponent::zipBar (int row) const
{
    if (! isZipped (row))
        return {};
    const auto slot = zipLayout.slot[(std::size_t) row];
    const auto top = rowY (row), height = rowHeight / 2;
    return { stickyX + nameLeft() + slot * (nameWidth + rackzip::barGap), top + (height - rackzip::barHeight) / 2, nameWidth, rackzip::barHeight };
}

int ChannelRackComponent::zipBarAt (int row, int x) const
{
    if (! isZipped (row))
        return -1;
    return rackzip::barAt (zipLayout, zipLayout.first[(std::size_t) row], x, stickyX + nameLeft(), nameWidth);
}

bool ChannelRackComponent::zipStripForSnapshot (int row, juce::Rectangle<int>& strip) const
{
    if (! isZipped (row))
        return false;
    strip = { 0, rowY (row), getWidth(), rowHeight / 2 };
    return true;
}

void ChannelRackComponent::paintZipStrips (juce::Graphics& g)
{
    const auto& song = engine.getSong();
    const auto clip = g.getClipBounds();
    for (int row = 0; row < (int) song.channels.size(); ++row)
    {
        if (! isZipped (row) || ! isShown (row))
            continue;
        const auto bar = zipBar (row);
        if (! bar.intersects (clip.expanded (0, 1)))
            continue;
        const auto& c = song.channels[(std::size_t) row];
        // FL: the bar in the channel's colour, darker at the bottom (its channel buttons' colour when none is set)
        const auto top = c.colour != 0 ? c.colour : (std::uint32_t) theme::colour::bg5.getARGB();
        const auto r = bar.toFloat();
        g.setGradientFill (juce::ColourGradient (juce::Colour (top), r.getX(), r.getY(), juce::Colour (rackzip::barBottom (top)), r.getX(), r.getBottom(), false));
        g.fillRect (r);
        if (hoverArea == Area::zipped && hoverRow == row)
        {
            g.setColour (theme::colour::textPrimary.withAlpha (0.6f));
            g.drawRect (r.expanded (1.0f), 1.0f);
        }
    }
}

void ChannelRackComponent::zipSelected()
{
    const auto rows = engine.getSelectedChannels();
    if (rows.empty())
        return;
    engine.editSong ([rows] (Song& s) {
        for (const auto r : rows)
            if (r >= 0 && r < (int) s.channels.size())
                s.channels[(std::size_t) r].zipped = true;
    });
    updateSize();
    repaint();
}

void ChannelRackComponent::unzipAll()
{
    const auto& channels = engine.getSong().channels;
    if (std::none_of (channels.begin(), channels.end(), [] (const ChannelDef& c) { return c.zipped; }))
        return;
    engine.editSong ([] (Song& s) {
        for (auto& c : s.channels)
            c.zipped = false;
    });
    updateSize();
    repaint();
}

void ChannelRackComponent::unzip (int row)
{
    if (row < 0 || row >= (int) engine.getSong().channels.size() || ! engine.getSong().channels[(std::size_t) row].zipped)
        return;
    engine.editSong ([row] (Song& s) { s.channels[(std::size_t) row].zipped = false; });
    updateSize();
    repaint();
}

int ChannelRackComponent::graphRow() const
{
    const auto count = (int) engine.getSong().channels.size();
    const auto row = graphLanes && count > 0 ? juce::jlimit (0, count - 1, engine.getSelectedChannel()) : -1;
    return row >= 0 && isShown (row) && ! isZipped (row) ? row : -1;
}

int ChannelRackComponent::rowY (int row) const
{
    const auto base = baseRowY (row);
    if (autoBefore.empty() || row < 0) // (round 190oo: no automation clip rows, the rows as before)
        return base;
    return base + autoBefore[(std::size_t) juce::jmin (row, (int) autoBefore.size() - 1)] * rowHeight;
}

int ChannelRackComponent::baseRowY (int row) const
{
    const auto lane = graphRow();
    const auto offset = lane >= 0 && row > lane ? laneHeight : 0;
    if (const auto count = (int) zipLayout.top.size() - 1; count >= 0 && row >= 0) // (round 190mk: zipped strips)
        return (row <= count ? zipLayout.top[(std::size_t) row] : zipLayout.top[(std::size_t) count] + (row - count) * rowHeight) + offset;
    return 4 + visibleIndex (row) * rowHeight + offset;
}

int ChannelRackComponent::rowAtY (int y) const
{
    if (y < 4)
        return -1;

    // round 190oo (PAT-7): the automation clip rows above y move it up a row each; on one, the channel row under it
    // answers (the drags and drops that look for a channel; areaAt finds the automation row itself first)
    if (! autoRows.empty())
    {
        int above = 0;
        for (int i = 0; i < (int) autoRows.size(); ++i)
        {
            const auto top = automationRowTop (i);
            if (y >= top + rowHeight)
                ++above;
            else
            {
                if (y >= top)
                    return autoRows[(std::size_t) i].gap;
                break;
            }
        }
        y -= above * rowHeight;
    }

    if (const auto lane = graphRow(); lane >= 0)
    {
        const auto laneTop = baseRowY (lane) + rowHeight;
        if (y >= laneTop && y < laneTop + laneHeight)
            return lane;
        if (y >= laneTop + laneHeight)
            y -= laneHeight;
    }

    if (! zipLayout.top.empty()) // (round 190mk: zipped strips; a strip answers with its first row)
        return rackzip::rowAt (zipLayout, y, rowHeight);

    const auto index = (y - 4) / rowHeight;

    if (filterGroup == rackfilter::all)
        return index;

    // the index-th shown row (the "add channel" row after the last one)
    const auto count = (int) engine.getSong().channels.size();
    int seen = 0;
    for (int r = 0; r < count; ++r)
        if (isShown (r) && seen++ == index)
            return r;
    return count + (index - seen);
}

juce::Rectangle<int> ChannelRackComponent::laneBounds() const
{
    const auto lane = graphRow();
    return lane < 0 ? juce::Rectangle<int>() : juce::Rectangle<int> (0, rowY (lane) + rowHeight, getWidth(), laneHeight);
}

juce::Rectangle<int> ChannelRackComponent::rowBounds (int row) const
{
    return { 0, rowY (row), getWidth(), rowHeight };
}

juce::Rectangle<int> ChannelRackComponent::stepBounds (int row, int step) const
{
    // FL's step keycaps (round 190io, FM-11): 15 x 20 at an 18 px pitch (FL: 13 x 20 at 16; a bit wider for the mouse)
    return { gridLeft() + step * stepWidth + 1, rowY (row) + 5, stepWidth - 3, rowHeight - 10 };
}

// --- channel looping (FL 20.5+, round 165f-rack) ------------------------------------------------------

int ChannelRackComponent::loopStepsOf (int row) const
{
    const auto& song = engine.getSong();
    if (song.patterns.empty() || row < 0 || row >= (int) song.channels.size())
        return 0;
    const auto& p = song.activePattern();
    if (! p.loopOn)
        return 0;
    const auto length = p.effectiveLength (song.meter());
    const auto loop = channelloops::loopLength (p, row, channelloops::effectiveSetting (p, row), length, song.meter());
    return loop > 0 && loop < length ? (int) (loop / ticksPerStep) : 0;
}

bool ChannelRackComponent::advancedLoopShown() const
{
    const auto& song = engine.getSong();
    if (song.patterns.empty())
        return false;
    const auto& p = song.activePattern();
    return p.loopOn && p.loopMode == RackLoopMode::advanced;
}

juce::Rectangle<int> ChannelRackComponent::loopBoxBounds (int row) const
{
    // the right edge of what the viewport shows (the column stays there while the steps scroll, like the controls)
    auto right = getWidth();
    if (const auto* holder = getParentComponent())
        right = juce::jmin (getWidth(), stickyX + holder->getWidth());
    return { right - loopColumnWidth + 3, rowY (row) + 5, loopColumnWidth - 8, rowHeight - 10 };
}

void ChannelRackComponent::paintLoopColumn (juce::Graphics& g)
{
    using namespace theme::colour;
    const auto& song = engine.getSong();
    const auto& p = song.activePattern();
    const auto first = loopBoxBounds (0);
    g.setColour (bg2);
    g.fillRect (first.getX() - 3, 0, loopColumnWidth, getHeight());
    g.setColour (border);
    g.fillRect (first.getX() - 4, 0, 1, getHeight());
    g.setFont (theme::font (theme::type::caption, true));
    for (int row = 0; row < (int) song.channels.size(); ++row)
    {
        if (! isShown (row) || isZipped (row) || song.channels[(std::size_t) row].instrument == InstrumentKind::audioClip)
            continue;
        const auto box = loopBoxBounds (row);
        const auto setting = channelloops::effectiveSetting (p, row);
        const auto hovered = (hoverArea == Area::loop && hoverRow == row) || loopDragRow == row;
        theme::fillRoundRect (g, box.toFloat(), theme::radius::sm, hovered ? bg4 : bg0);
        if (setting != ChannelLoop::dontLoop)
        {
            g.setColour (accent.withAlpha (hovered ? 0.9f : 0.6f));
            g.drawRoundedRectangle (box.toFloat().reduced (0.5f), theme::radius::sm, 1.0f);
        }
        g.setColour (setting == ChannelLoop::dontLoop ? textDisabled : textPrimary);
        const auto text = setting > 0 ? juce::String (setting) : setting == ChannelLoop::dontLoop ? juce::String ("...")
                        : tr (channelloops::settingText (setting).c_str());
        textcache::drawText (g, text, box, juce::Justification::centred, true);
    }
    g.setFont (theme::font (theme::type::body));
}

void ChannelRackComponent::setLoopPoint (int row, int setting)
{
    if (row < 0 || row >= (int) engine.getSong().channels.size())
        return;
    engine.editSong ([row, setting] (Song& s) {
        auto& p = s.activePattern();
        auto value = setting;
        // FL's "Set to next beat / bar": a fixed loop point, the steps to the boundary after the channel's last note
        if (setting == 1000 + ChannelLoop::nextBeat || setting == 1000 + ChannelLoop::nextBar)
            value = channelloops::nextBoundarySteps (p, row, setting - 1000, s.meter());
        channelloops::setSetting (p, row, value);
        // (FL switches the looping on, advanced, when a loop point is set from a channel)
        if (! p.loopOn && value != ChannelLoop::dontLoop)
        {
            p.loopOn = true;
            p.loopMode = RackLoopMode::advanced;
        }
    });
    updateSize();
    repaint();
}

void ChannelRackComponent::showLoopPointMenu (int row)
{
    const auto& song = engine.getSong();
    if (row < 0 || row >= (int) song.channels.size())
        return;
    const auto& p = song.activePattern();
    const auto setting = channelloops::effectiveSetting (p, row);
    juce::PopupMenu menu;
    ui::addItemHeader (menu, tr ("Loop point"), channelName (song, row), {});
    menu.addItem (tr ("Don't loop"), true, setting == ChannelLoop::dontLoop, [this, row] { setLoopPoint (row, ChannelLoop::dontLoop); });
    menu.addSeparator();
    menu.addItem (tr ("Step") + " - " + tr ("loop at the next step"), true, setting == ChannelLoop::nextStep, [this, row] { setLoopPoint (row, ChannelLoop::nextStep); });
    menu.addItem (tr ("Beat") + " - " + tr ("loop at the next beat"), true, setting == ChannelLoop::nextBeat, [this, row] { setLoopPoint (row, ChannelLoop::nextBeat); });
    menu.addItem (tr ("Bar") + " - " + tr ("loop at the next bar"), true, setting == ChannelLoop::nextBar, [this, row] { setLoopPoint (row, ChannelLoop::nextBar); });
    menu.addSeparator();
    menu.addItem (tr ("Set to next beat"), [this, row] { setLoopPoint (row, 1000 + ChannelLoop::nextBeat); });
    menu.addItem (tr ("Set to next bar"), [this, row] { setLoopPoint (row, 1000 + ChannelLoop::nextBar); });
    if (setting > 0)
        menu.addItem (tr ("Every {0} steps", { juce::String (setting) }), false, true, [] {});
    menu.addSectionHeader (tr ("Tools"));
    menu.addItem (tr ("Burn to pattern"), loopStepsOf (row) > 0, false, [this, row] {
        engine.editSong ([row] (Song& s) { channelloops::burnChannel (s.activePattern(), row, s.meter()); });
        repaint();
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (loopBoxBounds (row))));
}

bool ChannelRackComponent::isLoopingOn() const
{
    const auto& song = engine.getSong();
    return ! song.patterns.empty() && song.activePattern().loopOn;
}

void ChannelRackComponent::toggleLooping()
{
    engine.editSong ([] (Song& s) {
        auto& p = s.activePattern();
        p.loopOn = ! p.loopOn;
    });
    updateSize();
    repaint();
    if (onFilterChanged)
        onFilterChanged(); // (the title bar's Loop button)
}

void ChannelRackComponent::burnLoops (bool selectedOnly)
{
    const auto rows = engine.getSelectedChannels();
    engine.editSong ([rows, selectedOnly] (Song& s) {
        auto& p = s.activePattern();
        if (selectedOnly)
        {
            for (auto r : rows)
                channelloops::burnChannel (p, r, s.meter());
        }
        else
            channelloops::burnChannels (p, {}, s.meter());
    });
    repaint();
}

void ChannelRackComponent::showLoopMenu (juce::Component* target)
{
    const auto& song = engine.getSong();
    if (song.patterns.empty())
        return;
    const auto& p = song.activePattern();
    juce::PopupMenu menu;
    menu.addItem (tr ("Looping on"), true, p.loopOn, [this] { toggleLooping(); });
    menu.addSectionHeader (tr ("Loop type"));
    const std::pair<RackLoopMode, const char*> types[] = { { RackLoopMode::steps, TR_NOOP ("Loop step channels") },
                                                           { RackLoopMode::all, TR_NOOP ("Loop all channels") },
                                                           { RackLoopMode::advanced, TR_NOOP ("Advanced looping") } };
    for (const auto& [mode, name] : types)
        menu.addItem (tr (name), true, p.loopMode == mode, [this, mode = mode] {
            engine.editSong ([mode] (Song& s) {
                auto& q = s.activePattern();
                q.loopMode = mode;
                q.loopOn = true; // (FL: picking a type switches the looping on)
            });
            updateSize();
            repaint();
            if (onFilterChanged)
                onFilterChanged();
        });
    menu.addSectionHeader (tr ("Burn to pattern"));
    const auto looping = ! channelloops::loopingChannels (p, song.meter()).empty();
    menu.addItem (tr ("Burn selected channels"), looping, false, [this] { burnLoops (true); });
    menu.addItem (tr ("Burn all looping channels"), looping, false, [this] { burnLoops (false); });
    auto options = juce::PopupMenu::Options();
    if (target != nullptr)
        options = options.withTargetComponent (target);
    menu.showMenuAsync (options);
}

void ChannelRackComponent::burnMidi (int channel, bool toNewPattern)
{
    const auto& song = engine.getSong();
    if (song.patterns.empty() || channel < 0 || channel >= (int) song.channels.size())
        return;
    if (channelburn::playedNotes (song, song.currentPattern, channel).empty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Nothing to burn"),
                                                tr ("The selected channel doesn't contain any notes to burn."));
        return;
    }
    const auto baked = channelburn::whatBurns (song, song.currentPattern, channel);
    auto run = [this, channel, toNewPattern] (channelburn::Reset reset) {
        engine.editSong ([channel, toNewPattern, reset] (Song& s) { channelburn::burnMidi (s, channel, toNewPattern, reset); });
        updateSize();
        repaint();
    };
    if (! (baked.arpeggiator || baked.echo || baked.swing || baked.time))
    {
        run ({});
        return;
    }
    // FL's "Reset after burning to MIDI": what the burn wrote into the notes is switched off, or it would play twice
    struct ResetDialog final : juce::AlertWindow
    {
        explicit ResetDialog (const channelburn::Baked& baked)
            : juce::AlertWindow (tr ("Reset after burning to MIDI"), tr ("Select what you want to reset:"), juce::MessageBoxIconType::QuestionIcon)
        {
            const std::pair<const char*, bool> items[] = { { TR_NOOP ("Arpeggiator"), baked.arpeggiator }, { TR_NOOP ("Echo delay"), baked.echo },
                                                           { TR_NOOP ("Swing mix"), baked.swing }, { TR_NOOP ("Time (shift and gate)"), baked.time } };
            for (std::size_t i = 0; i < toggles.size(); ++i)
            {
                toggles[i].setButtonText (tr (items[i].first));
                toggles[i].setToggleState (items[i].second, juce::dontSendNotification);
                toggles[i].setEnabled (items[i].second);
                toggles[i].setSize (300, 24);
                addCustomComponent (&toggles[i]);
            }
            addButton (tr ("Burn"), 1, juce::KeyPress (juce::KeyPress::returnKey));
            addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        }
        channelburn::Reset reset() const
        {
            return { toggles[0].getToggleState(), toggles[1].getToggleState(), toggles[2].getToggleState(), toggles[3].getToggleState() };
        }
        std::array<juce::ToggleButton, 4> toggles;
    };
    auto* w = new ResetDialog (baked);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, run] (int result) {
        if (result == 1)
            run (w->reset());
    }), true);
}

ChannelRackComponent::Area ChannelRackComponent::areaAt (int x, int y, int& row) const
{
    const auto channels = (int) engine.getSong().channels.size();
    const auto rackX = x;

    if (x < stickyX + gridLeft()) // the controls column (painted at the visible left edge)
        x -= stickyX;

    // round 190oo (PAT-7): an automation clip row (FL's automation clip channel): its button, or the rest of the row
    if (const auto automationRow = autoRows.empty() ? -1 : automationRowAt (y); automationRow >= 0)
    {
        row = autoRows[(std::size_t) automationRow].automation;
        // (round 190ow: FL's row is a channel's: mute light, MIN / MAX knobs, the glyph, the button, selector, steps)
        if (x < muteWidth)
            return Area::autoMute;
        if (x < muteWidth + knobWidth)
            return Area::autoMin;
        if (x < muteWidth + 2 * knobWidth)
            return Area::autoMax;
        if (x < nameLeft())
            return Area::autoRow;
        if (x < selectLeft())
            return Area::autoName;
        if (x < gridLeft())
            return Area::autoSelect;
        const auto step = (x - gridLeft()) / stepWidth;
        return step >= 0 && step < shownSteps() && ! pianoRollView ? Area::autoStep : Area::autoRow;
    }

    row = rowAtY (y);

    if (row < 0 || row > channels)
        return Area::none;

    if (isZipped (row)) // round 190mk (DE-38): a zipped strip; on a bar, that bar's channel
    {
        if (const auto bar = zipBarAt (row, rackX); bar >= 0)
        {
            row = bar;
            return Area::zipped;
        }
        return Area::none;
    }

    if (laneBounds().contains (x, y))
        return x < gridLeft() ? Area::laneLevels : Area::lane;

    if (row == channels) // only the "+ add channel" box opens the menu (issue #116: as FL's + button)
        return racklayout::addChannelBox (nameLeft(), nameWidth, rowY (row), rowHeight).contains (x, y) ? Area::addRow : Area::none;

    if (advancedLoopShown() && x >= loopBoxBounds (row).getX() - 3) // the column covers the steps under it
        return loopBoxBounds (row).expanded (2, 3).contains (x, y) ? Area::loop : Area::none;

    if (x < muteWidth)
        return Area::mute;
    if (x < muteWidth + knobWidth)
        return Area::pan;
    if (x < muteWidth + 2 * knobWidth)
        return Area::volume;
    if (x < nameLeft())
        return Area::route;
    if (x < selectLeft())
        return Area::name;
    if (x < gridLeft())
        return Area::select;

    return Area::grid;
}

bool ChannelRackComponent::cellHasNote (int channel, int step) const
{
    const auto& song = engine.getSong();
    const auto& p = song.activePattern();
    const auto from = (std::int64_t) step * ticksPerStep;
    const auto to = from + ticksPerStep;

    for (const auto& n : p.notes) // (a muted note leaves its step looking off, as in FL: round 169f, FL session)
        if (n.channel == channel && n.start >= from && n.start < to && ! n.muted)
            return true;

    return false;
}

bool ChannelRackComponent::channelHitsNow (int row) const
{
    if (playheadStep < 0 || playheadStep >= numSteps())
        return false;
    const auto& song = engine.getSong();
    const auto& p = song.activePattern();
    const auto loopSteps = loopStepsOf (row);
    const auto step = loopSteps > 0 && playheadStep >= loopSteps ? playheadStep % loopSteps : playheadStep;
    return p.hasStep (row, step) || cellHasNote (row, step);
}

// Eight idle step keycaps (two groups of four, `a` then `b`) on the row's own background `bg`, from one opaque cached
// tile (round 190io): an opaque tile is copied, not blended, by the software renderer, so a row of idle steps costs about
// what the old flat tiles did although twice as many rows show steps now (the audio clip channels, FM-12). Drawn straight
// where the scale is not whole.
static void drawIdleStepStrip (juce::Graphics& g, juce::Rectangle<int> firstCell, int pitch, juce::Colour a, juce::Colour b, juce::Colour bg)
{
    const auto bounds = firstCell.withWidth (7 * pitch + firstCell.getWidth());
    if (! g.clipRegionIntersects (bounds))
        return;
    const auto paintInto = [&] (juce::Graphics& k, bool withBackground) {
        if (withBackground)
        {
            k.setColour (bg);
            k.fillRect (juce::Rectangle<int> (bounds.getWidth(), bounds.getHeight()));
        }
        for (int i = 0; i < 8; ++i)
            theme::drawKeycap (k, juce::Rectangle<int> (i * pitch, 0, firstCell.getWidth(), firstCell.getHeight()), i < 4 ? a : b, false);
    };
    const auto s = g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto scale = (int) std::round (s);
    if (! ui::paintCache() || scale < 1 || scale > 3 || std::abs (s - (float) scale) > 1.0e-4f || ! bg.isOpaque())
    {
        juce::Graphics::ScopedSaveState state (g);
        g.setOrigin (bounds.getPosition());
        paintInto (g, false);
        return;
    }
    static std::unordered_map<std::uint64_t, juce::Image> cache;
    static const bool registered = ui::registerImageCache ([] { cache.clear(); }); (void) registered;
    std::uint64_t key = 0xcbf29ce484222325ull;
    for (const std::uint64_t v : { (std::uint64_t) a.getARGB(), (std::uint64_t) b.getARGB(), (std::uint64_t) bg.getARGB(), (std::uint64_t) pitch,
                                   (std::uint64_t) firstCell.getWidth(), (std::uint64_t) firstCell.getHeight(), (std::uint64_t) scale,
                                   (std::uint64_t) theme::colour::bg0.getARGB(), (std::uint64_t) theme::colour::borderStrong.getARGB() })
        key = (key ^ v) * 0x100000001b3ull;
    auto it = cache.find (key);
    if (it == cache.end())
    {
        if (cache.size() > 200)
            cache.clear();
        juce::Image image (juce::Image::RGB, bounds.getWidth() * scale, bounds.getHeight() * scale, false,
                          #if JUCE_WINDOWS
                           juce::NativeImageType()
                          #else
                           juce::SoftwareImageType()
                          #endif
                          );
        {
            juce::Graphics ig (image);
            ig.addTransform (juce::AffineTransform::scale ((float) scale));
            paintInto (ig, true);
        }
        it = cache.emplace (key, std::move (image)).first;
    }
    g.setColour (juce::Colours::black); // (an image is drawn with the current colour's alpha as its opacity)
    if (scale == 1)
        g.drawImageAt (it->second, bounds.getX(), bounds.getY());
    else
        g.drawImage (it->second, bounds.toFloat(), juce::RectanglePlacement::stretchToFit);
}

void ChannelRackComponent::paintStepHit (juce::Graphics& g, juce::Rectangle<float> r)
{
    theme::fillRoundRect (g, r, keycapRadius, juce::Colours::white.withAlpha (0.55f));
    g.setColour (theme::colour::playhead);
    g.drawRoundedRectangle (r.reduced (0.75f), keycapRadius, 1.5f);
}

// FL's raised step keycap (round 190io, design190ia FM-11; FL: a light top edge, a dark bottom edge and the dark LED slot
// a quarter down): the body in the step's colour, a 1 px highlight on top, a 1 px shade at the bottom and the 1 px slot
// line, which turns light on a lit step. Plain rectangles, so a playhead frame costs what the flat tiles did.
void ChannelRackComponent::paintKeycap (juce::Graphics& g, juce::Rectangle<int> r, juce::Colour fill, bool lit)
{
    theme::drawKeycap (g, r, fill, lit);
}

void ChannelRackComponent::paint (juce::Graphics& g)
{
    const uitrace::Scope trace ("ChannelRackComponent paint", true, g.getClipBounds());
    using namespace theme::colour;
    g.fillAll (bg2);

    const auto& song = engine.getSong();

    if (song.patterns.empty())
        return;

    const auto& pattern = song.activePattern();
    const auto steps = shownSteps(), realSteps = numSteps();
    const auto clip = g.getClipBounds();
    const auto primary = engine.getSelectedChannel();
    const auto& selection = engine.getSelectedChannels();
    const auto palette = theme::currentPalette();
    const auto column = playheadColumn.load (std::memory_order_relaxed) && playheadStep >= 0 && playheadStep < steps;
    g.setFont (theme::font (theme::type::body));
    // round 190gr: which (channel, step) cells hold a Piano Roll note, from one pass over the pattern's notes (cellHasNote
    // walks every note of the pattern for each cell: rows x steps x notes per frame)
    const auto channelCount = (int) song.channels.size();
    const auto noteCells = ui::paintCache();
    if (noteCells)
    {
        noteCellBits.assign ((std::size_t) channelCount * (std::size_t) juce::jmax (0, steps), 0);
        for (const auto& n : pattern.notes)
            if (! n.muted && n.channel >= 0 && n.channel < channelCount && n.start >= 0)
                if (const auto step = n.start / ticksPerStep; step < steps)
                    noteCellBits[(std::size_t) n.channel * (std::size_t) steps + (std::size_t) step] = 1;
    }
    const auto hasNote = [&] (int channel, int step) {
        if (! noteCells)
            return cellHasNote (channel, step);
        return step >= 0 && step < steps && noteCellBits[(std::size_t) channel * (std::size_t) steps + (std::size_t) step] != 0;
    };

    for (int row = 0; row < (int) song.channels.size(); ++row)
    {
        if (isZipped (row)) // (round 190mk: drawn as a bar in its strip, paintZipStrips)
            continue;
        const auto y = rowY (row);

        if (! isShown (row) || y > clip.getBottom() || y + rowHeight < clip.getY())
            continue;

        const auto& channel = song.channels[(std::size_t) row];
        const auto selected = ! autoOnly && std::binary_search (selection.begin(), selection.end(), row); // (190ow: autoOnly)
        const auto litOn = stepsChannelColour.load (std::memory_order_relaxed) && channel.colour != 0
                               ? juce::Colour (themes::litStepColour (channel.colour, palette))
                               : theme::colour::stepOn;

        if (selected) // selected rows are subtly lifted
        {
            g.setColour (bg3);
            g.fillRect (0, y + 1, getWidth(), rowHeight - 2);
        }

        if (column) // LYNX: the playing column, soft, under the step buttons (their colours and groups stay readable)
        {
            g.setColour (playhead.withAlpha (0.2f));
            g.fillRect (gridLeft() + playheadStep * stepWidth - 1, y, stepWidth, rowHeight);
        }

        // (round 190io, FM-12: an audio clip channel shows its steps as every channel does; FL's manual: "If desired,
        // Audio Clips can be triggered from the Step Sequencer or the Piano roll")

        const auto loopSteps = loopStepsOf (row); // the row loops: the repeats after its loop point are ghost steps
        const auto stepOn = litOn; // (the channel's lit-step colour, or the theme's)

        if (showsNotePreview (pattern, row, pianoRollView)) // piano-roll notes: a miniature instead of the step buttons
        {
            paintNotePreview (g, row, pattern, channel.colour);
            const auto span = previewSpan();
            const auto stepX = [&] (int step) { return gridLeft() + span.x + (int) ((std::int64_t) span.width * step / realSteps); };
            if (loopSteps > 0)
            {
                g.setColour (accent.withAlpha (0.85f));
                g.fillRect (stepX (loopSteps) - 2, y + 3, 2, rowHeight - 6);
            }
            if (span.fitted && playheadStep >= 0 && playheadStep < realSteps) // (the playing column is elsewhere: a line here)
            {
                g.setColour (playhead.withAlpha (0.8f));
                g.fillRect (stepX (playheadStep), y + 4, juce::jmax (2, span.width / realSteps), rowHeight - 8);
            }
            continue;
        }

        // round 190io: the idle keycaps (and the faint steps past the pattern's end) come eight at a time from one tile;
        // the steps that differ (lit, hovered, playing, loop repeats) are drawn over them one by one below
        const auto idleLimit = loopSteps > 0 ? juce::jmin (loopSteps, realSteps) : realSteps;
        const auto stripKind = [&] (int step) { // 1: a keycap strip covers it, 2: a ghost strip, 0: drawn alone
            const auto start = step / 8 * 8;
            return start + 8 <= idleLimit ? 1 : start >= realSteps && start + 8 <= steps ? 2 : 0;
        };
        // (the strip under the playing column is drawn cell by cell: the column's soft tint shows between its steps)
        const auto columnStrip = column ? playheadStep / 8 * 8 : -1;
        for (int start = 0; start + 8 <= steps; start += 8)
            if (const auto kind = stripKind (start); kind != 0 && start != columnStrip)
            {
                const auto first = stepBounds (row, start);
                if (first.getX() + 8 * stepWidth < clip.getX() || first.getX() > clip.getRight())
                    continue;
                if (kind == 1)
                    drawIdleStepStrip (g, first, stepWidth, theme::stepOff (start), theme::stepOff (start + 4), selected ? bg3 : bg2);
                else
                    theme::drawStepStripOver (g, first, stepWidth, theme::stepOff (start), theme::stepOff (start + 4), true, selected ? bg3 : bg2);
            }

        for (int step = 0; step < steps; ++step)
        {
            const auto r = stepBounds (row, step);

            if (r.getRight() < clip.getX() || r.getX() > clip.getRight())
                continue;

            auto onGrid = pattern.hasStep (row, step);
            auto any = onGrid || hasNote (row, step);

            const auto hoverStepHere = hoverArea == Area::grid && hoverRow == row && hoverStep == step && paintMode == PaintMode::none;
            if (! any && ! hoverStepHere && stripKind (step) != 0 && step / 8 * 8 != columnStrip) // (drawn by its strip)
                continue;
            if (loopSteps > 0 && step >= loopSteps && step < realSteps)
            {
                // FL's loop ghosts: what the loop repeats here, faint; a step of its own past the loop point doesn't play
                const auto source = step % loopSteps;
                const auto repeated = pattern.hasStep (row, source) || hasNote (row, source);
                auto fill = repeated ? stepOn : theme::stepOff (step);
                theme::fillRoundRect (g, r.toFloat(), keycapRadius, fill.withMultipliedAlpha (hoverStepHere ? 0.75f : repeated ? 0.45f : 0.3f));
                if (repeated && step == playheadStep) // the repeat plays: lit as a hit (below)
                    paintStepHit (g, r.toFloat());
                if (any)
                {
                    g.setColour (textDisabled);
                    g.drawRoundedRectangle (r.toFloat().reduced (1.5f), keycapRadius, 1.0f);
                }
                if (hoverStepHere)
                {
                    g.setColour (textSecondary.withAlpha (0.7f));
                    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), keycapRadius, 1.0f);
                }
                continue;
            }
            const auto ghost = step >= realSteps && ! any; // after the pattern's end: faint until clicked
            auto fill = any ? (onGrid ? stepOn : stepNote) : theme::stepOff (step);
            if (ghost) // a faint outlined cell, filled while hovered
            {
                theme::fillRoundRect (g, r.toFloat(), keycapRadius, fill.withMultipliedAlpha (hoverStepHere ? 0.9f : 0.35f));
                g.setColour (fill.brighter (0.25f).withMultipliedAlpha (0.55f));
                g.drawRoundedRectangle (r.toFloat().reduced (0.5f), keycapRadius, 1.0f);
            }
            else
                paintKeycap (g, r, hoverStepHere ? fill.brighter (any ? 0.15f : 0.35f) : fill, any);

            // FL lights the step as the playhead hits it (its LED turns red, round 170a [SHOT]): a step that plays now glows
            if (any && step == playheadStep && step < realSteps)
                paintStepHit (g, r.toFloat());

            if (hoverStepHere)
            {
                g.setColour (textSecondary.withAlpha (0.7f));
                g.drawRoundedRectangle (r.toFloat().reduced (0.5f), keycapRadius, 1.0f);
            }
        }

        if (loopSteps > 0) // the loop point
        {
            g.setColour (accent.withAlpha (0.85f));
            g.fillRect (gridLeft() + loopSteps * stepWidth - 2, y + 3, 2, rowHeight - 6);
        }
    }

    if (! autoRows.empty()) // round 190oo (PAT-7): the automation clip rows' steps
        paintAutomationSteps (g, clip);

    paintLane (g);
    const auto addY = rowY ((int) song.channels.size());
    g.setColour (theme::colour::textSecondary); // (round 190oo PAT-14: an informative hint, 7:1; was textDisabled 2.9:1)
    g.setFont (theme::font (theme::type::small));
    textcache::drawText (g, tr ("or drop an audio file here"), gridLeft() + 4, addY + positionStripHeight, 400, rowHeight - positionStripHeight,
                         juce::Justification::centredLeft);

    const auto stepsPerBar = (int) std::max<std::int64_t> (1, song.barTicks() / ticksPerStep);
    // FL's "Playing step" strip (PosLEDPanel, REA round 170a §2): a slim LED row under the steps, beat groups
    // alternating, the playing step lit — in Pattern mode and whenever the shown pattern plays in Song mode
    for (int step = 0; step < realSteps; ++step)
    {
        const auto cell = juce::Rectangle<float> ((float) (gridLeft() + step * stepWidth) + 1.0f, (float) addY + 2.0f, (float) stepWidth - 4.0f,
                                                  (float) positionStripHeight - 3.0f);
        if (cell.getRight() < (float) clip.getX() || cell.getX() > (float) clip.getRight())
            continue;
        theme::fillRoundRect (g, cell, 1.5f, step == playheadStep ? playhead : theme::stepOff (step).brighter (0.12f));
        // LYNX: each bar's first step marked (finding the bars on 32 / 64-step patterns)
        if (playheadColumn.load (std::memory_order_relaxed) && step % stepsPerBar == 0 && step != playheadStep)
        {
            g.setColour (accent.withAlpha (0.7f));
            g.fillRect (cell.withHeight (2.0f));
        }
    }

    // the controls column stays put while the steps scroll sideways (FL's rack keeps the channel buttons in
    // view): the viewport moves the whole rack, so the column is painted again at the visible left edge
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (stickyX, 0, gridLeft(), getHeight());
        g.setOrigin (stickyX, 0);
        g.setColour (bg2);
        g.fillRect (0, 0, gridLeft(), getHeight());
        const auto column = g.getClipBounds();

        for (int row = 0; row < (int) song.channels.size(); ++row)
        {
            if (isZipped (row)) // (round 190mk)
                continue;
            const auto y = rowY (row);

            if (! isShown (row) || y > column.getBottom() || y + rowHeight < column.getY())
                continue;

            const auto& channel = song.channels[(std::size_t) row];
            const auto selected = ! autoOnly && std::binary_search (selection.begin(), selection.end(), row); // (round 190ow)
            const auto nameArea = juce::Rectangle<float> ((float) nameLeft(), (float) y + 3, (float) nameWidth, (float) rowHeight - 6);

            if (selected) // selected rows are subtly lifted
            {
                g.setColour (bg3);
                g.fillRect (0, y + 1, getWidth(), rowHeight - 2);
            }

            const auto hovered = [this, row] (Area a) { return hoverRow == row && hoverArea == a; };
            // round 190ge: a playhead step repaints the selector column only (the selectors flash with the hits): the mute
            // LED, knobs, route box and name, all left of it, are drawn only when the repainted area reaches them
            if (column.getX() < selectLeft() || ! ui::paintSavings())
            {
                const auto led = juce::Rectangle<float> (9.0f, (float) y + rowHeight / 2.0f - 4.5f, 9.0f, 9.0f);
                g.setColour (theme::muteLed (channel.muted));
                g.fillEllipse (led);

                if (hovered (Area::mute)) // hover ring
                {
                    g.setColour (textSecondary.withAlpha (0.8f));
                    g.drawEllipse (led.expanded (2.5f), 1.0f);
                }

                const auto touched = knobDrag != Area::none && knobRow == row;
                // (while the song plays an automated knob shows its automation's value, issue #12)
                const auto& automated = engine.getAutomationDisplay();
                theme::drawKnob (g, controlBounds (Area::pan, row).toFloat().reduced (1.0f, 2.0f),
                                 automated.shown ({ AutomationTargetKind::channelPan, row }, (channel.pan + 1.0f) / 2.0f), true,
                                 touched && knobDrag == Area::pan ? accent : info, hovered (Area::pan) || (touched && knobDrag == Area::pan));
                theme::drawKnob (g, controlBounds (Area::volume, row).toFloat().reduced (1.0f, 2.0f),
                                 automated.shown ({ AutomationTargetKind::channelVolume, row }, channel.volume), false,
                                 touched && knobDrag == Area::volume ? accent : knobArc, hovered (Area::volume) || (touched && knobDrag == Area::volume));

                // small box with the mixer insert ("-" = straight to Master)
                const auto routeArea = controlBounds (Area::route, row).toFloat();
                g.setColour (theme::drawDigitDisplay (g, routeArea, hovered (Area::route) || (touched && knobDrag == Area::route),
                                                      channel.mixerTrack <= 0));
                g.setFont (theme::font (theme::type::caption, true));
                textcache::drawText (g, channel.mixerTrack > 0 ? juce::String (channel.mixerTrack) : juce::String ("-"), routeArea.toNearestInt(),
                            juce::Justification::centred);
                g.setFont (theme::font (theme::type::body));

                // name button: filled with the channel colour as FL's (round 190io, design190ia FM-10: FL's Synth Bass
                // #11479C, Grv Kick #892019), its brightness held where white text reads (0.42-0.58), a slight top-down
                // gradient (+6 % / -6 %); FL's grey #485156 for a channel without a colour; muted = greyed and darker
                {
                    auto fill = theme::colourFill (channel.colour != 0 ? juce::Colour (channel.colour) : theme::colour::defaultTrack, 0.42f, 0.58f);
                    if (channel.muted)
                        fill = fill.withMultipliedSaturation (0.45f).withMultipliedBrightness (0.7f);
                    if (hovered (Area::name))
                        fill = fill.brighter (0.12f);
                    theme::fillPlate (g, nameArea, theme::radius::sm, fill.withMultipliedBrightness (1.06f), fill.withMultipliedBrightness (0.94f));
                }

                if (row == primary) // the focused channel: a light outline (the selector beside it shows the selection)
                {
                    g.setColour (textPrimary);
                    g.drawRoundedRectangle (nameArea.reduced (0.75f), theme::radius::sm, 1.5f);
                }

                const auto locked = ! channel.receiveFrom.empty();
                if (locked) // FL's lock icon: the channel receives the notes of one MIDI input ("Receive notes from")
                {
                    const auto lockBox = juce::Rectangle<float> (nameArea.getRight() - 15.0f, nameArea.getCentreY() - 6.0f, 9.0f, 11.0f);
                    g.setColour (warning);
                    juce::Path shackle;
                    shackle.addCentredArc (lockBox.getCentreX(), lockBox.getY() + 4.0f, 2.8f, 3.2f, 0.0f, -juce::MathConstants<float>::halfPi,
                                           juce::MathConstants<float>::halfPi, true);
                    g.strokePath (shackle, juce::PathStrokeType (1.4f));
                    g.fillRoundedRectangle (lockBox.withTrimmedTop (4.5f), 1.5f);
                }
                // Online Sessions (round 168a): the plugin is not installed here, its stand-in render plays (an amber tag)
                auto badgeWidth = 0.0f;
                if (engine.playsStandIn (row))
                {
                    badgeWidth = 16.0f;
                    juce::Graphics::ScopedSaveState keepFont (g);
                    const auto tag = juce::Rectangle<float> (nameArea.getRight() - (locked ? 34.0f : 18.0f), nameArea.getCentreY() - 6.5f, 14.0f, 13.0f);
                    theme::fillRoundRect (g, tag, 3.0f, warning.withAlpha (0.85f));
                    g.setColour (bg1);
                    g.setFont (theme::font (10.0f, true));
                    g.drawText ("R", tag, juce::Justification::centred, false);
                }
                // round 190ap: opened without third-party plugins, this channel's plugin is still off (a grey power tag);
                // round 190op (P4): a deactivated channel has the same tag and a dimmed plate
                else if (engine.channelPluginOff (row) || engine.channelDeactivated (row))
                {
                    if (engine.channelDeactivated (row))
                        theme::fillRoundRect (g, nameArea, theme::radius::md, bg1.withAlpha (0.45f));
                    badgeWidth = 16.0f;
                    juce::Graphics::ScopedSaveState keepFont (g);
                    ui::drawPluginOffTag (g, juce::Rectangle<float> (nameArea.getRight() - (locked ? 34.0f : 18.0f), nameArea.getCentreY() - 6.5f, 14.0f, 13.0f));
                }
                // Freeze (round 190ay): a frozen channel plays its rendered audio, its instrument is unloaded — a snowflake
                // tag (amber when the audio is out of date or missing) and a dimmed name
                const auto frozen = channel.freeze.on();
                if (frozen)
                {
                    const auto attention = engine.frozenAudioMissing (row) || engine.freezeOutOfDate (row);
                    const auto tag = juce::Rectangle<float> (nameArea.getRight() - (locked ? 34.0f : 18.0f) - badgeWidth, nameArea.getCentreY() - 6.5f, 14.0f, 13.0f);
                    badgeWidth += 16.0f;
                    theme::fillRoundRect (g, nameArea, theme::radius::md, bg1.withAlpha (0.35f)); // the plate, dimmed
                    theme::drawFrozenTag (g, tag, (attention ? warning : info).withAlpha (0.85f), bg1);
                }
                // the channel's icon (round 168h) at the button's right edge, where FL draws it (round 190io, FM-10: FL's
                // audio clip glyph sits 6 px from the right), left of the lock / tags
                const auto rightUsed = (locked ? 18.0f : 0.0f) + badgeWidth;
                auto iconWidth = 0.0f;
                if (const auto icon = trackicons::iconFor (channel.icon); icon != trackicons::Icon::none)
                {
                    iconWidth = 18.0f;
                    ui::drawTrackIcon (g, icon, juce::Rectangle<float> (nameArea.getRight() - 6.0f - rightUsed - 14.0f, nameArea.getCentreY() - 7.0f, 14.0f, 14.0f),
                                       onAccent.withAlpha (channel.muted ? 0.45f : 0.85f));
                }
                // white on the colour (FL), the name's middle dropped when it is too long ("Titey..m #2", FM-14), centred in
                // the room left of the icon and tags as FL centres it (round 190jp: "Synth Bass" on the button's centre line,
                // "Titey Tom" between the left edge and the icon)
                g.setColour (onAccent.withAlpha (channel.muted ? 0.5f : frozen ? 0.7f : 0.92f));
                g.setFont (theme::font (theme::type::body + 1.0f));
                // FL's Channel rack menu › Options › Channel name alignment › Left (round 190kd): the name from the left edge,
                // as LYNX drew it before 190jp
                const auto left = nameAlignLeft;
                const auto rightTrim = rightUsed + iconWidth > 0.0f ? rightUsed + iconWidth + 4.0f : left ? 5.0f : 6.0f;
                const auto textArea = nameArea.withTrimmedLeft (left ? 7.0f : 6.0f).withTrimmedRight (rightTrim);
                textcache::drawText (g, theme::fitMiddle (juce::String::fromUTF8 (channel.name.c_str()), g.getCurrentFont(), textArea.getWidth()), textArea,
                                     left ? juce::Justification::centredLeft : juce::Justification::centred, false);
                g.setFont (theme::font (theme::type::body));
            }

            // selector: FL's tall slot (round 190io, design190ia FM-13: 12 x 27 with a 2 px outline when selected), here
            // 10 x 24 in the inset colour with a hairline, outlined 2 px in the accent while its channel is selected
            const auto slot = juce::Rectangle<float> ((float) selectLeft() + 1.0f, (float) y + 3.0f, (float) selectWidth - 2.0f, (float) rowHeight - 6.0f);
            // (two filled plates, the rim and the well, from the rounded-rectangle tiles: no stroked path per row)
            theme::fillRoundRect (g, slot, 2.0f, selected ? (hovered (Area::select) ? accentHover : accent) : hovered (Area::select) ? textDisabled : border);
            theme::fillRoundRect (g, slot.reduced (selected ? 2.0f : 1.0f), selected ? 1.0f : 1.5f, bg1);
            const auto pill = slot.reduced (selected ? 3.0f : 2.0f);
            // FL flashes the channel's selector as its note plays (round 170a [SHOT]): the "sound lighting up when it hits"
            // round 190ih: and with every note the channel plays (typing keyboard, MIDI, Piano roll, off-grid notes), bright
            // at note-on, dimmer while held, fading out after the release (FL capture, details190if); orange for a
            // channel locked to a controller ("Receive notes from"; FL's manual: "Orange for locked Channels")
            const auto lit = row < (int) activityLights.size() ? activityLights[(std::size_t) row].level : 0.0f;
            const auto hit = ! channel.muted && channelHitsNow (row);
            if (hit || (! channel.muted && lit > 0.0f))
            {
                const auto top = selected ? 0.75f : 0.9f;
                const auto colour = ! channel.receiveFrom.empty() ? warning : juce::Colours::white;
                theme::fillRoundRect (g, pill, 1.5f, colour.withAlpha (juce::jmax (hit ? top : 0.0f, lit * top)));
            }
        }

        if (! autoRows.empty()) // round 190oo (PAT-7): the automation clip rows' controls and buttons
            paintAutomationButtons (g, column);

        paintLane (g);
        {
            const auto box = racklayout::addChannelBox (nameLeft(), nameWidth, addY, rowHeight);
            const auto addArea = juce::Rectangle<float> ((float) box.x, (float) box.y, (float) box.width, (float) box.height);
            if (g.clipRegionIntersects (addArea.getSmallestIntegerContainer().expanded (1)) || ! ui::paintSavings()) // (round 190ge: a playhead frame repaints the steps only)
            {
                const float dash[] = { 3.0f, 3.0f };
                juce::Path outline, dashed;
                outline.addRoundedRectangle (addArea.reduced (0.5f), theme::radius::md);
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dash, 2);
                const auto addHovered = hoverArea == Area::addRow;
                g.setColour (addHovered ? textSecondary : borderStrong);
                g.fillPath (dashed);
                g.setColour (addHovered ? textPrimary : textSecondary);
                g.setFont (theme::font (theme::type::small));
                textcache::drawText (g, tr ("+ add channel"), addArea.toNearestInt(), juce::Justification::centred);
            }
        }

        if (stickyX > 0) // steps pass under the column: a hairline marks its edge
        {
            g.setColour (border);
            g.fillRect (gridLeft() - 1, 0, 1, getHeight());
        }
    }

    if (! zipLayout.top.empty())
        paintZipStrips (g);

    if (advancedLoopShown())
        paintLoopColumn (g);
}

// --- FL's automation clip channels (round 190oo, design review 2 PAT-7; core/RackAutomation.h) ------------------------
// FL lists every automation clip as a channel of the rack (Thief: "Volume Duck", "GROSS..w Bass", "CRASH REVERB" between
// YELP and "Riser..Metal", design190ia shots/ref/fl-channelrack.png). Here they are rows of their own between the channel
// rows: the channel rows keep their indices, so the selection, the filter groups, the Piano roll's channel, the mixer
// routing and the steps never see them.

void ChannelRackComponent::refreshAutomationRows()
{
    autoRows.clear();
    autoBefore.clear();
    const auto& song = engine.getSong();
    if (song.automations.empty() || ! rackauto::shownUnder (song, filterGroup))
    {
        autoSelected.clear(), autoOnly = false; // (round 190ow)
        return;
    }
    autoRows = rackauto::rows (song);
    if (autoRows.empty())
    {
        autoSelected.clear(), autoOnly = false;
        return;
    }
    // a row inside a zipped strip (round 190mk) goes right below the strip: the strip stays whole
    if (! zipLayout.first.empty())
    {
        const auto count = (int) song.channels.size();
        for (auto& r : autoRows)
            while (r.gap < count && r.gap < (int) zipLayout.first.size() && zipLayout.first[(std::size_t) r.gap] != r.gap)
                ++r.gap;
        std::stable_sort (autoRows.begin(), autoRows.end(), [] (const rackauto::Row& a, const rackauto::Row& b) { return a.gap < b.gap; });
    }
    autoBefore = rackauto::rowsBefore (song, autoRows);
    // (round 190ow: the selection keeps the rows still shown)
    autoSelected.erase (std::remove_if (autoSelected.begin(), autoSelected.end(), [this] (int a) {
                            return std::none_of (autoRows.begin(), autoRows.end(), [a] (const rackauto::Row& r) { return r.automation == a; });
                        }),
                        autoSelected.end());
    if (autoSelected.empty())
        autoOnly = false;
}

int ChannelRackComponent::automationRowTop (int i) const
{
    // the channel rows above its gap (the graph lane and the zipped strips included) and the automation rows before it
    return baseRowY (autoRows[(std::size_t) i].gap) + i * rowHeight;
}

int ChannelRackComponent::automationRowAt (int y) const
{
    for (int i = 0; i < (int) autoRows.size(); ++i)
    {
        const auto top = automationRowTop (i);
        if (y < top)
            return -1; // (top to bottom: the others are lower)
        if (y < top + rowHeight)
            return i;
    }
    return -1;
}

juce::Rectangle<int> ChannelRackComponent::automationRowBounds (int automation) const
{
    for (int i = 0; i < (int) autoRows.size(); ++i)
        if (autoRows[(std::size_t) i].automation == automation)
            return { 0, automationRowTop (i), getWidth(), rowHeight };
    return {};
}

// FL draws an automation clip channel's steps as any channel's (idle: LYNX does not start a clip from steps, the triggered
// playback of details DE-123); the piano roll overview leaves the row empty (a clip has no notes)
void ChannelRackComponent::paintAutomationSteps (juce::Graphics& g, juce::Rectangle<int> clip)
{
    using namespace theme::colour;
    const auto steps = shownSteps(), realSteps = numSteps();
    const auto& song = engine.getSong();
    const auto& pattern = song.activePattern();
    // round 190pb: whole strips only (no step drawn one by one: a ghost step's outline is a stroked path), a whole display
    // scale, the software renderer's exact copies: the row's strips are drawn once into a picture, as they would be into
    // the frame (each strip a tile blitted on the background), and the picture is copied into every automation row that
    // shows only idle strips (rollup 0.1.31 step 7: since round 190ow a row can have steps of its own, a selection lift, a
    // hovered step and playhead hits: such a row is drawn strip by strip below)
    const juce::Image* shared = nullptr;
    const auto s = g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto scale = (int) std::round (s);
    const auto w = steps * stepWidth, h = rowHeight - 10;
    if (! pianoRollView && ui::paintCache() && realSteps % 8 == 0 && steps % 8 == 0 && steps > 0 && scale >= 1 && scale <= 3
        && std::abs (s - (float) scale) <= 1.0e-4f && bg2.isOpaque() && steps * stepWidth * scale <= 16384
        && dynamic_cast<juce::LowLevelGraphicsSoftwareRenderer*> (&g.getInternalContext()) != nullptr)
    {
        auto key = textcache::detail::mixHash (0x243f6a8885a308d3ull, (std::uint64_t) steps);
        for (const std::uint64_t v : { (std::uint64_t) realSteps, (std::uint64_t) scale, (std::uint64_t) theme::paletteStampNow(), (std::uint64_t) bg2.getARGB() })
            key = textcache::detail::mixHash (key, v);
        for (int start = 0; start < steps; start += 4)
            key = textcache::detail::mixHash (key, (std::uint64_t) theme::stepOff (start).getARGB());
        auto& picture = autoStepsPicture;
        if (picture.key != key || ! picture.image.isValid())
        {
            picture.key = key;
            picture.image = juce::Image (juce::Image::RGB, w * scale, h * scale, false, juce::SoftwareImageType());
            juce::Graphics ig (picture.image);
            ig.addTransform (juce::AffineTransform::scale ((float) scale));
            ig.setColour (bg2); // (the rack's background, paint()'s fillAll, under and between the strips)
            ig.fillRect (0, 0, w, h);
            for (int start = 0; start < steps; start += 8)
            {
                const auto first = juce::Rectangle<int> (start * stepWidth + 1, 0, stepWidth - 3, h); // (stepBounds, at the picture's origin)
                if (start + 8 <= realSteps)
                    drawIdleStepStrip (ig, first, stepWidth, theme::stepOff (start), theme::stepOff (start + 4), bg2);
                else
                    theme::drawStepStripOver (ig, first, stepWidth, theme::stepOff (start), theme::stepOff (start + 4), true, bg2);
            }
        }
        shared = &picture.image;
    }
    bool lastFromPicture = false;
    for (int i = 0; i < (int) autoRows.size(); ++i)
    {
        const auto y = automationRowTop (i);
        if (y > clip.getBottom() || y + rowHeight < clip.getY())
            continue;
        const auto a = autoRows[(std::size_t) i].automation;
        const auto selected = automationSelected (a);
        if (selected) // (round 190ow: a selected row is lifted as a channel's)
        {
            g.setColour (bg3);
            g.fillRect (0, y + 1, getWidth(), rowHeight - 2);
        }
        if (pianoRollView) // the piano roll overview: the notes as bars (FL's preview of the row's notes)
        {
            const auto span = (double) juce::jmax (1, steps) * ticksPerStep;
            g.setColour (stepNote.withAlpha (0.85f));
            for (const auto& n : pattern.autoNotes)
                if (n.channel == a && ! n.muted && (double) n.start < span)
                {
                    const auto x0 = gridLeft() + (float) ((double) n.start / span * steps * stepWidth);
                    const auto len = n.length > 0 ? (double) n.length : (double) ticksPerStep;
                    const auto nw = juce::jmax (3.0f, (float) (len / span * steps * stepWidth) - 1.0f);
                    g.fillRect (juce::Rectangle<float> (x0, (float) y + rowHeight / 2.0f - 2.0f, nw, 4.0f));
                }
            continue;
        }
        const auto hoveredRow = hoverArea == Area::autoStep && hoverRow == a;
        if (shared != nullptr && ! selected && ! hoveredRow
            && std::none_of (pattern.autoNotes.begin(), pattern.autoNotes.end(), [a] (const auto& n) { return n.channel == a; }))
        {
            g.setColour (juce::Colours::black); // (an image is drawn with the current colour's alpha as its opacity)
            if (scale == 1)
                g.drawImageAt (*shared, gridLeft(), y + 5);
            else
                g.drawImage (*shared, juce::Rectangle<float> ((float) gridLeft(), (float) y + 5.0f, (float) w, (float) h), juce::RectanglePlacement::stretchToFit);
            lastFromPicture = true;
            continue;
        }
        lastFromPicture = false;
        for (int start = 0; start < steps; start += 8)
        {
            const auto first = juce::Rectangle<int> (gridLeft() + start * stepWidth + 1, y + 5, stepWidth - 3, rowHeight - 10); // (stepBounds)
            if (first.getX() + 8 * stepWidth < clip.getX() || first.getX() > clip.getRight())
                continue;
            if (start + 8 <= realSteps)
                drawIdleStepStrip (g, first, stepWidth, theme::stepOff (start), theme::stepOff (start + 4), bg2);
            else if (start >= realSteps && start + 8 <= steps)
                theme::drawStepStripOver (g, first, stepWidth, theme::stepOff (start), theme::stepOff (start + 4), true, bg2);
            else
                for (int step = start; step < juce::jmin (start + 8, steps); ++step)
                {
                    const auto r = first.withX (gridLeft() + step * stepWidth + 1);
                    const auto fill = theme::stepOff (step);
                    if (step < realSteps)
                    {
                        paintKeycap (g, r, fill, false);
                        continue;
                    }
                    theme::fillRoundRect (g, r.toFloat(), keycapRadius, fill.withMultipliedAlpha (0.35f)); // (a ghost step)
                    g.setColour (fill.brighter (0.25f).withMultipliedAlpha (0.55f));
                    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), keycapRadius, 1.0f);
                }
        }
        // round 190ow (DE-123): the steps that start the clip, lit as a channel's (a note longer than a step in the
        // Piano roll's colour), the one under the mouse outlined, a step the playhead plays lit as a hit
        const auto muted = a >= 0 && a < (int) song.automations.size() && song.automations[(std::size_t) a].muted;
        for (const auto& n : pattern.autoNotes)
        {
            if (n.channel != a || n.muted || n.start < 0)
                continue;
            const auto step = (int) (n.start / ticksPerStep);
            if (step >= steps)
                continue;
            const auto r = juce::Rectangle<int> (gridLeft() + step * stepWidth + 1, y + 5, stepWidth - 3, rowHeight - 10);
            if (r.getRight() < clip.getX() || r.getX() > clip.getRight())
                continue;
            const auto onGrid = n.start % ticksPerStep == 0 && (n.length == 0 || n.length == ticksPerStep);
            const auto fill = onGrid ? stepOn : stepNote; // (a muted row keeps its steps lit, as FL draws it: fl-s1-rack.png)
            if (step >= realSteps)
                theme::fillRoundRect (g, r.toFloat(), keycapRadius, fill.withMultipliedAlpha (0.9f));
            else
                paintKeycap (g, r, hoverArea == Area::autoStep && hoverRow == a && hoverStep == step ? fill.brighter (0.15f) : fill, true);
            if (! muted && step == playheadStep && step < realSteps)
                paintStepHit (g, r.toFloat());
        }
        if (hoveredRow && hoverStep >= 0 && hoverStep < steps && paintMode == PaintMode::none)
        {
            const auto r = juce::Rectangle<int> (gridLeft() + hoverStep * stepWidth + 1, y + 5, stepWidth - 3, rowHeight - 10);
            g.setColour (textSecondary.withAlpha (0.7f));
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), keycapRadius, 1.0f);
        }
    }
    if (lastFromPicture && realSteps < steps) // (the colour the last ghost strip leaves set, as before)
        g.setColour (theme::stepOff (steps - 8));
}

// FL's automation clip rows' controls column (round 190pb: each row from its cached picture where the scale is whole)
void ChannelRackComponent::paintAutomationButtons (juce::Graphics& g, juce::Rectangle<int> column)
{
    using namespace theme::colour;
    const auto& song = engine.getSong();
    const auto s = g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto scale = (int) std::round (s);
    const auto pictures = ui::paintCache() && scale >= 1 && scale <= 3 && std::abs (s - (float) scale) <= 1.0e-4f && bg2.isOpaque();
    // (the left part of a row is skipped by a playhead frame that only repaints the steps, as the channel rows' is)
    const auto leftPart = column.getX() < selectLeft() || ! ui::paintSavings();
    if (autoRowPictures.size() != autoRows.size())
        autoRowPictures.assign (autoRows.size(), {});
    std::uint64_t common = 0;
    if (pictures)
    {
        // what every row's picture shares: the layout, the palette and the name's font
        const auto font = theme::font (theme::type::body + 1.0f);
        common = 0x9e3779b97f4a7c15ull;
        for (const std::uint64_t v : { (std::uint64_t) scale, (std::uint64_t) theme::paletteStampNow(), (std::uint64_t) nameLeft(), (std::uint64_t) nameWidth,
                                       (std::uint64_t) selectLeft(), (std::uint64_t) gridLeft(), (std::uint64_t) (showMixerSelectors ? 1 : 0),
                                       (std::uint64_t) (nameAlignLeft ? 1 : 0), (std::uint64_t) bg2.getARGB(), (std::uint64_t) bg1.getARGB(),
                                       (std::uint64_t) border.getARGB(), (std::uint64_t) textSecondary.getARGB(), (std::uint64_t) onAccent.getARGB(),
                                       (std::uint64_t) knobArc.getARGB(),
                                       (std::uint64_t) font.getTypefaceName().hashCode64(), (std::uint64_t) font.getTypefaceStyle().hashCode64(),
                                       (std::uint64_t) juce::roundToInt (font.getHeight() * 1000.0f), (std::uint64_t) juce::roundToInt (font.getHorizontalScale() * 1000.0f),
                                       (std::uint64_t) juce::roundToInt (font.getExtraKerningFactor() * 1000.0f), (std::uint64_t) (ui::paintSavings() ? 1 : 0) })
            common = textcache::detail::mixHash (common, v);
    }
    bool drewLeft = false;
    for (int i = 0; i < (int) autoRows.size(); ++i)
    {
        const auto a = autoRows[(std::size_t) i].automation;
        const auto y = automationRowTop (i);
        if (a < 0 || a >= (int) song.automations.size() || y > column.getBottom() || y + rowHeight < column.getY())
            continue;
        if (! pictures)
        {
            if (leftPart)
            {
                paintAutomationButton (g, a, y, true, true);
                drewLeft = true;
            }
            paintAutomationSlot (g, a, y);
            continue;
        }
        // rollup 0.1.31 step 7: the picture also shows round 190ow's live knobs (the clip's range, hover / drag); the mute
        // light, the curve glyph, the name and the selector (selected / hovered / lit by the playhead) are drawn over it
        const auto& automation = song.automations[(std::size_t) a];
        const auto knobState = [&] (Area which) { return (hoverArea == which && hoverRow == a) || (knobDrag == which && knobRow == a); };
        const auto bits = [] (float v) { std::uint32_t u = 0; std::memcpy (&u, &v, sizeof u); return (std::uint64_t) u; };
        auto key = textcache::detail::mixHash (common, (std::uint64_t) automation.colour);
        for (const std::uint64_t v : { (std::uint64_t) (hoverArea == Area::autoName && hoverRow == a ? 1 : 0),
                                       bits (automation.rangeMin), bits (automation.rangeMax),
                                       (std::uint64_t) (knobState (Area::autoMin) ? 1 : 0), (std::uint64_t) (knobState (Area::autoMax) ? 1 : 0) })
            key = textcache::detail::mixHash (key, v);
        key = textcache::detail::mixHash (key, (std::uint64_t) (automation.name.empty() ? tr ("Automation clip") : juce::String::fromUTF8 (automation.name.c_str())).hashCode64());
        auto& picture = autoRowPictures[(std::size_t) i];
        if (picture.key != key || ! picture.image.isValid())
        {
            picture.key = key;
            picture.image = juce::Image (juce::Image::RGB, gridLeft() * scale, rowHeight * scale, false,
                                        #if JUCE_WINDOWS
                                         juce::NativeImageType()
                                        #else
                                         juce::SoftwareImageType()
                                        #endif
                                         );
            juce::Graphics ig (picture.image);
            ig.addTransform (juce::AffineTransform::scale ((float) scale));
            ig.setColour (bg2); // (the column's background, as paint() fills it under the rows)
            ig.fillRect (0, 0, gridLeft(), rowHeight);
            ig.setFont (g.getCurrentFont());
            paintAutomationButton (ig, a, 0, false, true);
        }
        g.setColour (juce::Colours::black); // (an image is drawn with the current colour's alpha as its opacity)
        if (scale == 1)
            g.drawImageAt (picture.image, 0, y);
        else
            g.drawImage (picture.image, juce::Rectangle<float> (0.0f, (float) y, (float) gridLeft(), (float) rowHeight), juce::RectanglePlacement::stretchToFit);
        if (leftPart)
            paintAutomationButton (g, a, y, true, false);
        drewLeft = drewLeft || leftPart;
        paintAutomationSlot (g, a, y);
    }
    if (drewLeft) // (the font a drawn row leaves, as before)
        g.setFont (theme::font (theme::type::body));
}

// FL's row (fl-channelrack.png rows y 834-894): the mute light (round 190ow: working), the two knobs (FL's MIN / MAX of the
// clip, round 190ow: its range), the curve glyph where the mixer track box is, the button in the clip's colour (FL's grey
// #485156 without one) with the name in white and no icon; the selector's slot is paintAutomationSlot's
void ChannelRackComponent::paintAutomationButton (juce::Graphics& g, int a, int y, bool curves, bool rest)
{
    using namespace theme::colour;
    const auto& automation = engine.getSong().automations[(std::size_t) a];
    // (the parts do not overlap: drawn in two passes, the pixels of one)
    if (curves)
    {
        g.setColour (theme::muteLed (automation.muted)); // (round 190ow: FL's mute light, working)
        g.fillEllipse (juce::Rectangle<float> (9.0f, (float) y + rowHeight / 2.0f - 4.5f, 9.0f, 9.0f));
    }

    if (rest) // round 190ow: FL's MIN / MAX of the clip (its channel's pan / volume knobs)
    {
        const auto knob = [y] (int left) { return juce::Rectangle<float> ((float) left, (float) y, (float) knobWidth, (float) rowHeight).reduced (1.0f, 2.0f); };
        const auto knobHover = [&] (Area which) { return (hoverArea == which && hoverRow == a) || (knobDrag == which && knobRow == a); };
        theme::drawKnob (g, knob (muteWidth), automation.rangeMin, false, knobArc, knobHover (Area::autoMin));
        theme::drawKnob (g, knob (muteWidth + knobWidth), automation.rangeMax, false, knobArc, knobHover (Area::autoMax));
    }

    if (curves && showMixerSelectors) // FL's curve glyph: two small rings joined by a rising line
    {
        const auto box = juce::Rectangle<float> ((float) (muteWidth + 2 * knobWidth + 1), (float) y + 7.0f, (float) routeWidth - 3.0f, (float) rowHeight - 14.0f);
        const auto centre = box.getCentre();
        const juce::Point<float> low (centre.x - 5.0f, centre.y + 3.5f), high (centre.x + 5.0f, centre.y - 3.5f);
        const auto along = (high - low) / low.getDistanceFrom (high) * 2.0f;
        g.setColour (textSecondary.withAlpha (0.75f));
        g.drawEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (low), 1.0f);
        g.drawEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (high), 1.0f);
        g.drawLine (juce::Line<float> (low + along, high - along), 1.0f);
    }
    const auto nameArea = juce::Rectangle<float> ((float) nameLeft(), (float) y + 3, (float) nameWidth, (float) rowHeight - 6);
    if (rest)
    {
        auto fill = theme::colourFill (automation.colour != 0 ? juce::Colour (automation.colour) : juce::Colour (0xff485156), 0.42f, 0.58f);
        if (hoverArea == Area::autoName && hoverRow == a)
            fill = fill.brighter (0.12f);
        theme::fillPlate (g, nameArea, theme::radius::sm, fill.withMultipliedBrightness (1.06f), fill.withMultipliedBrightness (0.94f));
    }
    if (! curves) // (the name with the straight parts: a name squeezed to fit is drawn as paths, which depend on where they are drawn)
        return;
    g.setColour (onAccent.withAlpha (0.92f));
    g.setFont (theme::font (theme::type::body + 1.0f));
    const auto textArea = nameArea.withTrimmedLeft (nameAlignLeft ? 7.0f : 6.0f).withTrimmedRight (nameAlignLeft ? 5.0f : 6.0f);
    const auto name = automation.name.empty() ? tr ("Automation clip") : juce::String::fromUTF8 (automation.name.c_str());
    textcache::drawText (g, theme::fitMiddle (name, g.getCurrentFont(), textArea.getWidth()), textArea,
                         nameAlignLeft ? juce::Justification::centredLeft : juce::Justification::centred, false);
    g.setFont (theme::font (theme::type::body));
}

// the selector, outlined in the accent while the row is selected (round 190ow), lit while its clip plays from a step; drawn
// straight (it changes with the playhead), over the row's picture
void ChannelRackComponent::paintAutomationSlot (juce::Graphics& g, int a, int y)
{
    using namespace theme::colour;
    const auto& song = engine.getSong();
    const auto& automation = song.automations[(std::size_t) a];
    const auto selected = automationSelected (a);
    const auto hoveredSelect = hoverArea == Area::autoSelect && hoverRow == a;
    const auto slot = juce::Rectangle<float> ((float) selectLeft() + 1.0f, (float) y + 3.0f, (float) selectWidth - 2.0f, (float) rowHeight - 6.0f);
    theme::fillRoundRect (g, slot, 2.0f, selected ? (hoveredSelect ? accentHover : accent) : hoveredSelect ? textDisabled : border);
    theme::fillRoundRect (g, slot.reduced (selected ? 2.0f : 1.0f), selected ? 1.0f : 1.5f, bg1);
    if (! automation.muted && playheadStep >= 0 && playheadStep < numSteps() && song.activePattern().hasAutoStep (a, playheadStep))
        theme::fillRoundRect (g, slot.reduced (selected ? 3.0f : 2.0f), 1.5f, juce::Colours::white.withAlpha (selected ? 0.75f : 0.9f));
}

// Right-click on an automation clip row: what LYNX already does with a clip (the Playlist's automation clip items)
void ChannelRackComponent::showAutomationMenu (int automation)
{
    const auto& song = engine.getSong();
    if (automation < 0 || automation >= (int) song.automations.size())
        return;
    const auto& a = song.automations[(std::size_t) automation];
    const auto gap = rackauto::gapOf (song, a);
    const auto count = (int) song.channels.size();
    juce::PopupMenu menu;
    menu.addSectionHeader (a.name.empty() ? tr ("Automation clip") : juce::String::fromUTF8 (a.name.c_str()));
    menu.addItem (tr ("Edit points..."), [this, automation] { openAutomationEditor (engine, automation); });
    menu.addItem (tr ("Rename and color..."), [this, automation] {
        const auto& s = engine.getSong();
        if (automation >= (int) s.automations.size())
            return;
        const auto& clip = s.automations[(std::size_t) automation];
        showNameAndColourDialog (tr ("Rename automation clip"), juce::String::fromUTF8 (clip.name.c_str()), clip.colour,
                                 [this, automation] (const juce::String& name, std::uint32_t colour) {
                                     engine.editSong ([automation, text = name.toStdString(), colour] (Song& edited) {
                                         if (automation < (int) edited.automations.size())
                                         {
                                             edited.automations[(std::size_t) automation].name = text;
                                             edited.automations[(std::size_t) automation].colour = colour;
                                         }
                                     });
                                 });
    });
    menu.addItem (tr ("Mute"), true, a.muted, [this, automation] { toggleAutomationMute (automation); }); // (round 190ow)
    menu.addSeparator();
    menu.addItem (tr ("Move up"), gap > 0, false, [this, automation] { engine.editSong ([automation] (Song& s) { rackauto::moveRow (s, automation, -1); }); });
    menu.addItem (tr ("Move down"), gap < count, false, [this, automation] { engine.editSong ([automation] (Song& s) { rackauto::moveRow (s, automation, 1); }); });
    menu.addSeparator();
    menu.addItem (tr ("Delete automation (all of its clips)"), [this, automation] {
        engine.editSong ([automation] (Song& s) { s.removeAutomation (automation); });
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

// The miniature of a channel's piano-roll notes across its step area (FL shows one once a channel has notes
// that steps can't show); the notes keep their time and length, pitch maps onto the row's height.
void ChannelRackComponent::paintNotePreview (juce::Graphics& g, int row, const Pattern& pattern, std::uint32_t channelColour)
{
    using namespace theme::colour;
    const auto steps = numSteps();
    // (the whole pattern in the visible width when it is longer than the rack shows: FL's complete preview)
    const auto place = previewSpan();
    const auto area = juce::Rectangle<float> ((float) (gridLeft() + place.x) + 1.0f, (float) rowY (row) + 4.0f, (float) place.width - 3.0f,
                                              (float) rowHeight - 8.0f);
    const auto beatWidth = (float) place.width * 4.0f / (float) steps;
    const auto hoveredHere = hoverArea == Area::grid && hoverRow == row;
    theme::fillRoundRect (g, area, theme::radius::sm, hoveredHere ? bg4 : bg1);

    for (int beat = 4; beat < steps; beat += 4) // beat lines, bars a little stronger (bars only once beats crowd)
    {
        if (beat % 16 != 0 && beatWidth < 8.0f)
            continue;
        g.setColour (beat % 16 == 0 ? border.brighter (0.2f) : border);
        g.fillRect (area.getX() + beatWidth * (float) (beat / 4) - 1.5f, area.getY() + 2.0f, 1.0f, area.getHeight() - 4.0f);
    }

    const auto [low, high] = channelKeyRange (pattern, row);
    const auto span = juce::jmax (high - low, 6); // a single key sits mid-row, not on an edge
    const auto lowKey = low - (span - (high - low)) / 2;
    const auto inner = area.reduced (1.0f, 3.0f);
    const auto noteHeight = juce::jlimit (3.0f, 5.0f, inner.getHeight() / (float) (span + 1));
    const auto ticks = (float) (steps * ticksPerStep);
    const auto tint = channelColour != 0 ? theme::tame (juce::Colour (channelColour)).brighter (0.6f) : stepNote;

    // a looping row (round 165f-rack): the notes before the loop point, then their repeats faint; a note of its own past
    // the loop point doesn't play (dim)
    const auto loopTicks = (std::int64_t) loopStepsOf (row) * ticksPerStep;
    const auto patternTicks = (std::int64_t) steps * ticksPerStep;
    for (const auto& n : pattern.notes)
    {
        if (n.channel != row)
            continue;

        const auto pastLoop = loopTicks > 0 && n.start >= loopTicks;
        const auto repeats = loopTicks > 0 && ! pastLoop ? (int) ((patternTicks - n.start + loopTicks - 1) / loopTicks) : 1;
        for (int k = 0; k < repeats; ++k)
        {
            const auto start = n.start + k * loopTicks;
            const auto x = inner.getX() + inner.getWidth() * (float) start / ticks;
            if (x > inner.getRight())
                break;
            const auto w = juce::jmax (2.0f, inner.getWidth() * (float) n.length / ticks - 1.0f);
            const auto y = inner.getBottom() - noteHeight - (inner.getHeight() - noteHeight) * (float) (n.key - lowKey) / (float) span;
            g.setColour (n.muted || pastLoop ? textDisabled.withAlpha (pastLoop ? 0.5f : 1.0f) : k > 0 ? tint.withAlpha (0.4f) : tint);
            g.fillRect (juce::Rectangle<float> (x, y, juce::jmin (w, inner.getRight() - x), noteHeight));
        }
    }

    // round 190mk (DE-33, capture 190mk A2.3; FL's option "Show a mute button on the piano roll preview", on): a round
    // mute button in the preview's middle while it is hovered; a muted channel's preview dims and keeps the crossed icon
    const auto muted = row >= 0 && row < (int) engine.getSong().channels.size() && engine.getSong().channels[(std::size_t) row].muted;
    if (muted)
    {
        g.setColour (bg1.withAlpha (0.55f));
        g.fillRect (area);
    }
    if (previewMuteButton && (hoveredHere || muted))
    {
        const auto b = previewMuteBounds (row).toFloat();
        const auto hot = hoveredHere && b.contains (getMouseXYRelative().toFloat());
        g.setColour (hot ? bg5 : bg3);
        g.fillEllipse (b);
        g.setColour (border);
        g.drawEllipse (b.reduced (0.5f), 1.0f);
        // a speaker: box and cone, then sound waves (on) or a cross (muted)
        const auto c = b.getCentre();
        const auto u = b.getWidth() / 18.0f;
        juce::Path speaker;
        speaker.startNewSubPath (c.x - 5.0f * u, c.y - 2.0f * u);
        speaker.lineTo (c.x - 2.5f * u, c.y - 2.0f * u);
        speaker.lineTo (c.x + 0.5f * u, c.y - 5.0f * u);
        speaker.lineTo (c.x + 0.5f * u, c.y + 5.0f * u);
        speaker.lineTo (c.x - 2.5f * u, c.y + 2.0f * u);
        speaker.lineTo (c.x - 5.0f * u, c.y + 2.0f * u);
        speaker.closeSubPath();
        g.setColour (muted ? textSecondary : textPrimary);
        g.fillPath (speaker);
        if (muted)
        {
            g.drawLine (c.x + 2.0f * u, c.y - 2.5f * u, c.x + 5.5f * u, c.y + 2.5f * u, 1.2f * u);
            g.drawLine (c.x + 2.0f * u, c.y + 2.5f * u, c.x + 5.5f * u, c.y - 2.5f * u, 1.2f * u);
        }
        else
        {
            juce::Path wave;
            wave.addCentredArc (c.x + 0.5f * u, c.y, 3.5f * u, 3.5f * u, 0.0f, 0.6f, 2.55f, true);
            g.strokePath (wave, juce::PathStrokeType (1.1f * u));
        }
    }
}

juce::Rectangle<int> ChannelRackComponent::previewMuteBounds (int row) const
{
    const auto place = previewSpan();
    const auto centre = juce::Point<int> (gridLeft() + place.x + place.width / 2, rowY (row) + rowHeight / 2);
    return juce::Rectangle<int> (18, 18).withCentre (centre);
}

// --- the control contract for the knobs and the route box ----------------------------------------

ui::ValueSpec ChannelRackComponent::specFor (Area area, int row) const
{
    if (area == Area::autoMin || area == Area::autoMax) // round 190ow: FL's MIN / MAX of an automation clip (`row` = it)
    {
        const auto& song = engine.getSong();
        const auto clip = row >= 0 && row < (int) song.automations.size() && ! song.automations[(std::size_t) row].name.empty()
                              ? juce::String::fromUTF8 (song.automations[(std::size_t) row].name.c_str())
                              : tr ("Automation clip");
        return ui::percentSpec (clip + " - " + (area == Area::autoMin ? tr ("Minimum") : tr ("Maximum")), area == Area::autoMin ? 0.0 : 1.0);
    }
    const auto name = channelName (engine.getSong(), row);

    if (area == Area::pan)
    {
        auto spec = ui::panSpec (name + " - " + tr ("Pan"));
        spec.target = AutomationTarget { AutomationTargetKind::channelPan, row };
        return spec;
    }

    if (area == Area::volume)
    {
        auto spec = ui::percentSpec (name + " - " + tr ("Volume"), 0.78);
        spec.target = AutomationTarget { AutomationTargetKind::channelVolume, row };
        return spec;
    }

    // route box: the mixer insert, as a number
    ui::ValueSpec spec;
    spec.name = name + " - " + tr ("Target mixer track");
    spec.maximum = maxMixerTracks - 1; // every insert, like FL (round 162: the inserts past the mixer's last one are added)
    spec.interval = 1.0;
    spec.toText = [] (double v) { return v < 0.5 ? tr ("Master") : tr ("Insert {0}", { juce::String (juce::roundToInt (v)) }); };
    spec.fromText = [] (const juce::String& t) -> std::optional<double> {
        if (t.trim().startsWithIgnoreCase ("m"))
            return 0.0;
        const auto digits = t.retainCharacters ("0123456789");
        return digits.isEmpty() ? std::nullopt : std::optional<double> (digits.getDoubleValue());
    };
    return ui::digitWheel (spec); // FL's channel rack mixer-track digiwheel (round 168s)
}

double ChannelRackComponent::valueOf (Area area, int row) const
{
    if (area == Area::autoMin || area == Area::autoMax) // (round 190ow)
    {
        const auto& song = engine.getSong();
        if (row < 0 || row >= (int) song.automations.size())
            return 0.0;
        return area == Area::autoMin ? song.automations[(std::size_t) row].rangeMin : song.automations[(std::size_t) row].rangeMax;
    }
    const auto& c = engine.getSong().channels[(std::size_t) row];
    return area == Area::pan ? c.pan : area == Area::volume ? c.volume : (double) c.mixerTrack;
}

void ChannelRackComponent::setValue (Area area, int row, double value)
{
    if (area == Area::autoMin || area == Area::autoMax) // (round 190ow: the clip re-ranges, its points stay)
    {
        if (row >= 0 && row < (int) engine.getSong().automations.size())
            engine.editLevels ([row, min = area == Area::autoMin, v = juce::jlimit (0.0f, 1.0f, (float) value)] (Song& s) {
                auto& a = s.automations[(std::size_t) row];
                (min ? a.rangeMin : a.rangeMax) = v;
            });
        return;
    }
    if (row < 0 || row >= (int) engine.getSong().channels.size())
        return;

    if (area == Area::pan)
        engine.editLevels ([row, v = (float) value] (Song& s) { s.channels[(std::size_t) row].pan = v; });
    else if (area == Area::volume)
        engine.editLevels ([row, v = (float) value] (Song& s) { s.channels[(std::size_t) row].volume = v; });
    else if (area == Area::route)
    {
        const auto track = juce::roundToInt (value);
        if (track != engine.getSong().channels[(std::size_t) row].mixerTrack)
            engine.editSong ([row, track] (Song& s) { mixerops::routeChannel (s, row, track); });
    }
}

juce::Rectangle<int> ChannelRackComponent::controlBounds (Area area, int row) const
{
    if (area == Area::autoMin || area == Area::autoMax) // (round 190ow: `row` = the automation)
    {
        const auto r = automationRowBounds (row);
        return { area == Area::autoMin ? muteWidth : muteWidth + knobWidth, r.getY(), knobWidth, rowHeight };
    }
    const auto y = rowY (row);

    switch (area)
    {
        case Area::pan: return { muteWidth, y, knobWidth, rowHeight };
        case Area::volume: return { muteWidth + knobWidth, y, knobWidth, rowHeight };
        case Area::route: return showMixerSelectors ? juce::Rectangle<int> { muteWidth + 2 * knobWidth + 1, y + 7, routeWidth - 3, rowHeight - 14 } : juce::Rectangle<int>();
        case Area::lane:
        {
            const auto lane = laneBounds();
            const auto step = juce::jmax (0, hoverStep);
            return { gridLeft() + step * stepWidth, lane.getY() + 6, stepWidth, lane.getHeight() - 12 };
        }
        default: return {};
    }
}

void ChannelRackComponent::showTip (Area area, int row, bool transient)
{
    if (area == Area::autoMin || area == Area::autoMax ? row < 0 || row >= (int) engine.getSong().automations.size()
                                                       : row < 0 || row >= (int) engine.getSong().channels.size())
        return;

    const auto spec = specFor (area, row);
    const auto text = spec.text (valueOf (area, row));
    ui::postHint (ui::valueHint (spec, valueOf (area, row), ui::valueGestures()));

    const auto bounds = area == Area::lane ? controlBounds (area, row) : controlBounds (area, row).translated (stickyX, 0);

    if (transient)
        ui::flashValueTip (*this, bounds, text);
    else
        ui::showValueTip (*this, bounds, text);
}

void ChannelRackComponent::setHover (Area area, int row, int step)
{
    if (area == hoverArea && row == hoverRow && step == hoverStep)
        return;

    auto dirty = [this] (Area a, int r) {
        if (a == Area::lane || a == Area::laneLevels)
            repaint (laneBounds());
        else if (a == Area::addRow)
            repaint (rowBounds ((int) engine.getSong().channels.size()));
        else if (isAutomationArea (a)) // (round 190oo: `r` is an automation)
            repaint (automationRowBounds (r));
        else if (r >= 0)
            repaint (rowBounds (r));
    };

    dirty (hoverArea, hoverRow);
    hoverArea = area;
    hoverRow = row;
    hoverStep = step;
    dirty (hoverArea, hoverRow);
}

void ChannelRackComponent::mouseMove (const juce::MouseEvent& e)
{
    int row = 0;
    const auto area = areaAt (e.x, e.y, row);
    const auto step = area == Area::grid || area == Area::lane || area == Area::autoStep ? (e.x - gridLeft()) / stepWidth : -1;
    setHover (area, row, step >= 0 && step < (area == Area::grid || area == Area::autoStep ? shownSteps() : numSteps()) ? step : -1);
}

void ChannelRackComponent::mouseExit (const juce::MouseEvent&)
{
    setHover (Area::none, -1, -1);
}

void ChannelRackComponent::setSwingMixOfSelected (int channel)
{
    const auto& song = engine.getSong();

    if (channel < 0 || channel >= (int) song.channels.size())
        return;

    auto spec = ui::percentSpec (channelName (song, channel) + " - " + tr ("Swing mix"), 1.0);
    const auto rows = engine.getSelectedChannels();
    ui::typeValue (*this, spec, song.channels[(std::size_t) channel].swingMix, [this, rows, channel] (double v) {
        engine.editSong ([rows, channel, v] (Song& s) {
            auto targets = rows;
            if (std::find (targets.begin(), targets.end(), channel) == targets.end())
                targets = { channel };
            for (auto r : targets)
                if (r >= 0 && r < (int) s.channels.size())
                    s.channels[(std::size_t) r].swingMix = (float) v;
        });
    });
}

// --- selection -----------------------------------------------------------------------------------

void ChannelRackComponent::selectRows (std::vector<int> rows, int primary)
{
    // (round 190ow: a channel selection ends an automation row's own; a Ctrl / right click adds to it, clickSelector)
    if (! keepAutoSelection)
        autoSelected.clear();
    autoOnly = false;
    engine.setChannelSelection (std::move (rows), primary);
    repaint();
}

// --- round 190ow (PAT-7 rest, DE-123): the automation clip rows' selection, steps, mute light ------------------------
// FL selects an automation clip channel as any channel: a click on its selector selects it alone, Ctrl+click (or a
// right click) adds or removes it; the selected rows move with Alt+Up / Alt+Down and go with Delete.

void ChannelRackComponent::clickAutomationSelector (int automation, const juce::ModifierKeys& mods)
{
    if (flmods::ctrl (mods) || flmods::popup (mods) || mods.isShiftDown())
    {
        if (const auto it = std::lower_bound (autoSelected.begin(), autoSelected.end(), automation);
            it != autoSelected.end() && *it == automation)
            autoSelected.erase (it);
        else
            autoSelected.insert (it, automation);
        if (autoSelected.empty())
            autoOnly = false;
    }
    else
    {
        autoSelected = { automation };
        autoOnly = true;
    }
    repaint();
}

void ChannelRackComponent::clickAutomationSelectorForSnapshot (int automation, bool add)
{
    clickAutomationSelector (automation, add ? juce::ModifierKeys (juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::commandModifier) : juce::ModifierKeys());
}

// the selected rows one channel row up / down (each passes the channel next to it); false when one is at the end
bool ChannelRackComponent::moveSelectedAutomations (int delta)
{
    const auto& song = engine.getSong();
    const auto count = (int) song.channels.size();
    for (const auto a : autoSelected)
    {
        if (a < 0 || a >= (int) song.automations.size())
            return false;
        const auto gap = rackauto::gapOf (song, song.automations[(std::size_t) a]);
        if ((delta < 0 && gap <= 0) || (delta > 0 && gap >= count))
            return false;
    }
    engine.editSong ([rows = autoSelected, delta] (Song& s) {
        for (const auto a : rows)
            rackauto::moveRow (s, a, delta);
    });
    updateSize();
    repaint();
    return true;
}

void ChannelRackComponent::deleteSelectedAutomations()
{
    if (autoSelected.empty())
        return;
    engine.editSong ([rows = autoSelected] (Song& s) {
        for (auto it = rows.rbegin(); it != rows.rend(); ++it) // (from the last: the indices before stay)
            s.removeAutomation (*it);
    });
    autoSelected.clear();
    autoOnly = false;
    updateSize();
    repaint();
}

void ChannelRackComponent::toggleAutomationMute (int automation)
{
    if (automation >= 0 && automation < (int) engine.getSong().automations.size())
        engine.editSong ([automation] (Song& s) { s.automations[(std::size_t) automation].muted = ! s.automations[(std::size_t) automation].muted; });
}

// a drag over an automation row's steps paints that row only (FL's drag along a channel's steps)
void ChannelRackComponent::applyAutoPaint (const juce::MouseEvent& e)
{
    const auto a = autoPaintRow;
    const auto step = (e.x - gridLeft()) / stepWidth;
    if (a < 0 || a >= (int) engine.getSong().automations.size() || step < 0 || step >= shownSteps() || e.x < stickyX + gridLeft()
        || step == lastPaintedStep)
        return;
    lastPaintedStep = step;
    const auto on = paintMode == PaintMode::add;
    engine.editSong ([a, step, on] (Song& s) {
        auto& p = s.activePattern();
        if (p.hasAutoStep (a, step) != on)
            p.toggleAutoStep (a, step);
        if (on) // (a ghost step: the pattern's set length grows, as a channel's step makes it)
            p.lengthTicks = lengthWithStep (p.lengthTicks, step, ticksPerStep, s.barTicks());
    });
}

void ChannelRackComponent::clickSelector (int row, const juce::ModifierKeys& mods)
{
    ChannelSelection selection;
    selection.rows = engine.getSelectedChannels();
    selection.anchor = selectionAnchor;

    const auto adds = flmods::ctrl (mods) || flmods::popup (mods);
    if (adds && autoOnly) // (round 190ow: the channels were shown as not selected; this one joins the automation rows)
        selection.selectOnly (row);
    else if (mods.isShiftDown())
        selection.extendTo (row);
    else if (adds)
        selection.toggle (row);
    else
        selection.selectOnly (row);

    selectionAnchor = selection.anchor;
    const auto primary = selection.contains (row) ? row : selection.rows.front();
    keepAutoSelection = adds; // (round 190ow)
    selectRows (selection.rows, primary);
    keepAutoSelection = false;
}

void ChannelRackComponent::selectRelative (int delta, bool extend)
{
    const auto count = (int) engine.getSong().channels.size();

    if (count == 0)
        return;

    const auto row = juce::jlimit (0, count - 1, engine.getSelectedChannel() + delta);

    if (extend)
    {
        ChannelSelection selection;
        selection.anchor = selectionAnchor;
        selection.extendTo (row);
        selectRows (selection.rows, row);
    }
    else
    {
        selectionAnchor = row;
        selectRows ({ row }, row);
    }
}

void ChannelRackComponent::selectAll()
{
    ChannelSelection selection;
    selection.selectAll ((int) engine.getSong().channels.size());
    selectRows (selection.rows, engine.getSelectedChannel());
}

bool ChannelRackComponent::moveSelected (int delta)
{
    if (autoOnly && ! autoSelected.empty()) // round 190ow: the selected automation clip rows
        return moveSelectedAutomations (delta);
    auto rows = engine.getSelectedChannels();
    const auto primary = engine.getSelectedChannel();
    const auto primaryIndex = (int) (std::find (rows.begin(), rows.end(), primary) - rows.begin());
    bool moved = false;
    std::vector<int> after;

    // round 190oo (PAT-7): FL moves channels past an automation clip channel as past any channel: an automation clip row
    // right next to the selected block goes to its other side, the channels keep their rows (core/RackAutomation.h)
    if (! autoRows.empty())
        if (const auto pass = rackauto::passFor (engine.getSong(), rows, delta); pass.first >= 0)
        {
            engine.editSong ([pass] (Song& s) {
                if (pass.first < (int) s.automations.size())
                    s.automations[(std::size_t) pass.first].rackSlot = pass.second;
            });
            updateSize();
            repaint();
            return true;
        }

    engine.editSongStructural ([&] (Song& s) {
        after = rows;
        moved = moveChannels (s, after, delta);
    });

    if (! moved)
        return false;

    const auto newPrimary = primaryIndex < (int) after.size() ? after[(std::size_t) primaryIndex] : after.front();
    openEditorRow = -1;
    selectionAnchor = juce::jlimit (0, (int) engine.getSong().channels.size() - 1, selectionAnchor + delta);
    selectRows (after, newPrimary);
    return true;
}

void ChannelRackComponent::cloneSelected()
{
    auto rows = engine.getSelectedChannels();
    std::vector<int> clones;

    engine.editSong ([&] (Song& s) {
        // from the bottom up, so earlier clones don't shift the rows still to clone
        for (auto it = rows.rbegin(); it != rows.rend(); ++it)
            cloneChannel (s, *it);
    });

    // each clone sits right below its original: original i (sorted) ends at rows[i] + i, its clone one below
    for (std::size_t i = 0; i < rows.size(); ++i)
        clones.push_back (rows[i] + (int) i + 1);

    if (! clones.empty())
        selectRows (clones, clones.front());
}

void ChannelRackComponent::deleteSelected()
{
    // round 190ow: the selected automation clip rows go too (FL deletes the selected channels, automation clips included)
    const auto onlyAutomations = autoOnly;
    deleteSelectedAutomations();
    if (onlyAutomations)
        return;
    const auto rows = engine.getSelectedChannels();
    const auto count = (int) engine.getSong().channels.size();

    if (rows.empty() || (int) rows.size() >= count) // at least one channel stays
        return;

    for (auto r : rows)
        closeEditor (r);

    engine.editSong ([rows] (Song& s) { removeChannels (s, rows); });
    openEditorRow = -1;
    const auto row = juce::jlimit (0, juce::jmax (0, (int) engine.getSong().channels.size() - 1), rows.front());
    selectRows ({ row }, row);
}

void ChannelRackComponent::sendSelectedToInserts()
{
    const auto rows = engine.getSelectedChannels();
    engine.editSong ([rows] (Song& s) { sendChannelsToFreeInserts (s, rows); });
}

void ChannelRackComponent::toggleMute (int channel)
{
    if (channel >= 0 && channel < (int) engine.getSong().channels.size())
        engine.editSong ([channel] (Song& s) { s.channels[(std::size_t) channel].muted = ! s.channels[(std::size_t) channel].muted; });
}

void ChannelRackComponent::toggleSoloLock (int channel)
{
    if (channel >= 0 && channel < (int) engine.getSong().channels.size())
        engine.editSong ([channel] (Song& s) { s.channels[(std::size_t) channel].soloLock = ! s.channels[(std::size_t) channel].soloLock; });
}

void ChannelRackComponent::solo (int row)
{
    if (row < 0 || row >= (int) engine.getSong().channels.size())
        return;

    // only this channel plays (the channels with "Lock active/mute state" keep theirs); repeating on the soloed channel
    // unmutes them (FL's channel solo, REA_rodada167l §5)
    // (round 190bx: FL's General › "Restore previous state after solo" — the un-solo then puts back the mute switches
    // from before the solo; core/FlSettings.h)
    engine.editSong ([row] (Song& s) {
        noteedit::soloChannel (s, row, &flsettings::channelSoloMemory(), flsettings::options().restoreStateAfterSolo.load());
    });
}

// --- channel editors ---------------------------------------------------------------------------

bool ChannelRackComponent::isEditorOpen (int channel)
{
    if (channel < 0 || channel >= (int) engine.getSong().channels.size())
        return false;

    if (engine.getSong().channels[(std::size_t) channel].instrument == InstrumentKind::plugin)
    {
        if (auto* plugin = dynamic_cast<PluginInstrument*> (engine.getChannelInstrument (channel).get()))
            return PluginWindows::isOpen (plugin->getInstance());
        return false;
    }

    return isChannelSettingsOpen (channel);
}

void ChannelRackComponent::closeEditor (int channel)
{
    if (channel < 0 || channel >= (int) engine.getSong().channels.size())
        return;

    if (engine.getSong().channels[(std::size_t) channel].instrument == InstrumentKind::plugin)
    {
        if (auto* plugin = dynamic_cast<PluginInstrument*> (engine.getChannelInstrument (channel).get()))
            PluginWindows::close (plugin->getInstance());
    }
    else
        closeChannelSettings (channel);
}

// --- Freeze (round 190ay) -----------------------------------------------------------------------------------------------

ui::Hint ChannelRackComponent::getChannelHintForSnapshot (int channel)
{
    return hintAt ({ stickyX + nameLeft() + nameWidth / 2, rowY (channel) + rowHeight / 2 });
}

juce::String ChannelRackComponent::frozenHint (int channel)
{
    if (engine.frozenAudioMissing (channel))
        return tr ("Frozen, but its audio files are missing: the instrument plays instead.  [RMB] Re-freeze or Unfreeze");
    if (engine.freezeOutOfDate (channel))
        return tr ("Frozen, out of date (changed since the freeze): the old audio plays.  [RMB] Re-freeze"); // (round 190bj: shorter, the action fits the bar)
    return tr ("Frozen: its rendered audio plays and its instrument is unloaded (no CPU, no RAM).  [RMB] Unfreeze to edit");
}

bool ChannelRackComponent::refuseFrozenEdit (int channel)
{
    const auto& song = engine.getSong();
    if (channel < 0 || channel >= (int) song.channels.size() || ! song.channels[(std::size_t) channel].freeze.on())
        return false;
    ui::postNotice ({ tr ("{0} is frozen: Unfreeze to edit ([RMB] on the channel, Unfreeze)", { channelName (song, channel) }), {} }, 3000);
    return true;
}

void ChannelRackComponent::freezeChannels (std::vector<std::uint64_t> uids)
{
    // round 190bj (QA: "Freeze selected channels" stopped at the first channel that can't be frozen with "Can't freeze
    // this channel", not saying which): with several, the ones that can't are named with their reason and the others
    // are frozen, as the selection's Unfreeze treats each channel on its own
    if (uids.size() > 1)
    {
        const auto& song = engine.getSong();
        juce::StringArray refused;
        std::vector<std::uint64_t> go;
        for (const auto uid : uids)
        {
            const auto it = std::find_if (song.channels.begin(), song.channels.end(), [uid] (const ChannelDef& c) { return c.uid == uid && uid != 0; });
            if (it == song.channels.end())
                continue;
            if (const auto why = freezeRefusal (engine, (int) (it - song.channels.begin())); why.isNotEmpty())
                refused.add (juce::String::fromUTF8 (it->name.c_str()) + ": " + why);
            else
                go.push_back (uid);
        }
        if (! refused.isEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Some selected channels can't be frozen"),
                                                    refused.joinIntoString ("\n\n")
                                                        + (go.empty() ? juce::String() : "\n\n" + tr ("The other selected channels are frozen.")));
        uids = std::move (go);
    }
    while (! uids.empty())
    {
        const auto uid = uids.front();
        uids.erase (uids.begin());
        const auto& channels = engine.getSong().channels;
        const auto it = std::find_if (channels.begin(), channels.end(), [uid] (const ChannelDef& c) { return c.uid == uid && uid != 0; });
        if (it == channels.end())
            continue;
        const auto channel = (int) (it - channels.begin());
        if (openEditorRow == channel) // (freezeChannel closes the channel's windows)
            openEditorRow = -1;
        freezeChannel (engine, channel, [safe = juce::Component::SafePointer<ChannelRackComponent> (this), uids] (bool ok) {
            if (safe == nullptr)
                return;
            safe->repaint();
            if (ok && ! uids.empty()) // (a refusal or Cancel stops the rest)
                safe->freezeChannels (uids);
        });
        return;
    }
}

void ChannelRackComponent::toggleEditor (int channel, bool keepOthers)
{
    if (channel < 0 || channel >= (int) engine.getSong().channels.size())
        return;
    if (refuseFrozenEdit (channel)) // (its instrument is not loaded: there is nothing to show or change)
        return;

    const auto kind = engine.getSong().channels[(std::size_t) channel].instrument;

    if (kind == InstrumentKind::patcher) // its node graph editor (REA_rodada141)
    {
        if (PatcherWindows::isChannelOpen (channel))
            PatcherWindows::closeChannel (channel);
        else
            PatcherWindows::openChannel (engine, channel);
        return;
    }
    // built-in drums and audio clips have no instrument page: as in FL, the click opens their channel window anyway
    // (header + Misc tab — levels, polyphony, cut groups, arpeggiator), as the hint says; it only selected (round 165q)

    if (isEditorOpen (channel))
    {
        closeEditor (channel);
        if (openEditorRow == channel)
            openEditorRow = -1;
        return;
    }

    if (! keepOthers && ! keepEditorsOpen && openEditorRow != channel)
        closeEditor (openEditorRow);

    if (kind == InstrumentKind::plugin)
        openPluginWindow (channel);
    else
        openChannelSettings (engine, channel);

    openEditorRow = channel;
}

// --- step graph lanes (docs/WORKFLOW_FL.md P2, C4) --------------------------------------------------

juce::Rectangle<int> ChannelRackComponent::levelButtonBounds (int tab) const
{
    const auto lane = laneBounds();
    const auto width = (gridLeft() - 24) / 3;
    return { 12 + (tab % 3) * width, lane.getY() + 6 + (tab / 3) * 22, width - 4, 19 };
}

void ChannelRackComponent::paintLane (juce::Graphics& g)
{
    using namespace theme::colour;
    const auto lane = laneBounds();

    if (lane.isEmpty() || ! g.getClipBounds().intersects (lane))
        return;

    const auto& song = engine.getSong();
    const auto& pattern = song.activePattern();
    const auto channel = graphRow();
    const auto level = (notetools::Level) graphLevel;

    g.setColour (bg0);
    g.fillRect (lane);
    g.setColour (border);
    g.drawHorizontalLine (lane.getY(), 0.0f, (float) getWidth());
    g.drawHorizontalLine (lane.getBottom() - 1, 0.0f, (float) getWidth());

    // FL's nine lanes in FL's order (issue #208); like FL, a narrow tab takes the shorter name
    for (int tab = 0; tab < notetools::stepLevelCount; ++tab)
    {
        const auto l = (int) notetools::stepLevel (tab);
        const auto r = levelButtonBounds (tab).toFloat();
        theme::fillRoundRect (g, r, theme::radius::sm, l == graphLevel ? accent : bg4);
        g.setColour (l == graphLevel ? onAccent : textSecondary);
        const auto font = theme::font (theme::type::caption, l == graphLevel);
        g.setFont (font);
        auto name = tr (notetools::stepLevelName (tab, 2));
        for (int size = 1; size >= 0 && juce::GlyphArrangement::getStringWidth (font, name) > r.getWidth() - 6.0f; --size)
            name = tr (notetools::stepLevelName (tab, size));
        textcache::drawFittedText (g, name, r.toNearestInt(), juce::Justification::centred, 1, 0.8f);
    }

    g.setColour (textDisabled);
    g.setFont (theme::font (theme::type::caption));
    textcache::drawFittedText (g, tr ("Drag: set  ·  Ctrl+drag: scale all  ·  Right-drag: ramp  ·  Alt+click: reset"),
                      juce::Rectangle<int> (12, lane.getBottom() - 20, gridLeft() - 24, 16), juce::Justification::centredLeft, 1, 0.8f);

    const auto top = (float) lane.getY() + 6.0f, bottom = (float) lane.getBottom() - 6.0f;
    const auto bipolar = notetools::graphIsBipolar (graphLevel);
    const auto base = bipolar ? (top + bottom) * 0.5f : bottom;

    if (level == notetools::Level::notePitch) // FL's piano-roll rows: the black keys darker, the C rows named
    {
        const auto [lo, hi] = pitchLaneKeys();
        const auto rowH = (bottom - top) / (float) (hi - lo + 1);
        for (int key = lo; key <= hi; ++key)
        {
            const auto y = bottom - (float) (key - lo + 1) * rowH;
            if (juce::MidiMessage::isMidiNoteBlack (key))
            {
                g.setColour (bg1);
                g.fillRect (juce::Rectangle<float> ((float) gridLeft(), y, (float) (numSteps() * stepWidth), rowH));
            }
            if (key % 12 == 0 && rowH * 3.0f >= 9.0f)
            {
                g.setColour (textDisabled);
                g.setFont (theme::font (theme::type::caption));
                textcache::drawText (g, juce::String (notetools::levelText (level, (float) key / 127.0f)),
                                     juce::Rectangle<int> (gridLeft() + 2, (int) (y + rowH - 10.0f), 28, 10), juce::Justification::bottomLeft);
            }
        }
    }

    for (int step = 0; step < numSteps(); ++step)
    {
        const auto x = (float) (gridLeft() + step * stepWidth + 1);
        const auto w = (float) stepWidth - 3.0f;

        if (step % 4 == 0 && step > 0)
        {
            g.setColour (step % 16 == 0 ? gridBar : gridBeat);
            g.drawVerticalLine ((int) x - 2, top, bottom);
        }

        if (hoverArea == Area::lane && hoverStep == step)
        {
            g.setColour (bg3);
            g.fillRect (juce::Rectangle<float> (x, top, w, bottom - top));
        }

        const auto notes = notesInStep (pattern, channel, step);

        if (notes.empty())
        {
            g.setColour (theme::stepOff (step));
            g.fillRect (x + w * 0.5f - 1.0f, base - 1.0f, 2.0f, 2.0f);
            continue;
        }

        const auto& first = pattern.notes[(std::size_t) notes.front()];
        if (level == notetools::Level::notePitch) // FL: a block at the step's key, named
        {
            const auto [lo, hi] = pitchLaneKeys();
            const auto rowH = (bottom - top) / (float) (hi - lo + 1);
            const auto block = juce::Rectangle<float> (x + 1.0f, bottom - (float) (first.key - lo + 1) * rowH, w - 2.0f, juce::jmax (3.0f, rowH));
            g.setColour (info.withAlpha (0.85f));
            g.fillRoundedRectangle (block, 1.5f);
            if (block.getHeight() >= 9.0f && block.getWidth() >= 14.0f)
            {
                g.setColour (bg0);
                g.setFont (theme::font (theme::type::caption));
                textcache::drawFittedText (g, juce::String (notetools::levelText (level, (float) first.key / 127.0f)), block.toNearestInt(),
                                           juce::Justification::centred, 1, 0.6f);
            }
            continue;
        }
        if (level == notetools::Level::repeat) // FL: a block at the rate's height, a line for each extra strike in a step
        {
            if (first.repeat <= 7)
                continue; // (no second strike inside a step: FL draws nothing)
            const auto rowH = (bottom - top) / 8.0f;
            const auto block = juce::Rectangle<float> (x + 1.0f, bottom - (float) (first.repeat - 6) * rowH, w - 2.0f, rowH);
            g.setColour (info.withAlpha (0.85f));
            g.fillRoundedRectangle (block, 1.5f);
            const auto strikes = noterepeat::strikeCount (first, ticksPerStep);
            g.setColour (bg0.withAlpha (0.55f));
            for (int k = 1; k < strikes; ++k)
                g.fillRect (block.getX() + 3.0f, block.getY() + block.getHeight() * (float) k / (float) strikes - 0.5f, block.getWidth() - 6.0f, 1.0f);
            continue;
        }
        const auto v = notetools::levelValue (first, level);
        const auto y = bottom - v * (bottom - top);
        g.setColour ((graphLevel == (int) notetools::Level::velocity ? accent : info).withAlpha (0.85f));
        g.fillRect (juce::Rectangle<float> (x + 2.0f, juce::jmin (y, base), w - 4.0f, juce::jmax (2.0f, std::abs (base - y))));
        g.setColour (textPrimary);
        g.fillRect (x + 1.0f, y - 1.0f, w - 2.0f, 2.0f);
    }

    if (bipolar)
    {
        g.setColour (gridBar);
        g.drawHorizontalLine ((int) base, (float) gridLeft(), (float) getWidth());
    }
}

// FL's Note pitch lane is a small piano roll (issue #208, FL 2026 shot fl-note): the keys from a little below the
// channel's lowest step to a little above its highest (two octaves at least), each step a block at its key
std::pair<int, int> ChannelRackComponent::pitchLaneKeys() const
{
    const auto& pattern = engine.getSong().activePattern();
    const auto channel = graphRow();
    int lo = 127, hi = 0;
    for (const auto& n : pattern.notes)
        if (n.channel == channel)
            lo = juce::jmin (lo, n.key), hi = juce::jmax (hi, n.key);
    if (lo > hi)
        lo = hi = 60;
    const auto middle = (lo + hi) / 2;
    lo = juce::jmin (lo - 2, middle - 12);
    hi = juce::jmax (hi + 2, middle + 12);
    if (lo < 0)
        hi -= lo, lo = 0;
    if (hi > 127)
        lo = juce::jmax (0, lo - (hi - 127)), hi = 127;
    return { lo, hi };
}

float ChannelRackComponent::laneValueAt (int y) const
{
    const auto lane = laneBounds();
    const auto top = (float) lane.getY() + 6.0f, bottom = (float) lane.getBottom() - 6.0f;
    const auto t = juce::jlimit (0.0f, 1.0f, (bottom - (float) y) / juce::jmax (1.0f, bottom - top));
    const auto level = (notetools::Level) graphLevel;
    if (level == notetools::Level::notePitch) // (the key row under the mouse)
    {
        const auto [lo, hi] = pitchLaneKeys();
        return (float) juce::jlimit (0, 127, lo + (int) std::floor (t * (float) (hi - lo + 1))) / 127.0f;
    }
    if (level == notetools::Level::repeat) // FL's lane: 1/16 (one strike a step) at the bottom .. 1/64 triplet at the top
    {
        const auto index = juce::jlimit (7, noterepeat::rateCount, 7 + (int) std::floor (t * 8.0f));
        return index <= 7 ? 0.0f : (float) index / (float) noterepeat::rateCount;
    }
    return t;
}

juce::Point<int> ChannelRackComponent::laneSpotForSnapshot (int step, float value) const
{
    const auto lane = laneBounds();
    const auto top = (float) lane.getY() + 6.0f, bottom = (float) lane.getBottom() - 6.0f;
    return { gridLeft() + step * stepWidth + stepWidth / 2, juce::roundToInt (bottom - value * (bottom - top)) };
}

void ChannelRackComponent::setLaneValue (int step, float value)
{
    const auto channel = graphRow();
    const auto level = (notetools::Level) graphLevel;
    engine.editSong ([channel, step, value, level] (Song& s) {
        auto& p = s.activePattern();
        for (auto i : notesInStep (p, channel, step))
            notetools::setLevelValue (p.notes[(std::size_t) i], level, value);
        if (level == notetools::Level::shift) // (Shift moves the notes inside their step)
            p.sortNotes();
    });
}

void ChannelRackComponent::applyLane (const juce::MouseEvent& e)
{
    const auto steps = numSteps();
    const auto step = juce::jlimit (0, steps - 1, (e.x - gridLeft()) / stepWidth);
    const auto value = laneValueAt (e.y);

    if (laneScaleBase >= 0.0f) // round 190lx (DE-31): Ctrl scales every column (bipolar lanes about their centre)
    {
        const auto level = graphLevel;
        const auto centre = notetools::graphIsBipolar (level) ? 0.5f : 0.0f;
        const auto from = laneScaleBase - centre, to = value - centre;
        const auto k = std::abs (from) > 1.0e-4f ? to / from : 0.0f;
        const auto offset = std::abs (from) > 1.0e-4f ? 0.0f : to - from; // (a column at the centre: they move with it)
        engine.editSong ([=, items = laneScaleFrom] (Song& s) {
            auto& p = s.activePattern();
            for (const auto& [i, v0] : items)
                if (i < p.notes.size())
                    notetools::setGraphValue (p.notes[i], level, centre + (v0 - centre) * k + offset, ticksPerStep);
        });
        return;
    }

    if (e.mods.isRightButtonDown()) // a straight ramp from the press to here
    {
        const auto from = juce::jmin (laneDownStep, step), to = juce::jmax (laneDownStep, step);
        const auto channel = graphRow();
        const auto level = (notetools::Level) graphLevel;
        const auto v0 = laneDownStep <= step ? laneDownValue : value, v1 = laneDownStep <= step ? value : laneDownValue;
        engine.editSong ([=] (Song& s) {
            auto& p = s.activePattern();
            for (int st = from; st <= to; ++st)
            {
                const auto t = to > from ? (float) (st - from) / (float) (to - from) : 0.0f;
                for (auto i : notesInStep (p, channel, st))
                    notetools::setLevelValue (p.notes[(std::size_t) i], level, v0 + (v1 - v0) * t);
            }
            if (level == notetools::Level::shift)
                p.sortNotes();
        });
        return;
    }

    setLaneValue (step, value);
}

void ChannelRackComponent::setPianoRollView (bool on)
{
    if (pianoRollView == on)
        return;
    pianoRollView = on;
    repaint();

    if (onFilterChanged) // the title's switches follow
        onFilterChanged();
}

void ChannelRackComponent::toggleGraphLanes()
{
    graphLanes = ! graphLanes;
    updateSize();
    repaint();

    if (onFilterChanged) // the title's graph switch follows
        onFilterChanged();
}

// --- fills, rotation and organisation ------------------------------------------------------------

void ChannelRackComponent::rotateSelected (int steps)
{
    const auto rows = engine.getSelectedChannels();
    engine.editSong ([rows, steps] (Song& s) {
        auto& p = s.activePattern();
        const auto length = patternSteps (s, p);
        for (auto r : rows)
            rotateSteps (p, r, steps, length);
    });
}

void ChannelRackComponent::copySelected()
{
    const auto& song = engine.getSong();
    stepClipboard = copyChannelSteps (song.activePattern(), engine.getSelectedChannels(), (int) song.channels.size());
    if (windowclip::enabled() && ! stepClipboard.empty()) // (issue #232, round 190nz: and to another LYNX SHARE window)
    {
        crossclipboard::Payload copy;
        copy.kind = crossclipboard::Kind::steps;
        copy.steps = stepClipboard;
        windowclip::publish (std::move (copy));
    }
}

void ChannelRackComponent::copyChannels()
{
    const auto rows = engine.getSelectedChannels();
    if (rows.empty())
        return;
    auto fragment = crosspaste::channelsFragment (engine.getSong(), rows);
    auto sorted = rows;
    std::sort (sorted.begin(), sorted.end());
    sorted.erase (std::unique (sorted.begin(), sorted.end()), sorted.end());
    for (std::size_t i = 0; i < sorted.size() && i < fragment.channels.size(); ++i)
        windowclip::captureChannel (engine, sorted[i], fragment.channels[i]); // (the plugins' settings as they are now)
    copiedChannels = fragment;
    crossclipboard::Payload copy;
    copy.kind = crossclipboard::Kind::channels;
    copy.fragment = std::move (fragment);
    windowclip::publish (std::move (copy));
    ui::postHint ({ copiedChannels.channels.size() > 1 ? tr ("Copied {0} channels", { juce::String ((int) copiedChannels.channels.size()) })
                                                       : tr ("Copied the channel") },
                  2000);
}

bool ChannelRackComponent::canPasteChannels()
{
    return ! copiedChannels.channels.empty() || windowclip::hasIncoming (crossclipboard::Kind::channels);
}

void ChannelRackComponent::pasteChannels()
{
    const auto incoming = windowclip::incoming (crossclipboard::Kind::channels);
    const auto& fragment = incoming ? incoming->fragment : copiedChannels;
    if (fragment.channels.empty())
        return;
    const auto rows = engine.getSelectedChannels();
    const auto at = rows.empty() ? (int) engine.getSong().channels.size() : *std::max_element (rows.begin(), rows.end()) + 1;
    std::vector<int> added;
    engine.editSong ([&] (Song& s) { added = crosspaste::importChannels (s, fragment, at); });
    if (! added.empty())
        selectRows (added, added.front());
    if (incoming)
        ui::postHint ({ windowclip::pastedFrom (*incoming) }, 2500);
}

void ChannelRackComponent::cutSelected()
{
    copySelected();
    const auto rows = engine.getSelectedChannels();
    const auto& song = engine.getSong();
    auto copy = song.activePattern();
    if (clearChannelSteps (copy, rows)) // (nothing to cut: no undo step)
        engine.editSong ([rows] (Song& s) { clearChannelSteps (s.activePattern(), rows); });
}

void ChannelRackComponent::pasteSelected()
{
    // issue #232 (round 190nz): steps copied in another LYNX SHARE window
    if (auto incoming = windowclip::incoming (crossclipboard::Kind::steps); incoming && ! incoming->steps.empty())
        stepClipboard = std::move (incoming->steps);
    if (stepClipboard.empty() || engine.getSelectedChannels().empty())
        return;
    engine.editSong ([rows = engine.getSelectedChannels(), clip = stepClipboard] (Song& s) {
        pasteChannelSteps (s.activePattern(), clip, rows, (int) s.channels.size());
    });
}

void ChannelRackComponent::fillSelected (int every)
{
    const auto rows = engine.getSelectedChannels();
    engine.editSong ([rows, every] (Song& s) {
        auto& p = s.activePattern();
        const auto length = patternSteps (s, p);
        for (auto r : rows)
            if (r < (int) s.channels.size()) // (audio clip channels too: they show their steps, round 190io)
                fillEverySteps (p, r, every, length);
    });
}

void ChannelRackComponent::sortChannels (ChannelSort by)
{
    engine.editSong ([by] (Song& s) { permuteChannels (s, channelSortOrder (s, by)); });
    openEditorRow = -1;
    selectRows ({ 0 }, 0);
}

void ChannelRackComponent::selectUnused()
{
    const auto unused = unusedChannels (engine.getSong());

    if (unused.empty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Select unused channels"),
                                                tr ("Every channel is used by a pattern or a Playlist clip."));
        return;
    }

    selectRows (unused, unused.front());
}

int ChannelRackComponent::addChannel (ChannelDef c, bool openEditor, int insert)
{
    int index = 0;
    if (filterGroup >= 0 && c.group < 0) // a new channel joins the group shown (FL)
        c.group = filterGroup;
    // round 190ok (D2): Settings › Audio › "Smart disable for new instrument plugins" (LYNX extra; FL's default: off)
    if (c.instrument == InstrumentKind::plugin && flsettings::options().smartDisableNewPlugins.load())
        c.setSmartDisable (true);
    engine.editSong ([&] (Song& s) { index = addChannelRouted (s, c, insert); });
    selectionAnchor = index;
    selectRows ({ index }, index);

    if (openEditor && c.instrument == InstrumentKind::plugin)
    {
        openPluginWindow (index);
        openEditorRow = index;
    }
    else if (openEditor && natives::instrumentEngine (c.instrument) != nullptr) // an FL engine: its editor (round 142)
        openChannelSettings (engine, index);

    return index;
}

bool ChannelRackComponent::loadSampleIntoSelected (const juce::File& file)
{
    const auto row = engine.getSelectedChannel();
    const auto& song = engine.getSong();

    if (row < 0 || row >= (int) song.channels.size() || ! SampleLibrary::isSampleFile (file))
        return false;

    const auto kind = song.channels[(std::size_t) row].instrument;

    // only channels that play a sample (or the built-in drums, which become samplers) take a file
    if (kind != InstrumentKind::sampler && kind != InstrumentKind::kick && kind != InstrumentKind::snare && kind != InstrumentKind::hat
        && kind != InstrumentKind::clap && kind != InstrumentKind::granulizer && kind != InstrumentKind::wavetraveller)
        return false;

    assignSample (row, file);
    return true;
}

namespace
{
// Euclidean fill: pulses spread evenly over a length, rotated; previewed live on the pattern.
class EuclideanPanel final : public juce::Component
{
public:
    EuclideanPanel (AudioEngine& e, int c) : engine (e), channel (c)
    {
        const auto& song = engine.getSong();
        patternIndex = song.currentPattern;
        originals = song.patterns[(std::size_t) patternIndex].notes;
        steps = patternSteps (song, song.patterns[(std::size_t) patternIndex]);

        auto setup = [this] (juce::Slider& s, juce::Label& l, const juce::String& name, double lo, double hi, double v) {
            l.setText (name, juce::dontSendNotification);
            addAndMakeVisible (l);
            s.setSliderStyle (juce::Slider::LinearHorizontal);
            s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 20);
            s.setRange (lo, hi, 1.0);
            s.setValue (v, juce::dontSendNotification);
            s.onValueChange = [this] { preview(); };
            addAndMakeVisible (s);
        };
        setup (pulses, pulsesLabel, tr ("Pulses"), 0, 32, 5);
        setup (length, lengthLabel, tr ("Length (steps)"), 1, 32, 16);
        setup (rotation, rotationLabel, tr ("Rotation"), -16, 16, 0);
        title.setText (tr ("Euclidean fill: {0}", { juce::String::fromUTF8 (song.channels[(std::size_t) channel].name.c_str()) }), juce::dontSendNotification);
        ui::dialog::styleTitle (title); // (round 190oo, WG-6: the one dialog title row, centred, 44 px)
        addAndMakeVisible (title);

        for (auto* b : { &accept, &cancel })
            addAndMakeVisible (b);
        ui::dialog::styleButton (accept, true); // (WG-6: Cancel | Accept, the one accent button at the far right)
        accept.onClick = [this] {
            restore();
            const auto rhythm = current();
            engine.editSong ([this, rhythm] (Song& s) { setStepRhythm (s.patterns[(std::size_t) patternIndex], channel, rhythm, steps); });
            done = true;
            closeToolWindowOf (*this);
        };
        cancel.onClick = [this] { closeToolWindowOf (*this); };
        setSize (380, 330 + titleGrowth);
        preview();
    }

    ~EuclideanPanel() override
    {
        if (! done)
            restore();
    }

    void paint (juce::Graphics& g) override
    {
        ui::dialog::paintFrame (g, getLocalBounds().toFloat(), ui::dialog::Kind::window); // (round 190oo, WG-6)
        // the rhythm as a ring of steps, the first step at the top
        const auto rhythm = current();
        const auto ring = juce::Rectangle<float> (0, (float) (150 + titleGrowth), (float) getWidth(), 120).withSizeKeepingCentre (110, 110);
        const auto n = (int) rhythm.size();

        for (int i = 0; i < n; ++i)
        {
            const auto angle = juce::MathConstants<float>::twoPi * (float) i / (float) n;
            const auto centre = ring.getCentre() + juce::Point<float> (std::sin (angle), -std::cos (angle)) * 50.0f;
            const auto dot = juce::Rectangle<float> (12, 12).withCentre (centre);
            g.setColour (rhythm[(std::size_t) i] ? theme::colour::stepOn : theme::colour::stepOff);
            g.fillEllipse (dot);
            if (i == 0)
            {
                g.setColour (theme::colour::textPrimary);
                g.drawEllipse (dot.expanded (2.0f), 1.0f);
            }
        }
    }

    void resized() override
    {
        title.setBounds (ui::dialog::titleRow (getLocalBounds()));
        auto area = ui::dialog::bodyOf (getLocalBounds()).reduced (12, 10);
        for (auto [s, l] : { std::pair { &pulses, &pulsesLabel }, std::pair { &length, &lengthLabel }, std::pair { &rotation, &rotationLabel } })
        {
            auto row = area.removeFromTop (30);
            l->setBounds (row.removeFromLeft (120));
            s->setBounds (row);
        }
        auto buttons = area.removeFromBottom (28);
        accept.setBounds (buttons.removeFromRight (100));
        buttons.removeFromRight (8);
        cancel.setBounds (buttons.removeFromRight (90));
    }

private:
    std::vector<bool> current() const
    {
        return euclideanRhythm ((int) pulses.getValue(), (int) length.getValue(), (int) rotation.getValue());
    }

    void restore()
    {
        if (patternIndex < (int) engine.getSong().patterns.size())
            engine.editSong ([this] (Song& s) { s.patterns[(std::size_t) patternIndex].notes = originals; }, false);
    }

    void preview()
    {
        const auto rhythm = current();
        if (patternIndex < (int) engine.getSong().patterns.size())
            engine.editSong ([this, rhythm] (Song& s) {
                auto& p = s.patterns[(std::size_t) patternIndex];
                p.notes = originals;
                setStepRhythm (p, channel, rhythm, steps);
            }, false);
        repaint();
    }

    // the title row (WG-6): the rows start 15 px lower than under the old 24 px title line
    static constexpr int titleGrowth = ui::dialog::titleRowHeight + 1 + 10 - 40;

    AudioEngine& engine;
    int channel, patternIndex = 0, steps = 16;
    std::vector<Note> originals;
    bool done = false;
    juce::Label title, pulsesLabel, lengthLabel, rotationLabel;
    juce::Slider pulses, length, rotation;
    juce::TextButton accept { tr ("Accept") }, cancel { tr ("Cancel") };
};
} // namespace

std::unique_ptr<juce::Component> ChannelRackComponent::makeEuclideanPanel (int channel)
{
    return std::make_unique<EuclideanPanel> (engine, juce::jlimit (0, juce::jmax (0, (int) engine.getSong().channels.size() - 1), channel));
}

void ChannelRackComponent::showEuclideanFill (int channel)
{
    if (channel >= 0 && channel < (int) engine.getSong().channels.size())
        openToolWindow (makeEuclideanPanel (channel), tr ("Euclidean fill"), "euclidean");
}

// --- mouse -------------------------------------------------------------------------------------

void ChannelRackComponent::mouseDown (const juce::MouseEvent& e)
{
    const AudioEngine::UndoGesture undoGesture (engine, AudioEngine::UndoGesture::begin); // (round 190jv: one undo step per gesture)
    int row = 0;
    const auto area = areaAt (e.x, e.y, row);
    const auto& song = engine.getSong();

    switch (area)
    {
        case Area::none:
            return;

        case Area::autoName: // round 190oo (PAT-7): FL's automation clip channel; its button opens the clip
        case Area::autoRow:
            if (flmods::popup (e.mods))
                showAutomationMenu (row);
            else if (area == Area::autoName && e.mods.isLeftButtonDown())
            {
                // round 190ow: the button selects the row (as a channel's does) and opens the clip on release; a drag moves it
                if (! automationSelected (row) || autoSelected.size() > 1)
                    clickAutomationSelector (row, {});
                autoNameDown = row;
                autoNameDragged = false;
                nameDragY = e.position.y;
            }
            return;

        case Area::autoMute: // round 190ow: FL's mute light of the automation clip channel
            if (! flmods::popup (e.mods))
                toggleAutomationMute (row);
            return;

        case Area::autoSelect: // round 190ow: the row in the rack's selection
            clickAutomationSelector (row, e.mods);
            return;

        case Area::autoMin: // round 190ow: FL's MIN / MAX knobs
        case Area::autoMax:
        {
            const auto spec = specFor (area, row);
            if (flmods::popup (e.mods))
            {
                juce::PopupMenu menu;
                menu.addSectionHeader (spec.name + ": " + spec.text (valueOf (area, row)));
                ui::addValueItems (menu, *this, &engine, spec, valueOf (area, row), [this, area, row] (double v) { setValue (area, row, v); });
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
                return;
            }
            if (ui::isResetClick (e))
            {
                setValue (area, row, spec.defaultValue);
                showTip (area, row, true);
                return;
            }
            knobDrag = area;
            knobRow = row;
            drag.begin (e, valueOf (area, row));
            showTip (area, row, false);
            repaint();
            return;
        }

        case Area::autoStep: // round 190ow (DE-123): a step that starts the clip
        {
            const auto step = (e.x - gridLeft()) / stepWidth;
            const auto& pattern = song.activePattern();
            if (e.mods.isRightButtonDown())
                paintMode = PaintMode::remove;
            else
                paintMode = pattern.hasAutoStep (row, step) && ! flsettings::options().bothButtonsSteps.load() ? PaintMode::remove : PaintMode::add;
            autoPaintRow = row;
            lastPaintedRow = lastPaintedStep = -1;
            applyAutoPaint (e);
            return;
        }

        case Area::zipped: // round 190mk (FL's manual: "Right-Click the button of a zipped Channel to unzip it")
            if (flmods::popup (e.mods))
                unzip (row);
            else
            {
                selectionAnchor = row;
                selectRows ({ row }, row);
            }
            return;

        case Area::addRow:
            showAddChannelMenu();
            return;

        case Area::loop: // the advanced looping control: drag sets the loop point in steps, right-click its menu
        {
            if (flmods::popup (e.mods))
            {
                showLoopPointMenu (row);
                return;
            }
            const auto& p = song.activePattern();
            const auto setting = channelloops::effectiveSetting (p, row);
            const auto steps = loopStepsOf (row);
            loopDragRow = row;
            loopDragStart = setting > 0 ? setting : steps > 0 ? steps : numSteps();
            loopDragY = e.position.y;
            repaint();
            return;
        }

        case Area::laneLevels:
            for (int tab = 0; tab < notetools::stepLevelCount; ++tab)
                if (levelButtonBounds (tab).contains (e.getPosition().translated (-stickyX, 0)))
                {
                    graphLevel = (int) notetools::stepLevel (tab);
                    repaint();
                }
            return;

        case Area::lane:
        {
            const auto step = juce::jlimit (0, numSteps() - 1, (e.x - gridLeft()) / stepWidth);

            if (e.mods.isAltDown() || e.mods.isMiddleButtonDown())
            {
                setLaneValue (step, notetools::graphDefault (graphLevel));
                return;
            }

            laneDownStep = step;
            laneDownValue = laneValueAt (e.y);
            laneScaleFrom.clear();
            laneScaleBase = -1.0f;
            if (e.mods.isCommandDown() && e.mods.isLeftButtonDown()) // round 190lx (DE-31)
            {
                const auto& p = engine.getSong().activePattern();
                const auto level = graphLevel;
                for (int st = 0; st < numSteps(); ++st)
                    for (auto i : notesInStep (p, graphRow(), st))
                    {
                        const auto v = notetools::graphValue (p.notes[(std::size_t) i], level, ticksPerStep);
                        if (st == step && laneScaleBase < 0.0f)
                            laneScaleBase = v;
                        laneScaleFrom.push_back ({ (std::size_t) i, v });
                    }
                if (laneScaleBase < 0.0f) // (an empty column: nothing to scale from)
                    laneScaleFrom.clear();
                else
                    return; // (the drag scales)
            }
            applyLane (e);
            return;
        }

        case Area::mute:
            if (flmods::popup (e.mods) || flmods::ctrl (e.mods))
                solo (row);
            else if (e.mods.isShiftDown()) // round 190ih (DE-11), FL's manual: "Lock state - (Shift+Click) a Mute icon"
                toggleSoloLock (row);
            else if (e.mods.isAltDown()) // round 190mv (DE-12): FL's Alt+click = the solo within the channel's filter group
                engine.editSong ([row] (Song& s) { noteedit::soloChannelInGroup (s, row); });
            else
                toggleMute (row);
            return;

        case Area::pan:
        case Area::volume:
        case Area::route:
        {
            const auto spec = specFor (area, row);

            if (flmods::popup (e.mods))
            {
                juce::PopupMenu menu;
                menu.addSectionHeader (spec.name + ": " + spec.text (valueOf (area, row)));
                ui::addValueItems (menu, *this, &engine, spec, valueOf (area, row), [this, area, row] (double v) { setValue (area, row, v); });

                if (area == Area::route)
                {
                    menu.addSeparator();
                    menu.addItem (tr ("Next free insert"), [this, row] {
                        engine.editSong ([row] (Song& s) { s.channels[(std::size_t) row].mixerTrack = s.claimFreeInsert(); });
                    });
                    menu.addItem (tr ("Send the selected channels to free inserts") + "\tCtrl+L", [this] { sendSelectedToInserts(); });

                    if (onOpenMixerInsert)
                        menu.addItem (tr ("Show this insert in the Mixer"), [this, row] {
                            if (row < (int) engine.getSong().channels.size())
                                onOpenMixerInsert (engine.getSong().channels[(std::size_t) row].mixerTrack);
                        });
                }

                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
                return;
            }

            if (ui::isResetClick (e))
            {
                setValue (area, row, spec.defaultValue);
                showTip (area, row, true);
                return;
            }

            knobDrag = area;
            knobRow = row;
            drag.begin (e, valueOf (area, row));
            showTip (area, row, false);
            repaint();
            return;
        }

        case Area::name:
            if (flmods::popup (e.mods))
            {
                if (! std::binary_search (engine.getSelectedChannels().begin(), engine.getSelectedChannels().end(), row))
                    clickSelector (row, {});
                else
                    selectRows (engine.getSelectedChannels(), row);
                showChannelMenu (row);
                return;
            }

            if (e.mods.isShiftDown())
            {
                clickSelector (row, e.mods);
                return;
            }

            if (! std::binary_search (engine.getSelectedChannels().begin(), engine.getSelectedChannels().end(), row)
                || engine.getSelectedChannels().size() > 1)
            {
                selectionAnchor = row;
                selectRows ({ row }, row);
            }

            if (flmods::ctrl (e.mods)) // audition without opening anything
            {
                previews.press (row, 60, 0.8f); // stops on release
                return;
            }

            // the editor opens on release, unless the button is dragged (that reorders the channels)
            nameDownRow = row;
            nameDragged = false;
            nameDownAlt = e.mods.isAltDown();
            nameDragY = e.position.y;
            return;

        case Area::select:
            clickSelector (row, e.mods);

            if (flmods::popup (e.mods)) // right-drag adds the rows it crosses
            {
                selectDownRow = row;
                selectDragAdds = true;
                selectDragBase = engine.getSelectedChannels();
            }
            else if (! e.mods.isShiftDown() && ! flmods::ctrl (e.mods)) // left-drag selects a range
            {
                selectDownRow = row;
                selectDragAdds = false;
            }
            return;

        case Area::grid:
        {
            auto step = (e.x - gridLeft()) / stepWidth;
            if (const auto loop = loopStepsOf (row); loop > 0 && step >= loop && step < numSteps())
                step %= loop; // a loop repeat edits the step it repeats
            const auto& pattern = song.activePattern();

            // Freeze (round 190ay): a frozen channel's steps cannot be edited; the miniature still opens the Piano Roll (read-only)
            if (! showsNotePreview (pattern, row, pianoRollView) && refuseFrozenEdit (row))
                return;
            if (showsNotePreview (pattern, row, pianoRollView)) // the miniature opens the Piano Roll (steps would break the melody)
            {
                // round 190mk (DE-33, FL's manual "Channel Rack › Piano roll preview"): "Left-click the preview to show/hide
                // the Piano roll window"; "Right-Click to open the Piano roll centered on the notes clicked"
                if (previewMuteButton && ! flmods::popup (e.mods) && previewMuteBounds (row).contains (e.getPosition()))
                {
                    toggleMute (row); // (FL: the preview's mute button; the preview stays)
                    repaint (rowBounds (row));
                    return;
                }
                const auto wasPrimary = engine.getSelectedChannel() == row;
                selectionAnchor = row;
                selectRows ({ row }, row);
                if (flmods::popup (e.mods))
                {
                    const auto span = previewSpan();
                    const auto at = juce::jlimit (0.0, 1.0, (double) (e.x - gridLeft() - span.x) / (double) juce::jmax (1, span.width));
                    if (onPreviewRightClick)
                        onPreviewRightClick (row, at * (double) numSteps() * (double) ticksPerStep);
                }
                else if (onPreviewClick)
                    onPreviewClick (wasPrimary ? row : -1); // (-1: another channel's preview: show its notes, never hide)
                else if (onOpenPianoRoll)
                    onOpenPianoRoll();
                return;
            }

            // FL's General › Input › "Use both mouse buttons in step sequencer" (round 190mr, DE-16 / owner E2; off as in FL,
            // capture 190mr A1.1): on, the left button only switches steps on, the right one off; off, the left toggles
            if (e.mods.isRightButtonDown())
                paintMode = PaintMode::remove;
            else
                paintMode = pattern.hasStep (row, step) && ! flsettings::options().bothButtonsSteps.load() ? PaintMode::remove : PaintMode::add;

            lastPaintedRow = lastPaintedStep = -1;
            applyPaint (e);
            return;
        }
    }
}

void ChannelRackComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (loopDragRow >= 0)
    {
        const auto steps = juce::jlimit (1, numSteps(), loopDragStart + juce::roundToInt ((loopDragY - e.position.y) / 6.0f));
        const auto& p = engine.getSong().activePattern();
        if (channelloops::storedSetting (p, loopDragRow) != steps)
            setLoopPoint (loopDragRow, steps);
        return;
    }

    if (knobDrag != Area::none && drag.isActive())
    {
        setValue (knobDrag, knobRow, drag.update (e, specFor (knobDrag, knobRow)));
        showTip (knobDrag, knobRow, false);
        return;
    }

    if (laneDownStep >= 0)
    {
        applyLane (e);
        hoverStep = juce::jlimit (0, numSteps() - 1, (e.x - gridLeft()) / stepWidth);
        hoverArea = Area::lane;
        const auto v = laneValueAt (e.y);
        const auto bipolar = notetools::graphIsBipolar (graphLevel);
        const auto shown = juce::roundToInt ((bipolar ? v * 2.0f - 1.0f : v) * 100.0f);
        // Note pitch, Shift and Repeat show FL's own values (a key, the part of a step, the repeat rate: issue #208)
        const auto own = notetools::levelText ((notetools::Level) graphLevel, v);
        auto ownText = tr (juce::String::fromUTF8 (own.c_str())); // ("No repeat"; FL's rate names keep their notation)
        for (const char* word : { TR_NOOP ("dotted"), TR_NOOP ("triplet"), TR_NOOP ("Beat"), TR_NOOP ("Half step"), TR_NOOP ("Quarter step") })
            ownText = ownText.replace (word, tr (word));
        ownText = ownText.replace ("(Step)", "(" + tr ("Step") + ")");
        const auto valueText = ! own.empty() ? ownText : (bipolar && shown > 0 ? "+" : "") + juce::String (shown) + "%";
        const auto levelText = tr (notetools::levelName ((notetools::Level) graphLevel));
        ui::showValueTip (*this, controlBounds (Area::lane, graphRow()), levelText + ": " + valueText);
        ui::Hint live;
        live.text = channelName (engine.getSong(), graphRow()) + " - " + levelText;
        live.value = valueText;
        ui::postHint (live);
        repaint (laneBounds());
        return;
    }

    if (autoPaintRow >= 0) // round 190ow: painting an automation row's steps
    {
        if (paintMode != PaintMode::none)
            applyAutoPaint (e);
        return;
    }

    if (autoNameDown >= 0) // round 190ow: an automation row's button dragged: the row moves past the channel rows it crosses
    {
        if (! autoNameDragged && std::abs (e.position.y - nameDragY) < 4.0f)
            return;
        autoNameDragged = true;
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        const auto& song = engine.getSong();
        if (autoNameDown >= (int) song.automations.size())
            return;
        const auto count = (int) song.channels.size();
        const auto top = automationRowBounds (autoNameDown).getY();
        const auto rowsAway = (int) std::floor ((e.position.y - (float) top) / (float) rowHeight + 0.5f);
        if (rowsAway == 0)
            return;
        const auto gap = rackauto::gapOf (song, song.automations[(std::size_t) autoNameDown]);
        const auto to = juce::jlimit (0, count, gap + (rowsAway < 0 ? -1 : 1));
        if (to != gap)
        {
            engine.editSong ([a = autoNameDown, rowsAway] (Song& s) { rackauto::moveRow (s, a, rowsAway); });
            updateSize();
            repaint();
        }
        return;
    }

    if (nameDownRow >= 0)
    {
        if (! nameDragged && std::abs (e.position.y - nameDragY) < 4.0f)
            return;

        nameDragged = true;
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        const auto count = (int) engine.getSong().channels.size();
        const auto target = juce::jlimit (0, count - 1, rowAtY (e.y));

        // move the selection one row at a time until the grabbed channel is under the mouse (round 190oo: passing an
        // automation clip row is a step too)
        for (int guard = 0; guard < count + (int) autoRows.size() && engine.getSelectedChannel() != target; ++guard)
            if (! moveSelected (target < engine.getSelectedChannel() ? -1 : 1))
                break;
        return;
    }

    if (selectDownRow >= 0)
    {
        const auto count = (int) engine.getSong().channels.size();
        const auto row = juce::jlimit (0, juce::jmax (0, count - 1), rowAtY (e.y));
        std::vector<int> rows;

        for (int r = juce::jmin (selectDownRow, row); r <= juce::jmax (selectDownRow, row); ++r)
            rows.push_back (r);

        if (selectDragAdds)
        {
            rows.insert (rows.end(), selectDragBase.begin(), selectDragBase.end());
            std::sort (rows.begin(), rows.end());
            rows.erase (std::unique (rows.begin(), rows.end()), rows.end());
        }
        else
            selectionAnchor = selectDownRow;

        if (rows != engine.getSelectedChannels())
            selectRows (rows, row);
        return;
    }

    if (paintMode != PaintMode::none)
        applyPaint (e);
}

void ChannelRackComponent::mouseUp (const juce::MouseEvent& e)
{
    const AudioEngine::UndoGesture undoGesture (engine, AudioEngine::UndoGesture::end); // (closes when mouseUp returns)
    previews.releaseAll(); // the audition (Ctrl+click) and the steps' previews stop with the button

    if (nameDownRow >= 0 && ! nameDragged && e.mouseWasClicked())
        toggleEditor (nameDownRow, nameDownAlt);

    if (nameDragged || autoNameDragged)
        setMouseCursor (juce::MouseCursor::NormalCursor);

    // round 190ow: an automation row's button opens its clip on release (as the Playlist's double-click does)
    if (autoNameDown >= 0 && ! autoNameDragged && e.mouseWasClicked() && autoNameDown < (int) engine.getSong().automations.size())
        openAutomationEditor (engine, autoNameDown);
    autoNameDown = -1;
    autoNameDragged = false;
    autoPaintRow = -1;

    nameDownRow = -1;
    nameDragged = false;
    if (loopDragRow >= 0)
    {
        loopDragRow = -1;
        repaint();
    }
    selectDownRow = -1;
    selectDragBase.clear();
    ui::hideValueTip();
    ui::clearPostedHint();
    paintMode = PaintMode::none;
    laneDownStep = -1;
    if (laneScaleBase >= 0.0f && graphLevel == notetools::graphShift) // (rollup step 11: a Ctrl-scaled Shift moved notes in their
        engine.editSong ([] (Song& s) { s.activePattern().sortNotes(); }); //  steps; sorted once the drag ends, as round 190ma's lane does)
    laneScaleBase = -1.0f; // (round 190lx)
    laneScaleFrom.clear();

    if (knobDrag != Area::none)
        repaint();

    knobDrag = Area::none;
    drag.end();
}

void ChannelRackComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    int row = 0;
    const auto area = areaAt (e.x, e.y, row);

    if (area == Area::pan || area == Area::volume || area == Area::autoMin || area == Area::autoMax) // (190ow: MIN / MAX)
    {
        setValue (area, row, specFor (area, row).defaultValue);
        showTip (area, row, true);
    }
    else if (area == Area::route)
    {
        if (onOpenMixerInsert)
            onOpenMixerInsert (engine.getSong().channels[(std::size_t) row].mixerTrack);
        else
            engine.editSong ([row] (Song& s) { s.channels[(std::size_t) row].mixerTrack = s.claimFreeInsert(); });
    }
    else if (area == Area::loop && ! flmods::popup (e.mods))
        setLoopPoint (row, ChannelLoop::nextBar);
    else if (area == Area::select && ! flmods::popup (e.mods))
        selectAll();
    // a double-click on a channel's name is two clicks, as in FL: its window opens and closes again (FL 2026, checked
    // round 165q F7). It no longer opens the Piano Roll — the hint promised that, but FL's channel button does not.
}

void ChannelRackComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    int row = 0;
    const auto area = areaAt (e.x, e.y, row);

    if (area == Area::pan || area == Area::volume || area == Area::route)
    {
        if (const auto v = ui::wheelValue (valueOf (area, row), e, wheel, specFor (area, row)))
        {
            setValue (area, row, *v);
            showTip (area, row, true);
        }
        return;
    }

    if (area == Area::loop) // the loop point, a step at a time
    {
        const auto& song = engine.getSong();
        const auto& p = song.activePattern();
        const auto setting = channelloops::effectiveSetting (p, row);
        const auto steps = setting > 0 ? setting : loopStepsOf (row) > 0 ? loopStepsOf (row) : numSteps();
        const auto delta = (wheel.deltaY != 0.0f ? wheel.deltaY : -wheel.deltaX) > 0.0f ? 1 : -1;
        setLoopPoint (row, juce::jlimit (1, numSteps(), steps + delta));
        return;
    }

    if ((area == Area::name || area == Area::select) && e.mods.isShiftDown()) // Shift+wheel moves the channel
    {
        const auto delta = (wheel.deltaY != 0.0f ? wheel.deltaY : -wheel.deltaX) > 0.0f ? -1 : 1;
        selectRows ({ row }, row);
        moveSelected (delta);
        return;
    }

    juce::Component::mouseWheelMove (e, wheel); // scroll the viewport
}

ui::Hint ChannelRackComponent::hintAt (juce::Point<int> p)
{
    int row = 0;
    const auto area = areaAt (p.x, p.y, row);
    const auto& song = engine.getSong();
    ui::Hint h;

    if (isAutomationArea (area)) // round 190oo (PAT-7): FL's automation clip channel
    {
        if (row >= 0 && row < (int) song.automations.size())
        {
            const auto& a = song.automations[(std::size_t) row];
            const auto name = a.name.empty() ? tr ("Automation clip") : juce::String::fromUTF8 (a.name.c_str());
            if (area == Area::autoMin || area == Area::autoMax) // round 190ow: FL's MIN / MAX knobs
                return ui::valueHint (specFor (area, row), valueOf (area, row), ui::valueGestures());
            if (area == Area::autoMute)
                h.text = name + " - " + tr ("Mute the automation clip: it drives nothing while muted");
            else if (area == Area::autoSelect)
                h.text = name + " - " + tr ("Select the automation clip row: [LMB] alone  [Ctrl+LMB] add");
            else if (area == Area::autoStep)
                h.text = name + " - " + tr ("Steps start the automation clip: [LMB] set or clear  [drag] paint");
            else
                h.text = name + " - " + tr ("Automation clip: [LMB] open it  [RMB] menu");
            h.value = a.muted ? tr ("Muted") : tr ("Automation clip"); // (FL names the channel's type on the right)
            h.hasMenu = area != Area::autoStep;
        }
        return h;
    }

    if (row >= 0 && row < (int) song.channels.size() && (area == Area::pan || area == Area::volume || area == Area::route))
    {
        const auto active = knobDrag != Area::none && drag.isActive();
        const auto shownArea = active ? knobDrag : area;
        const auto shownRow = active ? knobRow : row;
        h = ui::valueHint (specFor (shownArea, shownRow), valueOf (shownArea, shownRow),
                           shownArea == Area::route ? tr ("[LMB] drag or [Wheel] routes  [2xLMB] show in the Mixer  [RMB] next free insert")
                                                    : ui::valueGestures());
        return h;
    }

    const auto name = channelName (song, row);

    switch (area)
    {
        case Area::mute:
            h.text = name + " - " + tr ("Click: mute.  Ctrl+click or right-click: solo.  Alt+click: solo within its filter group.  Shift+click: lock the mute state");
            break;
        case Area::name:
            h.text = name + " - " + tr ("[LMB] open/close the editor  [Alt]+[LMB] keep others open  [Ctrl]+[LMB] audition  [LMB] drag up/down reorders  [RMB] channel menu");
            // sample channels: where the pitch is (round 163, "getting to the sample pitching")
            if (row >= 0 && row < (int) song.channels.size()
                && (song.channels[(std::size_t) row].instrument == InstrumentKind::sampler || song.channels[(std::size_t) row].instrument == InstrumentKind::audioClip))
                h.text << "  " << tr ("(pitch: [RMB] › Pitch...)");
            if (row >= 0 && row < (int) song.channels.size() && ! song.channels[(std::size_t) row].receiveFrom.empty())
                h.text << "  " << tr ("(locked: receives the notes of {0})", { juce::String::fromUTF8 (song.channels[(std::size_t) row].receiveFrom.c_str()) });
            if (engine.playsStandIn (row))
                h.text = name + " - " + tr ("Stand-in: its plugin is not installed on this PC, a render of this part plays (the plugin's settings are kept)");
            else if (engine.channelPluginOff (row)) // round 190ap
                h.text = name + " - " + tr ("Plugin switched off: the project was opened without third-party plugins (its settings are kept; File › Switch plugins back on...)");
            if (engine.channelDeactivated (row)) // round 190op (P4)
                h.text = name + " - " + tr ("Deactivated: its instrument is unloaded and it is silent (its settings are kept; [RMB] › Reactivate)");
            // Freeze (round 190ay): what a frozen channel is, and what to do when its audio needs attention
            if (row >= 0 && row < (int) song.channels.size() && song.channels[(std::size_t) row].freeze.on())
                h.text = name + " - " + frozenHint (row);
            h.hasMenu = true;
            // FL's hint bar names what the channel is ("Audio Clip", the plugin's name) on the right (REA round 143b)
            if (row >= 0 && row < (int) song.channels.size())
            {
                const auto& c = song.channels[(std::size_t) row];
                h.value = c.instrument == InstrumentKind::plugin && ! c.plugin.name.empty() ? juce::String::fromUTF8 (c.plugin.name.c_str())
                                                                                             : instrumentLabel (c.instrument);
            }
            break;
        case Area::select:
            h.text = name + " - " + tr ("Select: [LMB] one  [Ctrl]+[LMB] or [RMB] add/remove  [Shift]+[LMB] range  [LMB] drag range  [RMB] drag adds  [2xLMB] all  [Alt]+[Up]/[Down] moves the selection");
            break;
        case Area::grid:
            if (! song.patterns.empty() && showsNotePreview (song.activePattern(), row, pianoRollView))
                h.text = name + " - " + (previewMuteButton && previewMuteBounds (row).contains (p) ? tr ("Mute / unmute the channel")
                                                                                                     : tr ("Piano roll preview: [LMB] show / hide the Piano Roll  [RMB] open it centred on the notes clicked"));
            else
                h.text = name + " - " + (hoverStep >= numSteps() ? tr ("After the pattern's end: [LMB] adds a step and makes the pattern longer")
                                         : loopStepsOf (row) > 0 && hoverStep >= loopStepsOf (row)
                                             ? tr ("Loop repeat (after the loop point): [LMB] edits the step it repeats")
                                             : tr ("Steps: [LMB] drag draws (starting on a lit step erases)  [RMB] drag erases"));
            // and the step's position as bar:step:tick, like FL's hint bar
            if (hoverStep >= 0)
                h.value = juce::String (stepPositionText ((std::int64_t) hoverStep * ticksPerStep, song.meter()));
            break;
        case Area::addRow:
            h.text = tr ("Add a channel (or drop audio files on the rack)");
            break;
        case Area::zipped: // round 190mk: FL's hint is the channel's name, its type on the right
            h.text = name + " - " + tr ("Zipped channel: [RMB] unzip  (Alt+U unzips all)");
            if (row >= 0 && row < (int) song.channels.size())
            {
                const auto& c = song.channels[(std::size_t) row];
                h.value = c.instrument == InstrumentKind::plugin && ! c.plugin.name.empty() ? juce::String::fromUTF8 (c.plugin.name.c_str())
                                                                                             : instrumentLabel (c.instrument);
            }
            break;
        case Area::loop:
            h.text = name + " - " + tr ("Loop point: [LMB] drag or [Wheel] sets it in steps  [2xLMB] next bar  [RMB] Don't loop / Step / Beat / Bar, Burn to pattern");
            h.hasMenu = true;
            if (row >= 0 && row < (int) song.channels.size() && ! song.patterns.empty())
            {
                const auto setting = channelloops::effectiveSetting (song.activePattern(), row);
                h.value = setting == ChannelLoop::dontLoop ? tr ("Don't loop") : setting > 0 ? tr ("Every {0} steps", { juce::String (setting) })
                                                                                            : tr (channelloops::settingText (setting).c_str());
            }
            break;
        case Area::laneLevels:
            h.text = tr ("Graph lane: choose what the bars edit (Ctrl+K hides the lane)");
            break;
        case Area::lane:
            h.text = name + " - " + tr ("Graph lane: drag sets the steps, Ctrl+drag scales them all, right-drag draws a ramp, Alt+click resets");
            break;
        default:
            break;
    }

    return h;
}

void ChannelRackComponent::applyPaint (const juce::MouseEvent& e)
{
    const auto& song = engine.getSong();
    const auto row = rowAtY (e.y);
    auto step = (e.x - gridLeft()) / stepWidth;
    if (const auto loop = loopStepsOf (row); loop > 0 && step >= loop && step < numSteps())
        step %= loop; // a loop repeat edits the step it repeats

    if (laneBounds().contains (e.getPosition()) || (advancedLoopShown() && e.x >= loopBoxBounds (0).getX() - 3))
        return;
    if (! autoRows.empty() && automationRowAt (e.y) >= 0) // (round 190oo: an automation clip row has no steps to draw)
        return;

    if (row < 0 || row >= (int) song.channels.size() || step < 0 || step >= shownSteps() || e.x < stickyX + gridLeft()
        || showsNotePreview (song.activePattern(), row, pianoRollView)) // drag-paint skips note rows
        return;

    if (row == lastPaintedRow && step == lastPaintedStep)
        return;

    lastPaintedRow = row;
    lastPaintedStep = step;
    const auto on = paintMode == PaintMode::add;

    // FL: setting a step is silent, stopped or playing (issue #11; Ctrl+click on the channel button auditions it)
    engine.editSong ([row, step, on, mute = muteRemovedSteps] (Song& s) {
        auto& p = s.activePattern();
        setStep (p, row, step, on, mute);
        // a ghost step of a pattern with a set length: the length grows to the bar holding it (automatic lengths
        // follow the notes by themselves)
        if (on)
            p.lengthTicks = lengthWithStep (p.lengthTicks, step, ticksPerStep, s.barTicks());
    });
}

// --- menus -------------------------------------------------------------------------------------

// Round 190in (details190if R8): the channel's items of FL's wrapper ▸ menu in the channel and plugin windows, done the
// way this rack's own channel menu does them
void ChannelRackComponent::fillWindowMenu (int channel, wrappermenu::Spec& spec)
{
    if (channel < 0 || channel >= (int) engine.getSong().channels.size())
        return;
    const juce::Component::SafePointer<ChannelRackComponent> safe (this);
    spec.pianoRoll = [safe, channel] {
        if (safe == nullptr || channel >= (int) safe->engine.getSong().channels.size())
            return;
        safe->selectionAnchor = channel;
        safe->selectRows ({ channel }, channel);
        if (safe->onOpenPianoRoll)
            safe->onOpenPianoRoll();
    };
    spec.replace = [safe, channel] (juce::PopupMenu& m) {
        if (safe != nullptr && channel < (int) safe->engine.getSong().channels.size())
            m = safe->replaceMenu (channel);
    };
    if (engine.getSong().channels.size() > 1)
        spec.remove = [safe, channel] {
            if (safe == nullptr || channel >= (int) safe->engine.getSong().channels.size() || safe->engine.getSong().channels.size() < 2)
                return;
            safe->closeEditor (channel);
            safe->engine.editSong ([channel] (Song& s) { s.removeChannel (channel); });
            safe->openEditorRow = -1;
        };
    spec.rename = [safe, channel] {
        if (safe != nullptr && channel < (int) safe->engine.getSong().channels.size())
            safe->rename (channel);
    };
    // FL's Channel column (round 190ld, DF-118): "Save channel state as..." writes the channel as the project stores it
    // (ProjectFile::channelStateBytes, LYNX's .lynxchannel); "Browse channel states" opens one into this channel (its
    // mixer track stays). FL's own .fst states are read by the Browser.
    spec.saveChannelState = [safe, channel] {
        if (safe != nullptr && channel < (int) safe->engine.getSong().channels.size())
            safe->saveChannelState (channel);
    };
    spec.browseChannelStates = [safe, channel] {
        if (safe != nullptr && channel < (int) safe->engine.getSong().channels.size())
            safe->openChannelState (channel);
    };
    spec.changeColour = [safe, channel] (juce::PopupMenu& m) {
        if (safe == nullptr || channel >= (int) safe->engine.getSong().channels.size())
            return;
        m = ui::colourMenu (safe->engine.getSong().channels[(std::size_t) channel].colour, [safe, channel] (std::uint32_t colour) {
                if (safe != nullptr && channel < (int) safe->engine.getSong().channels.size()) // (issue #201: every selected channel)
                    safe->engine.editSong ([targets = selrename::targets (channel, safe->engine.getSelectedChannels()), colour] (Song& s) {
                        selrename::colourChannels (s, targets, colour);
                    });
            }, juce::Rectangle<int> (juce::Desktop::getMousePosition().x, juce::Desktop::getMousePosition().y, 1, 1));
    };
    spec.randomColour = [safe, channel] {
        if (safe == nullptr || channel >= (int) safe->engine.getSong().channels.size())
            return;
        const auto& palette = lynxPalette();
        const auto colour = palette[(std::size_t) juce::Random::getSystemRandom().nextInt ((int) palette.size())];
        safe->engine.editSong ([targets = selrename::targets (channel, safe->engine.getSelectedChannels()), colour] (Song& s) {
            selrename::colourChannels (s, targets, colour); // (issue #201)
        });
    };
    spec.changeIcon = [safe, channel] {
        if (safe == nullptr || channel >= (int) safe->engine.getSong().channels.size())
            return;
        ui::showTrackIconPicker (juce::Rectangle<int> (juce::Desktop::getMousePosition().x, juce::Desktop::getMousePosition().y, 1, 1),
                                 safe->engine.getSong().channels[(std::size_t) channel].icon, [safe, channel] (int icon) {
                                     if (safe != nullptr && channel < (int) safe->engine.getSong().channels.size())
                                         safe->engine.editSong ([channel, icon] (Song& s) { s.channels[(std::size_t) channel].icon = icon; });
                                 });
    };
}

// Replace › lists exactly what Insert › and "+" list (issue #18: one source, quickpick::addGeneratorMenu); the channel
// keeps its place, notes, insert, levels and colour (FL; core replaceGenerator). (Round 190in: also the wrapper menu's.)
juce::PopupMenu ChannelRackComponent::replaceMenu (int channel)
{
    const auto& c = engine.getSong().channels[(std::size_t) channel];
    juce::PopupMenu instruments;
    quickpick::addGeneratorMenu (instruments, engine, [this, channel] (ChannelDef generator) {
        if (channel >= (int) engine.getSong().channels.size())
            return;
        closeEditor (channel);
        const auto kind = generator.instrument;
        engine.editSong ([channel, &generator] (Song& s) { replaceGenerator (s, channel, std::move (generator)); });
        selectionAnchor = channel;
        selectRows ({ channel }, channel);
        openNewGenerator (channel, kind);
    }, [this, channel] { chooseSample (channel); }, &c, this);
    return instruments;
}

juce::PopupMenu ChannelRackComponent::buildChannelMenu (int channel)
{
    const auto& c = engine.getSong().channels[(std::size_t) channel];
    auto instruments = replaceMenu (channel);

    // FL's layout (REA round 104): two columns, the channel on the left (editors, name and colour, sample,
    // insert / replace / clone / delete) and its steps on the right (clipboard, fills, rotation); LYNX's own
    // items (settings windows, arpeggiator, organisation) follow in their place
    juce::PopupMenu menu;
    ui::addItemHeader (menu, tr ("Channel"), juce::String::fromUTF8 (c.name.c_str()), c.colour != 0 ? juce::Colour (c.colour) : juce::Colour());
    const auto selected = (int) engine.getSelectedChannels().size();
    const auto count = (int) engine.getSong().channels.size();

    menu.addItem (tr ("Piano Roll") + "\tF7", [this] {
        if (onOpenPianoRoll)
            onOpenPianoRoll();
    });
    // round 190ob (LYNX's own, beside FL's Piano roll: the other view of the same notes): the Tracker on this channel's
    // track (the channel is selected first when it is not, so it has a track even without notes)
    menu.addItem (tr ("Edit in tracker") + (actionKeyText ? actionKeyText ("view.tracker") : juce::String()), [this, channel] {
        if (channel >= (int) engine.getSong().channels.size())
            return;
        const auto& selected = engine.getSelectedChannels();
        if (std::find (selected.begin(), selected.end(), channel) == selected.end())
        {
            selectionAnchor = channel;
            selectRows ({ channel }, channel);
        }
        if (onOpenTracker)
            onOpenTracker (channel);
    });
    menu.addItem (tr ("Graph editor") + "\tCtrl+K", true, graphLanes, [this] { toggleGraphLanes(); });
    menu.addSeparator();
    menu.addItem (theme::mnemonic (tr ("Rename, color and icon..."), 'R') + "\tF2", [this, channel] { rename (channel); }); // (FL's text, round 190ng)
    menu.addSubMenu (tr ("Change color"), ui::colourMenu (c.colour, [this, channel] (std::uint32_t colour) {
                         if (channel < (int) engine.getSong().channels.size())
                             engine.editSong ([channel, colour] (Song& s) { s.channels[(std::size_t) channel].colour = colour; });
                     }, juce::Rectangle<int> (juce::Desktop::getMousePosition().x, juce::Desktop::getMousePosition().y, 1, 1)));
    menu.addItem (tr ("Random color"), [this, channel] {
        const auto& palette = lynxPalette();
        const auto colour = palette[(std::size_t) juce::Random::getSystemRandom().nextInt ((int) palette.size())];
        engine.editSong ([channel, colour] (Song& s) { s.channels[(std::size_t) channel].colour = colour; });
    });
    // FL's "Change icon" (SetPlugIconMenuClick, REA round 168h); LYNX's own icon set
    menu.addItem (tr ("Change icon..."), [this, channel] {
        if (channel >= (int) engine.getSong().channels.size())
            return;
        ui::showTrackIconPicker (juce::Rectangle<int> (juce::Desktop::getMousePosition().x, juce::Desktop::getMousePosition().y, 1, 1), engine.getSong().channels[(std::size_t) channel].icon, [this, channel] (int icon) {
            if (channel < (int) engine.getSong().channels.size())
                engine.editSong ([channel, icon] (Song& s) { s.channels[(std::size_t) channel].icon = icon; });
        });
    });
    // FL's "Lock active/mute state" (the mute switch's menu, round 167l): soloing another channel leaves this one alone
    menu.addItem (tr ("Lock active/mute state") + "\t" + tr ("Shift+click"), true, c.soloLock, [this, channel] { toggleSoloLock (channel); });
    menu.addSeparator();
    menu.addItem (theme::mnemonic (tr ("Load sample..."), 'L'), c.instrument != InstrumentKind::audioClip, false, [this, channel] { chooseSample (channel); });
    {
        // "Cut itself": a new note cuts the channel's own sounding note (its cut group and "cut by" are its own)
        // (issue #68: the same state as the sample panel's checkbox and the Misc page's "Cut self")
        const auto cutsItself = channelCutsItself (c);
        menu.addItem (theme::mnemonic (tr ("Cut itself"), 't'), true, cutsItself, [this, channel, cutsItself] {
            engine.editSong ([channel, cutsItself] (Song& s) {
                if (channel < (int) s.channels.size())
                    setChannelCutsItself (s.channels[(std::size_t) channel], channel, ! cutsItself);
            });
        });
    }
    menu.addSeparator();
    {
        juce::PopupMenu insert;
        addInstrumentItems (insert, channel);
        menu.addSubMenu (theme::mnemonic (tr ("Insert"), 'I'), insert);
    }
    menu.addSubMenu (tr ("Replace"), instruments);
    // FL's Patcherize (round 165m): the channel's instrument into a new Patcher, same sound
    menu.addItem (tr ("Patcherize"), selected <= 1 && patcherize::canPatcherizeChannel (engine.getSong(), channel), false, [this, channel] {
        engine.capturePluginStates(); // (the plugin's current settings go with it)
        closeEditor (channel);
        engine.editSong ([channel] (Song& s) { patcherize::patcherizeChannel (s, channel); });
    });
    // Freeze (round 190ay; LYNX's own, FL has none): the channel's sound rendered to audio that plays in its place while
    // its instrument / plugin is unloaded; the clicked channel, or every selected one when it is among them
    {
        const auto& song = engine.getSong();
        std::vector<std::uint64_t> targets;
        const auto& rows = engine.getSelectedChannels();
        if (selected > 1 && std::binary_search (rows.begin(), rows.end(), channel))
        {
            for (const auto r : rows)
                if (r >= 0 && r < count && song.channels[(std::size_t) r].freeze.on() == c.freeze.on())
                    targets.push_back (song.channels[(std::size_t) r].uid);
        }
        else
            targets.push_back (c.uid);
        const auto several = targets.size() > 1;
        if (! c.freeze.on())
            menu.addItem (several ? tr ("Freeze selected channels") : trc ("@channel: Freeze"), [this, targets] { freezeChannels (targets); });
        else
        {
            menu.addItem (several ? tr ("Unfreeze selected channels") : tr ("Unfreeze"), [this, targets] {
                for (const auto uid : targets)
                {
                    const auto& channels = engine.getSong().channels;
                    const auto it = std::find_if (channels.begin(), channels.end(), [uid] (const ChannelDef& d) { return d.uid == uid && uid != 0; });
                    if (it != channels.end())
                        unfreezeChannel (engine, (int) (it - channels.begin()));
                }
                repaint();
            });
            menu.addItem (several ? tr ("Re-freeze selected channels") : tr ("Re-freeze"), [this, targets] { freezeChannels (targets); });
        }
    }
    // round 190op (P4, LYNX extra; owner decision O-3): Deactivate unloads the instrument with nothing rendered (Freeze
    // renders); the clicked channel, or every selected one when it is among them
    if (layoutstyle::current() != layoutstyle::Style::pattern || trackstates::patternMenus() || c.deactivated)
    {
        std::vector<int> targets;
        const auto& rows = engine.getSelectedChannels();
        if (selected > 1 && std::binary_search (rows.begin(), rows.end(), channel))
        {
            for (const auto r : rows)
                if (r >= 0 && r < count && engine.getSong().channels[(std::size_t) r].deactivated == c.deactivated)
                    targets.push_back (r);
        }
        else
            targets.push_back (channel);
        const auto off = c.deactivated;
        menu.addItem (off ? tr ("Reactivate") : (targets.size() > 1 ? tr ("Deactivate selected channels") : tr ("Deactivate")), true, off,
                      [this, targets, off] {
                          engine.setDeactivated (targets, {}, ! off);
                          repaint();
                      });
    }
    // FL's Loop Starter channel menu (round 168b): Replace sample, Generate steps, Locked, One-shot category
    loopstarterui::addChannelMenu (menu, engine, channel);
    menu.addItem (selected > 1 ? theme::mnemonic (tr ("Clone {0} channels", { juce::String (selected) }), 'C') : theme::mnemonic (tr ("Clone"), 'C') + "\tAlt+C", [this] { cloneSelected(); });
    // issue #232 stage 2 (round 190nz, LYNX extra): the channels themselves, also to another LYNX SHARE window
    menu.addItem (selected > 1 ? tr ("Copy {0} channels", { juce::String (selected) }) : tr ("Copy channel"), [this] { copyChannels(); });
    menu.addItem (tr ("Paste channels"), canPasteChannels(), false, [this] { pasteChannels(); });

    if (selected > 1)
        menu.addItem (theme::mnemonic (tr ("Delete {0} channels", { juce::String (selected) }), 'D') + "\tAlt+Del", selected < count, false, [this] { deleteSelected(); });
    else
        menu.addItem (theme::mnemonic (tr ("Delete..."), 'D') + "\tAlt+Del", count > 1, false, [this, channel] {
            closeEditor (channel);
            engine.editSong ([channel] (Song& s) { s.removeChannel (channel); });
            openEditorRow = -1;
        });

    // FL's "Assign to new instrument track" after a line, the last item of the left column (LineAssignInstrTrackMenu,
    // round 190dx; ChannelMenuPopup FUN_011f4450: "Unassign from instrument track" while the channel has one; enabled
    // only for a generator that can have one)
    menu.addSeparator();
    if (const auto& song = engine.getSong(); trackmodes::instrumentTracksOf (song, channel).empty())
        menu.addItem (theme::mnemonic (tr ("Assign to new instrument track"), 'A'), trackmodes::canBeInstrumentTrack (song, channel), false, [this, channel] {
            int track = -1;
            engine.editSong ([channel, &track] (Song& s) { track = trackmodes::assignToNewInstrumentTrack (s, channel); });
            if (track < 0)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Can't create new instrument track"),
                                                        tr ("There are no available mixer tracks."));
        });
    else
        menu.addItem (theme::mnemonic (tr ("Unassign from instrument track"), 'U'), [this, channel] {
            engine.editSong ([channel] (Song& s) { trackmodes::unassignFromInstrumentTracks (s, channel); });
        });

    // LYNX: the channel's settings windows and its arpeggiator
    menu.addSeparator();
    if (c.instrument == InstrumentKind::sampler)
    {
        menu.addItem (tr ("Sampler settings (pitch, envelope, reverse)..."),
                      [this, channel] { openChannelSettings (engine, channel); });
        menu.addItem (tr ("Pitch... (semitones, cents, keep length)"), ! c.samplePath.empty() || ! c.zones.empty(), false,
                      [this, channel] { showSamplePitchDialog (engine, channel); });
        menu.addItem (tr ("Edit the sample in the audio editor..."), ! c.samplePath.empty(), false, [this, channel] { openAudioEditorForChannel (engine, channel); });

        if (c.sampler.slices.empty())
            menu.addItem (tr ("Slice sample (slicer): one slice per key and a pattern with the loop"), [this, channel] { sliceChannel (channel); });
        else
            menu.addItem (tr ("Undo slices ({0})", { juce::String ((int) c.sampler.slices.size()) }), [this, channel] {
                engine.editSong ([channel] (Song& s) { s.channels[(std::size_t) channel].sampler.slices.clear(); });
            });
        // Live's Simpler › Slice to Drum Rack (round 190ec): the channel becomes a Drum Rack, one pad per slice from C1
        menu.addItem (tr ("Slice to Drum Rack"), ! c.samplePath.empty() && c.zones.empty(), false, [this, channel] { sliceChannelToDrumRack (engine, channel); });
    }
    else if (c.instrument == InstrumentKind::audioClip)
        menu.addItem (tr ("Pitch... (semitones, cents, keep length)"), ! c.samplePath.empty(), false,
                      [this, channel] { showSamplePitchDialog (engine, channel); });
    else if (c.instrument == InstrumentKind::synth)
        menu.addItem (tr ("Synth settings (oscillators, envelope, filter)..."), [this, channel] { openChannelSettings (engine, channel); });
    else if (hasChannelSettings (c.instrument))
        menu.addItem (tr ("Channel settings..."), [this, channel] { openChannelSettings (engine, channel); });
    // FL's wrench tab of the channel window, for every channel (REA round 143b)
    menu.addItem (tr ("Miscellaneous functions (polyphony, cut groups, arpeggiator)..."), [this, channel] { openChannelMisc (engine, channel); });
    if (c.instrument == InstrumentKind::patcher) // its published parameters (round 171b)
        addPatcherParameterItems (menu, engine, c.patcher, { AutomationTargetKind::channelParam, channel, 0, 0 });

    if (c.instrument == InstrumentKind::plugin)
    {
        menu.addItem (tr ("Open plugin editor"), [this, channel] { openPluginWindow (channel); });
        // round 190ao: only while the plugin is down (it crashed, or its separate process died)
        if (engine.hostedPluginDown ({ channel, -1, -1 }))
            menu.addItem (tr ("Reload crashed plugin"), [this, channel] { PluginWindows::reload (engine, { { channel, -1, -1 } }); });

        auto* live = dynamic_cast<PluginInstrument*> (engine.getChannelInstrument (channel).get());
        menu.addSubMenu (tr ("Processing"), pluginProcessingMenu (engine, c.plugin, live != nullptr ? &live->getInstance() : nullptr,
                                                                [channel] (Song& song) -> PluginRef* {
                                                                    if (channel >= (int) song.channels.size())
                                                                        return nullptr;
                                                                    return &song.channels[(std::size_t) channel].plugin;
                                                                }));

        if (live != nullptr)
            addPluginParameterItems (menu, engine, live->getInstance(), { AutomationTargetKind::channelParam, channel, 0, 0 });
    }
    {
        // channel arpeggiator
        const auto arp = c.arp;
        auto set = [this, channel] (std::function<void (ArpSettings&)> change) {
            engine.editSong ([channel, change] (Song& s) { change (s.channels[(std::size_t) channel].arp); });
        };
        juce::PopupMenu arpMenu;
        const char* directions[] = { TR_NOOP ("Off"), TR_NOOP ("Up"), TR_NOOP ("Down"), TR_NOOP ("Up and down"), TR_NOOP ("Random") };

        for (int d = 0; d < 5; ++d)
            arpMenu.addItem (tr (directions[d]), true, arp.direction == d, [set, d] { set ([d] (ArpSettings& a) { a.direction = d; }); });

        arpMenu.addSeparator();
        juce::PopupMenu range, speed, gate, repeat;

        for (int o = 1; o <= 4; ++o)
            range.addItem (o == 1 ? tr ("1 octave") : tr ("{0} octaves", { juce::String (o) }), true, arp.range == o, [set, o] { set ([o] (ArpSettings& a) { a.range = o; }); });

        const std::pair<const char*, float> speeds[] = { { "1/4", 1.0f }, { "1/8", 0.5f }, { TR_NOOP ("1/8 triplet"), 1.0f / 3.0f },
                                                          { "1/16", 0.25f }, { TR_NOOP ("1/16 triplet"), 1.0f / 6.0f }, { "1/32", 0.125f } };
        for (const auto& [name, beats] : speeds)
            speed.addItem (tr (name), true, std::abs (arp.stepBeats - beats) < 1e-4f, [set, b = beats] { set ([b] (ArpSettings& a) { a.stepBeats = b; }); });

        for (int g : { 25, 50, 75, 100 })
            gate.addItem (juce::String (g) + "%", true, std::abs (arp.gate - g / 100.0f) < 1e-4f, [set, g] { set ([g] (ArpSettings& a) { a.gate = g / 100.0f; }); });

        for (int n : { 1, 2, 3, 4 })
            repeat.addItem (juce::String (n) + "x", true, arp.repeat == n, [set, n] { set ([n] (ArpSettings& a) { a.repeat = n; }); });

        arpMenu.addSubMenu (tr ("Range"), range);
        arpMenu.addSubMenu (tr ("Speed"), speed);
        arpMenu.addSubMenu (tr ("Gate"), gate);
        arpMenu.addSubMenu (tr ("Repeat"), repeat);
        menu.addSubMenu (arp.enabled() ? tr ("Arpeggiator (on)") : tr ("Arpeggiator"), arpMenu);
    }

    // --- second column: the channel's steps in the current pattern ---
    menu.addColumnBreak();
    // FL's Cut / Copy / Paste (REA round 170a §1): the selected channels' steps and notes, as Ctrl+X / C / V
    {
        const auto& song = engine.getSong();
        const auto& selection = engine.getSelectedChannels();
        const auto& notes = song.activePattern().notes;
        const auto hasNotes = std::any_of (notes.begin(), notes.end(), [&selection] (const Note& n) {
            return std::binary_search (selection.begin(), selection.end(), n.channel);
        });
        menu.addItem (theme::mnemonic (tr ("Cut"), 't') + "\tCtrl+X", hasNotes, false, [this] { cutSelected(); });
        menu.addItem (theme::mnemonic (tr ("Copy"), 'o') + "\tCtrl+C", hasNotes, false, [this] { copySelected(); });
        menu.addItem (theme::mnemonic (tr ("Paste"), 'p') + "\tCtrl+V", ! stepClipboard.empty() || windowclip::hasIncoming (crossclipboard::Kind::steps), false,
                      [this] { pasteSelected(); });
    }
    menu.addSeparator();
    menu.addItem (theme::mnemonic (tr ("Fill each 2 steps"), '2'), [this] { fillSelected (2); });
    menu.addItem (theme::mnemonic (tr ("Fill each 4 steps"), '4'), [this] { fillSelected (4); });
    menu.addItem (theme::mnemonic (tr ("Fill each 8 steps"), '8'), [this] { fillSelected (8); });
    menu.addItem (theme::mnemonic (tr ("Advanced fill..."), 'f'), [this, channel] { showEuclideanFill (channel); });
    menu.addSeparator();
    menu.addItem (tr ("Rotate left") + "\tShift+Ctrl+Left", [this] { rotateSelected (-1); });
    menu.addItem (tr ("Rotate right") + "\tShift+Ctrl+Right", [this] { rotateSelected (1); });
    menu.addSeparator();
    {
        // FL's "Receive notes from" (round 165f-rack): a MIDI input device (any or one MIDI channel) plays only this channel
        juce::PopupMenu receive;
        receive.addItem (theme::mnemonic (tr ("Unlock"), 'u'), ! c.receiveFrom.empty(), false, [this, channel] {
            engine.editSong ([channel] (Song& s) {
                s.channels[(std::size_t) channel].receiveFrom.clear();
                s.channels[(std::size_t) channel].receiveMidiChannel = 0;
            });
        });
        receive.addSeparator();
        auto lockTo = [this, channel] (std::string device, int midiChannel) {
            engine.editSong ([channel, device, midiChannel] (Song& s) {
                // one device + MIDI channel plays one channel: another channel that had it lets go
                for (auto& other : s.channels)
                    if (other.receiveFrom == device && other.receiveMidiChannel == midiChannel)
                        other.receiveFrom.clear();
                s.channels[(std::size_t) channel].receiveFrom = device;
                s.channels[(std::size_t) channel].receiveMidiChannel = midiChannel;
            });
        };
        const auto devices = juce::MidiInput::getAvailableDevices();
        for (const auto& d : devices)
        {
            const auto name = d.name.toStdString();
            juce::PopupMenu channels;
            channels.addItem (tr ("Any MIDI channel"), true, c.receiveFrom == name && c.receiveMidiChannel == 0, [lockTo, name] { lockTo (name, 0); });
            for (int m = 1; m <= 16; ++m)
                channels.addItem (tr ("MIDI channel {0}", { juce::String (m) }), true, c.receiveFrom == name && c.receiveMidiChannel == m,
                                  [lockTo, name, m] { lockTo (name, m); });
            receive.addSubMenu (d.name, channels, true, nullptr, c.receiveFrom == name);
        }
        // a device of the project that is not connected now stays listed
        if (! c.receiveFrom.empty() && std::none_of (devices.begin(), devices.end(), [&c] (const juce::MidiDeviceInfo& d) { return d.name.toStdString() == c.receiveFrom; }))
            receive.addItem (juce::String::fromUTF8 (c.receiveFrom.c_str()) + " (" + tr ("not connected") + ")", false, true, [] {});
        if (devices.isEmpty() && c.receiveFrom.empty())
            receive.addItem (tr ("No MIDI input device"), false, false, [] {});
        menu.addSubMenu (theme::mnemonic (c.receiveFrom.empty() ? tr ("Receive notes from") : tr ("Receive notes from") + " (" + tr ("locked") + ")", 'n'), receive);
    }
    {
        // FL's "Burn MIDI to" (round 165f-rack): what the channel plays (loops, swing, time, arpeggiator, echo) as notes
        juce::PopupMenu burn;
        burn.addItem (theme::mnemonic (tr ("Current pattern"), 'c'), [this, channel] { burnMidi (channel, false); });
        burn.addItem (theme::mnemonic (tr ("New pattern"), 'n'), [this, channel] { burnMidi (channel, true); });
        menu.addSubMenu (tr ("Burn MIDI to"), burn);
        // its loop point in this pattern (FL's advanced looping control menu)
        const auto& song = engine.getSong();
        const auto& p = song.activePattern();
        const auto setting = channelloops::effectiveSetting (p, channel);
        juce::PopupMenu loop;
        loop.addItem (tr ("Don't loop"), true, setting == ChannelLoop::dontLoop && p.loopOn, [this, channel] { setLoopPoint (channel, ChannelLoop::dontLoop); });
        loop.addItem (tr ("Step"), true, p.loopOn && setting == ChannelLoop::nextStep, [this, channel] { setLoopPoint (channel, ChannelLoop::nextStep); });
        loop.addItem (tr ("Beat"), true, p.loopOn && setting == ChannelLoop::nextBeat, [this, channel] { setLoopPoint (channel, ChannelLoop::nextBeat); });
        loop.addItem (tr ("Bar"), true, p.loopOn && setting == ChannelLoop::nextBar, [this, channel] { setLoopPoint (channel, ChannelLoop::nextBar); });
        loop.addSeparator();
        loop.addItem (tr ("Set to next beat"), [this, channel] { setLoopPoint (channel, 1000 + ChannelLoop::nextBeat); });
        loop.addItem (tr ("Set to next bar"), [this, channel] { setLoopPoint (channel, 1000 + ChannelLoop::nextBar); });
        loop.addSeparator();
        loop.addItem (tr ("Burn to pattern"), loopStepsOf (channel) > 0, false, [this, channel] {
            engine.editSong ([channel] (Song& s) { channelloops::burnChannel (s.activePattern(), channel, s.meter()); });
            repaint();
        });
        menu.addSubMenu (setting > 0 && p.loopOn ? tr ("Loop point ({0} steps)", { juce::String (setting) }) : tr ("Loop point"), loop);
    }
    // FL's "Create DirectWave instrument..." (round 165f-rack): the instrument sampled key by key into a Sampler channel
    menu.addItem (tr ("Create sampled instrument..."), c.instrument != InstrumentKind::audioClip && c.instrument != InstrumentKind::controller,
                  false, [this, channel] { showAutoSamplerDialog (engine, channel); });
    menu.addSeparator();
    menu.addItem (tr ("Swing mix ({0})...", { juce::String (juce::roundToInt (c.swingMix * 100.0f)) + "%" }),
                  [this, channel] { setSwingMixOfSelected (channel); });
    menu.addItem (tr ("Clear notes in this pattern"), [this, channel] {
        engine.editSong ([channel] (Song& s) {
            auto& notes = s.activePattern().notes;
            notes.erase (std::remove_if (notes.begin(), notes.end(), [channel] (const Note& n) { return n.channel == channel; }),
                         notes.end());
        });
    });

    // organisation (acts on the selection; FL keeps these in the rack's options menu)
    menu.addSeparator();
    menu.addItem (tr ("Move up") + "\tAlt+Up", engine.getSelectedChannels().front() > 0, false, [this] { moveSelected (-1); });
    menu.addItem (tr ("Move down") + "\tAlt+Down", engine.getSelectedChannels().back() < count - 1, false, [this] { moveSelected (1); });
    menu.addItem (tr ("Send to free mixer inserts") + "\tCtrl+L", [this] { sendSelectedToInserts(); });
    {
        juce::PopupMenu sort;
        sort.addItem (tr ("By name"), [this] { sortChannels (ChannelSort::name); });
        sort.addItem (tr ("By color"), [this] { sortChannels (ChannelSort::colour); });
        sort.addItem (tr ("By mixer insert"), [this] { sortChannels (ChannelSort::insert); });
        sort.addItem (tr ("By type"), [this] { sortChannels (ChannelSort::type); });
        menu.addSubMenu (tr ("Sort channels"), sort);
    }
    menu.addItem (tr ("Select unused channels"), [this] { selectUnused(); });
    menu.addItem (theme::mnemonic (tr ("Group selected..."), 'G') + "\tAlt+G", [this] { groupSelected(); });
    return menu;
}

void ChannelRackComponent::showChannelMenu (int channel)
{
    buildChannelMenu (channel).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

juce::String flEngineName (const natives::InstrumentEngine& e)
{
    const auto name = juce::String::fromUTF8 (e.name);
    if (! natives::available (e, flStudioAccess()))
        return tr ("{0} (requires FL Studio)", { name });
    return e.beta ? tr ("{0} (beta)", { name }) : name;
}

void ChannelRackComponent::openNewGenerator (int row, InstrumentKind kind)
{
    switch (kind)
    {
        case InstrumentKind::kick: case InstrumentKind::snare: case InstrumentKind::hat: case InstrumentKind::clap:
        case InstrumentKind::synth: case InstrumentKind::sampler: case InstrumentKind::audioClip:
            return; // (LYNX's drums and synth: no window, as before)
        case InstrumentKind::plugin:
            openPluginWindow (row); // the plugin opens when added
            openEditorRow = row;
            return;
        case InstrumentKind::patcher:
            PatcherWindows::openChannel (engine, row);
            return;
        default:
            openChannelSettings (engine, row);
            return;
    }
}

void ChannelRackComponent::addInstrumentItems (juce::PopupMenu& menu, int insertAt)
{
    // a new channel at the end (insertAt < 0) or at a row; the list is the one Replace › shows (issue #18)
    quickpick::addGeneratorMenu (menu, engine, [this, insertAt] (ChannelDef c) {
        int index = 0;
        if (filterGroup >= 0 && c.group < 0)
            c.group = filterGroup;
        const auto kind = c.instrument;
        engine.editSong ([&] (Song& s) {
            c.mixerTrack = s.claimFreeInsert();
            index = insertAt < 0 ? (s.channels.push_back (c), (int) s.channels.size() - 1) : insertChannel (s, insertAt, c);
        });
        selectionAnchor = index;
        selectRows ({ index }, index);
        openNewGenerator (index, kind);
    }, [this, insertAt] { chooseSample (-1, insertAt); }, nullptr, this);
}

void ChannelRackComponent::showAddChannelMenu (int insertAt)
{
    juce::PopupMenu menu;
    // FL's renamed plugins first (issue #11): "1_Synth" renamed in the Browser's Plugin database is added by typing 1
    std::vector<quickpick::Pick> picks;
    if (insertAt < 0)
        picks = quickpick::renamedGenerators (engine, [safe = juce::Component::SafePointer<ChannelRackComponent> (this)] (ChannelDef c) {
            if (safe != nullptr)
                safe->addChannel (std::move (c), true);
        });
    quickpick::addPicks (menu, picks);
    // the generators under FL's Plugin database categories and the user's folders (rounds 166f, 170b: addGeneratorMenu)
    addInstrumentItems (menu, insertAt);
    quickpick::addFolderViewItems (menu, engine); // FL: Categories / Simple / Tree at the bottom
    quickpick::showMenu (menu, juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), std::move (picks));
}

juce::File ChannelRackComponent::channelStateFolder()
{
    return platform::documentsFolder().getChildFile ("LYNX/Channel states");
}

void ChannelRackComponent::saveChannelState (int channel)
{
    const auto& c = engine.getSong().channels[(std::size_t) channel];
    const auto folder = channelStateFolder();
    folder.createDirectory();
    const auto name = juce::File::createLegalFileName (juce::String::fromUTF8 (c.name.c_str())).trim();
    fileChooser = lynx::ui::makeFileChooser (tr ("Save channel state as..."), folder.getChildFile ((name.isEmpty() ? juce::String ("Channel") : name) + ".lynxchannel"),
                                             "*.lynxchannel");
    lynx::ui::launchChooser (fileChooser, juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                              | juce::FileBrowserComponent::warnAboutOverwriting,
                             [this, channel] (const juce::FileChooser& chooser) {
                                 auto file = chooser.getResult();
                                 if (file == juce::File() || channel >= (int) engine.getSong().channels.size())
                                     return;
                                 file = file.withFileExtension ("lynxchannel");
                                 const auto bytes = ProjectFile::channelStateBytes (engine.getSong().channels[(std::size_t) channel]);
                                 if (! file.replaceWithData (bytes.data(), bytes.size()))
                                     juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Save channel state as..."),
                                                                             tr ("Could not write {0}", { file.getFullPathName() }));
                             });
}

void ChannelRackComponent::openChannelState (int channel)
{
    fileChooser = lynx::ui::makeFileChooser (tr ("Browse channel states"), channelStateFolder(), "*.lynxchannel");
    lynx::ui::launchChooser (fileChooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [this, channel] (const juce::FileChooser& chooser) {
                                 juce::MemoryBlock data;
                                 if (! chooser.getResult().loadFileAsData (data) || channel >= (int) engine.getSong().channels.size())
                                     return;
                                 const auto* p = static_cast<const std::uint8_t*> (data.getData());
                                 auto loaded = ProjectFile::channelFromStateBytes (std::vector<std::uint8_t> (p, p + data.getSize()));
                                 if (! loaded)
                                 {
                                     juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Browse channel states"),
                                                                             tr ("This file is not a channel state LYNX SHARE can read."));
                                     return;
                                 }
                                 closeEditor (channel);
                                 engine.editSongStructural ([channel, def = *loaded] (Song& s) mutable {
                                     if (channel >= (int) s.channels.size())
                                         return;
                                     auto& old = s.channels[(std::size_t) channel];
                                     def.mixerTrack = old.mixerTrack; // (the channel keeps its place in the mix and its filter group)
                                     def.group = old.group;
                                     old = std::move (def);
                                 });
                             });
}

void ChannelRackComponent::chooseSample (int channel, int insertAt)
{
    fileChooser = lynx::ui::makeFileChooser (tr ("Choose sample"), juce::File(), engine.getSampleLibrary().wildcard());
    lynx::ui::launchChooser (fileChooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, channel, insertAt] (const juce::FileChooser& chooser) {
                                  if (! chooser.getResult().existsAsFile())
                                      return;
                                  if (channel >= 0 || insertAt < 0)
                                  {
                                      assignSample (channel, chooser.getResult());
                                      return;
                                  }
                                  // Insert › Sampler (choose file)...: a new sampler channel at the row
                                  if (engine.getSampleLibrary().load (chooser.getResult().getFullPathName()) == nullptr)
                                      return assignSample (channel, chooser.getResult()); // (its "could not read" message)
                                  ChannelDef c;
                                  c.instrument = InstrumentKind::sampler;
                                  c.samplePath = chooser.getResult().getFullPathName().toStdString();
                                  c.name = chooser.getResult().getFileNameWithoutExtension().toStdString();
                                  if (filterGroup >= 0)
                                      c.group = filterGroup;
                                  int index = 0;
                                  engine.editSong ([&] (Song& s) {
                                      c.mixerTrack = s.claimFreeInsert();
                                      index = insertChannel (s, std::min (insertAt, (int) s.channels.size()), c);
                                  });
                                  selectionAnchor = index;
                                  selectRows ({ index }, index);
                              });
}

void ChannelRackComponent::assignSample (int channel, const juce::File& file)
{
    // FL's .speech files open the Speech properties first, like FL's channel load does (round 168b); the file it
    // returns (the edited copy, or this one) goes into the channel
    if (file.hasFileExtension ("speech"))
    {
        speechui::editThenLoad (engine, file, [safe = juce::Component::SafePointer<ChannelRackComponent> (this), channel] (const juce::File& f) {
            if (safe != nullptr)
                safe->assignSampleNow (channel, f);
        });
        return;
    }
    assignSampleNow (channel, file);
}

void ChannelRackComponent::assignSampleNow (int channel, const juce::File& file)
{
    if (engine.getSampleLibrary().load (file.getFullPathName()) == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Sample"),
                                                tr ("Could not read {0}", { file.getFileName() }));
        return;
    }

    const auto path = file.getFullPathName().toStdString();
    const auto name = file.getFileNameWithoutExtension().toStdString();

    engine.editSong ([channel, path, name] (Song& s) {
        const auto isNew = channel < 0 || channel >= (int) s.channels.size();
        const auto freeInsert = s.claimFreeInsert();
        auto& c = isNew ? s.channels.emplace_back() : s.channels[(std::size_t) channel];

        if (isNew)
            c.mixerTrack = freeInsert;
        // (an audio clip channel stays one: as a Sampler its Playlist clips went silent, round 190ax)
        audioclips::loadSampleInto (c, path, name, flsettings::options().autoNameChannels.load());
    });

    if (channel < 0)
        engine.setSelectedChannel ((int) engine.getSong().channels.size() - 1);
}

void ChannelRackComponent::openPluginWindow (int channel)
{
    // Online Sessions P5-A1: a plugin the session's host plays for this channel opens its parameter editor
    if (channel >= 0 && channel < (int) engine.getSong().channels.size()
        && liveui::showHostedEditor ({ { live::Kind::channel, engine.getSong().channels[(std::size_t) channel].uid, 0 }, -1 }))
        return;
    auto instrument = engine.getChannelInstrument (channel);

    if (auto* plugin = dynamic_cast<PluginInstrument*> (instrument.get()))
    {
        // round 190ao: the same channel's window again, after a reload (the channel is found again by its instance tag:
        // channels may have moved since)
        auto reopen = [safe = juce::Component::SafePointer<ChannelRackComponent> (this), tag = engine.getSong().channels[(std::size_t) channel].instance.id] {
            if (safe == nullptr)
                return;
            const auto& channels = safe->engine.getSong().channels;
            for (std::size_t i = 0; i < channels.size(); ++i)
                if (channels[i].instance.id == tag && channels[i].instrument == InstrumentKind::plugin)
                {
                    if (safe->engine.hostedPluginDown ({ (int) i, -1, -1 }))
                        PluginWindows::reload (safe->engine, { { (int) i, -1, -1 } });
                    if (! safe->engine.hostedPluginDown ({ (int) i, -1, -1 }))
                        safe->openPluginWindow ((int) i);
                    return;
                }
        };
        // crashed inside a call (not called again: its window would call into it), or its separate process died:
        // "Reload" starts it again without reopening the project
        if (engine.hostedPluginDown ({ channel, -1, -1 }))
        {
            PluginWindows::promptReload (engine, { channel, -1, -1 }, [safe = juce::Component::SafePointer<ChannelRackComponent> (this), channel] {
                if (safe != nullptr)
                    safe->openPluginWindow (channel);
            });
            return;
        }
        PluginWindows::WrapperContext context { &engine, [channel] (Song& song) -> PluginRef* {
                                                    return channel < (int) song.channels.size() ? &song.channels[(std::size_t) channel].plugin : nullptr;
                                                }, true, -1, -1, {}, {} };
        context.reload = reopen;
        context.down = [weak = std::weak_ptr<Instrument> (instrument)] {
            const auto held = weak.lock();
            auto* p = dynamic_cast<PluginInstrument*> (held.get());
            return p != nullptr && p->hasCrashed();
        };
        PluginWindows::open (instrument, plugin->getInstance(),
                             juce::String::fromUTF8 (engine.getSong().channels[(std::size_t) channel].name.c_str()), std::move (context));
        return;
    }

    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Plugin"),
                                            tr ("This plugin could not be loaded (it is not installed or has not been scanned)."));
}

void ChannelRackComponent::rename (int channel)
{
    const auto& c = engine.getSong().channels[(std::size_t) channel];
    // issue #201 (round 190ll, core/SelectionRename.h): a new colour goes to every selected channel (the name to this one)
    const auto targets = selrename::targets (channel, engine.getSelectedChannels());
    // issue #220 (round 190ng): FL's "Rename, color and icon" sets the channel's icon in the same window
    showNameColourIconDialog (tr ("Rename channel"), juce::String::fromUTF8 (c.name.c_str()), c.colour, c.icon,
                              [this, channel, targets, oldName = c.name, oldColour = c.colour, oldIcon = c.icon] (const juce::String& name, std::uint32_t colour, int icon) {
                                  if (channel < (int) engine.getSong().channels.size())
                                      engine.editSong ([channel, targets, icon, oldIcon, r = selrename::Result { oldName, name.toStdString(), oldColour, colour }] (Song& s) {
                                          selrename::applyToChannels (s, channel, targets, r);
                                          if (icon >= 0 && icon != oldIcon && channel < (int) s.channels.size())
                                              s.channels[(std::size_t) channel].icon = icon;
                                      });
                              });
}

namespace
{
// Live's "Slice to Drum Rack" (round 190ec): Simpler in Slicing mode → a Drum Rack on the same track, one pad per slice
// (Live's Default slicing facts: gated, choke group 1), no clip. LYNX: the Sampler channel's own slices if it has them,
// else its transients (a .speech sample: its words), inside its played region; the channel keeps its name, insert,
// levels and colour.
void sliceChannelToDrumRack (AudioEngine& engine, int channel)
{
    const auto& song = engine.getSong();
    if (channel < 0 || channel >= (int) song.channels.size())
        return;
    const auto& c = song.channels[(std::size_t) channel];
    const auto sample = engine.getSampleLibrary().load (juce::String::fromUTF8 (c.samplePath.c_str()));
    if (sample == nullptr || sample->frames() == 0)
        return;
    std::vector<float> starts = c.sampler.slices;
    if (starts.empty())
    {
        if (juce::File (juce::String::fromUTF8 (c.samplePath.c_str())).hasFileExtension ("speech") && ! sample->markerStarts.empty())
            for (const auto start : sample->markerStarts)
                starts.push_back ((float) ((double) start / (double) sample->frames()));
        else
            starts = detectSlices (*sample, 0.5f, rack::maxSlicePads);
    }
    const auto from = std::clamp (c.sampler.regionStart, 0.0f, 1.0f), to = std::clamp (c.sampler.regionEnd, from, 1.0f);
    const auto slices = rack::slicesInRegion (starts, from, to);
    const auto stem = juce::File (juce::String::fromUTF8 (c.samplePath.c_str())).getFileNameWithoutExtension().toStdString();
    auto drum = rack::sliceToDrumRack (c.samplePath, slices, stem.empty() ? c.name : stem, rack::SliceTemplate::gated, to);
    engine.editSong ([channel, patcher = std::move (drum.patcher)] (Song& s) {
        if (channel >= (int) s.channels.size())
            return;
        auto& ch = s.channels[(std::size_t) channel];
        ch.instrument = InstrumentKind::patcher;
        ch.patcher = patcher;
        ch.sampler.slices.clear();
    });
}
} // namespace

void ChannelRackComponent::sliceChannel (int channel)
{
    const auto& song = engine.getSong();
    const auto& c = song.channels[(std::size_t) channel];
    const auto sample = engine.getSampleLibrary().load (juce::String::fromUTF8 (c.samplePath.c_str()));

    if (sample == nullptr || sample->frames() == 0)
        return;

    // a spoken .speech sample is cut at its words (FL hands its words to the slicer as a Zero-X grid; round 168b)
    std::vector<float> slices;
    if (juce::File (juce::String::fromUTF8 (c.samplePath.c_str())).hasFileExtension ("speech") && ! sample->markerStarts.empty())
        for (const auto start : sample->markerStarts)
            slices.push_back ((float) ((double) start / (double) sample->frames()));
    else
        slices = detectSlices (*sample, 0.5f);
    const auto seconds = (double) sample->frames() / sample->sampleRate;
    const auto ticksPerSecond = song.bpm * ppq / 60.0;
    const auto name = c.name + " (slices)";

    engine.editSong ([&] (Song& s) {
        s.channels[(std::size_t) channel].sampler.slices = slices;

        // pattern that plays the slices in their original order and timing (at the project BPM)
        auto& p = s.patterns.emplace_back();
        p.name = name;

        for (std::size_t i = 0; i < slices.size(); ++i)
        {
            const auto start = slices[i] * seconds;
            const auto end = i + 1 < slices.size() ? slices[i + 1] * seconds : seconds;
            Note n;
            n.start = (std::int64_t) std::llround (start * ticksPerSecond);
            n.length = std::max<std::int64_t> (1, (std::int64_t) std::llround ((end - start) * ticksPerSecond));
            n.key = 60 + (int) i;
            n.channel = channel;
            p.notes.push_back (n);
        }

        p.sortNotes();
        s.currentPattern = (int) s.patterns.size() - 1;
    });
}

bool ChannelRackComponent::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (SampleLibrary::isSampleFile (juce::File (f)) || juce::File (f).hasFileExtension ("sf2;sf3;sfz")
            || isSamplerInstrumentFile (juce::File (f))) // (.dwp / .dwb / .gig / .nki: round 190ii)
            return true;

    return false;
}

void ChannelRackComponent::filesDropped (const juce::StringArray& files, int x, int y)
{
    int row = 0;
    const auto area = areaAt (x, y, row);
    auto target = (area == Area::none || area == Area::addRow || isAutomationArea (area)) ? -1 : row; // (190oo: no channel)
    // round 190oa (DF-101, capture 190oa B1): FL opens a new sample channel's window as it drops, unless Alt is held
    const auto channelsBefore = engine.getSong().channels.size();
    const auto altHeld = juce::ModifierKeys::getCurrentModifiersRealtime().isAltDown();
    const struct OpenAfter
    {
        ChannelRackComponent& rack;
        std::size_t before;
        bool alt;
        ~OpenAfter()
        {
            const auto now = rack.engine.getSong().channels.size();
            if (alt || now <= before || ! rack.isShowing())
                return;
            const auto last = (int) now - 1;
            if (const auto kind = rack.engine.getSong().channels.back().instrument; kind == InstrumentKind::sampler || kind == InstrumentKind::audioClip)
                openChannelSettings (rack.engine, last);
        }
    } openAfter { *this, channelsBefore, altHeld };

    for (const auto& f : files)
    {
        const juce::File file (f);

        if (file.hasFileExtension ("sf2;sf3;sfz"))
        {
            // a SoundFont: into the LYNX Soundfont channel it was dropped on, else a new channel
            openSoundFontFile (engine, file, target);
            target = -1;
            continue;
        }
        if (isSamplerInstrumentFile (file))
        {
            // another sampler's instrument (round 190ii, issue #149): into the DirectWave channel it was dropped on, else
            // a new DirectWave channel
            openSamplerInstrumentFile (engine, file, target);
            target = -1;
            continue;
        }
        if (! SampleLibrary::isSampleFile (file)) // (FL's WavPack pack samples too: round 165q)
            continue;

        assignSample (target, file);
        target = -1; // the remaining files become new channels
    }
}

void ChannelRackComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    // a level drag (AudioEngine::editLevels, round 171c) changed only levels: repaint just the knobs that moved. Round
    // 173f (discussion #10): every mouse move of a Mixer fader resized and repainted the whole rack, which moved nothing
    const auto& channels = engine.getSong().channels;
    if (source == &engine.getLevelsBroadcaster() && shownLevels.size() == channels.size())
    {
        for (std::size_t i = 0; i < channels.size(); ++i)
            if (const std::array<float, 2> now { channels[i].volume, channels[i].pan }; now != shownLevels[i])
            {
                shownLevels[i] = now;
                repaint (controlBounds (Area::pan, (int) i).getUnion (controlBounds (Area::volume, (int) i)).expanded (2));
            }
        return;
    }
    shownLevels.resize (channels.size());
    for (std::size_t i = 0; i < channels.size(); ++i)
        shownLevels[i] = { channels[i].volume, channels[i].pan };
    updateSize();
    repaint();
}

juce::Rectangle<int> ChannelRackComponent::selectorBounds (int row) const
{
    // the whole selector slot (round 190io's tall slot, y + 3 … rowHeight - 6, whose lit pill reaches y + 5: rollup 0.1.30,
    // round 190iy; 190ih's bounds were the slim pill's, so a fading light left its top and bottom rows lit), 1 px around
    return { stickyX + selectLeft(), rowY (row) + 2, selectWidth, rowHeight - 4 };
}

// round 190ih (PLAN R4, DC-8 / DE-6): one step of every channel's activity light; only the selectors whose shade
// changed repaint, so a rack where nothing sounds costs one counter read per channel and no paint
void ChannelRackComponent::updateActivityLights()
{
    const auto& song = engine.getSong();
    const auto count = (int) song.channels.size();
    if ((int) activityLights.size() != count)
        activityLights.resize ((std::size_t) count);
    const auto fade = ui::motion::enabled();
    for (int row = 0; row < count; ++row)
    {
        auto& light = activityLights[(std::size_t) row];
        const auto before = activity::shade (light.level);
        const auto keys = engine.soundingKeys (row);
        activity::step (light, engine.noteStrikes (row), (keys[0] | keys[1]) != 0, fade);
        if (activity::shade (light.level) != before && isShown (row))
            repaint (selectorBounds (row));
    }
}

void ChannelRackComponent::timerCallback()
{
    if (isShowing())
        updateActivityLights();

    // the channels' volume / pan knobs follow their automation (issue #12): only the automated knobs repaint
    if (const auto& shown = engine.getAutomationDisplay(); shown.getGeneration() != shownAutomation)
    {
        shownAutomation = shown.getGeneration();
        std::vector<int> rows;
        for (const auto& t : shown.automatedTargets())
            if (t.kind == AutomationTargetKind::channelVolume || t.kind == AutomationTargetKind::channelPan)
                rows.push_back (t.index);
        if (isShowing())
            for (const auto list : { &rows, &automatedRows })
                for (const auto r : *list)
                    if (r >= 0 && r < (int) engine.getSong().channels.size())
                        repaint (controlBounds (Area::pan, r).getUnion (controlBounds (Area::volume, r)).expanded (2));
        automatedRows = std::move (rows);
    }
}

bool ChannelRackComponent::playheadFrame (double)
{
    auto& transport = engine.getTransport();
    auto step = -1;
    const auto playing = transport.playing.load();

    // the playhead shows whenever the shown pattern plays: Pattern mode, or a clip of it in Song mode (Busy Works
    // Beats' video, round 170a: in Song mode the rack showed none), as the Piano Roll's (transportedit::patternPlayhead)
    if (playing && isShowing())
    {
        const auto& song = engine.getSong();
        // (between two audio blocks it moves on at the song's tempo, round 174e: the step lights when it is heard)
        const auto tick = transportedit::patternPlayhead (song, song.currentPattern, transport.positionTicks.load() + engine.playheadAdvanceTicks());
        if (tick >= 0.0)
            step = (int) (tick / ticksPerStep);
    }

    if (step != playheadStep)
    {
        // (fitted miniatures draw their own playhead line, at the pattern's scale)
        const auto span = previewSpan();
        const auto total = numSteps();
        auto dirty = [this, span, total] (int s) {
            if (s < 0)
                return;
            repaint (gridLeft() + s * stepWidth - 1, 0, stepWidth + 1, getHeight());
            if (span.fitted && s < total)
                repaint (gridLeft() + span.x + (int) ((std::int64_t) span.width * s / total) - 1, 0, juce::jmax (2, span.width / total) + 3, getHeight());
        };
        dirty (playheadStep);
        playheadStep = step;
        dirty (playheadStep);
        repaint (stickyX + selectLeft(), 0, selectWidth, getHeight()); // the selectors flash with the hits
        followPlayhead();
        return true;
    }
    return playing;
}

void ChannelRackComponent::followPlayhead()
{
    // Follow (issue #12): a pattern longer than the view pages along with the playing step, as the Playlist does
    if (playheadStep < 0 || ! EditorGlobals::get().isFollowingView (EditorGlobals::FollowView::channelRack))
        return;
    auto* viewport = findParentComponentOfClass<juce::Viewport>();
    if (viewport == nullptr)
        return;
    const auto view = viewport->getViewArea();
    const auto stepsLeft = view.getX() + gridLeft(); // (the controls column stays on the left of the view)
    const auto x = gridLeft() + playheadStep * stepWidth;
    if (x >= stepsLeft && x + stepWidth <= view.getRight())
        return;
    const auto maxX = juce::jmax (0, getWidth() - view.getWidth());
    viewport->setViewPosition (juce::jlimit (0, maxX, x - gridLeft()), view.getY());
}

bool ChannelRackComponent::isInterestedInDragSource (const SourceDetails& details)
{
    std::string path;
    int bank = 0, program = 0;
    return BrowserComponent::isBrowserDrag (details) || BrowserComponent::isGeneratorDrag (details)
           || parseSoundFontDrag (details.description.toString(), path, bank, program);
}

void ChannelRackComponent::itemDropped (const SourceDetails& details)
{
    // a generator dragged from the Browser's Plugin database (round 162): below the channels a new channel with it, onto a
    // channel it replaces that channel's instrument (FL, audit round 165z-a: insert, notes and levels stay); window open
    if (BrowserComponent::isGeneratorDrag (details))
    {
        if (auto c = BrowserComponent::draggedGenerator (details))
        {
            int row = 0;
            const auto area = areaAt (details.localPosition.x, details.localPosition.y, row);
            if (area != Area::none && area != Area::addRow && ! isAutomationArea (area) && row >= 0
                && row < (int) engine.getSong().channels.size())
            {
                closeEditor (row);
                const auto kind = c->instrument;
                engine.editSong ([row, &c] (Song& s) { replaceGenerator (s, row, std::move (*c)); });
                selectionAnchor = row;
                selectRows ({ row }, row);
                if (kind == InstrumentKind::plugin)
                {
                    openPluginWindow (row);
                    openEditorRow = row;
                }
                else if (natives::instrumentEngine (kind) != nullptr)
                    openChannelSettings (engine, row);
            }
            else
                addChannel (std::move (*c), true);
        }
        return;
    }

    // a preset dragged from a LYNX Soundfont list: onto a LYNX Soundfont channel it loads there, else a new channel
    std::string path;
    int bank = 0, program = 0;
    if (parseSoundFontDrag (details.description.toString(), path, bank, program))
    {
        int row = 0;
        const auto area = areaAt (details.localPosition.x, details.localPosition.y, row);
        const auto& song = engine.getSong();
        if (area != Area::none && area != Area::addRow && ! isAutomationArea (area) && row >= 0
            && row < (int) song.channels.size() && song.channels[(std::size_t) row].instrument == InstrumentKind::soundfont)
            loadSoundFontInto (engine, row, path, bank, program);
        else
            addSoundFontChannel (engine, path, bank, program);
        return;
    }
    if (const auto file = BrowserComponent::draggedFile (details); file.existsAsFile())
        filesDropped ({ file.getFullPathName() }, details.localPosition.x, details.localPosition.y);
}
} // namespace lynx
