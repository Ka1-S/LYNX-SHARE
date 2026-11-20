// Round 190iv: the main toolbar per layout style (design190ia PLAN §2 "Toolbar per layout style", slices SG-A and CL-G).
//
// The Pattern (FL) layout keeps MainComponent::resized() / paint() exactly as they were. The Simple layout keeps FL's
// frame (the menu over the hint bar at the left) with BandLab's landmarks: the transport as one 40 px capsule where FL's
// transport sits (owner decision SG-A, recommended: muscle memory), a metronome / tempo / time signature capsule, the Key
// capsule, the "Last saved" status in four states and Finish at the right. The Clip-launch layout replaces the two rows
// with Live 12's frame: the menus on a 23 px row, then one 38 px control bar of 17 px boxes in Live's order (CL-2); LYNX's
// own panels (Pattern / Song, snap, patterns, windows, file, recording options, the output monitor) are in its "⋯" menu.
//
// Performance: static paint. No timer of its own: the main window's timer calls updateLookFields(), and each field
// repaints only when what it shows changed; the CPU box repaints at the meter's rate, nothing else moves.

#include "ui/MainComponent.h" // (included at the end of MainComponent.cpp)

#include "core/ClipLauncher.h"
#include "core/DrawMode.h"
#include "core/ValueMath.h"
#include "engine/Metronome.h"
#include "ui/ClipGridComponent.h"
#include "ui/I18n.h"
#include "ui/LiveSessionUi.h"
#include "ui/SimpleWorkflow.h"
#include "ui/Theme.h"

namespace lynx
{
using toolbarlayout::Look;
namespace sb = toolbarlayout::simplebar;
namespace lb = toolbarlayout::livebar;
using workspace::Panel;

namespace
{
const juce::Colour liveChrome { 0xff2a2a2a }, liveSeparator { 0xff242424 };
}

void MainComponent::setUpLookParts()
{
    lookPartsReady = true;
    for (auto* c : std::initializer_list<juce::Component*> { &signatureBox, &quantBox, &saveStatus, &moreButton, &browserToggleButton,
                                                             &backToArrangementButton, &captureButton, &sessionRecordButton, &drawModeButton,
                                                             &keyMapButton, &midiMapButton, &sessionViewButton, &arrangementViewButton, &cycleButton, &metronomeMenuButton,
                                                             &liveLoopButton, &punchOutButton, &nudgeDownButton, &nudgeUpButton })
    {
        addChildComponent (c);
        c->setMouseClickGrabsKeyboardFocus (false);
        c->setWantsKeyboardFocus (false);
    }
    moreButton.setFlat (true);
    moreButton.setTooltip (tr ("More: the panels and options this toolbar leaves out"));
    ui::setHint (moreButton, tr ("More: the panels and options this toolbar leaves out"), true);
    moreButton.onClick = [this] { makeToolbarMoreMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&moreButton)); };
    moreButton.onRightClick = moreButton.onClick;

    browserToggleButton.setTooltip ("Browser");
    ui::setHint (browserToggleButton, tr ("Browser"));
    browserToggleButton.onClick = [this] { runAction ("panel.browser"); };

    backToArrangementButton.setTooltip (tr ("Tracks that launched a clip stay silent in the Playlist until this is pressed"));
    ui::setHint (backToArrangementButton, tr ("Back to Playlist") + ": " + tr ("Tracks that launched a clip stay silent in the Playlist until this is pressed"));
    backToArrangementButton.onClick = [this] { engine.launcherCommand ({ launcher::Command::Type::backToArrangement }); };

    captureButton.setTooltip (tr ("Capture MIDI: the notes just played without recording into the pattern"));
    ui::setHint (captureButton, tr ("Capture MIDI: the notes just played without recording into the pattern"));
    captureButton.onClick = [this] { runAction ("record.captureMidi"); };

    const auto sessionText = tr ("Session Record: armed tracks record into the selected scene; while their clips play, notes are added to them (overdub)");
    sessionRecordButton.setTooltip (sessionText);
    ui::setHint (sessionRecordButton, sessionText);
    sessionRecordButton.onClick = [this] {
        if (clipGridView != nullptr)
            engine.toggleSessionRecord (clipGridSelection (*clipGridView).second);
    };

    drawModeButton.setTooltip (tr ("Draw Mode") + "  B");
    ui::setHint (drawModeButton, tr ("Draw Mode") + "  [B]");
    drawModeButton.onClick = [] { drawmode::toggle(); };

    keyMapButton.setText (trc ("@computer key: Key"));
    keyMapButton.setTooltip (tr ("Key Map Mode: map computer keys to clips, scenes, buttons and controls (Ctrl+K)"));
    ui::setHint (keyMapButton, tr ("Key Map Mode: map computer keys to clips, scenes, buttons and controls (Ctrl+K)"));
    keyMapButton.onClick = [this] { setMapMode (getMapMode() == remotemap::Mode::key ? remotemap::Mode::off : remotemap::Mode::key); };
    midiMapButton.setText ("MIDI");
    midiMapButton.setTooltip (tr ("MIDI Map Mode: map MIDI controls to clips, scenes, buttons and controls (Ctrl+M)"));
    ui::setHint (midiMapButton, tr ("MIDI Map Mode: map MIDI controls to clips, scenes, buttons and controls (Ctrl+M)"));
    midiMapButton.onClick = [this] { setMapMode (getMapMode() == remotemap::Mode::midi ? remotemap::Mode::off : remotemap::Mode::midi); };

    sessionViewButton.setTooltip (tr ("Session view: the clip grid (Tab)"));
    ui::setHint (sessionViewButton, tr ("Session view: the clip grid (Tab)"));
    sessionViewButton.onClick = [this] { setClipGridView (PlaylistArea::GridView::grid); };
    arrangementViewButton.setTooltip (tr ("Arrangement view: the Playlist (Tab)"));
    ui::setHint (arrangementViewButton, tr ("Arrangement view: the Playlist (Tab)"));
    arrangementViewButton.onClick = [this] { setClipGridView (PlaylistArea::GridView::song); };

    saveStatus.onSave = [this] { save (false); };

    // round 190ld (details DS-2, BandLab B07: ⟳ after Record in the transport capsule): the Playlist's loop switch
    const auto cycleText = tr ("Cycle: plays the loop range over and over (without one: the bar at the song position)");
    cycleButton.setTooltip (cycleText);
    ui::setHint (cycleButton, cycleText);
    cycleButton.onClick = [this] { toggleCycle(); };
    cycleButton.setToggleLook (true); // (a switch: lit while it cycles)

    // round 190ld (details DS-1, BandLab B07): the ▾ beside the metronome opens BandLab's metronome popover
    metronomeMenuButton.setTooltip (tr ("Metronome: tap tempo, sound, volume and count-in"));
    ui::setHint (metronomeMenuButton, tr ("Metronome: tap tempo, sound, volume and count-in"), true);
    metronomeMenuButton.onClick = [this] { showMetronomePopover(); };

    // round 190lx (DL-61, Live manual 19.3.1, capture190ji live-loop-punch-z3): Live's Loop switch and Punch-Out switch
    // after its Punch-In switch (the punch button in this look); lit: Live's orange plate (the live box's toggle look)
    const auto loopText = tr ("Loop switch: plays the loop region over and over");
    liveLoopButton.setTooltip (loopText);
    ui::setHint (liveLoopButton, loopText);
    liveLoopButton.onClick = [this] { toggleLiveLoop(); };
    liveLoopButton.setToggleLook (true);
    const auto outText = tr ("Punch-Out switch: audio recording stops at the end of the loop region");
    punchOutButton.setTooltip (outText);
    ui::setHint (punchOutButton, outText);
    punchOutButton.onClick = [this] { runAction ("record.punchOut"); };
    punchOutButton.setToggleLook (true);
}

// Round 190lx (DL-61): Live's Loop switch. With a loop region it switches its looping (the region stays, as Live's brace);
// without one it sets one as the Simple Cycle does, a bar at the song position.
void MainComponent::toggleLiveLoop()
{
    toggleCycle();
}

namespace
{
// BandLab's metronome popover (bandlab190p shots/07-metronome-tap-countin.png, 342 x 356 at 100 %): Tap Tempo (− tempo / Tap
// +), Metronome Sound (the sounds and ▶ to hear it), Metronome Volume (a slider and its dB) and Count-in Duration (Off /
// 1 Bar / 2 Bars); LYNX's colours, BandLab's order and controls (Simple layout only)
class MetronomePopover final : public juce::Component
{
public:
    explicit MetronomePopover (AudioEngine& e, std::function<void()> saveIn) : engine (e), save (std::move (saveIn))
    {
        using namespace theme;
        for (auto* b : { &minus, &plus })
        {
            b->setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
            b->setColour (juce::TextButton::textColourOffId, colour::textPrimary);
            addAndMakeVisible (b);
        }
        minus.onClick = [this] { setTempo (engine.getSong().bpm - 1.0); };
        plus.onClick = [this] { setTempo (engine.getSong().bpm + 1.0); };
        ui::setHint (minus, tr ("Tempo - 1 BPM"));
        ui::setHint (plus, tr ("Tempo + 1 BPM"));
        for (int i = 0; i < metronome::soundCount; ++i)
            sound.addItem (tr (metronome::soundName (i)), i + 1);
        sound.setSelectedId (metronome::clampSound (engine.getTransport().metronomeSound.load()) + 1, juce::dontSendNotification);
        sound.onChange = [this] {
            engine.getTransport().metronomeSound.store (sound.getSelectedId() - 1);
            save();
        };
        addAndMakeVisible (sound);
        hear.onClick = [this] { preview(); };
        hear.setTooltip (tr ("Hear the metronome sound"));
        addAndMakeVisible (hear);
        volume.setSliderStyle (juce::Slider::LinearHorizontal);
        volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        volume.setRange (0.0, controls::maxUtilityVolume, 0.01);
        volume.setDoubleClickReturnValue (true, 1.0);
        volume.setValue (engine.getTransport().metronomeVolume.load(), juce::dontSendNotification);
        volume.onValueChange = [this] {
            engine.getTransport().metronomeVolume.store ((float) volume.getValue());
            repaint (volumeText);
        };
        volume.onDragEnd = [this] { save(); };
        addAndMakeVisible (volume);
        const juce::String names[] { tr ("Off"), tr ("1 Bar"), tr ("2 Bars") };
        for (int i = 0; i < 3; ++i)
        {
            auto& b = countIn[(std::size_t) i];
            b.setButtonText (names[i]);
            b.setClickingTogglesState (false);
            b.setColour (juce::TextButton::buttonColourId, colour::bg1);
            b.setColour (juce::TextButton::buttonOnColourId, colour::bg5);
            b.setColour (juce::TextButton::textColourOffId, colour::textSecondary);
            b.setColour (juce::TextButton::textColourOnId, colour::textPrimary);
            b.onClick = [this, i] {
                auto o = engine.getRecordOptions();
                o.countInBars = i;
                engine.setRecordOptions (o);
                refreshCountIn();
                save();
            };
            addAndMakeVisible (b);
        }
        refreshCountIn();
        setSize (300, 340);
    }

    void refreshCountIn()
    {
        const auto bars = engine.getRecordOptions().countInBars;
        for (int i = 0; i < 3; ++i)
            countIn[(std::size_t) i].setToggleState (bars == i || (i == 2 && bars > 2), juce::dontSendNotification);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 10);
        r.removeFromTop (26); // "Tap Tempo"
        tapBox = r.removeFromTop (60);
        minus.setBounds (tapBox.withWidth (tapBox.getWidth() / 3).reduced (8));
        plus.setBounds (tapBox.withTrimmedLeft (tapBox.getWidth() * 2 / 3).reduced (8));
        tapArea = tapBox.withTrimmedLeft (tapBox.getWidth() / 3).withWidth (tapBox.getWidth() / 3);
        r.removeFromTop (10);
        r.removeFromTop (26); // "Metronome Sound"
        auto row = r.removeFromTop (30);
        hear.setBounds (row.removeFromRight (30));
        row.removeFromRight (8);
        sound.setBounds (row);
        r.removeFromTop (10);
        r.removeFromTop (26); // "Metronome Volume"
        row = r.removeFromTop (24);
        volumeText = row.removeFromRight (64);
        row.removeFromLeft (22); // (the speaker)
        volume.setBounds (row);
        r.removeFromTop (10);
        r.removeFromTop (26); // "Count-in Duration"
        row = r.removeFromTop (34);
        for (int i = 0; i < 3; ++i)
            countIn[(std::size_t) i].setBounds (row.withX (row.getX() + row.getWidth() * i / 3).withWidth (row.getWidth() / 3).reduced (2));
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        g.fillAll (colour::bg2);
        g.setColour (colour::textPrimary);
        g.setFont (font (type::body, true));
        auto y = 10;
        for (const auto& title : { tr ("Tap Tempo"), tr ("Metronome Sound"), tr ("Metronome Volume"), tr ("Count-in Duration") })
        {
            g.drawText (title, 12, y, getWidth() - 24, 22, juce::Justification::centredLeft, true);
            y += title == tr ("Tap Tempo") ? 26 + 60 + 10 : title == tr ("Metronome Sound") ? 26 + 30 + 10 : 26 + 24 + 10;
        }
        fillRoundRect (g, tapBox.toFloat(), radius::md, colour::bg1);
        g.setColour (colour::border);
        g.fillRect (tapArea.getX(), tapBox.getY() + 16, 1, tapBox.getHeight() - 32);
        g.fillRect (tapArea.getRight(), tapBox.getY() + 16, 1, tapBox.getHeight() - 32);
        if (flash > 0.0f)
            fillRoundRect (g, tapArea.reduced (4).toFloat(), radius::sm, colour::accent.withAlpha (0.25f * flash));
        g.setColour (colour::textPrimary);
        g.setFont (font (type::title, true));
        g.drawText (juce::String (juce::roundToInt (engine.getSong().bpm)), tapArea.withTrimmedBottom (24), juce::Justification::centredBottom, false);
        g.setColour (colour::textSecondary);
        g.setFont (font (type::small));
        g.drawText (tr ("Tap"), tapArea.withTrimmedTop (36), juce::Justification::centredTop, false);
        // the speaker beside the volume and the level in dB
        icons::draw (g, Icon::listen, volume.getBounds().toFloat().withX ((float) volume.getX() - 22.0f).withWidth (18.0f), colour::textSecondary);
        g.setColour (colour::textPrimary);
        g.setFont (font (type::small, true));
        const auto gain = (float) volume.getValue();
        g.drawText (gain <= 0.0f ? juce::String ("-inf dB") : juce::String (juce::Decibels::gainToDecibels (gain), 1).replace ("-0.0", "0.0") + " dB",
                    volumeText, juce::Justification::centredRight, false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! tapArea.contains (e.getPosition()))
            return;
        if (const auto bpm = taps.tap (juce::Time::getMillisecondCounterHiRes()))
            setTempo (*bpm);
        flash = 1.0f;
        repaint (tapBox);
        juce::Timer::callAfterDelay (150, [safe = juce::Component::SafePointer<MetronomePopover> (this)] {
            if (safe != nullptr)
            {
                safe->flash = 0.0f;
                safe->repaint (safe->tapBox);
            }
        });
    }

    std::function<void()> save;

private:
    void setTempo (double bpm)
    {
        bpm = controls::clampTempo (bpm);
        if (std::abs (bpm - engine.getSong().bpm) > 1e-9)
            engine.editSong ([bpm] (Song& s) { clipops::followTempo (s, bpm); });
        repaint (tapBox);
    }

    // ▶: four clicks of the chosen sound at the song's tempo, the first one the downbeat, through the preview player
    void preview()
    {
        const auto& data = flStudioAccess().roots.data;
        const auto sounds = Metronome::loadSounds (data.empty() ? juce::File() : platform::localFile (data));
        const auto& s = sounds[(std::size_t) metronome::clampSound (engine.getTransport().metronomeSound.load())];
        if (s.frames() <= 0)
            return;
        const auto beat = (int) std::lround (s.rate * 60.0 / juce::jmax (20.0, engine.getSong().bpm));
        auto out = std::make_shared<SampleData>();
        out->sampleRate = s.rate;
        out->left.assign ((std::size_t) (beat * 4 + s.frames()), 0.0f);
        out->right = out->left;
        const auto gain = (float) volume.getValue();
        for (int k = 0; k < 4; ++k)
            for (int i = 0; i < s.frames(); ++i)
            {
                out->left[(std::size_t) (k * beat + i)] += s.left[(std::size_t) i] * gain;
                out->right[(std::size_t) (k * beat + i)] += (s.right.empty() ? s.left : s.right)[(std::size_t) i] * gain;
            }
        engine.previewSample (out, false, 0.8f);
    }

    AudioEngine& engine;
    juce::TextButton minus { juce::String::fromUTF8 ("\xe2\x88\x92") }, plus { "+" };
    juce::ComboBox sound;
    IconButton hear { "hear", Icon::play };
    juce::Slider volume;
    std::array<juce::TextButton, 3> countIn;
    juce::Rectangle<int> tapBox, tapArea, volumeText;
    controls::TapTempo taps;
    float flash = 0.0f;
};
} // namespace

void MainComponent::showMetronomePopover()
{
    auto popover = std::make_unique<MetronomePopover> (engine, [this] { saveUiSettings(); refreshRecordOptionButtons(); });
    juce::CallOutBox::launchAsynchronously (std::move (popover), metronomeMenuButton.getScreenBounds(), nullptr);
}

std::unique_ptr<juce::Component> MainComponent::makeMetronomePopoverForSnapshot()
{
    return std::make_unique<MetronomePopover> (engine, [] {});
}

void MainComponent::toggleCycle()
{
    const auto& song = engine.getSong();
    if (song.hasLoop())
    {
        engine.editSong ([] (Song& s) { s.loopOff = ! s.loopOff; }, false);
        return;
    }
    // no range yet: BandLab's cycle region is one bar (B07: bar 1 at the song's start); here the bar at the song position
    const auto bar = juce::jmax<std::int64_t> (1, song.barTicks());
    const auto from = (std::int64_t) std::floor (engine.getTransport().positionTicks.load() / (double) bar) * bar;
    engine.editSong ([from, bar] (Song& s) {
        s.loopStart = from;
        s.loopEnd = from + bar;
        s.loopOff = false;
    });
}

std::vector<juce::Component*> MainComponent::lookOnlyParts()
{
    std::vector<juce::Component*> parts { &signatureBox, &quantBox, &saveStatus, &moreButton, &browserToggleButton, &backToArrangementButton, &captureButton,
                                          &sessionRecordButton, &drawModeButton, &keyMapButton, &midiMapButton, &sessionViewButton, &arrangementViewButton, &cycleButton, &metronomeMenuButton,
                                          &liveLoopButton, &punchOutButton, &nudgeDownButton, &nudgeUpButton };
    if (simpleWorkflow != nullptr)
        if (auto* finish = simpleWorkflow->finishComponent(); finish != nullptr && finish->getParentComponent() == this)
            parts.push_back (finish);
    if (simpleWorkflow != nullptr) // (round 190ke)
        if (auto* tuner = simpleWorkflow->tunerComponent(); tuner != nullptr && tuner->getParentComponent() == this)
            parts.push_back (tuner);
    return parts;
}

void MainComponent::hideToolbarParts()
{
    std::vector<juce::Component*> parts { &patternModeButton, &songModeButton, &playButton, &stopButton, &recordButton, &tempoDisplay, &tapTempoButton, &timeDisplay,
                                          &songPosition, &masterVolume, &masterPitch, &metronomeButton, &typingPianoButton, &recordMenuButton, &countInButton,
                                          &waitInputButton, &blendButton, &loopRecordButton, &stepEditButton, &punchButton, &midiLight, &gridBox, &snapToggleButton,
                                          &patternBox, &newPatternButton, &patternMenuButton, &pluginPickerButton, &newButton, &openButton, &saveButton,
                                          &exportButton, &undoButton, &redoButton, &audioEditorButton, &perfMeter, &output, &chordPanel, &multilinkButton, &keyPanel, &linkToggle };
    for (auto& b : panelButtons)
        parts.push_back (&b);
    for (auto* c : lookOnlyParts())
        parts.push_back (c);
    for (auto* c : parts)
        if (c->isVisible())
            c->setVisible (false);
}

// Round 190jo (design190ia FM-1/FM-3): the glyph box of a tiled Pattern-toolbar button: 24 px on the default 31 px rows
// (ink about 18 px, FL's 16-20), 20 / 28 px on the small / large rows
float MainComponent::flTileIconBox() const
{
    return std::floor ((float) rowHeight * 0.78f);
}

// Round 190jo: FL's tiles on the Pattern toolbar's buttons that applyToolbarLook does not set per look (the file, window,
// recording-option, snap and pattern buttons); in the other looks they stay the standard flat buttons they were
void MainComponent::applyFlTiles()
{
    using BL = IconButton::Look;
    const auto fl = toolbarLook == Look::fl;
    const auto box = fl ? flTileIconBox() : 0.0f;
    for (auto* b : { &newButton, &openButton, &saveButton, &exportButton, &undoButton, &redoButton, &audioEditorButton, &recordMenuButton, &countInButton, &waitInputButton,
                     &loopRecordButton, &stepEditButton, &newPatternButton, &patternMenuButton, &pluginPickerButton, &snapToggleButton, &multilinkButton })
        b->setLook (fl ? BL::flTile : BL::standard, box);
    for (auto& b : panelButtons)
        b.setLook (fl ? BL::flTile : BL::standard, box);
    // FM-4 / FM-7: FL's larger menu-bar text and hint (the window starts in the Pattern look without a look change, so
    // this runs from applyIconSizing as well as from applyToolbarLook)
    if ((bool) menuBar.getProperties().getWithDefault ("lynxFlMenuBar", false) != fl)
    {
        menuBar.getProperties().set ("lynxFlMenuBar", fl);
        menuBar.repaint();
    }
    hintBar.setLarger (fl);
    if (fl)
    {
        for (auto* b : { &playButton, &stopButton, &recordButton })
            b->setLook (BL::flTile, 0.0f);
        for (auto* b : { &metronomeButton, &tapTempoButton, &blendButton, &punchButton, &typingPianoButton })
            b->setLook (BL::flTile, box);
    }
    // round 190ki (perf; on by default on the software renderer since round 190mh, opt-in LYNX_TILE_MODE=2 on the native
    // ones): the tiles' bodies from an atlas of this component's toolbar band (toolbarBackdropStamp, and paint()'s clip)
    for (auto* c : getChildren())
        if (auto* b = dynamic_cast<IconButton*> (c))
        {
            if (fl && b->getLook() == BL::flTile)
                b->setFlTileBackdrop ([this] (juce::Rectangle<int> r) { return toolbarBackdropStamp (r); }, [this] { return lastPaintClip; });
            else
                b->setFlTileBackdrop (nullptr);
        }
}

// Round 190ki: everything MainComponent::paint draws within `b` (a toolbar button's bounds) as one number, for the tiles'
// cached pictures (IconButton::setFlTileBackdrop): the band, the wells, separators, the mode and transport wells, the hint
// well, the title and the captions, with the tokens they use. 0: not cacheable there (paint straight). Anything new that
// paint() draws under the toolbar's buttons must join this.
std::uint64_t MainComponent::toolbarBackdropStamp (juce::Rectangle<int> b) const
{
    using namespace theme;
    const auto mix = [] (std::uint64_t a, std::uint64_t v) { return (a ^ v) * 0x100000001b3ull; };
    if (toolbarLook != Look::fl)
        return 0;
    const auto near = b.expanded (2); // (an anti-aliased edge reaches past its shape)
    auto h = mix (mix (0x13198a2e03707344ull, (std::uint64_t) getWidth()), (std::uint64_t) toolbarHeight);
    for (auto c : { colour::bg1, colour::bg2, colour::bg3, colour::border, colour::borderStrong, colour::textSecondary, colour::textDisabled })
        h = mix (h, c.getARGB());
    const auto rect = [&] (juce::Rectangle<int> r) {
        h = r.intersects (near) ? mix (mix (mix (mix (h, (std::uint64_t) (std::uint32_t) r.getX()), (std::uint64_t) (std::uint32_t) r.getY()),
                                            (std::uint64_t) (std::uint32_t) r.getWidth()), (std::uint64_t) (std::uint32_t) r.getHeight())
                                : mix (h, 1u);
    };
    rect ({ 0, 0, getWidth(), toolbarHeight });
    h = mix (h, toolbarLayout.separators ? 2u : 3u);
    if (toolbarLayout.separators)
        for (const auto& r : panelBounds)
            rect (r);
    else
        for (auto x : dividers)
            rect ({ x, 12, 1, toolbarHeight - 24 });
    rect (modeArea);
    rect (transportArea);
    rect (hintBar.getBounds());
    if (titleArea.intersects (near))
        return 0; // (the title and a live session's people change; no button sits there)
    for (const auto& [area, text] : { std::pair { snapLabelArea, tr ("SNAP") }, std::pair { patternLabelArea, tr ("PATTERN") } })
        if (area.intersects (near))
        {
            rect (area);
            h = mix (mix (mix (h, (std::uint64_t) text.hashCode64()), (std::uint64_t) (std::int64_t) (type::caption * 1000.0f)),
                     (std::uint64_t) boldText());
        }
    return h == 0 ? 1 : h;
}

// The widgets' looks for the layout style shown now. Pattern: every widget back to FL's look (the parts only the other
// looks have are hidden); the FL toolbar's code then lays it out as before.
void MainComponent::applyToolbarLook()
{
    const auto look = toolbarlayout::lookFor (layoutStyle);
    if (look != Look::fl && ! lookPartsReady)
        setUpLookParts();
    const auto changed = look != toolbarLook;
    toolbarLook = look;
    const auto fl = look == Look::fl, simple = look == Look::simple;
    using BL = IconButton::Look;

    // the transport: FL's flat buttons / BandLab's capsule segments (SG-1 glyph sizes) / Live's bare glyphs (~12-13 px)
    playButton.setLook (fl ? BL::flTile : simple ? BL::capsule : BL::liveGlyph, fl ? 0.0f : simple ? sb::playBox : 21.0f);
    stopButton.setLook (fl ? BL::flTile : simple ? BL::capsule : BL::liveGlyph, fl ? 0.0f : simple ? sb::stopBox : 22.0f);
    recordButton.setLook (fl ? BL::flTile : simple ? BL::capsule : BL::liveGlyph, fl ? 0.0f : simple ? sb::recordBox : 22.0f);
    playButton.setCorners (true, ! simple);
    stopButton.setCorners (! simple, ! simple);
    recordButton.setCorners (! simple, ! simple);
    cycleButton.setLook (BL::capsule, sb::stopBox); // (Simple only)
    cycleButton.setCorners (false, false);
    metronomeMenuButton.setLook (BL::capsule, 14.0f);
    metronomeMenuButton.setCorners (false, false);
    // the metronome: the first segment of the tempo capsule / Live's ring-and-dot box
    metronomeButton.setLook (fl ? BL::flTile : simple ? BL::capsule : BL::liveBox, simple ? 20.0f : fl ? flTileIconBox() : 13.0f);
    metronomeButton.setCorners (true, fl);
    metronomeButton.setIcon (look == Look::live ? Icon::metronomeDots : Icon::metronome);
    // Live's "Tap" box, overdub "+", punch, computer MIDI keyboard
    tapTempoButton.setLook (look == Look::live ? BL::liveBox : fl ? BL::flTile : BL::standard, fl ? flTileIconBox() : 0.0f);
    tapTempoButton.setIcon (look == Look::live ? Icon::none : Icon::tapTempo);
    tapTempoButton.setText (look == Look::live ? juce::String ("Tap") : juce::String());
    blendButton.setLook (look == Look::live ? BL::liveBox : fl ? BL::flTile : BL::standard, look == Look::live ? 13.0f : fl ? flTileIconBox() : 0.0f);
    blendButton.setIcon (look == Look::live ? Icon::plus : Icon::blend);
    for (auto* b : { &punchButton, &typingPianoButton })
        b->setLook (look == Look::live ? BL::liveBox : fl ? BL::flTile : BL::standard, look == Look::live ? 14.0f : fl ? flTileIconBox() : 0.0f);
    // round 190lx (DL-61): in Live's bar the punch button is its Punch-In switch, Loop and Punch-Out beside it
    punchButton.setIcon (look == Look::live ? Icon::punchIn : Icon::punch);
    for (auto* b : { &liveLoopButton, &punchOutButton })
        b->setLook (BL::liveBox, 14.0f);
    refreshRecordOptionButtons();
    for (auto* b : { &browserToggleButton, &backToArrangementButton, &captureButton, &sessionRecordButton, &drawModeButton, &keyMapButton, &midiMapButton })
        b->setLook (BL::liveBox, 13.0f);
    for (auto* b : { &sessionViewButton, &arrangementViewButton })
        b->setLook (BL::liveSelector, 15.0f);
    moreButton.setLook (look == Look::live ? BL::liveBox : BL::standard, look == Look::live ? 13.0f : 0.0f);
    applyFlTiles(); // (round 190jo: FL's tiles, menu and hint on the Pattern toolbar; the other looks keep theirs)

    tempoDisplay.look = look;
    tempoDisplay.setRepaintsOnMouseActivity (! fl);
    tempoDisplay.repaint();
    timeDisplay.setLook (look);
    keyPanel.setLook (look);
    masterVolume.setLook (look); // (round 190jj: BandLab's slider with the master meter in Simple)
    masterPitch.setLook (look);
    perfMeter.setLook (look);
    linkToggle.setLook (look);
    signatureBox.setLook (look);

    // Finish: the Simple workflow's button, at the right of the Simple top bar (BandLab's Mastering / Publish place)
    if (simpleWorkflow != nullptr)
        if (auto* finish = simpleWorkflow->finishComponent())
        {
            if (simple && finish->getParentComponent() != this)
            {
                addChildComponent (finish);
                finish->setMouseClickGrabsKeyboardFocus (false);
                finish->setColour (juce::TextButton::buttonColourId, theme::colour::accent);
                finish->setColour (juce::TextButton::buttonOnColourId, theme::colour::accentPressed);
                finish->setColour (juce::TextButton::textColourOffId, theme::colour::onAccent);
                finish->setColour (juce::TextButton::textColourOnId, theme::colour::onAccent);
            }
            else if (! simple && finish->getParentComponent() == this)
                removeChildComponent (finish);
        }
    // round 190ke: the tuner's button, after the Simple tempo capsule (GarageBand's Tuner button in the control bar)
    if (simpleWorkflow != nullptr)
        if (auto* tuner = simpleWorkflow->tunerComponent())
        {
            if (simple && tuner->getParentComponent() != this)
                addChildComponent (tuner);
            else if (! simple && tuner->getParentComponent() == this)
                removeChildComponent (tuner);
        }

    if (fl)
        for (auto* c : lookOnlyParts())
            c->setVisible (false);

    // Live's look: the clip grid's button row goes to Live's places (CL-25), unless the user keeps it
    if (clipGridView != nullptr)
        setClipGridButtonRowShown (*clipGridView, look != Look::live || sessionButtonRowShown);
    syncClipLaunchSwitches();
    refreshSaveStatus();
    updateLookFields();

    if (changed)
    {
        applyIconSizing (appliedIconSize); // (the toolbar's height per look)
        resized();
        repaint();
    }
}

// --- Simple ------------------------------------------------------------------------------------------------------------

void MainComponent::layoutSimpleToolbar (juce::Rectangle<int> bar, int menuWidth)
{
    using toolbarlayout::Group;
    using P = sb::Part;
    hideToolbarParts();
    dividers.clear();
    panelBounds = {};
    snapLabelArea = patternLabelArea = transportArea = modeArea = {};
    lookCapsules.clear();
    lookDividers.clear();
    lookMenuRow = lookBand = {};
    auto toolbar = bar.reduced (8, toolbarLayout.visible ? 5 : 3);
    const auto h = rowHeight, row1 = toolbar.getY(), row2 = toolbar.getY() + rowHeight + 2;
    auto put = [] (juce::Component& c, juce::Rectangle<int> r) {
        c.setVisible (true);
        c.setBounds (r);
    };

    if (! toolbarLayout.visible) // Ctrl+F11: the menu and the hint bar on one row, as FL's
    {
        const auto row = toolbar.withHeight (h);
        const auto menu = row.withWidth (juce::jmin (menuWidth + 8, row.getWidth()));
        put (menuBar, menu);
        titleArea = {};
        placeHintBar (row.withTrimmedLeft (menu.getWidth() + 12).withWidth (juce::jmin (900, row.getWidth() - menu.getWidth() - 12)).reduced (0, 1));
        fixedToolbarArea = bar;
        return;
    }

    std::array<int, sb::partCount> widths {};
    widths[(std::size_t) P::transport] = 4 * sb::segment + sb::timeWidth; // (round 190ld: + BandLab's Cycle)
    widths[(std::size_t) P::tempo] = sb::segment + sb::chevronWidth + sb::tempoWidth + sb::signatureWidth; // (round 190ld: BandLab's ▾)
    widths[(std::size_t) P::key] = toolbarLayout.isShown (Group::key) ? sb::keyWidth : 0;
    // (round 190jj: BandLab's master volume slider with its meter, then LYNX's master pitch knob)
    widths[(std::size_t) P::master] = toolbarLayout.isShown (Group::master) ? toolbar::MasterKnob::sliderWidth + 8 + h + 26 : 0;
    widths[(std::size_t) P::file] = toolbarLayout.isShown (Group::file) ? 3 * h + 4 : 0;
    widths[(std::size_t) P::link] = toolbarLayout.isShown (Group::link) ? toolbar::LinkToggle::idealWidth (h) : 0;
    widths[(std::size_t) P::more] = sb::moreWidth;
    widths[(std::size_t) P::status] = sb::statusWidth;
    widths[(std::size_t) P::finish] = simpleWorkflow != nullptr && simpleWorkflow->finishComponent() != nullptr ? sb::finishWidth : 0;
    widths[(std::size_t) P::tuner] = simpleWorkflow != nullptr && simpleWorkflow->tunerComponent() != nullptr ? sb::segment : 0;

    // the hint column keeps 320 px when it can (FL's), else only the menu's width
    auto minHint = juce::jmax (menuWidth + 8, 320);
    auto shown = sb::fit (widths, sb::gap, toolbar.getWidth() - minHint);
    if (! shown[(std::size_t) P::tempo] || ! shown[(std::size_t) P::status])
    {
        minHint = menuWidth + 8;
        shown = sb::fit (widths, sb::gap, toolbar.getWidth() - minHint);
    }
    int used = 0;
    for (int i = 0; i < sb::partCount; ++i)
        used += shown[(std::size_t) i] ? widths[(std::size_t) i] + sb::gap : 0;
    // (the hint column keeps FL's width, so the transport capsule sits where FL's transport does: owner decision SG-A)
    const auto hintWidth = juce::jlimit (minHint, juce::jmax (minHint, 340), toolbar.getWidth() - used);

    // 1. the menu over the hint bar, as in the FL layout
    {
        auto column = toolbar.removeFromLeft (hintWidth);
        const auto menu = column.withY (row1).withHeight (h).removeFromLeft (juce::jmin (menuWidth + 8, column.getWidth()));
        put (menuBar, menu);
        titleArea = column.withY (row1).withHeight (h).withTrimmedLeft (menu.getWidth() + 12).withTrimmedRight (4);
        if (titleArea.getWidth() < 60)
            titleArea = {};
        placeHintBar (column.withY (row2).withHeight (h).reduced (0, 1));
        fixedToolbarArea = juce::Rectangle<int> (column.getX() - 4, 0, column.getWidth() + 8, toolbarHeight);
    }

    const auto capY = bar.getCentreY() - sb::capsuleHeight / 2;
    auto capsuleRow = [&] (int x, int width) { return juce::Rectangle<int> (x, capY, width, sb::capsuleHeight); };
    // a capsule of segments, left to right
    auto capsule = [&] (int x, std::initializer_list<std::pair<juce::Component*, int>> segments) {
        auto at = x;
        bool first = true;
        for (const auto& [c, w] : segments)
        {
            if (! first)
                lookDividers.push_back ({ at, capY, sb::divider, sb::capsuleHeight });
            put (*c, capsuleRow (at, w));
            at += w;
            first = false;
        }
        lookCapsules.push_back (capsuleRow (x, at - x));
    };

    for (const auto part : { P::transport, P::tempo, P::tuner, P::key, P::master, P::file, P::link })
    {
        if (! shown[(std::size_t) part])
            continue;
        toolbar.removeFromLeft (sb::gap);
        const auto r = toolbar.removeFromLeft (widths[(std::size_t) part]);
        switch (part)
        {
            case P::transport: // play | stop | record | the time (BandLab's transport capsule, at FL's transport place)
                capsule (r.getX(), { { &playButton, sb::segment }, { &stopButton, sb::segment }, { &recordButton, sb::segment }, { &cycleButton, sb::segment },
                                     { &timeDisplay, sb::timeWidth } });
                transportArea = {};
                break;
            case P::tempo: // metronome | tempo | time signature (BandLab's)
                capsule (r.getX(), { { &metronomeButton, sb::segment }, { &metronomeMenuButton, sb::chevronWidth }, { &tempoDisplay, sb::tempoWidth },
                                     { &signatureBox, sb::signatureWidth } });
                break;
            case P::tuner: // round 190ke: one segment, its own capsule
                if (auto* tuner = simpleWorkflow->tunerComponent())
                    capsule (r.getX(), { { tuner, sb::segment } });
                break;
            case P::key: put (keyPanel, capsuleRow (r.getX(), r.getWidth())); break;
            case P::master:
            {
                const auto both = r.withY (row1).withHeight (2 * h + 2);
                put (masterVolume, capsuleRow (r.getX(), toolbar::MasterKnob::sliderWidth));
                put (masterPitch, both.withTrimmedLeft (toolbar::MasterKnob::sliderWidth + 8));
                break;
            }
            case P::file:
            {
                int i = 0;
                for (auto* b : { &newButton, &openButton, &saveButton, &exportButton, &undoButton, &redoButton })
                {
                    put (*b, juce::Rectangle<int> (r.getX() + (i % 3) * (h + 2), i < 3 ? row1 : row2, h, h));
                    ++i;
                }
                break;
            }
            case P::link: put (linkToggle, r.withY (row1 + h / 2 + 1).withHeight (h).reduced (0, 1)); break;
            default: break;
        }
    }

    // the right end: "⋯", the save status, Finish
    auto right = toolbar;
    if (shown[(std::size_t) P::finish])
    {
        if (auto* finish = simpleWorkflow->finishComponent())
            put (*finish, right.removeFromRight (sb::finishWidth).withSizeKeepingCentre (sb::finishWidth, 32));
        right.removeFromRight (sb::gap);
    }
    if (shown[(std::size_t) P::status])
    {
        put (saveStatus, right.removeFromRight (sb::statusWidth).withSizeKeepingCentre (sb::statusWidth, 32));
        right.removeFromRight (4);
    }
    put (moreButton, right.removeFromRight (sb::moreWidth).withSizeKeepingCentre (sb::moreWidth, sb::moreWidth));
}

// --- Clip-launch: Live's control bar -----------------------------------------------------------------------------------

void MainComponent::layoutLiveControlBar (juce::Rectangle<int> bar, int menuWidth)
{
    using toolbarlayout::Group;
    using I = lb::Item;
    hideToolbarParts();
    dividers.clear();
    panelBounds = {};
    snapLabelArea = patternLabelArea = transportArea = modeArea = {};
    lookCapsules.clear();
    lookDividers.clear();
    auto put = [] (juce::Component& c, juce::Rectangle<int> r) {
        c.setVisible (true);
        c.setBounds (r);
    };

    // the menus on their own row (Live: 23 px), the hint bar after them, the project's name at the right
    lookMenuRow = bar.withHeight (lb::menuRow);
    lookBand = bar.withTrimmedTop (lb::menuRow);
    {
        auto row = lookMenuRow.reduced (8, 0);
        const auto menu = row.removeFromLeft (juce::jmin (menuWidth + 8, row.getWidth()));
        put (menuBar, menu);
        row.removeFromLeft (12);
        titleArea = row.removeFromRight (juce::jmin (320, row.getWidth() / 3));
        if (titleArea.getWidth() < 60)
            titleArea = {};
        placeHintBar (row.withTrimmedRight (12).withWidth (juce::jmin (900, row.getWidth() - 12)).reduced (0, 2));
        fixedToolbarArea = lookMenuRow;
    }
    if (! toolbarLayout.visible)
        return;

    std::array<int, lb::itemCount> widths {};
    for (int i = 0; i < lb::itemCount; ++i)
        widths[(std::size_t) i] = lb::defaultWidth ((I) i);
    widths[(std::size_t) I::link] = toolbarLayout.isShown (Group::link) ? toolbar::LinkToggle::idealWidth (lb::box) : 0;
    widths[(std::size_t) I::key] = toolbarLayout.isShown (Group::key) ? lb::defaultWidth (I::key) : 0;
    widths[(std::size_t) I::cpu] = toolbarLayout.isShown (Group::meters) ? lb::defaultWidth (I::cpu) : 0;
    widths[(std::size_t) I::metronome] = 24; // (the ring-and-dot box; its options are on the right-click, as FL's)
    liveShown = lb::fit (widths, bar.getWidth() - 16 - 3 * lb::sectionGap);

    const auto y = lookBand.getY() + (lookBand.getHeight() - lb::box) / 2;
    auto component = [this] (I item) -> juce::Component* {
        switch (item)
        {
            case I::link: return &linkToggle;
            case I::browser: return &browserToggleButton;
            case I::tap: return &tapTempoButton;
            case I::tempo: return &tempoDisplay;
            case I::nudgeDown: return &nudgeDownButton;
            case I::nudgeUp: return &nudgeUpButton;
            case I::signature: return &signatureBox;
            case I::metronome: return &metronomeButton;
            case I::quantisation: return &quantBox;
            case I::key: return &keyPanel;
            case I::backToArrangement: return &backToArrangementButton;
            case I::position: return &timeDisplay;
            case I::play: return &playButton;
            case I::stop: return &stopButton;
            case I::record: return &recordButton;
            case I::overdub: return &blendButton;
            case I::capture: return &captureButton;
            case I::sessionRecord: return &sessionRecordButton;
            case I::punch: return &punchButton;
            case I::loopSwitch: return &liveLoopButton;
            case I::punchOut: return &punchOutButton;
            case I::draw: return &drawModeButton;
            case I::computerKeys: return &typingPianoButton;
            case I::keyMap: return &keyMapButton;
            case I::midiMap: return &midiMapButton;
            case I::cpu: return &perfMeter;
            case I::more: return &moreButton;
            case I::sessionView: return &sessionViewButton;
            case I::arrangementView: return &arrangementViewButton;
            case I::count_: break;
        }
        return nullptr;
    };
    auto width = [&] (I item) { return liveShown[(std::size_t) item] ? widths[(std::size_t) item] : 0; };
    // Live's spacing (CL-2, measured): 2 px between boxes, a wider gap after the browser toggle, before the key and
    // around the transport glyphs
    auto extraBefore = [] (I item) {
        return item == I::tap || item == I::key ? lb::sectionGap : item == I::play || item == I::overdub ? 8 : item == I::sessionView ? lb::sectionGap : 0;
    };
    auto runWidth = [&] (I from, I to) {
        int w = 0;
        bool first = true;
        for (int i = (int) from; i <= (int) to; ++i)
            if (const auto iw = width ((I) i); iw > 0)
            {
                w += iw + (first ? 0 : lb::gap + extraBefore ((I) i));
                first = false;
            }
        return w;
    };
    auto placeRun = [&] (I from, I to, int x) {
        bool first = true;
        for (int i = (int) from; i <= (int) to; ++i)
            if (const auto iw = width ((I) i); iw > 0)
            {
                if (! first)
                    x += lb::gap + extraBefore ((I) i);
                if (auto* c = component ((I) i))
                    put (*c, { x, y, iw, lb::box });
                x += iw;
                first = false;
            }
        return x;
    };

    const auto left = bar.getX() + 8, rightEnd = bar.getRight() - 8;
    const auto leftEnd = placeRun (I::link, I::key, left);
    const auto rightW = runWidth (I::draw, I::arrangementView);
    placeRun (I::draw, I::arrangementView, rightEnd - rightW);
    const auto centreW = runWidth (I::backToArrangement, I::sessionRecord);
    const auto punchW = runWidth (I::punch, I::punchOut); // (round 190lx: Punch-In, Loop, Punch-Out)
    const auto tail = punchW > 0 ? lb::sectionGap + punchW : 0;
    // the transport where Live's is: Live centres its transport group together with the loop / punch group after it
    // (CL-2: 716..1011 and 1033..1267 at 1920 px); LYNX has only punch there, so the group keeps Live's loop width
    auto centreX = bar.getCentreX() - (centreW + lb::sectionGap + lb::loopSectionWidth) / 2;
    centreX = juce::jmin (centreX, rightEnd - rightW - lb::sectionGap - centreW - tail);
    centreX = juce::jmax (centreX, leftEnd + lb::sectionGap);
    const auto centreEnd = placeRun (I::backToArrangement, I::sessionRecord, centreX);
    if (punchW > 0)
        placeRun (I::punch, I::punchOut, centreEnd + lb::sectionGap);
}

// --- paint --------------------------------------------------------------------------------------------------------------

void MainComponent::paintLookToolbar (juce::Graphics& g)
{
    using namespace theme;
    const auto bar = juce::Rectangle<int> (0, 0, getWidth(), toolbarHeight);
    if (toolbarLook == Look::live)
    {
        g.setColour (liveChrome);
        g.fillRect (bar);
        g.setColour (liveSeparator);
        g.fillRect (bar.withHeight (1).withY (bar.getBottom() - 1));
    }
    else
    {
        g.setColour (colour::bg3);
        g.fillRect (bar);
        g.setColour (colour::border);
        g.fillRect (bar.withHeight (1).withY (bar.getBottom() - 1));
        // BandLab's capsules: bg4, fully round, 1 px darker lines between the segments (SG-1)
        for (const auto& c : lookCapsules)
        {
            g.setColour (colour::bg4);
            g.fillRoundedRectangle (c.toFloat(), (float) sb::radius);
        }
        g.setColour (colour::bg2);
        for (const auto& d : lookDividers)
            g.fillRect (d);
    }

    // the hint bar's inset well (FL's look: bg1; on Live's chrome a little darker than the band)
    if (! hintBar.getBounds().isEmpty())
    {
        g.setColour (toolbarLook == Look::live ? juce::Colour (0xff222222) : colour::bg1);
        g.fillRoundedRectangle (hintBar.getBounds().toFloat(), radius::md);
    }

    if (! titleArea.isEmpty())
    {
        const auto people = live != nullptr ? liveui::paintPresence (g, titleArea, *live) : 0;
        const auto name = projectName.isNotEmpty() ? projectName : tr ("Untitled");
        g.setFont (font (type::small, true));
        g.setColour (toolbarLook == Look::live ? juce::Colour (0xffb5b5b5) : colour::textSecondary);
        if (const auto rest = titleArea.withTrimmedLeft (people > 0 ? people + 10 : 0); rest.getWidth() >= 50)
            g.drawFittedText ((engine.isModified() ? juce::String ("* ") : juce::String()) + name
                                  + (engine.pluginsOff() ? " " + tr ("(third-party plugins off)") : juce::String()),
                              rest, juce::Justification::centredRight, 1, 0.8f);
    }
}

// --- the fields' states (from the main window's timer; each repaints only on a change) ------------------------------------

void MainComponent::updateLookFields()
{
    if (toolbarLook == Look::fl)
        return;
    signatureBox.update();
    if (toolbarLook == Look::simple)
    {
        // round 190jj: BandLab's master meter behind the master volume (its rail repaints only when a bar moved a pixel)
        if (masterVolume.isVisible())
            masterVolume.tickMeter (juce::Time::getMillisecondCounterHiRes());
        if (const auto on = engine.getSong().loopPlays(); cycleButton.getToggleState() != on) // round 190ld: lit while it cycles
            cycleButton.setToggleState (on, juce::dontSendNotification);
        if (++saveStatusTicks >= 60) // every 2 s at the 30 Hz timer (the minutes in "Backed up 2 min ago")
        {
            saveStatusTicks = 0;
            refreshSaveStatus();
        }
        return;
    }
    quantBox.update();
    // Back to Arrangement lights while a track plays a clip instead of the Playlist (the clip grid's own rule)
    auto taken = false;
    if (const auto* state = engine.launcherState())
        for (int t = 0; t < launcher::maxTracks && ! taken; ++t)
            taken = state->tracks[(std::size_t) t].taken.load (std::memory_order_relaxed);
    if (backToArrangementButton.getToggleState() != taken)
        backToArrangementButton.setToggleState (taken, juce::dontSendNotification);
    if (const auto on = engine.getSong().loopPlays(); liveLoopButton.getToggleState() != on) // round 190lx (DL-61)
        liveLoopButton.setToggleState (on, juce::dontSendNotification);
    if (const auto on = engine.isSessionRecording(); sessionRecordButton.getToggleState() != on)
        sessionRecordButton.setToggleState (on, juce::dontSendNotification);
    const auto view = playlistArea.getGridView();
    const auto session = view != PlaylistArea::GridView::song, arrangement = view != PlaylistArea::GridView::grid;
    if (sessionViewButton.getToggleState() != session)
        sessionViewButton.setToggleState (session, juce::dontSendNotification);
    if (arrangementViewButton.getToggleState() != arrangement)
        arrangementViewButton.setToggleState (arrangement, juce::dontSendNotification);
    if (const auto browserOn = workspace.isPanelVisible (Panel::browser); browserToggleButton.getToggleState() != browserOn)
        browserToggleButton.setToggleState (browserOn, juce::dontSendNotification);
}

void MainComponent::refreshSaveStatus()
{
    if (saveStatusFrozen || toolbarLook != Look::simple)
        return;
    const auto backedUp = lastBackupProject == engine.getSong().projectId && lastBackupMs > 0;
    const auto state = toolbarlayout::saveState (engine.isModified(), currentFile.existsAsFile(), backedUp);
    const auto text = state == toolbarlayout::SaveState::fresh ? tr ("Nothing to save yet") : simpleSaveStatus();
    saveStatus.set (state, text);
}

void MainComponent::setSaveStatusForSnapshot (toolbarlayout::SaveState state)
{
    using S = toolbarlayout::SaveState;
    const auto now = juce::Time::currentTimeMillis();
    saveStatusFrozen = true;
    saveStatus.set (state, state == S::fresh      ? tr ("Nothing to save yet")
                           : state == S::unsaved  ? simpleSaveStatusText (true, false, 0, now)
                           : state == S::backedUp ? simpleSaveStatusText (true, true, now - 125000, now)
                                                  : simpleSaveStatusText (false, true, 0, now));
}

juce::Component* MainComponent::toolbarPartForSnapshot (const juce::String& name)
{
    const std::pair<const char*, juce::Component*> parts[] {
        { "play", &playButton }, { "stop", &stopButton }, { "record", &recordButton }, { "metronome", &metronomeButton }, { "tap", &tapTempoButton },
        { "tempo", &tempoDisplay }, { "signature", &signatureBox }, { "quantisation", &quantBox }, { "key", &keyPanel }, { "link", &linkToggle },
        { "browser", &browserToggleButton }, { "backToArrangement", &backToArrangementButton }, { "position", &timeDisplay },
        { "overdub", &blendButton }, { "capture", &captureButton }, { "sessionRecord", &sessionRecordButton }, { "punch", &punchButton }, { "liveLoop", &liveLoopButton }, { "punchOut", &punchOutButton }, { "nudgeDown", &nudgeDownButton }, { "nudgeUp", &nudgeUpButton },
        { "draw", &drawModeButton }, { "computerKeys", &typingPianoButton }, { "keyMap", &keyMapButton }, { "midiMap", &midiMapButton },
        { "cpu", &perfMeter }, { "more", &moreButton }, { "sessionView", &sessionViewButton }, { "arrangementView", &arrangementViewButton },
        { "status", &saveStatus }, { "masterVolume", &masterVolume }, { "masterPitch", &masterPitch }, { "undo", &undoButton }, { "redo", &redoButton },
        { "cycle", &cycleButton }, { "metronomeMenu", &metronomeMenuButton }, { "countIn", &countInButton }, { "waitInput", &waitInputButton }, { "midiLight", &midiLight } // (round 190ld)
    };
    if (name == "finish")
        return simpleWorkflow != nullptr ? simpleWorkflow->finishComponent() : nullptr;
    for (const auto& [id, c] : parts)
        if (name == id)
            return c;
    return nullptr;
}

// round 190kn (BandLab, DS-11: the redo icon dims when there is nothing to redo; FL Studio Mobile shows Undo only after a
// change): the Simple top bar greys Undo / Redo; the other looks keep them always on. setEnabled repaints only on a change.
void MainComponent::updateUndoRedoEnabled()
{
    const auto simpleBar = toolbarLook == Look::simple;
    undoButton.setEnabled (! simpleBar || engine.canUndo());
    redoButton.setEnabled (! simpleBar || engine.canRedo());
}

void MainComponent::transportTickForSnapshot()
{
    updateUndoRedoEnabled();
    const auto playing = engine.getTransport().playing.load() || engine.isLinkWaiting();
    playButton.setToggleState (playing, juce::dontSendNotification);
    refreshModeButton();
    updateTransportStates (playing);
    timeDisplay.update();
    if (toolbarLook != Look::fl)
        updateLookFields();
}

void MainComponent::setSessionButtonRowShown (bool shown)
{
    sessionButtonRowShown = shown;
    if (clipGridView != nullptr)
        setClipGridButtonRowShown (*clipGridView, toolbarLook != Look::live || shown);
    saveUiSettings();
}

// --- "⋯" --------------------------------------------------------------------------------------------------------------

juce::PopupMenu MainComponent::makeToolbarMoreMenu()
{
    using toolbarlayout::Group;
    using I = lb::Item;
    juce::PopupMenu menu;
    const auto live = toolbarLook == Look::live;
    auto liveDropped = [&] (I item) { return live && ! liveShown[(std::size_t) item]; };

    // the control-bar items the window had no room for
    if (live)
    {
        bool any = false;
        auto item = [&] (I i, const juce::String& text, bool on, std::function<void()> act) {
            if (! liveDropped (i))
                return;
            if (! any)
                menu.addSectionHeader (tr ("Control bar"));
            any = true;
            menu.addItem (text, true, on, std::move (act));
        };
        item (I::tap, tr ("Tap tempo"), false, [this] { tapTempoButton.triggerClick(); });
        item (I::overdub, tr ("Blend recorded notes"), blendButton.getToggleState(), [this] { runAction ("record.blend"); });
        item (I::capture, tr ("Capture MIDI: the notes just played without recording into the pattern"), false, [this] { runAction ("record.captureMidi"); });
        item (I::punch, tr ("Punch-In"), punchButton.getToggleState(), [this] { runAction ("record.punchIn"); });
        item (I::loopSwitch, tr ("Loop"), liveLoopButton.getToggleState(), [this] { toggleLiveLoop(); });
        item (I::punchOut, tr ("Punch-Out"), punchOutButton.getToggleState(), [this] { runAction ("record.punchOut"); });
        item (I::computerKeys, tr ("Typing keyboard to piano"), typingPianoButton.getToggleState(), [this] { runAction ("view.typingPiano"); });
        item (I::draw, tr ("Draw Mode"), drawmode::isOn(), [] { drawmode::toggle(); });
        item (I::keyMap, tr ("Key Map Mode: map computer keys to clips, scenes, buttons and controls (Ctrl+K)"), getMapMode() == remotemap::Mode::key,
              [this] { keyMapButton.triggerClick(); });
        item (I::midiMap, tr ("MIDI Map Mode: map MIDI controls to clips, scenes, buttons and controls (Ctrl+M)"), getMapMode() == remotemap::Mode::midi,
              [this] { midiMapButton.triggerClick(); });
        item (I::browser, "Browser", workspace.isPanelVisible (Panel::browser), [this] { runAction ("panel.browser"); });
    }

    // Pattern / Song (FL's switch in front of its transport)
    menu.addSectionHeader (tr ("Transport"));
    const auto songMode = engine.getSong().mode == PlayMode::song;
    menu.addItem (tr ("Pattern mode: plays the current pattern in a loop") + "  (L)", true, ! songMode, [this] { patternModeButton.triggerClick(); });
    menu.addItem (tr ("Song mode: plays the Playlist arrangement") + "  (L)", true, songMode, [this] { songModeButton.triggerClick(); });

    // the recording options (FL's recording panel)
    juce::PopupMenu recording;
    recording.addItem (tr ("Metronome"), true, metronomeButton.getToggleState(), [this] { metronomeButton.triggerClick(); });
    recording.addItem (tr ("Wait for input"), true, waitInputButton.getToggleState(), [this] { runAction ("record.waitForInput"); });
    recording.addItem (tr ("Count-in"), true, countInButton.getToggleState(), [this] { runAction ("record.countIn"); });
    recording.addItem (tr ("Loop record"), true, loopRecordButton.getToggleState(), [this] { runAction ("record.loop"); });
    recording.addItem (tr ("Step edit"), true, stepEditButton.getToggleState(), [this] { runAction ("record.stepEdit"); });
    if (! live)
    {
        recording.addItem (tr ("Blend recorded notes"), true, blendButton.getToggleState(), [this] { runAction ("record.blend"); });
        recording.addItem (tr ("Punch in / out"), true, punchButton.getToggleState(), [this] { runAction ("record.punch"); });
        recording.addItem (tr ("Typing keyboard to piano"), true, typingPianoButton.getToggleState(), [this] { runAction ("view.typingPiano"); });
    }
    recording.addSeparator();
    recording.addSubMenu (tr ("Recording options: what to record"), buildRecordMenu());
    menu.addSubMenu (tr (toolbarlayout::groupName (Group::record)), recording);

    // global snap and the pattern selector
    juce::PopupMenu snap;
    for (int i = 0; i < gridBox.getNumItems(); ++i)
        snap.addItem (gridBox.getItemText (i), true, gridBox.getSelectedItemIndex() == i, [this, i] { gridBox.setSelectedItemIndex (i); });
    menu.addSubMenu (tr ("Snap"), snap);
    juce::PopupMenu patterns;
    const auto& song = engine.getSong();
    for (int p = 0; p < (int) song.patterns.size() && p < 200; ++p)
        patterns.addItem (juce::String::fromUTF8 (song.patterns[(std::size_t) p].name.c_str()), true, song.currentPattern == p, [this, p] { selectPattern (p); });
    patterns.addSeparator();
    patterns.addSubMenu (tr ("Pattern"), makePatternMenu());
    menu.addSubMenu (tr ("Patterns"), patterns);

    // the windows and the file buttons
    juce::PopupMenu windows;
    for (const auto& [name, action] : { std::pair<const char*, const char*> { "Playlist", "panel.playlist" }, { "Channel Rack", "panel.channelRack" },
                                        { "Piano Roll", "panel.pianoRoll" }, { "Mixer", "panel.mixer" }, { "Browser", "panel.browser" } })
        windows.addItem (juce::String (name) + shortcutText (action), [this, action = std::string (action)] { runAction (action); });
    windows.addItem (tr ("Plugin picker") + shortcutText ("view.pluginPicker"), [this] { togglePluginPicker(); });
    menu.addSubMenu (tr (toolbarlayout::groupName (Group::windows)), windows);
    juce::PopupMenu file;
    file.addItem (tr ("New") + shortcutText ("file.new"), [this] { runAction ("file.new"); });
    file.addItem (tr ("Open...") + shortcutText ("file.open"), [this] { runAction ("file.open"); });
    file.addItem (tr ("Save") + shortcutText ("file.save"), [this] { runAction ("file.save"); });
    file.addItem (tr ("Export") + shortcutText ("file.export"), [this] { runAction ("file.export"); });
    file.addSeparator();
    file.addItem (tr ("Undo") + shortcutText ("edit.undo"), [this] { runAction ("edit.undo"); });
    file.addItem (tr ("Redo") + shortcutText ("edit.redo"), [this] { runAction ("edit.redo"); });
    menu.addSubMenu (tr (toolbarlayout::groupName (Group::file)), file);

    // the Session view's parts that left the footer row (Live: View menu, and the toggles under the Session)
    if (live && clipGridView != nullptr)
    {
        menu.addSectionHeader (tr ("Clip grid"));
        const auto grooves = clipGridGroovePoolShown (*clipGridView);
        menu.addItem (tr ("Groove Pool"), true, grooves, [this, grooves] {
            if (clipGridView != nullptr)
                setClipGridGroovePoolShown (*clipGridView, ! grooves);
        });
        juce::PopupMenu mixer;
        addClipGridMixerViewItems (*clipGridView, mixer);
        menu.addSubMenu (tr ("Session mixer"), mixer);
        menu.addItem (tr ("Show the Session button row"), true, sessionButtonRowShown, [this] { setSessionButtonRowShown (! sessionButtonRowShown); });
    }

    menu.addSeparator();
    menu.addItem (tr ("Customize toolbar..."), [this] { showCustomizeToolbar(); });
    return menu;
}
} // namespace lynx
