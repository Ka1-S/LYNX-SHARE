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
// The hints overlay: the pad, drawn by LYNX, with each control's action written next to it. Every label sits on the
// side of its control (left half → left column, right half → right column, shoulders and triggers at the top, Back
// and Start under the pad) with a short leader that crosses no other leader or control.
class GamepadHints final : public juce::Component
{
public:
    GamepadHints()
    {
        setInterceptsMouseClicks (false, false);
        setSize (1000, 520);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace gamepad;
        using P = juce::Point<float>;
        using R = juce::Rectangle<float>;
        auto r = getLocalBounds().toFloat();
        g.setColour (theme::colour::bg0.withAlpha (0.94f));
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (theme::colour::border);
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
        g.setColour (theme::colour::textPrimary);
        g.setFont (theme::font (theme::type::title, true));
        g.drawText (tr ("Game controller"), R (20.0f, 10.0f, 400.0f, 30.0f), juce::Justification::centredLeft);

        // --- the pad (centre 500, 300) ---
        const auto body = theme::colour::bg2.brighter (0.15f), part = theme::colour::bg0, raised = theme::colour::bg2.brighter (0.35f);
        const R lt (315.0f, 162.0f, 70.0f, 20.0f), rt (615.0f, 162.0f, 70.0f, 20.0f);    // triggers
        const R lb (300.0f, 184.0f, 100.0f, 16.0f), rb (600.0f, 184.0f, 100.0f, 16.0f);  // shoulders
        g.setColour (raised);
        g.fillRoundedRectangle (lt, 6.0f);
        g.fillRoundedRectangle (rt, 6.0f);
        g.setColour (raised.brighter (0.1f));
        g.fillRoundedRectangle (lb, 6.0f);
        g.fillRoundedRectangle (rb, 6.0f);
        juce::Path shape;
        shape.addRoundedRectangle (280.0f, 196.0f, 440.0f, 160.0f, 70.0f);
        shape.addEllipse (285.0f, 250.0f, 160.0f, 170.0f);
        shape.addEllipse (555.0f, 250.0f, 160.0f, 170.0f);
        g.setColour (body);
        g.fillPath (shape);

        const P leftStick (370.0f, 255.0f), rightStick (570.0f, 320.0f), dpad (430.0f, 320.0f);
        const P back (465.0f, 250.0f), start (535.0f, 250.0f);
        const P faceY (630.0f, 226.0f), faceX (602.0f, 254.0f), faceB (658.0f, 254.0f), faceA (630.0f, 282.0f);
        g.setColour (part);
        for (auto c : { leftStick, rightStick })
            g.fillEllipse (R (48.0f, 48.0f).withCentre (c));
        g.setColour (body.brighter (0.2f));
        for (auto c : { leftStick, rightStick })
            g.drawEllipse (R (34.0f, 34.0f).withCentre (c), 1.5f);
        g.setColour (part);
        g.fillRoundedRectangle (R (18.0f, 56.0f).withCentre (dpad), 3.0f);
        g.fillRoundedRectangle (R (56.0f, 18.0f).withCentre (dpad), 3.0f);
        g.fillRoundedRectangle (R (22.0f, 10.0f).withCentre (back), 4.0f);
        g.fillRoundedRectangle (R (22.0f, 10.0f).withCentre (start), 4.0f);
        g.setFont (theme::font (theme::type::caption, true));
        for (auto [c, b] : { std::pair { faceA, Button::a }, std::pair { faceB, Button::b }, std::pair { faceX, Button::x }, std::pair { faceY, Button::y } })
        {
            g.setColour (b == Button::a ? theme::colour::accent : part);
            g.fillEllipse (R (24.0f, 24.0f).withCentre (c));
            g.setColour (theme::colour::textPrimary);
            g.drawText (buttonName (b), R (24.0f, 24.0f).withCentre (c), juce::Justification::centred);
        }
        g.setFont (theme::font (theme::type::caption, true));
        g.setColour (theme::colour::textSecondary);
        g.drawText ("LT", lt, juce::Justification::centred);
        g.drawText ("RT", rt, juce::Justification::centred);
        g.drawText ("LB", lb, juce::Justification::centred);
        g.drawText ("RB", rb, juce::Justification::centred);

        // --- the labels ---
        const auto& map = defaultMap();
        const auto line = [&] (Button b) {
            // (the face buttons, shoulders, Back and Start keep the names printed on the pad)
            const auto name = (int) b >= (int) Button::leftThumb ? tr (buttonName (b)) : juce::String (buttonName (b));
            return name + ": " + tr (actionName (map[(std::size_t) b]));
        };
        const auto lead = [&g] (P from, P to) {
            g.setColour (theme::colour::textSecondary.withAlpha (0.55f));
            g.drawLine ({ from, to }, 1.0f);
            g.fillEllipse (R (5.0f, 5.0f).withCentre (to));
        };
        const auto text = [&g] (const juce::String& s, R box, juce::Justification j) {
            g.setColour (theme::colour::textPrimary);
            g.setFont (theme::font (theme::type::small));
            g.drawFittedText (s, box.toNearestInt(), j, 4, 0.8f);
        };
        constexpr float leftEdge = 20.0f, leftWidth = 250.0f, rightX = 740.0f, rightWidth = 240.0f, rowH = 18.0f;
        // top: the triggers (one label, two leaders), the shoulders beside their buttons
        text (tr ("LT / RT: track volume down / up"), R (300.0f, 96.0f, 400.0f, rowH), juce::Justification::centred);
        lead ({ 450.0f, 116.0f }, { lt.getCentreX(), lt.getY() });
        lead ({ 550.0f, 116.0f }, { rt.getCentreX(), rt.getY() });
        text (line (Button::leftShoulder), R (leftEdge, 183.0f, leftWidth, rowH), juce::Justification::centredRight);
        lead ({ leftEdge + leftWidth + 6.0f, 192.0f }, { lb.getX(), 192.0f });
        text (line (Button::rightShoulder), R (rightX, 183.0f, rightWidth, rowH), juce::Justification::centredLeft);
        lead ({ rightX - 6.0f, 192.0f }, { rb.getRight(), 192.0f });
        // left column: the left stick, then the D-pad's four directions as one group
        text (tr ("Left stick: track pan"), R (leftEdge, 246.0f, leftWidth, rowH), juce::Justification::centredRight);
        lead ({ leftEdge + leftWidth + 6.0f, 255.0f }, { leftStick.x - 24.0f, 255.0f });
        {
            const R block (leftEdge, 284.0f, leftWidth, rowH * 4.0f);
            auto row = block;
            for (auto b : { Button::up, Button::down, Button::left, Button::right })
                text (line (b), row.removeFromTop (rowH), juce::Justification::centredRight);
            g.setColour (theme::colour::textSecondary.withAlpha (0.55f));
            g.drawLine (block.getRight() + 6.0f, block.getY() + 3.0f, block.getRight() + 6.0f, block.getBottom() - 3.0f, 1.0f); // the group's bracket
            lead ({ block.getRight() + 6.0f, dpad.y }, { dpad.x - 28.0f, dpad.y });
        }
        // right column: the four face buttons as one group, then the right stick
        {
            const R block (rightX, 218.0f, rightWidth, rowH * 4.0f);
            auto row = block;
            for (auto b : { Button::y, Button::x, Button::b, Button::a })
                text (line (b), row.removeFromTop (rowH), juce::Justification::centredLeft);
            g.setColour (theme::colour::textSecondary.withAlpha (0.55f));
            g.drawLine (block.getX() - 6.0f, block.getY() + 3.0f, block.getX() - 6.0f, block.getBottom() - 3.0f, 1.0f);
            lead ({ block.getX() - 6.0f, faceB.y }, { faceB.x + 12.0f, faceB.y });
        }
        text (tr ("Right stick: link it to any knob"), R (rightX, 320.0f, rightWidth, rowH), juce::Justification::centredLeft);
        lead ({ rightX - 6.0f, 329.0f }, { rightStick.x + 24.0f, 324.0f });
        // under the pad: Back and Start, straight down from their buttons
        text (line (Button::back), R (175.0f, 432.0f, 285.0f, rowH), juce::Justification::centredRight);
        lead ({ back.x, 438.0f }, { back.x, back.y + 5.0f });
        text (line (Button::start), R (540.0f, 432.0f, 285.0f, rowH), juce::Justification::centredLeft);
        lead ({ start.x, 438.0f }, { start.x, start.y + 5.0f });

        g.setColour (theme::colour::textSecondary);
        g.setFont (theme::font (theme::type::small));
        g.drawText (tr ("Every button and stick can also be linked to a knob: right-click it > Link to MIDI controller, then move the stick."),
                    getLocalBounds().removeFromBottom (40).reduced (20, 0), juce::Justification::centredLeft, true);
    }
};
} // namespace

std::unique_ptr<juce::Component> makeGamepadHints() { return std::make_unique<GamepadHints>(); }
} // namespace lynx
