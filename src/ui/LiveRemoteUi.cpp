#include "ui/LiveRemoteUi.h"

#include <cmath>
#include <map>
#include <memory>

#include "engine/AudioEngine.h"
#include "engine/LiveSessions.h"
#include "ui/AutomationUi.h"
#include "ui/I18n.h"
#include "ui/ShotScale.h"
#include "ui/Theme.h"
#include "ui/ValueControl.h"
#include "ui/WindowOwner.h"

namespace lynx::liveui
{
using namespace theme;

namespace
{
using HostedState = LiveSessionManager::HostedState;

juce::String str (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

LiveSessionManager* liveSession = nullptr;
AudioEngine* sessionEngine = nullptr;

juce::String hostNameOf (const LiveSessionManager& live)
{
    for (const auto& p : live.getPeople())
        if (p.host)
            return p.name;
    return tr ("the host");
}

// wrapped text in a box (the notices)
void drawWrapped (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area, juce::Colour colour, float size, bool bold = false)
{
    juce::AttributedString s;
    s.setWordWrap (juce::AttributedString::byWord);
    s.append (text, juce::Font (font (size, bold)), colour);
    juce::TextLayout layout;
    layout.createLayout (s, (float) area.getWidth());
    layout.draw (g, area.toFloat());
}

int wrappedHeight (const juce::String& text, int width, float size, bool bold = false)
{
    juce::AttributedString s;
    s.setWordWrap (juce::AttributedString::byWord);
    s.append (text, juce::Font (font (size, bold)), juce::Colours::white);
    juce::TextLayout layout;
    layout.createLayout (s, (float) juce::jmax (40, width));
    return (int) std::ceil (layout.getHeight());
}

void styleSearch (juce::TextEditor& e, const juce::String& placeholder)
{
    e.setTextToShowWhenEmpty (placeholder, colour::textDisabled);
    e.setColour (juce::TextEditor::backgroundColourId, colour::bg1);
    e.setColour (juce::TextEditor::outlineColourId, colour::borderStrong);
    e.setColour (juce::TextEditor::focusedOutlineColourId, colour::accent);
    e.setColour (juce::TextEditor::textColourId, colour::textPrimary);
}

bool matches (const juce::String& text, const juce::String& query)
{
    for (const auto& word : juce::StringArray::fromTokens (query, " ", ""))
        if (word.isNotEmpty() && ! text.containsIgnoreCase (word))
            return false;
    return true;
}

// --- the host: Share plugins with guests ----------------------------------------------------------------------------

struct ShareRow
{
    std::string id;
    juce::String name, detail, usedBy;
    bool on = false;
};

struct ShareModel
{
    std::function<std::vector<ShareRow>()> rows;
    std::function<juce::String (const std::string& id, bool on)> set; // the error, if any
};

class SharePanel final : public juce::Component, private juce::ListBoxModel, private juce::Timer
{
public:
    explicit SharePanel (ShareModel m) : model (std::move (m))
    {
        addAndMakeVisible (search);
        styleSearch (search, tr ("Search your plugins"));
        search.onTextChange = [this] { refresh (true); };
        addAndMakeVisible (list);
        list.setModel (this);
        list.setRowHeight (34);
        list.setColour (juce::ListBox::backgroundColourId, colour::bg0);
        list.setColour (juce::ListBox::outlineColourId, colour::border);
        list.setOutlineThickness (1);
        setSize (560, 560);
        refresh (true);
        startTimer (500);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colour::bg2);
        auto r = getLocalBounds().reduced (space::lg);
        // the licence notice: always in view, above the list
        auto box = r.removeFromTop (noticeHeight());
        g.setColour (colour::bg3);
        g.fillRoundedRectangle (box.toFloat(), radius::md);
        g.setColour (colour::warning);
        g.fillRoundedRectangle (box.removeFromLeft (4).toFloat(), 2.0f);
        box = box.reduced (space::lg, space::md);
        g.setColour (colour::textPrimary);
        g.setFont (font (type::title, true));
        g.drawText (title(), box.removeFromTop (22), juce::Justification::centredLeft, true);
        box.removeFromTop (space::sm);
        const auto h1 = wrappedHeight (what(), box.getWidth(), type::body);
        drawWrapped (g, what(), box.removeFromTop (h1), colour::textSecondary, type::body);
        box.removeFromTop (space::md);
        const auto h2 = wrappedHeight (licence(), box.getWidth(), type::body, true);
        drawWrapped (g, licence(), box.removeFromTop (h2), colour::textPrimary, type::body, true);
        box.removeFromTop (space::md);
        drawWrapped (g, when(), box, colour::textSecondary, type::body);
        // the count under the list
        auto foot = getLocalBounds().reduced (space::lg).removeFromBottom (22);
        int on = 0;
        for (const auto& row : all)
            on += row.on ? 1 : 0;
        g.setColour (on > 0 ? colour::textPrimary : colour::textDisabled);
        g.setFont (font (type::small));
        g.drawText (on == 0 ? tr ("Nothing is shared.") : tr ("{0} shared with the session", { juce::String (on) }), foot, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (space::lg);
        r.removeFromTop (noticeHeight() + space::lg);
        r.removeFromBottom (22 + space::sm);
        search.setBounds (r.removeFromTop (26));
        r.removeFromTop (space::md);
        list.setBounds (r);
    }

private:
    ShareModel model;
    juce::TextEditor search;
    juce::ListBox list;
    std::vector<ShareRow> all, shown;

    static juce::String title() { return tr ("Share plugins with your guests"); }
    static juce::String what()
    {
        return tr ("Guests can put the plugins you tick on their own channels and inserts. The plugin stays on this computer and runs "
                   "under your licence: guests send notes and knob values, and hear the result as audio for this song. LYNX SHARE never "
                   "sends the plugin, its files, its presets or its licence.");
    }
    static juce::String licence()
    {
        return tr ("Some plugin licences don't allow other people to operate the plugin, even remotely. Check the licence of each plugin "
                   "before you share it: you are responsible for how your plugins are used.");
    }
    static juce::String when() { return tr ("Nothing is shared until you tick it, and sharing stops when the session ends."); }

    int noticeHeight() const
    {
        const auto w = getWidth() - 2 * space::lg - 4 - 2 * space::lg;
        return 2 * space::md + 22 + space::sm + wrappedHeight (what(), w, type::body) + space::md + wrappedHeight (licence(), w, type::body, true) + space::md
               + wrappedHeight (when(), w, type::body);
    }

    void timerCallback() override { refresh (false); }

    void refresh (bool force)
    {
        auto rows = model.rows();
        bool same = ! force && rows.size() == all.size();
        for (std::size_t i = 0; same && i < rows.size(); ++i)
            same = rows[i].id == all[i].id && rows[i].on == all[i].on && rows[i].usedBy == all[i].usedBy;
        if (same)
            return;
        all = std::move (rows);
        shown.clear();
        const auto query = search.getText().trim();
        for (const auto& row : all)
            if (matches (row.name + " " + row.detail, query))
                shown.push_back (row);
        list.updateContent();
        list.repaint();
        repaint();
    }

    int getNumRows() override { return (int) shown.size(); }

    void paintListBoxItem (int index, juce::Graphics& g, int width, int height, bool) override
    {
        if (index < 0 || index >= (int) shown.size())
            return;
        const auto& row = shown[(std::size_t) index];
        auto r = juce::Rectangle<int> (0, 0, width, height);
        if (row.on)
        {
            g.setColour (colour::accentMuted.withAlpha (0.45f));
            g.fillRect (r);
        }
        g.setColour (colour::border);
        g.fillRect (r.removeFromBottom (1));
        r = r.reduced (space::md, 0);
        // the tick box
        const auto box = r.removeFromLeft (26).withSizeKeepingCentre (16, 16).toFloat();
        g.setColour (row.on ? colour::accent : colour::bg4);
        g.fillRoundedRectangle (box, radius::sm);
        if (! row.on)
        {
            g.setColour (colour::borderStrong);
            g.drawRoundedRectangle (box, radius::sm, 1.0f);
        }
        else
        {
            juce::Path tick;
            tick.startNewSubPath (box.getX() + 3.5f, box.getCentreY());
            tick.lineTo (box.getX() + 6.5f, box.getBottom() - 4.5f);
            tick.lineTo (box.getRight() - 3.5f, box.getY() + 4.5f);
            g.setColour (colour::onAccent);
            g.strokePath (tick, juce::PathStrokeType (1.8f));
        }
        r.removeFromLeft (space::sm);
        if (row.usedBy.isNotEmpty())
        {
            const auto f = juce::Font (font (type::small));
            const auto w = juce::jmin (r.getWidth() / 2, juce::GlyphArrangement::getStringWidthInt (f, row.usedBy) + 8);
            g.setColour (colour::info);
            g.setFont (f);
            g.drawText (row.usedBy, r.removeFromRight (w), juce::Justification::centredRight, true);
        }
        const auto nameFont = juce::Font (font (type::body, row.on));
        const auto nameWidth = juce::jmin (r.getWidth(), juce::GlyphArrangement::getStringWidthInt (nameFont, row.name) + 10);
        g.setColour (colour::textPrimary);
        g.setFont (nameFont);
        g.drawText (row.name, r.removeFromLeft (nameWidth), juce::Justification::centredLeft, true);
        g.setColour (colour::textSecondary);
        g.setFont (font (type::small));
        g.drawText (row.detail, r, juce::Justification::centredLeft, true);
    }

    void listBoxItemClicked (int index, const juce::MouseEvent&) override { toggle (index); }
    void returnKeyPressed (int index) override { toggle (index); }

    void toggle (int index)
    {
        if (index < 0 || index >= (int) shown.size())
            return;
        const auto row = shown[(std::size_t) index];
        const auto error = model.set (row.id, ! row.on);
        if (error.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Share plugins with guests"),
                                                    tr ("{0} could not be shared: {1}", { row.name, error }));
        refresh (true);
    }
};

// --- a guest: the parameter editor of a plugin the host plays --------------------------------------------------------

struct HostedModel
{
    juce::String hostName, title;
    remoteplug::Shared plugin;                                              // as the host described it (kept if un-shared)
    std::function<PluginRef()> ref;                                         // the reference now (its values)
    std::function<void (const std::vector<std::pair<int, float>>&)> write;  // values into the song
    std::function<HostedState()> state;
    std::function<void (juce::Slider&, int paramIndex)> menu;               // right-click (automation); may be empty
};

juce::String valueText (const remoteplug::Param& p, float value)
{
    if (p.steps >= 2)
    {
        const auto step = juce::jlimit (0, p.steps - 1, (int) std::lround (value * (float) (p.steps - 1)));
        if ((int) p.stepNames.size() == p.steps)
            return str (p.stepNames[(std::size_t) step]);
        return juce::String (step + 1) + " / " + juce::String (p.steps);
    }
    return juce::String ((int) std::lround (value * 100.0f)) + "%";
}

class ParamRow final : public juce::Component
{
public:
    ParamRow()
    {
        addAndMakeVisible (slider);
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.setColour (juce::Slider::trackColourId, colour::accent);
        slider.setColour (juce::Slider::backgroundColourId, colour::bg4);
        slider.setColour (juce::Slider::thumbColourId, colour::textPrimary);
        slider.onValueChange = [this] {
            if (onMoved && ! setting)
                onMoved (param.index, (float) slider.getValue());
            repaint();
        };
        slider.addMouseListener (this, false);
        ui::setOwnMenu (slider);
    }

    void set (const remoteplug::Param& p, float value, bool usable, bool odd)
    {
        const bool other = p.index != param.index || p.name != param.name;
        param = p;
        stripe = odd;
        if (other)
        {
            setting = true;
            slider.setRange (0.0, 1.0, p.steps >= 2 ? 1.0 / (double) (p.steps - 1) : 0.0);
            slider.setDoubleClickReturnValue (true, (double) p.def);
            slider.setName (str (p.name));
            slider.setTooltip (p.low.empty() && p.high.empty() ? juce::String() : str (p.low) + juce::String::fromUTF8 (" \xe2\x80\x93 ") + str (p.high));
            setting = false;
        }
        if (! slider.isMouseButtonDown() && std::abs ((float) slider.getValue() - value) > 1.0e-6f)
        {
            setting = true;
            slider.setValue ((double) value, juce::dontSendNotification);
            setting = false;
        }
        slider.setEnabled (usable);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        if (stripe)
            g.fillAll (colour::bg1.withAlpha (0.55f));
        auto r = getLocalBounds().reduced (space::md, 0);
        const bool changed = std::abs ((float) slider.getValue() - param.def) > 1.0e-4f;
        g.setColour (slider.isEnabled() ? colour::textPrimary : colour::textDisabled);
        g.setFont (font (type::body));
        g.drawText (str (param.name), r.removeFromLeft (nameWidth()), juce::Justification::centredLeft, true);
        g.setColour (! slider.isEnabled() ? colour::textDisabled : changed ? colour::textPrimary : colour::textSecondary);
        g.setFont (font (type::small, changed));
        g.drawText (valueText (param, (float) slider.getValue()), r.removeFromRight (valueWidth), juce::Justification::centredRight, true);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (space::md, 0);
        r.removeFromLeft (nameWidth() + space::md);
        r.removeFromRight (valueWidth + space::md);
        slider.setBounds (r.reduced (0, 4));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onMenu && slider.isEnabled())
            onMenu (slider, param.index);
    }

    std::function<void (int, float)> onMoved;
    std::function<void (juce::Slider&, int)> onMenu;

private:
    static constexpr int valueWidth = 96;
    remoteplug::Param param;
    juce::Slider slider;
    bool setting = false, stripe = false;
    int nameWidth() const { return juce::jlimit (110, 220, getWidth() * 34 / 100); }
};

class HostedPanel final : public juce::Component, private juce::ListBoxModel, private juce::Timer
{
public:
    explicit HostedPanel (HostedModel m) : model (std::move (m))
    {
        addAndMakeVisible (search);
        styleSearch (search, tr ("Search parameters"));
        search.onTextChange = [this] { filter(); };
        addAndMakeVisible (list);
        list.setModel (this);
        list.setRowHeight (30);
        list.setColour (juce::ListBox::backgroundColourId, colour::bg0);
        list.setColour (juce::ListBox::outlineColourId, colour::border);
        list.setOutlineThickness (1);
        current = model.ref();
        shownState = model.state();
        filter();
        setSize (520, 560);
        startTimer (100);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colour::bg2);
        auto r = getLocalBounds().reduced (space::lg);
        auto head = r.removeFromTop (headHeight);
        // who plays it: the badge the whole feature hangs on
        const auto badge = tr ("Played by {0}", { model.hostName });
        const auto badgeFont = juce::Font (font (type::small, true));
        const auto bw = juce::GlyphArrangement::getStringWidthInt (badgeFont, badge) + 16;
        auto top = head.removeFromTop (26);
        auto pill = top.removeFromRight (bw).withSizeKeepingCentre (bw, 20).toFloat();
        g.setColour (colour::accent);
        g.fillRoundedRectangle (pill, 10.0f);
        g.setColour (colour::onAccent);
        g.setFont (badgeFont);
        g.drawText (badge, pill, juce::Justification::centred, false);
        g.setColour (colour::textPrimary);
        g.setFont (font (type::display, true));
        g.drawText (str (model.plugin.name), top.withTrimmedRight (space::md), juce::Justification::centredLeft, true);
        // vendor, and the state of the render
        head.removeFromTop (2);
        auto line = head.removeFromTop (20);
        const auto [text, dot] = stateText (shownState);
        const auto stateFont = juce::Font (font (type::small, shownState == HostedState::updating));
        const auto sw = juce::GlyphArrangement::getStringWidthInt (stateFont, text) + 18;
        auto stateArea = line.removeFromRight (juce::jmin (sw, line.getWidth() * 3 / 4));
        g.setColour (dot);
        g.fillEllipse (stateArea.removeFromLeft (14).toFloat().withSizeKeepingCentre (8.0f, 8.0f));
        g.setColour (shownState == HostedState::upToDate ? colour::textSecondary : colour::textPrimary);
        g.setFont (stateFont);
        g.drawText (text, stateArea, juce::Justification::centredLeft, true);
        g.setColour (colour::textSecondary);
        g.setFont (font (type::small));
        g.drawText (str (model.plugin.vendor) + (model.plugin.vendor.empty() ? "" : juce::String::fromUTF8 (" \xc2\xb7 ")) + str (model.plugin.format)
                        + juce::String::fromUTF8 (" \xc2\xb7 ") + tr ("{0} parameters", { juce::String ((int) model.plugin.params.size()) }),
                    line, juce::Justification::centredLeft, true);
        // what this is, under the list
        auto foot = getLocalBounds().reduced (space::lg).removeFromBottom (footHeight());
        g.setColour (colour::border);
        g.fillRect (foot.removeFromTop (1));
        foot.removeFromTop (space::md);
        drawWrapped (g, footText(), foot, colour::textSecondary, type::small);
        if (shown.empty())
        {
            g.setColour (colour::textDisabled);
            g.setFont (font (type::body));
            g.drawText (model.plugin.params.empty() ? tr ("This plugin has no parameters to change.") : tr ("No parameter matches."), list.getBounds(),
                        juce::Justification::centred, true);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (space::lg);
        r.removeFromTop (headHeight + space::md);
        r.removeFromBottom (footHeight() + space::md);
        search.setBounds (r.removeFromTop (26));
        r.removeFromTop (space::md);
        list.setBounds (r);
    }

private:
    static constexpr int headHeight = 50;
    HostedModel model;
    juce::TextEditor search;
    juce::ListBox list;
    std::vector<int> shown; // indices into model.plugin.params
    PluginRef current;
    HostedState shownState = HostedState::none;
    remoteplug::Outbox outbox;

    juce::String footText() const
    {
        return tr ("You hear a render made on {0}'s computer: it follows your notes and these values after a moment, it is not played live.", { model.hostName })
               + " "
               + tr ("These sounds come from {0}'s plugin under {0}'s licence. Use them for this song; don't redistribute the audio as samples.", { model.hostName });
    }
    int footHeight() const { return 1 + space::md + wrappedHeight (footText(), getWidth() - 2 * space::lg, type::small); }

    std::pair<juce::String, juce::Colour> stateText (HostedState s) const
    {
        switch (s)
        {
            case HostedState::upToDate: return { tr ("Up to date"), colour::ledOn };
            case HostedState::updating: return { tr ("Updating... ({0} is rendering)", { model.hostName }), colour::warning };
            case HostedState::noNotes: return { tr ("Add notes to hear it"), colour::textSecondary };
            case HostedState::hostAway: return { tr ("{0} is away: the last render plays", { model.hostName }), colour::warning };
            case HostedState::notShared: return { tr ("No longer shared: the last render plays"), colour::textDisabled };
            case HostedState::none: break;
        }
        return { tr ("The session ended: the last render stays"), colour::textDisabled };
    }

    bool usable() const { return shownState == HostedState::upToDate || shownState == HostedState::updating || shownState == HostedState::noNotes; }

    void filter()
    {
        shown.clear();
        const auto query = search.getText().trim();
        for (int i = 0; i < (int) model.plugin.params.size(); ++i)
            if (matches (str (model.plugin.params[(std::size_t) i].name), query))
                shown.push_back (i);
        list.updateContent();
        repaint();
    }

    void timerCallback() override
    {
        current = model.ref();
        const auto due = outbox.due (current, juce::Time::getMillisecondCounterHiRes());
        if (! due.empty() && usable())
        {
            model.write (due);
            current = model.ref();
        }
        if (const auto s = model.state(); s != shownState)
        {
            shownState = s;
            repaint();
        }
        for (int row = 0; row < (int) shown.size(); ++row)
            if (auto* c = dynamic_cast<ParamRow*> (list.getComponentForRowNumber (row)))
                fill (*c, row);
    }

    float valueFor (const remoteplug::Param& p) const
    {
        if (const auto mine = outbox.showing (p.index))
            return *mine; // (on its way to the host)
        return remoteplug::valueOf (current, p.index, p.def);
    }

    void fill (ParamRow& row, int index)
    {
        const auto& p = model.plugin.params[(std::size_t) shown[(std::size_t) index]];
        row.set (p, valueFor (p), usable(), index % 2 == 1);
    }

    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}

    juce::Component* refreshComponentForRow (int row, bool, juce::Component* existing) override
    {
        auto* c = dynamic_cast<ParamRow*> (existing);
        if (row < 0 || row >= (int) shown.size())
        {
            delete existing;
            return nullptr;
        }
        if (c == nullptr)
        {
            delete existing;
            c = new ParamRow();
            c->onMoved = [this] (int index, float value) { outbox.set (index, value, juce::Time::getMillisecondCounterHiRes()); };
            c->onMenu = [this] (juce::Slider& s, int index) {
                if (model.menu)
                    model.menu (s, index);
            };
        }
        fill (*c, row);
        return c;
    }
};

class ToolWindow final : public juce::DocumentWindow
{
public:
    ToolWindow (const juce::String& title, juce::Component* content, std::function<void()> closed)
        : juce::DocumentWindow (title, colour::bg2, DocumentWindow::closeButton), onClosed (std::move (closed))
    {
        setUsingNativeTitleBar (true);
        setContentOwned (content, true);
        setResizable (true, false);
        setResizeLimits (420, 380, 1100, 1600);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        keepAboveMainWindow (*this);
    }
    void closeButtonPressed() override
    {
        // (deleted by its owner, not from inside its own callback)
        juce::MessageManager::callAsync ([closed = onClosed] {
            if (closed)
                closed();
        });
    }

private:
    std::function<void()> onClosed;
};

std::unique_ptr<ToolWindow> shareWindow;
std::map<remoteplug::Place, std::unique_ptr<ToolWindow>> editors;

// closes the windows of a session that is over (polled: the manager has no "ended" list of listeners)
struct Watcher final : private juce::Timer
{
    Watcher() { startTimer (1000); }
    void timerCallback() override
    {
        if (liveSession == nullptr || ! liveSession->isActive())
            closeRemoteWindows();
        else if (! liveSession->isHost())
            shareWindow.reset();
    }
};
std::unique_ptr<Watcher> watcher;

ShareModel shareModelFor (LiveSessionManager& live, AudioEngine& engine)
{
    ShareModel m;
    m.rows = [&live, &engine] {
        std::vector<ShareRow> rows;
        auto types = engine.getPluginHost().getKnownPlugins().getTypes();
        std::sort (types.begin(), types.end(), [] (const auto& a, const auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });
        for (const auto& type : types)
        {
            ShareRow row;
            row.id = type.createIdentifierString().toStdString();
            row.name = type.name;
            row.detail = (type.manufacturerName.isNotEmpty() ? type.manufacturerName + juce::String::fromUTF8 (" \xc2\xb7 ") : juce::String()) + type.pluginFormatName
                         + juce::String::fromUTF8 (" \xc2\xb7 ") + (type.isInstrument ? tr ("Instrument") : tr ("Effect"));
            row.on = remoteplug::find (live.sharedPlugins(), row.id) != nullptr;
            if (row.on)
                if (const auto users = live.sharedPluginUsers (row.id); ! users.isEmpty())
                    row.usedBy = tr ("Used by {0}", { users.joinIntoString (", ") });
            rows.push_back (std::move (row));
        }
        return rows;
    };
    m.set = [&live] (const std::string& id, bool on) {
        juce::String error;
        return live.sharePlugin (id, on, &error) ? juce::String() : (error.isNotEmpty() ? error : tr ("the plugin did not load"));
    };
    return m;
}
} // namespace

void setRemoteSession (LiveSessionManager* live, AudioEngine* engine)
{
    liveSession = live;
    sessionEngine = engine;
    if (live == nullptr)
    {
        closeRemoteWindows();
        watcher.reset();
    }
}

void showSharePlugins (LiveSessionManager& live, AudioEngine& engine)
{
    if (! live.isHost() || ! live.isActive())
        return;
    if (shareWindow == nullptr)
        shareWindow = std::make_unique<ToolWindow> (tr ("Share plugins with guests"), new SharePanel (shareModelFor (live, engine)), [] { shareWindow.reset(); });
    shareWindow->setVisible (true);
    shareWindow->toFront (true);
    if (watcher == nullptr)
        watcher = std::make_unique<Watcher>();
}

void addSharedPluginItems (juce::PopupMenu& menu, bool instruments, const std::function<void (PluginRef)>& onChosen)
{
    if (liveSession == nullptr || ! liveSession->mayUseSharedPlugins())
        return;
    const auto hostName = hostNameOf (*liveSession);
    bool any = false;
    for (const auto& plugin : liveSession->sharedPlugins())
    {
        if (plugin.instrument != instruments)
            continue;
        if (! any)
            menu.addSectionHeader (tr ("On {0}'s computer", { hostName }));
        any = true;
        const auto ref = liveSession->sharedPluginRef (plugin);
        menu.addItem (str (plugin.name) + (plugin.vendor.empty() ? juce::String() : " (" + str (plugin.vendor) + ")") + juce::String::fromUTF8 ("  \xe2\x80\x94  ")
                          + tr ("played by {0}", { hostName }),
                      [ref, onChosen] { onChosen (ref); });
    }
    if (any)
        menu.addSeparator();
}

bool showHostedEditor (const remoteplug::Place& place)
{
    if (liveSession == nullptr || sessionEngine == nullptr || ! liveSession->isActive())
        return false;
    auto& live = *liveSession;
    auto& engine = *sessionEngine;
    const auto* ref = remoteplug::refAt (engine.getSong(), place);
    if (ref == nullptr)
        return false;
    const auto hostName = live.playedByName (*ref);
    if (hostName.isEmpty())
        return false; // (this computer plays it: its own window)
    if (auto it = editors.find (place); it != editors.end())
    {
        it->second->toFront (true);
        return true;
    }
    HostedModel m;
    m.hostName = hostName;
    if (const auto* described = remoteplug::find (live.sharedPlugins(), ref->id))
        m.plugin = *described;
    else
    {
        m.plugin.id = ref->id; // (no longer shared: its name, no parameters)
        m.plugin.name = ref->name;
    }
    m.ref = [&engine, place] {
        const auto* r = remoteplug::refAt (engine.getSong(), place);
        return r != nullptr ? *r : PluginRef();
    };
    m.write = [&engine, place] (const std::vector<std::pair<int, float>>& values) {
        // (not an undo step each: a slider drag writes a few times a second)
        engine.editSong ([&] (Song& s) {
                             if (auto* r = remoteplug::refAt (s, place))
                                 for (const auto& [index, value] : values)
                                     remoteplug::setValue (*r, index, value);
                         },
                         false);
    };
    m.state = [&live, &engine, place] {
        if (remoteplug::refAt (engine.getSong(), place) == nullptr)
            return HostedState::none;
        return live.hostedState (place);
    };
    m.menu = [&engine, place] (juce::Slider& slider, int param) {
        // the same parameter menu as every LYNX control: Create automation clip, Type value...
        AutomationTarget target;
        const auto& song = engine.getSong();
        if (place.slot < 0)
        {
            int channel = -1;
            for (int i = 0; i < (int) song.channels.size(); ++i)
                if (song.channels[(std::size_t) i].uid == place.part.a)
                    channel = i;
            if (channel < 0)
                return;
            target = { AutomationTargetKind::channelParam, channel, 0, param };
        }
        else
            target = { AutomationTargetKind::effectParam, (int) place.part.a, place.slot, param };
        ui::showSliderMenu (slider, &engine, target);
    };
    const auto title = str (ref->name) + juce::String::fromUTF8 (" \xe2\x80\x94 ") + tr ("played by {0}", { hostName });
    editors[place] = std::make_unique<ToolWindow> (title, new HostedPanel (std::move (m)), [place] { editors.erase (place); });
    if (watcher == nullptr)
        watcher = std::make_unique<Watcher>();
    return true;
}

void closeRemoteWindows()
{
    shareWindow.reset();
    editors.clear();
}

void saveRemoteShots (const juce::String& prefix)
{
    auto save = [&] (juce::Component& c, const juce::String& name) {
        const auto file = shot::file (juce::File (prefix + "-" + name + ".png")); // (round 190bi: ui/ShotScale.h)
        file.deleteFile();
        juce::PNGImageFormat png;
        if (auto out = file.createOutputStream())
            png.writeImageToStream (shot::snapshot (c), *out);
    };
    // the host's list
    {
        ShareModel m;
        m.rows = [] {
            std::vector<ShareRow> rows;
            auto add = [&] (const char* name, const char* vendor, const char* format, bool instrument, bool on, const char* usedBy = "") {
                ShareRow row;
                row.id = name;
                row.name = name;
                row.detail = juce::String (vendor) + juce::String::fromUTF8 (" \xc2\xb7 ") + format + juce::String::fromUTF8 (" \xc2\xb7 ") + (instrument ? tr ("Instrument") : tr ("Effect"));
                row.on = on;
                row.usedBy = *usedBy != 0 ? tr ("Used by {0}", { juce::String (usedBy) }) : juce::String();
                rows.push_back (row);
            };
            add ("Analog Lab", "Sample Vendor", "VST3", true, false);
            add ("Glass Keys", "Northern Audio", "VST3", true, true, "Rui: Glass Keys, Lia: Pad");
            add ("Hall of Mirrors", "Northern Audio", "VST3", false, true);
            add ("Mono Bass", "Small Synths", "CLAP", true, false);
            add ("Plate 140", "Sample Vendor", "VST3", false, false);
            add ("Tape Echo", "Small Synths", "VST", false, true, "Rui: Insert 4");
            add ("Wavetable One", "Sample Vendor", "VST3", true, false);
            return rows;
        };
        m.set = [] (const std::string&, bool) { return juce::String(); };
        SharePanel panel (std::move (m));
        save (panel, "share");
    }
    // a guest's editor, in three states
    remoteplug::Shared plugin;
    plugin.name = "Glass Keys";
    plugin.vendor = "Northern Audio";
    plugin.format = "VST3";
    plugin.instrument = true;
    int index = 0;
    auto param = [&] (const char* name, float def, const char* low = "", const char* high = "", std::vector<std::string> steps = {}) {
        plugin.params.push_back ({ index++, name, "", low, high, def, (int) steps.size(), steps });
    };
    param ("Volume", 0.8f, "-inf dB", "0 dB");
    param ("Osc 1 Wave", 0.0f, "", "", { "Sine", "Triangle", "Saw", "Square" });
    param ("Osc 1 Detune", 0.5f, "-50 ct", "+50 ct");
    param ("Osc 2 Wave", 0.0f, "", "", { "Sine", "Triangle", "Saw", "Square" });
    param ("Osc Mix", 0.5f);
    param ("Filter Cutoff", 1.0f, "20 Hz", "20 kHz");
    param ("Filter Resonance", 0.1f);
    param ("Filter Type", 0.0f, "", "", { "Low pass", "Band pass", "High pass" });
    param ("Attack", 0.02f, "0 ms", "10 s");
    param ("Decay", 0.3f, "0 ms", "10 s");
    param ("Sustain", 0.7f);
    param ("Release", 0.25f, "0 ms", "10 s");
    param ("Chorus", 0.0f, "", "", { "Off", "On" });
    param ("Reverb Mix", 0.2f);
    PluginRef ref;
    ref.values = { { 1, 2.0f / 3.0f }, { 5, 0.42f }, { 6, 0.35f }, { 12, 1.0f } };
    const std::pair<const char*, HostedState> states[] = { { "editor", HostedState::upToDate }, { "editor-updating", HostedState::updating }, { "editor-away", HostedState::hostAway } };
    for (const auto& [name, state] : states)
    {
        HostedModel m;
        m.hostName = "Ana";
        m.plugin = plugin;
        m.ref = [ref] { return ref; };
        m.write = [] (const std::vector<std::pair<int, float>>&) {};
        m.state = [state = state] { return state; };
        HostedPanel panel (std::move (m));
        save (panel, name);
    }
}
} // namespace lynx::liveui
