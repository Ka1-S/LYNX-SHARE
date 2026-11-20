#include "ui/FlMods.h"
#include "ui/HintBar.h"

#include <cmath>

#include "core/HintMarkup.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/TextCache.h"

namespace lynx::ui
{
namespace
{
struct Posted
{
    Hint hint;
    double until = 0.0;
};

Posted& posted()
{
    static Posted p;
    return p;
}

Posted& notice() // round 190bj: postNotice's message, which a gesture's end or a hover does not replace
{
    static Posted p;
    return p;
}

// one drawable piece of a hint line
struct Item
{
    enum class Kind { word, mouse, key } kind = Kind::word;
    juce::String text;  // word, key name, or mouse token
    float gap = 0.0f;   // space before it
    float width = 0.0f;
    bool title = false; // the hint's subject ("Volume" in "Volume: drag...")
};

bool isCjk (juce::juce_wchar c)
{
    return (c >= 0x2e80 && c <= 0x9fff) || (c >= 0xf900 && c <= 0xfaff) || (c >= 0xff00 && c <= 0xffef);
}

// closing punctuation (、。，．：；！？）」』】〉》, the long-vowel mark ー, ASCII ),.:;!?) stays on the previous line
bool noLineStart (juce::juce_wchar c)
{
    static const auto marks = juce::String::fromUTF8 ("\xe3\x80\x81\xe3\x80\x82\xef\xbc\x8c\xef\xbc\x8e\xef\xbc\x9a\xef\xbc\x9b\xef\xbc\x81\xef\xbc\x9f"
                                   "\xef\xbc\x89\xe3\x80\x8d\xe3\x80\x8f\xe3\x80\x91\xe3\x80\x89\xe3\x80\x8b\xe3\x83\xbc");
    return marks.containsChar (c) || c == ')' || c == ',' || c == '.' || c == ':' || c == ';' || c == '!' || c == '?';
}

std::vector<Item> makeItems (const juce::String& text, const juce::Font& font, const juce::Font& keyFont, float glyph)
{
    std::vector<Item> items;
    const auto space = textcache::stringWidth (font, " ");

    // the subject: the words before the first ": " or " - " (when it comes early, before any glyph)
    auto titleEnd = -1;
    {
        auto colon = text.indexOf (": ");
        if (const auto wide = text.indexOf (juce::String::fromUTF8 ("\xef\xbc\x9a")); wide > 0 && (colon < 0 || wide < colon))
            colon = wide; // CJK full-width colon
        const auto dash = text.indexOf (" - "), bracket = text.indexOf ("[");
        const auto first = colon < 0 ? dash : dash < 0 ? colon : juce::jmin (colon, dash);
        if (first > 0 && first < 48 && (bracket < 0 || first < bracket))
            titleEnd = first + (first == colon ? 1 : 0);
    }

    int consumed = 0; // characters of `text` already turned into items (for the title)
    float pendingGap = 0.0f;

    for (const auto& segment : hintmarkup::parse (text.toStdString()))
    {
        // macOS: FL-for-Mac key names (Ctrl -> ⌘, Alt -> ⌥), in the key caps and in the words (core/ModifierMap.h)
        const auto value = flmods::keyNames (juce::String::fromUTF8 (segment.value.c_str()));

        if (segment.kind != hintmarkup::Segment::Kind::text)
        {
            Item item;
            item.kind = segment.kind == hintmarkup::Segment::Kind::mouse ? Item::Kind::mouse : Item::Kind::key;
            item.text = value;
            item.gap = pendingGap;
            item.width = item.kind == Item::Kind::mouse
                             ? glyph + (value == "2xLMB" ? textcache::stringWidth (keyFont, "x2") + 1.0f : 0.0f)
                             : textcache::stringWidth (keyFont, value) + 8.0f;
            items.push_back (item);
            pendingGap = 0.0f;
            consumed += (int) segment.value.size() + 2;
            continue;
        }

        // words, keeping how much space separates them (two spaces = a wider break between gestures)
        int i = 0;
        while (i < value.length())
        {
            int spaces = 0;
            while (i < value.length() && value[i] == ' ')
            {
                ++spaces;
                ++i;
            }

            pendingGap += spaces == 0 ? 0.0f : spaces == 1 ? space : space * 2.5f;
            const auto start = i;
            while (i < value.length() && value[i] != ' ')
                ++i;

            // Chinese/Japanese text has no spaces: a line may break before any of its characters (so each is its own item,
            // with the punctuation that must not start a line kept on the previous one)
            for (auto from = start; from < i;)
            {
                auto to = from + 1;
                if (! isCjk (value[from]))
                    while (to < i && ! isCjk (value[to]))
                        ++to;
                while (to < i && noLineStart (value[to]))
                    ++to;

                Item item;
                item.text = value.substring (from, to);
                item.gap = pendingGap;
                item.width = textcache::stringWidth (font, item.text);
                item.title = titleEnd > 0 && consumed + from < titleEnd;
                items.push_back (item);
                pendingGap = 0.0f;
                from = to;
            }
        }

        consumed += value.length();
    }

    return items;
}

// Splits the items into at most `maxLines` lines of `width`; false if they do not fit.
bool layOut (const std::vector<Item>& items, float width, int maxLines, std::vector<std::vector<Item>>& lines)
{
    lines.assign (1, {});
    float x = 0.0f;

    // a line only breaks after a word: key and mouse glyphs stay with what follows them ("[Ctrl][T] typing")
    for (std::size_t i = 0; i < items.size();)
    {
        auto end = i + 1;
        while (end < items.size() && items[end - 1].kind != Item::Kind::word)
            ++end;

        float chunkWidth = 0.0f;
        for (auto j = i; j < end; ++j)
            chunkWidth += (j == i ? 0.0f : items[j].gap) + items[j].width;

        const auto needed = (lines.back().empty() ? 0.0f : items[i].gap) + chunkWidth;

        if (! lines.back().empty() && x + needed > width)
        {
            if ((int) lines.size() == maxLines)
                return false;

            lines.emplace_back();
            x = 0.0f;
        }

        for (auto j = i; j < end; ++j)
        {
            auto item = items[j];
            if (lines.back().empty())
                item.gap = 0.0f;
            x += item.gap + item.width;
            lines.back().push_back (item);
        }

        i = end;
    }

    return x <= width;
}

void drawLine (juce::Graphics& g, const std::vector<Item>& line, juce::Rectangle<float> area, const juce::Font& font,
               const juce::Font& keyFont, float glyph, juce::Colour plain = theme::colour::textSecondary)
{
    using namespace theme;
    auto x = area.getX();

    for (const auto& item : line)
    {
        x += item.gap;

        if (x >= area.getRight())
            break;

        const auto w = juce::jmin (item.width, area.getRight() - x);
        const auto box = juce::Rectangle<float> (x, area.getY(), w, area.getHeight());

        switch (item.kind)
        {
            case Item::Kind::word:
                g.setFont (font);
                g.setColour (item.title ? colour::textPrimary : plain);
                g.drawText (item.text, box, juce::Justification::centredLeft, true);
                break;

            case Item::Kind::mouse:
            {
                const auto icon = item.text == "RMB" ? Icon::rightClick : item.text == "MMB" ? Icon::mouseMiddle
                                                                          : item.text == "Wheel" ? Icon::mouseWheel
                                                                                                 : Icon::mouseLeft;
                icons::draw (g, icon, box.withWidth (glyph).withSizeKeepingCentre (glyph, glyph), colour::textPrimary);

                if (item.text == "2xLMB")
                {
                    g.setFont (keyFont);
                    g.setColour (colour::textPrimary);
                    g.drawText ("x2", box.withTrimmedLeft (glyph), juce::Justification::centredLeft, false);
                }
                break;
            }

            case Item::Kind::key:
            {
                const auto cap = box.withSizeKeepingCentre (box.getWidth(), juce::jmin (box.getHeight(), keyFont.getHeight() + 5.0f));
                g.setColour (colour::bg4);
                g.fillRoundedRectangle (cap, radius::sm);
                g.setColour (colour::borderStrong);
                g.drawRoundedRectangle (cap.reduced (0.5f), radius::sm, 1.0f);
                g.setColour (colour::textPrimary);
                g.setFont (keyFont);
                g.drawText (item.text, cap, juce::Justification::centred, false);
                break;
            }
        }

        x += item.width;
    }
}
} // namespace

void postHint (const Hint& hint, int holdMs)
{
    posted().hint = hint;
    posted().until = juce::Time::getMillisecondCounterHiRes() + holdMs;
}

void postNotice (const Hint& hint, int holdMs)
{
    notice().hint = hint;
    notice().until = juce::Time::getMillisecondCounterHiRes() + holdMs;
}

void clearPostedHint()
{
    posted() = {};
}

std::optional<Hint> postedHint()
{
    if (notice().until > 0.0 && juce::Time::getMillisecondCounterHiRes() < notice().until && ! notice().hint.isEmpty())
        return notice().hint;

    if (posted().until > 0.0 && juce::Time::getMillisecondCounterHiRes() < posted().until && ! posted().hint.isEmpty())
        return posted().hint;

    return std::nullopt;
}

juce::String valueGestures()
{
    return tr ("[LMB] drag  [Ctrl] fine  [Wheel] step  [Alt][LMB] reset  [2xLMB] type  [RMB] menu");
}

juce::String plainHint (const juce::String& markup)
{
    const std::vector<std::string> words { tr ("click").toStdString(), tr ("double-click").toStdString(), tr ("right-click").toStdString(),
                                           tr ("middle-click").toStdString(), tr ("wheel").toStdString() };
    return flmods::keyNames (juce::String::fromUTF8 (hintmarkup::plain (markup.toStdString(), words).c_str()));
}

juce::String withMarkup (const juce::String& text)
{
    // the old gesture phrases still used by some panels (kept translated, so they match in both languages)
    static const char* const valuePhrases[] = { TR_NOOP ("drag (Ctrl: fine), wheel, Alt+click resets") };

    for (const auto* phrase : valuePhrases)
        if (const auto translated = tr (phrase); text.contains (translated))
            return text.replace (translated, valueGestures());

    return text;
}

void HintBar::setHint (const Hint& hint, bool isLive)
{
    if (hint.text != current.text || hint.value != current.value || hint.automatable != current.automatable || hint.hasMenu != current.hasMenu
        || isLive != live)
    {
        current = hint;
        live = isLive;
        repaint();
    }
}

void InfoView::setHint (const Hint& hint)
{
    if (hint.text == lastText && hint.value == lastValue) // (the hint timer's every tick: nothing new)
        return;
    lastText = hint.text;
    lastValue = hint.value;
    auto text = plainHint (withMarkup (hint.text)).trim();
    juce::String t, b = text;
    if (const auto colon = text.indexOfChar (':'); colon > 0 && colon <= 48 && ! text.substring (0, colon).containsChar ('\n'))
    {
        t = text.substring (0, colon).trim();
        b = text.substring (colon + 1).trim();
    }
    if (hint.value.isNotEmpty())
        b << (b.isEmpty() ? "" : "\n") << hint.value;
    if (t == title && b == body)
        return;
    title = t;
    body = b;
    layoutWidth = -1.0f;
    repaint();
}

void InfoView::paint (juce::Graphics& g)
{
    namespace ll = theme::livelook;
    auto r = getLocalBounds();
    g.setColour (ll::surface);
    g.fillRect (r);
    const auto bar = r.removeFromTop (17);
    g.setColour (ll::raised);
    g.fillRect (bar);
    g.setColour (ll::text);
    g.setFont (theme::font (theme::type::small, true));
    g.drawText (title, bar.withTrimmedLeft (5).withTrimmedRight (4), juce::Justification::centredLeft, true);
    g.setFont (theme::font (theme::type::small));
    // Live: 15 px lines from 6 px in (its text 15..226 in a 231 px view)
    const auto area = r.withTrimmedTop (4).reduced (6, 0).toFloat();
    if (area.getWidth() != layoutWidth)
    {
        juce::AttributedString text;
        text.append (body, theme::font (theme::type::small), ll::text);
        text.setLineSpacing (15.0f - theme::font (theme::type::small).getHeight());
        text.setWordWrap (juce::AttributedString::byWord);
        layout.createLayout (text, area.getWidth());
        layoutWidth = area.getWidth();
    }
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (area.toNearestInt());
    layout.draw (g, area.withHeight (layout.getHeight()));
}

juce::PopupMenu HintBar::copyMenu() const
{
    juce::PopupMenu menu;
    const auto value = plainHint (current.value).trim();
    const auto text = plainHint (current.text).trim();
    menu.addItem (value.isEmpty() ? tr ("Copy value") : tr ("Copy value: {0}", { value }), value.isNotEmpty(), false,
                  [value] { juce::SystemClipboard::copyTextToClipboard (value); });
    menu.addItem (tr ("Copy text"), text.isNotEmpty(), false, [text] { juce::SystemClipboard::copyTextToClipboard (text); });
    return menu;
}

void HintBar::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        return;
    auto menu = onMenu != nullptr ? onMenu() : juce::PopupMenu();
    if (menu.getNumItems() > 0)
        menu.addSeparator();
    for (juce::PopupMenu::MenuItemIterator it (copyMenu()); it.next();)
        menu.addItem (it.getItem());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

// rollup 0.1.30 step 13 (CI paint gate, stream playlist +5 %): the laid-out text kept between paints
struct HintBar::LaidOut
{
    juce::String text;
    float width = -1.0f;
    juce::Font big { juce::FontOptions() }, bigKey { juce::FontOptions() }, small { juce::FontOptions() }, smallKey { juce::FontOptions() };
    bool larger = false;
    bool oneLine = false, fits = false;
    std::vector<std::vector<Item>> lines;
};

void HintBar::paint (juce::Graphics& g)
{
    using namespace theme;
    auto r = getLocalBounds().toFloat().reduced (6.0f, 1.0f);

    // capability markers (dim when not applicable, so the text does not jump)
    if (current.automatable || current.hasMenu)
    {
        icons::draw (g, Icon::automate, r.removeFromLeft (16.0f).withSizeKeepingCentre (14.0f, 14.0f),
                     current.automatable ? colour::automation : colour::textDisabled.withAlpha (0.35f));
        r.removeFromLeft (2.0f);
        icons::draw (g, Icon::rightClick, r.removeFromLeft (16.0f).withSizeKeepingCentre (14.0f, 14.0f),
                     current.hasMenu ? colour::textSecondary : colour::textDisabled.withAlpha (0.35f));
        r.removeFromLeft (8.0f);
    }

    // the value, in a pill on the right (accent while it changes under the mouse)
    if (current.value.isNotEmpty())
    {
        const juce::Font valueFont (font (type::small, true));
        const auto w = juce::jmin (r.getWidth() / 3.0f, textcache::stringWidth (valueFont, current.value) + 18.0f);
        const auto valueArea = r.removeFromRight (juce::jmax (44.0f, w)).reduced (0.0f, 3.0f);
        g.setColour (live ? colour::accentMuted : colour::bg3);
        g.fillRoundedRectangle (valueArea, radius::sm);
        if (live)
        {
            g.setColour (colour::accent);
            g.drawRoundedRectangle (valueArea.reduced (0.5f), radius::sm, 1.0f);
        }
        g.setColour (colour::textPrimary);
        g.setFont (valueFont);
        g.drawFittedText (current.value, valueArea.toNearestInt(), juce::Justification::centred, 1, 0.8f);
        r.removeFromRight (8.0f);
    }

    // the text: one line if it fits, else two smaller lines, else two lines cut at the end
    const auto text = withMarkup (current.text);

    // (round 190jo FM-7: the Pattern toolbar's hint one step larger, its words in the primary text colour)
    const auto plain = larger ? colour::textPrimary.withAlpha (0.88f) : colour::textSecondary;
    const juce::Font big (font (larger ? type::body + 1.0f : type::small)), bigKey (font (larger ? type::caption : type::caption - 1.0f, true));
    const auto bigGlyph = larger ? 16.0f : 15.0f;
    const juce::Font smallFont (font (larger ? type::small : type::caption)), smallKey (font (larger ? type::caption - 1.0f : type::caption - 2.0f, true));
    const auto smallGlyph = larger ? 13.0f : 12.0f;
    if (laidOut == nullptr)
        laidOut = std::make_shared<LaidOut>();
    auto& laid = *laidOut;
    if (laid.text != text || laid.width != r.getWidth() || laid.larger != larger || laid.big != big || laid.bigKey != bigKey
        || laid.small != smallFont || laid.smallKey != smallKey)
    {
        laid.text = text;
        laid.width = r.getWidth();
        laid.larger = larger;
        laid.big = big;
        laid.bigKey = bigKey;
        laid.small = smallFont;
        laid.smallKey = smallKey;
        laid.oneLine = layOut (makeItems (text, big, bigKey, bigGlyph), r.getWidth(), 1, laid.lines);
        laid.fits = laid.oneLine || layOut (makeItems (text, smallFont, smallKey, smallGlyph), r.getWidth(), 2, laid.lines);
    }
    const auto& lines = laid.lines;
    if (laid.oneLine)
    {
        drawLine (g, lines.front(), r.withSizeKeepingCentre (r.getWidth(), larger ? 19.0f : 17.0f), big, bigKey, bigGlyph, plain);
        return;
    }

    const auto fits = laid.fits;
    const auto lineHeight = juce::jmin (larger ? 14.0f : 13.0f, r.getHeight() / 2.0f);
    auto area = r.withSizeKeepingCentre (r.getWidth(), lineHeight * 2.0f);

    for (std::size_t i = 0; i < lines.size() && i < 2; ++i)
        drawLine (g, lines[i], area.removeFromTop (lineHeight), smallFont, smallKey, smallGlyph, plain);

    if (! fits) // the rest did not fit: a fade at the end of the second line says so
    {
        const auto fade = r.withLeft (r.getRight() - 24.0f).withTrimmedTop (r.getHeight() / 2.0f);
        g.setGradientFill (juce::ColourGradient (colour::bg1.withAlpha (0.0f), fade.getX(), 0.0f, colour::bg1, fade.getRight(), 0.0f, false));
        g.fillRect (fade);
    }
}
// --- quick tips ----------------------------------------------------------------------------------

namespace
{
const juce::Identifier quickTipProperty ("lynxQuickTip");

juce::Font tipNameFont() { return juce::Font (theme::font (theme::type::caption)); }
juce::Font tipKeyFont() { return juce::Font (theme::font (theme::type::caption - 1.0f)); }
constexpr float tipPadX = 7.0f, tipGap = 8.0f;
constexpr float tipCapPad = 8.0f; // the shortcut's key cap around its text (round 190je, SG-25: as the hint bar's keys)
constexpr int tipHeight = 20;
} // namespace

void setQuickTip (juce::Component& c, const juce::String& name, const juce::String& shortcut)
{
    c.getProperties().set (quickTipProperty, shortcut.trim().isEmpty() ? name : name + "\t" + flmods::keyNames (shortcut.trim()));
}

juce::String quickTipOf (const juce::Component& c)
{
    return c.getProperties()[quickTipProperty].toString();
}

juce::Rectangle<int> quickTipSize (const juce::String& tip)
{
    const auto name = tip.upToFirstOccurrenceOf ("\t", false, false), key = tip.fromFirstOccurrenceOf ("\t", false, false);
    auto w = textcache::stringWidth (tipNameFont(), name) + 2.0f * tipPadX;
    if (key.isNotEmpty())
        w += tipGap + textcache::stringWidth (tipKeyFont(), key) + tipCapPad;
    return { 0, 0, (int) std::ceil (w) + 2, tipHeight };
}

void paintQuickTip (juce::Graphics& g, juce::Rectangle<float> bubble, const juce::String& tip)
{
    using namespace theme;
    const auto name = tip.upToFirstOccurrenceOf ("\t", false, false), key = tip.fromFirstOccurrenceOf ("\t", false, false);
    g.setColour (colour::bg0.withAlpha (0.96f));
    g.fillRoundedRectangle (bubble, radius::sm);
    g.setColour (colour::borderStrong.withAlpha (0.9f));
    g.drawRoundedRectangle (bubble.reduced (0.5f), radius::sm, 1.0f);

    auto text = bubble.reduced (tipPadX, 0.0f);
    g.setColour (colour::textPrimary); // (round 190je, SG-25: the full token, was × 0.92)
    g.setFont (tipNameFont());
    const auto nameWidth = textcache::stringWidth (tipNameFont(), name);
    g.drawText (name, text.removeFromLeft (nameWidth + 1.0f), juce::Justification::centredLeft, false);

    if (key.isNotEmpty())
    {
        // the shortcut as a key cap, the way the hint bar shows keys (round 190je, SG-25: the text read 4.6:1 at × 0.85)
        text.removeFromLeft (tipGap);
        const auto keyWidth = textcache::stringWidth (tipKeyFont(), key) + tipCapPad;
        const auto cap = text.withWidth (juce::jmin (keyWidth, text.getWidth()))
                             .withSizeKeepingCentre (juce::jmin (keyWidth, text.getWidth()), juce::jmin (text.getHeight() - 4.0f, tipKeyFont().getHeight() + 4.0f));
        g.setColour (colour::bg4);
        g.fillRoundedRectangle (cap, radius::sm);
        g.setColour (colour::borderStrong);
        g.drawRoundedRectangle (cap.reduced (0.5f), radius::sm, 1.0f);
        g.setColour (colour::textPrimary);
        g.setFont (tipKeyFont());
        g.drawText (key, cap, juce::Justification::centred, false);
    }
}

juce::Rectangle<int> quickTipBounds (const juce::String& tip, juce::Rectangle<int> target, juce::Rectangle<int> area)
{
    auto r = quickTipSize (tip).withCentre ({ target.getCentreX(), 0 }).withY (target.getBottom() + 5);
    if (r.getBottom() > area.getBottom())
        r.setY (target.getY() - 5 - r.getHeight());
    return r.constrainedWithin (area);
}

struct QuickTipWindow::Look final : juce::LookAndFeel_V4
{
    juce::Rectangle<int> target; // screen bounds of the control the tip is for

    juce::Rectangle<int> getTooltipBounds (const juce::String& tip, juce::Point<int> pos, juce::Rectangle<int> parentArea) override
    {
        return quickTipBounds (tip, target.isEmpty() ? juce::Rectangle<int> (pos.x, pos.y, 1, 16) : target, parentArea);
    }

    void drawTooltip (juce::Graphics& g, const juce::String& tip, int width, int height) override
    {
        paintQuickTip (g, juce::Rectangle<int> (width, height).toFloat(), tip);
    }
};

QuickTipWindow::QuickTipWindow() : juce::TooltipWindow (nullptr, 400), look (std::make_unique<Look>())
{
    setOpaque (false);
    setLookAndFeel (look.get());
}

QuickTipWindow::~QuickTipWindow()
{
    hideTip();
    setLookAndFeel (nullptr);
}

juce::String QuickTipWindow::getTipFor (juce::Component& c)
{
    if (juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown() || c.isCurrentlyBlockedByAnotherModalComponent())
        return {};

    // the control itself or the component it is drawn in (a button's inner parts)
    auto* owner = &c;
    for (int depth = 0; owner != nullptr && depth < 3 && quickTipOf (*owner).isEmpty(); ++depth)
        owner = owner->getParentComponent();

    if (owner == nullptr || quickTipOf (*owner).isEmpty() || ! owner->isEnabled())
        return {};

    look->target = owner->getScreenBounds();
    return quickTipOf (*owner);
}

// --- last tweaked read-out (round 162d) -------------------------------------------------------------------------

TweakReadout::TweakReadout()
{
    setRepaintsOnMouseActivity (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip (tr ("Last tweaked parameter: the control you moved last and its value. Click for its menu (automation clip, link to controller, events, value)"));
    setHint (*this, tr ("Last tweaked parameter: [LMB] its menu (create automation clip, link to controller, edit events, init song with this position, value)"),
             true, true);
}

void TweakReadout::setContent (const juce::String& newName, const juce::String& newValue)
{
    if (newName == name && newValue == value)
        return;
    if (newName == name && newValue != value)
    {
        changedMs = juce::Time::getMillisecondCounterHiRes();
        startTimer (950);
    }
    name = newName;
    value = newValue;
    repaint();
}

void TweakReadout::paint (juce::Graphics& g)
{
    using namespace theme;
    auto r = getLocalBounds().toFloat().reduced (0.0f, 1.0f);
    const auto hover = isMouseOver();
    fillRoundRect (g, r, radius::md, hover ? colour::bg4 : colour::bg1);
    if (hover)
    {
        g.setColour (colour::borderStrong);
        g.drawRoundedRectangle (r.reduced (0.5f), radius::md, 1.0f);
    }
    r = r.reduced (6.0f, 0.0f);
    // round 170e (Busy Works review #2: "I can't tell what that is"): a knob icon and a "LAST TWEAKED" caption over the
    // parameter's name, as the toolbar's SNAP / PATTERN captions
    icons::draw (g, Icon::tweak, r.removeFromLeft (17.0f).withSizeKeepingCentre (16.0f, 16.0f),
                 name.isNotEmpty() ? colour::automation : colour::textDisabled);
    r.removeFromLeft (6.0f);

    const auto fresh = juce::Time::getMillisecondCounterHiRes() - changedMs < 900.0;
    if (name.isNotEmpty() && value.isNotEmpty())
    {
        const juce::Font valueFont (font (type::small, true));
        const auto w = juce::jmin (r.getWidth() * 0.4f, textcache::stringWidth (valueFont, value) + 14.0f);
        const auto valueArea = r.removeFromRight (juce::jmax (36.0f, w)).reduced (0.0f, 3.0f);
        g.setColour (fresh ? colour::accentMuted : colour::bg3);
        g.fillRoundedRectangle (valueArea, radius::sm);
        g.setColour (colour::textPrimary);
        g.setFont (valueFont);
        g.drawFittedText (value, valueArea.toNearestInt(), juce::Justification::centred, 1, 0.75f);
        r.removeFromRight (6.0f);
    }

    const auto twoLines = r.getHeight() >= 22.0f;
    if (twoLines)
    {
        auto caption = r.removeFromTop (r.getHeight() * 0.42f);
        g.setFont (font (type::caption - 1.0f, true));
        g.setColour (colour::textDisabled);
        g.drawFittedText (tr ("LAST TWEAKED"), caption.withTrimmedTop (2.0f).toNearestInt(), juce::Justification::bottomLeft, 1, 0.8f);
    }
    g.setFont (font (type::small));
    if (name.isEmpty())
    {
        g.setColour (colour::textDisabled);
        g.drawFittedText (twoLines ? tr ("none yet: move any knob") : tr ("Last tweaked: none"), r.toNearestInt(),
                          twoLines ? juce::Justification::topLeft : juce::Justification::centredLeft, 1, 0.8f);
        return;
    }
    g.setColour (hover ? colour::textPrimary : colour::textSecondary);
    g.drawFittedText (twoLines ? name : tr ("Last tweaked: {0}", { name }), r.toNearestInt(),
                      twoLines ? juce::Justification::topLeft : juce::Justification::centredLeft, 1, 0.75f);
}

void TweakReadout::mouseDown (const juce::MouseEvent&)
{
    if (onClick)
        onClick();
}

} // namespace lynx::ui
