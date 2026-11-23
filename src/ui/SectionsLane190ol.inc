// Round 190ol (research190oe P1): the Playlist's side of the Sections lane (ui/SectionsLane190ol.h). Included in
// PlaylistComponent.cpp (namespace lynx).

// --- the Playlist's side -----------------------------------------------------------------------------------------------
bool PlaylistComponent::sectionsLaneShown() const
{
    if (simpleHeaders)
        return ! engine.getSong().sections().empty() && ! simpleSectionsHidden;
    if (liveStyle)
        return false; // (owner decision O-1: off in the Clip-launch layout)
    return sections::patternLaneOn();
}

int PlaylistComponent::sectionsLaneHeight() const
{
    return sectionsLaneShown() ? (simpleHeaders ? sectionsLaneSimpleHeight : sectionsLaneFlHeight) : 0;
}

void PlaylistComponent::toggleSimpleSections()
{
    if (engine.getSong().sections().empty())
    {
        // GarageBand's Show Arrangement Track then "+": the first section (Intro, eight bars) and the lane appears
        simpleSectionsHidden = false;
        engine.editSong ([name = nextSectionName (engine.getSong().sections())] (Song& s) { sections::add (s, name); });
    }
    else
        simpleSectionsHidden = ! simpleSectionsHidden;
    sectionsLaneChanged();
}

void PlaylistComponent::sectionsLaneChanged()
{
    if (sectionsLane == nullptr || videoLanes == nullptr || blocksView == nullptr)
        return; // (not built yet)
    if (sectionsLaneHeight() != laidOutSectionsHeight)
        resized();
    sectionsLane->repaint();
    repaint();
}
