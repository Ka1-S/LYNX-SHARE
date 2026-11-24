#include "ui/Notices.h"

#include "ui/HintBar.h"
#include "ui/I18n.h"
#include "ui/Onboarding.h"
#include "ui/Theme.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace lynx::ui
{
namespace
{
class NoticeCard final : public juce::Component,
                         private juce::Timer
{
public:
    std::function<void (NoticeCard&)> closed;

    explicit NoticeCard (Notice n) : notice (std::move (n))
    {
        setComponentID ("notice");
        closeButton.setComponentID ("notice.close");
        closeButton.setTooltip (tr ("Close"));
        closeButton.onClick = [this] { close(); };
        closeButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        closeButton.setColour (juce::TextButton::textColourOffId, theme::colour::textSecondary);
        addAndMakeVisible (closeButton);
        if (notice.actionText.isNotEmpty() && notice.action)
        {
            actionButton.setButtonText (notice.actionText);
            actionButton.setComponentID ("notice.action");
            actionButton.setColour (juce::TextButton::buttonColourId, theme::colour::bg5); // (a raised button on the card)
            actionButton.onClick = [this] {
                auto fn = notice.action;
                close();
                if (fn)
                    fn();
            };
            addAndMakeVisible (actionButton);
        }
        setSize (width, idealHeight());
        restart();
    }

    ~NoticeCard() override { stopTimer(); }

    static constexpr int width = 380;
    const Notice& getNotice() const noexcept { return notice; }

    int idealHeight() const
    {
        auto h = 14 + textHeight (notice.text, theme::font (theme::type::body, true));
        if (notice.detail.isNotEmpty())
            h += 3 + textHeight (notice.detail, theme::font (theme::type::small));
        if (actionButton.isVisible())
            h += 8 + 24;
        return juce::jmax (44, h + 12);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme::colour;
        const auto b = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (juce::Colours::black.withAlpha (0.35f)); // (a soft shadow, drawn: no effect component)
        g.fillRoundedRectangle (b.translated (0.0f, 2.0f), theme::radius::lg);
        g.setColour (bg3);
        g.fillRoundedRectangle (b, theme::radius::lg);
        g.setColour (borderStrong);
        g.drawRoundedRectangle (b, theme::radius::lg, 1.0f);
        g.setColour (stripe());
        g.fillRoundedRectangle (b.withWidth (4.0f).reduced (0.0f, 6.0f).translated (6.0f, 0.0f), 2.0f);
        auto r = textArea();
        g.setColour (textPrimary);
        g.setFont (theme::font (theme::type::body, true));
        const auto th = textHeight (notice.text, g.getCurrentFont());
        g.drawFittedText (notice.text, r.removeFromTop (th), juce::Justification::topLeft, 3, 1.0f);
        if (notice.detail.isNotEmpty())
        {
            r.removeFromTop (3);
            g.setColour (textSecondary);
            g.setFont (theme::font (theme::type::small));
            g.drawFittedText (notice.detail, r.removeFromTop (textHeight (notice.detail, g.getCurrentFont())), juce::Justification::topLeft, 3, 1.0f);
        }
    }

    void resized() override
    {
        closeButton.setBounds (getWidth() - 30, 6, 24, 22);
        if (actionButton.isVisible())
        {
            const auto w = juce::roundToInt (juce::GlyphArrangement::getStringWidth (getLookAndFeel().getTextButtonFont (actionButton, 24), actionButton.getButtonText())) + 24;
            actionButton.setBounds (textArea().getX(), getHeight() - 12 - 24, juce::jmin (w, getWidth() - 40), 24);
        }
    }

    void mouseEnter (const juce::MouseEvent&) override { stopTimer(); }
    void mouseExit (const juce::MouseEvent&) override
    {
        if (! isMouseOverOrDragging (true))
            restart();
    }

    void restart() { startTimer (notice.holdMs > 0 ? notice.holdMs : notice.kind == Notice::Kind::problem ? 10000 : 6000); }

private:
    juce::Colour stripe() const
    {
        switch (notice.kind)
        {
            case Notice::Kind::done: return theme::colour::ledOn;
            case Notice::Kind::problem: return theme::colour::warning;
            case Notice::Kind::info: break;
        }
        return theme::colour::accent;
    }

    juce::Rectangle<int> textArea() const { return getLocalBounds().reduced (20, 12).withTrimmedRight (20); }

    int textHeight (const juce::String& text, const juce::Font& font) const
    {
        juce::GlyphArrangement glyphs;
        glyphs.addJustifiedText (font, text, 0.0f, 0.0f, (float) (width - 60), juce::Justification::topLeft);
        return juce::jmax ((int) std::ceil (font.getHeight()), (int) std::ceil (glyphs.getBoundingBox (0, -1, true).getHeight()) + 2);
    }

    void timerCallback() override
    {
        if (isMouseOverOrDragging (true))
            return; // (read on: it waits)
        close();
    }

    void close()
    {
        stopTimer();
        if (closed)
            closed (*this);
    }

    Notice notice;
    juce::TextButton closeButton { juce::String::fromUTF8 ("\xc3\x97") }, actionButton;
};

struct Notices
{
    std::vector<juce::Component::SafePointer<juce::Component>> hosts;
    std::vector<std::unique_ptr<NoticeCard>> cards;
    juce::Component::SafePointer<juce::Component> hostShown;

    juce::Component* host()
    {
        hosts.erase (std::remove_if (hosts.begin(), hosts.end(), [] (const auto& h) { return h == nullptr; }), hosts.end());
        for (auto& h : hosts)
            if (auto* top = h->getTopLevelComponent(); top != nullptr && top->isShowing())
                if (auto* peer = top->getPeer(); peer != nullptr && peer->isFocused())
                    return h.getComponent();
        for (auto& h : hosts)
            if (h->isShowing())
                return h.getComponent();
        return hosts.empty() ? nullptr : hosts.front().getComponent();
    }

    void place()
    {
        auto* window = hostShown.getComponent();
        if (window == nullptr)
            return;
        // the lower right, over the hint bar's height from the bottom edge; the newest at the bottom
        auto bottom = window->getHeight() - 52;
        for (auto it = cards.rbegin(); it != cards.rend(); ++it)
        {
            auto& card = **it;
            const auto h = card.idealHeight();
            const auto bounds = juce::Rectangle<int> (window->getWidth() - NoticeCard::width - 16, bottom - h, NoticeCard::width, h);
            onboardui::hostOnTop (card, *window, bounds);
            bottom -= h + 8;
        }
    }

    void remove (NoticeCard& card)
    {
        // (later: the card's own button is still on the stack)
        juce::Component::SafePointer<NoticeCard> safe (&card);
        juce::MessageManager::callAsync ([this, safe] {
            if (safe == nullptr)
                return;
            auto it = std::find_if (cards.begin(), cards.end(), [&safe] (const auto& c) { return c.get() == safe.getComponent(); });
            if (it == cards.end())
                return;
            onboardui::removeHosted (**it);
            cards.erase (it);
            place();
        });
    }
};

Notices& notices()
{
    static Notices n;
    return n;
}
} // namespace

void notify (Notice notice)
{
    // the hint bar says it too (FL users read the hint bar)
    postNotice ({ notice.text, notice.detail, false, false, notice.kind == Notice::Kind::problem ? Hint::Mark::warning : Hint::Mark::none },
                notice.kind == Notice::Kind::problem ? 6000 : 4000);
    auto& n = notices();
    auto* window = n.host();
    if (window == nullptr)
        return;
    if (n.hostShown != window)
    {
        clearNotices();
        n.hostShown = window;
    }
    if (notice.id.isNotEmpty())
        if (auto it = std::find_if (n.cards.begin(), n.cards.end(), [&notice] (const auto& c) { return c->getNotice().id == notice.id; }); it != n.cards.end())
        {
            onboardui::removeHosted (**it);
            n.cards.erase (it);
        }
    while (n.cards.size() >= 3)
    {
        onboardui::removeHosted (*n.cards.front());
        n.cards.erase (n.cards.begin());
    }
    auto card = std::make_unique<NoticeCard> (std::move (notice));
    card->closed = [] (NoticeCard& c) { notices().remove (c); };
    n.cards.push_back (std::move (card));
    n.place();
}

void addNoticeHost (juce::Component* window)
{
    if (window != nullptr)
        notices().hosts.emplace_back (window);
}

void removeNoticeHost (juce::Component* window)
{
    auto& n = notices();
    if (n.hostShown == window)
        clearNotices();
    n.hosts.erase (std::remove_if (n.hosts.begin(), n.hosts.end(), [window] (const auto& h) { return h == nullptr || h.getComponent() == window; }),
                   n.hosts.end());
}

void pluginNotLoaded (const juce::String& pluginName)
{
    Notice n;
    n.kind = Notice::Kind::problem;
    n.id = "plugin.notLoaded";
    n.text = pluginName.isNotEmpty() ? tr ("Couldn't open {0}", { pluginName }) : tr ("Couldn't open this plugin");
    n.detail = tr ("It isn't installed on this computer, or it hasn't been scanned yet. Its slot stays as it is in the project.");
    if (openPluginManager)
    {
        n.actionText = tr ("Plugin manager...");
        n.action = [] {
            if (openPluginManager)
                openPluginManager();
        };
    }
    notify (std::move (n));
}

void savedFile (const juce::String& what, const juce::File& file)
{
    Notice n;
    n.kind = Notice::Kind::done;
    n.id = "file.saved";
    n.text = what;
    n.detail = file.getFileName();
    if (file.exists())
    {
        n.actionText = tr ("Show in folder");
        n.action = [file] { file.revealToUser(); };
    }
    notify (std::move (n));
}

int shownNoticeCount() { return (int) notices().cards.size(); }

juce::Component* shownNotice (int index)
{
    auto& cards = notices().cards;
    return juce::isPositiveAndBelow (index, (int) cards.size()) ? cards[(std::size_t) index].get() : nullptr;
}

void clearNotices()
{
    auto& n = notices();
    for (auto& c : n.cards)
        onboardui::removeHosted (*c);
    n.cards.clear();
}

std::unique_ptr<juce::Component> makeNoticeCard (const Notice& notice)
{
    auto card = std::make_unique<NoticeCard> (notice);
    card->setSize (NoticeCard::width, card->idealHeight());
    return card;
}
} // namespace lynx::ui
