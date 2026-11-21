// Round 190iz (included by ui/BrowserComponent.cpp inside namespace lynx, after the Browser's other parts): FL's tab
// icons, the search row's TAGS button and the tags panel (FL's Browser tagging), and the Clip-launch layout's Live-style
// sidebar (Collections / Library / Places). Everything here is drawn statically: no timer, nothing painted while idle.
// docs/analise/REA_rodada190iz.md.

juce::File userTemplatesFolder(); // (ui/NewProjectDialog.h: Live's Templates place lists it)

namespace browsertabicons
{
// LYNX vectors for FL's six tab icons (reference fl-playlist.png 0,104,205,140: a waveform, two documents, a plug, a
// globe, a cloud, a star), drawn for a box of `area` in `colour`; nothing of FL's artwork is used.
inline void draw (juce::Graphics& g, browserplaces::Tab tab, juce::Rectangle<float> area, juce::Colour colour)
{
    using browserplaces::Tab;
    const auto s = juce::jmin (area.getWidth(), area.getHeight());
    const auto b = area.withSizeKeepingCentre (s, s);
    const auto line = juce::jmax (1.2f, s * 0.085f);
    const auto x = [&b] (float f) { return b.getX() + b.getWidth() * f; };
    const auto y = [&b] (float f) { return b.getY() + b.getHeight() * f; };
    g.setColour (colour);
    switch (tab)
    {
        case Tab::all: // a short waveform burst on a centre line
        {
            juce::Path p;
            p.startNewSubPath (x (0.06f), y (0.5f));
            p.lineTo (x (0.94f), y (0.5f));
            g.strokePath (p, juce::PathStrokeType (line, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            const float bars[][2] = { { 0.30f, 0.16f }, { 0.42f, 0.30f }, { 0.50f, 0.40f }, { 0.58f, 0.30f }, { 0.70f, 0.16f } };
            for (const auto& bar : bars)
                g.fillRoundedRectangle (juce::Rectangle<float> (x (bar[0]) - line * 0.6f, y (0.5f - bar[1]), line * 1.2f, b.getHeight() * bar[1] * 2.0f), line * 0.5f);
            break;
        }
        case Tab::project: // two sheets, the front one with a folded corner
        {
            g.fillRoundedRectangle (juce::Rectangle<float> (x (0.18f), y (0.12f), b.getWidth() * 0.46f, b.getHeight() * 0.66f), 1.0f);
            juce::Path front;
            front.startNewSubPath (x (0.36f), y (0.24f));
            front.lineTo (x (0.64f), y (0.24f));
            front.lineTo (x (0.82f), y (0.42f));
            front.lineTo (x (0.82f), y (0.90f));
            front.lineTo (x (0.36f), y (0.90f));
            front.closeSubPath();
            g.setColour (colour.darker (0.55f));
            g.strokePath (front, juce::PathStrokeType (line * 0.9f));
            g.setColour (colour);
            g.fillPath (front);
            juce::Path fold;
            fold.addTriangle (x (0.64f), y (0.24f), x (0.64f), y (0.42f), x (0.82f), y (0.42f));
            g.setColour (colour.darker (0.45f));
            g.fillPath (fold);
            break;
        }
        case Tab::plugins: // a plug: a cone (the speaker) with two prongs
        {
            juce::Path cone;
            cone.startNewSubPath (x (0.10f), y (0.38f));
            cone.lineTo (x (0.30f), y (0.38f));
            cone.lineTo (x (0.56f), y (0.16f));
            cone.lineTo (x (0.56f), y (0.84f));
            cone.lineTo (x (0.30f), y (0.62f));
            cone.lineTo (x (0.10f), y (0.62f));
            cone.closeSubPath();
            g.fillPath (cone);
            g.fillRoundedRectangle (juce::Rectangle<float> (x (0.62f), y (0.30f), b.getWidth() * 0.12f, b.getHeight() * 0.40f), 1.0f);
            g.fillRect (juce::Rectangle<float> (x (0.74f), y (0.34f), b.getWidth() * 0.18f, line));
            g.fillRect (juce::Rectangle<float> (x (0.74f), y (0.66f) - line, b.getWidth() * 0.18f, line));
            break;
        }
        case Tab::presets: // a globe: the outline, a meridian, the equator
        {
            const auto r = b.reduced (s * 0.12f);
            g.drawEllipse (r, line);
            g.drawEllipse (r.withSizeKeepingCentre (r.getWidth() * 0.46f, r.getHeight()), line * 0.85f);
            g.fillRect (juce::Rectangle<float> (r.getX(), r.getCentreY() - line * 0.42f, r.getWidth(), line * 0.85f));
            break;
        }
        case Tab::sounds: // a cloud: three lobes on a flat base
        {
            juce::Path p;
            p.addEllipse (x (0.10f), y (0.42f), b.getWidth() * 0.34f, b.getHeight() * 0.34f);
            p.addEllipse (x (0.28f), y (0.24f), b.getWidth() * 0.42f, b.getHeight() * 0.42f);
            p.addEllipse (x (0.56f), y (0.38f), b.getWidth() * 0.34f, b.getHeight() * 0.36f);
            p.addRoundedRectangle (x (0.26f), y (0.52f), b.getWidth() * 0.50f, b.getHeight() * 0.24f, 1.0f);
            p.setUsingNonZeroWinding (true);
            g.fillPath (p);
            break;
        }
        case Tab::starred: // a five-point star
        {
            juce::Path p;
            p.addStar (b.getCentre().translated (0.0f, s * 0.03f), 5, s * 0.20f, s * 0.44f, 0.0f);
            g.fillPath (p);
            break;
        }
    }
}

// The row's favourite star (FL: "hover over a file and click the star that appears at the end of the content").
inline void drawRowStar (juce::Graphics& g, juce::Rectangle<float> box, bool on, juce::Colour colour)
{
    juce::Path p;
    p.addStar (box.getCentre(), 5, box.getWidth() * 0.22f, box.getWidth() * 0.5f, 0.0f);
    g.setColour (colour);
    if (on)
        g.fillPath (p);
    else
        g.strokePath (p, juce::PathStrokeType (1.0f));
}
} // namespace browsertabicons

// --- the TAGS button (FL's search row: ".searchpanel.tagsbtn", hint "|Search by tags") ------------------------------------
class BrowserComponent::TagsButton final : public juce::Component, public ui::HintSource
{
public:
    explicit TagsButton (BrowserComponent& b) : browser (b)
    {
        setRepaintsOnMouseActivity (true);
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
    }
    static constexpr int width = 40;

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        const auto on = browser.isTagsPanelShown(), filtering = ! browser.pickedTags().empty();
        const auto r = getLocalBounds().toFloat().reduced (0.5f, 2.5f);
        g.setColour (isMouseOver() ? colour::bg5 : colour::bg3);
        g.fillRoundedRectangle (r, radius::sm);
        if (on || filtering)
        {
            g.setColour (colour::accent);
            g.drawRoundedRectangle (r, radius::sm, 1.0f);
        }
        g.setColour (filtering ? colour::accentHover : on ? colour::textPrimary : colour::textSecondary);
        g.setFont (font (type::caption, true));
        g.drawText (tr ("TAGS"), getLocalBounds(), juce::Justification::centred, false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            browser.showEditTags();
            return;
        }
        browser.setTagsPanelShown (! browser.isTagsPanelShown());
    }

    ui::Hint hintAt (juce::Point<int>) override
    {
        ui::Hint h;
        h.text = tr ("Search by tags: show / hide the tags; the ones you pick filter the list (Any or All)  [RMB] edit tags");
        h.hasMenu = true;
        return h;
    }

private:
    BrowserComponent& browser;
};

// --- the tags panel (FL's ".tagspanel": a grid of the tags and the Any / All buttons; Live's tag chips) ----------------
class BrowserComponent::TagsPanel final : public juce::Component, public ui::HintSource
{
public:
    explicit TagsPanel (BrowserComponent& b) : browser (b) { setRepaintsOnMouseActivity (true); }

    static constexpr int chipHeight = 18, gap = 3, pad = 4, maxRows = 4;
    // round 190ke: the Simple layout lists LYNX content's tags too (GarageBand's Loop Browser shows its keyword buttons
    // as a grid of up to seven rows): more rows there, and the picked tags first so a pick never falls off the panel
    static int rowsAllowed() { return layoutstyle::current() == layoutstyle::Style::simple ? 9 : maxRows; }

    // The height the panel needs at `width` (its chips wrapped, at most maxRows rows), from the tags as they are.
    int heightFor (int width)
    {
        layout (width);
        return pad * 2 + rowsUsed * (chipHeight + gap) - gap;
    }

    void refresh()
    {
        layout (getWidth());
        repaint();
    }

    void resized() override { layout (getWidth()); }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        g.fillAll (colour::bg2);
        g.setColour (colour::border);
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
        const auto mouse = isMouseOver() ? getMouseXYRelative() : juce::Point<int> (-1, -1);
        g.setFont (font (type::caption, true));
        for (const auto& chip : chips)
        {
            const auto hover = chip.box.contains (mouse);
            const auto on = chip.kind == ChipKind::any ? browser.tagMatch() == browsertags::Match::any
                            : chip.kind == ChipKind::all ? browser.tagMatch() == browsertags::Match::all
                                                         : isPicked (chip.tag);
            // Live's chips: #363636 at rest, #464646 hovered; a picked tag (FL's filter) in the accent
            g.setColour (on ? colour::accent : hover ? juce::Colour (0xff464646) : juce::Colour (0xff363636));
            g.fillRoundedRectangle (chip.box.toFloat(), 3.0f);
            g.setColour (on ? juce::Colours::white : chip.kind == ChipKind::tag ? colour::textPrimary : colour::textSecondary);
            g.drawText (chip.text, chip.box, juce::Justification::centred, false);
        }
        if (chips.size() <= 2)
        {
            g.setColour (colour::textDisabled);
            g.setFont (font (type::caption));
            g.drawText (tr ("No tags yet: right-click a file > Add tag..."), getLocalBounds().withTrimmedLeft (lastX + 6).withTrimmedRight (4),
                        juce::Justification::centredLeft, true);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (const auto& chip : chips)
            if (chip.box.contains (e.getPosition()))
            {
                if (chip.kind == ChipKind::any || chip.kind == ChipKind::all)
                {
                    browser.setTagMatch (chip.kind == ChipKind::any ? browsertags::Match::any : browsertags::Match::all);
                    return;
                }
                if (e.mods.isPopupMenu())
                {
                    tagMenu (chip.tag);
                    return;
                }
                auto now = browser.pickedTags();
                if (isPicked (chip.tag))
                    std::erase_if (now, [&chip] (const std::string& t) { return browsertags::Store::key (t) == browsertags::Store::key (chip.tag); });
                else
                    now.push_back (chip.tag);
                browser.setPickedTags (std::move (now));
                return;
            }
        if (e.mods.isPopupMenu())
            browser.showEditTags();
    }

    ui::Hint hintAt (juce::Point<int> p) override
    {
        ui::Hint h;
        for (const auto& chip : chips)
            if (chip.box.contains (p))
            {
                if (chip.kind == ChipKind::any)
                    h.text = tr ("Any: show items with any of the picked tags");
                else if (chip.kind == ChipKind::all)
                    h.text = tr ("All: show only items with all the picked tags");
                else
                {
                    h.text = tr ("Tag \"{0}\": [LMB] pick / unpick it to filter the list  [RMB] rename or delete", { juce::String::fromUTF8 (chip.tag.c_str()) });
                    h.value = juce::String (chip.count);
                    h.hasMenu = true;
                }
                return h;
            }
        h.text = tr ("Tags: pick tags to filter the list  [RMB] edit tags");
        return h;
    }

private:
    enum class ChipKind
    {
        any,
        all,
        tag
    };
    struct Chip
    {
        ChipKind kind = ChipKind::tag;
        std::string tag;
        juce::String text;
        int count = 0;
        juce::Rectangle<int> box;
    };

    bool isPicked (const std::string& tag) const
    {
        const auto k = browsertags::Store::key (tag);
        return std::any_of (browser.pickedTags().begin(), browser.pickedTags().end(), [&k] (const std::string& t) { return browsertags::Store::key (t) == k; });
    }

    void layout (int width)
    {
        chips.clear();
        rowsUsed = 1;
        const auto font = theme::font (theme::type::caption, true);
        int x = pad, y = pad;
        auto place = [&] (Chip chip) {
            const auto w = juce::GlyphArrangement::getStringWidthInt (font, chip.text) + 12;
            if (x > pad && x + w > width - pad)
            {
                if (rowsUsed == rowsAllowed())
                    return;
                ++rowsUsed;
                x = pad;
                y += chipHeight + gap;
            }
            chip.box = { x, y, juce::jmin (w, juce::jmax (20, width - pad * 2)), chipHeight };
            x += chip.box.getWidth() + gap;
            lastX = x;
            chips.push_back (std::move (chip));
        };
        place ({ ChipKind::any, {}, tr ("Any"), 0, {} });
        place ({ ChipKind::all, {}, tr ("All"), 0, {} });
        x += 4;
        const auto all = BrowserComponent::shownTags().all(); // (round 190ke: LYNX content's tags in Simple)
        const auto simple = layoutstyle::current() == layoutstyle::Style::simple;
        for (int pass = simple ? 0 : 1; pass < 2; ++pass)
            for (const auto& t : all)
                if (! simple || isPicked (t.name) == (pass == 0))
                    place ({ ChipKind::tag, t.name, juce::String::fromUTF8 (t.name.c_str()), t.items, {} });
    }

    void tagMenu (const std::string& tag)
    {
        juce::PopupMenu menu;
        ui::addItemHeader (menu, tr ("Tag"), juce::String::fromUTF8 (tag.c_str()), theme::colour::accent);
        auto safe = juce::Component::SafePointer<BrowserComponent> (&browser);
        menu.addItem (tr ("Rename..."), [safe, tag] {
            if (safe == nullptr)
                return;
            auto* window = new juce::AlertWindow (tr ("Rename tag"), tr ("The tag's new name, on every item that has it:"), juce::MessageBoxIconType::NoIcon);
            window->addTextEditor ("name", juce::String::fromUTF8 (tag.c_str()));
            window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
            window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
            window->enterModalState (true, juce::ModalCallbackFunction::create ([window, safe, tag] (int result) {
                                         const auto name = window->getTextEditorContents ("name").toStdString();
                                         if (result == 1 && safe != nullptr && BrowserComponent::tagStore().rename (tag, name))
                                         {
                                             auto now = safe->pickedTags();
                                             for (auto& t : now)
                                                 if (browsertags::Store::key (t) == browsertags::Store::key (tag))
                                                     t = browsertags::cleanTag (name);
                                             saveTags();
                                             safe->setPickedTags (std::move (now));
                                             safe->tagsChanged();
                                         }
                                     }),
                                     true);
        });
        menu.addItem (tr ("Delete tag"), [safe, tag] {
            if (safe == nullptr)
                return;
            BrowserComponent::tagStore().remove (tag);
            saveTags();
            auto now = safe->pickedTags();
            std::erase_if (now, [&tag] (const std::string& t) { return browsertags::Store::key (t) == browsertags::Store::key (tag); });
            safe->setPickedTags (std::move (now));
            safe->tagsChanged();
        });
        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
    }

    BrowserComponent& browser;
    std::vector<Chip> chips;
    int rowsUsed = 1, lastX = 0;
};

// --- the Clip-launch layout's sidebar (Live 12: Collections / Library / Places; live-session.png 9..108) ---------------
class BrowserComponent::LiveSidebar final : public juce::Component, public ui::HintSource
{
public:
    explicit LiveSidebar (BrowserComponent& b) : browser (b) { setRepaintsOnMouseActivity (true); }

    static constexpr int rowPitch = 19, headingHeight = 22; // Live: rows 19 px, the group titles above them

    void refresh()
    {
        std::vector<std::pair<int, std::string>> collections;
        for (const auto c : collectionStore().shown())
        {
            // Live names its first collection "Favorites" (LYNX's Pattern layout calls it Starred)
            const auto name = c == 0 && ! collectionStore().hasOwnName (0) ? tr ("Favorites") : collectionName (c);
            collections.emplace_back (c, name.toStdString());
        }
        std::vector<browserplaces::Place> places;
        for (int i = 0; i < browser.allRoots.size(); ++i)
            if (browser.roots.contains (browser.allRoots[i]))
                places.push_back ({ browser.allRootNames[i].toStdString(), browser.allRoots[i].getFullPathName().toStdString(),
                                    browser.allRootKinds[(std::size_t) i] });
        entries = browserplaces::sidebar (collections, places);
        // the rows' places: a group's title before its first row
        tops.clear();
        int y = 0;
        auto group = (browserplaces::Group) -1;
        for (const auto& e : entries)
        {
            if (e.group != group)
            {
                group = e.group;
                y += headingHeight;
            }
            tops.push_back (y);
            y += rowPitch;
        }
        setSize (getWidth(), y + 6);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        g.fillAll (colour::bg2);
        const auto mouse = isMouseOver() ? getMouseXYRelative() : juce::Point<int> (-1, -1);
        const auto clip = g.getClipBounds();
        auto group = (browserplaces::Group) -1;
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            const auto& e = entries[i];
            const auto r = juce::Rectangle<int> (0, tops[i], getWidth(), rowPitch);
            if (e.group != group)
            {
                group = e.group;
                const auto title = r.translated (0, -headingHeight).withHeight (headingHeight);
                if (title.intersects (clip))
                {
                    g.setColour (colour::textDisabled);
                    g.setFont (font (type::caption));
                    const auto name = group == browserplaces::Group::collections ? tr ("Collections")
                                      : group == browserplaces::Group::library   ? tr ("Library")
                                                                                 : tr ("Places");
                    g.drawText (name, title.withTrimmedLeft (8).withTrimmedTop (6), juce::Justification::centredLeft, false);
                }
            }
            if (! r.intersects (clip))
                continue;
            const auto selected = isSelected (e);
            if (selected)
            {
                g.setColour (juce::Colour (0xff637e86)); // Live's selected sidebar row
                g.fillRect (r);
            }
            else if (r.contains (mouse) && e.available)
            {
                g.setColour (colour::bg4);
                g.fillRect (r);
            }
            auto row = r.withTrimmedLeft (8);
            const auto iconBox = row.removeFromLeft (14).toFloat().withSizeKeepingCentre (12.0f, 12.0f);
            const auto ink = ! e.available ? colour::textDisabled : selected ? juce::Colours::white : colour::textSecondary;
            if (e.category == browserplaces::Category::collection)
            {
                g.setColour (collectionColour (e.collection));
                g.fillRect (iconBox.withSizeKeepingCentre (8.0f, 8.0f));
            }
            else
                icons::draw (g, iconOf (e.category), iconBox, ink);
            row.removeFromLeft (5);
            g.setColour (! e.available ? colour::textDisabled : selected ? juce::Colours::white : colour::textPrimary);
            g.setFont (font (type::small));
            g.drawFittedText (labelOf (e), row.withTrimmedRight (4), juce::Justification::centredLeft, 1, 0.8f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto i = entryAt (e.y);
        if (i < 0 || ! entries[(std::size_t) i].available)
            return;
        const auto entry = entries[(std::size_t) i];
        if (e.mods.isPopupMenu() && entry.category == browserplaces::Category::collection)
        {
            juce::PopupMenu menu;
            auto safe = juce::Component::SafePointer<BrowserComponent> (&browser);
            ui::addItemHeader (menu, tr ("Collection"), labelOf (entry), collectionColour (entry.collection));
            menu.addItem (tr ("Rename..."), [safe, c = entry.collection] {
                if (safe != nullptr)
                    safe->renameCollection (c);
            });
            menu.addItem (tr ("Hide this collection"), [safe, c = entry.collection] {
                if (safe != nullptr)
                    safe->setCollectionShown (c, false);
            });
            menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
            return;
        }
        browser.showLiveEntry (entry);
    }

    ui::Hint hintAt (juce::Point<int> p) override
    {
        ui::Hint h;
        const auto i = entryAt (p.y);
        if (i < 0)
        {
            h.text = tr ("Browser sidebar: Collections, Library and Places; the list beside it shows what you pick");
            return h;
        }
        const auto& e = entries[(std::size_t) i];
        if (! e.available)
            h.text = e.category == browserplaces::Category::grooves ? tr ("Grooves: the Groove Pool under the clip grid holds them")
                                                                    : tr ("{0}: LYNX SHARE has none", { labelOf (e) });
        else if (e.category == browserplaces::Category::addFolder)
            h.text = tr ("Add a folder to the Browser's places");
        else if (e.category == browserplaces::Category::folder)
            h.text = labelOf (e) + ": " + juce::String::fromUTF8 (e.path.c_str()) + "  " + tr ("[LMB] show it");
        else
            h.text = labelOf (e) + "  " + tr ("[LMB] show it");
        return h;
    }

private:
    bool isSelected (const browserplaces::Entry& e) const
    {
        const auto& s = browser.liveEntry();
        return e.category == s.category && e.collection == s.collection && e.path == s.path;
    }

    int entryAt (int y) const
    {
        for (std::size_t i = 0; i < entries.size(); ++i)
            if (y >= tops[i] && y < tops[i] + rowPitch)
                return (int) i;
        return -1;
    }

    static juce::String labelOf (const browserplaces::Entry& e)
    {
        if (e.category == browserplaces::Category::folder || e.category == browserplaces::Category::collection)
            return juce::String::fromUTF8 (e.label.c_str());
        using browserplaces::Category;
        switch (e.category)
        {
            case Category::all: return tr ("All");
            case Category::sounds: return tr ("Sounds");
            case Category::drums: return tr ("Drums");
            case Category::instruments: return tr ("Instruments");
            case Category::audioEffects: return tr ("Audio Effects");
            case Category::midiEffects: return tr ("MIDI Effects");
            case Category::modulators: return tr ("Modulators");
            case Category::maxForLive: return tr ("Max for Live");
            case Category::plugIns: return tr ("Plug-Ins");
            case Category::clips: return tr ("Clips");
            case Category::samples: return tr ("Samples");
            case Category::grooves: return tr ("Grooves");
            case Category::tunings: return tr ("Tunings");
            case Category::templates: return tr ("Templates");
            case Category::packs: return tr ("Packs");
            case Category::userLibrary: return tr ("User Library");
            case Category::currentProject: return tr ("Current Project");
            case Category::addFolder: return tr ("Add Folder...");
            default: return juce::String::fromUTF8 (e.label.c_str());
        }
    }

    // LYNX's vector icons in the places of Live's 12 px line icons
    static Icon iconOf (browserplaces::Category c)
    {
        using browserplaces::Category;
        switch (c)
        {
            case Category::all: return Icon::browser;
            case Category::sounds: return Icon::preset;
            case Category::drums: return Icon::drumBrush;
            case Category::instruments: return Icon::keyboard;
            case Category::audioEffects: return Icon::mixer;
            case Category::midiEffects: return Icon::note;
            case Category::modulators: return Icon::tweak;
            case Category::maxForLive: return Icon::plugin;
            case Category::plugIns: return Icon::plugin;
            case Category::clips: return Icon::pattern;
            case Category::samples: return Icon::audioFile;
            case Category::grooves: return Icon::metronome;
            case Category::tunings: return Icon::tuningFork;
            case Category::templates: return Icon::newFile;
            case Category::packs: return Icon::folder;
            case Category::userLibrary: return Icon::folder;
            case Category::currentProject: return Icon::project;
            case Category::folder: return Icon::folder;
            case Category::addFolder: return Icon::plus;
            case Category::collection: return Icon::none;
        }
        return Icon::none;
    }

    BrowserComponent& browser;
    std::vector<browserplaces::Entry> entries;
    std::vector<int> tops;
};
