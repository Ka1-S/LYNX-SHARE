#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/BrowserCollections.h"
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

    enum Tab
    {
        allTab,
        projectTab,
        starredTab, // the files and folders the user starred (FL's STARRED)
        tabCount
    };
    void setTab (int tab);
    int getTab() const noexcept { return tab; }

    void focusSearch();
    void addUserFolder(); // asks for a folder and adds it to the folder list
    void reloadUserFolders() { rebuildRoots(); } // after the list changed elsewhere (Settings > Files)
    void setSearchForSnapshot (const juce::String& text);
    // For snapshots: selects (and "previews" without sound) the first audio file under the first folder,
    // and puts the mouse over the row after it.
    void selectFirstSampleForSnapshot();
    // --snapshot: the PROJECT tab with the patterns, generators and samples open, a sample selected
    void showProjectForSnapshot();
    void showPluginDatabaseForSnapshot();
    std::vector<juce::PopupMenu> pluginDatabaseMenusForSnapshot(); // round 190bf: a folder's and an entry's right-click menus
    // FL's "Locate sample in browser" (the sampler's File row, round 190df): the ALL tab with the folders opened down to
    // the file, its row selected; false (and a note) when no folder the Browser shows holds it
    bool locateFile (const juce::File& file);
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
    void loadSelected();
    juce::File getSelectedFile() const;
    // Left / Right: close / open the selected folder (Left on a file goes to its folder)
    void openFolder (bool open);

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
    bool previewWanted (bool asked) const noexcept { return asked && autoPreview; }
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
