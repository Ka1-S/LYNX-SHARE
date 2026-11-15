#include "ui/StartFromPanel.h"

#include <thread>

#include "core/DemoSongs.h"
#include "engine/AudioEngine.h"
#include "engine/Exporter.h"
#include "ui/I18n.h"
#include "ui/NewProjectDialog.h"
#include "ui/Theme.h"

namespace lynx
{
namespace startfrom
{
void describe (Entry& entry, const Song& song)
{
    entry.bpm = song.bpm;
    entry.channels = (int) song.channels.size();
    std::int64_t length = 0;
    const auto exported = makeExportSong (song, true, 0, length);
    juce::ignoreUnused (exported);
    const auto bar = song.barTicks();
    entry.bars = (int) ((length + bar - 1) / bar);
    auto notes = false;
    for (const auto& p : song.patterns)
        notes = notes || ! p.notes.empty();
    entry.playable = length > 0 && (notes || ! song.playlist.empty());
}

std::optional<Song> songOf (const Entry& entry)
{
    if (entry.kind == Entry::Kind::demo)
        return demos::make (entry.demo);
    return makeFromTemplate (entry.choice);
}

std::vector<Entry> entries()
{
    std::vector<Entry> out;
    for (int d = 0; d < demos::count; ++d)
    {
        Entry demo;
        demo.kind = Entry::Kind::demo;
        demo.demo = d;
        // round 190dv: two more short demos in other genres, made with LYNX's own instruments and starter kit
        demo.name = d == 0 ? tr ("LYNX demo song") : tr ("LYNX demo: {0} ({1})", { juce::String (demos::name (d)), tr (demos::genre (d)) });
        describe (demo, demos::make (d));
        out.push_back (demo);
    }
    for (const auto& choice : availableTemplates())
    {
        Entry e;
        e.kind = choice.kind == TemplateChoice::Kind::lynxDefault ? Entry::Kind::lynxDefault
                 : choice.kind == TemplateChoice::Kind::builtIn ? Entry::Kind::builtIn : Entry::Kind::user;
        e.choice = choice;
        e.name = templateChoiceName (choice);
        if (e.kind != Entry::Kind::user) // (a user's file is read when it is selected or played)
            if (const auto song = makeFromTemplate (choice))
                describe (e, *song);
        out.push_back (e);
    }
    return out;
}

bool renderPreview (const Song& source, const juce::File& file, int bars)
{
    auto song = source;
    exportopts::Options o;
    o.format = exportopts::Format::wav;
    o.wavBits = 16;
    o.sampleRate = 44100.0;
    o.maxTailSeconds = 1.5;
    o.pluginWarmUp = false;
    o.hqAllPlugins = false;
    if (! song.playlist.empty())
    {
        std::int64_t length = 0;
        juce::ignoreUnused (makeExportSong (song, true, 0, length));
        const auto bar = song.barTicks();
        song.loopStart = 0;
        song.loopEnd = juce::jmax<std::int64_t> (bar, juce::jmin (length, bar * juce::jmax (1, bars)));
        o.range = exportopts::Range::selection;
    }
    else
        o.range = exportopts::Range::pattern;
    ExportJob job (song, o, file);
    job.setExtraData (false);
    juce::String error;
    return job.run ([] (double) { return true; }, error) && file.existsAsFile();
}
} // namespace startfrom

struct StartFromPanel::Shared
{
    std::atomic<int> state { 0 }; // 0 idle, 1 rendering, 2 ready, 3 failed
    std::atomic<int> serial { 0 }; // the request the render belongs to (an older one is dropped)
};

class StartFromPanel::Row final : public juce::Component
{
public:
    Row (StartFromPanel& p, int i) : owner (p), index (i)
    {
        setWantsKeyboardFocus (false);
        play.setTooltip (tr ("Plays the first bars without opening it"));
        play.onClick = [this] { owner.togglePreview (index); };
        addAndMakeVisible (play);
    }

    void update()
    {
        const auto& e = owner.list[(std::size_t) index];
        const auto known = e.bpm > 0.0;
        play.setEnabled (! known || e.playable);
        play.setButtonText (owner.previewing == index ? tr ("Stop") : owner.rendering == index ? juce::String ("...") : tr ("Play"));
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& e = owner.list[(std::size_t) index];
        auto r = getLocalBounds().toFloat().reduced (1.0f, 2.0f);
        const auto sel = owner.selected == index, hover = isMouseOver (true);
        g.setColour (sel ? theme::colour::bg5 : hover ? theme::colour::bg4 : theme::colour::bg3);
        g.fillRoundedRectangle (r, theme::radius::md);
        g.setColour (sel ? theme::colour::accent : theme::colour::border);
        g.drawRoundedRectangle (r.reduced (0.5f), theme::radius::md, sel ? 1.5f : 1.0f);
        auto text = getLocalBounds().reduced (12, 6);
        text.removeFromRight (84);
        g.setColour (theme::colour::textPrimary);
        g.setFont (theme::font (theme::type::body, true));
        g.drawFittedText (e.name, text.removeFromTop (text.getHeight() / 2), juce::Justification::centredLeft, 1, 0.8f);
        using Kind = startfrom::Entry::Kind;
        auto line = e.kind == Kind::demo ? tr ("Demo song") : e.kind == Kind::user ? tr ("Your template") : tr ("Template");
        if (e.bpm > 0.0)
        {
            line << "  -  " << tr ("{0} BPM", { juce::String (juce::roundToInt (e.bpm)) });
            if (! e.playable)
                line << "  -  " << tr ("Empty: nothing to play yet");
            else if (e.bars > 1) // (a one-bar pattern is a loop, not a length worth naming)
                line << "  -  " << tr ("{0} bars", { juce::String (e.bars) });
        }
        g.setColour (theme::colour::textSecondary);
        g.setFont (theme::font (theme::type::small));
        g.drawFittedText (line, text, juce::Justification::centredLeft, 1, 0.8f);
    }

    void resized() override { play.setBounds (getLocalBounds().removeFromRight (84).reduced (10, 11)); }
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override { owner.select (index); }
    void mouseDoubleClick (const juce::MouseEvent&) override { owner.open (index); }

private:
    StartFromPanel& owner;
    int index;
    juce::TextButton play;
};

StartFromPanel::StartFromPanel (AudioEngine& e, Hooks h) : engine (e), hooks (std::move (h)), shared (std::make_shared<Shared>())
{
    setWantsKeyboardFocus (true);
    getProperties().set ("lynxKeepsKeys", true); // (round 190fc: its list keys while it is up; MainComponent::simpleClickEnded)
    setTitle (tr ("Start from"));
    title.setText (tr ("Start from"), juce::dontSendNotification);
    title.setFont (theme::font (theme::type::display, true));
    title.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (title);
    closeButton.setButtonText (juce::String::fromUTF8 ("\xc3\x97"));
    closeButton.setTooltip (tr ("Close"));
    closeButton.onClick = [this] {
        stopPreview();
        if (hooks.close)
            hooks.close();
    };
    addAndMakeVisible (closeButton);
    hint.setText (tr ("Opens as a new, untitled project."), juce::dontSendNotification);
    hint.setFont (theme::font (theme::type::small));
    hint.setColour (juce::Label::textColourId, theme::colour::textSecondary);
    addAndMakeVisible (hint);
    openButton.setButtonText (tr ("Open"));
    openButton.onClick = [this] { open (selected); };
    addAndMakeVisible (openButton);

    list = startfrom::entries();
    for (int i = 0; i < (int) list.size(); ++i)
    {
        rows.push_back (std::make_unique<Row> (*this, i));
        rowHolder.addAndMakeVisible (*rows.back());
    }
    viewport.setViewedComponent (&rowHolder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
    previewFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lynx-startfrom-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()) + ".wav");
    updateRows();
}

StartFromPanel::~StartFromPanel()
{
    stopTimer();
    shared->serial.fetch_add (1);
    stopPreview();
    previewFile.deleteFile();
}

juce::Rectangle<int> StartFromPanel::card() const
{
    const auto w = juce::jmin (620, getWidth() - 24), h = juce::jmin (520, getHeight() - 24);
    return getLocalBounds().withSizeKeepingCentre (juce::jmax (320, w), juce::jmax (300, h));
}

void StartFromPanel::updateRows()
{
    for (auto& r : rows)
        r->update();
    openButton.setEnabled (selected >= 0 && selected < (int) list.size());
}

void StartFromPanel::select (int index)
{
    if (index < 0 || index >= (int) list.size())
        return;
    selected = index;
    auto& e = list[(std::size_t) index];
    if (e.bpm <= 0.0) // a user's template: read once, now
        if (const auto song = startfrom::songOf (e))
            startfrom::describe (e, *song);
    updateRows();
}

void StartFromPanel::selectForSnapshot (int index) { select (index); }

void StartFromPanel::open (int index)
{
    if (index < 0 || index >= (int) list.size())
        return;
    stopPreview();
    auto song = startfrom::songOf (list[(std::size_t) index]);
    if (! song)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Start from"),
                                                tr ("{0} could not be opened.", { list[(std::size_t) index].name }));
        return;
    }
    if (hooks.open)
        hooks.open (std::move (*song));
}

bool StartFromPanel::openForSnapshot (int index)
{
    if (index < 0 || index >= (int) list.size() || ! startfrom::songOf (list[(std::size_t) index]))
        return false;
    open (index);
    return true;
}

void StartFromPanel::stopPreview()
{
    shared->serial.fetch_add (1); // (a render still running is dropped when it ends)
    if (previewing >= 0)
        engine.previewFile (juce::File());
    previewing = rendering = -1;
    stopTimer();
    updateRows();
}

void StartFromPanel::togglePreview (int index)
{
    const auto was = previewing >= 0 ? previewing : rendering;
    stopPreview();
    select (index);
    if (was == index)
        return;
    const auto song = startfrom::songOf (list[(std::size_t) index]);
    if (! song || ! list[(std::size_t) index].playable)
        return;
    rendering = index;
    const auto serial = shared->serial.load();
    shared->state.store (1);
    auto state = shared;
    // (each request renders to its own file: an older render may still be writing the last one)
    const auto file = previewFile.getSiblingFile (previewFile.getFileNameWithoutExtension() + "-" + juce::String (serial) + ".wav");
    std::thread ([state, serial, file, s = *song] {
        const auto ok = startfrom::renderPreview (s, file);
        if (state->serial.load() != serial)
        {
            file.deleteFile();
            return;
        }
        state->state.store (ok ? 2 : 3);
    }).detach();
    updateRows();
    startTimer (80);
}

void StartFromPanel::timerCallback()
{
    const auto state = shared->state.load();
    if (rendering >= 0 && state >= 2)
    {
        const auto file = previewFile.getSiblingFile (previewFile.getFileNameWithoutExtension() + "-" + juce::String (shared->serial.load()) + ".wav");
        shared->state.store (0);
        const auto index = rendering;
        rendering = -1;
        if (state == 2)
        {
            // (through the preview output as it is: never stretched to the open project's tempo)
            engine.getSampleLibrary().forget (file.getFullPathName());
            engine.previewSample (engine.getSampleLibrary().load (file.getFullPathName()), false, 1.0f);
            previewing = index;
            heard = false;
        }
        file.deleteFile(); // (the sound is in memory now)
        updateRows();
        return;
    }
    // the preview ran out: the row's button goes back to Play
    const auto sounding = engine.getPreviewPosition() >= 0.0;
    heard = heard || sounding;
    if (previewing >= 0 && heard && ! sounding)
    {
        previewing = -1;
        stopTimer();
        updateRows();
    }
}

void StartFromPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::colour::bg0.withAlpha (0.72f));
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

void StartFromPanel::resized()
{
    auto c = card().reduced (20, 14);
    auto top = c.removeFromTop (34);
    closeButton.setBounds (top.removeFromRight (28).withSizeKeepingCentre (28, 28));
    top.removeFromLeft (28);
    title.setBounds (top);
    c.removeFromTop (16);
    auto bottom = c.removeFromBottom (32);
    openButton.setBounds (bottom.removeFromRight (120));
    bottom.removeFromRight (10);
    hint.setBounds (bottom);
    c.removeFromBottom (10);
    viewport.setBounds (c);
    const auto rowH = 56;
    const auto w = c.getWidth() - ((int) rows.size() * rowH > c.getHeight() ? viewport.getScrollBarThickness() : 0);
    rowHolder.setSize (w, (int) rows.size() * rowH);
    for (int i = 0; i < (int) rows.size(); ++i)
        rows[(std::size_t) i]->setBounds (0, i * rowH, w, rowH);
}

bool StartFromPanel::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        closeButton.triggerClick();
        return true;
    }
    if (key == juce::KeyPress::returnKey)
    {
        open (selected);
        return true;
    }
    if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
    {
        select (juce::jlimit (0, (int) list.size() - 1, selected + (key == juce::KeyPress::downKey ? 1 : -1)));
        return true;
    }
    if (key == juce::KeyPress::spaceKey)
    {
        togglePreview (selected);
        return true;
    }
    return false;
}

void StartFromPanel::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && ! card().contains (e.getPosition()))
        closeButton.triggerClick();
}
} // namespace lynx
