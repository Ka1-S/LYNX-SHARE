// Issue #254 (round 190ou): the horizontal wheel (a tilt wheel, a mouse's second wheel). core/EditorGestures.h:
// horizontalWheelNotch tells its notches from a trackpad's swipe and from macOS's Shift+wheel; FL 2026's step is 1.5 beats.
// The views themselves (Playlist, Piano roll, Mixer, Browser) are checked by LYNX --hwheel254test (src/HWheel254Test.inc).

#include "core/EditorGestures.h"

TEST (hwheel254_notches_of_the_horizontal_wheel_only)
{
    using namespace lynx::gesture;
    constexpr double notch = 60.0 / 256.0; // (JUCE on Windows: WM_MOUSEHWHEEL's 120, halved, over 256)
    // Windows: a tilt to the right is a negative deltaX (JUCE turns the sign, as for a trackpad): the view goes later
    CHECK (horizontalWheelNotch (-notch, 0.0, false, false) == 1);
    CHECK (horizontalWheelNotch (notch, 0.0, false, false) == -1);
    // X11's buttons 6 / 7 (JUCE patch fix 49): 50 / 256 a press
    CHECK (horizontalWheelNotch (-50.0 / 256.0, 0.0, false, false) == 1);
    CHECK (horizontalWheelNotch (50.0 / 256.0, 0.0, false, false) == -1);
    // not a horizontal wheel: the vertical wheel, a trackpad's swipe (smooth), macOS's Shift+wheel (deltaX with Shift),
    // a diagonal swipe, nothing
    CHECK (horizontalWheelNotch (0.0, notch, false, false) == 0);
    CHECK (horizontalWheelNotch (-0.05, 0.0, true, false) == 0);
    CHECK (horizontalWheelNotch (-notch, 0.0, false, true) == 0);
    CHECK (horizontalWheelNotch (-notch, notch, false, false) == 0);
    CHECK (horizontalWheelNotch (0.0, 0.0, false, false) == 0);
    // Windows' pieces of a notch (a high-resolution wheel, a touchpad) are made smooth first and keep their own path
    const auto piece = normaliseWheel (-notch / 4.0, 0.0, false, true);
    CHECK (piece.smooth && horizontalWheelNotch (piece.x, piece.y, piece.smooth, false) == 0);
    const auto whole = normaliseWheel (-notch, 0.0, false, true);
    CHECK (! whole.smooth && horizontalWheelNotch (whole.x, whole.y, whole.smooth, false) == 1);
    // FL 2026's step (measured at two zooms, 15.5 and 384 px a bar): 1.5 beats a notch
    CHECK (horizontalWheelBeats == 1.5);
}
