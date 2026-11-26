#include "ui/HowTo.h"

#include "ui/I18n.h"
#include "ui/Theme.h"

#ifndef TR_NOOP
 #define TR_NOOP(text) text
#endif

namespace lynx::howto
{
namespace
{
struct Guide
{
    const char* id;
    const char* title;
    const char* slug;
    const char* steps[5];
};

// (FL Studio's gestures in the Pattern layout's windows, Live's in the Clip-launch ones, BandLab's in Simple)
const Guide guides[windowCount] = {
    { "playlist", TR_NOOP ("How to use the Playlist"), "pattern-tour",
      { TR_NOOP ("Click with the Pencil (P) to place the pattern you're editing; the toolbar's pattern box says which one."),
        TR_NOOP ("Drag a sample, a loop or a MIDI file from the Browser onto a track."),
        TR_NOOP ("Right-click a clip to delete it. Drag a clip's edge to make it longer or shorter."),
        TR_NOOP ("Song mode (beside Play) plays the whole Playlist; Pattern mode plays only the pattern."), nullptr } },
    { "channelRack", TR_NOOP ("How to use the Channel rack"), "pattern-tour",
      { TR_NOOP ("Each row is a channel: an instrument or a sample. Click its steps to switch them on, right-click to switch them off."),
        TR_NOOP ("The two knobs beside a channel's name set its volume and pan."),
        TR_NOOP ("Click a channel's name for its settings; right-click it for its menu (Piano roll, rename, replace...)."),
        TR_NOOP ("The + under the channels adds an instrument, a sample player or a plugin."),
        // round 190rs (G-03, queue item 92): the route box
        TR_NOOP ("The number box beside the knobs is the Mixer track the channel plays through: drag it to change it, double-click it to show it.") } },
    { "pianoRoll", TR_NOOP ("How to use the Piano roll"), "pattern-tour",
      { TR_NOOP ("Click to draw a note, right-click to delete it. Drag a note's right end to change its length."),
        TR_NOOP ("Ctrl+drag selects notes; hold Alt while dragging to move without snapping."),
        TR_NOOP ("The lane at the bottom edits each note's velocity; its chooser picks another property (pan, release, pitch...)."),
        TR_NOOP ("The toolbar's tools slice, paint, mute and zoom; the menu has quantize, chords, arpeggios and more."), nullptr } },
    { "mixer", TR_NOOP ("How to use the Mixer"), "mixing-basics",
      { TR_NOOP ("Each insert is a track with its own effects and fader; the Master is the first one, everything ends there."),
        TR_NOOP ("Click an empty effect slot (the list on the right) to add an effect to the selected insert."),
        TR_NOOP ("To route an insert into another, select it, then click the arrow at the bottom of the one it should go to."),
        TR_NOOP ("Right-click that arrow for a sidechain; right-click an insert's name for its menu (rename, color, render...)."),
        // round 190rs (G-03, queue item 92): how a channel reaches an insert; Playlist tracks don't route
        TR_NOOP ("Each channel plays into the insert in its box in the Channel rack; Playlist tracks only hold clips. Want them paired? Playlist menu › Link Playlist tracks to mixer tracks.") } },
    { "browser", TR_NOOP ("How to use the Browser"), "simple-loops",
      { TR_NOOP ("Click a sample to hear it; drag it onto the Channel rack, the Playlist or a Mixer slot."),
        TR_NOOP ("Type in the search box to find samples, presets and plugins in every folder."),
        TR_NOOP ("Add your own sample folders in Settings › Files."),
        TR_NOOP ("Right-click a file or a folder for more: favorites, open in the file manager..."), nullptr } },
    { "clipGrid", TR_NOOP ("How to use the clip grid"), "clip-launch-tour",
      { TR_NOOP ("Each column is a track and each row a scene. A clip's play button launches it in time with the song."),
        TR_NOOP ("The play button at the end of a row launches the whole scene; a column's stop button stops that track."),
        TR_NOOP ("Drag a loop or a sample from the Browser into a slot; double-click an empty slot for a new MIDI clip."),
        TR_NOOP ("Select a clip to edit it in the detail view below; the Device view shows the track's devices."), nullptr } },
    { "deviceView", TR_NOOP ("How to use the Device view"), "device-view",
      { TR_NOOP ("The selected track's instrument and effects, left to right in the order the sound goes through them."),
        TR_NOOP ("Drop an effect from the Browser onto the row to add it where you drop it."),
        TR_NOOP ("Click a device to select it, double-click its title to fold it, drag it to move it (Ctrl+drag: a copy)."),
        TR_NOOP ("Right-click a device for its menu: its window, presets, delete."), nullptr } },
    { "simple", TR_NOOP ("How to use the Simple layout"), "simple-beat",
      { TR_NOOP ("+ Add track picks a voice to record, an instrument, drums or a sampler."),
        TR_NOOP ("The dock under the tracks has the selected track's instrument, keyboard, editor and effects."),
        TR_NOOP ("Drag loops from Loops onto the timeline: they follow the song's tempo and key."),
        TR_NOOP ("Finish gives the song a master look and saves it as WAV or MP3."), nullptr } },
    { "tracker", TR_NOOP ("How to use the Tracker"), "tracker",
      { TR_NOOP ("Time runs down the rows; each channel of the pattern is a column with Note, Ins, Vol, Pan, Dly and Fx fields."),
        TR_NOOP ("Turn Edit on, then type notes with the typing keyboard's two rows; 1 ends a note (OFF), Del clears a cell."),
        TR_NOOP ("Values are hex digits; the bar at the top sets the octave and the edit step (rows moved after each entry)."),
        TR_NOOP ("It shows the same notes as the Piano roll: an edit in one is in the other."), nullptr } },
    { "automation", TR_NOOP ("How to use automation"), "automation-basics",
      { TR_NOOP ("Right-click any knob › Create automation clip: its movement is drawn as a curve over the song."),
        TR_NOOP ("In the automation editor, click to add a point and drag points to move them."),
        TR_NOOP ("Right-click a point for its menu: delete it, or pick the curve's shape (single curve, stairs, pulse...)."),
        TR_NOOP ("Automation lanes (LYNX extras) show a track's automation right under it."), nullptr } },
    { "settings", TR_NOOP ("How to use Settings"), "settings-tour",
      { TR_NOOP ("The pages follow FL Studio's order; LYNX's own pages (Ableton Link, LYNX extras) come after a gap."),
        TR_NOOP ("Ctrl+F finds a setting on every page and outlines it; Enter goes to the next match."),
        TR_NOOP ("Reset this page puts a page's options back at their defaults, with Undo for a few seconds."),
        TR_NOOP ("The \"?\" command palette finds any setting from the main window too."), nullptr } },
};

const Guide& guide (Window w) noexcept { return guides[juce::jlimit (0, windowCount - 1, (int) w)]; }

class Card final : public juce::Component
{
public:
    explicit Card (Window w) : window (w)
    {
        setComponentID (juce::String ("howto.") + windowId (w));
        watch.setButtonText (tr ("Watch the tutorial"));
        watch.setComponentID ("howto.watch");
        watch.setTooltip (tutorialUrl (w));
        watch.setColour (juce::TextButton::buttonColourId, theme::colour::accent);
        watch.setColour (juce::TextButton::textColourOffId, theme::colour::onAccent);
        watch.onClick = [w] { juce::URL (tutorialUrl (w)).launchInDefaultBrowser(); };
        closeButton.setButtonText (tr ("Close"));
        closeButton.setComponentID ("howto.close");
        closeButton.setColour (juce::TextButton::buttonColourId, theme::colour::bg4);
        closeButton.onClick = [this] {
            if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
                box->dismiss();
            else
                setVisible (false);
        };
        addAndMakeVisible (watch);
        addAndMakeVisible (closeButton);
        setSize (width, idealHeight());
    }

    static constexpr int width = 440;

    int idealHeight() const
    {
        int h = 16 + 24 + 6;
        for (int i = 0; i < stepCount (window); ++i)
            h += linesHeight (tr (step (window, i))) + 6;
        return h + 10 + 28 + 14;
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme::colour;
        g.fillAll (bg3);
        auto r = getLocalBounds().reduced (16, 16);
        g.setColour (textPrimary);
        g.setFont (theme::font (theme::type::title, true));
        g.drawText (tr (title (window)), r.removeFromTop (24), juce::Justification::centredLeft, true);
        r.removeFromTop (6);
        g.setFont (theme::font (theme::type::body));
        for (int i = 0; i < stepCount (window); ++i)
        {
            const auto text = tr (step (window, i));
            const auto h = linesHeight (text);
            auto line = r.removeFromTop (h);
            // a numbered dot in the accent
            const auto dot = line.removeFromLeft (22).withHeight (18).toFloat().withSizeKeepingCentre (17.0f, 17.0f);
            g.setColour (accent);
            g.fillEllipse (dot);
            g.setColour (onAccent);
            g.setFont (theme::font (theme::type::small, true));
            g.drawText (juce::String (i + 1), dot, juce::Justification::centred, false);
            line.removeFromLeft (6);
            g.setColour (textSecondary.interpolatedWith (textPrimary, 0.4f));
            g.setFont (theme::font (theme::type::body));
            g.drawFittedText (text, line, juce::Justification::topLeft, 4, 1.0f);
            r.removeFromTop (6);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16, 14).removeFromBottom (28);
        const auto widthOf = [this] (juce::TextButton& b) {
            return juce::roundToInt (juce::GlyphArrangement::getStringWidth (getLookAndFeel().getTextButtonFont (b, 28), b.getButtonText())) + 26;
        };
        closeButton.setBounds (r.removeFromRight (widthOf (closeButton)));
        r.removeFromRight (8);
        watch.setBounds (r.removeFromRight (widthOf (watch)));
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            if (closeButton.onClick)
                closeButton.onClick();
            return true;
        }
        return false;
    }

private:
    int linesHeight (const juce::String& text) const
    {
        juce::GlyphArrangement glyphs;
        glyphs.addJustifiedText (theme::font (theme::type::body), text, 0.0f, 0.0f, (float) (width - 32 - 28), juce::Justification::topLeft);
        return juce::jmax (18, (int) std::ceil (glyphs.getBoundingBox (0, -1, true).getHeight()) + 2);
    }

    Window window;
    juce::TextButton watch, closeButton;
};

juce::Component::SafePointer<juce::Component> shown;
} // namespace

const char* windowId (Window w) noexcept { return guide (w).id; }
const char* title (Window w) noexcept { return guide (w).title; }
const char* tutorialSlug (Window w) noexcept { return guide (w).slug; }

int stepCount (Window w) noexcept
{
    int n = 0;
    while (n < 5 && guide (w).steps[n] != nullptr)
        ++n;
    return n;
}

const char* step (Window w, int index) noexcept { return juce::isPositiveAndBelow (index, stepCount (w)) ? guide (w).steps[index] : ""; }

juce::String siteUrl (const juce::String& path)
{
    auto url = "https://lynx-share.com/" + path;
    const auto language = i18n::current();
    if (language.isNotEmpty() && language != "en")
        url << "?lang=" << (language == "pt-BR" ? juce::String ("pt") : language);
    return url;
}

juce::String tutorialUrl (Window w) { return siteUrl (juce::String ("learn/") + tutorialSlug (w) + "/"); }

void addMenuItem (juce::PopupMenu& menu, Window w, juce::Component* anchor)
{
    menu.addSeparator();
    juce::Component::SafePointer<juce::Component> safe (anchor);
    menu.addItem (tr ("How to use this..."), [w, safe, hadAnchor = anchor != nullptr] {
        if (! hadAnchor || safe != nullptr)
            show (w, safe.getComponent());
    });
}

void show (Window w, juce::Component* anchor)
{
    if (auto* old = shown.getComponent())
        if (auto* box = old->findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    auto card = std::make_unique<Card> (w);
    shown = card.get();
    juce::Component* parent = nullptr;
    juce::Rectangle<int> area;
    if (anchor != nullptr && anchor->isShowing())
    {
        parent = anchor->getTopLevelComponent();
        area = parent->getLocalArea (anchor, anchor->getLocalBounds());
        // (beside a big window: its top-left corner region, so the card sits over the window's edge, not its middle)
        if (area.getWidth() > 300 || area.getHeight() > 200)
            area = area.withSize (juce::jmin (area.getWidth(), 220), juce::jmin (area.getHeight(), 60));
    }
    else if (auto* front = juce::TopLevelWindow::getActiveTopLevelWindow())
    {
        parent = front;
        area = front->getLocalBounds().withSizeKeepingCentre (40, 40);
    }
    if (parent == nullptr)
        return;
    juce::CallOutBox::launchAsynchronously (std::move (card), area, parent);
}

std::unique_ptr<juce::Component> makeCard (Window w) { return std::make_unique<Card> (w); }

juce::Component* shownCard() { return shown.getComponent(); }
} // namespace lynx::howto
