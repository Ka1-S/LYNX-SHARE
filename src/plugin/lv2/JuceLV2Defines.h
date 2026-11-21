#pragma once

// JUCE's LV2 wrapper reads its URI from this header (round 190ov): cmake/LynxPlugins.cmake passes it per plugin, from the
// clap id of plugin/plugins.def (https://lynx-share.com/plugins/lv2/<the id after "com.ka1-s.lynx.">). It never changes once
// released: hosts store it in their projects.
#ifndef JucePlugin_LV2URI
 #error "JucePlugin_LV2URI: set by cmake/LynxPlugins.cmake"
#endif
