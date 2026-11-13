#pragma once

// The "Clean voice" items (round 190ch, core/CleanVoice.h): the same three lines in the Simple layout's track menu and
// in the Mixer's track menu, so the button is one click wherever the track is seen.

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/CleanVoice.h"
#include "engine/AudioEngine.h"
#include "ui/I18n.h"

namespace lynx::ui
{
// Applies (or replaces) the chain on `insert`; a message when the insert has no room. True when it is on the insert.
inline bool applyCleanVoice (AudioEngine& engine, int insert, bool noiseReduction)
{
    bool ok = false;
    cleanvoice::Options options;
    options.noiseReduction = noiseReduction;
    engine.editSongStructural ([&ok, insert, options] (Song& s) { ok = cleanvoice::apply (s, insert, options); });
    if (! ok)
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Clean voice"),
                                                tr ("This track has no room for the chain: it needs {0} free effect slots.",
                                                    { juce::String ((int) cleanvoice::chain (options).size()) }));
    return ok;
}

// The submenu: Clean voice / with noise reduction (the one in use is ticked) and Remove.
inline juce::PopupMenu cleanVoiceMenu (AudioEngine& engine, int insert)
{
    const auto& song = engine.getSong();
    const auto has = cleanvoice::present (song, insert), noise = has && cleanvoice::hasNoiseReduction (song, insert);
    juce::PopupMenu menu;
    auto* e = &engine;
    menu.addItem (tr ("Clean voice"), insert > 0, has && ! noise, [e, insert] { applyCleanVoice (*e, insert, false); });
    menu.addItem (tr ("Clean voice with noise reduction"), insert > 0, noise, [e, insert] { applyCleanVoice (*e, insert, true); });
    menu.addSeparator();
    menu.addItem (tr ("Remove Clean voice"), has, false, [e, insert] { e->editSongStructural ([insert] (Song& s) { cleanvoice::remove (s, insert); }); });
    return menu;
}
} // namespace lynx::ui
