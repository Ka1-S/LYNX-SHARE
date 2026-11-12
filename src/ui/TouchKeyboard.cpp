#include "ui/TouchKeyboard.h"
#include "core/NoteNames.h"

#include <array>
#include <map>
#include <set>

#include "core/ChordReadout.h"
#include "core/HeldNotes.h"
#include "core/ProjectKey.h"
#include "core/ScaleKeys.h"
#include "core/TouchKeyboard.h"
#include "engine/AudioEngine.h"
#include "ui/I18n.h"
#include "ui/SettingsPages.h"
#include "ui/Theme.h"
#include "ui/ToolWindow.h"

namespace lynx
{
namespace
{
juce::String keyName (int key)
{
    return juce::String (notenames::keyName (key)); // Settings › General › Note names (round 165f)
}

juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

// "A minor", "C Dorian" (the Key button's names)
juce::String projectKeyText (const Song& song)
{
    const auto key = projectkey::get (song);
    if (! key)
        return tr ("No key");
    return utf8 (projectkey::rootName (key->root)) + " " + tr (projectkey::shortScaleName (key->flScale).c_str());
}

// The scale selector on the keyboard (round 190ds, BandLab's "Scale" on the instrument, FL Studio Mobile's key locker):
// the project key, the same one the toolbar's Key button sets (one undo step)
void showKeyMenu (AudioEngine& engine, juce::Component* target)
{
    const auto current = projectkey::get (engine.getSong());
    auto set = [&engine] (std::optional<projectkey::Key> key) { engine.editSong ([key] (Song& s) { projectkey::set (s, key); }); };
    const auto order = projectkey::commonFirst();
    juce::PopupMenu menu;
    auto roots = [&] (int flScale) {
        juce::PopupMenu m;
        for (int root = 0; root < 12; ++root)
            m.addItem (utf8 (projectkey::rootName (root)), true, current && current->root == root && current->flScale == flScale,
                       [set, root, flScale] { set (projectkey::Key { root, flScale }); });
        return m;
    };
    menu.addSectionHeader (tr ("Project key"));
    menu.addSubMenu (tr ("Major"), roots (order[0]), true, nullptr, current && current->flScale == order[0]);
    menu.addSubMenu (tr ("Minor"), roots (order[1]), true, nullptr, current && current->flScale == order[1]);
    juce::PopupMenu more;
    for (std::size_t i = 2; i < order.size(); ++i)
    {
        const auto fl = order[i];
        more.addSubMenu (tr (projectkey::shortScaleName (fl).c_str()), roots (fl), true, nullptr, current && current->flScale == fl);
        if (i == 9)
            more.addSeparator();
    }
    menu.addSubMenu (tr ("Other scales"), more);
    menu.addSeparator();
    menu.addItem (tr ("No key"), true, ! current, [set] { set (std::nullopt); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target));
}

std::function<std::string (int)>& typingLetterSource()
{
    static std::function<std::string (int)> source;
    return source;
}

// Live notes of one of the play surfaces, recorded like the typing keyboard's; drawn pressed while held. With Sustain
// (round 190ds) a released note sounds on until Sustain goes off (or the same key is played again).
struct Player
{
    explicit Player (AudioEngine& e, std::function<void()> changedFn) : engine (e), changed (std::move (changedFn)), notes ([this] (int ch, int key, float velocity, bool on) {
                                                                          if (! on && sustain)
                                                                          {
                                                                              sustained.insert ({ ch, key });
                                                                              return;
                                                                          }
                                                                          if (on && sustained.erase ({ ch, key }) > 0)
                                                                              send (ch, key, 0.0f, false);
                                                                          send (ch, key, velocity, on);
                                                                      })
    {
    }
    ~Player()
    {
        notes.releaseAll();
        sustain = false;
        flush();
    }
    void send (int ch, int key, float velocity, bool on)
    {
        if (live)
            engine.sendLiveNote (ch, key, velocity, on, true);
        if (on)
            shown[key] = velocity;
        else
            shown.erase (key);
        if (changed)
            changed();
    }
    // the notes Sustain holds stop
    void flush()
    {
        const auto held = sustained;
        sustained.clear();
        for (const auto& [ch, key] : held)
            send (ch, key, 0.0f, false);
    }
    void setSustain (bool on)
    {
        sustain = on;
        if (! on)
            flush();
    }
    AudioEngine& engine;
    std::function<void()> changed;
    std::set<std::pair<int, int>> sustained;
    bool sustain = false;
    HeldNotes notes;
    std::map<int, float> shown; // key → velocity
    bool live = true;
};

class Keys final : public juce::Component
{
    AudioEngine& engine;
    Player player;

public:
    std::function<int()> channel;              // the channel to play (-1: none)
    std::function<float (float)> fixedVelocity; // returns < 0 when the position decides
    bool labels = true;
    touchkb::Layout layout;
    std::map<int, float>& shown;               // key → velocity, for drawing (also the snapshot's pressed keys)

    explicit Keys (AudioEngine& e) : engine (e), player (e, [this] { repaint(); }), shown (player.shown)
    {
        setRepaintsOnMouseActivity (false);
    }



    void setLive (bool on) { player.live = on; }
    void setSustain (bool on) { player.setSustain (on); }
    // Keys held on the typing piano or a MIDI input (AudioEngine::heldInputKeyBits, all channels): drawn pressed too.
    void setHeldElsewhere (std::array<std::uint64_t, 2> bits)
    {
        if (bits == elsewhere)
            return;
        elsewhere = bits;
        repaint();
    }
    void releaseAll()
    {
        player.notes.releaseAll();
        player.flush();
    }

    void resized() override
    {
        layout.width = (float) getWidth();
        layout.height = (float) getHeight();
    }

    // the typing key that plays `key` now ("" when the typing keyboard is off or no key plays it)
    static std::string typingLetter (int key)
    {
        const auto& source = typingLetterSource();
        return source ? source (key) : std::string();
    }

    // Lock to key (round 190ds): the keys outside the project key don't play
    bool locked (int key) const
    {
        if (! scalekeys::current().lockToKey)
            return false;
        const auto k = projectkey::get (engine.getSong());
        return k && ! projectkey::contains (*k, key);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg0);
        const auto top = layout.lowest, last = layout.highest();
        // the keys held here, and those held elsewhere: the computer keys or a MIDI keyboard (round 190t)
        auto pressed = shown;
        for (int key = 0; key < 128; ++key)
            if ((elsewhere[(std::size_t) (key >> 6)] >> (key & 63) & 1u) != 0)
                pressed.emplace (key, 0.8f);
        auto draw = [&] (bool black) {
            for (int key = top; key <= last + 1 && key <= 127; ++key)
            {
                if (touchkb::isBlack (key) != black || (key > last && ! black))
                    continue;
                const auto r = layout.keyRect (key);
                if (r.x + r.w > layout.width + 0.5f)
                    continue;
                auto box = juce::Rectangle<float> (r.x, r.y, r.w, r.h);
                const auto held = pressed.find (key);
                const auto off = locked (key);
                if (! black)
                {
                    box = box.reduced (1.0f, 0.0f).withTrimmedBottom (1.0f);
                    auto fill = held != pressed.end() ? theme::colour::keyWhite.interpolatedWith (theme::colour::accent, 0.35f + 0.5f * held->second)
                                                      : theme::colour::keyWhite;
                    g.setColour (off ? fill.withMultipliedBrightness (0.45f) : fill);
                    g.fillRoundedRectangle (box, 3.0f);
                    if (labels && (key % 12 == 0 || held != pressed.end()))
                    {
                        g.setColour (held != pressed.end() ? theme::colour::onAccent : juce::Colour (0xff6b707a));
                        g.setFont (theme::font (theme::type::caption, key % 12 == 0));
                        g.drawText (keyName (key), box.withTrimmedBottom (6.0f).toNearestInt(), juce::Justification::centredBottom);
                    }
                    if (const auto letter = typingLetter (key); ! letter.empty()) // the typing key that plays it (round 190ds)
                    {
                        g.setColour (juce::Colour (0xff8a8f99));
                        g.setFont (theme::monoFont (theme::type::caption));
                        g.drawText (letter, box.withTrimmedTop (box.getHeight() * 0.64f).withHeight (16.0f).toNearestInt(), juce::Justification::centred);
                    }
                }
                else
                {
                    g.setColour (held != pressed.end() ? theme::colour::accent.withMultipliedBrightness (0.6f + 0.4f * held->second)
                                                       : off ? theme::colour::bg4 : theme::colour::keyBlack); // (locked out: a flat grey)
                    g.fillRoundedRectangle (box.withTrimmedTop (-3.0f), 3.0f);
                    g.setColour (juce::Colours::white.withAlpha (off ? 0.02f : 0.07f));
                    g.fillRect (box.reduced (3.0f, 0.0f).withTrimmedTop (box.getHeight() - 6.0f));
                    if (const auto letter = typingLetter (key); ! letter.empty())
                    {
                        g.setColour (juce::Colour (0xffa1a6b0));
                        g.setFont (theme::monoFont (theme::type::caption));
                        g.drawText (letter, box.withTrimmedTop (box.getHeight() - 22.0f).withTrimmedBottom (6.0f).toNearestInt(), juce::Justification::centred);
                    }
                }
            }
        };
        draw (false);
        draw (true);
        // the window's top edge over the keys
        g.setColour (theme::colour::accent.withAlpha (0.8f));
        g.fillRect (0, 0, getWidth(), 2);
    }

    void mouseDown (const juce::MouseEvent& e) override { touch (e); }
    void mouseDrag (const juce::MouseEvent& e) override { touch (e); }
    void mouseUp (const juce::MouseEvent& e) override { player.notes.releaseTag (e.source.getIndex()); }
    void mouseExit (const juce::MouseEvent& e) override
    {
        if (! e.mods.isAnyMouseButtonDown())
            player.notes.releaseTag (e.source.getIndex());
    }
    void focusLost (FocusChangeType) override { player.notes.releaseAll(); }
    void visibilityChanged() override
    {
        if (! isShowing())
            player.notes.releaseAll();
    }
    void parentHierarchyChanged() override
    {
        if (! isShowing())
            player.notes.releaseAll();
    }

private:
    void touch (const juce::MouseEvent& e)
    {
        const auto tag = e.source.getIndex();
        const auto ch = channel ? channel() : -1;
        const auto pos = e.position;
        auto key = getLocalBounds().toFloat().contains (pos) ? layout.noteAt (pos.x, pos.y) : -1;
        if (key >= 0 && locked (key))
            key = -1;
        const auto currentKey = keyOf (tag);

        if (key < 0 || ch < 0)
        {
            player.notes.releaseTag (tag); // dragged off the keys
            return;
        }
        if (key == currentKey)
            return;

        player.notes.releaseTag (tag); // glide: the previous key stops before the next starts
        const auto fixed = fixedVelocity ? fixedVelocity (0.0f) : -1.0f;
        player.notes.press (ch, key, fixed >= 0.0f ? fixed : layout.velocityAt (key, pos.y), tag);
        tagKeys[tag] = key;
    }

    int keyOf (int tag) const
    {
        const auto it = tagKeys.find (tag);
        return it != tagKeys.end() && player.notes.hasTag (tag) ? it->second : -1;
    }

    std::map<int, int> tagKeys;
    std::array<std::uint64_t, 2> elsewhere {};
};

// "Set a key first" over Scale keys and Chord pad when the project has no key
void paintNoKey (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (theme::colour::textSecondary);
    g.setFont (theme::font (theme::type::title));
    g.drawText (tr ("Set a key first: Scale keys and Chord pad play in the project key"), area.withTrimmedBottom (40), juce::Justification::centred, true);
}

// Scale keys (round 190ds): one wide key per degree of the project key over two octaves, from the root
class ScaleKeysView final : public juce::Component
{
    AudioEngine& engine;
    Player player;

public:
    std::function<int()> channel;
    std::function<float()> velocity; // < 0: where the key is touched (top soft, bottom loud)
    std::function<int()> octaveBase; // the lowest C (the keyboard's octave)
    bool labels = true;
    std::map<int, float>& shown;

    explicit ScaleKeysView (AudioEngine& e) : engine (e), player (e, [this] { repaint(); }), shown (player.shown) { setRepaintsOnMouseActivity (false); }
    void setLive (bool on) { player.live = on; }
    void setSustain (bool on) { player.setSustain (on); }
    void releaseAll()
    {
        player.notes.releaseAll();
        player.flush();
    }

    int keyCount() const
    {
        const auto key = projectkey::get (engine.getSong());
        return key ? 2 * scalekeys::degreeCount (*key) + 1 : 0; // two octaves and the top root
    }
    int noteOf (int index) const
    {
        const auto key = projectkey::get (engine.getSong());
        if (! key)
            return -1;
        const auto n = scalekeys::scaleNote (*key, index, scalekeys::rootAtOrAbove (*key, octaveBase ? octaveBase() : 48));
        return n >= 0 && n <= 127 ? n : -1;
    }
    int indexAt (juce::Point<float> p) const
    {
        const auto count = keyCount();
        if (count <= 0 || ! getLocalBounds().toFloat().contains (p))
            return -1;
        return juce::jlimit (0, count - 1, (int) (p.x / ((float) getWidth() / (float) count)));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg0);
        const auto key = projectkey::get (engine.getSong());
        if (! key)
        {
            paintNoKey (g, getLocalBounds());
            return;
        }
        const auto count = keyCount();
        const auto w = (float) getWidth() / (float) count;
        const auto n = scalekeys::degreeCount (*key);
        for (int i = 0; i < count; ++i)
        {
            const auto note = noteOf (i);
            auto box = juce::Rectangle<float> ((float) i * w, 0.0f, w, (float) getHeight()).reduced (2.0f, 3.0f);
            const auto held = shown.find (note);
            const auto root = i % n == 0;
            auto fill = root ? theme::colour::accentMuted.brighter (0.25f) : theme::colour::bg4;
            if (held != shown.end())
                fill = theme::colour::accent.withMultipliedBrightness (0.7f + 0.3f * held->second);
            g.setColour (fill);
            g.fillRoundedRectangle (box, 6.0f);
            g.setColour (root ? theme::colour::accent.withAlpha (0.7f) : theme::colour::borderStrong);
            g.drawRoundedRectangle (box, 6.0f, 1.0f);
            if (! labels || note < 0)
                continue;
            g.setColour (theme::colour::textPrimary);
            g.setFont (theme::font (box.getWidth() > 44.0f ? theme::type::title : theme::type::small, true));
            g.drawText (keyName (note), box.withTrimmedBottom (box.getHeight() * 0.45f).toNearestInt(), juce::Justification::centredBottom, false);
            g.setColour (theme::colour::textSecondary);
            g.setFont (theme::font (theme::type::caption));
            g.drawText (juce::String ((i % n) + 1), box.withTrimmedTop (box.getHeight() * 0.55f).withHeight (16.0f).toNearestInt(),
                        juce::Justification::centredTop, false);
            if (const auto* letter = scalekeys::typingLabel (i); *letter != 0)
            {
                auto chip = box.removeFromBottom (24.0f).reduced (box.getWidth() * 0.5f - 11.0f, 3.0f);
                g.setColour (theme::colour::bg1.withAlpha (0.8f));
                g.fillRoundedRectangle (chip, 3.0f);
                g.setColour (theme::colour::textSecondary);
                g.setFont (theme::monoFont (theme::type::caption));
                g.drawText (letter, chip.toNearestInt(), juce::Justification::centred, false);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override { touch (e); }
    void mouseDrag (const juce::MouseEvent& e) override { touch (e); }
    void mouseUp (const juce::MouseEvent& e) override { player.notes.releaseTag (e.source.getIndex()); }
    void visibilityChanged() override
    {
        if (! isShowing())
            player.notes.releaseAll();
    }

private:
    void touch (const juce::MouseEvent& e)
    {
        const auto tag = e.source.getIndex();
        const auto ch = channel ? channel() : -1;
        const auto i = indexAt (e.position);
        const auto note = i >= 0 ? noteOf (i) : -1;
        if (note < 0 || ch < 0)
        {
            player.notes.releaseTag (tag);
            return;
        }
        if (const auto it = tagKeys.find (tag); it != tagKeys.end() && it->second == note && player.notes.hasTag (tag))
            return;
        player.notes.releaseTag (tag);
        const auto v = velocity ? velocity() : -1.0f;
        player.notes.press (ch, note, v >= 0.0f ? v : 0.2f + 0.8f * std::sqrt (juce::jlimit (0.0f, 1.0f, e.position.y / (float) juce::jmax (1, getHeight()))), tag);
        tagKeys[tag] = note;
    }

    std::map<int, int> tagKeys;
};

// Chord pad (round 190ds): one pad per degree (the diatonic chords of the project key) or per chord of the project's
// chord set; a pad plays its chord, strummed if asked; a right-click edits it
class ChordPadView final : public juce::Component,
                           private juce::Timer
{
    AudioEngine& engine;
    Player player;

public:
    std::function<int()> channel;
    std::function<float()> velocity;
    std::function<int()> octaveBase;
    std::map<int, float>& shown;

    explicit ChordPadView (AudioEngine& e) : engine (e), player (e, [this] { repaint(); }), shown (player.shown) { setRepaintsOnMouseActivity (true); }
    ~ChordPadView() override { stopTimer(); }
    void setLive (bool on) { player.live = on; }
    void setSustain (bool on) { player.setSustain (on); }
    void releaseAll()
    {
        pending.clear();
        player.notes.releaseAll();
        player.flush();
        pressedPad.clear();
    }

    int padCount() const { return scalekeys::padCount (engine.getSong(), scalekeys::current()); }
    std::vector<int> notesOf (int pad) const { return scalekeys::padNotes (engine.getSong(), scalekeys::current(), pad, octaveBase ? octaveBase() : 48); }
    bool usable() const
    {
        const auto& s = scalekeys::current();
        return (s.projectChords && ! engine.getSong().keyboardChords.empty()) || projectkey::get (engine.getSong()).has_value();
    }
    int padAt (juce::Point<float> p) const
    {
        const auto n = padCount();
        if (n <= 0 || ! getLocalBounds().toFloat().contains (p))
            return -1;
        return juce::jlimit (0, n - 1, (int) (p.x / ((float) getWidth() / (float) n)));
    }
    // pads drawn held for the snapshot
    void showPressedForSnapshot (int pad)
    {
        pressedPad[0] = pad;
        for (auto n : notesOf (pad))
            shown[n] = 0.9f;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg0);
        if (! usable())
        {
            paintNoKey (g, getLocalBounds());
            return;
        }
        const auto n = padCount();
        const auto w = (float) getWidth() / (float) n;
        const auto key = projectkey::get (engine.getSong());
        const auto& settings = scalekeys::current();
        const bool fromSet = settings.projectChords && ! engine.getSong().keyboardChords.empty();
        for (int i = 0; i < n; ++i)
        {
            const auto notes = notesOf (i);
            auto box = juce::Rectangle<float> ((float) i * w, 0.0f, w, (float) getHeight()).reduced (4.0f, 5.0f);
            bool held = false;
            for (const auto& [tag, pad] : pressedPad)
                held = held || pad == i;
            const auto hover = isMouseOver() && padAt (getMouseXYRelative().toFloat()) == i;
            g.setColour (held ? theme::colour::accent : hover ? theme::colour::bg5 : theme::colour::bg4);
            g.fillRoundedRectangle (box, 8.0f);
            g.setColour (held ? theme::colour::accentHover : theme::colour::borderStrong);
            g.drawRoundedRectangle (box, 8.0f, 1.2f);
            if (notes.empty())
                continue;
            auto text = box.reduced (6.0f, 8.0f);
            // the numeral against the key (diatonic pads), then the chord's name, then its typing key
            int degree = i;
            if (fromSet && key)
                degree = (int) std::distance (projectkey::intervals (*key).begin(),
                                              std::find (projectkey::intervals (*key).begin(), projectkey::intervals (*key).end(),
                                                         scalekeys::wrap12 (notes.front() - key->root)));
            g.setColour (held ? theme::colour::onAccent : theme::colour::textPrimary);
            g.setFont (theme::font (theme::type::display, true));
            const auto numeral = fromSet && (! key || degree >= (int) projectkey::intervals (*key).size()) ? juce::String() : utf8 (scalekeys::numeral (degree, notes));
            g.drawText (numeral, text.removeFromTop (text.getHeight() * 0.42f).toNearestInt(), juce::Justification::centredBottom, false);
            chordread::NameOptions options;
            const auto name = chordread::nameKeys (notes, options);
            g.setColour (held ? theme::colour::onAccent : theme::colour::textSecondary);
            g.setFont (theme::font (theme::type::title));
            g.drawText (utf8 (name.novelty ? std::string() : name.text), text.removeFromTop (24.0f).toNearestInt(), juce::Justification::centred, true);
            if (const auto* letter = scalekeys::typingLabel (i); *letter != 0)
            {
                auto chip = text.removeFromBottom (22.0f).withSizeKeepingCentre (24.0f, 20.0f);
                g.setColour (theme::colour::bg1.withAlpha (0.8f));
                g.fillRoundedRectangle (chip, 3.0f);
                g.setColour (theme::colour::textSecondary);
                g.setFont (theme::monoFont (theme::type::caption));
                g.drawText (letter, chip.toNearestInt(), juce::Justification::centred, false);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (const auto pad = padAt (e.position); pad >= 0)
                editMenu (pad);
            return;
        }
        touch (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu())
            touch (e);
    }
    void mouseUp (const juce::MouseEvent& e) override { release (e.source.getIndex()); }
    void visibilityChanged() override
    {
        if (! isShowing())
            releaseAll();
    }

    juce::PopupMenu editMenuForSnapshot (int pad) { return buildEditMenu (pad); }

private:
    void touch (const juce::MouseEvent& e)
    {
        const auto tag = e.source.getIndex();
        const auto pad = usable() ? padAt (e.position) : -1;
        const auto ch = channel ? channel() : -1;
        if (pad < 0 || ch < 0)
        {
            release (tag);
            return;
        }
        if (const auto it = pressedPad.find (tag); it != pressedPad.end() && it->second == pad)
            return;
        release (tag);
        pressedPad[tag] = pad;
        const auto v = velocity ? velocity() : -1.0f;
        const auto vel = v >= 0.0f ? v : 0.8f;
        const auto now = juce::Time::getMillisecondCounterHiRes();
        for (const auto& [note, delay] : scalekeys::strummed (notesOf (pad), scalekeys::current()))
        {
            if (delay <= 0)
                player.notes.press (ch, note, vel, tag);
            else
                pending.push_back ({ now + delay, ch, note, vel, tag });
        }
        if (! pending.empty() && ! isTimerRunning())
            startTimer (2);
        repaint();
    }

    void release (int tag)
    {
        pending.erase (std::remove_if (pending.begin(), pending.end(), [tag] (const Pending& p) { return p.tag == tag; }), pending.end());
        player.notes.releaseTag (tag);
        if (pressedPad.erase (tag) > 0)
            repaint();
    }

    void timerCallback() override
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        for (std::size_t i = 0; i < pending.size();)
            if (pending[i].at <= now)
            {
                const auto p = pending[i];
                pending.erase (pending.begin() + (std::ptrdiff_t) i);
                player.notes.press (p.channel, p.note, p.velocity, p.tag);
            }
            else
                ++i;
        if (pending.empty())
            stopTimer();
    }

    juce::PopupMenu buildEditMenu (int pad)
    {
        juce::PopupMenu menu;
        const auto& song = engine.getSong();
        const auto key = projectkey::get (song);
        auto& settings = scalekeys::current();
        const bool fromSet = settings.projectChords && ! song.keyboardChords.empty();
        // the set the edit starts from: the project's, or the key's diatonic triads
        auto base = fromSet ? song.keyboardChords : key ? scalekeys::diatonicSet (*key) : std::vector<KeyboardChord> {};
        if (pad < 0 || pad >= (int) base.size())
            return menu;
        menu.addSectionHeader (tr ("Edit chord"));
        juce::PopupMenu roots, qualities;
        auto apply = [this, base, pad] (int root, int quality) {
            auto set = base;
            if (root >= 0)
                set[(std::size_t) pad].root = root;
            if (quality >= 0)
                set[(std::size_t) pad].quality = quality;
            set[(std::size_t) pad].extensions.clear();
            engine.editSong ([set] (Song& s) { s.keyboardChords = set; });
            scalekeys::current().projectChords = true;
            repaint();
        };
        for (int r = 0; r < 12; ++r)
            roots.addItem (utf8 (projectkey::rootName (r)), true, base[(std::size_t) pad].root == r, [apply, r] { apply (r, -1); });
        for (int q = 0; q < scalekeys::qualityCount; ++q)
            qualities.addItem (tr (scalekeys::qualityName (q)), true, base[(std::size_t) pad].quality == q, [apply, q] { apply (-1, q); });
        menu.addSubMenu (tr ("Root"), roots);
        menu.addSubMenu (tr ("Chord type"), qualities);
        menu.addSeparator();
        menu.addItem (tr ("Add a pad"), fromSet && song.keyboardChords.size() < 24, false, [this] {
            auto set = engine.getSong().keyboardChords;
            set.push_back (set.empty() ? KeyboardChord {} : set.back());
            engine.editSong ([set] (Song& s) { s.keyboardChords = set; });
        });
        menu.addItem (tr ("Remove this pad"), fromSet && song.keyboardChords.size() > 1, false, [this, pad] {
            auto set = engine.getSong().keyboardChords;
            if (pad < (int) set.size())
                set.erase (set.begin() + pad);
            engine.editSong ([set] (Song& s) { s.keyboardChords = set; });
        });
        menu.addItem (tr ("Back to the key's chords"), fromSet, false, [this] {
            scalekeys::current().projectChords = false;
            repaint();
        });
        return menu;
    }

    void editMenu (int pad)
    {
        auto menu = buildEditMenu (pad);
        if (menu.getNumItems() > 0)
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
    }

    struct Pending
    {
        double at;
        int channel, note;
        float velocity;
        int tag;
    };
    std::vector<Pending> pending;
    std::map<int, int> pressedPad; // tag → pad
};

class TouchKeyboardPanel final : public juce::Component,
                                 private juce::Timer
{
public:
    TouchKeyboardPanel (AudioEngine& e, bool liveMode, std::function<int()> channelFn = {})
        : engine (e), keys (e), scaleKeys (e), pads (e), channelOf (std::move (channelFn))
    {
        const auto ch = [this] { return channelOf ? channelOf() : engine.getSelectedChannel(); };
        const auto vel = [this] { return velocityMode.getSelectedItemIndex() == 0 ? -1.0f : (float) fixedVelocity.getValue() / 100.0f; };
        const auto base = [this] { return keys.layout.lowest; };
        keys.setLive (liveMode);
        keys.channel = ch;
        keys.fixedVelocity = [vel] (float) { return vel(); };
        scaleKeys.setLive (liveMode);
        scaleKeys.channel = ch;
        scaleKeys.velocity = vel;
        scaleKeys.octaveBase = base;
        pads.setLive (liveMode);
        pads.channel = ch;
        pads.velocity = [this] { return velocityMode.getSelectedItemIndex() == 0 ? 0.8f : (float) fixedVelocity.getValue() / 100.0f; };
        pads.octaveBase = base;
        addAndMakeVisible (keys);
        addChildComponent (scaleKeys);
        addChildComponent (pads);

        // the mode switch (round 190ds)
        const juce::String modeNames[] { tr ("Keys"), tr ("Scale keys"), tr ("Chord pad") };
        for (int i = 0; i < 3; ++i)
        {
            auto& b = modeButtons[(std::size_t) i];
            b.setButtonText (modeNames[i]);
            b.setClickingTogglesState (true);
            b.setRadioGroupId (19045);
            b.setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < 2 ? juce::Button::ConnectedOnRight : 0));
            b.onClick = [this, i] {
                if (modeButtons[(std::size_t) i].getToggleState())
                    setMode ((scalekeys::Mode) i);
            };
            addAndMakeVisible (b);
        }
        modeButtons[0].setTooltip (tr ("The usual piano"));
        modeButtons[1].setTooltip (tr ("One key per note of the project key: nothing out of key can be played. The typing keys Z X C V... play the notes of the scale"));
        modeButtons[2].setTooltip (tr ("One pad per chord of the project key. The typing keys Z X C V... play the chords; right-click a pad to change its chord"));
        modeButtons[2].setComponentID ("keyboard.chordPad"); // (round 190dv: the first-song tour's ring)
        keyButton.setComponentID ("keyboard.key");
        keyButton.setTooltip (tr ("The project key (the same as the toolbar's Key)"));
        keyButton.onClick = [this] { showKeyMenu (engine, &keyButton); };
        addAndMakeVisible (keyButton);

        octaveDown.setTooltip (tr ("One octave down"));
        octaveUp.setTooltip (tr ("One octave up"));
        octaveDown.onClick = [this] { shift (-1); };
        octaveUp.onClick = [this] { shift (1); };
        addAndMakeVisible (octaveDown);
        addAndMakeVisible (octaveUp);
        range.setFont (theme::monoFont (theme::type::small));
        range.setJustificationType (juce::Justification::centred);
        range.setColour (juce::Label::textColourId, theme::colour::textPrimary);
        addAndMakeVisible (range);
        sustain.setButtonText (tr ("Sustain"));
        sustain.setTooltip (tr ("Released notes sound on until Sustain is switched off"));
        sustain.onClick = [this] {
            keys.setSustain (sustain.getToggleState());
            scaleKeys.setSustain (sustain.getToggleState());
            pads.setSustain (sustain.getToggleState());
        };
        addAndMakeVisible (sustain);

        sizeBox.addItemList ({ tr ("Small keys"), tr ("Medium keys"), tr ("Large keys") }, 1);
        sizeBox.setSelectedItemIndex (1, juce::dontSendNotification);
        sizeBox.onChange = [this] { layoutKeys(); };
        addAndMakeVisible (sizeBox);
        velocityMode.addItemList ({ tr ("Velocity: where you touch"), tr ("Velocity: fixed") }, 1);
        velocityMode.setSelectedItemIndex (0, juce::dontSendNotification);
        velocityMode.setTooltip (tr ("Where you touch: near the top of a key plays soft, near the bottom loud"));
        velocityMode.onChange = [this] { fixedVelocity.setEnabled (velocityMode.getSelectedItemIndex() == 1); };
        addAndMakeVisible (velocityMode);
        fixedVelocity.setSliderStyle (juce::Slider::LinearHorizontal);
        fixedVelocity.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
        fixedVelocity.setRange (1.0, 100.0, 1.0);
        fixedVelocity.setValue (80.0, juce::dontSendNotification);
        fixedVelocity.setEnabled (false);
        addAndMakeVisible (fixedVelocity);
        labels.setButtonText (tr ("Note names"));
        labels.setToggleState (true, juce::dontSendNotification);
        labels.onClick = [this] {
            keys.labels = scaleKeys.labels = labels.getToggleState();
            keys.repaint();
            scaleKeys.repaint();
        };
        addAndMakeVisible (labels);
        lock.setButtonText (tr ("Lock to key"));
        lock.setTooltip (tr ("Keys outside the project key don't play (also on the typing keys)"));
        lock.onClick = [this] {
            scalekeys::current().lockToKey = lock.getToggleState();
            keys.repaint();
            settingsChanged();
        };
        addChildComponent (lock);

        // the Chord pad's controls
        for (int c = 0; c < scalekeys::complexityCount; ++c)
            complexity.addItem (tr (scalekeys::complexityName ((scalekeys::Complexity) c)), c + 1);
        complexity.setTooltip (tr ("How rich the chords are"));
        complexity.onChange = [this] {
            scalekeys::current().complexity = (scalekeys::Complexity) juce::jmax (0, complexity.getSelectedItemIndex());
            pads.repaint();
            settingsChanged();
        };
        for (int s = 0; s < scalekeys::spreadCount; ++s)
            spread.addItem (tr (scalekeys::spreadName ((scalekeys::Spread) s)), s + 1);
        spread.setTooltip (tr ("Close chords, or spread over two octaves"));
        spread.onChange = [this] {
            scalekeys::current().spread = (scalekeys::Spread) juce::jmax (0, spread.getSelectedItemIndex());
            pads.repaint();
            settingsChanged();
        };
        strum.setSliderStyle (juce::Slider::LinearHorizontal);
        strum.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
        strum.setRange (0.0, scalekeys::maxStrumMs, 1.0);
        strum.setTextValueSuffix (" ms");
        strum.setTooltip (tr ("Strum: the chord's notes start this far apart"));
        strum.onValueChange = [this] {
            scalekeys::current().strumMs = (int) strum.getValue();
            settingsChanged();
        };
        strumDown.setButtonText (tr ("Down"));
        strumDown.setTooltip (tr ("Strum from the highest note"));
        strumDown.onClick = [this] {
            scalekeys::current().strumDown = strumDown.getToggleState();
            settingsChanged();
        };
        chordSet.setTooltip (tr ("The key's chords, or the project's own chord set (from an FL Studio Mobile project, or edited on the pads)"));
        chordSet.onChange = [this] {
            scalekeys::current().projectChords = chordSet.getSelectedItemIndex() == 1;
            complexity.setEnabled (! scalekeys::current().projectChords);
            pads.repaint();
            settingsChanged();
        };
        strumLabel.setText (tr ("Strum"), juce::dontSendNotification);
        strumLabel.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        for (auto* c : std::initializer_list<juce::Component*> { &complexity, &spread, &strum, &strumDown, &chordSet, &strumLabel })
            addChildComponent (*c);

        target.setFont (theme::font (theme::type::body));
        target.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        target.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (target);

        if (liveMode)
            startTimerHz (30); // (fast enough for the held keys to light as they are played)
        timerCallback();
        syncFromSettings();
        setSize (900, 240);
        layoutKeys();
    }

    ~TouchKeyboardPanel() override { releaseAll(); }

    void releaseAll()
    {
        keys.releaseAll();
        scaleKeys.releaseAll();
        pads.releaseAll();
    }

    void showPressedForSnapshot()
    {
        keys.shown = { { 60, 0.95f }, { 64, 0.7f }, { 67, 0.45f }, { 70, 0.8f } };
        keys.repaint();
    }
    void showModeForSnapshot (scalekeys::Mode mode, int pressed)
    {
        setMode (mode);
        if (mode == scalekeys::Mode::scaleKeys && pressed >= 0)
            scaleKeys.shown = { { scaleKeys.noteOf (pressed), 0.9f } };
        if (mode == scalekeys::Mode::chordPad && pressed >= 0)
            pads.showPressedForSnapshot (pressed);
        timerCallback();
    }
    juce::PopupMenu chordMenuForSnapshot (int pad) { return pads.editMenuForSnapshot (pad); }
    void refreshForSnapshot() { timerCallback(); }

    void paint (juce::Graphics& g) override { g.fillAll (theme::colour::bg3); }

    void resized() override
    {
        auto area = getLocalBounds();
        auto bar = area.removeFromTop (36).reduced (10, 5);
        for (auto& b : modeButtons)
            b.setBounds (bar.removeFromLeft (96));
        bar.removeFromLeft (8);
        keyButton.setBounds (bar.removeFromLeft (150));
        bar.removeFromLeft (12);
        octaveDown.setBounds (bar.removeFromLeft (28));
        range.setBounds (bar.removeFromLeft (92));
        octaveUp.setBounds (bar.removeFromLeft (28));
        bar.removeFromLeft (10);
        sustain.setBounds (bar.removeFromLeft (100));
        bar.removeFromLeft (8);
        target.setBounds (bar);

        auto row = area.removeFromTop (32).reduced (10, 4);
        const auto mode = scalekeys::current().mode;
        velocityMode.setBounds (row.removeFromLeft (190));
        row.removeFromLeft (6);
        fixedVelocity.setBounds (row.removeFromLeft (120));
        row.removeFromLeft (10);
        if (mode == scalekeys::Mode::chordPad)
        {
            chordSet.setBounds (row.removeFromLeft (170));
            row.removeFromLeft (8);
            complexity.setBounds (row.removeFromLeft (110));
            row.removeFromLeft (8);
            spread.setBounds (row.removeFromLeft (90));
            row.removeFromLeft (10);
            strumLabel.setBounds (row.removeFromLeft (52));
            strum.setBounds (row.removeFromLeft (150));
            row.removeFromLeft (6);
            strumDown.setBounds (row.removeFromLeft (80));
        }
        else
        {
            labels.setBounds (row.removeFromLeft (110));
            row.removeFromLeft (8);
            if (mode == scalekeys::Mode::keys)
            {
                sizeBox.setBounds (row.removeFromLeft (124));
                row.removeFromLeft (8);
                lock.setBounds (row.removeFromLeft (130));
            }
        }
        keys.setBounds (area);
        scaleKeys.setBounds (area.reduced (4, 2));
        pads.setBounds (area.reduced (4, 2));
    }

private:
    void setMode (scalekeys::Mode mode)
    {
        releaseAll(); // (the notes held in the old mode stop)
        scalekeys::current().mode = mode;
        syncFromSettings();
        settingsChanged();
    }

    // shows the shared settings (another keyboard may have changed them)
    void syncFromSettings()
    {
        const auto& s = scalekeys::current();
        shownSettings = s;
        const auto mode = (int) s.mode;
        for (int i = 0; i < 3; ++i)
            modeButtons[(std::size_t) i].setToggleState (i == mode, juce::dontSendNotification);
        keys.setVisible (s.mode == scalekeys::Mode::keys);
        scaleKeys.setVisible (s.mode == scalekeys::Mode::scaleKeys);
        pads.setVisible (s.mode == scalekeys::Mode::chordPad);
        const bool chordsMode = s.mode == scalekeys::Mode::chordPad;
        for (auto* c : std::initializer_list<juce::Component*> { &complexity, &spread, &strum, &strumDown, &chordSet, &strumLabel })
            c->setVisible (chordsMode);
        labels.setVisible (! chordsMode);
        sizeBox.setVisible (s.mode == scalekeys::Mode::keys);
        lock.setVisible (s.mode == scalekeys::Mode::keys);
        lock.setToggleState (s.lockToKey, juce::dontSendNotification);
        complexity.setSelectedItemIndex ((int) s.complexity, juce::dontSendNotification);
        spread.setSelectedItemIndex ((int) s.spread, juce::dontSendNotification);
        strum.setValue (s.strumMs, juce::dontSendNotification);
        strumDown.setToggleState (s.strumDown, juce::dontSendNotification);
        updateChordSetBox();
        resized();
        repaint();
    }

    void updateChordSetBox()
    {
        const auto has = ! engine.getSong().keyboardChords.empty();
        chordSet.clear (juce::dontSendNotification);
        chordSet.addItem (tr ("The key's chords"), 1);
        chordSet.addItem (tr ("Project chord set"), 2);
        chordSet.setItemEnabled (2, has);
        chordSet.setSelectedItemIndex (scalekeys::current().projectChords && has ? 1 : 0, juce::dontSendNotification);
        complexity.setEnabled (! (scalekeys::current().projectChords && has)); // (a chord set's chords are as they are)
        chordSetHad = has;
    }

    static void settingsChanged()
    {
        if (onSettingsChanged)
            onSettingsChanged();
    }

    void layoutKeys()
    {
        static const int whites[] = { 35, 28, 21 };
        keys.layout.whiteKeys = whites[juce::jlimit (0, 2, sizeBox.getSelectedItemIndex())];
        keys.layout.lowest = touchkb::shiftOctave (keys.layout.lowest, 0, keys.layout.whiteKeys);
        keys.releaseAll();
        updateRange();
        keys.repaint();
    }

    void shift (int octaves)
    {
        releaseAll(); // the held keys would otherwise change under the finger
        keys.layout.lowest = touchkb::shiftOctave (keys.layout.lowest, octaves, keys.layout.whiteKeys);
        updateRange();
        keys.repaint();
        scaleKeys.repaint();
        pads.repaint();
    }

    void updateRange()
    {
        range.setText (keyName (keys.layout.lowest) + " - " + keyName (keys.layout.highest()), juce::dontSendNotification);
        octaveDown.setEnabled (keys.layout.lowest >= 12);
        touchkb::Layout probe = keys.layout;
        probe.lowest += 12;
        octaveUp.setEnabled (probe.highest() <= 127);
    }

    void timerCallback() override
    {
        std::array<std::uint64_t, 2> held {};
        for (int midi = 0; midi < 16; ++midi)
        {
            const auto bits = engine.heldInputKeyBits (midi);
            held[0] |= bits[0];
            held[1] |= bits[1];
        }
        keys.setHeldElsewhere (held);
        const auto channel = channelOf ? channelOf() : engine.getSelectedChannel();
        if (channel != lastChannel)
        {
            releaseAll(); // the notes go to the channel that started them; stop them before switching
            lastChannel = channel;
        }
        const auto& song = engine.getSong();
        target.setText (channel >= 0 && channel < (int) song.channels.size()
                            ? tr ("Plays: {0}", { juce::String::fromUTF8 (song.channels[(std::size_t) channel].name.c_str()) })
                            : tr ("Select a channel in the Channel Rack"),
                        juce::dontSendNotification);
        // the key, the shared settings and the project's chord set may change elsewhere
        const auto keyText = projectKeyText (song);
        if (keyButton.getButtonText() != tr ("Key: {0}", { keyText }))
        {
            keyButton.setButtonText (tr ("Key: {0}", { keyText }));
            scaleKeys.repaint();
            pads.repaint();
            keys.repaint();
        }
        if (! (shownSettings == scalekeys::current()))
            syncFromSettings();
        if (chordSetHad != ! song.keyboardChords.empty() || song.keyboardChords != shownChords)
        {
            shownChords = song.keyboardChords;
            updateChordSetBox();
            pads.repaint();
        }
    }

public:
    static inline std::function<void()> onSettingsChanged; // the main window saves the keyboard's settings

private:
    AudioEngine& engine;
    Keys keys;
    ScaleKeysView scaleKeys;
    ChordPadView pads;
    std::function<int()> channelOf;
    int lastChannel = -2;
    scalekeys::Settings shownSettings;
    std::vector<KeyboardChord> shownChords;
    bool chordSetHad = false;
    std::array<juce::TextButton, 3> modeButtons;
    juce::TextButton keyButton, octaveDown { "-" }, octaveUp { "+" };
    juce::Label range, target, strumLabel;
    juce::ComboBox sizeBox, velocityMode, complexity, spread, chordSet;
    juce::Slider fixedVelocity, strum;
    juce::ToggleButton labels, lock, strumDown, sustain;
};
} // namespace

bool isTouchKeyboardOpen()
{
    return findToolWindow ("touch-keyboard") != nullptr;
}

void toggleTouchKeyboard (AudioEngine& engine)
{
    if (auto* w = findToolWindow ("touch-keyboard"))
    {
        if (auto* content = w->getContentComponent())
            closeToolWindowOf (*content);
        return;
    }
    if (auto* window = openToolWindow (std::make_unique<TouchKeyboardPanel> (engine, true), tr ("Touch keyboard"), "touch-keyboard"))
        if (auto* main = juce::TopLevelWindow::getTopLevelWindow (0))
        {
            // along the bottom of the main window, where an on-screen keyboard is expected
            const auto area = main->getScreenBounds();
            window->setTopLeftPosition (area.getCentreX() - window->getWidth() / 2, area.getBottom() - window->getHeight() - 40);
        }
}

std::unique_ptr<juce::Component> makeTouchKeyboardPanel (AudioEngine& engine)
{
    auto panel = std::make_unique<TouchKeyboardPanel> (engine, false);
    panel->showPressedForSnapshot();
    return panel;
}

std::unique_ptr<juce::Component> makeKeyboardPanel (AudioEngine& engine, std::function<int()> channel)
{
    return std::make_unique<TouchKeyboardPanel> (engine, true, std::move (channel));
}

std::unique_ptr<juce::Component> makeKeyboardPanelForSnapshot (AudioEngine& engine, scalekeys::Mode mode, int pressed, std::function<int()> channel)
{
    auto panel = std::make_unique<TouchKeyboardPanel> (engine, false, std::move (channel));
    panel->showModeForSnapshot (mode, pressed);
    return panel;
}

juce::PopupMenu chordPadMenuForSnapshot (juce::Component& panel, int pad)
{
    if (auto* p = dynamic_cast<TouchKeyboardPanel*> (&panel))
        return p->chordMenuForSnapshot (pad);
    return {};
}

void refreshKeyboardPanel (juce::Component& panel)
{
    if (auto* p = dynamic_cast<TouchKeyboardPanel*> (&panel))
        p->refreshForSnapshot();
}

void setTypingKeyLetters (std::function<std::string (int)> letterOfKey) { typingLetterSource() = std::move (letterOfKey); }

void setKeyboardSettingsListener (std::function<void()> listener) { TouchKeyboardPanel::onSettingsChanged = std::move (listener); }
} // namespace lynx
