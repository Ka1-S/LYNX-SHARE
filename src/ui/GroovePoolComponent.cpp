#include "ui/GroovePoolComponent.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "core/Groove.h"
#include "core/Gzip.h"
#include "engine/AudioEngine.h"
#include "ui/FileChoosers.h"
#include "ui/I18n.h"
#include "ui/Theme.h"

#ifndef TR_NOOP
 #define TR_NOOP(text) text
#endif

namespace lynx
{
namespace
{
constexpr int titleHeight = 24, headHeight = 18, rowHeight = 20;
constexpr int baseWidth = 52, valueWidth = 64, amountWidth = 56;

juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
juce::String percent (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }

// the folders of LYNX's own grooves, as shown
const char* const folderNames[] = { TR_NOOP ("Swing"), TR_NOOP ("Feel"), TR_NOOP ("Accent"), TR_NOOP ("Utility") };

// A groove file of the user's (.agr; as saved it may be gzip-packed) into the pool. Returns the new id, 0: not read.
std::uint32_t importGrooveFile (AudioEngine& engine, const juce::File& file)
{
    juce::MemoryBlock data;
    if (! file.loadFileAsData (data) || data.getSize() == 0)
        return 0;
    std::string text;
    const auto* bytes = static_cast<const std::uint8_t*> (data.getData());
    if (gzip::isGzip (bytes, data.getSize()))
    {
        const auto plain = gzip::decompress (bytes, data.getSize(), 16u << 20);
        if (! plain)
            return 0;
        text.assign (plain->begin(), plain->end());
    }
    else
        text.assign (static_cast<const char*> (data.getData()), data.getSize());
    Groove g;
    std::string error;
    if (! groove::parseAgr (std::move (text), g, &error))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Could not read the groove file"), file.getFileName());
        return 0;
    }
    if (g.name.empty())
        g.name = file.getFileNameWithoutExtension().toStdString();
    std::uint32_t id = 0;
    engine.editSong ([&] (Song& s) { id = groove::add (s.grooves, g); });
    return id;
}

class GroovePoolComponent final : public juce::Component,
                                  private juce::ChangeListener
{
public:
    explicit GroovePoolComponent (AudioEngine& e) : engine (e)
    {
        setTitle (tr ("Groove Pool"));
        setAccessible (true);
        setWantsKeyboardFocus (true);
        addAndMakeVisible (addButton);
        addButton.setButtonText ("+");
        addButton.setConnectedEdges (0);
        addButton.setTooltip (tr ("Add a groove to the project"));
        addButton.onClick = [this] {
            juce::PopupMenu menu;
            addGrooveMenuItems (menu, engine, this, [safe = juce::Component::SafePointer<GroovePoolComponent> (this)] (std::uint32_t id) {
                if (safe != nullptr)
                {
                    safe->selected = id;
                    safe->repaint();
                }
            });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addButton));
        };
        engine.addChangeListener (this);
    }
    ~GroovePoolComponent() override { engine.removeChangeListener (this); }

    void resized() override { addButton.setBounds (96, 3, 22, titleHeight - 6); }

    void paint (juce::Graphics& g) override
    {
        namespace c = theme::colour;
        const auto& pool = engine.getSong().grooves;
        g.fillAll (c::bg2);
        auto area = getLocalBounds();
        const auto title = area.removeFromTop (titleHeight);
        g.setColour (c::bg3);
        g.fillRect (title);
        g.setColour (c::border);
        g.fillRect (0, 0, getWidth(), 1);
        g.setColour (c::textPrimary);
        g.setFont (theme::font (theme::type::small, true));
        g.drawText (tr ("Groove Pool"), title.withTrimmedLeft (8).withWidth (86), juce::Justification::centredLeft, true);
        // the pool's amount, on the right
        const auto amount = amountBox();
        g.setColour (c::textSecondary);
        g.setFont (theme::font (theme::type::small));
        g.drawText (tr ("Global Amount"), title.withRight (amount.getX() - 6).withTrimmedLeft (124), juce::Justification::centredRight, true);
        paintValue (g, amount, percent (pool.amount), dragging == Column::amount);

        const auto head = area.removeFromTop (headHeight);
        g.setColour (c::textSecondary);
        g.setFont (theme::font (theme::type::small));
        g.drawText (tr ("Groove Name"), columnRect (Column::name, head).withTrimmedLeft (8), juce::Justification::centredLeft, true);
        g.drawText (tr ("Base"), columnRect (Column::base, head), juce::Justification::centred, true);
        g.drawText (tr ("Quantize"), columnRect (Column::quantize, head), juce::Justification::centred, true);
        g.drawText (tr ("Timing"), columnRect (Column::timing, head), juce::Justification::centred, true);
        g.drawText (tr ("Random"), columnRect (Column::random, head), juce::Justification::centred, true);
        g.drawText (tr ("Velocity"), columnRect (Column::velocity, head), juce::Justification::centred, true);
        g.setColour (c::border);
        g.fillRect (0, head.getBottom() - 1, getWidth(), 1);

        if (pool.grooves.empty())
        {
            g.setColour (c::textDisabled);
            g.drawText (tr ("No grooves in this project: add one with +"), area.withTrimmedLeft (8), juce::Justification::centredLeft, true);
            return;
        }
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (area);
        for (int i = 0; i < (int) pool.grooves.size(); ++i)
        {
            const auto row = rowRect (i);
            if (row.getBottom() <= area.getY() || row.getY() >= area.getBottom())
                continue;
            const auto& gr = pool.grooves[(std::size_t) i];
            const auto isSelected = gr.id == selected;
            g.setColour (isSelected ? c::accentMuted : (i % 2 == 0 ? c::bg2 : c::bg1));
            g.fillRect (row);
            g.setColour (c::textPrimary);
            g.setFont (theme::font (theme::type::small));
            g.drawText (utf8 (gr.name), columnRect (Column::name, row).withTrimmedLeft (8), juce::Justification::centredLeft, true);
            const auto hot = [&] (Column col) { return dragging == col && dragRow == i; };
            paintValue (g, columnRect (Column::base, row).reduced (3, 2), groove::baseName (gr.base), false);
            paintValue (g, columnRect (Column::quantize, row).reduced (3, 2), percent (gr.quantize), hot (Column::quantize));
            paintValue (g, columnRect (Column::timing, row).reduced (3, 2), percent (gr.timing), hot (Column::timing));
            paintValue (g, columnRect (Column::random, row).reduced (3, 2), percent (gr.random), hot (Column::random));
            paintValue (g, columnRect (Column::velocity, row).reduced (3, 2), percent (gr.velocity), hot (Column::velocity));
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        dragging = Column::none;
        if (amountBox().contains (e.getPosition()))
        {
            if (! e.mods.isPopupMenu())
                beginDrag (Column::amount, -1, engine.getSong().grooves.amount);
            return;
        }
        const auto row = rowAt (e.getPosition());
        const auto& pool = engine.getSong().grooves;
        if (row < 0 || row >= (int) pool.grooves.size())
            return;
        const auto& gr = pool.grooves[(std::size_t) row];
        selected = gr.id;
        repaint();
        if (e.mods.isPopupMenu())
        {
            showRowMenu (gr.id);
            return;
        }
        switch (const auto col = columnAt (e.x))
        {
            case Column::base: showBaseMenu (gr.id, gr.base); break;
            case Column::quantize: beginDrag (col, row, gr.quantize); break;
            case Column::timing: beginDrag (col, row, gr.timing); break;
            case Column::random: beginDrag (col, row, gr.random); break;
            case Column::velocity: beginDrag (col, row, gr.velocity); break;
            default: break;
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging == Column::none)
            return;
        // one percent per two pixels, up is more
        const auto value = dragStart + (float) juce::roundToInt ((float) -e.getDistanceFromDragStartY() / 2.0f) / 100.0f;
        setValue (dragging, dragRow, value);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        dragging = Column::none;
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (amountBox().contains (e.getPosition()))
        {
            dragEdited = false;
            setValue (Column::amount, -1, 1.0f);
            return;
        }
        const auto row = rowAt (e.getPosition());
        const auto& pool = engine.getSong().grooves;
        if (row < 0 || row >= (int) pool.grooves.size())
            return;
        dragEdited = false;
        switch (const auto col = columnAt (e.x))
        {
            case Column::name: rename (pool.grooves[(std::size_t) row].id); break;
            case Column::timing: setValue (col, row, 1.0f); break; // (the values a new groove has)
            case Column::quantize:
            case Column::random:
            case Column::velocity: setValue (col, row, 0.0f); break;
            default: break;
        }
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        scrollY -= juce::roundToInt (w.deltaY * 120.0f);
        clampScroll();
        repaint();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey)
        {
            removeGroove (selected);
            return true;
        }
        return false;
    }

private:
    enum class Column { none, name, base, quantize, timing, random, velocity, amount };

    AudioEngine& engine;
    juce::TextButton addButton;
    std::uint32_t selected = 0;
    int scrollY = 0;
    Column dragging = Column::none;
    int dragRow = -1;
    float dragStart = 0.0f;
    bool dragEdited = false;

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        clampScroll();
        repaint();
    }

    juce::Rectangle<int> amountBox() const { return { getWidth() - amountWidth - 6, 3, amountWidth, titleHeight - 6 }; }
    juce::Rectangle<int> rowRect (int i) const { return { 0, titleHeight + headHeight + i * rowHeight - scrollY, getWidth(), rowHeight }; }
    int rowAt (juce::Point<int> p) const
    {
        if (p.y < titleHeight + headHeight)
            return -1;
        return (p.y - titleHeight - headHeight + scrollY) / rowHeight;
    }
    void clampScroll()
    {
        const auto rows = (int) engine.getSong().grooves.grooves.size();
        scrollY = juce::jlimit (0, juce::jmax (0, rows * rowHeight - (getHeight() - titleHeight - headHeight)), scrollY);
    }
    // the value columns sit on the right, the name takes the rest
    juce::Rectangle<int> columnRect (Column col, juce::Rectangle<int> row) const
    {
        const auto right = getWidth() - 6;
        const auto values = right - 4 * valueWidth;
        switch (col)
        {
            case Column::name: return row.withRight (juce::jmax (60, values - baseWidth));
            case Column::base: return row.withX (values - baseWidth).withWidth (baseWidth);
            case Column::quantize: return row.withX (values).withWidth (valueWidth);
            case Column::timing: return row.withX (values + valueWidth).withWidth (valueWidth);
            case Column::random: return row.withX (values + 2 * valueWidth).withWidth (valueWidth);
            case Column::velocity: return row.withX (values + 3 * valueWidth).withWidth (valueWidth);
            default: break;
        }
        return {};
    }
    Column columnAt (int x) const
    {
        const juce::Rectangle<int> row (0, 0, getWidth(), 1);
        for (const auto col : { Column::name, Column::base, Column::quantize, Column::timing, Column::random, Column::velocity })
            if (const auto r = columnRect (col, row); x >= r.getX() && x < r.getRight())
                return col;
        return Column::none;
    }

    static void paintValue (juce::Graphics& g, juce::Rectangle<int> box, const juce::String& text, bool hot)
    {
        namespace c = theme::colour;
        g.setColour (c::bg0);
        g.fillRect (box);
        g.setColour (hot ? c::accent : c::borderStrong);
        g.drawRect (box, 1);
        g.setColour (c::textPrimary);
        g.setFont (theme::font (theme::type::small));
        g.drawText (text, box, juce::Justification::centred, false);
    }

    void beginDrag (Column col, int row, float value)
    {
        dragging = col;
        dragRow = row;
        dragStart = value;
        dragEdited = false;
    }

    // (a drag is one undo step: its first change makes it, the rest follow silently)
    void setValue (Column col, int row, float value)
    {
        const auto& pool = engine.getSong().grooves;
        const auto id = row >= 0 && row < (int) pool.grooves.size() ? pool.grooves[(std::size_t) row].id : 0u;
        engine.editSong ([=] (Song& s) {
            if (col == Column::amount)
            {
                s.grooves.amount = juce::jlimit (0.0f, groove::maxAmount, value);
                return;
            }
            auto* gr = groove::find (s.grooves, id);
            if (gr == nullptr)
                return;
            switch (col)
            {
                case Column::quantize: gr->quantize = juce::jlimit (0.0f, 1.0f, value); break;
                case Column::timing: gr->timing = juce::jlimit (0.0f, 1.0f, value); break;
                case Column::random: gr->random = juce::jlimit (0.0f, 1.0f, value); break;
                case Column::velocity: gr->velocity = juce::jlimit (-1.0f, 1.0f, value); break;
                default: break;
            }
        }, ! dragEdited);
        dragEdited = true;
        repaint();
    }

    void showBaseMenu (std::uint32_t id, GrooveBase current)
    {
        juce::PopupMenu menu;
        for (const auto base : { GrooveBase::quarter, GrooveBase::eighth, GrooveBase::eighthTriplet, GrooveBase::sixteenth,
                                 GrooveBase::sixteenthTriplet, GrooveBase::thirtySecond })
            menu.addItem (groove::baseName (base), true, base == current, [this, id, base] {
                engine.editSong ([=] (Song& s) {
                    if (auto* gr = groove::find (s.grooves, id))
                        gr->base = base;
                });
            });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void showRowMenu (std::uint32_t id)
    {
        juce::PopupMenu menu;
        menu.addItem (tr ("Rename..."), [this, id] { rename (id); });
        const auto users = groove::users (engine.getSong(), id);
        menu.addItem (users > 0 ? tr ("Remove (used by {0} clips)", { juce::String (users) }) : tr ("Remove"), [this, id] { removeGroove (id); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void removeGroove (std::uint32_t id)
    {
        if (groove::find (engine.getSong().grooves, id) == nullptr)
            return;
        engine.editSong ([id] (Song& s) { groove::remove (s, id); });
        if (selected == id)
            selected = 0;
    }

    void rename (std::uint32_t id)
    {
        const auto* gr = groove::find (engine.getSong().grooves, id);
        if (gr == nullptr)
            return;
        auto* window = new juce::AlertWindow (tr ("Rename groove"), {}, juce::MessageBoxIconType::NoIcon, this);
        window->addTextEditor ("name", utf8 (gr->name));
        window->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<GroovePoolComponent> self (this);
        window->enterModalState (true, juce::ModalCallbackFunction::create ([self, window, id] (int result) {
            const std::unique_ptr<juce::AlertWindow> owned (window);
            if (result != 1 || self == nullptr)
                return;
            const auto name = owned->getTextEditorContents ("name").trim().toStdString();
            if (! name.empty())
                self->engine.editSong ([=] (Song& s) {
                    if (auto* g = groove::find (s.grooves, id))
                        g->name = name;
                });
        }), false);
    }
};
} // namespace

void addGrooveMenuItems (juce::PopupMenu& menu, AudioEngine& engine, juce::Component* owner, std::function<void (std::uint32_t)> added)
{
    const auto& bank = groove::factoryBank();
    for (const auto* folder : folderNames)
    {
        juce::PopupMenu sub;
        for (std::size_t i = 0; i < bank.size(); ++i)
            if (juce::String (bank[i].folder) == folder)
                sub.addItem (utf8 (bank[i].groove.name), [&engine, i, added] {
                    std::uint32_t id = 0;
                    engine.editSong ([&] (Song& s) { id = groove::add (s.grooves, groove::factoryBank()[i].groove); });
                    if (added != nullptr && id != 0)
                        added (id);
                });
        if (sub.getNumItems() > 0)
            menu.addSubMenu (tr (folder), sub);
    }
    menu.addSeparator();
    menu.addItem (tr ("Import a groove file..."), [&engine, added, safe = juce::Component::SafePointer<juce::Component> (owner)] {
        auto chooser = lynx::ui::makeSharedFileChooser (tr ("Import a groove file"), {}, "*.agr");
        lynx::ui::launchChooser (chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                 [&engine, added, safe, chooser] (const juce::FileChooser& c) {
                                     const auto file = c.getResult();
                                     if (safe == nullptr || ! file.existsAsFile())
                                         return;
                                     if (const auto id = importGrooveFile (engine, file); id != 0 && added != nullptr)
                                         added (id);
                                 });
    });
}

std::unique_ptr<juce::Component> makeGroovePool (AudioEngine& engine)
{
    return std::make_unique<GroovePoolComponent> (engine);
}

int groovePoolHeight (const AudioEngine& engine, int maxRows)
{
    const auto rows = juce::jlimit (1, maxRows, (int) engine.getSong().grooves.grooves.size());
    return titleHeight + headHeight + rows * rowHeight + 2;
}
} // namespace lynx
