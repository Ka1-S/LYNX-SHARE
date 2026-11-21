// Round 190od: GitHub issues #236 (slide notes move velocity and pan), #238 (the colour of tracks without one), #239
// (automatic track colours and icons; FL's "clips use note colors"), #240 (a new project from a template starts its own clock).

// #240: a template's own date and working time stay behind; the new project is dated when it starts
TEST (issues190od_template_clock_is_cleared)
{
    using namespace lynx;
    ProjectInfo info;
    info.title = "My template";
    info.created = 1.7e9;
    info.workSeconds = 1693.0;
    clearProjectClock (info);
    CHECK (info.created == 0.0 && info.workSeconds == 0.0 && info.title == "My template");
}
