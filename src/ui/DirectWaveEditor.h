#pragma once

// DirectWave's editor (round 190hz, issue #157; core/DirectWave.h, core/DirectWaveState.h). FL's layout: its form is
// 990 × 627 (DirectWave's editor_x64.dll, TSynthEditorForm; docs/analise/REA_rodada190hz_dw.md §1), drawn 1.25 × larger:
// the program bar (index, arrows, name), the zone grid with the key / velocity map and the keyboard, the Zone / Sample /
// Program pages at the bottom and the browser / options panel on the right. Every edit writes the channel's FL state
// through the document (only the fields that changed), so the program goes back to FL as edited.

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx
{
class AudioEngine;
struct ChannelDef;
struct Song;

// `channel`: the Channel Rack channel (−1 in a Patcher: `locate` finds the node)
std::unique_ptr<juce::Component> makeDirectWaveEditor (AudioEngine& engine, std::function<ChannelDef* (Song&)> locate, int channel);

// The editor test (LYNX --dwedittest): one command for the DirectWave editor inside `root`, its result as text. The
// commands are listed at DirectWaveEditor::test.
juce::String directWaveEditorTest (juce::Component& root, const juce::String& command);
} // namespace lynx
