// Round 190qi: the Browser parts that need its tree (included at the end of ui/BrowserComponent.cpp, namespace lynx).

std::vector<std::string> BrowserComponent::searchNames() const
{
    std::vector<std::string> names;
    for (const auto& n : allRootNames)
        names.push_back (n.toStdString());
    if (tree != nullptr)
        tree->folderNames (names);
    return names;
}

void BrowserComponent::updateSearchSuggestion (bool evenWithoutFocus)
{
    // the last word's completion (core/BrowserTags.h completeQuery: '#' words by the tags, others by the folder names
    // listed so far, then the tags), only while the caret is at the end of what is typed
    juce::String rest, suggestion;
    const auto text = searchBox.getText();
    if (browsertags::tabCompletes() && text.isNotEmpty() && (evenWithoutFocus || searchBox.hasKeyboardFocus (true)) && searchBox.getCaretPosition() >= text.length() && similarTo == juce::File())
    {
        std::vector<std::string> tags;
        for (const auto& t : shownTags().all())
        {
            tags.push_back (t.name);
            for (auto parent = browsertags::tagParent (t.name); ! parent.empty(); parent = browsertags::tagParent (parent))
                if (std::find (tags.begin(), tags.end(), parent) == tags.end())
                    tags.push_back (parent); // (a group made only by its children's names: "Drums" of "Drums/Kick")
        }
        const auto done = juce::String::fromUTF8 (browsertags::completeQuery (text.toStdString(), tags, searchNames()).c_str());
        if (done.length() > text.length())
        {
            rest = done.substring (text.length());
            suggestion = done;
        }
    }
    // (Tab gives the candidate's own spelling: "#dr" becomes "#Drums")
    searchSuggestion = rest.isEmpty() ? juce::String() : suggestion;
    if (searchGhost != nullptr)
    {
        searchGhost->setBounds (searchBox.getLocalBounds());
        searchGhost->setRest (rest);
    }
}

bool BrowserComponent::acceptSearchSuggestion()
{
    if (searchSuggestion.isEmpty() || ! searchSuggestion.startsWithIgnoreCase (searchBox.getText()))
        return false;
    searchBox.setText (searchSuggestion, true); // (searches as typing does)
    searchBox.moveCaretToEnd();
    searchSuggestion.clear();
    if (searchGhost != nullptr)
        searchGhost->setRest ({});
    return true;
}

// --- --picker190qishot (round 190qi) --------------------------------------------------------------------------------------

void BrowserComponent::markForSnapshot (const juce::Array<juce::File>& files)
{
    tree->marked.clear();
    for (const auto& f : files)
        tree->marked.push_back (f.getFullPathName().toStdString());
    tree->repaint();
}

void BrowserComponent::typeSearchForSnapshot (const juce::String& text)
{
    searchBox.setText (text, false);
    searchBox.moveCaretToEnd();
    updateSearchSuggestion (true);
}

std::unique_ptr<juce::Component> BrowserComponent::makeFileTypesPanelForSnapshot()
{
    return std::make_unique<FileTypesPanel> (this);
}

bool BrowserComponent::midiPreviewPlaying() const
{
    return midiPreview != nullptr && midiPreview->playing();
}

int BrowserComponent::midiPreviewChannel() const
{
    return midiPreview != nullptr && midiPreview->playing() ? midiPreview->playingChannel() : -1;
}

int BrowserComponent::listedRowsForSnapshot() const
{
    return tree->numRows();
}

juce::StringArray BrowserComponent::listedLabelsForSnapshot() const
{
    return tree->rowLabels();
}
