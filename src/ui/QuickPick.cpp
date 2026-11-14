#include "ui/QuickPick.h"

#include "core/Rack.h"

#include <algorithm>

#include "core/FlSynths.h"
#include "core/NativeEngines.h"
#include "core/PluginCatalog.h"
#include "core/PluginDbFolder.h"
#include "core/Preferences.h"
#include "core/ProjectContents.h"
#include "engine/PluginHost.h"
#include "platform/Paths.h"
#include "ui/ChannelRackComponent.h"
#include "ui/I18n.h"
#include "ui/MixerComponent.h"
#include "ui/PluginWindow.h"
#include "ui/Theme.h"

namespace lynx::quickpick
{
std::vector<GeneratorEntry> generatorEntries (AudioEngine& engine, bool withUnavailable)
{
    auto out = generators::entries (engine, withUnavailable);
    for (auto& entry : out)
    {
        if (entry.folder == 1)
        {
            if (const auto* native = natives::instrumentEngine (entry.kind))
                entry.label = flEngineName (*native);
            entry.detail = tr ("built-in FL engine");
        }
        else if (entry.folder != 3)
        {
            entry.label = tr (entry.label);
            entry.detail = tr ("built-in");
        }
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
    // round 183c: Live-style Audio Effect Rack (a Patcher in rack mode, core/Rack.h)
    out.push_back ({ "lynx:fx:Audio Effect Rack", tr ("Audio Effect Rack"), tr ("built-in"), 0,
                     [] { return std::optional<EffectSlot> (rack::makeRackSlot (RackDoc::Kind::audioEffect)); } });

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

// The plugins as the database folder knows them (core/PluginDbFolder.h): the key, the own name (English for the
// built-in ones, so the files do not change with the language), where it comes from and a built-in one's default folder
template <typename Entry>
void addDatabasePlugins (std::vector<plugindb::Plugin>& out, const std::vector<Entry>& entries, bool instruments)
{
    static const char* const formats[] { "LYNX", "FL engines", "VFX" };
    for (const auto& e : entries)
    {
        plugindb::Plugin p;
        p.key = e.key;
        p.instrument = instruments;
        p.builtin = e.folder != 3;
        if (p.builtin)
        {
            const auto b = builtinOf (e);
            p.name = b.name;
            p.format = formats[juce::jlimit (0, 2, e.folder)];
            const auto placed = plugincatalog::withDefaultCategories ({}, instruments, { b });
            if (const auto it = placed.of.find (e.key); it != placed.of.end())
                p.defaultFolder = it->second;
        }
        else
        {
            p.name = e.label.toStdString();
            p.format = e.detail.fromLastOccurrenceOf (juce::String::fromUTF8 (" \xc2\xb7 "), false, false).toStdString();
        }
        out.push_back (std::move (p));
    }
}
std::vector<plugindb::Plugin> databasePlugins (AudioEngine& engine)
{
    std::vector<plugindb::Plugin> out;
    addDatabasePlugins (out, effectEntries (engine, true), false);
    addDatabasePlugins (out, generatorEntries (engine, true), true);
    return out;
}
juce::uint32 lastDatabaseRefresh = 0;
bool databaseBusy = false;

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
    refreshPluginDatabase (engine); // (the folders as they are on disk now: issue #104)
    auto& host = engine.getPluginHost();
    const auto folders = plugincatalog::withDefaultCategories (host.databaseFolders(), instruments, builtins);
    const auto tree = plugincatalog::menuTree (folders, instruments, keys);
    // FL's root entries (round 190bf, fl190be Q3): plugins at the top of Effects / Generators, in no folder
    const auto root = plugincatalog::rootItems (folders, instruments, keys);
    if (tree.empty() && root.empty()) return false;
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
    {
        auto all = plugincatalog::allItems (tree); // (one A-Z list, root entries among the rest)
        all.insert (all.end(), root.begin(), root.end());
        addItems (menu, all);
    }
    else
    {
        // Categories: the root entries first, without a heading, then each folder under its own (FL's order)
        if (view != plugincatalog::MenuView::tree)
            addItems (menu, root);
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
        // Tree: the folders as submenus first, then the root entries
        if (view == plugincatalog::MenuView::tree)
            addItems (menu, root);
    }
    menu.addSeparator();
    return true;
}
} // namespace

void addGeneratorMenu (juce::PopupMenu& menu, AudioEngine& engine, std::function<void (ChannelDef)> pick,
                       std::function<void()> chooseSample, const ChannelDef* current, juce::Component* owner)
{
    const auto entries = generatorEntries (engine, true);
    // issue #130: the plugin list first (the user's own instruments are the ones they reach for most), then FL's
    // generator categories below
    juce::PopupMenu plugins;
    addPluginItems (plugins, engine, true, [pick] (PluginRef ref) {
        ChannelDef c;
        c.name = ref.name;
        c.instrument = InstrumentKind::plugin;
        c.plugin = ref;
        pick (std::move (c));
    }, owner, pick);
    menu.addSubMenu (tr ("Plugin (VST/VST3/CLAP)"), plugins);
    menu.addSeparator();
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
}

juce::File pluginDatabaseFolder()
{
    return platform::documentsFolder().getChildFile ("LYNX").getChildFile ("Plugin database");
}

juce::File pluginDatabaseFolder (bool instruments, const std::string& userFolder)
{
    return juce::File (juce::String::fromUTF8 (plugindb::folderOnDisk (pluginDatabaseFolder().getFullPathName().toStdString(), instruments, userFolder).c_str()));
}

juce::File pluginDatabaseEntry (const std::string& key, bool instruments)
{
    const auto path = plugindb::entryFile (pluginDatabaseFolder().getFullPathName().toStdString(), key, instruments);
    return path.empty() ? juce::File() : juce::File (juce::String::fromUTF8 (path.c_str()));
}

void markPluginDatabaseChanged()
{
    lastDatabaseRefresh = 0; // (the next menu, picker or Browser reads the folder again, without the two-second wait)
}

void refreshPluginDatabase (AudioEngine& engine, bool force)
{
    auto& host = engine.getPluginHost();
    if (! host.isPersistent() || databaseBusy)
        return;
    const auto now = juce::Time::getMillisecondCounter();
    if (! force && lastDatabaseRefresh != 0 && now - lastDatabaseRefresh < 2000)
        return;
    const juce::ScopedValueSetter<bool> busy (databaseBusy, true);
    plugindb::State state { host.databaseFolders(), host.pluginNames() };
    if (plugindb::refresh (pluginDatabaseFolder().getFullPathName().toStdString(), databasePlugins (engine), state).ok)
        host.adoptDatabase (state.folders, state.names);
    lastDatabaseRefresh = juce::Time::getMillisecondCounter();
}

void installPluginDatabase (AudioEngine& engine)
{
    auto& host = engine.getPluginHost();
    if (! host.isPersistent())
        return;
    refreshPluginDatabase (engine, true);
    host.onDatabaseEdited = [&engine] {
        auto& h = engine.getPluginHost();
        if (databaseBusy)
            return;
        {
            const juce::ScopedValueSetter<bool> busy (databaseBusy, true);
            plugindb::apply (pluginDatabaseFolder().getFullPathName().toStdString(), databasePlugins (engine), { h.databaseFolders(), h.pluginNames() });
        }
        refreshPluginDatabase (engine, true); // (the folder has the last word, e.g. on a folder name it cannot hold)
    };
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
    if (slot.kind == EffectKind::patcher && slot.patcher.isRack()) // (round 183c: a rack: its name, else its kind's)
        return slot.patcher.rack.front().name.empty() ? tr ("Audio Effect Rack") : juce::String::fromUTF8 (slot.patcher.rack.front().name.c_str());
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
struct MenuKeys final : public juce::KeyListener, private juce::Timer
{
    std::vector<Pick> picks;
    std::function<bool (const juce::KeyPress&)> onKey;

    ~MenuKeys() override { detachAll(); }

    // Every window of the menu (the root, then each submenu it opens) is the modal component while it has the keys
    void start()
    {
        attachToModal();
        startTimer (40);
    }
    void detachAll()
    {
        stopTimer();
        for (auto& w : windows)
            if (auto* c = w.getComponent())
                c->removeKeyListener (this);
        windows.clear();
    }

    bool keyPressed (const juce::KeyPress& key, juce::Component* window) override
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
        if (const auto index = prefs::uniqueTypeAhead (labels, (char) typed); index >= 0)
        {
            auto action = picks[(std::size_t) index].action;
            juce::PopupMenu::dismissAllActiveMenus();
            juce::MessageManager::callAsync ([action] {
                if (action)
                    action();
            });
            return true;
        }
        return highlightNext (window, (char) typed);
    }

private:
    std::vector<juce::Component::SafePointer<juce::Component>> windows;

    void timerCallback() override { attachToModal(); }

    void attachToModal()
    {
        auto* modal = juce::Component::getCurrentlyModalComponent();
        if (modal == nullptr)
            return;
        for (auto& w : windows)
            if (w.getComponent() == modal)
                return;
        modal->addKeyListener (this);
        windows.emplace_back (modal);
    }

    // FL's letter keys (issue #64): the next item of this menu window starting with the letter gets the highlight
    // (and scrolls into view); Enter picks it, the arrows / wheel / Esc stay JUCE's. The items are the window's
    // children in menu order; JUCE's own accessibility actions highlight one ("focus") and tell which one is.
    static bool highlightNext (juce::Component* window, char typed)
    {
        if (window == nullptr)
            return false;
        std::vector<std::string> labels;
        std::vector<bool> selectable;
        std::vector<juce::AccessibilityHandler*> handlers;
        int current = -1;
        for (auto* child : window->getChildren())
        {
            auto* handler = child->getAccessibilityHandler();
            const auto isItem = handler != nullptr && handler->getRole() == juce::AccessibilityRole::menuItem;
            const auto& actions = isItem ? handler->getActions() : juce::AccessibilityActions();
            labels.push_back (isItem ? handler->getTitle().toStdString() : std::string());
            selectable.push_back (isItem && actions.contains (juce::AccessibilityActionType::focus)
                                  && (actions.contains (juce::AccessibilityActionType::press) || actions.contains (juce::AccessibilityActionType::showMenu)));
            if (isItem && handler->hasFocus (false))
                current = (int) handlers.size();
            handlers.push_back (handler);
        }
        const auto next = prefs::nextTypeAhead (labels, selectable, current, typed);
        if (next < 0)
            return false;
        return handlers[(std::size_t) next]->getActions().invoke (juce::AccessibilityActionType::focus);
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
    // (one setting for every list: the Add menu, the Channel Rack's "+" menu and the mixer slots' menus)
    static const char* const names[] { TR_NOOP ("Categories"), TR_NOOP ("Simple"), TR_NOOP ("Tree") };
    for (int v = 0; v < 3; ++v)
        addRadioItem (menu, tr (names[v]), folders.view == v, [&engine, v] {
            auto& h = engine.getPluginHost();
            auto f = h.databaseFolders();
            f.view = v;
            h.setDatabaseFolders (f);
        });
}

void addRadioItem (juce::PopupMenu& menu, const juce::String& text, bool selected, std::function<void()> action)
{
    // FL's radio items (a dot, not a tick): a small circle drawn as the item's icon, filled when it is the one chosen
    // (a picture at twice the size, so it stays sharp at the menu's 14 px)
    constexpr int px = 28;
    juce::Image picture (juce::Image::ARGB, px, px, true);
    {
        juce::Graphics g (picture);
        g.setColour (selected ? theme::colour::accent : theme::colour::textSecondary);
        g.drawEllipse (juce::Rectangle<float> (4.0f, 4.0f, px - 8.0f, px - 8.0f), 2.4f);
        if (selected)
            g.fillEllipse (juce::Rectangle<float> (px * 0.5f - 5.5f, px * 0.5f - 5.5f, 11.0f, 11.0f));
    }
    auto icon = std::make_unique<juce::DrawableImage> (picture);
    juce::PopupMenu::Item item (text);
    item.setImage (std::move (icon));
    item.setAction (std::move (action));
    menu.addItem (std::move (item));
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
    menu.showMenuAsync (options, [keys] (int) { keys->detachAll(); });
    // the menu's window is the modal component now (JUCE enters its modal state while showing it); its submenus are
    // when they open (the timer finds them)
    keys->start();
}
} // namespace lynx::quickpick
