// Round gaps190kf (item 3; included at the end of ui/BrowserComponent.cpp inside namespace lynx): the Browser's Loop
// Browser members (the panel itself is in ui/LoopBrowserPanel.inc; the logic in core/LoopKeywords.h).

// --- the loops' lengths (round 190mf, DS-41) ---------------------------------------------------------------------------
// BandLab's Sounds rows start with the loop's length in bars ("4 bars · Synth · Moody, Bright · 149bpm · B", capture B11).
// The length is the audio file's (its header: frames / sample rate), read on a low-priority thread of its own, never
// while listing or painting, and kept for the session; the rows that wait for one are filled in when it arrives
// (loopLengthsRead() changes, the Browser updates those rows in place).
class LoopLengths final : private juce::Thread,
                          private juce::DeletedAtShutdown
{
public:
    static LoopLengths& get()
    {
        if (instance == nullptr)
            instance = new LoopLengths();
        return *instance;
    }

    // message thread: the file's length in seconds; -1 = not read yet (call request), 0 = unreadable
    double seconds (const std::string& path) const
    {
        const std::lock_guard<std::mutex> guard (lock);
        const auto it = known.find (path);
        return it == known.end() ? -1.0 : it->second;
    }

    // message thread: reads these (the ones not known or queued yet) in the background
    void request (std::vector<std::string> paths)
    {
        if (paths.empty())
            return;
        {
            const std::lock_guard<std::mutex> guard (lock);
            for (auto& p : paths)
                if (known.count (p) == 0 && queued.insert (p).second)
                    todo.push_back (std::move (p));
        }
        if (! isThreadRunning())
            startThread (juce::Thread::Priority::low);
        notify();
    }

    juce::ChangeBroadcaster changed; // (sendChangeMessage: delivered on the message thread, coalesced)

private:
    LoopLengths() : juce::Thread ("LYNX loop lengths") { formats.registerBasicFormats(); }
    ~LoopLengths() override
    {
        stopThread (4000);
        instance = nullptr;
    }
    static inline LoopLengths* instance = nullptr;

    void run() override
    {
        while (! threadShouldExit())
        {
            std::string path;
            {
                const std::lock_guard<std::mutex> guard (lock);
                if (! todo.empty())
                {
                    path = std::move (todo.front());
                    todo.pop_front();
                }
            }
            if (path.empty())
            {
                wait (-1);
                continue;
            }
            double secs = 0.0;
            if (std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (juce::File (juce::String::fromUTF8 (path.c_str()))) })
                if (reader->sampleRate > 0.0)
                    secs = (double) reader->lengthInSamples / reader->sampleRate;
            bool last = false;
            {
                const std::lock_guard<std::mutex> guard (lock);
                known[path] = secs;
                queued.erase (path);
                last = todo.empty();
            }
            // (every 32 files, and at the end of the queue: the rows are updated in batches)
            if (last || ++sinceNotice >= 32)
            {
                sinceNotice = 0;
                changed.sendChangeMessage();
            }
        }
    }

    mutable std::mutex lock;
    std::unordered_map<std::string, double> known;
    std::set<std::string> queued;
    std::deque<std::string> todo;
    int sinceNotice = 0;
    juce::AudioFormatManager formats;
};

juce::ChangeBroadcaster& loopLengthsRead() { return LoopLengths::get().changed; }

juce::String BrowserComponent::simpleLoopDetail (const loopkeys::LoopInfo& info, bool& lengthPending) const
{
    // round 190ld (details DS-41, BandLab B11 "4 bars · Synth · Moody, Bright · 149bpm · B"): in the Simple layout a row
    // names its instrument and its moods (the loop's keywords) before the tempo and the key; round 190mf: and starts
    // with its length in bars (the file's length at the loop's own tempo) once that has been read
    juce::StringArray parts, moods;
    juce::String instrument;
    lengthPending = false;
    if (info.tempo > 0.0 && factoryloops::itemOf (info.path) == nullptr)
    {
        const auto secs = LoopLengths::get().seconds (info.path);
        if (secs < 0.0)
            lengthPending = true;
        else if (const auto bars = loopkeys::loopBars (secs, info.tempo); bars > 0)
            parts.add (bars == 1 ? tr ("1 bar") : tr ("{0} bars", { juce::String (bars) }));
    }
    for (const auto& id : info.keywords)
        if (const auto* k = loopkeys::find (id))
        {
            if (k->category == loopkeys::Category::instrument && instrument.isEmpty() && juce::String (k->id).startsWith ("all ") == false)
                instrument = tr (k->label);
            else if (k->category == loopkeys::Category::descriptor && moods.size() < 2)
                moods.add (tr (k->label));
        }
    if (instrument.isNotEmpty())
        parts.add (instrument);
    if (! moods.isEmpty())
        parts.add (moods.joinIntoString (", "));
    const auto tempo = loopkeys::tempoText (info), key = loopkeys::keyText (info);
    if (! tempo.empty())
        parts.add (juce::String (tempo) + "bpm");
    if (! key.empty())
        parts.add (juce::String (key));
    return parts.isEmpty() ? juce::String (" ") : parts.joinIntoString (juce::String::fromUTF8 (" \xc2\xb7 "));
}

void BrowserComponent::loopLengthsArrived()
{
    if (! loopsShown || layoutstyle::current() != layoutstyle::Style::simple || results == nullptr)
        return;
    std::unordered_map<std::string, const loopkeys::LoopInfo*> byPath;
    for (const auto& info : loopInfos)
        if (info.tempo > 0.0)
            byPath.emplace (info.path, &info);
    results->updateDetails ([this, &byPath] (FileTree::Node& n) {
        if (n.file == juce::File())
            return false;
        const auto it = byPath.find (n.file.getFullPathName().toStdString());
        if (it == byPath.end())
            return false;
        bool pending = false;
        auto detail = simpleLoopDetail (*it->second, pending);
        if (pending || detail == n.detail)
            return false;
        n.detail = std::move (detail);
        return true;
    });
}

// --- BrowserComponent's Loop Browser parts ------------------------------------------------------------------------------
juce::Array<juce::File> BrowserComponent::loopPlaces() const
{
    // the sample places (the SOUNDS tab's) and FL's Scores (pattern loops), as the view shows them
    juce::Array<juce::File> out;
    for (int i = 0; i < allRoots.size() && i < (int) allRootKinds.size(); ++i)
    {
        const auto kind = allRootKinds[(std::size_t) i];
        if (roots.contains (allRoots[i]) && (browserplaces::tabShowsRoot (browserplaces::Tab::sounds, kind) || kind == browserview::RootKind::flMidi))
            out.add (allRoots[i]);
    }
    return out;
}

juce::String BrowserComponent::loopPlaceName() const
{
    const auto places = loopPlaces();
    if (loopPlaceIndex >= 0 && loopPlaceIndex < places.size())
        if (const auto i = allRoots.indexOf (places[loopPlaceIndex]); i >= 0)
            return allRootNames[i];
    return tr ("Show All");
}

void BrowserComponent::setLoopsShown (bool shown)
{
    if (shown == loopsShown)
        return;
    loopsShown = shown;
    if (shown)
        tagsPanelShown = false; // (one filter panel at a time)
    loopsButton->repaint();
    tagsButton->repaint();
    resized();
    if (shown)
        runLoopSearch();
    else
    {
        loopInfos.clear();
        loopCounts.clear();
        loopsAnswer = false;
        runSearch();
    }
    savePrefs();
}

void BrowserComponent::setLoopFilter (loopkeys::Filter f)
{
    loopFilterState = std::move (f);
    applyLoopFilter();
}

void BrowserComponent::setLoopCategories (int mask)
{
    loopCategoryMask = juce::jlimit (1, 7, mask);
    resized();
    loopPanel->refresh();
    savePrefs();
}

void BrowserComponent::setLoopLayout (loopkeys::Layout layout)
{
    loopLayoutState = std::move (layout);
    loopPanel->refresh();
    savePrefs();
}

void BrowserComponent::setLoopTempoChoice (int choice)
{
    loopTempo = juce::jlimit (0, (int) loopkeys::tempoRanges().size() - 1, choice);
    applyLoopFilter();
}

void BrowserComponent::setLoopNearKey (bool on)
{
    loopNearKey = on;
    applyLoopFilter();
}

void BrowserComponent::setLoopPlace (int index)
{
    loopPlaceIndex = index;
    loopPanel->repaint();
    runLoopSearch();
}

void BrowserComponent::showLoopPlacesMenu()
{
    juce::PopupMenu menu;
    auto safe = juce::Component::SafePointer<BrowserComponent> (this);
    menu.addItem (tr ("Show All"), true, loopPlaceIndex < 0, [safe] { if (safe != nullptr) safe->setLoopPlace (-1); });
    menu.addSeparator();
    const auto places = loopPlaces();
    for (int p = 0; p < places.size(); ++p)
        if (const auto i = allRoots.indexOf (places[p]); i >= 0)
            menu.addItem (allRootNames[i], true, loopPlaceIndex == p, [safe, p] { if (safe != nullptr) safe->setLoopPlace (p); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (loopPanel.get()));
}

void BrowserComponent::showLoopScaleMenu()
{
    buildLoopScaleMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (loopPanel.get()));
}

juce::PopupMenu BrowserComponent::buildLoopScaleMenu()
{
    juce::PopupMenu menu;
    auto safe = juce::Component::SafePointer<BrowserComponent> (this);
    using S = loopkeys::Scale;
    const std::pair<S, juce::String> items[] { { S::any, tr ("Any") }, { S::minor, tr ("Minor") }, { S::major, tr ("Major") }, { S::neither, tr ("Neither") }, { S::both, tr ("Good for Both") } };
    for (const auto& [s, name] : items)
        menu.addItem (name, true, loopFilterState.scale == s, [safe, s = s] {
            if (safe == nullptr)
                return;
            auto f = safe->loopFilter();
            f.scale = s;
            safe->setLoopFilter (std::move (f));
        });
    menu.addSeparator();
    // GarageBand's Loops setting "Filter for more relevant results" (keys within two semitones of the project key)
    const auto key = projectkey::get (engine.getSong());
    menu.addItem (tr ("Only keys close to the project key (within two semitones)"), key.has_value(), loopNearKey, [safe] {
        if (safe != nullptr)
            safe->setLoopNearKey (! safe->loopNearKey);
    });
    return menu;
}

void BrowserComponent::showLoopTempoMenu()
{
    juce::PopupMenu menu;
    auto safe = juce::Component::SafePointer<BrowserComponent> (this);
    const auto& ranges = loopkeys::tempoRanges();
    for (int i = 0; i < (int) ranges.size(); ++i)
    {
        auto name = tr (ranges[(std::size_t) i].label);
        if (ranges[(std::size_t) i].min < 0.0)
            name << " (" << juce::String (engine.getSong().bpm, 0) << " BPM)";
        menu.addItem (name, true, loopTempo == i, [safe, i] { if (safe != nullptr) safe->setLoopTempoChoice (i); });
        if (i == 1)
            menu.addSeparator();
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (loopPanel.get()));
}

void BrowserComponent::showLoopTypesMenu()
{
    buildLoopTypesMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (loopPanel.get()));
}

juce::PopupMenu BrowserComponent::buildLoopTypesMenu()
{
    // GarageBand's Loop Types: All Loops, Audio Loops, MIDI Loops, Drummer Loops (LYNX: pattern loops), each ticked
    juce::PopupMenu menu;
    auto safe = juce::Component::SafePointer<BrowserComponent> (this);
    menu.addSectionHeader (tr ("Loop Types"));
    const auto types = loopFilterState.types;
    menu.addItem (tr ("All Loops"), true, types == loopkeys::allTypes, [safe] {
        if (safe == nullptr)
            return;
        auto f = safe->loopFilter();
        f.types = loopkeys::allTypes;
        safe->setLoopFilter (std::move (f));
    });
    const std::pair<int, juce::String> items[] { { loopkeys::audio, tr ("Audio Loops") }, { loopkeys::midi, tr ("MIDI Loops") }, { loopkeys::pattern, tr ("Pattern Loops") } };
    for (const auto& [bit, name] : items)
        menu.addItem (name, true, (types & bit) != 0, [safe, bit = bit] {
            if (safe == nullptr)
                return;
            auto f = safe->loopFilter();
            const auto next = f.types ^ bit;
            f.types = next == 0 ? bit : next; // (one type at least)
            safe->setLoopFilter (std::move (f));
        });
    return menu;
}

// Rollup 0.1.30 step 6 (the owner's yes to gaps190kf's question): LYNX's own beats and sounds (round 190ke,
// core/FactoryLoops.h) are listed with the places' loops under Show All, where the Browser shows LYNX content (the Simple
// layout, round 190ke's rule) — also when no sample folder is there yet.
static bool factoryLoopsShown (int placeIndex)
{
    return placeIndex < 0 && layoutstyle::current() == layoutstyle::Style::simple;
}

// Each listed file that is a loop (core/LoopKeywords typeOf), described with the words below the place it is in (the
// user's own folder names above it don't count). Any thread: it touches nothing of the Browser.
static std::vector<loopkeys::LoopInfo> describeLoops (const std::vector<std::string>& paths, const std::vector<std::string>& rootPaths)
{
    std::vector<loopkeys::LoopInfo> out;
    out.reserve (paths.size());
    for (const auto& path : paths)
    {
        const auto slash = path.find_last_of ("/\\");
        if (loopkeys::typeOf (slash == std::string::npos ? path : path.substr (slash + 1)) == 0)
            continue;
        std::string below = path;
        for (const auto& root : rootPaths)
            if (path.size() > root.size() && path.compare (0, root.size(), root) == 0)
            {
                below = path.substr (root.size() + 1);
                break;
            }
        out.push_back (loopkeys::describe (path, below, {}, loopkeys::factoryKeywords (path)));
    }
    return out;
}

// The loops of the places: every listed file under them from the search index (one search with an empty query), each
// described on the worker thread (core/LoopKeywords: words, name tempo and key, factory keywords); the user's tags and
// the filter are applied on the message thread.
void BrowserComponent::runLoopSearch()
{
    if (! loopsShown)
        return;
    searcher->service->cancelSearch();
    auto scope = loopPlaces();
    if (loopPlaceIndex >= 0 && loopPlaceIndex < scope.size())
        scope = { scope[loopPlaceIndex] };
    scope = withMerged (scope);
    if (scope.isEmpty())
    {
        loopsAnswered (loopSearchId, {}, false, false); // (no place: LYNX's own loops only, where shown)
        return;
    }
    if (loopInfos.empty())
    {
        results->setFiles ({}, scope, tr ("Looking for loops..."));
        resultsView->setVisible (true);
        treeView->setVisible (false);
    }
    std::vector<std::string> rootPaths;
    for (const auto& f : scope)
        rootPaths.push_back (f.getFullPathName().toStdString());
    loopSearchId = searcher->service->search ("", toPaths (scope), (std::size_t) maxLoops,
                                              [safe = juce::Component::SafePointer<BrowserComponent> (this), rootPaths] (const search::SearchResult& r) {
                                                  // (worker thread: describe each loop; nothing of the Browser is touched here)
                                                  auto described = std::make_shared<std::vector<loopkeys::LoopInfo>> (describeLoops (r.paths, rootPaths));
                                                  juce::MessageManager::callAsync ([safe, described, id = r.id, partial = r.partial, truncated = r.truncated] {
                                                      if (safe != nullptr)
                                                          safe->loopsAnswered (id, std::move (*described), partial, truncated);
                                                  });
                                              });
}

// --loopshot190kf: the same, synchronously (the index is made first; the shot is drawn before any message is dispatched)
void BrowserComponent::loadLoopsForSnapshot()
{
    loopsShown = true;
    auto scope = withMerged (loopPlaces());
    std::vector<std::string> rootPaths;
    for (const auto& f : scope)
        rootPaths.push_back (f.getFullPathName().toStdString());
    auto answer = std::make_shared<std::pair<std::mutex, search::SearchResult>>();
    searcher->service->search ("", toPaths (scope), (std::size_t) maxLoops, [answer] (const search::SearchResult& r) {
        const std::lock_guard<std::mutex> guard (answer->first);
        answer->second = r;
    });
    searcher->service->waitIdle (120000);
    search::SearchResult r;
    {
        const std::lock_guard<std::mutex> guard (answer->first);
        r = answer->second;
    }
    loopSearchId = r.id;
    loopsAnswered (r.id, describeLoops (r.paths, rootPaths), false, r.truncated);
}

void BrowserComponent::loopsAnswered (std::uint64_t id, std::vector<loopkeys::LoopInfo> infos, bool partial, bool truncated)
{
    if (id != loopSearchId || ! loopsShown)
        return;
    if (factoryLoopsShown (loopPlaceIndex)) // (rollup step 6: LYNX's own beats and sounds, round 190ke)
    {
        auto own = factoryloops::loops();
        infos.insert (infos.end(), std::make_move_iterator (own.begin()), std::make_move_iterator (own.end()));
    }
    // the user's tags that name a keyword give it (round 190iz's tags)
    const auto& store = tagStore();
    for (auto& info : infos)
        if (const auto it = store.items.find (info.path); it != store.items.end())
            for (const auto& key : it->second)
                if (const auto* k = loopkeys::find (store.nameOf (key)))
                {
                    info.keywords.insert (k->id);
                    for (const auto* a : k->also)
                        info.keywords.insert (a);
                }
    std::stable_sort (infos.begin(), infos.end(), [] (const loopkeys::LoopInfo& a, const loopkeys::LoopInfo& b) {
        return juce::String::fromUTF8 (a.name.c_str()).compareNatural (juce::String::fromUTF8 (b.name.c_str())) < 0;
    });
    // round 190kx: the loops whose name gives no tempo or key are analysed (ui/LoopInfo; the answers fill the Tempo and
    // Key columns and the filters as they come)
    {
        std::vector<std::string> unknown;
        for (const auto& info : infos)
            if ((! (info.tempo > 0.0) || info.keyRoot < 0) && factoryloops::itemOf (info.path) == nullptr && unknown.size() < 2000)
                unknown.push_back (info.path);
        if (! unknown.empty())
            loopInfoShared().request (unknown);
    }
    loopInfos = std::move (infos);
    loopsAnswer = ! partial;
    loopsTruncated = truncated;
    applyLoopFilter();
}

void BrowserComponent::applyLoopFilter()
{
    if (! loopsShown)
        return;
    auto f = loopFilterState;
    f.text = searchBox.getText().trim().toStdString();
    const auto& song = engine.getSong();
    std::tie (f.tempoMin, f.tempoMax) = loopkeys::tempoBounds (loopTempo, song.bpm);
    if (const auto key = projectkey::get (song); key.has_value() && loopNearKey)
        f.nearKey = key->root;
    const auto& store = collectionStore();
    const auto favourite = [this, &store] (std::size_t i) { return store.has (loopInfos[i].path, 0); };
    // round 190kx: what the analysis found where the name says nothing (a loop of whole beats: its tempo; a key)
    if (loopInfo != nullptr)
        for (auto& info : loopInfos)
            if (! (info.tempo > 0.0) || info.keyRoot < 0)
                if (const auto r = loopInfo->known (info.path))
                {
                    if (! (info.tempo > 0.0) && r->isLoop())
                        info.tempo = r->bpm;
                    if (info.keyRoot < 0 && r->key)
                    {
                        info.keyRoot = r->key->root;
                        info.scale = r->key->minor ? loopkeys::Scale::minor : loopkeys::Scale::major;
                    }
                }
    loopCounts = loopkeys::keywordCounts (loopInfos, f, favourite);
    std::vector<std::unique_ptr<FileTree::Node>> nodes;
    std::vector<std::string> unread; // (round 190mf: the Simple rows whose length is not known yet)
    for (std::size_t i = 0; i < loopInfos.size(); ++i)
    {
        const auto& info = loopInfos[i];
        if (! loopkeys::matches (info, f, favourite (i)))
            continue;
        std::unique_ptr<FileTree::Node> n;
        if (const auto* own = factoryloops::itemOf (info.path)) // (rollup step 6: a LYNX beat or sound, 190ke's row)
        {
            if ((n = FileTree::makeFactoryItem (*this, own->key)) == nullptr)
                continue;
        }
        else
        {
            n = std::make_unique<FileTree::Node>();
            n->file = juce::File (juce::String::fromUTF8 (info.path.c_str()));
        }
        // GarageBand's Tempo and Key columns ("Display original tempo and key"), on the row's right
        const auto tempo = loopkeys::tempoText (info), key = loopkeys::keyText (info);
        if (layoutstyle::current() == layoutstyle::Style::simple)
        {
            bool pending = false;
            n->detail = simpleLoopDetail (info, pending);
            if (pending)
                unread.push_back (info.path);
        }
        else if (! tempo.empty() || ! key.empty() || n->file != juce::File())
            n->detail = tempo.empty() && key.empty() ? juce::String (" ")
                                                      : juce::String (tempo) + (tempo.empty() || key.empty() ? "" : juce::String::fromUTF8 ("  \xc2\xb7  ")) + juce::String (key);
        nodes.push_back (std::move (n));
    }
    LoopLengths::get().request (std::move (unread));
    const auto count = (int) nodes.size();
    resultsView->setVisible (true);
    treeView->setVisible (false);
    results->setNodes (std::move (nodes), count > 0 ? juce::String()
                                          : loopInfos.empty() ? (loopsAnswer ? tr ("No loops in these places: add a sample folder (Browser options > Add folder...).")
                                                                             : tr ("Looking for loops..."))
                                                              : tr ("No loop matches these keywords."));
    resultsView->setViewPosition (0, 0);
    // GarageBand: "18491 items" under the list
    auto line = count == 1 ? tr ("1 item") : tr ("{0} items", { juce::String (count) });
    if (! loopsAnswer)
        line << "  ·  " << tr ("indexing...");
    else if (loopsTruncated)
        line << "  ·  " << tr ("the first {0} files searched", { juce::String (maxLoops) });
    if (line != statusLine)
    {
        const auto relayout = statusLine.isEmpty();
        statusLine = line;
        if (relayout)
            resized();
        repaint();
    }
    loopPanel->repaint();
    loopsButton->repaint();
}

void BrowserComponent::showLoopsInSampleBrowser()
{
    for (auto* b : liveBrowsers())
        if (b->role == Role::samples)
            b->setLoopsShown (true);
}

int BrowserComponent::numLoopsShownForSnapshot() const { return loopsShown ? results->numRows() : 0; }

juce::Point<int> BrowserComponent::loopKeywordCentreForSnapshot (const std::string& id) const
{
    return loopPanel->cellCentre (id) + loopPanel->getPosition();
}
