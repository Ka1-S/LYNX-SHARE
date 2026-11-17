#pragma once

// FL Studio's Chord progression tool in the Piano roll: its top panel (the spanner's options for generation, Generate and
// the three Generate modes) and its Chord viewer, the strip of chord blocks (round 190kd; FL 2026 screen session
// shots f27–f36 in /mnt/e/lynx-work/prchord190kd/raw, DFM TPRChordToolForm, FL's manual; core/ChordStrip.h has the rules).
// Included by ui/NoteToolDialogs.cpp only (the Chord progression dialog). LYNX vectors only.
//
// Layout, top to bottom (FL's order): the top bar [spanner][Generate][Rework][Start over][Analyze]; the ruler with the
// bar numbers; the key tab ("♪ A Major (Ionian)", right-click: Edit › root / scale, Randomize); the blocks — each with
// its padlock (top left), its inversion "⇕ n" (top right), its notes as lines (main notes over the bass line), the
// chord name in a label in the middle, and at the bottom Regenerate (left), Show alternatives (▾, middle) and the bass
// switch (right); the selected blocks outlined green, red while learning from MIDI; the bottom bar with FL's Names
// switch (abs / rel). The alternatives open under the blocks: "Select alternative chord (right click to preview)".
// Perf: no timer runs unless a preview is sounding or Learn from MIDI input is on; paint is a few dozen rectangles.

#include <functional>
#include <set>

#include "core/ChordStrip.h"
#include "engine/AudioEngine.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

namespace lynx
{
struct ChordToolState
{
    chordstrip::Strip strip;
    std::set<int> selected;               // block indices
    chordstrip::GenMode mode = chordstrip::GenMode::rework; // FL's default (the arrows lit when the tool opens)
    chordstrip::NonChord nonChord = chordstrip::NonChord::keep; // FL's default
    bool main = true;                     // spanner › Notes › Main (Bass is the dialog's "Bass note")
    bool relativeNames = false;           // FL's Names switch: abs
    bool learning = false;                // FL's "Learn from MIDI input"
    bool autoLearnNext = false;           // FL's "Auto learn next chord" (off): the selection moves on after a learn
    bool analyzeAvailable = false;        // FL shows Analyze only when the Piano roll had notes as the tool opened
    int octave = 4;
    bool smooth = true;
    float adventure = 0.3f;
    std::uint32_t seed = 1;
    std::optional<chordstrip::Block> clipboard;
    std::set<int> snapBlocks;             // chord menu › Snap existing notes
    std::vector<Note> opening;            // the notes the tool found (from the progression's start)
};

class ChordStripView final : public juce::Component, public juce::TooltipClient, private juce::Timer
{
public:
    ChordStripView (AudioEngine& e, ChordToolState& s, int previewChannel) : engine (e), state (s), channel (previewChannel)
    {
        setWantsKeyboardFocus (true);
    }

    ~ChordStripView() override { releasePreview(); }

    std::function<void()> onChanged;       // a block or the key changed: the dialog writes the notes again
    std::function<void()> onGenerate;      // the Generate button (the dialog runs the chosen mode)
    std::function<void()> onKeyChanged;    // the key tab changed the key / scale (the dialog's Key and Scale follow)
    std::function<void()> onBassChanged;   // the spanner's Bass (the dialog's "Bass note" follows)
    std::function<bool()> bassOn;          // the dialog's "Bass note"
    std::function<void (bool)> setBassOn;

    static constexpr int topBarHeight = 34, rulerHeight = 16, tabHeight = 19, blocksHeight = 124, bottomHeight = 26,
                         altHeight = 86;
    int preferredHeight() const noexcept { return topBarHeight + rulerHeight + tabHeight + blocksHeight + bottomHeight + 4; }

    // FL: "You must have exactly one chord selected to enable MIDI learn." Returns whether learning is on.
    bool setLearning (bool on)
    {
        if (on && state.selected.size() != 1)
        {
            if (onMessage)
                onMessage (tr ("You must have exactly one chord selected to enable MIDI learn."));
            on = false;
        }
        state.learning = on;
        learnPeak.clear();
        learnHeld = false;
        updateTimer();
        repaint();
        return on;
    }

    // (for the tests and the shot route)
    juce::PopupMenu chordMenuForSnapshot (int block) { return chordMenu (block); }
    juce::PopupMenu optionsMenuForSnapshot() { return optionsMenu(); }
    juce::PopupMenu keyMenuForSnapshot() { return keyMenu(); }
    void showAlternativesForSnapshot (int block)
    {
        if (block >= 0 && block < (int) state.strip.blocks.size())
            showAlternatives (block);
        else
            hideAlternatives();
    }
    // a played chord as Learn from MIDI input takes it (the keys held at their fullest, then released)
    bool learnKeysForSnapshot (const std::vector<int>& keys) { return learn (keys); }

    void paint (juce::Graphics& g) override
    {
        using namespace theme::colour;
        g.fillAll (bg2);

        // --- top bar: [spanner] [Generate] [Rework] [Start over] [Analyze]
        const auto top = topBar();
        theme::fillRoundRect (g, top.toFloat().reduced (0.0f, 2.0f), theme::radius::md, bg3);
        icons::draw (g, Icon::wrench, spannerArea().toFloat().reduced (4.0f), hovered == Part::spanner ? textPrimary : textSecondary);
        {
            const auto b = generateArea().toFloat();
            theme::fillRoundRect (g, b, theme::radius::md, hovered == Part::generate ? accentHover : accent);
            g.setColour (onAccent);
            g.setFont (theme::font (theme::type::body, true));
            g.drawText (tr ("Generate"), b.toNearestInt(), juce::Justification::centred, false);
        }
        for (int m = 0; m < 3; ++m)
        {
            if (m == 2 && ! state.analyzeAvailable)
                continue;
            const auto r = modeArea (m).toFloat();
            const auto lit = (int) state.mode == m;
            if (hovered == Part::mode && hoveredIndex == m)
                theme::fillRoundRect (g, r, theme::radius::sm, bg5);
            const Icon icon = m == 0 ? Icon::loop : m == 1 ? Icon::dice : Icon::zoom;
            icons::draw (g, icon, r.reduced (3.0f), lit ? warning : textSecondary);
        }

        // --- ruler and key tab
        const auto ruler = rulerArea();
        g.setColour (bg0);
        g.fillRect (ruler);
        g.setFont (theme::font (theme::type::caption));
        const auto bar = (double) barTicks();
        const auto total = (double) std::max<std::int64_t> (1, state.strip.length());
        for (double t = 0.0; t < total; t += bar)
        {
            const auto x = tickToX (t);
            g.setColour (textDisabled);
            g.drawVerticalLine (x, (float) ruler.getY() + 3.0f, (float) ruler.getBottom());
            g.setColour (textSecondary);
            g.drawText (juce::String ((int) std::lround (t / bar) + 1), x + 3, ruler.getY(), 30, ruler.getHeight(), juce::Justification::centredLeft, false);
        }
        {
            const auto tab = tabArea();
            const auto text = juce::String (chordstrip::keyName (state.strip));
            g.setFont (theme::font (theme::type::small));
            const auto w = juce::jmin (tab.getWidth(), juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), text) + 32);
            const auto t = tab.withWidth (w).toFloat();
            juce::Path p; // FL's tab: square left, a slanted right edge
            p.startNewSubPath (t.getX(), t.getBottom());
            p.lineTo (t.getX(), t.getY() + 2.0f);
            p.lineTo (t.getRight() - 10.0f, t.getY() + 2.0f);
            p.lineTo (t.getRight(), t.getBottom());
            p.closeSubPath();
            g.setColour (hovered == Part::tab ? bg6 : bg5);
            g.fillPath (p);
            icons::draw (g, Icon::note, t.withWidth (16.0f).withTrimmedTop (3.0f).reduced (2.0f), textPrimary);
            g.setColour (textPrimary);
            g.drawText (text, t.withTrimmedLeft (18.0f).toNearestInt(), juce::Justification::centredLeft, false);
        }

        // --- the blocks
        const auto area = blocksArea();
        g.setColour (bg0);
        g.fillRect (area);
        const auto [low, high] = keyRange();
        for (std::size_t i = 0; i < state.strip.blocks.size(); ++i)
            paintBlock (g, (int) i, low, high);

        // --- bottom bar: Names abs / rel
        {
            const auto b = bottomArea();
            g.setColour (textSecondary);
            g.setFont (theme::font (theme::type::small));
            const auto sw = namesSwitchArea();
            g.drawText (tr ("Names"), sw.withX (sw.getX() - 60).withWidth (54), juce::Justification::centredRight, false);
            theme::fillRoundRect (g, sw.toFloat(), sw.getHeight() * 0.5f, bg4);
            const auto knob = state.relativeNames ? sw.toFloat().removeFromRight ((float) sw.getHeight()) : sw.toFloat().removeFromLeft ((float) sw.getHeight());
            g.setColour (textSecondary);
            g.fillEllipse (knob.reduced (3.0f));
            g.setColour (textPrimary);
            g.setFont (theme::font (theme::type::caption));
            g.drawText (state.relativeNames ? "rel" : "abs", state.relativeNames ? sw.withTrimmedRight (sw.getHeight()) : sw.withTrimmedLeft (sw.getHeight()),
                        juce::Justification::centred, false);
            juce::ignoreUnused (b);
        }

        // --- the alternatives (FL's AltChordsPanel)
        if (altBlock >= 0)
            paintAlternatives (g);
    }

    void mouseMove (const juce::MouseEvent& e) override { setHover (e.getPosition()); }
    void mouseExit (const juce::MouseEvent&) override { setHover ({ -1, -1 }); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        const auto pos = e.getPosition();
        const auto right = e.mods.isPopupMenu();

        if (altBlock >= 0 && altPanel().contains (pos))
        {
            if (altClose().contains (pos))
                hideAlternatives();
            else if (altRefresh().contains (pos))
            {
                ++state.seed;
                alternativesList = altCandidates (altBlock);
                repaint();
            }
            else if (const auto k = altAt (pos); k >= 0)
            {
                if (right) // (right click: preview)
                {
                    auto b = state.strip.blocks[(std::size_t) altBlock];
                    b.chord = alternativesList[(std::size_t) k];
                    preview ({ b });
                }
                else
                {
                    auto& b = state.strip.blocks[(std::size_t) altBlock];
                    b.chord = alternativesList[(std::size_t) k];
                    edited();
                    hideAlternatives();
                }
            }
            return;
        }

        if (! right && spannerArea().contains (pos))
        {
            optionsMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (spannerArea())));
            return;
        }
        if (! right && generateArea().contains (pos))
        {
            if (onGenerate)
                onGenerate();
            return;
        }
        for (int m = 0; m < 3; ++m)
            if (! right && (m < 2 || state.analyzeAvailable) && modeArea (m).contains (pos))
            {
                state.mode = (chordstrip::GenMode) m;
                repaint();
                return;
            }
        if (tabArea().contains (pos) && tabTextArea().contains (pos))
        {
            keyMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
            return;
        }
        if (! right && namesSwitchArea().expanded (2).contains (pos))
        {
            state.relativeNames = ! state.relativeNames;
            repaint();
            return;
        }

        const auto i = blockAt (pos);
        if (i < 0)
        {
            if (! right)
                state.selected.clear();
            repaint();
            return;
        }
        const auto r = blockArea (i);
        auto& b = state.strip.blocks[(std::size_t) i];

        if (right && e.mods.isAltDown()) // FL: Alt+right-click previews the chord
        {
            preview ({ b });
            return;
        }
        if (right)
        {
            if (! state.selected.contains (i))
                state.selected = { i };
            repaint();
            chordMenu (i).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
            return;
        }
        if (lockArea (r).contains (pos))
        {
            b.locked = ! b.locked;
            repaint();
            return;
        }
        if (regenArea (r).contains (pos))
        {
            regenerate (i);
            return;
        }
        if (altArrowArea (r).contains (pos))
        {
            state.selected = { i };
            showAlternatives (i);
            return;
        }
        if (bassArea (r).contains (pos))
        {
            b.bass = ! b.bass;
            edited();
            return;
        }
        if (e.mods.isCommandDown()) // FL: Ctrl+click selects more than one
        {
            if (state.selected.contains (i))
                state.selected.erase (i);
            else
                state.selected.insert (i);
        }
        else
            state.selected = { i };
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        // FL: a double-click on a chord shows its alternatives; with Shift it adds a passing chord
        const auto i = blockAt (e.getPosition());
        if (i < 0 || (altBlock >= 0 && altPanel().contains (e.getPosition())))
            return;
        if (e.mods.isShiftDown())
            passing (i);
        else
            showAlternatives (i);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        // FL: Shift+wheel changes the hovered chord's inversion, Alt+wheel transposes it
        const auto i = blockAt (e.getPosition());
        if (i < 0 || w.deltaY == 0.0f)
            return;
        auto& b = state.strip.blocks[(std::size_t) i];
        const auto step = w.deltaY > 0 ? 1 : -1;
        if (e.mods.isShiftDown())
            b.inversion += step;
        else if (e.mods.isAltDown())
            b.transpose += step;
        else
            return;
        edited();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto cmd = key.getModifiers().isCommandDown();
        if (key == juce::KeyPress::deleteKey)
        {
            deleteSelected();
            return true;
        }
        if (cmd && key.getKeyCode() == 'A') { for (int i = 0; i < (int) state.strip.blocks.size(); ++i) state.selected.insert (i); repaint(); return true; }
        if (cmd && key.getKeyCode() == 'D') { state.selected.clear(); repaint(); return true; }
        if (cmd && key.getKeyCode() == 'G') { if (onGenerate) onGenerate(); return true; }
        if (cmd && key.getKeyCode() == 'C' && ! state.selected.empty()) { state.clipboard = state.strip.blocks[(std::size_t) *state.selected.begin()]; return true; }
        if (cmd && key.getKeyCode() == 'V' && state.clipboard && ! state.selected.empty()) { pasteInto (*state.selected.begin()); return true; }
        if (key == juce::KeyPress::spaceKey) { previewFrom (0); return true; }
        if (key == juce::KeyPress::escapeKey && altBlock >= 0) { hideAlternatives(); return true; }
        return false;
    }

    void resized() override {}

    // FL's hints for the top bar (DFM TPRChordToolForm)
    juce::String getTooltip() override
    {
        switch (hovered)
        {
            case Part::spanner: return tr ("Options for generation");
            case Part::generate: return tr ("Run generative model on all existing chords");
            case Part::mode:
                return hoveredIndex == 0 ? tr ("Generate mode") : hoveredIndex == 1 ? tr ("Generate mode: Start over")
                                                                                    : tr ("Generate mode: Analyze the notes found in the Piano roll");
            case Part::tab: return tr ("Right-click: the key and scale");
            case Part::block: return tr ("[LMB] select  [Ctrl] more  [RMB] chord options  [Alt][RMB] preview  [Double-click] alternatives");
            case Part::none:
            case Part::alt: break;
        }
        return {};
    }

private:
    enum class Part { none, spanner, generate, mode, tab, block, alt };

    // --- geometry ----------------------------------------------------------------------------------
    juce::Rectangle<int> topBar() const { return getLocalBounds().removeFromTop (topBarHeight); }
    juce::Rectangle<int> spannerArea() const { return { topBar().getCentreX() - 110, 6, 24, 24 }; }
    juce::Rectangle<int> generateArea() const { return { topBar().getCentreX() - 80, 5, 96, 26 }; }
    juce::Rectangle<int> modeArea (int m) const { return { topBar().getCentreX() + 22 + m * 26, 7, 22, 22 }; }
    juce::Rectangle<int> rulerArea() const { return { 0, topBarHeight, getWidth(), rulerHeight }; }
    juce::Rectangle<int> tabArea() const { return { 0, topBarHeight + rulerHeight, getWidth(), tabHeight }; }
    juce::Rectangle<int> tabTextArea() const { return tabArea().withWidth (220); }
    juce::Rectangle<int> blocksArea() const { return { 0, topBarHeight + rulerHeight + tabHeight, getWidth(), blocksHeight }; }
    juce::Rectangle<int> bottomArea() const { return { 0, blocksArea().getBottom(), getWidth(), bottomHeight }; }
    juce::Rectangle<int> namesSwitchArea() const { return { getWidth() - 62, bottomArea().getY() + 5, 52, 17 }; }

    std::int64_t barTicks() const { return std::max<std::int64_t> (1, engine.getSong().meter().bar()); }
    int tickToX (double t) const
    {
        const auto total = (double) std::max<std::int64_t> (1, state.strip.length());
        return juce::roundToInt (2.0 + t / total * (getWidth() - 4)); // FL's Zoom: every chord across the viewer
    }
    juce::Rectangle<int> blockArea (int i) const
    {
        const auto a = blocksArea();
        const auto x0 = tickToX ((double) state.strip.startOf ((std::size_t) i));
        const auto x1 = tickToX ((double) state.strip.startOf ((std::size_t) i + 1));
        return { x0 + 1, a.getY() + 1, juce::jmax (8, x1 - x0 - 1), a.getHeight() - 2 };
    }
    static juce::Rectangle<int> lockArea (juce::Rectangle<int> r) { return { r.getX() + 4, r.getY() + 4, 14, 14 }; }
    static juce::Rectangle<int> inversionArea (juce::Rectangle<int> r) { return { r.getRight() - 30, r.getY() + 4, 26, 14 }; }
    static juce::Rectangle<int> regenArea (juce::Rectangle<int> r) { return { r.getX() + 4, r.getBottom() - 18, 14, 14 }; }
    static juce::Rectangle<int> altArrowArea (juce::Rectangle<int> r) { return { r.getCentreX() - 8, r.getBottom() - 18, 16, 14 }; }
    static juce::Rectangle<int> bassArea (juce::Rectangle<int> r) { return { r.getRight() - 18, r.getBottom() - 18, 14, 14 }; }
    int blockAt (juce::Point<int> p) const
    {
        for (int i = 0; i < (int) state.strip.blocks.size(); ++i)
            if (blockArea (i).contains (p))
                return i;
        return -1;
    }

    // the keys the lines show: every block's notes, padded
    std::pair<int, int> keyRange() const
    {
        int low = 128, high = -1;
        for (const auto& b : state.strip.blocks)
        {
            for (auto k : chordstrip::mainKeys (b, state.octave))
                low = juce::jmin (low, k), high = juce::jmax (high, k);
            const auto bk = chordstrip::bassKey (b, state.octave);
            low = juce::jmin (low, bk), high = juce::jmax (high, bk);
        }
        if (high < low)
            return { 48, 72 };
        return { low - 2, high + 2 };
    }

    void setHover (juce::Point<int> p)
    {
        auto part = Part::none;
        int index = -1;
        if (spannerArea().contains (p))
            part = Part::spanner;
        else if (generateArea().contains (p))
            part = Part::generate;
        else if (tabTextArea().contains (p))
            part = Part::tab;
        else
            for (int m = 0; m < 3; ++m)
                if (modeArea (m).contains (p))
                    part = Part::mode, index = m;
        if (part == Part::none)
            if (const auto i = blockAt (p); i >= 0)
                part = Part::block, index = i;
        if (part != hovered || index != hoveredIndex)
        {
            hovered = part;
            hoveredIndex = index;
            repaint();
        }
    }

    // --- painting ------------------------------------------------------------------------------------
    void paintBlock (juce::Graphics& g, int i, int low, int high) const
    {
        using namespace theme::colour;
        const auto& b = state.strip.blocks[(std::size_t) i];
        const auto r = blockArea (i);
        const auto rf = r.toFloat();
        const auto isHovered = hovered == Part::block && hoveredIndex == i;
        theme::fillRoundRect (g, rf, theme::radius::sm, isHovered ? bg6 : bg5);

        // lock (top left)
        drawLock (g, lockArea (r).toFloat(), b.locked ? textPrimary : textDisabled, b.locked);
        // inversion "⇕ n" (top right)
        {
            const auto n = (int) juce::jmax<std::size_t> (1, b.chord.intervals.size());
            const auto inv = ((b.inversion % n) + n) % n;
            const auto a = inversionArea (r).toFloat();
            juce::Path arrows;
            const auto cx = a.getX() + 6.0f, cy = a.getCentreY();
            arrows.addTriangle (cx - 3.0f, cy - 1.5f, cx + 3.0f, cy - 1.5f, cx, cy - 5.5f);
            arrows.addTriangle (cx - 3.0f, cy + 1.5f, cx + 3.0f, cy + 1.5f, cx, cy + 5.5f);
            g.setColour (textPrimary);
            g.fillPath (arrows);
            g.setFont (theme::font (theme::type::caption, true));
            g.drawText (juce::String (inv), a.withTrimmedLeft (12.0f).toNearestInt(), juce::Justification::centredLeft, false);
        }

        // the notes as lines: main notes above, the bass line under them
        const auto lines = rf.reduced (6.0f, 0.0f).withTrimmedTop (22.0f).withTrimmedBottom (24.0f);
        const auto keyY = [&] (int key) {
            return lines.getBottom() - (float) (key - low) / (float) juce::jmax (1, high - low) * lines.getHeight();
        };
        const auto lineColour = theme::noteColour (0.78f);
        if (b.main)
            for (auto k : chordstrip::mainKeys (b, state.octave))
            {
                g.setColour (lineColour);
                g.fillRect (lines.getX(), juce::jlimit (lines.getY(), lines.getBottom() - 2.0f, keyY (k) - 1.0f), lines.getWidth(), 2.0f);
            }
        if (b.bass && (bassOn == nullptr || bassOn()))
        {
            g.setColour (lineColour.withMultipliedAlpha (0.85f));
            g.fillRect (lines.getX(), juce::jlimit (lines.getY(), lines.getBottom() - 2.0f, keyY (chordstrip::bassKey (b, state.octave)) - 1.0f), lines.getWidth(), 2.0f);
        }

        // the name in its label
        {
            const auto name = juce::String::fromUTF8 (chordstrip::blockName (state.strip, b, state.relativeNames).c_str());
            g.setFont (theme::font (theme::type::title, false));
            const auto w = juce::jmin ((int) rf.getWidth() - 8, juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), name) + 10);
            const auto label = juce::Rectangle<float> ((float) w, 22.0f).withCentre ({ rf.getCentreX(), rf.getY() + rf.getHeight() * 0.45f });
            theme::fillRoundRect (g, label, theme::radius::sm, bg3);
            g.setColour (textPrimary);
            g.drawFittedText (name, label.toNearestInt(), juce::Justification::centred, 1, 0.7f);
        }

        // bottom: Regenerate, Show alternatives, Bass
        icons::draw (g, Icon::loop, regenArea (r).toFloat(), textSecondary);
        {
            const auto a = altArrowArea (r).toFloat();
            juce::Path v;
            v.addTriangle (a.getCentreX() - 4.0f, a.getCentreY() - 2.0f, a.getCentreX() + 4.0f, a.getCentreY() - 2.0f, a.getCentreX(), a.getCentreY() + 3.0f);
            g.setColour (textPrimary);
            g.fillPath (v);
        }
        drawBassClef (g, bassArea (r).toFloat(), b.bass ? textPrimary : textDisabled);

        // selected: green outline (FL), red while learning from MIDI
        if (state.selected.contains (i))
        {
            g.setColour (state.learning ? record : ledOn);
            g.drawRoundedRectangle (rf.reduced (1.0f), theme::radius::sm, 2.0f);
        }
    }

    static void drawLock (juce::Graphics& g, juce::Rectangle<float> a, juce::Colour c, bool closed)
    {
        const auto body = juce::Rectangle<float> (a.getX() + 2.0f, a.getY() + a.getHeight() * 0.45f, a.getWidth() - 4.0f, a.getHeight() * 0.5f);
        g.setColour (c);
        g.fillRoundedRectangle (body, 1.5f);
        juce::Path shackle;
        const auto w = body.getWidth() * 0.62f;
        const auto x = body.getCentreX() - w * 0.5f;
        shackle.startNewSubPath (x, body.getY());
        shackle.lineTo (x, a.getY() + 3.5f);
        shackle.quadraticTo (body.getCentreX(), a.getY() - 0.5f, x + w, a.getY() + 3.5f);
        shackle.lineTo (x + w, closed ? body.getY() : a.getY() + 5.0f);
        g.strokePath (shackle, juce::PathStrokeType (1.5f));
    }

    static void drawBassClef (juce::Graphics& g, juce::Rectangle<float> a, juce::Colour c)
    {
        // a bass clef: a hook curling down to the left, and two dots on its right
        juce::Path p;
        const auto x = a.getX() + 1.5f, y = a.getY() + 2.0f, h = a.getHeight() - 3.0f;
        p.startNewSubPath (x + 1.5f, y + h * 0.32f);
        p.cubicTo (x + 1.0f, y - 0.5f, x + h * 0.78f, y - 0.5f, x + h * 0.72f, y + h * 0.42f);
        p.cubicTo (x + h * 0.66f, y + h * 0.75f, x + h * 0.3f, y + h * 0.92f, x, y + h);
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.fillEllipse (x + 1.0f, y + h * 0.22f, 2.6f, 2.6f);
        g.fillEllipse (a.getRight() - 3.0f, y + h * 0.12f, 2.0f, 2.0f);
        g.fillEllipse (a.getRight() - 3.0f, y + h * 0.45f, 2.0f, 2.0f);
    }

    juce::Rectangle<int> altPanel() const
    {
        const auto r = blockArea (altBlock);
        return juce::Rectangle<int> (juce::jmax (0, r.getX()), blocksArea().getBottom() - altHeight, 3 * 96 + 4, altHeight)
            .constrainedWithin (getLocalBounds());
    }
    juce::Rectangle<int> altClose() const { const auto p = altPanel(); return { p.getRight() - 16, p.getY() + 2, 14, 14 }; }
    juce::Rectangle<int> altRefresh() const { const auto p = altPanel(); return { p.getRight() - 34, p.getY() + 2, 14, 14 }; }
    juce::Rectangle<int> altCell (int k) const
    {
        const auto p = altPanel();
        return { p.getX() + 2 + (k % 3) * 96, p.getY() + 20 + (k / 3) * 32, 94, 30 };
    }
    int altAt (juce::Point<int> pos) const
    {
        for (int k = 0; k < (int) alternativesList.size(); ++k)
            if (altCell (k).contains (pos))
                return k;
        return -1;
    }

    void paintAlternatives (juce::Graphics& g) const
    {
        using namespace theme::colour;
        const auto p = altPanel();
        theme::fillRoundRect (g, p.toFloat(), theme::radius::md, bg3);
        g.setColour (borderStrong);
        g.drawRoundedRectangle (p.toFloat().reduced (0.5f), theme::radius::md, 1.0f);
        g.setColour (textSecondary);
        g.setFont (theme::font (theme::type::caption));
        g.drawText (tr ("Select alternative chord (right click to preview)"), p.withHeight (18).withTrimmedLeft (5).withTrimmedRight (36),
                    juce::Justification::centredLeft, true);
        icons::draw (g, Icon::loop, altRefresh().toFloat(), textSecondary);
        icons::draw (g, Icon::close, altClose().toFloat(), textSecondary);
        for (int k = 0; k < (int) alternativesList.size(); ++k)
        {
            const auto c = altCell (k);
            theme::fillRoundRect (g, c.toFloat(), theme::radius::sm, bg5);
            chordstrip::Block b = state.strip.blocks[(std::size_t) altBlock];
            b.chord = alternativesList[(std::size_t) k];
            g.setColour (textPrimary);
            g.setFont (theme::font (theme::type::body));
            g.drawFittedText (juce::String::fromUTF8 (chordstrip::blockName (state.strip, b, state.relativeNames).c_str()), c, juce::Justification::centred, 1, 0.7f);
        }
    }

    // --- actions -------------------------------------------------------------------------------------
    void edited()
    {
        repaint();
        if (onChanged)
            onChanged();
    }

    chordstrip::GenerateOptions genOptions() const
    {
        chordstrip::GenerateOptions o;
        o.mode = state.mode;
        o.adventure = state.adventure;
        o.seed = state.seed;
        o.octave = state.octave;
        o.smoothVoicing = state.smooth;
        return o;
    }

    void regenerate (int i)
    {
        ++state.seed;
        auto& b = state.strip.blocks[(std::size_t) i];
        // (FL's single-chord regenerate: never the same chord again, the notes found as a guide unless Replace all)
        auto c = chordstrip::regenerateChord (state.strip, (std::size_t) i, state.adventure, state.seed, false, true,
                                              state.nonChord != chordstrip::NonChord::replaceAll ? &state.opening : nullptr);
        if (b.fixedBass >= 0)
            c.bass = -1;
        b.chord = c;
        edited();
    }

    void passing (int i)
    {
        ++state.seed;
        chordstrip::addPassingChord (state.strip, (std::size_t) i, genOptions());
        state.selected = { i };
        edited();
    }

    std::vector<chordstrip::Chord> altCandidates (int i) const
    {
        auto list = chordstrip::alternatives (state.strip, (std::size_t) i, 6 + (int) (state.seed % 3u));
        // (Refresh: the list starts further down the ranking)
        if (list.size() > 6)
            list.erase (list.begin(), list.begin() + (std::ptrdiff_t) (list.size() - 6));
        return list;
    }

    void showAlternatives (int i)
    {
        altBlock = i;
        alternativesList = altCandidates (i);
        repaint();
    }

    void hideAlternatives()
    {
        altBlock = -1;
        alternativesList.clear();
        repaint();
    }

    void deleteSelected()
    {
        if (state.selected.empty() || state.strip.blocks.size() <= 1)
            return;
        for (auto it = state.selected.rbegin(); it != state.selected.rend(); ++it)
            if (state.strip.blocks.size() > 1 && *it < (int) state.strip.blocks.size())
                state.strip.blocks.erase (state.strip.blocks.begin() + *it);
        state.selected.clear();
        hideAlternatives();
        edited();
    }

    void pasteInto (int i)
    {
        if (! state.clipboard || i < 0 || i >= (int) state.strip.blocks.size())
            return;
        const auto length = state.strip.blocks[(std::size_t) i].length;
        state.strip.blocks[(std::size_t) i] = *state.clipboard;
        state.strip.blocks[(std::size_t) i].length = length;
        edited();
    }

    // the dialog's chord types for Select chord › (FL's chord list, in LYNX's names)
    static const std::vector<std::pair<const char*, std::vector<int>>>& chordTypes()
    {
        static const std::vector<std::pair<const char*, std::vector<int>>> types {
            { TR_NOOP ("Major"), { 0, 4, 7 } }, { TR_NOOP ("Minor"), { 0, 3, 7 } }, { TR_NOOP ("Diminished"), { 0, 3, 6 } },
            { TR_NOOP ("Augmented"), { 0, 4, 8 } }, { TR_NOOP ("Sus2"), { 0, 2, 7 } }, { TR_NOOP ("Sus4"), { 0, 5, 7 } }, { "7", { 0, 4, 7, 10 } }, { "Maj7", { 0, 4, 7, 11 } },
            { "m7", { 0, 3, 7, 10 } }, { "m7b5", { 0, 3, 6, 10 } }, { "dim7", { 0, 3, 6, 9 } }, { "6", { 0, 4, 7, 9 } },
            { "m6", { 0, 3, 7, 9 } }, { "add9", { 0, 4, 7, 14 } }, { "madd9", { 0, 3, 7, 14 } }, { "9", { 0, 4, 7, 10, 14 } },
            { "m9", { 0, 3, 7, 10, 14 } }, { "Maj9", { 0, 4, 7, 11, 14 } },
        };
        return types;
    }

    // FL's Extra notes: how many notes over the triad (the key's seventh, ninth, eleventh)
    void setExtraNotes (int i, int count)
    {
        auto& c = state.strip.blocks[(std::size_t) i].chord;
        std::vector<int> triad (c.intervals.begin(), c.intervals.begin() + (std::ptrdiff_t) juce::jmin<std::size_t> (3, c.intervals.size()));
        const auto& steps = chordstrip::scaleSteps (state.strip.minor);
        const auto rel = chordstrip::pc (c.root - state.strip.tonic);
        const auto it = std::find (steps.begin(), steps.end(), rel);
        const auto diatonic = [&] (int degreesUp, int fallback) {
            if (it == steps.end())
                return fallback;
            const auto d = (int) (it - steps.begin());
            const auto v = chordstrip::pc (steps[(std::size_t) ((d + degreesUp) % 7)] - rel);
            return degreesUp >= 7 ? v + 12 : (v == 0 ? 11 : v);
        };
        const int extra[] = { diatonic (6, 10), diatonic (8, 2), diatonic (10, 5) };
        for (int k = 0; k < juce::jlimit (0, 3, count); ++k)
            triad.push_back (k == 0 ? extra[0] : extra[k] + 12);
        std::sort (triad.begin(), triad.end());
        triad.erase (std::unique (triad.begin(), triad.end()), triad.end());
        c.intervals = triad;
        edited();
    }

    juce::PopupMenu optionsMenu()
    {
        // FL's "Options for generation" (the spanner): Notes › Main, Bass; Non chord notes › four modes
        juce::PopupMenu m;
        m.addSectionHeader (tr ("Notes"));
        m.addItem (tr ("Main"), true, state.main, [this] { state.main = ! state.main; edited(); });
        m.addItem (tr ("Bass"), true, bassOn != nullptr && bassOn(), [this] {
            if (setBassOn)
                setBassOn (! (bassOn != nullptr && bassOn()));
            edited();
        });
        m.addSectionHeader (tr ("Non chord notes"));
        const std::pair<chordstrip::NonChord, const char*> modes[] = {
            { chordstrip::NonChord::replaceAll, TR_NOOP ("Replace all") }, { chordstrip::NonChord::replaceClashing, TR_NOOP ("Replace clashing") },
            { chordstrip::NonChord::keep, TR_NOOP ("Keep") }, { chordstrip::NonChord::snap, TR_NOOP ("Snap") }
        };
        for (const auto& [mode, name] : modes)
            m.addItem (tr (name), true, state.nonChord == mode, [this, mode = mode] {
                state.nonChord = mode;
                edited();
            });
        return m;
    }

    juce::PopupMenu keyMenu()
    {
        // FL's key tab menu: Edit › Root note / Scale, Randomize, Delete (the first tab can't be deleted)
        juce::PopupMenu edit, m;
        edit.addSectionHeader (tr ("Root note"));
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        for (int r = 0; r < 12; ++r)
            edit.addItem (names[r], true, state.strip.tonic == r, [this, r] { setKey (r, state.strip.minor); });
        edit.addSectionHeader (tr ("Scale"));
        edit.addItem (tr ("Major (Ionian)"), true, ! state.strip.minor, [this] { setKey (state.strip.tonic, false); });
        edit.addItem (tr ("Minor Natural (Aeolian)"), true, state.strip.minor, [this] { setKey (state.strip.tonic, true); });
        m.addSubMenu (tr ("Edit"), edit);
        m.addItem (tr ("Randomize"), [this] {
            juce::Random r ((juce::int64) ++state.seed * 31337);
            setKey (r.nextInt (12), r.nextBool());
        });
        m.addItem (tr ("Delete"), false, false, [] {});
        return m;
    }

    void setKey (int tonic, bool minor)
    {
        // the chords move with the key (their roots shift by the same interval); a mode change keeps the roots
        const auto delta = tonic - state.strip.tonic;
        for (auto& b : state.strip.blocks)
        {
            b.chord.root = chordstrip::pc (b.chord.root + delta);
            if (b.chord.bass >= 0)
                b.chord.bass = chordstrip::pc (b.chord.bass + delta);
            if (b.fixedBass >= 0)
                b.fixedBass = chordstrip::pc (b.fixedBass + delta);
        }
        state.strip.tonic = chordstrip::pc (tonic);
        state.strip.minor = minor;
        if (onKeyChanged)
            onKeyChanged();
        edited();
    }

    juce::PopupMenu chordMenu (int i)
    {
        // FL's ChordMenu, in FL's order and sections (shot f30-chordmenu)
        auto& b = state.strip.blocks[(std::size_t) i];
        juce::PopupMenu m;
        m.addItem (tr ("Preview"), [this, i] { preview ({ state.strip.blocks[(std::size_t) i] }); });
        m.addItem (tr ("Preview from this chord"), [this, i] { previewFrom (i); });
        m.addItem (tr ("Lock"), true, b.locked, [this, i] { auto& x = state.strip.blocks[(std::size_t) i]; x.locked = ! x.locked; repaint(); });
        m.addItem (tr ("Main notes"), true, b.main, [this, i] { auto& x = state.strip.blocks[(std::size_t) i]; x.main = ! x.main; edited(); });
        m.addItem (tr ("Bass note"), true, b.bass, [this, i] { auto& x = state.strip.blocks[(std::size_t) i]; x.bass = ! x.bass; edited(); });

        m.addSectionHeader (tr ("Change chord"));
        m.addItem (tr ("Regenerate"), [this, i] { regenerate (i); });
        m.addItem (tr ("Show alternatives..."), [this, i] { showAlternatives (i); });
        m.addItem (tr ("Add passing chord"), [this, i] { passing (i); });
        {
            juce::PopupMenu select;
            static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            select.addSectionHeader (tr ("Root note"));
            for (int r = 0; r < 12; ++r)
                select.addItem (names[r], true, b.chord.root == r, [this, i, r] {
                    auto& x = state.strip.blocks[(std::size_t) i];
                    x.chord.root = r;
                    x.chord.bass = -1;
                    edited();
                });
            select.addSectionHeader (tr ("Chord"));
            for (const auto& [name, intervals] : chordTypes())
                select.addItem (tr (name), true, b.chord.intervals == intervals, [this, i, iv = intervals] {
                    state.strip.blocks[(std::size_t) i].chord.intervals = iv;
                    edited();
                });
            m.addSubMenu (tr ("Select chord"), select);
        }
        m.addItem (tr ("Type in chord name..."), [this, i] { typeChordName (i); });
        m.addItem (tr ("Learn from MIDI input"), true, state.learning, [this, i] {
            state.selected = { i };
            const auto on = setLearning (! state.learning);
            if (onLearnToggled)
                onLearnToggled (on);
        });

        m.addSectionHeader (tr ("Change notes"));
        {
            juce::PopupMenu invert;
            invert.addItem (tr ("Up"), [this, i] { state.strip.blocks[(std::size_t) i].inversion++; edited(); });
            invert.addItem (tr ("Down"), [this, i] { state.strip.blocks[(std::size_t) i].inversion--; edited(); });
            m.addSubMenu (tr ("Invert"), invert);
        }
        {
            juce::PopupMenu t;
            t.addItem (tr ("One semitone up"), [this, i] { state.strip.blocks[(std::size_t) i].transpose += 1; edited(); });
            t.addItem (tr ("One semitone down"), [this, i] { state.strip.blocks[(std::size_t) i].transpose -= 1; edited(); });
            t.addItem (tr ("One octave up"), [this, i] { state.strip.blocks[(std::size_t) i].transpose += 12; edited(); });
            t.addItem (tr ("One octave down"), [this, i] { state.strip.blocks[(std::size_t) i].transpose -= 12; edited(); });
            t.addItem (tr ("Bass: one octave up"), [this, i] { state.strip.blocks[(std::size_t) i].bassTranspose += 1; edited(); });
            t.addItem (tr ("Bass: one octave down"), [this, i] { state.strip.blocks[(std::size_t) i].bassTranspose -= 1; edited(); });
            m.addSubMenu (tr ("Transpose"), t);
        }
        {
            juce::PopupMenu fixed;
            static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            fixed.addItem (tr ("None"), true, b.fixedBass < 0, [this, i] { state.strip.blocks[(std::size_t) i].fixedBass = -1; edited(); });
            for (int r = 0; r < 12; ++r)
                fixed.addItem (names[r], true, b.fixedBass == r, [this, i, r] { state.strip.blocks[(std::size_t) i].fixedBass = r; edited(); });
            m.addSubMenu (tr ("Fixed bass"), fixed);
        }
        {
            juce::PopupMenu extra;
            const auto now = (int) b.chord.intervals.size() - 3;
            extra.addItem (tr ("Remove 1 note"), now > 0, false, [this, i, now] { setExtraNotes (i, now - 1); });
            extra.addItem (tr ("None"), true, now <= 0, [this, i] { setExtraNotes (i, 0); });
            extra.addItem (tr ("1 note"), true, now == 1, [this, i] { setExtraNotes (i, 1); });
            extra.addItem (tr ("2 notes"), true, now == 2, [this, i] { setExtraNotes (i, 2); });
            extra.addItem (tr ("3 notes"), true, now == 3, [this, i] { setExtraNotes (i, 3); });
            m.addSubMenu (tr ("Extra notes"), extra);
        }
        {
            juce::PopupMenu v;
            const std::pair<chordstrip::Voicing, const char*> list[] = { { chordstrip::Voicing::block, TR_NOOP ("Block") }, { chordstrip::Voicing::open, TR_NOOP ("Open") },
                                                                          { chordstrip::Voicing::octave, TR_NOOP ("Octave") }, { chordstrip::Voicing::stacked, TR_NOOP ("Stacked") } };
            for (const auto& [voicing, name] : list)
                v.addItem (tr (name), true, b.voicing == voicing, [this, i, voicing = voicing] { state.strip.blocks[(std::size_t) i].voicing = voicing; edited(); });
            m.addSubMenu (tr ("Voicing"), v);
        }
        {
            juce::PopupMenu colour;
            for (int c = 0; c < 8; ++c)
                colour.addItem (juce::String (c + 1), true, b.colour == c, [this, i, c] { state.strip.blocks[(std::size_t) i].colour = c; edited(); });
            m.addSubMenu (tr ("Color"), colour);
        }

        m.addSectionHeader (tr ("Piano roll notes"));
        m.addItem (tr ("Snap existing notes"), [this, i] {
            if (onSnapExisting)
                onSnapExisting (i);
        });
        {
            juce::PopupMenu linking; // (FL's Replace / Link / Unlink: not built in this round)
            linking.addItem (tr ("Replace"), false, false, [] {});
            linking.addItem (tr ("Link"), false, false, [] {});
            linking.addItem (tr ("Unlink"), false, false, [] {});
            m.addSubMenu (tr ("Note linking"), linking, false);
        }

        m.addSectionHeader (tr ("Chord actions"));
        m.addItem (tr ("Copy"), [this, i] { state.clipboard = state.strip.blocks[(std::size_t) i]; });
        m.addItem (tr ("Paste"), state.clipboard.has_value(), false, [this, i] { pasteInto (i); });
        {
            juce::PopupMenu swap;
            swap.addItem (tr ("Left"), i > 0, false, [this, i] { std::swap (state.strip.blocks[(std::size_t) i], state.strip.blocks[(std::size_t) i - 1]); keepLengths (i - 1, i); });
            swap.addItem (tr ("Right"), i + 1 < (int) state.strip.blocks.size(), false, [this, i] { std::swap (state.strip.blocks[(std::size_t) i], state.strip.blocks[(std::size_t) i + 1]); keepLengths (i, i + 1); });
            m.addSubMenu (tr ("Swap with"), swap);
        }
        m.addItem (tr ("Delete"), state.strip.blocks.size() > 1, false, [this, i] {
            state.selected = { i };
            deleteSelected();
        });
        return m;
    }

    void keepLengths (int a, int b)
    {
        // a swap moves the chords, not the time grid
        std::swap (state.strip.blocks[(std::size_t) a].length, state.strip.blocks[(std::size_t) b].length);
        edited();
    }

    void typeChordName (int i)
    {
        auto* w = new juce::AlertWindow (tr ("Type in chord name..."), tr ("A chord name (C, Am7, F#m add9) or a Roman numeral (vi, IV)."),
                                         juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", juce::String::fromUTF8 (chordstrip::blockName (state.strip, state.strip.blocks[(std::size_t) i], state.relativeNames).c_str()));
        w->addButton (tr ("OK"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<ChordStripView> (this), w, i] (int result) {
            if (safe != nullptr && result == 1 && i < (int) safe->state.strip.blocks.size())
                if (const auto parsed = chordstrip::parseText (w->getTextEditorContents ("name").toStdString(), safe->state.strip.tonic, safe->state.strip.minor);
                    parsed && ! parsed->empty())
                {
                    safe->state.strip.blocks[(std::size_t) i].chord = parsed->front();
                    safe->edited();
                }
        }), true);
    }

public:
    std::function<void (bool)> onLearnToggled; // the menu's Learn from MIDI input (the dialog's tick box follows)
    std::function<void (const juce::String&)> onMessage; // a note for the dialog's info line
    std::function<void (int)> onSnapExisting;  // Snap existing notes for one block

    // --- preview -------------------------------------------------------------------------------------
    void preview (std::vector<chordstrip::Block> blocks)
    {
        releasePreview();
        previewQueue = std::move (blocks);
        playNextPreview();
    }
    void previewFrom (int i)
    {
        std::vector<chordstrip::Block> list (state.strip.blocks.begin() + juce::jlimit (0, (int) state.strip.blocks.size(), i), state.strip.blocks.end());
        preview (std::move (list));
    }

private:
    void playNextPreview()
    {
        releasePreview (false);
        if (previewQueue.empty())
        {
            updateTimer();
            return;
        }
        const auto b = previewQueue.front();
        previewQueue.erase (previewQueue.begin());
        auto keys = b.main ? chordstrip::mainKeys (b, state.octave) : std::vector<int> {};
        if (b.bass && (bassOn == nullptr || bassOn()))
            keys.insert (keys.begin(), chordstrip::bassKey (b, state.octave));
        for (auto k : keys)
            if (k >= 0 && k <= 127)
            {
                engine.sendLiveNote (channel, k, 0.78f, true);
                sounding.push_back (k);
            }
        previewTicksLeft = 18; // (about 0.6 s at 30 Hz per chord)
        updateTimer();
    }

    void releasePreview (bool clearQueue = true)
    {
        for (auto k : sounding)
            engine.sendLiveNote (channel, k, 0.0f, false);
        sounding.clear();
        if (clearQueue)
            previewQueue.clear();
    }

    void updateTimer()
    {
        const auto need = state.learning || ! sounding.empty() || ! previewQueue.empty();
        if (need && ! isTimerRunning())
            startTimerHz (30);
        else if (! need && isTimerRunning())
            stopTimer();
    }

public:
    // Learn from MIDI input (FL's learn_chord, core/ChordStrip.h learnBlock): the chord held at its fullest, once every
    // key is up, replaces the one selected block; the selection moves on only with "Auto learn next chord".
    bool learn (const std::vector<int>& keys)
    {
        if (state.selected.size() != 1)
            return false;
        const auto target = *state.selected.begin();
        if (target < 0 || target >= (int) state.strip.blocks.size())
            return false;
        const auto learned = chordstrip::learnBlock (state.strip.blocks[(std::size_t) target], keys, state.octave);
        if (! learned)
            return false;
        state.strip.blocks[(std::size_t) target] = *learned;
        if (state.autoLearnNext && target + 1 < (int) state.strip.blocks.size())
            state.selected = { target + 1 };
        edited();
        return true;
    }

private:
    void timerCallback() override
    {
        if (! sounding.empty() || ! previewQueue.empty())
            if (--previewTicksLeft <= 0)
                playNextPreview();

        if (state.learning)
        {
            const auto held = engine.heldInputKeys();
            if (! held.empty())
            {
                learnHeld = true;
                if (held.size() >= learnPeak.size())
                    learnPeak = held;
            }
            else if (learnHeld)
            {
                learnHeld = false;
                learn (learnPeak);
                learnPeak.clear();
            }
        }
        updateTimer();
    }

    AudioEngine& engine;
    ChordToolState& state;
    int channel = 0;
    Part hovered = Part::none;
    int hoveredIndex = -1;
    int altBlock = -1;
    std::vector<chordstrip::Chord> alternativesList;
    std::vector<chordstrip::Block> previewQueue;
    std::vector<int> sounding;
    int previewTicksLeft = 0;
    std::vector<int> learnPeak;
    bool learnHeld = false;
};
} // namespace lynx
