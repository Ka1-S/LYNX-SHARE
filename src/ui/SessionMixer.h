#pragma once

// The Session mixer under the clip grid (round 190dr, Clip-launch workflow; model in core/SessionMixer.h): a strip per
// grid column with Live's sections In/Out, Sends, Track Options and Volume, and the Main strip under the scene column
// [SHOT ableton/probe/p3/session1.png]. The clip grid owns it, places it under its status row and scrolls it with its
// columns. Painted in one component (no child per strip); its meters tick with the grid's timer.

#include <cstdint>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/MeterBallistics.h"
#include "core/SessionMixer.h"
#include "ui/ValueControl.h"
#include "ui/MapMode.h"

namespace lynx
{
class AudioEngine;

class SessionMixerComponent final : public juce::Component, public juce::TooltipClient, public mapmode::SpotSource
{
public:
    // round 190ef: the volume, pan and send knobs are mappable (MIDI / Key Map Mode)
    void mapSpots (std::vector<mapmode::Spot>& out) override;
    explicit SessionMixerComponent (AudioEngine& engine);

    // The grid's columns: `count` columns `columnWidth` wide, scrolled by `scrollX`, in [0, tracksWidth); round 190kc:
    // `returns` return strips (Live's return tracks, columnWidth each) right of the tracks; the Main strip is `mainWidth`
    // wide to the right of them.
    // Round qafix190kq (QA-2): `tracks` = the Playlist track of each column (the grid skips take lanes); empty: column c
    // is track c.
    void setColumns (int count, int columnWidth, int scrollX, int tracksWidth, int mainWidth, int returns = 0,
                     const std::vector<int>& tracks = {});
    // Round 190kc: the return strips the grid shows for this song (Live's View › Mixer Controls › Return Tracks).
    static int returnColumnsFor (const Song& song);
    // the grid column code of return strip k (Hit::column, partBounds)
    static constexpr int returnBase = 1 << 16;
    static constexpr int returnColumn (int k) noexcept { return returnBase + k; }
    // The height the shown sections take when at most `available` px are free (0: the mixer is hidden).
    int wantedHeight (int available) const;
    // True when the Volume section (with each track's Arm button) is on screen: the grid's status row then shows no arm.
    bool armsShown() const noexcept { return sections.volume > 0; }
    // One display tick of the meters (the grid's 30 Hz timer while it is on screen): the meters that moved are
    // repainted (round 190ge). True when one moved.
    bool tick();

    // Live's View > Mixer and View > Mixer Controls items, on the grid's Mixer button and in its options menu.
    void addViewItems (juce::PopupMenu& menu);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    juce::String getTooltip() override;

    // the parts of a strip (also what --liveshot190dr checks)
    // (round 190jn: `peak` = Live's peak-level pill above the volume field, Live's look only)
    // (round 190kc: `post` = a return's Pre / Post toggle in the Main strip's Sends section (row = the return's row);
    // `xfadeA` / `xfadeB` = the Crossfade Assign buttons; `crossfader` = the Main strip's crossfader)
    // (round 190nc, DL-110, Live's look only: the Main strip's `cueOut` / `mainOut` choosers, its Solo/Cue Mode switch
    // `soloCue` and Cue Volume knob `cueVolume`; a track's `solo` is its Cue switch while Cue mode is in effect)
    enum class Part { none, inType, inChannel, monIn, monAuto, monOff, output, send, delay, volume, fader, pan, activator, solo, arm, peak,
                      post, xfadeA, xfadeB, crossfader, cueOut, mainOut, soloCue, cueVolume };
    struct Hit
    {
        int column = -1; // the grid column; -2 = the Main strip; returnColumn (k) = return strip k (round 190kc)
        Part part = Part::none;
        int row = 0;     // Sends: the row
        bool operator== (const Hit&) const = default;
    };
    // where a part is drawn (empty when it is not shown); `column` -2 = Main
    juce::Rectangle<int> partBounds (int column, Part part, int row = 0) const;
    bool returnRow (int insert) const; // (round 190rb: a send row's insert is a Live return the song still has)
    Hit hitAt (juce::Point<int> p) const;
    const std::vector<int>& sendTargets() const noexcept { return rows; }

    // Round 190jn (--livelook190jnshot): an insert's meter as if the song played (bars, hold lines and the peak pill's
    // loudest peak, linear gain; >= 1 = clipped), and a hovered part. The next tick() moves the meters on as usual.
    void setMeterForSnapshot (int insert, float left, float right, float holdLeft, float holdRight, float maxPeak);
    void setHoverForSnapshot (Hit hit) { hover = hit; repaint(); }
    juce::String tipForSnapshot (const Hit& hit) const { return tipFor (hit); } // (the Info View's text for a part)
    // Round 190kc: the menus a right-click opens on a return's send knob and on the crossfader (--cliplaunch190kcshot)
    juce::PopupMenu sendMenu (const Hit& hit);
    juce::PopupMenu crossfaderMenu();
    juce::PopupMenu deviceOutputMenu (bool cue); // round 190nc: the Cue Out (true) / Main Out chooser's menu
    int returnColumnCount() const noexcept { return returnColumns; }
    // the loudest peak the pill holds (linear gain, 0 = none)
    float loudestPeak (int insert) const noexcept
    {
        return insert >= 0 && insert < (int) loudest.size() ? loudest[(std::size_t) insert] : 0.0f;
    }

    // round 190ou (--trackstates190opshot): where a column's strip is
    int stripXForSnapshot (int column) const noexcept { return stripX (column); }
    int stripWidthForSnapshot (int column) const noexcept { return stripWidth (column); }
private:
    bool isAutomated (const AutomationTarget& t) const; // (round 190lx, DL-59)
    void refreshCache() const;
    sessionmixer::Strip stripFor (int column) const;
    int insertFor (int column) const; // Main: 0
    // round qafix190kq: the Playlist track of a track column (the grid's map), and the column of a track (−1: none)
    int trackOf (int column) const noexcept
    {
        return column >= 0 && column < (int) columnTracks.size() ? columnTracks[(std::size_t) column] : column;
    }
    int columnOf (int track) const noexcept;
    int stripX (int column) const noexcept;
    int stripWidth (int column) const noexcept { return column == mainColumn ? mainWidth : columnWidth; }
    void layoutSections();
    ui::ValueSpec specFor (const Hit& hit) const;
    double valueFor (const Hit& hit) const;
    void setValue (const Hit& hit, double v);
    void paintStrip (juce::Graphics& g, int column);
    void paintMeter (juce::Graphics& g, int insert, juce::Rectangle<int> area) const;
    // round 190jn (design190ia CL-16): Live 12.4's Session mixer strip, in Live's look only (theme::livelook::active)
    juce::Rectangle<int> livePartBounds (int column, Part part, int row) const;
    void paintStripLive (juce::Graphics& g, int column, int selected);
    void paintDeactivatedVeil (juce::Graphics& g, int column) const; // (round 190ou)
    void paintMeterLive (juce::Graphics& g, int insert, juce::Rectangle<int> area) const;
    void showInputMenu (int insert, bool channels);
    void showSendMenu (const Hit& hit);   // round 190kc: a return's send knob (Enable Send / Enable All Sends)
    void showCrossfaderMenu();            // round 190kc: the crossfader's seven curves
    bool isReturn (int column) const noexcept { return column >= returnBase && column < returnBase + returnColumns; }
    void paintCrossfadeSection (juce::Graphics& g, int column, bool live);
    void showOutputMenu (int insert);
    // round 190nc (DL-110): the Main strip's Cue Out / Main Out choosers (the device's pairs, then its single outputs)
    void showDeviceOutputMenu (bool cue);
    bool cueInEffect() const; // Live's Cue mode in effect (the switch on Cue, the Cue Out separate from Main)
    juce::String tipFor (const Hit& hit) const;

    static constexpr int mainColumn = -2;
    AudioEngine& engine;
    int columns = 0, columnWidth = 104, scroll = 0, tracksWidth = 0, mainWidth = 128;
    int returnColumns = 0;                            // round 190kc: return strips shown
    std::vector<int> columnTracks;                    // round qafix190kq: each column's Playlist track (empty: the same)
    mutable std::vector<int> returnInserts;           // round 190kc: the returns' inserts (refreshCache)
    sessionmixer::Sections sections;
    std::vector<int> rows;                            // the Sends rows shown: their target inserts
    mutable std::vector<sessionmixer::Strip> strips;  // per column (refreshCache)
    mutable std::vector<int> allRows;                 // the Sends rows the strips would show
    mutable std::uint64_t cacheStamp = ~std::uint64_t (0);
    std::vector<meters::PeakHold> levels;             // per insert, left / right
    std::vector<float> loudest;                       // round 190jn: per insert, the peak pill's loudest peak since a reset
    std::vector<int> groupedInserts (int column) const; // round 190lr (DL-109)
    std::vector<int> paintGroup;                         // (the tracks selected together, read at each paint)
    std::vector<float> rms;                           // round 190lr (DL-108): per insert, left / right, the RMS bar (linear)
    int selectedShown = -1;                           // round 190jn: the grid's selected track the strips last showed
    std::uint64_t armsShownState = 0;                 // round 190ge: the arms the last tick saw (tick repaints on a change)
    ui::ValueDrag drag;
    Hit dragging, hover;
    int cueChannels = 0; // round 190nc: the device's output count, read at each paint (Cue mode needs a separate Cue Out)
};
} // namespace lynx
