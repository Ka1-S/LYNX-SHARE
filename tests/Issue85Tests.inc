// Issue #85: every start described all LV2 plugins again (25 min for 510), because nothing told the scan that an LV2
// plugin had not changed. The stamp of the LV2 folders is that answer: same stamp, no child process.

#include "core/PluginScanPolicy.h"

TEST (lv2_stamp_changes_only_when_a_bundle_does)
{
    using lynx::scanpolicy::lv2Stamp;
    const auto stamp = lv2Stamp ({ { "/usr/lib/lv2/a.lv2", 100 }, { "/usr/lib/lv2/b.lv2", 200 } });

    // nothing changed, even if the disk lists the folders in another order: a stamp that moves rescans everything
    CHECK (lv2Stamp ({ { "/usr/lib/lv2/b.lv2", 200 }, { "/usr/lib/lv2/a.lv2", 100 } }) == stamp);

    CHECK (lv2Stamp ({ { "/usr/lib/lv2/a.lv2", 100 }, { "/usr/lib/lv2/b.lv2", 201 } }) != stamp); // updated
    CHECK (lv2Stamp ({ { "/usr/lib/lv2/a.lv2", 100 } }) != stamp);                                // removed
    CHECK (lv2Stamp ({ { "/usr/lib/lv2/a.lv2", 100 }, { "/usr/lib/lv2/b.lv2", 200 }, { "/usr/lib/lv2/c.lv2", 1 } }) != stamp); // installed

    CHECK (lv2Stamp ({}).empty()); // no LV2 plugins: nothing to describe, and no difference from no stamp at all
}
