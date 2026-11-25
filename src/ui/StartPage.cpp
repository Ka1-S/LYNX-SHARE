#include "ui/StartPage.h"

#include "ui/DialogFrame.h"
#include "ui/I18n.h"
#include "ui/Theme.h"

namespace lynx::startpageui
{
namespace
{
constexpr int pad = 20, rowHeight = 64, templateRowHeight = 50, footerHeight = 44, captionHeight = 28;

juce::String u8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

// the date column, in FL's Welcome window's words (core/StartPage.h)
juce::String whenText (std::int64_t now, std::int64_t then)
{
    const auto a = startpage::ago (now, then);
    using Kind = startpage::Ago::Kind;
    switch (a.kind)
    {
        case Kind::seconds: return tr ("seconds ago");
        case Kind::minutes: return tr ("{0}m ago", { juce::String (a.minutes) });
        case Kind::hours: return tr ("{0}h {1}m ago", { juce::String (a.hours), juce::String (a.minutes) });
        case Kind::days: return tr ("{0} days ago", { juce::String (a.days) });
        case Kind::unknown: break;
    }
    return {};
}

class CloseButton final : public juce::Button
{
public:
    CloseButton() : juce::Button ("close") { setTooltip (tr ("Close (Esc): keep the project that is open")); }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (4.0f);
        if (over || down)
        {
            g.setColour (theme::colour::bg4);
            g.fillRoundedRectangle (r, theme::radius::md);
        }
        r = r.withSizeKeepingCentre (10.0f, 10.0f);
        g.setColour (over ? theme::colour::textPrimary : theme::colour::textSecondary);
        g.drawLine (r.getX(), r.getY(), r.getRight(), r.getBottom(), 1.6f);
        g.drawLine (r.getRight(), r.getY(), r.getX(), r.getBottom(), 1.6f);
    }
};

class Card final : public juce::Component,
                   private juce::ListBoxModel,
                   private juce::KeyListener
{
public:
    explicit Card (Host h) : host (std::move (h))
    {
        setComponentID ("startpage.card");
        setWantsKeyboardFocus (true);
        now = host.nowMs > 0 ? host.nowMs : juce::Time::currentTimeMillis();

        closeButton.setComponentID ("startpage.close");
        closeButton.onClick = [this] { if (host.close) host.close(); };
        addAndMakeVisible (closeButton);

        search.setTextToShowWhenEmpty (tr ("Search recent projects"), theme::colour::textDisabled);
        search.setFont (theme::font (theme::type::body));
        search.setIndents (28, 6);
        search.setComponentID ("startpage.search");
        search.addKeyListener (this);
        search.onTextChange = [this] { refilter(); };
        search.onReturnKey = [this] { openSelected(); };
        search.onEscapeKey = [this] {
            if (search.getText().isNotEmpty())
                search.clear();
            else if (host.close)
                host.close();
        };
        addAndMakeVisible (search);

        for (auto* list : { &recentList, &templateList })
        {
            list->setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
            list->setOutlineThickness (0);
            addAndMakeVisible (*list);
        }
        recentList.setModel (this);
        recentList.setRowHeight (rowHeight);
        recentList.setComponentID ("startpage.recent");
        recentList.addKeyListener (this);
        templateModel.card = this; // (before setModel: the list asks for its rows at once)
        templateList.setModel (&templateModel);
        templateList.setRowHeight (templateRowHeight);
        templateList.setComponentID ("startpage.templates");

        atStart.setButtonText (tr ("Show the start page when LYNX starts"));
        atStart.setToggleState (host.showAtStart, juce::dontSendNotification);
        atStart.setComponentID ("startpage.atStart");
        atStart.onClick = [this] { if (host.setShowAtStart) host.setShowAtStart (atStart.getToggleState()); };
        addAndMakeVisible (atStart);

        refilter();
    }

    // --- shots / tests (rows: the recent projects shown, "Other projects..." not counted)
    void type (const juce::String& t)
    {
        search.setText (t, juce::dontSendNotification); // (a TextEditor posts its change: filtered here at once)
        refilter();
    }
    int shownRows() const noexcept { return (int) shown.size(); }
    void selectRow (int r) { recentList.selectRow (r + firstRecentRow()); }
    bool openSelectedNow() { return openSelected(); }
    juce::TextEditor& searchBox() noexcept { return search; }

    void paint (juce::Graphics& g) override
    {
        const auto card = getLocalBounds();
        ui::dialog::paintFrame (g, card.toFloat(), ui::dialog::Kind::overlay, true);
        ui::dialog::paintTitle (g, card, tr ("Start page"));
        // FL's two panels (its Welcome window: "Start a new project" | "Recent projects"), each a darker well with its
        // caption tab
        for (const auto& [panel, caption] : { std::pair { leftPanel, tr ("Start a new project") }, std::pair { rightPanel, tr ("Recent projects") } })
        {
            g.setColour (theme::colour::bg1);
            g.fillRoundedRectangle (panel.toFloat(), theme::radius::md);
            g.setColour (theme::colour::border);
            g.drawRoundedRectangle (panel.toFloat().reduced (0.5f), theme::radius::md, 1.0f);
            g.setFont (theme::font (theme::type::small, true));
            g.setColour (theme::colour::textSecondary);
            g.drawText (caption, panel.withHeight (captionHeight).reduced (12, 0), juce::Justification::centredLeft);
        }
        g.setFont (theme::font (theme::type::small));
        g.setColour (theme::colour::textDisabled);
        const auto count = search.getText().trim().isEmpty() ? juce::String ((int) host.recent.size())
                                                             : tr ("{0} of {1}", { juce::String ((int) shown.size()), juce::String ((int) host.recent.size()) });
        g.drawText (count, rightPanel.withHeight (captionHeight).reduced (12, 0), juce::Justification::centredRight);
        if (shown.empty() && search.getText().trim().isNotEmpty())
        {
            g.setFont (theme::font (theme::type::body));
            g.drawFittedText (tr ("No recent project matches the search."), recentList.getBounds().withHeight (60).reduced (8, 0), juce::Justification::centred, 2);
        }
        else if (host.recent.empty())
        {
            g.setFont (theme::font (theme::type::body));
            g.drawFittedText (tr ("No recent projects yet: the projects you open or save are listed here."),
                              recentList.getBounds().withTrimmedTop (rowHeight + 8).withHeight (60).reduced (8, 0), juce::Justification::centred, 2);
        }
        g.setColour (theme::colour::border);
        g.fillRect (1, getHeight() - footerHeight, getWidth() - 2, 1);
        g.setFont (theme::font (theme::type::small));
        g.setColour (theme::colour::textDisabled);
        g.drawText (tr ("Enter: open  ·  Esc: keep the current project"), footerArea.withTrimmedLeft (atStart.getRight() - footerArea.getX() + 12),
                    juce::Justification::centredRight);
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        // the magnifier in the search box (LYNX vector)
        const auto b = search.getBounds().toFloat();
        const auto c = juce::Point<float> (b.getX() + 14.0f, b.getCentreY() - 1.0f);
        g.setColour (theme::colour::textSecondary);
        g.drawEllipse (c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f, 1.5f);
        g.drawLine (c.x + 3.6f, c.y + 3.6f, c.x + 7.0f, c.y + 7.0f, 1.8f);
    }

    void resized() override
    {
        const auto w = getWidth();
        closeButton.setBounds (w - 14 - 30, ui::dialog::accentStrip + 7, 30, 30);
        auto body = getLocalBounds().withTrimmedTop (ui::dialog::titleRowHeight + 1).withTrimmedBottom (footerHeight).reduced (pad, pad - 6);
        leftPanel = body.removeFromLeft ((body.getWidth() - pad) / 2);
        body.removeFromLeft (pad);
        rightPanel = body;
        templateList.setBounds (leftPanel.withTrimmedTop (captionHeight).reduced (6, 4));
        auto right = rightPanel.withTrimmedTop (captionHeight).reduced (6, 4);
        search.setBounds (right.removeFromTop (30));
        right.removeFromTop (6);
        recentList.setBounds (right);
        footerArea = getLocalBounds().removeFromBottom (footerHeight).reduced (pad, 0);
        const auto toggleWidth = juce::jmin (360, footerArea.getWidth() / 2);
        atStart.setBounds (footerArea.withWidth (toggleWidth).withSizeKeepingCentre (toggleWidth, 26).withX (footerArea.getX()));
    }

    void visibilityChanged() override
    {
        if (isShowing())
            focusSearch();
    }
    void parentHierarchyChanged() override
    {
        if (isShowing())
            focusSearch();
    }
    void focusSearch()
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Card> (this)] {
            if (safe != nullptr && safe->isShowing())
                safe->search.grabKeyboardFocus();
        });
    }

    bool keyPressed (const juce::KeyPress& key) override { return handleKey (key); }

private:
    // FL's first row "Other projects..." (only without a search), then the recent projects
    int firstRecentRow() const noexcept { return search.getText().trim().isEmpty() ? 1 : 0; }

    static void paintRowBack (juce::Graphics& g, juce::Rectangle<int> area, bool selected)
    {
        g.setColour (selected ? theme::colour::bg3 : theme::colour::bg2);
        g.fillRoundedRectangle (area.toFloat(), theme::radius::md);
        if (selected)
        {
            g.setColour (theme::colour::accent);
            g.fillRoundedRectangle (area.toFloat().withWidth (3.0f), 1.5f);
        }
    }

    // --- the recent list (ListBoxModel)
    int getNumRows() override { return (int) shown.size() + firstRecentRow(); }

    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        auto area = juce::Rectangle<int> (0, 0, width, height).reduced (0, 3);
        paintRowBack (g, area, selected);
        area = area.reduced (12, 6);
        const auto titleFont = theme::font (theme::type::body, true), lineFont = theme::font (theme::type::small);
        if (row < firstRecentRow())
        {
            g.setFont (titleFont);
            g.setColour (theme::colour::accent);
            g.drawText (tr ("Other projects..."), area.removeFromTop (20), juce::Justification::centredLeft, true);
            g.setFont (lineFont);
            g.setColour (theme::colour::textSecondary);
            g.drawText (tr ("Choose another project to open"), area.removeFromTop (16), juce::Justification::centredLeft, true);
            return;
        }
        const auto index = row - firstRecentRow();
        if (index < 0 || index >= (int) shown.size())
            return;
        const auto& r = host.recent[(std::size_t) shown[(std::size_t) index]];
        g.setFont (titleFont);
        g.setColour (r.exists ? theme::colour::accent : theme::colour::textDisabled);
        g.drawText (u8 (r.name), area.removeFromTop (20), juce::Justification::centredLeft, true);
        g.setFont (lineFont);
        g.setColour (r.exists ? theme::colour::textSecondary : theme::colour::textDisabled);
        // FL's line: "<size>  <date time>  (<ago>)"
        const auto line = r.exists ? u8 (startpage::sizeText (r.bytes)) + "   " + juce::Time (r.modifiedMs).formatted ("%Y-%m-%d %H:%M:%S") + "   ("
                                         + whenText (now, r.modifiedMs) + ")"
                                   : tr ("Missing");
        g.drawText (line, area.removeFromTop (16), juce::Justification::centredLeft, true);
        g.setColour (theme::colour::textDisabled);
        g.drawText (u8 (r.folder), area.removeFromTop (16), juce::Justification::centredLeft, true);
    }

    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
    {
        recentList.selectRow (row);
        openSelected();
    }
    void returnKeyPressed (int) override { openSelected(); }
    juce::String getTooltipForRow (int row) override
    {
        const auto index = row - firstRecentRow();
        return index >= 0 && index < (int) shown.size() ? u8 (host.recent[(std::size_t) shown[(std::size_t) index]].path) : juce::String();
    }

    // --- the templates ("Start a new project")
    struct TemplateModel final : public juce::ListBoxModel
    {
        Card* card = nullptr;
        int getNumRows() override { return 1 + (int) card->host.templates.size(); }
        void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
        {
            auto area = juce::Rectangle<int> (0, 0, width, height).reduced (0, 3);
            paintRowBack (g, area, selected);
            area = area.reduced (12, 6);
            // (JUCE may ask for a row past getNumRows(): nothing to draw there)
            if (row < 0 || row >= getNumRows())
                return;
            const auto& details = card->host.templateDetails;
            const auto name = row == 0 ? tr ("New project...") : card->host.templates[(std::size_t) row - 1];
            const auto detail = row == 0 ? tr ("Choose a template, the tempo and where to save")
                                         : (row - 1 < (int) details.size() ? details[(std::size_t) row - 1] : juce::String());
            g.setFont (theme::font (theme::type::body, true));
            g.setColour (theme::colour::accent);
            g.drawText (name, area.removeFromTop (20), juce::Justification::centredLeft, true);
            g.setFont (theme::font (theme::type::small));
            g.setColour (theme::colour::textSecondary);
            g.drawText (detail, area.removeFromTop (16), juce::Justification::centredLeft, true);
        }
        void listBoxItemClicked (int row, const juce::MouseEvent&) override
        {
            if (row == 0)
            {
                if (card->host.newProject)
                    card->host.newProject();
            }
            else if (row >= 1 && row - 1 < (int) card->host.templates.size() && card->host.newFromTemplate)
                card->host.newFromTemplate (row - 1);
        }
        juce::String getTooltipForRow (int row) override
        {
            return row >= 1 && row < getNumRows() ? tr ("Start a new project from this template") : juce::String();
        }
    };

    bool keyPressed (const juce::KeyPress& key, juce::Component*) override { return handleKey (key); }

    bool handleKey (const juce::KeyPress& key)
    {
        const auto code = key.getKeyCode();
        int delta = 0;
        if (code == juce::KeyPress::upKey)
            delta = -1;
        else if (code == juce::KeyPress::downKey)
            delta = 1;
        else if (code == juce::KeyPress::pageUpKey)
            delta = -6;
        else if (code == juce::KeyPress::pageDownKey)
            delta = 6;
        if (delta != 0)
        {
            const auto row = startpage::moveRow (recentList.getSelectedRow(), delta, getNumRows());
            if (row >= 0)
            {
                recentList.selectRow (row);
                recentList.scrollToEnsureRowIsOnscreen (row);
            }
            return true;
        }
        if (key == juce::KeyPress::returnKey)
            return openSelected();
        if (key == juce::KeyPress::escapeKey)
        {
            if (search.getText().isNotEmpty())
                search.clear();
            else if (host.close)
                host.close();
            return true;
        }
        return false;
    }

    void refilter()
    {
        shown = startpage::filter (host.recent, search.getText().toStdString()); // (core lowers it, keeping OR / AND / NOT)
        recentList.updateContent();
        // the first recent project (FL selects the row clicked; Enter opens the selection)
        recentList.selectRow (shown.empty() ? (firstRecentRow() > 0 ? 0 : -1) : firstRecentRow());
        repaint();
    }

    bool openSelected()
    {
        const auto row = recentList.getSelectedRow();
        if (row >= 0 && row < firstRecentRow())
        {
            if (host.openDialog)
                host.openDialog();
            return true;
        }
        const auto index = row - firstRecentRow();
        if (index < 0 || index >= (int) shown.size())
            return false;
        const auto& r = host.recent[(std::size_t) shown[(std::size_t) index]];
        if (host.open)
            host.open (u8 (r.path));
        return true;
    }

    Host host;
    std::int64_t now = 0;
    std::vector<int> shown;
    TemplateModel templateModel; // (before the lists: they go first, their model after them)
    CloseButton closeButton;
    juce::TextEditor search;
    juce::ListBox recentList, templateList;
    juce::ToggleButton atStart;
    juce::Rectangle<int> leftPanel, rightPanel, footerArea;
};

class Overlay final : public juce::Component
{
public:
    explicit Overlay (Host host) : card (std::move (host))
    {
        setComponentID ("startpage.overlay");
        setWantsKeyboardFocus (true);
        addAndMakeVisible (card);
    }

    void paint (juce::Graphics& g) override { ui::dialog::paintDim (g, getLocalBounds()); }

    void resized() override
    {
        const auto area = getLocalBounds().reduced (16);
        card.setBounds (area.withSizeKeepingCentre (juce::jmin (cardWidth, area.getWidth()), juce::jmin (cardHeight, area.getHeight())));
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        card.keyPressed (key);
        return true; // (the window's keys stay with the page while it shows)
    }
    bool keyStateChanged (bool) override { return true; }

    Card card;
};

Card* cardOf (juce::Component& c)
{
    if (auto* card = dynamic_cast<Card*> (&c))
        return card;
    if (auto* overlay = dynamic_cast<Overlay*> (&c))
        return &overlay->card;
    return nullptr;
}
} // namespace

std::unique_ptr<juce::Component> makeOverlay (Host host) { return std::make_unique<Overlay> (std::move (host)); }

std::unique_ptr<juce::Component> makeCard (Host host)
{
    auto card = std::make_unique<Card> (std::move (host));
    card->setSize (cardWidth, cardHeight);
    return card;
}

void typeForSnapshot (juce::Component& c, const juce::String& text)
{
    if (auto* card = cardOf (c))
        card->type (text);
}

int shownRowsForSnapshot (juce::Component& c)
{
    auto* card = cardOf (c);
    return card != nullptr ? card->shownRows() : -1;
}

void selectRowForSnapshot (juce::Component& c, int row)
{
    if (auto* card = cardOf (c))
        card->selectRow (row);
}

bool openSelectedForSnapshot (juce::Component& c)
{
    auto* card = cardOf (c);
    return card != nullptr && card->openSelectedNow();
}

juce::TextEditor* searchBoxForSnapshot (juce::Component& c)
{
    auto* card = cardOf (c);
    return card != nullptr ? &card->searchBox() : nullptr;
}
} // namespace lynx::startpageui
