// --- Round 184a: the project key (core/ProjectKey.h): one accessor over Song::scaleHighlight (FL's .flp 0xF3), shared by
// the Simple workflow and the Clip-launch workflow's clip column (round 183a).

#include "core/ProjectKey.h"

TEST (simple184a_project_key_get_set)
{
    using namespace lynx;
    Song s;
    CHECK (! projectkey::get (s)); // a new project: FL's "Black notes", no key
    CHECK (s.scaleHighlight.scale == projectkey::noKey);
    const auto minor = projectkey::commonFirst()[1];
    projectkey::set (s, projectkey::Key { 9, minor });
    CHECK (s.scaleHighlight.scale == minor && s.scaleHighlight.root == 9); // the stored FL index and root
    CHECK (projectkey::get (s) == (projectkey::Key { 9, minor }));
    CHECK (projectkey::name (*projectkey::get (s)) == "A Minor Natural (Aeolian)");
    projectkey::set (s, projectkey::Key { -3, 0 }); // root wrapped
    CHECK (s.scaleHighlight.root == 9 && s.scaleHighlight.scale == 0);
    projectkey::set (s, projectkey::Key { 2, 9999 }); // invalid scale: cleared
    CHECK (! projectkey::get (s));
    projectkey::set (s, projectkey::Key { 2, 0 });
    projectkey::set (s, std::nullopt);
    CHECK (! projectkey::get (s) && s.scaleHighlight.scale == projectkey::noKey);
    // FL's Default (-1) and Automatic (-3) are no key here either
    s.scaleHighlight = { -1, 4 };
    CHECK (! projectkey::get (s));
    s.scaleHighlight = { -3, 4 };
    CHECK (! projectkey::get (s));
}

TEST (simple184a_project_key_scales)
{
    using namespace lynx;
    const auto& list = notetools::flReferenceScales();
    const auto order = projectkey::commonFirst();
    CHECK (order.size() == list.size());
    auto sorted = order;
    std::sort (sorted.begin(), sorted.end());
    CHECK (std::adjacent_find (sorted.begin(), sorted.end()) == sorted.end()); // each once
    CHECK (std::string (list[(std::size_t) order[0]].name) == "Major (Ionian)");
    CHECK (std::string (list[(std::size_t) order[1]].name) == "Minor Natural (Aeolian)");
    const projectkey::Key cMajor { 0, order[0] };
    CHECK (projectkey::intervals (cMajor) == (std::vector<int> { 0, 2, 4, 5, 7, 9, 11 }));
    CHECK (projectkey::contains (cMajor, 64) && ! projectkey::contains (cMajor, 61) && projectkey::contains (cMajor, 0));
    const projectkey::Key aMinor { 9, order[1] };
    CHECK (projectkey::contains (aMinor, 60) && projectkey::contains (aMinor, 69) && ! projectkey::contains (aMinor, 68));
    CHECK (projectkey::lynxScale (cMajor) > 0); // LYNX's own list has Major (the Piano roll's snap)
    CHECK (projectkey::intervals (projectkey::Key { 0, -1 }).empty());
    CHECK (projectkey::rootName (13) == "C#" && projectkey::rootName (-1) == "B");
}
