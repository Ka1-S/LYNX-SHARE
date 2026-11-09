#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx::nativechild
{
// Issue #116 (FL's docked plugin windows; design: docs/analise/REA_rodada181c.md): a third-party editor is a native child
// window, and a native child is always drawn above the lightweight JUCE components of the same window. So a docked
// plugin window and the panels that must be able to come in front of it are native child windows of the main window
// too, siblings whose stacking order the system keeps (as FL's MDI children). These helpers host a component that way
// inside `area` (the Workspace), in `area`'s coordinates.

// The developer switch (LYNX_DOCKED_PLUGINS=1 in the environment): until every panel is a native child, docking stays
// off by default and plugin windows are FL's "Detached" (LYNX's behaviour so far).
bool dockingEnabled();

// Makes `c` a native child window of `area`'s top-level window (it must not have a parent component). False when that
// window has no native peer yet; `c` is left as it was.
bool attach (juce::Component& c, juce::Component& area);
// Back to a plain component (no native window); the caller adds it to a parent again.
void detach (juce::Component& c);
bool isAttached (const juce::Component& c);

// Where an attached component is, in `area`'s coordinates, and moving it there. For a component that is not attached
// these are its plain bounds in its parent.
void place (juce::Component& c, const juce::Component& area, juce::Rectangle<int> boundsInArea);
juce::Rectangle<int> boundsIn (const juce::Component& c, const juce::Component& area);
// After the main window's native window was made again, or `area` moved inside it: attached again if needed, and put
// back at the same place in `area`.
void refresh (juce::Component& c, juce::Component& area);

// Stacking among the attached siblings without disturbing the others: if `c` is not above `below` it goes just above it
// (under whatever was right above `below`); `below` == nullptr: nothing. Elsewhere than Windows: `c` comes to the front
// when it has to move.
void keepAbove (juce::Component& c, juce::Component* below);

// Keys nothing in an attached component used: they don't travel up to the main window by themselves (it is another
// native window), so `c` hands them to the main window's shortcuts (the secondary windows' route, ui/WindowOwner.h).
void forwardUnusedKeys (juce::Component& c);

// Tests (Windows; elsewhere false / nullptr): `a` is above its sibling `b` in the native stacking order; a native window's
// parent window (a child window) and owner (a top-level window kept above another).
bool isAbove (void* a, void* b);
void* parentOf (void* window);
void* ownerOf (void* window);
} // namespace lynx::nativechild
