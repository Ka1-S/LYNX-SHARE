#include "ui/SimpleWorkflow.h"

#include <algorithm>

#include "core/FirstSongTour.h"
#include "core/KeyFollow.h"
#include "core/LoopStarter.h"
#include "core/MidiImport.h"
#include "engine/AudioEngine.h"
#include "engine/MidiFileIo.h"
#include "ui/FileChoosers.h"
#include "ui/FinishDock.h"
#include "ui/FirstSongTour.h"
#include "ui/I18n.h"
#include "ui/LoopStarterWindow.h"
#include "ui/QuickPick.h"
#include "ui/SimpleDock.h"
#include "ui/StartFromPanel.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
using simpletracks::Kind;

// --- the tiles' pictures: LYNX's own simple line drawings in a 1×1 box ------------------------------------------------
enum class Glyph { voice, instrument, drums, sampler, guitar, import, loops };

juce::Path glyphPath (Glyph glyph)
{
    juce::Path p;
    switch (glyph)
    {
        case Glyph::voice: // a microphone on its stand
            p.addRoundedRectangle (0.36f, 0.08f, 0.28f, 0.48f, 0.14f);
            p.startNewSubPath (0.24f, 0.44f);
            p.cubicTo (0.24f, 0.78f, 0.76f, 0.78f, 0.76f, 0.44f);
            p.startNewSubPath (0.5f, 0.72f);
            p.lineTo (0.5f, 0.9f);
            p.startNewSubPath (0.34f, 0.9f);
            p.lineTo (0.66f, 0.9f);
            break;
        case Glyph::instrument: // three octave keys
            p.addRectangle (0.1f, 0.22f, 0.8f, 0.56f);
            for (float x : { 0.3f, 0.5f, 0.7f })
            {
                p.startNewSubPath (x, 0.22f);
                p.lineTo (x, 0.78f);
            }
            for (float x : { 0.25f, 0.45f, 0.65f })
                p.addRectangle (x, 0.22f, 0.1f, 0.3f);
            break;
        case Glyph::drums: // a drum with two sticks
            p.addEllipse (0.16f, 0.4f, 0.68f, 0.2f);
            p.startNewSubPath (0.16f, 0.5f);
            p.lineTo (0.16f, 0.78f);
            p.quadraticTo (0.5f, 0.94f, 0.84f, 0.78f);
            p.lineTo (0.84f, 0.5f);
            p.startNewSubPath (0.28f, 0.1f);
            p.lineTo (0.46f, 0.42f);
            p.startNewSubPath (0.72f, 0.1f);
            p.lineTo (0.54f, 0.42f);
            break;
        case Glyph::sampler: // a waveform over a key
        {
            const float h[] { 0.1f, 0.24f, 0.4f, 0.18f, 0.32f, 0.12f, 0.06f };
            for (int i = 0; i < 7; ++i)
            {
                const auto x = 0.14f + 0.12f * (float) i;
                p.startNewSubPath (x, 0.42f - h[i]);
                p.lineTo (x, 0.42f + h[i]);
            }
            p.addRectangle (0.14f, 0.7f, 0.72f, 0.16f);
            break;
        }
        case Glyph::guitar: // a body and a neck
            p.addEllipse (0.1f, 0.5f, 0.34f, 0.34f);
            p.addEllipse (0.22f, 0.36f, 0.26f, 0.26f);
            p.startNewSubPath (0.38f, 0.56f);
            p.lineTo (0.84f, 0.1f);
            p.addEllipse (0.24f, 0.62f, 0.08f, 0.08f);
            break;
        case Glyph::import: // an arrow into a tray
            p.startNewSubPath (0.5f, 0.1f);
            p.lineTo (0.5f, 0.6f);
            p.startNewSubPath (0.32f, 0.44f);
            p.lineTo (0.5f, 0.62f);
            p.lineTo (0.68f, 0.44f);
            p.startNewSubPath (0.14f, 0.62f);
            p.lineTo (0.14f, 0.86f);
            p.lineTo (0.86f, 0.86f);
            p.lineTo (0.86f, 0.62f);
            break;
        case Glyph::loops: // a circle of arrows
            p.addCentredArc (0.5f, 0.5f, 0.32f, 0.32f, 0.0f, 0.4f, 5.6f, true);
            p.startNewSubPath (0.62f, 0.12f);
            p.lineTo (0.7f, 0.22f);
            p.lineTo (0.56f, 0.28f);
            break;
    }
    return p;
}

void drawGlyph (juce::Graphics& g, Glyph glyph, juce::Rectangle<float> box, juce::Colour tint)
{
    g.setColour (tint.withAlpha (0.2f));
    g.fillRoundedRectangle (box, theme::radius::lg);
    g.setColour (tint.withAlpha (0.55f));
    g.drawRoundedRectangle (box.reduced (0.5f), theme::radius::lg, 1.0f);
    const auto inner = box.reduced (box.getWidth() * 0.18f);
    auto path = glyphPath (glyph);
    path.applyTransform (juce::AffineTransform::scale (inner.getWidth(), inner.getHeight()).translated (inner.getX(), inner.getY()));
    g.setColour (theme::colour::textPrimary);
    g.strokePath (path, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// A tile: picture, name, one line of help. Clicking anywhere on it runs `onClick`.
class Tile final : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    Tile (Glyph g, juce::Colour c, juce::String n, juce::String h) : glyph (g), tint (c), name (std::move (n)), help (std::move (h))
    {
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTitle (name);
        setDescription (help);
        setTooltip (help);
    }

    std::function<void()> onClick;
    // the part of the tile on the right that holds other controls (the Guitar | Bass switch): the text stops before it
    int rightReserve = 0;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const auto hover = isMouseOver (true), down = isMouseButtonDown();
        g.setColour (down ? theme::colour::bg5 : hover ? theme::colour::bg4 : theme::colour::bg3);
        g.fillRoundedRectangle (r, theme::radius::lg);
        g.setColour (hasKeyboardFocus (false) ? theme::colour::accent : hover ? theme::colour::borderStrong : theme::colour::border);
        g.drawRoundedRectangle (r.reduced (0.5f), theme::radius::lg, 1.0f);
        const auto side = juce::jmin (44.0f, r.getHeight() - 16.0f);
        drawGlyph (g, glyph, { r.getX() + 10.0f, r.getCentreY() - side / 2.0f, side, side }, tint);
        auto text = getLocalBounds().withTrimmedLeft ((int) side + 22).withTrimmedRight (8 + rightReserve).reduced (0, 8);
        g.setColour (theme::colour::textPrimary);
        g.setFont (theme::font (theme::type::title, true));
        g.drawFittedText (name, text.removeFromTop (20), juce::Justification::centredLeft, 1, 0.85f);
        g.setColour (theme::colour::textSecondary);
        g.setFont (theme::font (theme::type::small));
        g.drawFittedText (help, text, juce::Justification::topLeft, 2, 0.9f);
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override { repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        repaint();
        if (e.mouseWasClicked() && getLocalBounds().contains (e.getPosition()) && onClick)
            onClick();
    }
    bool keyPressed (const juce::KeyPress& key) override
    {
        if ((key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey) && onClick)
        {
            onClick();
            return true;
        }
        return false;
    }
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

private:
    Glyph glyph;
    juce::Colour tint;
    juce::String name, help;
};

juce::Colour kindColour (Kind kind) { return juce::Colour (simpletracks::defaultColour (kind)); }

// The name a new track of `kind` takes, in the UI's language
std::string trackNameFor (Kind kind)
{
    switch (kind)
    {
        case Kind::voice: return tr ("Voice").toStdString();
        case Kind::instrument: return tr ("Instrument").toStdString();
        case Kind::drums: return tr ("Drums").toStdString();
        case Kind::sampler: return tr ("Sampler").toStdString();
        case Kind::guitar: return tr ("Guitar").toStdString();
        case Kind::bass: return trc ("@instrument: Bass").toStdString();
        case Kind::audio: break;
    }
    return tr ("Audio").toStdString();
}
} // namespace

// --- the chooser: the tiles, then (after Drums) the starter beats -----------------------------------------------------
class SimpleWorkflow::Chooser final : public juce::Component
{
public:
    explicit Chooser (SimpleWorkflow& w)
        : owner (w),
          voice (Glyph::voice, kindColour (Kind::voice), tr ("Voice / audio"), tr ("Record your voice or any sound from a microphone.")),
          instrument (Glyph::instrument, kindColour (Kind::instrument), tr ("Instrument"), tr ("Play a sound with the computer keys, the mouse or a MIDI keyboard.")),
          drums (Glyph::drums, kindColour (Kind::drums), tr ("Drums"), tr ("Make a beat on a step grid, or start from a starter beat.")),
          sampler (Glyph::sampler, kindColour (Kind::sampler), tr ("Sampler"), tr ("Turn any sound into an instrument you play on keys.")),
          guitar (Glyph::guitar, kindColour (Kind::guitar), tr ("Guitar / bass"), tr ("Plug in a guitar or a bass and play it through the track's effects.")),
          import (Glyph::import, kindColour (Kind::audio), tr ("Import"), tr ("Bring in audio or MIDI files: one track each.")),
          loops (Glyph::loops, kindColour (Kind::audio), tr ("Loops"), tr ("Browse loops and drag them onto the timeline."))
    {
        setWantsKeyboardFocus (true);
        getProperties().set ("lynxKeepsKeys", true); // (round 190fc: its tiles' keys while it is up)
        title.setText (tr ("New track"), juce::dontSendNotification);
        title.setFont (theme::font (theme::type::display, true));
        title.setJustificationType (juce::Justification::centred);
        footer.setText (tr ("Each track gets its own channel and mixer track."), juce::dontSendNotification);
        footer.setFont (theme::font (theme::type::small));
        footer.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        footer.setJustificationType (juce::Justification::centred);
        close.setButtonText (juce::String::fromUTF8 ("\xc3\x97"));
        close.setTooltip (tr ("Close"));
        close.onClick = [this] { owner.closeByHand(); };
        // round 190ch: a new project from LYNX's demo song or a template, with a preview
        startFrom.setButtonText (tr ("Start from a demo song or a template..."));
        startFrom.onClick = [this] { owner.showStartFrom(); };
        // round 190dv: the first-song tour (continues a paused one)
        tourButton.setButtonText (tr ("First-song tour"));
        tourButton.setTooltip (tr ("A short guided tour that makes a first song with you"));
        tourButton.onClick = [this] { owner.startOrContinueTour(); };
        // round 190dv: what the tour's ring finds (ui/FirstSongTour.h)
        voice.setComponentID ("simple.tile.voice");
        instrument.setComponentID ("simple.tile.instrument");
        drums.setComponentID ("simple.tile.drums");
        loops.setComponentID ("simple.tile.loops");

        voice.onClick = [this] { owner.addAudio (Kind::voice); };
        // one click gives a sound to play at once (LYNX's own synth); "More..." lists every instrument, plugin and Soundfont
        instrument.onClick = [this] { owner.addDefaultInstrument(); };
        instrument.rightReserve = 104;
        moreInstruments.setButtonText (tr ("More..."));
        moreInstruments.onClick = [this] { owner.chooseInstrument (moreInstruments); };
        instrument.addAndMakeVisible (moreInstruments);
        drums.onClick = [this] { showStarterPage(); };
        sampler.onClick = [this] { owner.chooseSample(); };
        guitar.onClick = [this] { owner.addAudio (bassButton.getToggleState() ? Kind::bass : Kind::guitar); };
        import.onClick = [this] { owner.importFiles(); };
        loops.onClick = [this] {
            if (owner.host.showLoops)
                owner.host.showLoops();
        };
        guitar.rightReserve = 168;
        guitarButton.setButtonText (tr ("Guitar"));
        bassButton.setButtonText (trc ("@instrument: Bass"));
        for (auto* b : { &guitarButton, &bassButton })
        {
            b->setClickingTogglesState (true);
            b->setRadioGroupId (1184);
            guitar.addAndMakeVisible (*b);
        }
        guitarButton.setToggleState (true, juce::dontSendNotification);

        for (auto* c : std::initializer_list<juce::Component*> { &title, &close, &voice, &instrument, &drums, &sampler, &guitar, &import, &loops, &footer, &startFrom, &tourButton })
            addAndMakeVisible (*c);

        // the starter-beat page
        starterTitle.setText (tr ("Starter beat"), juce::dontSendNotification);
        starterTitle.setFont (theme::font (theme::type::display, true));
        starterTitle.setJustificationType (juce::Justification::centred);
        back.setButtonText (juce::String::fromUTF8 ("\xe2\x80\xb9 ") + tr ("Back"));
        back.onClick = [this] { showTilesPage(); };
        useTempo.setButtonText (tr ("Use the suggested tempo"));
        useTempo.setToggleState (true, juce::dontSendNotification);
        for (auto* c : std::initializer_list<juce::Component*> { &starterTitle, &back, &useTempo })
            addChildComponent (*c);
    }

    void showStarterPage()
    {
        if (genreButtons.empty())
        {
            // Empty, then Loop Starter's genres (FL's when installed, else LYNX's own) with their suggested tempo
            genreIds.push_back ({});
            genreButtons.push_back (std::make_unique<juce::TextButton> (tr ("Empty grid")));
            if (const auto* config = loopstarterui::sources().config)
                for (const auto* genre : config->listed())
                {
                    genreIds.push_back (genre->id);
                    genreButtons.push_back (std::make_unique<juce::TextButton> (juce::String::fromUTF8 (genre->label.c_str()) + "  "
                                                                                + tr ("{0} BPM", { juce::String (loopstarter::suggestedTempo (*genre)) })));
                }
            // one click on a starter adds it (round 190t): nothing to pick first, nothing to confirm
            for (std::size_t i = 0; i < genreButtons.size(); ++i)
            {
                genreButtons[i]->onClick = [this, i] { owner.addDrums (genreIds[i], i != 0, useTempo.getToggleState()); };
                if (i == 1)
                    genreButtons[i]->setComponentID ("simple.starters"); // (the tour's ring: the first starter beat)
                addChildComponent (*genreButtons[i]);
            }
        }
        // the starter's tempo is for a project that has nothing yet: with tracks in it the tempo stays, unless asked
        useTempo.setToggleState (simpletracks::isEmptySong (owner.engine.getSong()), juce::dontSendNotification);
        starterPage = true;
        updatePage();
    }

    void showTilesPage()
    {
        starterPage = false;
        updatePage();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg0.withAlpha (0.72f)); // the timeline stays faintly visible behind
        const auto c = card().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (c.translated (0.0f, 4.0f).expanded (2.0f), theme::radius::lg + 2.0f);
        g.setColour (theme::colour::bg2);
        g.fillRoundedRectangle (c, theme::radius::lg);
        g.setColour (theme::colour::borderStrong);
        g.drawRoundedRectangle (c.reduced (0.5f), theme::radius::lg, 1.0f);
        g.setColour (theme::colour::accent);
        g.fillRect (c.getX() + 24.0f, c.getY() + 52.0f, c.getWidth() - 48.0f, 1.0f);
    }

    void resized() override
    {
        auto c = card().reduced (20, 14);
        auto top = c.removeFromTop (34);
        close.setBounds (top.removeFromRight (28).withSizeKeepingCentre (28, 28));
        back.setBounds (top.removeFromLeft (90).withSizeKeepingCentre (90, 28));
        title.setBounds (top);
        starterTitle.setBounds (top);
        c.removeFromTop (16);
        footer.setBounds (c.removeFromBottom (20));
        c.removeFromBottom (6);
        {
            // "Start from..." and the tour's button side by side
            auto row = c.removeFromBottom (28);
            const auto both = juce::jmin (340 + 8 + 170, row.getWidth());
            row = row.withSizeKeepingCentre (both, 28);
            tourButton.setBounds (row.removeFromRight (both * 170 / 518));
            row.removeFromRight (8);
            startFrom.setBounds (row);
        }
        c.removeFromBottom (8);

        // tiles: two columns, then Guitar / bass across, then Import and Loops
        const auto gap = 10;
        const auto rowH = juce::jlimit (50, 78, (c.getHeight() - 4 * gap) / 5);
        auto tiles = c;
        auto row = [&] { auto r = tiles.removeFromTop (rowH); tiles.removeFromTop (gap); return r; };
        auto pair = [&] (juce::Component& a, juce::Component& b) {
            auto r = row();
            a.setBounds (r.removeFromLeft ((r.getWidth() - gap) / 2));
            r.removeFromLeft (gap);
            b.setBounds (r);
        };
        pair (voice, instrument);
        moreInstruments.setBounds (instrument.getLocalBounds().removeFromRight (instrument.rightReserve).reduced (8, 0).withSizeKeepingCentre (instrument.rightReserve - 16, 28));
        pair (drums, sampler);
        auto g = row();
        guitar.setBounds (g);
        pair (import, loops);
        auto sw = guitar.getLocalBounds().removeFromRight (guitar.rightReserve).reduced (8, 0).withSizeKeepingCentre (guitar.rightReserve - 16, 28);
        guitarButton.setBounds (sw.removeFromLeft (sw.getWidth() / 2));
        bassButton.setBounds (sw);

        // the starter-beat page: the tempo switch, then the starters in three columns (a click adds one)
        auto s = card().reduced (20, 14).withTrimmedTop (50);
        useTempo.setBounds (s.removeFromTop (28));
        s.removeFromTop (10);
        const auto columns = 3, buttonH = 32;
        const auto w = (s.getWidth() - (columns - 1) * gap) / columns;
        for (int i = 0; i < (int) genreButtons.size(); ++i)
            genreButtons[(std::size_t) i]->setBounds (s.getX() + (i % columns) * (w + gap), s.getY() + (i / columns) * (buttonH + 8), w, buttonH);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            if (starterPage)
                showTilesPage();
            else
                owner.closeByHand();
            return true;
        }
        return false;
    }

    // clicks on the dimmed area outside the card close the chooser
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasClicked() && ! card().contains (e.getPosition()))
            owner.closeByHand();
    }

private:
    juce::Rectangle<int> card() const
    {
        const auto w = juce::jmin (680, getWidth() - 24), h = juce::jmin (520, getHeight() - 24);
        return getLocalBounds().withSizeKeepingCentre (juce::jmax (320, w), juce::jmax (300, h));
    }

    void updatePage()
    {
        for (auto* c : std::initializer_list<juce::Component*> { &title, &voice, &instrument, &drums, &sampler, &guitar, &import, &loops, &footer, &startFrom, &tourButton })
            c->setVisible (! starterPage);
        tourButton.setButtonText (owner.tourCanContinue() ? tr ("Continue the tour") : tr ("First-song tour"));
        tourButton.setVisible (! starterPage && ! owner.tourRunning()); // (not while its card is up)
        for (auto* c : std::initializer_list<juce::Component*> { &starterTitle, &back, &useTempo })
            c->setVisible (starterPage);
        for (auto& b : genreButtons)
            b->setVisible (starterPage);
        resized();
        repaint();
    }

    SimpleWorkflow& owner;
    juce::Label title, footer, starterTitle;
    juce::TextButton close, back, guitarButton, bassButton, moreInstruments, startFrom, tourButton;
    juce::ToggleButton useTempo;
    Tile voice, instrument, drums, sampler, guitar, import, loops;
    std::vector<std::unique_ptr<juce::TextButton>> genreButtons;
    std::vector<std::string> genreIds;
    bool starterPage = false;

    friend class SimpleWorkflow;
};

// --- the workflow ------------------------------------------------------------------------------------------------------
namespace
{
// "+ Add track" in the Playlist's title bar
class AddTrackButton final : public juce::TextButton
{
public:
    AddTrackButton() : juce::TextButton ("+ " + tr ("Add track"))
    {
        setComponentID ("simple.addTrack");
        setTooltip (tr ("Add a track: voice, instrument, drums, sampler, guitar or bass, or import files"));
    }
};
} // namespace

// the Playlist title bar's tools: "+ Add track" and "Finish" side by side
class SimpleTitleTools final : public juce::Component,
                               private juce::Timer
{
public:
    SimpleTitleTools (juce::Component& a, juce::Component& b, juce::Component& c, std::function<juce::String()> statusText)
        : add (a), finish (b), lyrics (c), text (std::move (statusText))
    {
        addAndMakeVisible (add);
        addAndMakeVisible (finish);
        addAndMakeVisible (lyrics);
        // round 190t: LYNX backs the work up by itself; a beginner should see it ("Backed up 2 min ago")
        status.setFont (theme::font (theme::type::small));
        status.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        status.setJustificationType (juce::Justification::centredLeft);
        status.setMinimumHorizontalScale (0.8f);
        status.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (status);
        refresh();
        startTimer (2000);
    }
    static constexpr int statusWidth = 150;
    static constexpr int width = 110 + 6 + 80 + 6 + 80 + 10 + statusWidth; // (round 190ch: + Lyrics)
    void resized() override
    {
        auto r = getLocalBounds();
        add.setBounds (r.removeFromLeft (110));
        r.removeFromLeft (6);
        finish.setBounds (r.removeFromLeft (80));
        r.removeFromLeft (6);
        lyrics.setBounds (r.removeFromLeft (80));
        r.removeFromLeft (10);
        status.setBounds (r);
    }
    void showForSnapshot (const juce::String& fixed)
    {
        stopTimer();
        text = [fixed] { return fixed; };
        refresh();
    }
    void refresh()
    {
        const auto now = text ? text() : juce::String();
        if (now != status.getText())
            status.setText (now, juce::dontSendNotification);
    }

private:
    void timerCallback() override { refresh(); }

    juce::Component& add;
    juce::Component& finish;
    juce::Component& lyrics;
    std::function<juce::String()> text;
    juce::Label status;
};

SimpleWorkflow::SimpleWorkflow (AudioEngine& e, Host h) : engine (e), host (std::move (h))
{
    instances.fetch_add (1, std::memory_order_relaxed);
    alive.fetch_add (1, std::memory_order_relaxed);
    auto button = std::make_unique<AddTrackButton>();
    button->onClick = [this] { showChooser(); };
    addButton = std::move (button);
    auto finish = std::make_unique<juce::TextButton> (tr ("Finish"));
    finish->setComponentID ("simple.finish");
    finish->setTooltip (tr ("Master looks: give the whole song a finished sound"));
    finish->setClickingTogglesState (false);
    finish->onClick = [this] {
        if (isFinishShowing())
            hideFinish();
        else
            showFinish();
    };
    finishButton = std::move (finish);
    // round 190ch: the project's Lyrics / Notes panel, docked beside the timeline
    auto lyrics = std::make_unique<juce::TextButton> (tr ("Lyrics"));
    lyrics->setTooltip (tr ("Lyrics and notes of this project"));
    lyrics->setComponentID ("simple.lyrics");
    lyrics->onClick = [this] {
        if (host.toggleLyrics)
            host.toggleLyrics();
    };
    lyricsButton = std::move (lyrics);
    titleTools = std::make_unique<SimpleTitleTools> (*addButton, *finishButton, *lyricsButton, host.saveStatus);

    // round 190dv: the Welcome screen and the first-song tour
    FirstSongTour::Host t;
    t.window = host.window;
    t.setOverlay = host.setOverlay;
    t.doStep = [this] (int step) { doTourStep (step); };
    t.showLoops = [this] {
        if (host.showLoops)
            host.showLoops();
    };
    t.openDemos = [this] { showStartFrom(); };
    t.startEmpty = [this] { showChooser(); };
    t.tourStarting = [this] {
        if (simpletracks::isEmptySong (engine.getSong()) && ! isChooserShowing())
            showChooser(); // (the first step's control: the Drums tile)
    };
    t.saveSong = [this] {
        if (host.exportAudio)
            host.exportAudio (0);
    };
    t.songIsEmpty = [this] { return simpletracks::isEmptySong (engine.getSong()); };
    t.newProjectFirst = host.newProjectForTour;
    tour = std::make_unique<FirstSongTour> (engine, std::move (t), host.offerWelcome);
}

// --- "Start from" (round 190ch) -----------------------------------------------------------------------------------------
void SimpleWorkflow::showStartFrom()
{
    if (startFrom == nullptr)
    {
        StartFromPanel::Hooks hooks;
        hooks.open = [this] (Song song) {
            hideStartFrom();
            if (host.startFrom)
                host.startFrom (std::move (song));
        };
        hooks.close = [this] {
            hideStartFrom();
            if (simpletracks::isEmptySong (engine.getSong()) && engine.getSong().projectId != dismissedFor)
                showChooser(); // (back to where it was opened from)
        };
        startFrom = std::make_unique<StartFromPanel> (engine, std::move (hooks));
    }
    if (host.setOverlay)
        host.setOverlay (startFrom.get());
    if (startFrom->isShowing())
        startFrom->grabKeyboardFocus();
}

void SimpleWorkflow::hideStartFrom()
{
    if (! isStartFromShowing())
        return;
    static_cast<StartFromPanel&> (*startFrom).stopPreview();
    if (host.setOverlay)
        host.setOverlay (nullptr);
}

bool SimpleWorkflow::isStartFromShowing() const noexcept { return startFrom != nullptr && startFrom->getParentComponent() != nullptr; }

juce::Component* SimpleWorkflow::startFromForSnapshot()
{
    showStartFrom();
    return startFrom.get();
}

void SimpleWorkflow::showFinish()
{
    if (finishDock == nullptr)
        finishDock = std::make_unique<FinishDock> (engine, [this] { hideFinish(); }, FinishDock::Saving { host.exportAudio, host.exportOtherApps, host.showReferenceMatch });
    static_cast<FinishDock&> (*finishDock).refresh();
    if (host.setSideDock)
        host.setSideDock (finishDock.get(), FinishDock::width);
    if (auto* b = dynamic_cast<juce::Button*> (finishButton.get()))
        b->setToggleState (true, juce::dontSendNotification);
}

void SimpleWorkflow::hideFinish()
{
    if (isFinishShowing() && host.setSideDock)
        host.setSideDock (nullptr, 0);
    if (auto* b = dynamic_cast<juce::Button*> (finishButton.get()))
        b->setToggleState (false, juce::dontSendNotification);
}

bool SimpleWorkflow::isFinishShowing() const noexcept { return finishDock != nullptr && finishDock->getParentComponent() != nullptr; }

SimpleWorkflow::~SimpleWorkflow()
{
    if (active)
        left();
    tour.reset(); // (before the chooser and the docks it may point at)
    alive.fetch_sub (1, std::memory_order_relaxed);
}

// --- the Welcome screen and the first-song tour (round 190dv) ------------------------------------------------------------
void SimpleWorkflow::showWelcome()
{
    if (! active)
        return;
    hideStartFrom();
    tour->showWelcome();
}

void SimpleWorkflow::startOrContinueTour()
{
    if (! active)
        return;
    hideStartFrom();
    if (isChooserShowing() && ! simpletracks::isEmptySong (engine.getSong()))
        hideChooser();
    tour->startOrContinue();
}

bool SimpleWorkflow::tourCanContinue() const { return tour != nullptr && tour->canContinue(); }
bool SimpleWorkflow::tourRunning() const { return tour != nullptr && tour->isRunning(); }
int SimpleWorkflow::tourStep() const { return tour != nullptr ? tour->step() : 0; }

void SimpleWorkflow::doTourStep (int step)
{
    const auto& song = engine.getSong();
    switch (step)
    {
        case tour::beat:
            if (const auto* config = loopstarterui::sources().config)
            {
                if (isChooserShowing())
                    hideChooser();
                addDrums (config->defaultGenre, true, true);
            }
            break;
        case tour::key:
            engine.editSong ([] (Song& s) { projectkey::set (s, tour::defaultKey()); });
            break;
        case tour::instrument:
            addDefaultInstrument();
            break;
        case tour::chords:
        {
            if (tour::firstInstrumentTrack (song) < 0)
                addDefaultInstrument();
            const auto track = tour::firstInstrumentTrack (engine.getSong());
            if (track < 0)
                break;
            engine.editSong ([track] (Song& s) { tour::writeChords (s, track); });
            dockTrack = track;
            if (SimpleDock::shownSetting)
            {
                updateDock();
                if (dock != nullptr)
                    dock->setTab (SimpleDock::Tab::editor); // (the chords, as notes)
            }
            break;
        }
        case tour::effect:
        {
            auto track = tour::firstInstrumentTrack (song);
            for (int t = 0; t < (int) song.playlistTracks.size() && track < 0; ++t)
                if (simpletracks::kindOf (song, t))
                    track = t;
            if (track < 0)
                break;
            engine.editSong ([track] (Song& s) { tour::addOneKnob (s, track); });
            showEffectsOfInsert (simpletracks::insertOf (engine.getSong(), track));
            break;
        }
        case tour::finish:
        {
            showFinish();
            MasterLookSettings look;
            look.id = looks::balanced;
            engine.editSong ([look] (Song& s) { looks::apply (s, look); });
            break;
        }
        default:
            break;
    }
}

void SimpleWorkflow::entered()
{
    active = true;
    if (host.setTitleAccessory)
        host.setTitleAccessory (titleTools.get(), SimpleTitleTools::width);
    songChanged();
}

void SimpleWorkflow::left()
{
    active = false;
    if (tour != nullptr) // (round 190dv: kept where it is; Help continues it)
    {
        if (tour->isRunning())
            tour->pause();
        tour->hideWelcome();
    }
    hideStartFrom();
    hideChooser();
    hideFinish();
    releaseEditor(); // (the docked Piano roll goes back to its panel)
    if (dock != nullptr && dock->getParentComponent() != nullptr && host.setBottomDock)
        host.setBottomDock (nullptr, nullptr);
    if (host.setTitleAccessory)
        host.setTitleAccessory (nullptr, 0);
}

void SimpleWorkflow::editorTakenBack()
{
    if (dock != nullptr)
        dock->editorTakenBack();
}

void SimpleWorkflow::releaseEditor()
{
    if (dock != nullptr)
        dock->releaseEditor();
}

// --- the dock for the selected track (round 190bu) --------------------------------------------------------------------
void SimpleWorkflow::updateDock()
{
    if (! active)
        return;
    if (! SimpleDock::shownSetting)
    {
        if (dock != nullptr && dock->getParentComponent() != nullptr && host.setBottomDock)
            host.setBottomDock (nullptr, nullptr);
        return;
    }
    if (dock == nullptr)
    {
        SimpleDock::Hooks hooks;
        hooks.toggleChannelWindow = host.toggleChannelWindow;
        hooks.showKeys = host.showKeys;
        hooks.dockPianoRoll = host.dockPianoRoll;
        hooks.undockPianoRoll = host.undockPianoRoll;
        hooks.setFoldToKey = host.setFoldToKey;
        hooks.foldedToKey = host.foldedToKey;
        hooks.openPianoRollPanel = host.openPianoRollPanel;
        hooks.layoutChanged = [this] {
            updateDock(); // (the ▾ button hides it)
            if (host.settingsChanged)
                host.settingsChanged();
        };
        dock = std::make_unique<SimpleDock> (engine, std::move (hooks));
    }
    // the track it shows: the Playlist's current track while that is one of the song's tracks, else the first track
    const auto& song = engine.getSong();
    if (dockTrack < 0 && host.selectedTrack)
        dockTrack = host.selectedTrack();
    if (dockTrack >= (int) song.playlistTracks.size() || (dockTrack >= 0 && ! simpletracks::kindOf (song, dockTrack)))
        dockTrack = -1;
    if (dockTrack < 0)
        for (int t = 0; t < (int) song.playlistTracks.size() && dockTrack < 0; ++t)
            if (simpletracks::kindOf (song, t))
                dockTrack = t;
    if (dock->shownTrack() != dockTrack)
        dock->showTrack (dockTrack);
    else
        dock->refresh();
    if (dock->getParentComponent() == nullptr && host.setBottomDock)
    {
        dock->setVisible (true);
        host.setBottomDock (dock.get(), [this] (int available) { return dock != nullptr ? dock->heightFor (available) : 0; });
    }
}

void SimpleWorkflow::trackSelected (int track)
{
    if (! active || track == dockTrack)
        return;
    dockTrack = track;
    if (isDockShowing())
        updateDock();
}

void SimpleWorkflow::showEffectsOfInsert (int insert)
{
    if (! active)
        return;
    const auto& song = engine.getSong();
    for (int t = 0; t < (int) song.playlistTracks.size(); ++t)
        if (simpletracks::kindOf (song, t) && simpletracks::insertOf (song, t) == insert)
        {
            dockTrack = t;
            break;
        }
    if (! SimpleDock::shownSetting)
    {
        SimpleDock::shownSetting = true;
        if (host.settingsChanged)
            host.settingsChanged();
    }
    updateDock();
    if (dock != nullptr)
        dock->setTab (SimpleDock::Tab::effects);
}

void SimpleWorkflow::toggleDock()
{
    SimpleDock::shownSetting = ! SimpleDock::shownSetting;
    updateDock();
    if (host.settingsChanged)
        host.settingsChanged();
}

void SimpleWorkflow::cycleDockTab()
{
    if (isDockShowing())
        dock->cycleTab();
}

bool SimpleWorkflow::isDockShowing() const noexcept { return dock != nullptr && dock->getParentComponent() != nullptr; }

SimpleDock* SimpleWorkflow::dockForSnapshot()
{
    SimpleDock::shownSetting = true;
    updateDock();
    return dock.get();
}

void SimpleWorkflow::songChanged()
{
    if (! active)
        return;
    if (isFinishShowing())
        static_cast<FinishDock&> (*finishDock).refresh();
    static_cast<SimpleTitleTools&> (*titleTools).refresh();
    updateDock();
    if (tour != nullptr)
        tour->songChanged(); // (round 190dv: a step the song shows done moves the tour on)
    const auto& song = engine.getSong();
    if (simpletracks::isEmptySong (song))
    {
        // round 190dv: the first time, the Welcome screen instead of the chooser
        if (tour != nullptr && host.offerWelcome && tour->shouldOfferWelcome() && ! isStartFromShowing())
            tour->showWelcome();
        else if (song.projectId != dismissedFor && ! isChooserShowing() && ! isStartFromShowing() && (tour == nullptr || ! tour->isWelcomeShowing()))
            showChooser();
    }
    else
    {
        if (isChooserShowing() && ! chooser->starterPage)
            hideChooser(); // (something was added some other way: an undo, a drop, a loaded project)
        if (tour != nullptr && tour->isWelcomeShowing())
            tour->hideWelcome();
    }
}

void SimpleWorkflow::showChooser()
{
    if (chooser == nullptr)
        chooser = std::make_unique<Chooser> (*this);
    chooser->showTilesPage();
    if (host.setOverlay)
        host.setOverlay (chooser.get());
    if (chooser->isShowing())
        chooser->grabKeyboardFocus();
}

void SimpleWorkflow::hideChooser()
{
    if (chooser != nullptr && chooser->getParentComponent() != nullptr && host.setOverlay)
        host.setOverlay (nullptr);
}

bool SimpleWorkflow::isChooserShowing() const noexcept { return chooser != nullptr && chooser->getParentComponent() != nullptr; }

void SimpleWorkflow::closeByHand()
{
    dismissedFor = engine.getSong().projectId;
    hideChooser();
}

juce::Component* SimpleWorkflow::chooserForSnapshot()
{
    if (chooser == nullptr)
        chooser = std::make_unique<Chooser> (*this);
    return chooser.get();
}

void SimpleWorkflow::showStarterPageForSnapshot()
{
    chooserForSnapshot();
    chooser->showStarterPage();
}

void SimpleWorkflow::clickStarterForSnapshot (int index)
{
    showStarterPageForSnapshot();
    if (index >= 0 && index < (int) chooser->genreButtons.size() && chooser->genreButtons[(std::size_t) index]->onClick)
        chooser->genreButtons[(std::size_t) index]->onClick();
}

void SimpleWorkflow::setStatusForSnapshot (const juce::String& text)
{
    static_cast<SimpleTitleTools&> (*titleTools).showForSnapshot (text);
}

bool SimpleWorkflow::add (simpletracks::Request request)
{
    // tried on a copy first, so a refused track leaves no undo step behind
    auto trial = engine.getSong();
    if (const auto r = simpletracks::addTrack (trial, request); ! r.ok())
    {
        const auto text = r.error == simpletracks::Error::mixerFull ? tr ("There are no available mixer tracks.")
                        : r.error == simpletracks::Error::noFreeTrack ? tr ("No free Playlist track is left.")
                                                                       : tr ("This sound can't be played as an instrument.");
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("New track"), text);
        return false;
    }
    simpletracks::Result r;
    engine.editSong ([&] (Song& s) { r = simpletracks::addTrack (s, request); });
    if (! r.ok())
        return false;
    if (simpletracks::isAudioKind (request.kind) && request.kind != Kind::audio && r.insert > 0)
        engine.setInsertArmed (r.insert, true);
    if (! r.channels.empty() && ! simpletracks::isAudioKind (request.kind))
        engine.setSelectedChannel (r.channels.front());
    lastAddedTrack = r.track;
    if (r.pattern >= 0 && r.pattern < (int) engine.getSong().patterns.size() && ! simpletracks::isAudioKind (request.kind) && request.kind != Kind::drums)
        engine.editSong ([p = r.pattern] (Song& s) { s.currentPattern = p; }, false); // (the typing keys record into it)
    hideChooser();
    if (host.trackAdded)
        host.trackAdded (r.track);
    return true;
}

void SimpleWorkflow::addAudio (Kind kind)
{
    simpletracks::Request q;
    q.kind = kind;
    q.name = trackNameFor (kind);
    q.input = 0; // the device's first input, mono (a microphone or an instrument cable)
    q.inputStereo = false;
    add (std::move (q));
}

void SimpleWorkflow::addDefaultInstrument()
{
    // LYNX's own synth (what an imported MIDI track plays too), ready for the computer keys and the on-screen keyboard
    ChannelDef synth;
    synth.instrument = InstrumentKind::synth;
    simpletracks::Request q;
    q.kind = Kind::instrument;
    q.name = trackNameFor (q.kind);
    q.channel = std::move (synth);
    q.emptyBars = 4; // a pattern to write into (round 190ds)
    if (add (std::move (q)))
        showKeysForNewTrack();
}

void SimpleWorkflow::showKeysForNewTrack()
{
    if (SimpleDock::shownSetting && active)
    {
        dockTrack = lastAddedTrack;
        updateDock();
        if (dock != nullptr)
            dock->showKeyboard();
        return;
    }
    if (host.showKeys)
        host.showKeys();
}

void SimpleWorkflow::chooseInstrument (juce::Component& tile)
{
    // the Channel Rack's own generator list (the same instruments, plugins and Soundfonts)
    juce::PopupMenu menu;
    quickpick::addGeneratorMenu (
        menu, engine,
        [this] (ChannelDef c) {
            simpletracks::Request q;
            q.kind = c.instrument == InstrumentKind::sampler ? Kind::sampler : Kind::instrument;
            q.name = trackNameFor (q.kind);
            q.channel = std::move (c);
            q.emptyBars = 4; // a pattern to write into (round 190ds)
            if (add (std::move (q)))
                showKeysForNewTrack();
        },
        [this] { chooseSample(); }, nullptr, chooser.get());
    quickpick::showMenu (menu, juce::PopupMenu::Options().withTargetComponent (&tile), {});
}

void SimpleWorkflow::chooseSample()
{
    // the dialog opens where there are samples to choose from (round 190t): the Browser's sample folder the first time,
    // then the folder the last sample came from
    const auto start = sampleFolder.isDirectory() ? sampleFolder : host.sampleFolder ? host.sampleFolder() : juce::File();
    fileChooser = ui::makeFileChooser (tr ("Choose sample"), start, engine.getSampleLibrary().wildcard());
    ui::launchChooser (fileChooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                       [this] (const juce::FileChooser& fc) {
                           const auto file = fc.getResult();
                           if (! file.existsAsFile())
                               return;
                           sampleFolder = file.getParentDirectory();
                           if (engine.getSampleLibrary().load (file.getFullPathName()) == nullptr)
                           {
                               juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not import"),
                                                                       tr ("Could not read {0}", { file.getFileName() }));
                               return;
                           }
                           ChannelDef c;
                           c.instrument = InstrumentKind::sampler;
                           c.samplePath = file.getFullPathName().toStdString();
                           simpletracks::Request q;
                           q.kind = Kind::sampler;
                           q.name = file.getFileNameWithoutExtension().toStdString();
                           q.channel = std::move (c);
                           q.emptyBars = 4; // a pattern to write into (round 190ds)
                           add (std::move (q));
                       });
}

void SimpleWorkflow::importFiles()
{
    fileChooser = ui::makeFileChooser (tr ("Import audio or MIDI"), juce::File(), engine.getSampleLibrary().wildcard() + ";*.mid;*.midi");
    ui::launchChooser (fileChooser,
                       juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                       [this] (const juce::FileChooser& fc) { addFiles (fc.getResults(), 0); });
}

void SimpleWorkflow::dropFiles (const juce::StringArray& files, std::int64_t tick)
{
    juce::Array<juce::File> list;
    for (const auto& f : files)
        list.add (juce::File (f));
    addFiles (list, tick);
}

void SimpleWorkflow::addFiles (const juce::Array<juce::File>& files, std::int64_t start)
{
    juce::StringArray failed;
    std::optional<keyfollow::LoopKey> firstKey; // the first keyed loop (a project without a key is offered its key)
    for (const auto& file : files)
    {
        if (file.hasFileExtension ("mid;midi"))
        {
            // one instrument track (LYNX's synth) per MIDI track with notes, from `start`
            juce::String error;
            const auto midi = MidiFileIo::parseFile (file, error);
            if (! midi)
            {
                failed.add (file.getFileName());
                continue;
            }
            midiimport::Options options;
            bool any = false;
            for (auto& group : midiimport::groups (*midi, options))
            {
                if (group.notes.empty())
                    continue;
                simpletracks::Request q;
                q.kind = Kind::instrument;
                q.name = group.name.empty() ? file.getFileNameWithoutExtension().toStdString() : group.name;
                ChannelDef synth;
                synth.instrument = InstrumentKind::synth;
                q.channel = synth;
                Pattern notes;
                notes.notes = std::move (group.notes);
                q.pattern = std::move (notes);
                q.start = start;
                any = add (std::move (q)) || any;
            }
            if (! any)
                failed.add (file.getFileName());
            continue;
        }
        const auto data = engine.getSampleLibrary().load (file.getFullPathName());
        if (data == nullptr)
        {
            failed.add (file.getFileName());
            continue;
        }
        // the file's length in ticks at the project's tempo (as a drop on the Playlist)
        const auto seconds = (double) data->frames() / data->sampleRate;
        simpletracks::Request q;
        q.kind = Kind::audio;
        q.name = file.getFileNameWithoutExtension().toStdString();
        q.audioFile = file.getFullPathName().toStdString();
        q.audioLength = juce::jmax<std::int64_t> (ppq / 4, (std::int64_t) (seconds * engine.getSong().bpm / 60.0 * ppq));
        // a loop made for a tempo (ACID beats, or a tempo in its name that makes whole bars) keeps time: it lasts its
        // beats at the project's tempo, at its own pitch, and follows the tempo from then on (round 190p)
        q.audioBeats = simpletracks::loopBeats (file.getFileName().toStdString(), seconds, data->acidBeats, engine.getSong().meter());
        q.start = start;
        // round 190ds: a loop follows the project key — its key from its name, else detected from a loop's audio (a
        // file made for a tempo); voice takes and songs (over 70 s) are left alone
        if (seconds <= 70.0)
        {
            auto key = keyfollow::keyInName (file.getFileName().toStdString());
            int source = key ? 1 : 0;
            if (! key && q.audioBeats > 0.0f)
                if (key = keyfollow::detectKey (*data).key; key)
                    source = 2;
            if (key)
            {
                q.audioKeyRoot = key->root;
                q.audioKeyMinor = key->minor;
                q.audioKeySource = source;
                q.followKey = true;
                if (! firstKey)
                    firstKey = key;
            }
        }
        add (std::move (q));
    }
    if (! failed.isEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not import"),
                                                tr ("Could not read {0}", { failed.joinIntoString (", ") }));
    if (firstKey && keyfollow::asksToSetKey (engine.getSong()))
        offerProjectKey (firstKey->root, firstKey->minor);
}

void SimpleWorkflow::offerProjectKey (int root, bool minor)
{
    const keyfollow::LoopKey key { root, minor };
    const auto name = juce::String (projectkey::rootName (root)) + " " + (minor ? tr ("minor") : tr ("major"));
    lastKeyQuestion = tr ("Set the project key to {0}? Loops that follow the key are moved to it, keeping their length.", { name });
    auto setKey = [this, key] {
        engine.editSong ([key] (Song& s) { projectkey::set (s, keyfollow::projectKeyFor (key)); });
    };
    if (answerForSnapshot >= 0)
    {
        if (answerForSnapshot == 1)
            setKey();
        return;
    }
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, tr ("Project key"), lastKeyQuestion, tr ("Set the key"), tr ("Not now"),
                                        nullptr, juce::ModalCallbackFunction::create ([setKey] (int result) {
                                            if (result == 1)
                                                setKey();
                                        }));
}

void SimpleWorkflow::addDrums (const std::string& genre, bool steps, bool useTempo)
{
    // a Loop Starter plan for the genre (FL's configuration and presets when installed, else LYNX's own); an empty grid
    // takes the default genre's kit without its steps
    const auto& sources = loopstarterui::sources();
    if (sources.config == nullptr)
        return;
    const auto id = genre.empty() ? sources.config->defaultGenre : genre;
    const auto plan = loopstarter::generate (sources, id, (std::uint64_t) juce::Random::getSystemRandom().nextInt64());
    simpletracks::Request q;
    q.kind = Kind::drums;
    q.name = trackNameFor (Kind::drums);
    q.kit = loopstarter::kitChannels (plan);
    auto pattern = loopstarter::kitPattern (plan);
    if (! steps)
    {
        pattern.notes.clear();
        pattern.lengthTicks = 16 * ticksPerStep;
    }
    q.pattern = std::move (pattern);
    q.fillBars = 4; // the beat loops for four bars: long enough to play along and build on (round 190t)
    if (q.kit.empty())
        return;
    if (useTempo && steps)
        q.bpm = loopstarter::suggestedTempo (sources, id); // the number on the starter's button, not the plan's random
                                                           // one: the beat is in steps, so it follows (same undo step)
    add (std::move (q));
}
} // namespace lynx
