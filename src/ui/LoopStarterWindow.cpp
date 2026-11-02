#include "ui/LoopStarterWindow.h"

#include <chrono>

#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/ToolWindow.h"
#include "ui/ValueControl.h"

namespace lynx::loopstarterui
{
namespace
{
std::uint64_t timeSeed()
{
    // FL seeds with time.time_ns() // 100 (toolbox rand_int)
    return (std::uint64_t) std::chrono::duration_cast<std::chrono::nanoseconds> (std::chrono::system_clock::now().time_since_epoch()).count() / 100u;
}

loopstarter::FlRand& rng()
{
    static loopstarter::FlRand r (timeSeed());
    return r;
}

std::string readText (const juce::File& f) { return f.loadFileAsString().toStdString(); }

struct Data
{
    loopstarter::Config config;
    loopstarter::Library library;
    loopstarter::Sources sources;
    std::map<int, std::vector<std::string>> recent; // channel -> its last samples (a re-roll avoids them)
};

Data& data()
{
    static Data d = [] {
        Data out;
        const auto roots = findFlStudioRoots();
        const juce::File factory (juce::String::fromUTF8 (roots.factoryData.c_str()));
        const auto notes = factory.getChildFile ("System/Tools/Chord progression/Notes");
        if (auto c = roots.factoryData.empty() ? std::nullopt : loopstarter::parseConfig (readText (notes.getChildFile ("genres.lstj"))))
            out.config = std::move (*c);
        else
            out.config = loopstarter::builtinConfig();
        // FL's "<genre> presets.lstj" (the file system may be case-sensitive: match the names ignoring case)
        if (out.config.fromFl)
        {
            const auto files = notes.getChildFile ("content").findChildFiles (juce::File::findFiles, false, "*.lstj");
            for (const auto& g : out.config.genres)
                for (const auto& f : files)
                    if (f.getFileName().equalsIgnoreCase (juce::String::fromUTF8 (loopstarter::presetFileName (g.id).c_str())))
                        out.sources.presets[g.id] = loopstarter::parsePresets (readText (f));
        }
        // the local library: FL's installed packs (LYNX's built-in drums play the categories it has none of)
        const juce::File packs (juce::String::fromUTF8 (roots.data.c_str()));
        if (! roots.data.empty())
            for (const auto& f : packs.getChildFile ("Patches/Packs").findChildFiles (juce::File::findFiles, true, "*.wav;*.wv;*.flac;*.ogg;*.mp3;*.aif;*.aiff"))
                out.library.add (f.getFullPathName().toStdString());
        return out;
    }();
    d.sources.config = &d.config;
    d.sources.library = &d.library;
    return d;
}

juce::String categoryLabel (const std::string& category)
{
    static const std::map<std::string, const char*> names {
        { "kick", "Kick" }, { "sub kick", "Sub kick" }, { "snare", "Snare" }, { "ghost snare", "Ghost snare" }, { "clap", "Clap" },
        { "rim", "Rim" }, { "closed hat", "Closed hat" }, { "open hat", "Open hat" }, { "extra hat", "Extra hat" },
        { "ride", "Ride" }, { "crash", "Crash" }, { "cymbals", "Cymbals" }, { "percussion", "Percussion" }, { "shaker", "Shaker" },
        { "tom", "Tom" }, { "hand drums", "Hand drums" }, { "cowbell", "Cowbell" },
    };
    const auto it = names.find (category);
    return it != names.end() ? tr (it->second) : juce::String::fromUTF8 (category.c_str());
}

// the one-shot categories offered for a channel (FL's "One-shot category" list): the genre's kit, then the rest
std::vector<std::string> categoriesFor (const std::string& genreId)
{
    std::vector<std::string> out;
    if (const auto* g = data().config.find (genreId))
        for (const auto& [c, w] : g->kit)
            out.push_back (c);
    for (const char* c : { "kick", "snare", "clap", "closed hat", "open hat", "percussion", "shaker", "rim", "tom", "ride", "crash" })
        if (std::find (out.begin(), out.end(), c) == out.end())
            out.push_back (c);
    return out;
}

std::string genreOf (const Song& s, int channel)
{
    if (channel < 0 || channel >= (int) s.channels.size())
        return {};
    const auto p = s.channels[(std::size_t) channel].loopStarterPattern;
    return p >= 0 && p < (int) s.patterns.size() ? s.patterns[(std::size_t) p].loopStarterGenre : std::string();
}

void applySample (ChannelDef& c, const std::string& path)
{
    if (path.empty())
    {
        c.instrument = loopstarter::builtinDrumFor (c.loopStarterCategory);
        c.samplePath.clear();
    }
    else
    {
        c.instrument = InstrumentKind::sampler;
        c.samplePath = path;
    }
    c.name = categoryLabel (c.loopStarterCategory).toStdString();
}

int currentLoopPattern (const Song& s)
{
    const auto p = s.currentPattern;
    return p >= 0 && p < (int) s.patterns.size() && ! s.patterns[(std::size_t) p].loopStarterGenre.empty() ? p : -1;
}
} // namespace

const loopstarter::Sources& sources() { return data().sources; }

void replaceSamples (AudioEngine& engine, const std::vector<int>& channels)
{
    auto& d = data();
    const auto& song = engine.getSong();
    std::vector<std::pair<int, std::string>> picks;
    for (const auto ch : channels)
    {
        if (ch < 0 || ch >= (int) song.channels.size() || song.channels[(std::size_t) ch].loopStarterPattern < 0)
            continue;
        const auto& c = song.channels[(std::size_t) ch];
        // "Replace samples in all Loop Starter channels" leaves the locked ones (FL's menu: "Locked: Don't replace the
        // channel's sample when all samples are replaced"); a single channel's dice replaces it anyway
        if (channels.size() > 1 && c.loopStarterLocked)
            continue;
        auto& recent = d.recent[ch];
        if (! c.samplePath.empty() && std::find (recent.begin(), recent.end(), c.samplePath) == recent.end())
            recent.push_back (c.samplePath);
        while ((int) recent.size() > d.config.sameSampleBlockCount)
            recent.erase (recent.begin());
        auto path = loopstarter::pickSample (d.library, c.loopStarterCategory, rng(), recent);
        picks.emplace_back (ch, path);
        if (! path.empty())
            recent.push_back (path);
    }
    if (picks.empty())
        return;
    engine.editSong ([&] (Song& s) {
        for (const auto& [ch, path] : picks)
            applySample (s.channels[(std::size_t) ch], path);
    });
}

void generateSteps (AudioEngine& engine, const std::vector<int>& channels)
{
    const auto& song = engine.getSong();
    struct Job { int channel, pattern, bars; std::vector<Note> notes; };
    std::vector<Job> jobs;
    for (const auto ch : channels)
    {
        if (ch < 0 || ch >= (int) song.channels.size())
            continue;
        const auto& c = song.channels[(std::size_t) ch];
        if (c.loopStarterPattern < 0 || c.loopStarterPattern >= (int) song.patterns.size())
            continue;
        Job j { ch, c.loopStarterPattern, 1, {} };
        j.notes = loopstarter::generateSteps (sources(), genreOf (song, ch), c.loopStarterCategory, rng(), &j.bars);
        jobs.push_back (std::move (j));
    }
    if (jobs.empty())
        return;
    engine.editSong ([&] (Song& s) {
        for (const auto& j : jobs)
            loopstarter::setChannelNotes (s, j.pattern, j.channel, j.notes, j.bars);
    });
}

void setCategory (AudioEngine& engine, int channel, const std::string& category)
{
    const auto& song = engine.getSong();
    if (channel < 0 || channel >= (int) song.channels.size() || song.channels[(std::size_t) channel].loopStarterPattern < 0)
        return;
    // a new category: a sample and steps of it (FL's "One-shot category" re-rolls the channel)
    const auto path = loopstarter::pickSample (data().library, category, rng());
    int bars = 1;
    const auto notes = loopstarter::generateSteps (sources(), genreOf (song, channel), category, rng(), &bars);
    const auto pattern = song.channels[(std::size_t) channel].loopStarterPattern;
    engine.editSong ([&] (Song& s) {
        auto& c = s.channels[(std::size_t) channel];
        c.loopStarterCategory = category;
        applySample (c, path);
        loopstarter::setChannelNotes (s, pattern, channel, notes, bars);
    });
}

void setLocked (AudioEngine& engine, int channel, bool locked)
{
    if (channel >= 0 && channel < (int) engine.getSong().channels.size())
        engine.editSong ([channel, locked] (Song& s) { s.channels[(std::size_t) channel].loopStarterLocked = locked; });
}

void addChannelMenu (juce::PopupMenu& menu, AudioEngine& engine, int channel)
{
    const auto& song = engine.getSong();
    if (channel < 0 || channel >= (int) song.channels.size() || song.channels[(std::size_t) channel].loopStarterPattern < 0)
        return;
    const auto& c = song.channels[(std::size_t) channel];
    juce::PopupMenu sub;
    // FL's channel dice menu (FLEngine 0x01034d90): -Sound, Replace sample, Generate steps, Locked, -One-shot category
    sub.addSectionHeader (tr ("Sound"));
    sub.addItem (tr ("Replace sample"), [&engine, channel] { replaceSamples (engine, { channel }); });
    sub.addItem (tr ("Generate steps"), [&engine, channel] { generateSteps (engine, { channel }); });
    sub.addItem (tr ("Locked"), true, c.loopStarterLocked, [&engine, channel, on = ! c.loopStarterLocked] { setLocked (engine, channel, on); });
    sub.addSectionHeader (tr ("One-shot category"));
    for (const auto& cat : categoriesFor (genreOf (song, channel)))
        sub.addItem (categoryLabel (cat), true, cat == c.loopStarterCategory, [&engine, channel, cat] { setCategory (engine, channel, cat); });
    menu.addSubMenu (tr ("Loop Starter"), sub);
}

namespace
{
class LoopStarterPanel final : public juce::Component,
                               private juce::ChangeListener
{
public:
    explicit LoopStarterPanel (AudioEngine& e) : engine (e)
    {
        const auto& config = data().config;
        int id = 1;
        for (const auto* g : config.listed())
        {
            genreBox.addItem (juce::String::fromUTF8 (g->label.c_str()), id++);
            genreIds.push_back (g->id);
        }
        genreBox.onChange = [this] { genreChanged(); };
        ui::setHint (genreBox, tr ("Loop Starter genre: its tempo range, kit and drum patterns; changing it re-rolls the loop"));
        addAndMakeVisible (genreBox);

        newButton.setButtonText (tr ("New loop"));
        newButton.onClick = [this] { newLoop(); };
        ui::setHint (newButton, tr ("A new \"Loop Starter\" pattern with the genre's kit channels and generated steps"));
        diceButton.onClick = [this] { diceAll(); };
        ui::setHint (diceButton, tr ("Replace the samples of every unlocked Loop Starter channel (with channels selected in the Channel Rack: only those)"));
        stepsButton.setButtonText (tr ("Generate steps"));
        stepsButton.onClick = [this] { generateSteps (engine, channelsHere()); };
        ui::setHint (stepsButton, tr ("New steps for every channel of the loop"));
        sendButton.setButtonText (tr ("Send to Playlist"));
        sendButton.onClick = [this] { send(); };
        ui::setHint (sendButton, tr ("Place the loop's pattern in the Playlist at the playhead; its channels leave Loop Starter"));
        for (auto* b : std::initializer_list<juce::Component*> { &newButton, &diceButton, &stepsButton, &sendButton })
        {
            b->setWantsKeyboardFocus (false);
            addAndMakeVisible (b);
        }

        info.setFont (theme::font (theme::type::small));
        info.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        info.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (info);

        engine.addChangeListener (this);
        refresh();
    }

    ~LoopStarterPanel() override { engine.removeChangeListener (this); }

    void newLoop()
    {
        const auto genre = selectedGenre();
        const auto plan = loopstarter::generate (sources(), genre, rng().nextInt() + timeSeed());
        engine.editSong ([&] (Song& s) { loopstarter::apply (s, plan); });
        refresh();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg2);
        g.setColour (theme::colour::textSecondary);
        g.setFont (theme::font (theme::type::small, true));
        g.drawText (tr ("Genre"), getLocalBounds().reduced (10).removeFromTop (24).removeFromLeft (60), juce::Justification::centredLeft);
        if (rows.empty())
        {
            g.setFont (theme::font (theme::type::small));
            g.drawFittedText (tr ("Pick a genre and click New loop. The kit's samples come from the sample packs installed on this "
                                  "computer (FL Studio's packs when FL is installed, else LYNX's built-in drums); FL Studio's "
                                  "audio loops from FL Cloud are not used."),
                              getLocalBounds().reduced (12).withTop (100).withHeight (60), juce::Justification::topLeft, 4);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10);
        auto top = r.removeFromTop (24);
        top.removeFromLeft (60);
        genreBox.setBounds (top.removeFromLeft (170));
        top.removeFromLeft (8);
        newButton.setBounds (top.removeFromLeft (110));
        r.removeFromTop (8);
        auto tools = r.removeFromTop (26);
        diceButton.setBounds (tools.removeFromLeft (34));
        tools.removeFromLeft (6);
        stepsButton.setBounds (tools.removeFromLeft (130));
        tools.removeFromLeft (6);
        sendButton.setBounds (tools.removeFromLeft (140));
        r.removeFromTop (6);
        info.setBounds (r.removeFromTop (20));
        r.removeFromTop (4);
        for (auto& row : rows)
        {
            auto line = r.removeFromTop (26);
            r.removeFromTop (3);
            row->name.setBounds (line.removeFromLeft (150));
            row->category.setBounds (line.removeFromLeft (130).reduced (0, 1));
            line.removeFromLeft (6);
            row->dice.setBounds (line.removeFromLeft (28));
            line.removeFromLeft (4);
            row->steps.setBounds (line.removeFromLeft (64));
            line.removeFromLeft (6);
            row->locked.setBounds (line.removeFromLeft (90));
        }
    }

    int idealHeight() const { return 10 + 24 + 8 + 26 + 6 + 20 + 4 + std::max (2, (int) rows.size()) * 29 + 40; }

private:
    struct Row
    {
        int channel = -1;
        juce::Label name;
        juce::ComboBox category;
        IconButton dice { "lsRowDice", Icon::dice };
        juce::TextButton steps;
        juce::ToggleButton locked;
        std::vector<std::string> categories;
    };

    std::string selectedGenre() const
    {
        const auto i = genreBox.getSelectedItemIndex();
        if (i >= 0 && i < (int) genreIds.size())
            return genreIds[(std::size_t) i];
        return data().config.defaultGenre;
    }

    std::vector<int> channelsHere() const { return loopstarter::channelsOf (engine.getSong(), currentLoopPattern (engine.getSong())); }

    void diceAll()
    {
        // FLEngine 0x01044550: with channels selected only those, else all (the locked ones keep their sample)
        auto chans = channelsHere();
        std::vector<int> selected;
        for (const auto c : engine.getSelectedChannels())
            if (std::find (chans.begin(), chans.end(), c) != chans.end())
                selected.push_back (c);
        if (selected.size() > 1 && selected.size() < chans.size())
            chans = selected;
        if (chans.size() == 1)
            chans.push_back (-1); // (still "all": the locked channel stays)
        replaceSamples (engine, chans);
    }

    void genreChanged()
    {
        if (refreshing)
            return;
        const auto p = currentLoopPattern (engine.getSong());
        if (p < 0)
            return;
        // FL's "change Loop Starter genre": the loop is rolled again in the new genre (samples of the unlocked
        // channels, steps of all) and the tempo picked in its range
        const auto genre = selectedGenre();
        const auto* g = data().config.find (genre);
        const auto bpm = g != nullptr ? loopstarter::pickTempo (*g, rng()) : engine.getSong().bpm;
        engine.editSong ([&] (Song& s) {
            s.patterns[(std::size_t) p].loopStarterGenre = genre;
            s.bpm = bpm;
        });
        auto chans = channelsHere();
        chans.push_back (-1);
        replaceSamples (engine, chans);
        generateSteps (engine, channelsHere());
    }

    void send()
    {
        const auto p = currentLoopPattern (engine.getSong());
        if (p < 0)
            return;
        const auto at = engine.getTransport().positionTicks.load();
        int track = -1;
        engine.editSong ([&] (Song& s) { track = loopstarter::sendToPlaylist (s, p, at); });
        if (track < 0)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Failed to insert track"),
                                                    tr ("There aren't enough empty playlist tracks available."));
        refresh();
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        const auto& s = engine.getSong();
        std::vector<std::string> now;
        for (const auto ch : channelsHere())
        {
            const auto& c = s.channels[(std::size_t) ch];
            now.push_back (std::to_string (ch) + c.name + c.loopStarterCategory + (c.loopStarterLocked ? "1" : "0"));
        }
        const auto p = currentLoopPattern (s);
        now.push_back (p >= 0 ? s.patterns[(std::size_t) p].loopStarterGenre : std::string ("-"));
        now.push_back (std::to_string ((int) s.bpm));
        if (now != shown)
            refresh();
    }

    void refresh()
    {
        refreshing = true;
        const auto& s = engine.getSong();
        const auto p = currentLoopPattern (s);
        const auto genre = p >= 0 ? s.patterns[(std::size_t) p].loopStarterGenre : data().config.defaultGenre;
        for (std::size_t i = 0; i < genreIds.size(); ++i)
            if (genreIds[i] == genre)
                genreBox.setSelectedItemIndex ((int) i, juce::dontSendNotification);
        if (genreBox.getSelectedItemIndex() < 0 && ! genreIds.empty())
            genreBox.setSelectedItemIndex (0, juce::dontSendNotification);

        diceButton.setEnabled (p >= 0);
        stepsButton.setEnabled (p >= 0);
        sendButton.setEnabled (p >= 0);
        info.setText (p >= 0 ? juce::String::fromUTF8 (s.patterns[(std::size_t) p].name.c_str()) + "  ·  " + juce::String (juce::roundToInt (s.bpm)) + " BPM"
                             : tr ("The current pattern is not a Loop Starter pattern."),
                      juce::dontSendNotification);

        rows.clear();
        shown.clear();
        for (const auto ch : channelsHere())
        {
            const auto& c = s.channels[(std::size_t) ch];
            auto row = std::make_unique<Row>();
            row->channel = ch;
            row->name.setText (juce::String::fromUTF8 (c.loopStarterSlot.c_str()) + "  " + juce::String::fromUTF8 (c.name.c_str()), juce::dontSendNotification);
            row->name.setTooltip (c.samplePath.empty() ? tr ("LYNX's built-in drum") : juce::String::fromUTF8 (c.samplePath.c_str()));
            row->categories = categoriesFor (genre);
            for (std::size_t i = 0; i < row->categories.size(); ++i)
                row->category.addItem (categoryLabel (row->categories[i]), (int) i + 1);
            for (std::size_t i = 0; i < row->categories.size(); ++i)
                if (row->categories[i] == c.loopStarterCategory)
                    row->category.setSelectedItemIndex ((int) i, juce::dontSendNotification);
            auto* raw = row.get();
            row->category.onChange = [this, raw] {
                const auto i = raw->category.getSelectedItemIndex();
                if (i >= 0 && i < (int) raw->categories.size())
                    juce::MessageManager::callAsync ([this, ch = raw->channel, cat = raw->categories[(std::size_t) i], safe = juce::Component::SafePointer (this)] {
                        if (safe != nullptr)
                            setCategory (engine, ch, cat);
                    });
            };
            ui::setHint (row->category, tr ("One-shot category: the kind of drum this channel plays"));
            row->dice.onClick = [this, ch] { replaceSamples (engine, { ch }); };
            ui::setHint (row->dice, tr ("Replace sample: the channel's sample with a similar other sample"));
            row->steps.setButtonText (tr ("Steps"));
            row->steps.onClick = [this, ch] { generateSteps (engine, { ch }); };
            ui::setHint (row->steps, tr ("Generate steps: new steps for this channel"));
            row->locked.setButtonText (tr ("Locked"));
            row->locked.setToggleState (c.loopStarterLocked, juce::dontSendNotification);
            row->locked.onClick = [this, raw] { setLocked (engine, raw->channel, raw->locked.getToggleState()); };
            ui::setHint (row->locked, tr ("Locked: don't replace the channel's sample when all samples are replaced"));
            for (auto* comp : std::initializer_list<juce::Component*> { &row->name, &row->category, &row->dice, &row->steps, &row->locked })
            {
                comp->setWantsKeyboardFocus (false);
                addAndMakeVisible (comp);
            }
            shown.push_back (std::to_string (ch) + c.name + c.loopStarterCategory + (c.loopStarterLocked ? "1" : "0"));
            rows.push_back (std::move (row));
        }
        shown.push_back (p >= 0 ? genre : std::string ("-"));
        shown.push_back (std::to_string ((int) s.bpm));
        refreshing = false;

        const auto h = idealHeight();
        if (getHeight() != h)
            setSize (getWidth() > 0 ? getWidth() : 520, h);
        resized();
        repaint();
    }

    AudioEngine& engine;
    juce::ComboBox genreBox;
    std::vector<std::string> genreIds;
    juce::TextButton newButton, stepsButton, sendButton;
    IconButton diceButton { "lsDice", Icon::dice };
    juce::Label info;
    std::vector<std::unique_ptr<Row>> rows;
    std::vector<std::string> shown;
    bool refreshing = false;
};
} // namespace

void open (AudioEngine& engine)
{
    auto panel = std::make_unique<LoopStarterPanel> (engine);
    panel->setSize (520, panel->idealHeight());
    openToolWindow (std::move (panel), tr ("Loop Starter"), "loop-starter");
}

std::unique_ptr<juce::Component> makeForSnapshot (AudioEngine& engine, bool generate)
{
    auto panel = std::make_unique<LoopStarterPanel> (engine);
    if (generate)
        panel->newLoop();
    panel->setSize (520, panel->idealHeight());
    return panel;
}
} // namespace lynx::loopstarterui
