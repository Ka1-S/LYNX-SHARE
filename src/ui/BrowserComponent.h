#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/BrowserCollections.h"
#include "core/BrowserPlaces.h"
#include "core/BrowserSend.h"
#include "core/BrowserTags.h"
#include "core/LoopKeywords.h"
#include "core/ProjectKey.h"
#include "core/BrowserView.h"
#include "core/HotSwap.h"
#include "engine/AudioEngine.h"
#include "ui/Icons.h"

namespace lynx::search
{
struct SearchResult;
struct FolderMatches;
}
namespace lynx::similarity
{
struct Result;
}

namespace lynx
{
// File browser (like FL's Browser): drawn by LYNX itself so it can show what the mouse is over and explain it
// in the hint bar.
//  - three tabs, as FL's Browser: STARRED (the files and folders starred from their right-click menu), ALL (the current project, then every sample folder: FL's folders, Documents,
//    Music, Desktop, Downloads and the user's folders, each with its own colour) and PROJECT (only the current
//    project, opened)
//  - "Current project" (REA_rodada115): history, patterns, effects, generators, automation, the project folder
//    and the audio files the project uses, each item in the colour of what it belongs to, with a short detail
//    (the insert, how often it is used, "missing"); click / double-click / drag / right-click act on it
//  - a click on a folder opens / closes it; a click on an audio file previews it (the strip at the bottom
//    shows its waveform and the playback position); double-click: new sampler channel with it
//  - drag a file onto the Channel Rack (new channel, or onto a row: replaces its sample), the Playlist (audio
//    clip where it is dropped) or a mixer insert (effect presets); drag a pattern onto the Playlist (a clip)
//  - double-click a .lynx/.flp/.mid: opens it (onOpenProject)
//  - search box (Ctrl+F): ALL searches the folder of the selection (or every folder) and below
//    (core/SearchQuery syntax) through the search index (core/SearchService: a background worker, round 162);
//    PROJECT filters the project's items; Esc clears and leaves the box, Enter leaves it; a click anywhere else
//    leaves it too, so Space is the transport again (issue #9)
//  - keys while the Browser has focus: Up/Down step through the rows and preview each sample, Shift+Up/Down step
//    and load each file into the selected channel, Enter loads the selected file into the selected channel,
//    Left/Right close / open folders (Left on a file or a closed folder: to its folder)
//  - the Plugin database's plugins can be dragged onto the Channel Rack (generators) and the Mixer (effects)
class BrowserComponent final : public juce::Component,
                               private juce::TextEditor::Listener,
                               private juce::ChangeListener,
                               private juce::Timer
{
public:
    // Live's pad menu › Next Similar (round 190el, ui/RackEditor.cpp): the samples most like `file` in the main
    // Browser's folders, nearest first, from its "Find similar samples" index (the first use starts the analysis, as the
    // Browser's own does). `done` runs on the message thread, with an empty list when there is no Browser or no match yet.
    static void findSimilarSamples (const juce::File& file, std::function<void (juce::Array<juce::File>)> done);
    // The main Browser (a panel) or the separate Sample browser window (round 163): each keeps its own view (which
    // top-level folders it shows, which kinds of files it lists).
    enum class Role
    {
        main,
        samples
    };
    explicit BrowserComponent (AudioEngine& engine, Role role = Role::main);
    ~BrowserComponent() override;
    Role getRole() const noexcept { return role; }

    // What this Browser shows (round 163): the content filter, the hidden top-level entries; saved at once.
    const browserview::View& getView() const noexcept { return view; }
    void setContentFilter (browserview::ContentFilter filter);
    void setRootShown (const juce::String& id, bool shown);
    void showAllRoots();
    // The Browser's options menu (the button beside the search box, the panel menu): filter, folders, search index.
    void addViewItems (juce::PopupMenu& menu);
    void showViewMenu();
    juce::PopupMenu buildViewMenu(); // (what showViewMenu shows)
    // The Sample browser window opens / closes from here (the main window sets it): View › Sample browser.
    std::function<void()> onToggleSampleBrowser;
    // The Sample browser window's content: a drag container holding a Browser of role `samples`; `wire` connects
    // its callbacks like the main Browser's. The window keeps its bounds and open state in the view file.
    static std::unique_ptr<juce::Component> makeSampleBrowserContent (AudioEngine& engine, const std::function<void (BrowserComponent&)>& wire);
    // Whether the Sample browser was open when LYNX last closed (its view file).
    static bool sampleBrowserWasOpen();
    // Round 190hd: the Browser's search index kept up to date by a low-priority child process (LYNX --browser-indexer
    // <parent pid> <roots file>) woken by file-system changes, as Live's Indexer. Main allows it for the normal window
    // only (command-line modes and tests index in-process, as before); LYNX_INDEXER_PROCESS=0 turns it off.
    static inline bool allowIndexerProcess = false;
    static int runIndexerProcess (const juce::StringArray& args); // the child: never returns while its parent lives

private:
    bool constructing = true; // (round 190hd: the start-up trace's lines of the main Browser's construction)
    void browserStartupMark (const char* step) const;

public:
    static juce::Rectangle<int> sampleBrowserBounds(); // its last window bounds (empty: default)

    std::function<void (const juce::File&)> onOpenProject;
    // Enter / Shift+Up/Down: put this file into the selected channel; returns false if it can't take it.
    std::function<bool (const juce::File&)> onLoadIntoChannel;
    // Double-click an FL mixer preset / LYNX mixer track state: onto the Mixer's selected tracks (false = not one)
    std::function<bool (const juce::File&)> onOpenMixerPreset;
    // Current project items (set by the main window)
    std::function<void (int)> onOpenPattern;     // double-click a pattern: it opens in the Piano Roll
    std::function<void (int)> onPatternMenu;     // right-click a pattern (already selected): the pattern menu
    std::function<void (int)> onOpenChannel;     // double-click a generator: its settings / plugin window
    std::function<void (int, int)> onOpenEffect; // double-click an effect (insert, slot): its editor
    std::function<void (int)> onShowInsert;      // an effect's / generator's insert in the Mixer
    std::function<void (int)> onOpenAutomation;  // double-click an automation clip: its editor
    // The Plugin database (round 142): double-click a generator = a new channel, an effect = into the selected insert
    std::function<void (ChannelDef)> onAddInstrument;
    std::function<void (EffectSlot)> onAddEffect;
    std::function<void (const juce::File&)> onSendToPlaylist; // a sample's "Send to the Playlist as an audio clip"
    std::function<void()> onPreviewVolumeChanged;             // the preview volume knob moved (the main window saves it)

    // The sample preview volume slider on the preview strip (0..125%, engine.setPreviewVolume): follows the engine.
    void refreshPreviewVolume();
    void refreshRoots() { rebuildRoots(); } // (after Tools › Clipboard saved a file; round 181b)
    void pluginDatabaseChanged() { rebuildTree(); } // (the Plugin database folder was changed in the file manager; round 190ba)
    void stopPreview(); // the strip's Play / Stop toggle beside it
    // F2 on a Plugin database entry: rename it (FL's renamed plugins, issue #11); false when no plugin is selected
    bool renameSelectedPlugin();

    // The search index (round 162): a background pass over every Browser folder (rebuild: from scratch), and its
    // state in words ("12,345 files in 678 folders, checked 14:03").
    void refreshSearchIndex (bool rebuild);
    juce::String searchIndexStatus() const;
    // "Find similar samples" (round 190as, core/SampleSimilarity.h): the samples of this Browser's folders that sound
    // most like `file`, most similar first, in the results list (the search box says "Similar to <name>"; Esc or
    // another search leaves it). The first use starts the background analysis of the folders.
    void findSimilar (const juce::File& file);
    void findSimilarForSnapshot (const juce::File& file, const juce::File& folder); // --snapshot: waits for the answer
    // The search box has the keyboard: it gives it back to the main window (a click elsewhere, Esc, Enter).
    void leaveSearchBox();
    bool isTypingInSearch() const;

    // The project's file (.lynx, or the imported .flp): its folder shows as "Project folder".
    void setProjectFile (const juce::File& file);

    // FL 2026's six tabs (round 190iz, core/BrowserPlaces.h): the numbers 0-2 are what they were; FL's left-to-right
    // order is ALL, PROJECT, PLUGINS, PRESETS, SOUNDS, STARRED (browserplaces::tabOrder)
    enum Tab
    {
        allTab,
        projectTab,
        starredTab, // the files and folders the user starred (FL's STARRED)
        pluginsTab, // the Plugin database alone, opened (FL's PLUGINS)
        presetsTab, // the preset places (FL's PRESETS; FL Cloud's presets are not part of LYNX)
        soundsTab,  // the sample places, audio only (FL's SOUNDS; FL Cloud's sounds are not part of LYNX)
        tabCount
    };
    void setTab (int tab);
    int getTab() const noexcept { return tab; }
    // round 190iz: the tab the content acts as — in the Clip-launch layout (no tabs) PROJECT for Live's Current Project,
    // else ALL
    int shownTab() const noexcept
    {
        return ! liveLayout ? tab : liveSelection.category == browserplaces::Category::currentProject ? (int) projectTab : (int) allTab;
    }

    void focusSearch();
    void addUserFolder(); // asks for a folder and adds it to the folder list
    void reloadUserFolders() { rebuildRoots(); } // after the list changed elsewhere (Settings > Files)
    void setSearchForSnapshot (const juce::String& text);
    // For snapshots: selects (and "previews" without sound) the first audio file under the first folder,
    // and puts the mouse over the row after it.
    void selectFirstSampleForSnapshot();
    // --snapshot: the PROJECT tab with the patterns, generators and samples open, a sample selected
    void showProjectForSnapshot();
    // round 190ke (--simplegb190keshot): "LYNX content" with these folders open ("lynx-content", "lynx-content/Beats"...),
    // and the rows listed now (tree / tag-filter results)
    void showFactoryContentForSnapshot (const juce::StringArray& openFolders);
    juce::StringArray rowLabelsForCheck() const;
    juce::StringArray resultLabelsForCheck() const;
    void showPluginDatabaseForSnapshot();
    std::vector<juce::PopupMenu> pluginDatabaseMenusForSnapshot(); // round 190bf: a folder's and an entry's right-click menus
    // FL's "Locate sample in browser" (the sampler's File row, round 190df): the ALL tab with the folders opened down to
    // the file, its row selected; false (and a note) when no folder the Browser shows holds it
    bool locateFile (const juce::File& file);
    // --plugwintest's drop check (issue #174): the selected row of the tree on screen (empty: none shown)
    juce::Rectangle<int> selectedRowScreenBounds() const;
    void showScores (const juce::String& subfolder = {});         // the Piano roll's File › Browse scores: FL's "Scores" (the user's Presets/Scores and Data/Patches/Scores)
    // FL's Shift+F8: the All tab with the Plugin database opened and scrolled into view; with a folder (the Plugin
    // picker's tab, round 171c; `generators` 1 / 0, -1 = none) that folder of the Generators / Effects opened
    void showPluginDatabase (const juce::String& folder = {}, int generators = -1);
    // --snapshot (round 164): only `folder`, opened, listed under `filter` (folders with nothing to list left out)
    void showFilterForSnapshot (const juce::File& folder, browserview::ContentFilter filter);
    void resetViewForSnapshot(); // its Generators / Effects FL engines open (round 142)
    // --snapshot: shows this audio file in the preview strip (without playing it)
    void showPreviewForSnapshot (const juce::File& file);
    // Up/Down (load = false) or Shift+Up/Down (load = true); Enter = loadSelected()
    void step (int delta, bool load);
    // Issue #182: a typed character selects the next row whose name starts with it (more letters typed quickly: a
    // longer prefix; ui/TypeAhead.h). The row is selected and shown, not previewed. False: not a typing key, or no match.
    bool typeToJump (const juce::KeyPress& key);
    void loadSelected();
    juce::File getSelectedFile() const;
    // Left / Right: close / open the selected folder (Left on a file goes to its folder)
    void openFolder (bool open);
    // Live (manual 4.9, round 190ld DL-98): "preview ... by pressing Shift+Enter or the right arrow key when a browser item
    // is selected" while the preview switch is off. False when the selection is not a file
    bool previewSelected();

    // Drags from the Browser (tree or search results): is it one, and which file it carries.
    static bool isBrowserDrag (const juce::DragAndDropTarget::SourceDetails& details);
    static juce::File draggedFile (const juce::DragAndDropTarget::SourceDetails& details);
    // A pattern dragged from "Current project > Patterns": its index, else -1.
    static int draggedPattern (const juce::DragAndDropTarget::SourceDetails& details);
    // A plugin dragged from the Plugin database (round 162): a generator (a new channel) or an effect.
    static bool isGeneratorDrag (const juce::DragAndDropTarget::SourceDetails& details);
    static bool isEffectDrag (const juce::DragAndDropTarget::SourceDetails& details);
    static std::optional<ChannelDef> draggedGenerator (const juce::DragAndDropTarget::SourceDetails& details);
    static std::optional<EffectSlot> draggedEffect (const juce::DragAndDropTarget::SourceDetails& details);
    // The same drag started elsewhere (the Plugin picker's tiles, round 171c): `source` must sit in a drag container
    // (the main window); one of the two makers is set. Returns whether the drag started.
    static bool startPluginDrag (juce::Component& source, std::function<std::optional<ChannelDef>()> generator,
                                 std::function<std::optional<EffectSlot>()> effect, const juce::String& label, const juce::Image& image,
                                 juce::Component* containerOwner = nullptr);
    static bool isAudioFile (const juce::File& file);

    void resized() override;

    // --snapshot: the tree is listed synchronously; kept for the snapshot code
    void waitForListing (int timeoutMs);
    void paint (juce::Graphics& g) override;

    static constexpr const char* dragDescription = "lynx-arquivo";
    static constexpr const char* patternDragDescription = "lynx-pattern";
    static constexpr const char* generatorDragDescription = "lynx-plugin-generator";
    static constexpr const char* effectDragDescription = "lynx-plugin-effect";
    static constexpr int addFolderId = 1000;

    class FileTree;     // the folder tree / search results (implementation detail)
    class PreviewStrip; // waveform of the selected sample
    class TabBar;       // ALL / PROJECT / STARRED
    class DetailsBox;   // date, size / version and folder of the selection (FL's details box)
    class FoldersPanel; // the "Show folders" window: which folders this Browser shows, the user's own folders (round 164)

    // The "Show folders" window of this Browser (one per Browser), and its content for --snapshot.
    void showFoldersDialog();
    std::unique_ptr<juce::Component> makeFoldersPanelForSnapshot();

    // starred files and folders: collection 1 of the Browser's collections (below)
    bool isStarred (const juce::File& file) const { return collectionStore().has (file.getFullPathName().toStdString(), 0); }
    void setStarred (const juce::File& file, bool on);

    // --- Collections (round 190ce, core/BrowserCollections.h; docs/analise/REA_rodada190ce.md §1) -----------------------
    // Seven colour collections an item (a file, a folder, a Plugin database entry) can be in, several at once; Starred is
    // the first. One store for every Browser of the process, saved in the settings folder at each change. The STARRED
    // tab lists the collections that are switched on (only Starred at first: the tab then looks as it always did).
    static collections::Store& collectionStore();
    static juce::String collectionName (int collection);    // the user's name, else the translated default
    static juce::Colour collectionColour (int collection);
    void setInCollection (const std::string& item, int collection, bool on);
    // the number keys on the selected row: 1-7 put it into that collection or take it out, 0 takes it out of all
    void toggleCollectionOnSelection (int collection);
    void clearCollectionsOnSelection();
    void renameCollection (int collection);                  // asks for the name (empty: the default again)
    void setCollectionShown (int collection, bool shown);
    void addCollectionItems (juce::PopupMenu& menu, const std::string& item); // the collections part of a row's menu
    // Round 190cy: the Clip-launch workflow's row menus list the collections flat, as Live's item menu does (the
    // Pattern workflow keeps the "Collections" sub-menu). Set by the main window with the workflow.
    void setLiveItemMenus (bool on) noexcept { liveItemMenus = on; }
    void selectRowForSnapshot (const juce::String& label);   // --shots: the first row with this name
    juce::PopupMenu collectionsMenuForSnapshot (const std::string& item);

    // The automatic preview (the reference's headphone switch, its menu "browser pre-hear"): off = selecting a file
    // shows its waveform and details without playing it; the strip's Play button still plays it. A user setting.
    static inline bool autoPreview = true;

    // --- Hot-swap (round 190ce, core/HotSwap.h; docs/analise/REA_rodada190ce.md §2) --------------------------------------
    // The Browser linked to a device: a bar over the tabs names it, and Enter / a double-click on a Plugin database
    // entry, a preset or a sample replaces it in place. Ended by the bar's X, Esc, the hot-swap key or the device's own
    // button, and when the device is deleted.
    void beginHotSwap (const hotswap::Target& target);
    void endHotSwap();
    bool isHotSwapping() const noexcept { return swapSession.active(); }
    const hotswap::Target& hotSwapTarget() const noexcept { return swapSession.target; }
    juce::String hotSwapTargetName() const;
    bool hotSwapLoadSelected(); // Enter while linked: true when the mode took the key
    std::function<void()> onHotSwapChanged;                                // the link began, moved to a new device or ended
    std::function<void (int channel)> onHotSwapWillReplaceInstrument;      // (the channel's editor closes first)
    std::function<void (int channel, const juce::File&)> onHotSwapSample;  // a sample into the linked channel
    class HotSwapBar;

    // --- Round 190iz: tags (FL's Browser tagging, core/BrowserTags.h), Live's browser in the Clip-launch layout,
    // "Send to selected channel or focused plugin", FL's preview length ----------------------------------------------------
    // One tag store for every Browser of the process (the user's, browser-tags.txt in the settings folder; not the project).
    static browsertags::Store& tagStore();
    // Round 190ke (owner decision D2): what the TAGS panel, the tag filter and the row menus' ticks see — the user's store,
    // plus the built-in tags of LYNX's own factory content (core/FactoryContent.h) in the Simple layout
    static browsertags::Store shownTags();
    // Round 190ke: the "LYNX content" items' actions the main window carries out (a beat becomes a Drums track at the
    // playhead; an effect chain goes onto the selected track's insert). Sounds go through onAddInstrument.
    std::function<void (const std::string& pluginKey)> onFactoryItem;
    // FL's "Tags" section of a row's menu: Tags ▸ (each tag ticked on / off), Add tag..., Edit tags...
    void addTagItems (juce::PopupMenu& menu, const std::string& item);
    void askAddTag (const std::string& item); // FL's "Add a tag": names separated by commas
    void showEditTags();                      // FL's "Edit tags": rename / delete a tag everywhere
    void setTagged (const std::string& item, const juce::String& tag, bool on);
    // The search row's TAGS button (FL's) shows the tags panel; the tags picked there filter the list, Any or All.
    void setTagsPanelShown (bool shown);
    bool isTagsPanelShown() const noexcept { return tagsPanelShown; }
    void setPickedTags (std::vector<std::string> tags);
    const std::vector<std::string>& pickedTags() const noexcept { return picked; }
    void setTagMatch (browsertags::Match match);
    browsertags::Match tagMatch() const noexcept { return matchMode; }
    // The Clip-launch layout's browser (Live 12's): Collections / Library / Places beside the list; the main window sets
    // it with the layout. The Pattern and Simple layouts keep FL's tabs and tree.
    void setLiveLayout (bool on);
    bool isLiveLayout() const noexcept { return liveLayout; }
    void showLiveEntry (const browserplaces::Entry& entry); // the sidebar picked an entry (also --shots)
    const browserplaces::Entry& liveEntry() const noexcept { return liveSelection; }
    // FL's "Send to selected channel or focused plugin" (file menu, Plugin database entries, the middle button): a file
    // into the selected channel (or the focused plugin window's plugin), a generator replacing the selected channel's.
    void sendToSelected (const juce::File& file);
    void sendPluginToSelected (const std::function<std::optional<ChannelDef>()>& generator, const std::function<std::optional<EffectSlot>()>& effect);
    browsersend::Channel selectedChannelKind() const;
    // The next preview (fileSelected) plays from `startFraction` of the file, or to its end when `full` (Alt+Click,
    // the strip's Play button); FL's "Full sample preview" option lives in the engine.
    void askPreview (double startFraction, bool full) noexcept
    {
        previewStartAsked = startFraction;
        previewFullAsked = full;
    }
    // A click on the hover star of a row (FL's favourite): the row's item in or out of Starred.
    void toggleStarredItem (const std::string& item);
    // --browsershot190iz: a row drawn as under the mouse (its hover star), the file menu, the strip with a played part
    void hoverRowForSnapshot (const juce::String& label);
    juce::PopupMenu buildFileMenu (const juce::File& file);
    void showPreviewPlayedForSnapshot (const juce::File& file, double fromFraction);
    // --shots: the Browser's prefs file is not read (a clean look)
    static inline bool readPrefs = true;
    class TagsPanel;
    class LiveSidebar;

    // --- Round gaps190kf: the Loop Browser of the Sample browser window (GarageBand's: keyword buttons, Scale, Loop
    // Types, favourites; LYNX's Tempo and Places; core/LoopKeywords.h). The search row's LOOPS button shows it.
    void setLoopsShown (bool shown);
    bool isLoopsShown() const noexcept { return loopsShown; }
    const loopkeys::Filter& loopFilter() const noexcept { return loopFilterState; }
    void setLoopFilter (loopkeys::Filter filter);
    int loopCategories() const noexcept { return loopCategoryMask; } // a bit per loopkeys::Category
    void setLoopCategories (int mask);
    const loopkeys::Layout& loopLayout() const noexcept { return loopLayoutState; }
    void setLoopLayout (loopkeys::Layout layout);
    int loopTempoChoice() const noexcept { return loopTempo; } // loopkeys::tempoRanges() index
    void setLoopTempoChoice (int choice);
    void setLoopNearKey (bool on);
    void setLoopPlace (int index); // -1 = every place, else loopPlaces()'s index
    juce::Array<juce::File> loopPlaces() const;
    juce::String loopPlaceName() const;
    const std::map<std::string, int>& loopKeywordCounts() const noexcept { return loopCounts; }
    bool loopsReady() const noexcept { return loopsAnswer; } // the loops were listed (the dimmed buttons mean something)
    void showLoopPlacesMenu();
    void showLoopScaleMenu();
    void showLoopTempoMenu();
    void showLoopTypesMenu();
    juce::PopupMenu buildLoopScaleMenu();
    juce::PopupMenu buildLoopTypesMenu();
    int numLoopsShownForSnapshot() const;
    void loadLoopsForSnapshot();
    juce::Point<int> loopKeywordCentreForSnapshot (const std::string& id) const;
    // the Simple layout's Loops tile: the next Sample browser opens with its Loop Browser shown; one already open shows it
    static inline bool openLoopsNext = false;
    static void showLoopsInSampleBrowser();
    class LoopPanel;
    class LoopsButton;

private:
    void textEditorTextChanged (juce::TextEditor&) override;
    void textEditorEscapeKeyPressed (juce::TextEditor&) override;
    void textEditorReturnKeyPressed (juce::TextEditor&) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void runSearch (bool quiet = false); // quiet: the index changed, the results are replaced without "Searching..."
    void searchAnswered (const search::SearchResult& result, const juce::String& query, const juce::Array<juce::File>& scope);
    void indexStatusChanged();
    void runSimilar (bool quiet);
    void similarAnswered (const similarity::Result& result);
    void similarStatusChanged();
    juce::String similarLabel() const; // the search box's text while similar samples are listed
    juce::Array<juce::File> similarScope() const;
    juce::String statusLine; // under the search box: indexing progress / what the search found
    void showNote (const juce::String& text); // a short note there (files copied in), gone after a few seconds
    juce::Rectangle<int> statusArea;
    juce::Rectangle<int> nameHeader; // round 190iz: Live's "Name" title over the list (Clip-launch layout)
    void rebuildRoots();
public:
    // Where a file dialog for samples should start (round 190t, the Simple workflow's Sampler tile): FL's sample packs
    // when FL Studio is installed, else the first folder the user added, else the Music folder. May not exist.
    juce::File firstSampleFolder() const;
private:
    void rebuildTree();
    static juce::File userFoldersFile();
    static juce::File starredFile();
    static juce::File viewFile (Role role);
    void loadView();
    void saveView() const;
    bool viewLoaded = false, viewFromFile = false;
    bool lists (const juce::File& file) const; // a file listed under the content filter
    // round 164: a folder whose files are all left out by the content filter (from the search index; unknown: false)
    bool filterHides (const juce::File& folder) const;
    std::shared_ptr<const search::FolderMatches> filterMatches() const;
    void applyFilterToTree();
    void removeUserFolder (const juce::File& folder); // takes one of the user's own folders out of the Browsers
    juce::Component::SafePointer<juce::Component> foldersPanel;
    void refreshFoldersPanel();
    struct FolderEntry
    {
        juce::String id, name, path;
        bool shown = true, user = false, empty = false; // empty: nothing in it is listed under the content filter
    };
    std::vector<FolderEntry> folderEntries() const; // the open "Show folders" window's content // drops the folders the index now knows to be empty under the filter
    std::uint64_t prunedGeneration = 0;
    void addHideItem (juce::PopupMenu& menu, const juce::String& id, const juce::String& label);
    juce::String rootId (const juce::File& folder) const; // the root's id, empty if `folder` isn't a root
    static juce::File collectionsFile();
    static void loadCollections();
    static void saveCollections();
    static std::vector<BrowserComponent*>& liveBrowsers();
    void collectionsChanged(); // every Browser draws its rows (and its STARRED tab) again
    std::set<int> closedCollections; // the STARRED tab's collection headings the user folded
    bool previewWanted (bool asked) const noexcept { return asked && (autoPreview || forcePreview); }
    bool forcePreview = false; // round 190ld (DL-98): Live's Shift+Enter / → preview even with the automatic preview off
    hotswap::Session swapSession;
    bool liveItemMenus = false;
    // round 190cy: hot-swap starts on the linked effect's current preset (Live 12.4.16 reveals it, live check 190cw
    // shot 02a): in its folder when the Browser shows that folder, else the effect's presets as a list, the current
    // one selected. False: the effect has no preset file.
    bool revealCurrentPreset (const hotswap::Target& target);
    bool presetsListed = false; // (that list is up: it goes when the mode ends)
    std::unique_ptr<HotSwapBar> swapBar;
    bool hotSwapLoadFile (const juce::File& file);
    bool hotSwapLoadPlugin (const std::function<std::optional<ChannelDef>()>& generator, const std::function<std::optional<EffectSlot>()>& effect);
    void hotSwapRefused();
    juce::Array<juce::File> collectionMatches (const juce::String& query) const; // files found through a collection's name
    void openFile (const juce::File& file); // what a double-click does
    void newSampleChannel (const juce::File& file); // a new Sampler channel playing `file`
    void fileSelected (const juce::File& file, bool preview);
    void showFolderMenu (const juce::File& folder, std::function<void()> expandAll = {});
    void showFileMenu (const juce::File& file);
    void addSystemItems (juce::PopupMenu& menu, const juce::File& file);
    void confirmDeleteFile (const juce::File& file);
    FileTree& activeList() const;
    juce::Array<juce::File> searchRoots() const;
    juce::File previewedFile; // the sample last sent to the preview player
    bool isPreviewPlaying() const;

    std::unique_ptr<FileTree> tree, results;
    std::unique_ptr<juce::Viewport> treeView, resultsView;
    std::unique_ptr<PreviewStrip> strip;
    std::unique_ptr<DetailsBox> details;
    std::unique_ptr<TabBar> tabs;
    // round 190iz
    std::unique_ptr<TagsPanel> tagsPanel;
    std::unique_ptr<LiveSidebar> sidebar;
    std::unique_ptr<juce::Viewport> sidebarView;
    juce::String categoryQuery; // round 190iz: Live's Drums lists a search of the sample places
    class TagsButton;
    std::unique_ptr<TagsButton> tagsButton;
    bool tagsPanelShown = false, liveLayout = false;
    std::vector<std::string> picked;
    browsertags::Match matchMode = browsertags::Match::any;
    browserplaces::Entry liveSelection; // (Library › All at first)
    double previewStartAsked = 0.0;
    bool previewFullAsked = false;
    static juce::File tagsFile();
    static void loadTags();
    static void saveTags();
    void tagsChanged(); // every Browser draws its rows, its tags panel and its tag filter again
    juce::File prefsFile() const;
    void loadPrefs();
    void savePrefs() const;
    bool tagFilterActive() const;
    juce::String effectiveQuery() const { const auto q = searchBox.getText().trim(); return q.isEmpty() ? categoryQuery : q; }
    void runTagFilter();
    // round gaps190kf: the Loop Browser
    void runLoopSearch();
    void loopsAnswered (std::uint64_t id, std::vector<loopkeys::LoopInfo> infos, bool partial, bool truncated);
    void applyLoopFilter();
    std::unique_ptr<LoopPanel> loopPanel;
    std::unique_ptr<LoopsButton> loopsButton;
    bool loopsShown = false, loopsAnswer = false, loopsTruncated = false, loopNearKey = false;
    loopkeys::Filter loopFilterState;
    loopkeys::Layout loopLayoutState = loopkeys::Layout::defaults();
    int loopCategoryMask = 1, loopTempo = 0, loopPlaceIndex = -1;
    std::vector<loopkeys::LoopInfo> loopInfos;
    std::map<std::string, int> loopCounts;
    std::uint64_t loopSearchId = 0;
    browserview::ContentFilter activeFilter() const; // the tab's / Live entry's filter, else the view's
    bool showsRoot (int rootIndex) const;            // a folder place the current tab / Live entry lists
    struct SearchShared; // the process's search worker (core/SearchService), shared by every Browser
    std::shared_ptr<SearchShared> searcher;
    std::uint64_t searchId = 0;
    bool searchPartial = false;
    struct SimilarShared; // the process's similarity worker (core/SampleSimilarity), shared by every Browser
    std::shared_ptr<SimilarShared> similar;
    juce::File similarTo; // the sample whose similar ones are listed (none: an ordinary search)
    std::uint64_t similarId = 0;
    bool similarPartial = false;
    juce::uint32 similarAskedAt = 0;
    juce::TextEditor searchBox;
    class FocusWatcher; // a click outside the search box takes the keyboard back from it
    std::unique_ptr<FocusWatcher> focusWatcher;
    IconButton stopButton { "play or stop preview", Icon::play }; // on the preview strip (FL 2026)
    IconButton viewButton { "browser options", Icon::panelMenu };
    void refreshViewButton();
    juce::Slider previewVolume { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    std::unique_ptr<juce::FileChooser> chooser;
    int tab = allTab;
    bool searchPending = false, projectDirty = false;
    juce::File projectFile;
    std::set<juce::String> openPluginFolders; // the Plugin database folders the user opened (round 142)
    std::set<juce::String> openFactoryFolders; // round 190ke: the "LYNX content" folders the user opened
public:
    // round 190ke: the layout style changed (the Simple layout lists "LYNX content" and its tags): every Browser follows
    static void layoutStyleChanged();
private:

    AudioEngine& engine;
    Role role = Role::main;
    browserview::View view;
    juce::Array<juce::File> roots; // the folder roots shown
    juce::StringArray rootNames;
    juce::Array<juce::File> allRoots; // every folder root (hidden ones too)
    juce::StringArray allRootNames;
    std::vector<browserview::RootKind> allRootKinds;
    std::map<juce::String, juce::Array<juce::File>> rootAlso; // a merged root's other folders, by its path (round 178a)
    juce::Array<juce::File> mergedInto (const juce::File& root) const;              // a root's merged folders (none: empty)
    juce::Array<juce::File> withMerged (const juce::Array<juce::File>& folders) const; // the folders and theirs

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrowserComponent)
};
} // namespace lynx
