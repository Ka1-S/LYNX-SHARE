#include "ui/PluginsOffPanel.h"

#include "engine/ProjectFile.h"
#include "platform/CrashGuard.h"
#include "ui/I18n.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
class PluginsOffPanel final : public juce::Component,
                              private juce::ListBoxModel,
                              private juce::ChangeListener
{
public:
    explicit PluginsOffPanel (AudioEngine& e) : engine (e)
    {
        title.setText (tr ("Third-party plugins are off"), juce::dontSendNotification);
        title.setFont (theme::font (theme::type::title, true));
        addAndMakeVisible (title);
        note.setText (tr ("This project was opened without its third-party plugins. Their settings are kept, also if you save now. "
                          "Switch them on one at a time to find the one that fails."),
                      juce::dontSendNotification);
        note.setFont (theme::font (theme::type::small));
        note.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        note.setJustificationType (juce::Justification::topLeft);
        note.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (note);

        list.setModel (this);
        list.setRowHeight (rowHeight);
        list.setColour (juce::ListBox::backgroundColourId, theme::colour::bg1);
        list.setOutlineThickness (0);
        addAndMakeVisible (list);

        onButton.setColour (juce::TextButton::buttonColourId, theme::colour::accent);
        onButton.setColour (juce::TextButton::textColourOffId, theme::colour::onAccent);
        onButton.setTooltip (tr ("Loads the chosen plugin with the settings kept in the project (every slot that uses it)"));
        allButton.setTooltip (tr ("Loads every plugin that is still off: the project is then as after a normal open"));
        addAndMakeVisible (onButton);
        addAndMakeVisible (allButton);
        onButton.onClick = [this] { switchOn (list.getSelectedRow()); };
        allButton.onClick = [this] { switchAll(); };

        engine.addChangeListener (this);
        refresh();
        setSize (560, 400);
    }

    ~PluginsOffPanel() override { engine.removeChangeListener (this); }

    void paint (juce::Graphics& g) override { g.fillAll (theme::colour::bg2); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (12, 10);
        title.setBounds (area.removeFromTop (24));
        note.setBounds (area.removeFromTop (36));
        area.removeFromTop (4);
        auto buttons = area.removeFromBottom (30);
        area.removeFromBottom (8);
        onButton.setBounds (buttons.removeFromLeft (120));
        allButton.setBounds (buttons.removeFromRight (140));
        list.setBounds (area);
    }

    void selectRowForSnapshot (int row) { list.selectRow (row); }

private:
    static constexpr int rowHeight = 42;

    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

    void refresh()
    {
        auto fresh = engine.hostedPluginUses();
        const auto failures = engine.getPluginHost().getLoadFailures();
        const auto off = engine.pluginsOff();
        if (fresh == uses && failures == failed && off == modeOn)
            return;
        const auto selected = list.getSelectedRow() >= 0 && list.getSelectedRow() < (int) uses.size() ? uses[(std::size_t) list.getSelectedRow()].id : std::string();
        uses = std::move (fresh);
        failed = failures;
        modeOn = off;
        title.setText (off ? tr ("Third-party plugins are off") : tr ("Third-party plugins are on"), juce::dontSendNotification);
        list.updateContent();
        list.deselectAllRows();
        for (int i = 0; i < (int) uses.size(); ++i)
            if (uses[(std::size_t) i].id == selected)
                list.selectRow (i);
        list.repaint();
        updateButtons();
    }

    void updateButtons()
    {
        const auto row = list.getSelectedRow();
        onButton.setEnabled (row >= 0 && row < (int) uses.size() && uses[(std::size_t) row].off);
        allButton.setEnabled (std::any_of (uses.begin(), uses.end(), [] (const pluginhold::Use& u) { return u.off; }));
    }

    void switchOn (int row)
    {
        if (row < 0 || row >= (int) uses.size() || ! uses[(std::size_t) row].off)
            return;
        const auto use = uses[(std::size_t) row];
        // (a crash while it loads: the session's last step names the plugin)
        crashguard::note ("switching on plugin " + use.name);
        engine.switchPluginOn (use.id);
        refresh();
    }

    void switchAll()
    {
        crashguard::note ("switching on all plugins");
        engine.switchPluginOn ({});
        refresh();
    }

    // the plugin did not come up after it was switched on (the host's own line: "name: reason")
    juce::String failureOf (const pluginhold::Use& u) const
    {
        const auto name = juce::String::fromUTF8 (u.name.c_str());
        for (const auto& line : failed)
            if (line.startsWith (name + ":"))
                return line.fromFirstOccurrenceOf (":", false, false).trim();
        return {};
    }

    int getNumRows() override { return (int) uses.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (row < 0 || row >= (int) uses.size())
            return;
        const auto& u = uses[(std::size_t) row];
        if (selected)
        {
            g.setColour (theme::colour::accentMuted);
            g.fillRect (0, 0, width, height);
        }
        g.setColour (theme::colour::border);
        g.fillRect (0, height - 1, width, 1);

        const auto failure = u.off ? juce::String() : failureOf (u);
        const int stateWidth = 96;
        // the state tag on the right: Off (grey), On (accent), Did not load (warning)
        const auto tag = juce::Rectangle<float> ((float) (width - stateWidth - 10), 11.0f, (float) stateWidth, 20.0f);
        const auto tagColour = u.off ? theme::colour::textDisabled : failure.isNotEmpty() ? theme::colour::warning : theme::colour::accent;
        g.setColour (tagColour);
        g.drawRoundedRectangle (tag, 4.0f, 1.0f);
        g.setFont (theme::font (theme::type::small, ! u.off));
        g.drawText (u.off ? tr ("Off") : failure.isNotEmpty() ? tr ("Did not load") : tr ("On"), tag, juce::Justification::centred, true);

        const auto textWidth = width - stateWidth - 34;
        g.setFont (theme::font (theme::type::body, true));
        g.setColour (u.off ? theme::colour::textSecondary : theme::colour::textPrimary);
        g.drawText (juce::String::fromUTF8 (u.name.c_str()), 12, 4, textWidth, 18, juce::Justification::centredLeft, true);

        juce::StringArray where;
        for (const auto& w : u.usedBy)
            where.add (ownerText (w));
        auto line = tr ("{0} slot(s)", { juce::String (u.instruments + u.effects) }) + "  ·  " + where.joinIntoString (", ");
        if (failure.isNotEmpty())
            line = failure;
        g.setFont (theme::font (theme::type::small));
        g.setColour (failure.isNotEmpty() ? theme::colour::warning : theme::colour::textDisabled);
        g.drawText (line, 12, 22, textWidth, 16, juce::Justification::centredLeft, true);
    }

    void selectedRowsChanged (int) override { updateButtons(); }
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { switchOn (row); }

    juce::String getTooltipForRow (int row) override
    {
        if (row < 0 || row >= (int) uses.size())
            return {};
        juce::StringArray where;
        for (const auto& w : uses[(std::size_t) row].usedBy)
            where.add (ownerText (w));
        return where.joinIntoString ("\n");
    }

    // pluginhold's owner ("Channel: Lead", "Master", "Insert 3: Vocals", each maybe with " (Patcher)") in the UI language
    static juce::String ownerText (const std::string& owner)
    {
        const auto text = juce::String::fromUTF8 (owner.c_str());
        if (text.startsWith ("Channel: "))
            return tr ("Channel: {0}", { text.substring (9) });
        if (text.startsWith ("Master"))
            return tr ("Master") + text.substring (6);
        if (text.startsWith ("Insert "))
        {
            const auto number = text.substring (7).initialSectionContainingOnly ("0123456789");
            return tr ("Insert {0}", { number }) + text.substring (7 + number.length());
        }
        return text;
    }

    AudioEngine& engine;
    std::vector<pluginhold::Use> uses;
    juce::StringArray failed;
    bool modeOn = false;
    juce::Label title, note;
    juce::ListBox list;
    juce::TextButton onButton { tr ("Switch on") }, allButton { tr ("Switch all on") };
};
} // namespace

std::unique_ptr<juce::Component> makePluginsOffPanel (AudioEngine& engine)
{
    return std::make_unique<PluginsOffPanel> (engine);
}

juce::String pluginsOffSelfTest()
{
    const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("lynx-pluginsoff-selftest-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
    if (! folder.createDirectory())
        return "could not create " + folder.getFullPathName();
    const juce::ScopeGuard cleanUp { [&folder] { folder.deleteRecursively(); } };

    // a project with hosted plugins everywhere one can be: two channels (one plugin twice), insert slots, a Patcher
    // with a generator and an effect node, and a LYNX substitute that keeps its original's reference. The plugins are
    // not installed (the test must never run third-party code); their settings are long, binary-looking strings.
    const auto stateOf = [] (int seed) {
        juce::MemoryBlock block ((size_t) (3000 + seed * 517));
        juce::Random random (seed);
        for (size_t i = 0; i < block.getSize(); ++i)
            block[i] = (char) random.nextInt (256);
        return block.toBase64Encoding().toStdString();
    };
    const auto ref = [&stateOf] (const char* id, const char* name, int seed) {
        PluginRef r;
        r.id = id;
        r.name = name;
        r.state = stateOf (seed);
        return r;
    };
    auto song = Song::makeDefault();
    while (song.channels.size() < 3)
        song.channels.push_back (song.channels.front());
    while (song.mixer.size() < 4)
        song.mixer.push_back (song.mixer.back());
    song.channels[0].instrument = InstrumentKind::plugin;
    song.channels[0].plugin = ref ("VST3-TestSynth-11111111-aaaaaaaa", "Test Synth", 1);
    song.channels[1].instrument = InstrumentKind::plugin;
    song.channels[1].plugin = ref ("VST3-TestSynth-11111111-aaaaaaaa", "Test Synth", 2); // the same plugin, other settings
    {
        EffectSlot fx;
        fx.kind = EffectKind::plugin;
        fx.plugin = ref ("VST3-TestVerb-22222222-bbbbbbbb", "Test Verb", 3);
        song.mixer[1].effects.push_back (fx);
        fx.plugin = ref ("CLAP-TestComp-33333333-cccccccc", "Test Comp", 4);
        song.mixer[0].effects.push_back (fx);
        // a Patcher on insert 2: a plugin generator and a plugin effect inside
        EffectSlot patcher = makeEffectSlot (EffectKind::patcher);
        PatchNode generator;
        generator.generator = true;
        generator.channel.instrument = InstrumentKind::plugin;
        generator.channel.plugin = ref ("VST3-TestSynth-11111111-aaaaaaaa", "Test Synth", 5);
        PatchNode effect;
        effect.effect.kind = EffectKind::plugin;
        effect.effect.plugin = ref ("VST3-TestDelay-44444444-dddddddd", "Test Delay", 6);
        patcher.patcher.nodes.push_back (generator);
        patcher.patcher.nodes.push_back (effect);
        song.mixer[2].effects.push_back (patcher);
        // a substitute (a LYNX effect standing for a plugin that is not installed) keeps the original
        EffectSlot substitute = makeEffectSlot (EffectKind::compressor);
        substitute.substituteFor = ref ("VST3-TestLimiter-55555555-eeeeeeee", "Test Limiter", 7);
        song.mixer[3].effects.push_back (substitute);
    }
    const auto original = pluginhold::pluginStates (song);
    if (original.size() != 7)
        return "the test project has " + juce::String ((int) original.size()) + " plugin settings, not 7";

    const auto first = folder.getChildFile ("first.lynx"), second = folder.getChildFile ("second.lynx"), third = folder.getChildFile ("third.lynx");
    juce::String error;
    if (! ProjectFile::save (song, first, error))
        return "could not save the test project: " + error;
    auto loaded = ProjectFile::load (first, error);
    if (! loaded)
        return "could not load the test project: " + error;
    if (pluginhold::pluginStates (*loaded) != original)
        return "the test project does not read back as written";

    {
        AudioEngine engine (false, 0, false);
        int asked = 0;
        juce::StringArray askedFor;
        engine.getPluginHost().onInstantiate = [&] (const juce::String& name) {
            ++asked;
            askedFor.add (name);
        };
        // 1. opened with the plugins off: none is asked for, every slot is marked off, nothing counts as a failure
        engine.setSongWithPluginsOff (*loaded);
        if (! engine.pluginsOff() || ! pluginsHeld())
            return "the mode is not on after the open";
        if (asked != 0)
            return "plugins were created with the mode on: " + askedFor.joinIntoString (", ");
        if (! engine.getPluginHost().getLoadFailures().isEmpty())
            return "a switched-off plugin was reported as a load failure";
        const auto uses = engine.hostedPluginUses();
        if (uses.size() != 4 || ! std::all_of (uses.begin(), uses.end(), [] (const pluginhold::Use& u) { return u.off; }))
            return "the plugin list is not 4 plugins, all off";
        if (uses[0].instruments != 3 || uses[0].effects != 0 || uses[0].usedBy.size() != 3)
            return "the first plugin's slots are not counted (2 channels and a Patcher node)";
        if (! engine.channelPluginOff (0) || ! engine.channelPluginOff (1) || engine.channelPluginOff (2) || ! engine.insertPluginOff (0)
            || ! engine.insertPluginOff (1) || ! engine.insertPluginOff (2) || engine.insertPluginOff (3))
            return "the off badges are on the wrong channels or inserts";
        // (a substitute stays a substitute, and applySubstitutes changes nothing in this mode)
        {
            auto copy = engine.getSong();
            if (engine.getPluginHost().applySubstitutes (copy) != 0 || pluginhold::pluginStates (copy) != original)
                return "substitutes were changed with the mode on";
        }
        // an export's engine (another PluginHost) must not create them either
        {
            AudioEngine other (false, 0, false);
            int otherAsked = 0;
            other.getPluginHost().onInstantiate = [&] (const juce::String&) { ++otherAsked; };
            other.setSong (engine.getSong());
            if (otherAsked != 0)
                return "another engine created plugins while the mode was on";
            if (! pluginsHeld())
                return "another engine's project ended the mode";
        }

        // 2. the saves a user can make in this state: Save, a timed backup, edits and undo in between
        for (int pass = 0; pass < 3; ++pass)
        {
            engine.capturePluginStates();
            if (pluginhold::pluginStates (engine.getSong()) != original)
                return "capturing the plugin settings changed them (pass " + juce::String (pass) + ")";
            if (! ProjectFile::save (engine.getSong(), second, error))
                return "could not save with the mode on: " + error;
            auto back = ProjectFile::load (second, error);
            if (! back)
                return "the project saved with the mode on does not load: " + error;
            if (pluginhold::pluginStates (*back) != original)
                return "a save made with the plugins off lost plugin settings (pass " + juce::String (pass) + ")";
            // an edit, its undo and redo, then save again
            engine.editSong ([pass] (Song& s) { s.bpm = 100.0 + pass; });
            if (pass == 1)
            {
                engine.undo();
                engine.redo();
            }
        }
        if (asked != 0)
            return "plugins were created while saving and editing with the mode on";

        // 3. one plugin back on: only its slots are asked for (2 channels + the Patcher's node; the Patcher is built
        // again, and its other plugin is still off), the others stay off
        asked = 0;
        askedFor.clear();
        engine.switchPluginOn ("VST3-TestSynth-11111111-aaaaaaaa");
        if (asked != 3 || askedFor.contains ("Test Verb") || askedFor.contains ("Test Comp") || askedFor.contains ("Test Delay"))
            return "switching one plugin on asked for: " + askedFor.joinIntoString (", ");
        if (! engine.pluginsOff() || engine.channelPluginOff (0) || ! engine.insertPluginOff (1) || ! engine.insertPluginOff (2))
            return "the others did not stay off after one was switched on";
        engine.capturePluginStates();
        if (pluginhold::pluginStates (engine.getSong()) != original)
            return "switching a plugin on changed plugin settings";

        // 4. all back on: every remaining slot is asked for once and the mode is over
        asked = 0;
        askedFor.clear();
        engine.switchPluginOn ({});
        if (engine.pluginsOff() || pluginsHeld())
            return "the mode is still on after all plugins were switched on";
        if (! askedFor.contains ("Test Verb") || ! askedFor.contains ("Test Comp") || ! askedFor.contains ("Test Delay"))
            return "switching all on did not ask for every plugin: " + askedFor.joinIntoString (", ");
        engine.capturePluginStates();
        if (! ProjectFile::save (engine.getSong(), third, error))
            return "could not save after switching all on: " + error;
        auto back = ProjectFile::load (third, error);
        if (! back || pluginhold::pluginStates (*back) != original)
            return "the settings of plugins that are not installed were lost after switching all on";

        // 5. the mode again, then another project the plain way: it ends
        engine.setSongWithPluginsOff (*loaded);
        if (! engine.pluginsOff())
            return "the mode did not start a second time";
        engine.setSong (Song::makeDefault());
        if (engine.pluginsOff() || pluginsHeld())
            return "opening another project did not end the mode";
    }
    // 6. the engine going away ends it too
    {
        AudioEngine engine (false, 0, false);
        engine.setSongWithPluginsOff (*loaded);
    }
    if (pluginsHeld())
        return "the mode outlived its engine";
    return "OK";
}
} // namespace lynx
