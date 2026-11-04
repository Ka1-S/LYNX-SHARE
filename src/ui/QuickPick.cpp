#include "ui/QuickPick.h"

#include <algorithm>

#include "core/ControlSurface.h"
#include "core/FlSynths.h"
#include "core/FruityDance.h"
#include "core/MidiControls.h"
#include "core/SoundFont.h"
#include "core/NativeEngines.h"
#include "core/PluginCatalog.h"
#include "core/Preferences.h"
#include "core/ProjectContents.h"
#include "core/VideoPlayer.h"
#include "core/VoltageController.h"
#include "core/Vfx.h"
#include "engine/PluginHost.h"
#include "ui/ChannelRackComponent.h"
#include "ui/I18n.h"
#include "ui/MixerComponent.h"
#include "ui/PluginWindow.h"

namespace lynx::quickpick
{
std::vector<GeneratorEntry> generatorEntries (AudioEngine& engine, bool withUnavailable)
{
    std::vector<GeneratorEntry> out;
    auto add = [&out] (std::string english, juce::String label, juce::String detail, int folder, int section, InstrumentKind kind,
                       std::function<std::optional<ChannelDef>()> make) {
        out.push_back ({ "lynx:gen:" + english, std::move (label), std::move (detail), folder, std::move (make), section, kind });
    };
    // a built-in whose channel is `kind` with the English name; `setup` fills its state
    auto builtIn = [&add] (const char* english, juce::String label, int section, InstrumentKind kind, std::function<void (ChannelDef&)> setup = nullptr) {
        add (english, std::move (label), tr ("built-in"), 0, section, kind, [kind, name = std::string (english), setup] {
            ChannelDef c;
            c.name = name;
            c.instrument = kind;
            if (setup)
                setup (c);
            return std::optional<ChannelDef> (c);
        });
    };

    // the first block, FL's menu order (Kick … Synth)
    builtIn (TR_NOOP ("Kick"), tr ("Kick"), 0, InstrumentKind::kick);
    builtIn (TR_NOOP ("Snare"), tr ("Snare"), 0, InstrumentKind::snare);
    builtIn (TR_NOOP ("Hat"), tr ("Hat"), 0, InstrumentKind::hat);
    builtIn (TR_NOOP ("Clap"), tr ("Clap"), 0, InstrumentKind::clap);
    builtIn (TR_NOOP ("Synth"), tr ("Synth"), 0, InstrumentKind::synth);

    // built-in controllers and tools (rounds 133–165): no sound of their own
    builtIn (TR_NOOP ("Envelope Controller"), tr ("Envelope Controller"), 1, InstrumentKind::controller,
             [] (ChannelDef& c) { c.controllerState = writeEnvelopeController (defaultEnvelopeController()); });
    builtIn (TR_NOOP ("Keyboard Controller"), tr ("Keyboard Controller"), 1, InstrumentKind::controller,
             [] (ChannelDef& c) { c.controllerState = defaultKeyboardController(); });
    add ("MIDI Out", tr ("MIDI Out"), tr ("built-in"), 0, 1, InstrumentKind::midiOut, [&engine] {
        ChannelDef c;
        c.name = "MIDI Out";
        c.instrument = InstrumentKind::midiOut;
        // FL gives each new MIDI Out the next MIDI channel, skipping the drum channel 10 (REA_rodada16 §1.1)
        int count = 0;
        for (const auto& ch : engine.getSong().channels)
            count += ch.instrument == InstrumentKind::midiOut;
        midictl::MidiOutSettings settings;
        settings.channel = count % 15 >= 9 ? count % 15 + 1 : count % 15;
        midictl::writeMidiOut (c.pluginState, settings);
        return std::optional<ChannelDef> (c);
    });
    builtIn (TR_NOOP ("Layer"), tr ("Layer"), 1, InstrumentKind::layer);
    builtIn ("Dashboard", "Dashboard", 1, InstrumentKind::dashboard, [] (ChannelDef& c) { c.patcher.surface = surface::starterControls(); });
    builtIn ("Voltage Controller", "Voltage Controller", 1, InstrumentKind::voltage,
             [] (ChannelDef& c) { c.pluginState = voltage::write (voltage::Settings {}); });
    builtIn ("Fruity Dance", "Fruity Dance", 1, InstrumentKind::fruitydance, [] (ChannelDef& c) { c.pluginState = dance::toState (dance::Settings {}); });
    builtIn ("Fruity Video Player", "Fruity Video Player", 1, InstrumentKind::videoplayer,
             [] (ChannelDef& c) { c.pluginState = video::write (video::Settings {}); });
    builtIn ("Patcher", tr ("Patcher"), 2, InstrumentKind::patcher);
    add ("LYNX Soundfont", "LYNX Soundfont", tr ("built-in"), 0, 2, InstrumentKind::soundfont, [] { return std::optional<ChannelDef> (sf::makeChannel()); });

    // FL's own instrument engines (round 142)
    const auto& fl = flStudioAccess();
    for (const auto& e : natives::instrumentEngines())
    {
        const auto available = natives::available (e, fl);
        if (available || withUnavailable)
        {
            add (e.name, flEngineName (e), tr ("built-in FL engine"), 1, 1, e.kind, [kind = e.kind] { return natives::makeChannel (kind, flStudioAccess()); });
            out.back().available = available;
        }
    }

    // FL's VFX note processors (round 164b): channels with no sound, they play other channels / Patcher nodes
    for (const auto kind : vfx::allKinds)
        add (vfx::flName (kind), vfx::flName (kind), tr ("built-in"), 2, 1, InstrumentKind::vfx, [kind] {
            ChannelDef c;
            vfx::makeChannel (c, kind);
            return std::optional<ChannelDef> (c);
        });

    auto types = engine.getPluginHost().getKnownPlugins().getTypes();
    std::sort (types.begin(), types.end(), [] (const auto& a, const auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    for (const auto& t : types)
        if (t.isInstrument)
        {
            const auto id = t.createIdentifierString().toStdString();
            out.push_back ({ id, t.name, t.manufacturerName + " · " + t.pluginFormatName, 3, [id, name = t.name.toStdString()] {
                                ChannelDef c;
                                c.name = name;
                                c.instrument = InstrumentKind::plugin;
                                c.plugin.id = id;
                                c.plugin.name = name;
                                return std::optional<ChannelDef> (c);
                            }, 1, InstrumentKind::plugin });
        }
    return out;
}

std::vector<EffectEntry> effectEntries (AudioEngine& engine, bool withUnavailable)
{
    std::vector<EffectEntry> out;
    const auto bpm = engine.getSong().bpm;
    for (const auto kind : addableEffects())
        if (const auto available = addableEffectAvailable (kind); available || withUnavailable)
        {
            const auto flEngine = natives::effectEngine (kind) != nullptr;
            out.push_back ({ std::string ("lynx:fx:") + effectInfo (kind).name, addableEffectName (kind),
                             flEngine ? tr ("built-in FL engine") : tr ("built-in"), flEngine ? 1 : 0,
                             [kind, bpm] { return std::optional<EffectSlot> (newAddableEffect (kind, bpm)); } });
            out.back().available = available;
            out.back().flPlugin = isFlEffect (kind);
        }

    auto types = engine.getPluginHost().getKnownPlugins().getTypes();
    std::sort (types.begin(), types.end(), [] (const auto& a, const auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    for (const auto& t : types)
        if (! t.isInstrument)
        {
            const auto id = t.createIdentifierString().toStdString();
            out.push_back ({ id, t.name, t.manufacturerName + " · " + t.pluginFormatName, 3, [id, name = t.name.toStdString()] {
                                EffectSlot fx;
                                fx.kind = EffectKind::plugin;
                                fx.plugin.id = id;
                                fx.plugin.name = name;
                                return std::optional<EffectSlot> (fx);
                            } });
        }
    return out;
}

namespace
{
// The built-in plugins of `entries` with what their default category needs (round 170b)
plugincatalog::BuiltinPlugin builtinOf (const GeneratorEntry& e)
{
    const auto name = e.key.rfind ("lynx:gen:", 0) == 0 ? e.key.substr (9) : e.key;
    return { e.key, name, true, e.folder == 1 };
}
plugincatalog::BuiltinPlugin builtinOf (const EffectEntry& e)
{
    const auto name = e.key.rfind ("lynx:fx:", 0) == 0 ? e.key.substr (8) : e.key;
    return { e.key, name, e.flPlugin, e.flPlugin };
}

// FL's plugin menu (REA_rodada166f §1, round 170b): a heading per folder with its subfolders as submenus and its plugins
// A-Z (Categories), every folder a submenu (Tree) or one A-Z list (Simple); `item` adds entry i to a menu.
template <typename Entry>
bool categorizedItems (juce::PopupMenu& menu, AudioEngine& engine, const std::vector<Entry>& entries, bool instruments,
                       const std::function<void (juce::PopupMenu&, int)>& item)
{
    std::vector<std::string> keys;
    std::vector<plugincatalog::BuiltinPlugin> builtins;
    for (const auto& e : entries)
    {
        keys.push_back (e.key);
        if (e.folder != 3) // (installed plugins only under a folder the user made)
            builtins.push_back (builtinOf (e));
    }
    auto& host = engine.getPluginHost();
    const auto folders = plugincatalog::withDefaultCategories (host.databaseFolders(), instruments, builtins);
    const auto tree = plugincatalog::menuTree (folders, instruments, keys);
    if (tree.empty()) return false;
    const auto view = (plugincatalog::MenuView) host.databaseFolders().view;
    auto label = [&] (int i) { return shownName (engine, entries[(std::size_t) i].key, entries[(std::size_t) i].label); };
    auto addItems = [&] (juce::PopupMenu& m, std::vector<int> items) {
        std::stable_sort (items.begin(), items.end(), [&] (int a, int b) { return label (a).compareNatural (label (b)) < 0; });
        for (const auto i : items)
            item (m, i);
    };
    std::function<juce::PopupMenu (const plugincatalog::MenuFolder&)> submenu = [&] (const plugincatalog::MenuFolder& f) {
        juce::PopupMenu m;
        for (const auto& sub : f.sub) m.addSubMenu (juce::String::fromUTF8 (sub.name.c_str()), submenu (sub));
        addItems (m, f.items);
        return m;
    };
    if (view == plugincatalog::MenuView::simple)
        addItems (menu, plugincatalog::allItems (tree));
    else
        for (const auto& f : tree)
        {
            if (view == plugincatalog::MenuView::tree)
            {
                menu.addSubMenu (juce::String::fromUTF8 (f.name.c_str()), submenu (f));
                continue;
            }
            menu.addSectionHeader (juce::String::fromUTF8 (f.name.c_str()));
            for (const auto& sub : f.sub) menu.addSubMenu (juce::String::fromUTF8 (sub.name.c_str()), submenu (sub));
            addItems (menu, f.items);
        }
    menu.addSeparator();
    return true;
}
} // namespace

void addGeneratorMenu (juce::PopupMenu& menu, AudioEngine& engine, std::function<void (ChannelDef)> pick,
                       std::function<void()> chooseSample, const ChannelDef* current, juce::Component* owner)
{
    const auto entries = generatorEntries (engine, true);
    // FL's Add menu (round 170b): the generators under their categories, A-Z (FL's Plugin database folders, LYNX's own
    // first); the channel's own generator is ticked (only kinds with one entry: the VFX and FL engines share kinds / names)
    categorizedItems<GeneratorEntry> (menu, engine, entries, true, [&] (juce::PopupMenu& m, int i) {
        const auto& e = entries[(std::size_t) i];
        const auto own = current != nullptr && current->instrument == e.kind && e.folder == 0 && e.kind != InstrumentKind::controller;
        m.addItem (shownName (engine, e.key, e.label), e.available, own, [make = e.make, pick] {
            if (auto c = make())
                pick (std::move (*c));
            else
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("FL engines"),
                                                        tr ("This engine needs the data files of an FL Studio installation, which were not found or could not be read."));
        });
    });
    if (chooseSample)
        menu.addItem (tr ("Sampler (choose file)..."), chooseSample);
    juce::PopupMenu plugins;
    addPluginItems (plugins, engine, true, [pick] (PluginRef ref) {
        ChannelDef c;
        c.name = ref.name;
        c.instrument = InstrumentKind::plugin;
        c.plugin = ref;
        pick (std::move (c));
    }, owner, pick);
    menu.addSubMenu (tr ("Plugin (VST/VST3/CLAP)"), plugins);
}

juce::String shownName (AudioEngine& engine, const std::string& key, const juce::String& original)
{
    const auto& names = engine.getPluginHost().pluginNames();
    if (const auto it = names.find (key); it != names.end())
        return juce::String::fromUTF8 (it->second.c_str());
    return original;
}

juce::String slotName (AudioEngine& engine, const EffectSlot& slot)
{
    const auto original = juce::String::fromUTF8 (projectcontents::effectSlotName (slot).c_str());
    if (slot.kind == EffectKind::plugin)
        return slot.plugin.id.empty() ? original : shownName (engine, slot.plugin.id, original);
    if (slot.isSubstitute() || original != juce::String (effectInfo (slot.kind).name)) // (a stand-in shows what it stands for)
        return original;
    return shownName (engine, std::string ("lynx:fx:") + effectInfo (slot.kind).name, original);
}

void rename (AudioEngine& engine, const std::string& key, const juce::String& original, std::function<void()> done)
{
    auto* window = new juce::AlertWindow (tr ("Rename plugin"),
                                          tr ("A name for \"{0}\" in the Plugin database and the menus. Start it with a digit or letter of its own "
                                              "(\"1_EQ\", \"2_Limiter\"): on an empty mixer slot (or in the Channel Rack's add menu) typing it adds "
                                              "the plugin. Empty: the original name.",
                                              { original }),
                                          juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", shownName (engine, key, original) == original ? juce::String() : shownName (engine, key, original));
    window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([window, &engine, key, done] (int result) {
                                 if (result == 1)
                                 {
                                     engine.getPluginHost().setPluginName (key, window->getTextEditorContents ("name").toStdString());
                                     if (done)
                                         done();
                                 }
                             }),
                             true);
}

namespace
{
template <typename Entry, typename Thing>
std::vector<Pick> renamed (AudioEngine& engine, const std::vector<Entry>& entries, std::function<void (Thing)> add)
{
    std::vector<Pick> out;
    const auto& names = engine.getPluginHost().pluginNames();
    for (const auto& e : entries)
        if (const auto it = names.find (e.key); it != names.end())
            out.push_back ({ juce::String::fromUTF8 (it->second.c_str()), [make = e.make, add] {
                                if (auto thing = make())
                                    add (std::move (*thing));
                            } });
    std::sort (out.begin(), out.end(), [] (const Pick& a, const Pick& b) { return a.label.compareNatural (b.label) < 0; });
    return out;
}

// Listens to the keys of an open menu (its window is the modal component while it shows).
struct MenuKeys final : public juce::KeyListener
{
    std::vector<Pick> picks;
    std::function<bool (const juce::KeyPress&)> onKey;

    bool keyPressed (const juce::KeyPress& key, juce::Component*) override
    {
        if (onKey && onKey (key))
        {
            juce::PopupMenu::dismissAllActiveMenus();
            return true;
        }
        const auto typed = key.getTextCharacter();
        if (typed <= ' ' || typed > 0x7e || key.getModifiers().isCtrlDown() || key.getModifiers().isAltDown() || key.getModifiers().isCommandDown())
            return false;
        std::vector<std::string> labels;
        for (const auto& p : picks)
            labels.push_back (p.label.toStdString());
        const auto index = prefs::uniqueTypeAhead (labels, (char) typed);
        if (index < 0)
            return false;
        auto action = picks[(std::size_t) index].action;
        juce::PopupMenu::dismissAllActiveMenus();
        juce::MessageManager::callAsync ([action] {
            if (action)
                action();
        });
        return true;
    }
};
} // namespace

std::vector<Pick> renamedEffects (AudioEngine& engine, std::function<void (EffectSlot)> add)
{
    return renamed<EffectEntry, EffectSlot> (engine, effectEntries (engine), std::move (add));
}

std::vector<Pick> renamedGenerators (AudioEngine& engine, std::function<void (ChannelDef)> add)
{
    return renamed<GeneratorEntry, ChannelDef> (engine, generatorEntries (engine), std::move (add));
}

void addFolderViewItems (juce::PopupMenu& menu, AudioEngine& engine)
{
    const auto& folders = engine.getPluginHost().databaseFolders();
    // FL's view switch under the list (radio items): Categories / Simple / Tree
    menu.addSeparator();
    static const char* const names[] { TR_NOOP ("Categories"), TR_NOOP ("Simple"), TR_NOOP ("Tree") };
    for (int v = 0; v < 3; ++v)
        menu.addItem (tr (names[v]), true, folders.view == v, [&engine, v] {
            auto& h = engine.getPluginHost();
            auto f = h.databaseFolders();
            f.view = v;
            h.setDatabaseFolders (f);
        });
}

bool inUserFolder (AudioEngine& engine, const std::string& key)
{
    return engine.getPluginHost().databaseFolders().of.count (key) > 0;
}

bool addFolderGroups (juce::PopupMenu& menu, AudioEngine& engine, std::function<void (EffectSlot)> add)
{
    const auto entries = effectEntries (engine, true);
    return categorizedItems<EffectEntry> (menu, engine, entries, false, [&] (juce::PopupMenu& m, int i) {
        const auto& e = entries[(std::size_t) i];
        m.addItem (shownName (engine, e.key, e.label), e.available, false, [make = e.make, add] {
            if (auto thing = make())
                add (std::move (*thing));
        });
    });
}

bool addFolderGroups (juce::PopupMenu& menu, AudioEngine& engine, std::function<void (ChannelDef)> add)
{
    const auto entries = generatorEntries (engine);
    return categorizedItems<GeneratorEntry> (menu, engine, entries, true, [&] (juce::PopupMenu& m, int i) {
        const auto& e = entries[(std::size_t) i];
        m.addItem (shownName (engine, e.key, e.label), e.available, false, [make = e.make, add] {
            if (auto thing = make())
                add (std::move (*thing));
        });
    });
}

void addPicks (juce::PopupMenu& menu, const std::vector<Pick>& picks)
{
    if (picks.empty())
        return;
    menu.addSectionHeader (tr ("Renamed plugins (type the first character)"));
    for (const auto& p : picks)
        menu.addItem (p.label, p.action);
    menu.addSeparator();
}

void showMenu (juce::PopupMenu& menu, const juce::PopupMenu::Options& options, std::vector<Pick> picks,
               std::function<bool (const juce::KeyPress&)> onKey)
{
    auto keys = std::make_shared<MenuKeys>();
    keys->picks = std::move (picks);
    keys->onKey = std::move (onKey);
    auto holder = std::make_shared<juce::Component::SafePointer<juce::Component>>();
    menu.showMenuAsync (options, [keys, holder] (int) {
        if (auto* w = holder->getComponent())
            w->removeKeyListener (keys.get());
    });
    // the menu's window is the modal component now (JUCE enters its modal state while showing it)
    if (auto* modal = juce::Component::getCurrentlyModalComponent())
    {
        *holder = modal;
        modal->addKeyListener (keys.get());
    }
}
} // namespace lynx::quickpick
