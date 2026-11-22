// Round 190pf (research190oe P13): the Playlist's side of the Chord lane (ui/ChordLane190pf.h). Included in
// PlaylistComponent.cpp (namespace lynx).

bool PlaylistComponent::chordLaneShown() const
{
    return chordlane::laneOn(); // (a LYNX extra in every layout, off by default)
}

int PlaylistComponent::chordLaneHeight() const
{
    return chordLaneShown() ? (simpleHeaders ? chordLaneSimpleHeight : chordLaneFlHeight) : 0;
}

void PlaylistComponent::chordLaneChanged()
{
    if (chordLane == nullptr || videoLanes == nullptr || blocksView == nullptr)
        return; // (not built yet)
    if (chordLaneHeight() != laidOutChordHeight)
        resized();
    chordLane->repaint();
    repaint();
}

// Round 190pf (research190oe P12): Smart Tempo — the preview window, then (on Follow) the tempo markers and the unwarped
// clip in one undo step
void PlaylistComponent::followClipTempo (int clip)
{
    if (smarttempo::blockedBy (engine.getSong()))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, tr ("Make the project tempo follow this clip"),
                                                tr ("Something else moves the project's tempo (a tempo automation clip or pattern tempo events): remove it first."));
        return;
    }
    double rate = 44100.0;
    auto mono = smartTempoAudio (engine, clip, rate);
    if (mono.empty())
        return;
    auto* box = new SmartTempoWindow (engine, clip, std::move (mono), rate, this);
    juce::Component::SafePointer<PlaylistComponent> self (this);
    box->enterModalState (true, juce::ModalCallbackFunction::create ([box, self] (int result) {
                              if (result == 1 && box->apply() && self != nullptr)
                              {
                                  self->sortClips();
                                  self->repaint();
                              }
                          }),
                          true);
}
