#include "ui/GamepadInput.h"

#if JUCE_WINDOWS
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

#include "ui/I18n.h"
#include "ui/Theme.h"

namespace lynx
{
namespace
{
// XInput's XINPUT_STATE (declared here: no xinput.h, no import library)
struct XState
{
    unsigned long packet;
    unsigned short buttons;
    unsigned char leftTrigger, rightTrigger;
    short leftX, leftY, rightX, rightY;
};
constexpr int maxPads = 4; // XUSER_MAX_COUNT
} // namespace

GamepadInput::GamepadInput (Handler onChanges) : handler (std::move (onChanges))
{
#if JUCE_WINDOWS
    for (const auto* name : { L"xinput1_4.dll", L"xinput9_1_0.dll" })
        if (auto* lib = LoadLibraryW (name))
        {
            if (auto* fn = GetProcAddress (lib, "XInputGetState"))
            {
                library = lib;
                getState = reinterpret_cast<GetState> (reinterpret_cast<void*> (fn));
                break;
            }
            FreeLibrary (lib);
        }
#endif
    lastTime = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    if (getState != nullptr)
        startTimerHz (60);
}

GamepadInput::~GamepadInput()
{
    stopTimer();
#if JUCE_WINDOWS
    if (library != nullptr)
        FreeLibrary (static_cast<HMODULE> (library));
#endif
}

void GamepadInput::inject (const gamepad::State& fake, double seconds) { apply (fake, seconds); }

void GamepadInput::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const auto dt = juce::jlimit (0.0, 0.25, now - lastTime);
    lastTime = now;
    gamepad::State s;
    if (getState != nullptr)
    {
        // XInputGetState on a slot with no pad is slow (it enumerates devices): while none is connected, look again
        // once a second
        const auto read = [this, &s] (int index) {
            XState x {};
            if (getState ((unsigned long) index, &x) != 0) // ERROR_SUCCESS = 0
                return false;
            s.connected = true;
            s.buttons = x.buttons;
            s.leftTrigger = x.leftTrigger;
            s.rightTrigger = x.rightTrigger;
            s.leftX = x.leftX;
            s.leftY = x.leftY;
            s.rightX = x.rightX;
            s.rightY = x.rightY;
            return true;
        };
        if (last.connected)
            read (padIndex);
        if (! s.connected && --searchCountdown <= 0)
        {
            searchCountdown = 60;
            for (int i = 0; i < maxPads && ! s.connected; ++i)
                if (read (i))
                    padIndex = i;
        }
    }
    apply (s, dt);
}

void GamepadInput::apply (const gamepad::State& now, double seconds)
{
    const auto changes = gamepad::compare (last, now);
    last = now;
    if (handler)
        handler (changes, now, seconds);
}

namespace
{
// The hints overlay: the pad, drawn by LYNX, with each button's action written next to it
class GamepadHints final : public juce::Component
{
public:
    GamepadHints()
    {
        setInterceptsMouseClicks (false, false);
        setSize (780, 440);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace gamepad;
        auto r = getLocalBounds().toFloat();
        g.setColour (theme::colour::bg0.withAlpha (0.94f));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (theme::colour::border);
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

        g.setColour (theme::colour::textPrimary);
        g.setFont (theme::font (theme::type::title, true));
        g.drawText (tr ("Game controller"), r.removeFromTop (44).reduced (20, 0), juce::Justification::centredLeft);

        // the pad: a body with two grips, in the middle
        const auto centre = juce::Point<float> (getWidth() * 0.5f, 230.0f);
        juce::Path body;
        body.addRoundedRectangle (centre.x - 150.0f, centre.y - 70.0f, 300.0f, 120.0f, 48.0f);
        body.addEllipse (centre.x - 165.0f, centre.y - 40.0f, 110.0f, 130.0f);
        body.addEllipse (centre.x + 55.0f, centre.y - 40.0f, 110.0f, 130.0f);
        g.setColour (theme::colour::bg2.brighter (0.15f));
        g.fillPath (body);
        // shoulders
        g.setColour (theme::colour::bg2.brighter (0.3f));
        g.fillRoundedRectangle (centre.x - 140.0f, centre.y - 84.0f, 70.0f, 16.0f, 6.0f);
        g.fillRoundedRectangle (centre.x + 70.0f, centre.y - 84.0f, 70.0f, 16.0f, 6.0f);

        const auto spot = [&] (Button b) -> juce::Point<float> {
            switch (b)
            {
                case Button::leftShoulder: return { centre.x - 105.0f, centre.y - 76.0f };
                case Button::rightShoulder: return { centre.x + 105.0f, centre.y - 76.0f };
                case Button::up: return { centre.x - 70.0f, centre.y + 8.0f };
                case Button::down: return { centre.x - 70.0f, centre.y + 40.0f };
                case Button::left: return { centre.x - 86.0f, centre.y + 24.0f };
                case Button::right: return { centre.x - 54.0f, centre.y + 24.0f };
                case Button::back: return { centre.x - 26.0f, centre.y - 22.0f };
                case Button::start: return { centre.x + 26.0f, centre.y - 22.0f };
                case Button::y: return { centre.x + 105.0f, centre.y - 42.0f };
                case Button::x: return { centre.x + 83.0f, centre.y - 20.0f };
                case Button::b: return { centre.x + 127.0f, centre.y - 20.0f };
                case Button::a: return { centre.x + 105.0f, centre.y + 2.0f };
                case Button::leftThumb: return { centre.x - 105.0f, centre.y - 22.0f };
                case Button::rightThumb: return { centre.x + 50.0f, centre.y + 24.0f };
                case Button::count: break;
            }
            return centre;
        };
        // sticks
        g.setColour (theme::colour::bg0);
        for (auto b : { Button::leftThumb, Button::rightThumb })
            g.fillEllipse (juce::Rectangle<float> (34.0f, 34.0f).withCentre (spot (b)));
        // D-pad cross
        g.setColour (theme::colour::bg0);
        g.fillRoundedRectangle (juce::Rectangle<float> (16.0f, 48.0f).withCentre (spot (Button::up).translated (0.0f, 16.0f)), 3.0f);
        g.fillRoundedRectangle (juce::Rectangle<float> (48.0f, 16.0f).withCentre (spot (Button::up).translated (0.0f, 16.0f)), 3.0f);
        // face buttons
        for (auto b : { Button::a, Button::b, Button::x, Button::y })
        {
            g.setColour (b == Button::a ? theme::colour::accent : theme::colour::bg0);
            g.fillEllipse (juce::Rectangle<float> (20.0f, 20.0f).withCentre (spot (b)));
            g.setColour (theme::colour::textPrimary);
            g.setFont (theme::font (theme::type::caption, true));
            g.drawText (buttonName (b), juce::Rectangle<float> (20.0f, 20.0f).withCentre (spot (b)), juce::Justification::centred);
        }
        g.setColour (theme::colour::bg0);
        for (auto b : { Button::back, Button::start })
            g.fillRoundedRectangle (juce::Rectangle<float> (18.0f, 9.0f).withCentre (spot (b)), 4.0f);

        // the labels: left side for the left half of the pad, right side for the right half; a leader line to each
        struct Row
        {
            juce::String text;
            Button button;
        };
        std::vector<Row> left, right;
        const auto& map = defaultMap();
        const auto add = [&] (Button b, const juce::String& text) {
            // (the face buttons, shoulders, Back and Start keep the names printed on the pad)
            const auto name = (int) b >= (int) Button::leftThumb ? tr (buttonName (b)) : juce::String (buttonName (b));
            (spot (b).x < centre.x ? left : right).push_back ({ name + ": " + text, b });
        };
        for (int i = 0; i < buttonCount; ++i)
            if (map[(std::size_t) i] != Action::none)
                add ((Button) i, tr (actionName (map[(std::size_t) i])));
        left.push_back ({ tr ("LT / RT: track volume down / up"), Button::leftShoulder });
        left.push_back ({ tr ("Left stick: track pan"), Button::leftThumb });
        right.push_back ({ tr ("Right stick: link it to any knob"), Button::rightThumb });
        g.setFont (theme::font (theme::type::small));
        const auto draw = [&] (const std::vector<Row>& rows, bool onLeft) {
            const auto x = onLeft ? 20.0f : getWidth() - 240.0f;
            for (std::size_t i = 0; i < rows.size(); ++i)
            {
                const auto y = 66.0f + (float) i * 26.0f;
                const auto box = juce::Rectangle<float> (x, y, 220.0f, 22.0f);
                g.setColour (theme::colour::textSecondary.withAlpha (0.35f));
                const auto from = juce::Point<float> (onLeft ? box.getRight() + 4.0f : box.getX() - 4.0f, box.getCentreY());
                g.drawLine ({ from, spot (rows[i].button) }, 1.0f);
                g.setColour (theme::colour::textPrimary);
                g.drawText (rows[i].text, box, onLeft ? juce::Justification::centredRight : juce::Justification::centredLeft, true);
            }
        };
        draw (left, true);
        draw (right, false);

        g.setColour (theme::colour::textSecondary);
        g.drawText (tr ("Every button and stick can also be linked to a knob: right-click it > Link to MIDI controller, then move the stick."),
                    getLocalBounds().removeFromBottom (36).reduced (20, 0), juce::Justification::centredLeft, true);
    }
};
} // namespace

std::unique_ptr<juce::Component> makeGamepadHints() { return std::make_unique<GamepadHints>(); }
} // namespace lynx
