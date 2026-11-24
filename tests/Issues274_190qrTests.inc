// Round 190qr: issues #274-#281 (yannikthegenius, 2026-11-23).

#include "core/DesktopEntries.h"
#include "core/MarkerPlan.h"

// #281: FL's Time markers › Delete — the markers in the time selection, or all of them without one
TEST (issues190qr_281_time_markers_delete)
{
    using namespace lynx;
    const auto bar = (std::int64_t) 4 * ppq;
    const auto make = [bar] {
        Song s;
        for (int i = 0; i < 6; ++i)
        {
            Marker m;
            m.tick = i * bar;
            m.name = "M" + std::to_string (i);
            s.addMarker (m);
        }
        Marker sig;
        sig.tick = 2 * bar;
        sig.numerator = 3;
        sig.denominator = 4;
        s.addMarker (sig);
        return s;
    };
    auto song = make();
    CHECK (song.markers().size() == 7u);
    song.loopStart = 2 * bar;
    song.loopEnd = 4 * bar; // bars 3-4: M2, the 3/4 change, M3 (M4 sits on the selection's end: it stays)
    CHECK (markers::deleteInSelection (song) == 3);
    CHECK (song.markers().size() == 4u);
    for (const auto& m : song.markers())
        CHECK (m.tick < 2 * bar || m.tick >= 4 * bar);
    auto all = make();
    CHECK (markers::deleteInSelection (all) == 7); // no selection: every marker
    CHECK (all.markers().empty());
    CHECK (markers::deleteInSelection (all) == 0);
}

// #278: an AppImage manager's entry for the running AppImage (Gear Lever, AppImageLauncher, appimaged)
TEST (issues190qr_278_appimage_manager_entries)
{
    using namespace lynx::desktopentries;
    const std::string gearLever = "[Desktop Entry]\nType=Application\nName=LYNX SHARE\nExec=/home/u/AppImages/lynx_share.appimage %F\n"
                                  "Icon=/home/u/AppImages/.icons/lynx_share\nX-AppImage-Version=0.1.31\n[Desktop Action Foo]\nExec=other\n";
    CHECK (valueOf (gearLever, "Exec") == "/home/u/AppImages/lynx_share.appimage %F");
    CHECK (valueOf (gearLever, "Name") == "LYNX SHARE");
    CHECK (valueOf (gearLever, "Comment").empty());
    CHECK (startsAppImage (gearLever, "/home/u/AppImages/lynx_share.appimage"));
    CHECK (! startsAppImage (gearLever, "/home/u/AppImages/other.appimage"));
    CHECK (! startsAppImage (gearLever, ""));
    // LYNX's own entry never counts (its marker line), even though it starts the same file
    const std::string own = "[Desktop Entry]\nType=Application\nExec=\"/home/u/AppImages/lynx_share.appimage\" %F\nTryExec=/home/u/AppImages/lynx_share.appimage\nX-LYNX-AppImage=true\n";
    CHECK (! startsAppImage (own, "/home/u/AppImages/lynx_share.appimage"));
    CHECK (startsAppImage (own, "/home/u/AppImages/lynx_share.appimage", ""));
    // quoting and escapes of the desktop entry spec: a path with spaces, quoted (LYNX's own writer doubles the
    // backslashes), or with "\s"; TryExec alone
    CHECK (programOf ("\"/home/u/My Apps/LYNX.AppImage\" %F") == "/home/u/My Apps/LYNX.AppImage");
    CHECK (programOf ("\"/home/u/a\\\\\"b/LYNX.AppImage\" %F") == "/home/u/a\"b/LYNX.AppImage");
    CHECK (programOf ("\"/home/u/My\\sApps/LYNX.AppImage\" %U") == "/home/u/My Apps/LYNX.AppImage");
    CHECK (programOf ("/home/u/My\\sApps/LYNX.AppImage %U") == "/home/u/My"); // (unquoted, \s is a space: two arguments)
    CHECK (programOf ("env FOO=1 /x/LYNX.AppImage") == "env");
    CHECK (startsAppImage ("[Desktop Entry]\r\nType=Application\r\nExec=\"/home/u/My Apps/LYNX.AppImage\" %F\r\n", "/home/u/My Apps/LYNX.AppImage"));
    CHECK (startsAppImage ("[Desktop Entry]\nExec=wrapper\nTryExec=/opt/LYNX.AppImage\n", "/opt/LYNX.AppImage"));
    // an Exec in another group, or a link entry, is not one
    CHECK (! startsAppImage ("[Desktop Action New]\nExec=/opt/LYNX.AppImage\n", "/opt/LYNX.AppImage"));
    CHECK (! startsAppImage ("[Desktop Entry]\nType=Link\nExec=/opt/LYNX.AppImage\n", "/opt/LYNX.AppImage"));
}
