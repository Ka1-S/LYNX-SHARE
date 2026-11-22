#pragma once

#include <juce_core/juce_core.h>

namespace lynx
{
// Round 190ox (modulators stage 2): the --selftest checks outside the core (engine/Modulators190oxTest.cpp): "OK" or
// "ERROR <what>".
juce::String modulators190oxSelfTest();
} // namespace lynx
