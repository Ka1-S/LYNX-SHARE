#include "ui/QuickAddPalette.h"
#include "core/ControlSurface.h"
#include "core/VideoPlayer.h"
#include "core/VoltageController.h"

#include "core/Effects.h"
#include "core/PluginSubstitutes.h"
#include "core/PluginPicks.h"
#include "core/SearchQuery.h"
#include "engine/PluginHost.h"
#include "ui/I18n.h"
#include "ui/MixerComponent.h"
#include "ui/QuickPick.h"
#include "ui/ChannelRackComponent.h"
#include "ui/Theme.h"
#include "ui/ToolWindow.h"
#include "core/SoundFont.h"
#include "core/SoundFontIndex.h"
#include "ui/SoundFontEditor.h"

namespace lynx
{
namespace
{
struct Entry
{
    juce::String name, detail;
    std::function<void()> add;
    std::string key = {}; // its Plugin database key (core/Preferences.h plugin names): a renamed one shows its new name
    bool recent = false;  // round 190qi: a copy at the top of the list, one of the plugins picked last (only while nothing is typed)
};

class EntryList final : public juce::ListBox,
                        private juce::ListBoxModel
{
public:
    EntryList()
    {
        setModel (this);
        setRowHeight (24);
        setColour (juce::ListBox::backgroundColourId, theme::colour::bg1);
        setOutlineThickness (0);
        setWantsKeyboardFocus (false);
    }

    std::vector<Entry> all, shown;
    std::function<void (const Entry&)> onChosen;

    void filter (const juce::String& query)
    {
        const auto q = search::parse (query.toStdString());
        shown.clear();
        const auto typed = query.trim().isNotEmpty();
        for (const auto& e : all)
            if (e.recent ? ! typed : search::matches (q, (e.name + " " + e.detail).toStdString()))
                shown.push_back (e);
        updateContent();
        selectRow (shown.empty() ? -1 : 0);
        repaint();
    }

    void chooseSelected()
    {
        const auto row = getSelectedRow();
        if (row >= 0 && row < (int) shown.size() && onChosen)
            onChosen (shown[(std::size_t) row]);
    }

private:
    int getNumRows() override { return (int) shown.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (row < 0 || row >= (int) shown.size())
            return;

        if (selected)
        {
            g.setColour (theme::colour::accentMuted);
            g.fillRect (0, 0, width, height);
        }

        const auto& e = shown[(std::size_t) row];
        auto area = juce::Rectangle<int> (8, 0, width - 16, height);

        // round 190qi: the recent picks first, marked by the accent's edge and a "Recent" caption; a line under the last
        if (e.recent)
        {
            g.setColour (theme::colour::accent);
            g.fillRect (0, 0, 2, height);
            const auto last = row + 1 >= (int) shown.size() || ! shown[(std::size_t) row + 1].recent;
            if (last)
            {
                g.setColour (theme::colour::border);
                g.fillRect (0, height - 1, width, 1);
            }
        }
        // the source on the right first, then the name in what is left (cut with "..."): a long name no longer runs
        // under the source (round 165q)
        const auto detail = e.recent ? tr ("Recent") : e.detail;
        if (detail.isNotEmpty())
        {
            const auto caption = theme::font (theme::type::caption);
            const auto detailWidth = juce::jmin (juce::GlyphArrangement::getStringWidthInt (juce::Font (caption), detail) + 2, area.getWidth() * 2 / 5);
            g.setFont (caption);
            g.setColour (e.recent ? theme::colour::accent : theme::colour::textSecondary);
            g.drawText (detail, area.removeFromRight (detailWidth), juce::Justification::centredRight, true);
            area.removeFromRight (10);
        }

        g.setFont (theme::font (theme::type::body));
        g.setColour (theme::colour::textPrimary);
        g.drawText (e.name, area, juce::Justification::centredLeft, true);
    }

    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
    {
        if (row >= 0 && row < (int) shown.size() && onChosen)
            onChosen (shown[(std::size_t) row]);
    }
};

class QuickAddPalette final : public juce::Component,
                              private juce::TextEditor::Listener,
                              private juce::KeyListener
{
public:
    QuickAddPalette (AudioEngine& engine, QuickAddTargets t) : targets (std::move (t)), pluginHost (engine.getPluginHost())
    {
        search.setTextToShowWhenEmpty (tr ("Type to filter (e.g. reverb, synth OR bass, -delay)"), theme::colour::textDisabled);
        search.addListener (this);
        search.addKeyListener (this);
        focusOnOpen (search); // (issue #244: "Find an effect" and the Quick-add palette open with the caret in the search)
        // "Esc closes" (the hint below): the search box used to swallow Esc, so the window stayed open (round 165q)
        search.onEscapeKey = [this] { closeToolWindowOf (*this); };
        addAndMakeVisible (search);

        for (auto* l : { &instrumentsTitle, &effectsTitle })
        {
            l->setFont (theme::font (theme::type::small, true));
            l->setColour (juce::Label::textColourId, theme::colour::textSecondary);
            addAndMakeVisible (*l);
        }
        instrumentsTitle.setText (tr ("Instruments: new channel"), juce::dontSendNotification);
        effectsTitle.setText (targets.effectsTitle.isNotEmpty() ? targets.effectsTitle : tr ("Effects: into the selected insert"), juce::dontSendNotification);
        hint.setText (targets.effectsOnly ? tr ("Enter adds the highlighted effect, Up/Down move, Esc closes")
                                          : tr ("Enter adds the highlighted item, Up/Down move, Tab switches lists, Esc closes"),
                      juce::dontSendNotification);
        instrumentsTitle.setVisible (! targets.effectsOnly);
        hint.setFont (theme::font (theme::type::caption));
        hint.setColour (juce::Label::textColourId, theme::colour::textDisabled);
        addAndMakeVisible (hint);

        // built-in instruments
        const std::pair<InstrumentKind, const char*> builtIn[] = { { InstrumentKind::synth, TR_NOOP ("Synth") }, { InstrumentKind::kick, TR_NOOP ("Kick") },
                                                                   { InstrumentKind::snare, TR_NOOP ("Snare") }, { InstrumentKind::hat, TR_NOOP ("Hat") },
                                                                   { InstrumentKind::clap, TR_NOOP ("Clap") } };
        for (const auto& [kind, name] : builtIn)
            instruments.all.push_back ({ tr (name), tr ("built-in"),
                                         [this, kind = kind, english = juce::String (name)] {
                                             ChannelDef c;
                                             c.name = english.toStdString();
                                             c.instrument = kind;
                                             targets.addInstrument (c);
                                         },
                                         std::string ("lynx:gen:") + name });

        // FL's Layer and Dashboard (round 164): no sound of their own
        instruments.all.push_back ({ tr ("Layer"), tr ("built-in"), [this] {
                                        ChannelDef c;
                                        c.name = "Layer";
                                        c.instrument = InstrumentKind::layer;
                                        targets.addInstrument (c);
                                    },
                                     "lynx:gen:Layer" });
        instruments.all.push_back ({ "Dashboard", tr ("built-in"), [this] {
                                        ChannelDef c;
                                        c.name = "Dashboard";
                                        c.instrument = InstrumentKind::dashboard;
                                        c.patcher.surface = surface::starterControls();
                                        targets.addInstrument (c);
                                    },
                                     "lynx:gen:Dashboard" });
        instruments.all.push_back ({ "Voltage Controller", tr ("built-in"), [this] {
                                        ChannelDef c;
                                        c.name = "Voltage Controller";
                                        c.instrument = InstrumentKind::voltage;
                                        c.pluginState = voltage::write (voltage::Settings {});
                                        targets.addInstrument (c);
                                    },
                                     "lynx:gen:Voltage Controller" });
        instruments.all.push_back ({ "Fruity Video Player", tr ("built-in"), [this] {
                                        ChannelDef c;
                                        c.name = "Fruity Video Player";
                                        c.instrument = InstrumentKind::videoplayer;
                                        c.pluginState = video::write (video::Settings {});
                                        targets.addInstrument (c);
                                    },
                                     "lynx:gen:Fruity Video Player" });

        // FL's own instrument engines (round 142): those this PC can make (the user's FL installation)
        for (const auto& e : natives::instrumentEngines())
            if (natives::available (e, flStudioAccess()))
                instruments.all.push_back ({ flEngineName (e), tr ("built-in FL engine"), [this, kind = e.kind] {
                                                if (auto c = natives::makeChannel (kind, flStudioAccess()))
                                                    targets.addInstrument (*c);
                                            },
                                             std::string ("lynx:gen:") + e.name });

        // LYNX Soundfont (round 158): the empty player, and every indexed SoundFont preset ("piano", "808"...)
        instruments.all.push_back ({ "LYNX Soundfont", tr ("built-in"), [this] { targets.addInstrument (sf::makeChannel()); } });
        ensureSoundFontIndex();
        for (const auto& hit : sf::PresetIndex::shared().search ({}, 5000))
            instruments.all.push_back ({ juce::String::fromUTF8 (hit.presetName.c_str()),
                                         tr ("SoundFont preset: {0}", { juce::String::fromUTF8 (hit.fontName.c_str()) }) + " " + juce::String::fromUTF8 (hit.category.c_str()),
                                         [this, hit] {
                                             auto c = sf::makeChannel (hit.path, hit.bank, hit.program);
                                             c.name = hit.presetName;
                                             targets.addInstrument (c);
                                         } });

        for (auto kind : addableEffects())
            if (addableEffectAvailable (kind)) // (FL engines without the user's FL installation are left out)
                effects.all.push_back ({ addableEffectName (kind), natives::effectEngine (kind) != nullptr ? tr ("built-in FL engine") : tr ("built-in"),
                                         [this, kind, bpm = engine.getSong().bpm] {
                                             if (const auto fx = newAddableEffect (kind, bpm))
                                                 targets.addEffect (*fx);
                                         },
                                         std::string ("lynx:fx:") + effectInfo (kind).name });
        // LYNX equivalents of well-known third-party effects (the substitutes table): findable by the product's name
        for (const auto& entry : pluginSubstitutes())
            if (! entry.instrument)
                effects.all.push_back ({ tr ("{0} (LYNX SHARE equivalent of {1})", { juce::String::fromUTF8 (effectInfo (entry.effect).name), juce::String::fromUTF8 (entry.product.c_str()) }),
                                         tr ("built-in") + " " + juce::String::fromUTF8 (entry.vendor.c_str()),
                                         [this, entry] { targets.addEffect (makeSubstituteSlot (entry)); } });

        // plugins: favourites (Plugin manager) first, each group by name; round 190qi: none hidden in the Plugin manager,
        // and one entry per plugin installed in several formats (core/PluginPicks.h: VST3 › CLAP › VST2), its other
        // formats named in the detail
        auto& host = engine.getPluginHost();
        juce::Array<juce::PluginDescription> types, otherBuilds;
        std::vector<juce::StringArray> otherFormats;
        {
            juce::Array<juce::PluginDescription> builds;
            for (const auto& t : host.getKnownPlugins().getTypes())
                if (! host.isHidden (t.createIdentifierString().toStdString()))
                    builds.add (t);
            std::vector<pluginpicks::Item> items;
            for (const auto& t : builds)
                items.push_back ({ t.name.toStdString(), t.manufacturerName.toStdString(), t.pluginFormatName.toStdString(), t.isInstrument });
            for (const auto& group : pluginpicks::groupByFormat (items))
            {
                types.add (builds.getReference (group.preferred()));
                juce::StringArray others;
                for (std::size_t m = 1; m < group.members.size(); ++m)
                {
                    others.add (builds.getReference (group.members[m]).pluginFormatName);
                    otherBuilds.add (builds.getReference (group.members[m])); // (a recent pick of that format: Recent)
                }
                otherFormats.push_back (others);
            }
        }
        const auto& favourites = host.favourites();
        const auto isFavourite = [&favourites] (const juce::PluginDescription& t) { return favourites.count (t.createIdentifierString().toStdString()) > 0; };
        std::vector<int> order ((std::size_t) types.size());
        for (int i = 0; i < types.size(); ++i)
            order[(std::size_t) i] = i;
        std::sort (order.begin(), order.end(), [&] (int ia, int ib) {
            const auto& a = types.getReference (ia);
            const auto& b = types.getReference (ib);
            if (isFavourite (a) != isFavourite (b))
                return isFavourite (a);
            return a.name.compareIgnoreCase (b.name) < 0;
        });

        // one plugin build as an entry (`detail` after its vendor and format)
        auto pluginEntry = [this, &isFavourite] (const juce::PluginDescription& type, const juce::String& more) {
            PluginRef ref;
            ref.id = type.createIdentifierString().toStdString();
            ref.name = type.name.toStdString();
            auto detail = (isFavourite (type) ? juce::String::fromUTF8 ("\xe2\x98\x85 ") : juce::String()) + type.manufacturerName + " · " + type.pluginFormatName + more;
            if (type.isInstrument)
                return Entry { type.name, detail, [this, ref] {
                                  ChannelDef c;
                                  c.name = ref.name;
                                  c.instrument = InstrumentKind::plugin;
                                  c.plugin = ref;
                                  targets.addInstrument (c);
                              },
                               ref.id };
            return Entry { type.name, detail, [this, ref] {
                              EffectSlot slot;
                              slot.kind = EffectKind::plugin;
                              slot.plugin = ref;
                              targets.addEffect (slot);
                          },
                           ref.id };
        };
        for (const auto index : order)
        {
            const auto& type = types.getReference (index);
            const auto& others = otherFormats[(std::size_t) index];
            (type.isInstrument ? instruments : effects).all.push_back (pluginEntry (type, others.isEmpty() ? juce::String() : " (+" + others.joinIntoString (", ") + ")"));
        }

        // plugins renamed in the Plugin database (FL, issue #11: "1_EQ"): their new name, first in the list; the
        // original name stays in the detail, so typing it still finds them
        const auto& names = engine.getPluginHost().pluginNames();
        for (auto* list : { &instruments, &effects })
        {
            std::vector<Entry> renamed, rest;
            for (auto& e : list->all)
            {
                const auto it = e.key.empty() ? names.end() : names.find (e.key);
                if (it == names.end())
                {
                    rest.push_back (std::move (e));
                    continue;
                }
                e.detail = e.name + juce::String::fromUTF8 (" \xc2\xb7 ") + e.detail;
                e.name = juce::String::fromUTF8 (it->second.c_str());
                renamed.push_back (std::move (e));
            }
            std::stable_sort (renamed.begin(), renamed.end(), [] (const Entry& a, const Entry& b) { return a.name.compareNatural (b.name) < 0; });
            renamed.insert (renamed.end(), std::make_move_iterator (rest.begin()), std::make_move_iterator (rest.end()));
            list->all = std::move (renamed);
        }

        // round 190qi: the plugins picked last first (copies, while nothing is typed), newest first; a recent build of a
        // plugin now listed under another format is found by its other builds
        for (auto* list : { &instruments, &effects })
        {
            std::vector<Entry> recent;
            for (const auto& key : pluginpicks::recentShown() ? host.recentPlugins() : std::vector<std::string>()) // (the extra's switch)
            {
                if (host.isHidden (key))
                    continue;
                auto it = std::find_if (list->all.begin(), list->all.end(), [&key] (const Entry& e) { return e.key == key; });
                if (it != list->all.end())
                {
                    auto copy = *it;
                    copy.recent = true;
                    recent.push_back (std::move (copy));
                    continue;
                }
                for (const auto& build : otherBuilds) // (the format picked last time, not the row's own)
                    if (build.isInstrument == (list == &instruments) && build.createIdentifierString().toStdString() == key)
                    {
                        auto copy = pluginEntry (build, {});
                        copy.name = quickpick::shownName (engine, key, build.name);
                        copy.recent = true;
                        recent.push_back (std::move (copy));
                        break;
                    }
            }
            list->all.insert (list->all.begin(), recent.begin(), recent.end());
        }

        for (auto* list : { &instruments, &effects })
        {
            list->onChosen = [this] (const Entry& e) { choose (e); };
            addAndMakeVisible (*list);
            list->filter ({});
        }

        active = targets.effectsFirst || targets.effectsOnly ? &effects : &instruments;
        instruments.setVisible (! targets.effectsOnly); // (issue #206: the effects alone)
        setSize (targets.effectsOnly ? 420 : 640, 420);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg2);
        g.setColour (theme::colour::accent);
        const auto r = active->getBounds().toFloat().expanded (1.0f);
        g.drawRect (r, 1.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10);
        search.setBounds (area.removeFromTop (28));
        area.removeFromTop (6);
        hint.setBounds (area.removeFromBottom (18));
        auto titles = area.removeFromTop (20);
        if (targets.effectsOnly)
        {
            effectsTitle.setBounds (titles);
            effects.setBounds (area);
            return;
        }
        instrumentsTitle.setBounds (titles.removeFromLeft (area.getWidth() / 2));
        effectsTitle.setBounds (titles.withTrimmedLeft (6));
        instruments.setBounds (area.removeFromLeft (area.getWidth() / 2).withTrimmedRight (3));
        effects.setBounds (area.withTrimmedLeft (3));
    }

    void parentHierarchyChanged() override
    {
        if (isShowing())
            search.grabKeyboardFocus();
    }

    bool keyPressed (const juce::KeyPress& key) override { return handleKey (key); }
    bool keyPressed (const juce::KeyPress& key, juce::Component*) override { return handleKey (key); }

    bool handleKey (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::returnKey)
        {
            active->chooseSelected();
            return true;
        }

        if (key == juce::KeyPress::tabKey)
        {
            if (targets.effectsOnly)
                return true;
            active = active == &instruments ? &effects : &instruments;
            repaint();
            return true;
        }

        if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
        {
            const auto count = (int) active->shown.size();
            if (count > 0)
            {
                const auto row = juce::jlimit (0, count - 1, active->getSelectedRow() + (key == juce::KeyPress::upKey ? -1 : 1));
                active->selectRow (row);
                active->scrollToEnsureRowIsOnscreen (row);
            }
            return true;
        }

        return false;
    }

private:
    void textEditorTextChanged (juce::TextEditor&) override
    {
        instruments.filter (search.getText());
        effects.filter (search.getText());

        // the list that has matches when the preferred one has none
        if (active->shown.empty() && ! targets.effectsOnly)
        {
            auto* other = active == &instruments ? &effects : &instruments;
            if (! other->shown.empty())
                active = other;
        }
        repaint();
    }

    void textEditorReturnKeyPressed (juce::TextEditor&) override { active->chooseSelected(); }

    void choose (const Entry& e)
    {
        if (! e.key.empty())
            pluginHost.notePluginPicked (e.key); // (round 190qi: Recent)
        if (e.add)
            e.add();
        closeToolWindowOf (*this);
    }

    QuickAddTargets targets;
    PluginHost& pluginHost;
    juce::TextEditor search;
    juce::Label instrumentsTitle, effectsTitle, hint;
    EntryList instruments, effects;
    EntryList* active = nullptr;
};
} // namespace

std::unique_ptr<juce::Component> makeQuickAddPalette (AudioEngine& engine, QuickAddTargets targets)
{
    return std::make_unique<QuickAddPalette> (engine, std::move (targets));
}
} // namespace lynx
