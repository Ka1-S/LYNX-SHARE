// Issue #116: only the "+ add channel" box opens the Channel rack's add menu, as FL's + button.

TEST (rack116_add_menu_opens_only_from_its_box)
{
    using namespace lynx::racklayout;
    const auto box = addChannelBox (100, 150, 400, 28); // nameLeft, nameWidth, the add row's top, the row height
    CHECK (box.contains (175, 414));                 // the middle of the drawn box
    CHECK (box.contains (100, 403) && box.contains (249, 424));
    CHECK (! box.contains (99, 414));                // the mute / pan / volume columns left of it
    CHECK (! box.contains (250, 414));               // right of it (the step grid side)
    CHECK (! box.contains (175, 401) && ! box.contains (175, 425)); // the row's edges above and below the box
}
