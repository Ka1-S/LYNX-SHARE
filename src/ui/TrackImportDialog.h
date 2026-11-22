#pragma once

// File › Import › "Tracks from another project..." (round 190pe, P11 of research190oe; a LYNX extra: FL Studio's File ›
// Import holds MIDI file only). The other project's Playlist tracks as a checklist (with what each holds), its Channel
// Rack channels that no clip plays, "With mixer inserts and effects", "With automation" and where the tracks go; the
// merge itself is core/TrackImport.h.

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/TrackImport.h"

namespace lynx
{
struct TrackImportChoice
{
    std::vector<int> tracks;
    trackimport::Options options;
};

// The window ("Import" = 1, "Cancel" = 0). `selectionStart` < 0: the song has no time selection. `picked` ticks those
// rows first (shots, tests). Also used by --tracks190peshot.
juce::AlertWindow* buildTrackImportWindow (const juce::String& fileName, const Song& source, std::int64_t selectionStart,
                                           const std::vector<int>& picked = {});
// What the window's rows and switches say now
TrackImportChoice trackImportChoiceOf (juce::AlertWindow& window);

void showTrackImportDialog (const juce::String& fileName, const Song& source, std::int64_t selectionStart,
                            std::function<void (const TrackImportChoice&)> onAccept);

// The report after the import: what came in, then the notes
juce::String trackImportReport (const trackimport::Result& result, const juce::String& fileName, const juce::StringArray& missingPlugins);
} // namespace lynx
