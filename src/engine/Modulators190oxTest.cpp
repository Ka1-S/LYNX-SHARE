// Round 190ox (modulators stage 2): the app's --selftest part for what lives outside the core — the .lynx round trip of
// every kind, an older project's text unchanged, the copy for 0.1.30 (the losses listed), and a hosted plugin's state
// captured at a modulated parameter's base.

#include "engine/Modulators190oxTest.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include "core/Effects.h"
#include "engine/PluginHost.h"
#include "engine/ProjectDowngrade.h"
#include "engine/ProjectFile.h"

namespace lynx
{
namespace
{
// a hosted plugin with one parameter whose saved state is that parameter's value as text
struct ValueStatePlugin final : juce::AudioPluginInstance
{
    struct Parameter final : juce::HostedAudioProcessorParameter
    {
        float getValue() const override { return value.load(); }
        void setValue (float v) override { value.store (v); }
        float getDefaultValue() const override { return 0.0f; }
        juce::String getName (int) const override { return "Value"; }
        juce::String getLabel() const override { return {}; }
        juce::String getParameterID() const override { return "value"; }
        float getValueForText (const juce::String& text) const override { return text.getFloatValue(); }
        std::atomic<float> value { 0.0f };
    };
    ValueStatePlugin()
        : AudioPluginInstance (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
        addHostedParameter (std::make_unique<Parameter>());
    }
    const juce::String getName() const override { return "Modulation base probe"; }
    void fillInPluginDescription (juce::PluginDescription& d) const override { d.name = getName(); }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& state) override
    {
        const auto text = juce::String (getParameters()[0]->getValue(), 3);
        state.replaceAll (text.toRawUTF8(), text.getNumBytesAsUTF8());
    }
    void setStateInformation (const void* data, int size) override
    {
        getParameters()[0]->setValue (juce::String::fromUTF8 (static_cast<const char*> (data), size).getFloatValue());
    }
};

juce::String decoded (const std::string& base64)
{
    juce::MemoryBlock block;
    block.fromBase64Encoding (juce::String (base64));
    return block.toString();
}

Song stage2Song()
{
    auto song = Song::makeDefault();
    if (song.mixer.size() < 3)
        song.mixer.resize (3);
    if (song.channels.empty())
        song.channels.resize (1);
    song.mixer[2].effects.push_back (makeEffectSlot (EffectKind::plugin));
    song.mixer[2].effects.back().plugin.id = "probe";
    int k = 0;
    for (int kind = 0; kind < modKindCount; ++kind)
    {
        auto m = Modulator::make ((ModKind) kind);
        m.name = std::string (Modulator::kindName ((ModKind) kind)) + " " + std::to_string (++k);
        m.host = kind % 2 == 0 ? AutomationTarget { AutomationTargetKind::channelVolume, 0, 0, 0 } : AutomationTarget { AutomationTargetKind::mixerVolume, 1, 0, 0 };
        if (m.kind == ModKind::follower)
            m.input = { AutomationTargetKind::mixerVolume, 2, 0, 0 };
        if (m.kind != ModKind::lfo) // (an LFO keeps stage 1's fields only: the others stay at their defaults)
        {
            m.attack = 12.5f + (float) kind;
            m.macro = 0.25f;
        }
        m.routes = { { { AutomationTargetKind::mixerPan, 1, 0, 0 }, 0.125f * (float) (kind + 1) } };
        if (kind == 0)
            m.routes.push_back ({ { AutomationTargetKind::effectParam, 2, (int) song.mixer[2].effects.size() - 1, 0 }, 0.5f, 0.25f });
        song.modulators.push_back (m);
    }
    Automation knob; // an automation of the Macro's knob (modulator 5)
    knob.target = { AutomationTargetKind::modulatorMacro, 5, 0, 0 };
    knob.points = { { 0, 0.5f, 0.0f } };
    song.automations.push_back (knob);
    return song;
}
} // namespace

juce::String modulators190oxSelfTest()
{
    // 1. every kind back from the file as it was, at its place
    const auto song = stage2Song();
    juce::String why;
    const auto json = ProjectFile::toJson (song);
    const auto back = ProjectFile::fromJson (json, why);
    if (! back)
        return "ERROR round trip: " + why;
    if (! (back->modulators == song.modulators))
        return "ERROR round trip: modulators differ";
    if (back->automations.empty() || ! (back->automations.back().target == song.automations.back().target))
        return "ERROR round trip: the macro automation's target";
    if (! json.contains ("modulators190ox"))
        return "ERROR the stage 2 kinds not in their own list";

    // 2. a project with stage 1's modulators only writes what it wrote before
    {
        auto older = Song::makeDefault();
        Modulator m;
        m.name = "LFO 1";
        m.routes = { { { AutomationTargetKind::mixerPan, 1, 0, 0 }, 0.25f } };
        older.modulators.push_back (m);
        const auto text = ProjectFile::toJson (older);
        const auto root = juce::JSON::parse (text);
        if (root.hasProperty ("modulators190ox"))
            return "ERROR an LFO-only project writes modulators190ox";
        const auto* entry = root["modulators"][0].getDynamicObject();
        if (entry == nullptr)
            return "ERROR an LFO-only project lost its modulator";
        const juce::StringArray stage1 { "name", "shape", "sync", "beats", "hz", "phase", "unipolar", "seed", "routes" };
        for (const auto& p : entry->getProperties())
            if (! stage1.contains (p.name.toString()))
                return "ERROR an LFO-only project writes " + p.name.toString();
        if (root["modulators"][0]["routes"][0].size() != 5)
            return "ERROR an LFO-only route is not stage 1's five values";
        const auto again = ProjectFile::fromJson (text, why);
        if (! again || ! (again->modulators == older.modulators) || ProjectFile::toJson (*again) != text)
            return "ERROR an LFO-only project's round trip";
    }

    // 3. the copy for 0.1.30: the other kinds, the hosted route and the macro clip's link go, each listed
    {
        const auto copy = olderlynx::forRelease (song, "0.1.30");
        if (! copy.ok || copy.json.contains ("modulators190ox") || copy.json.contains ("modulatorMacro"))
            return "ERROR copy for 0.1.30 keeps stage 2";
        int lines = 0;
        for (const auto& l : copy.lost)
        {
            const auto text = juce::String (l.text);
            if (text.contains ("modulators are not in this release") && l.detail.contains ("Steps") && l.detail.contains ("Envelope follower"))
                ++lines;
            if (text.contains ("hosted plugins' parameters") && l.detail.contains ("LFO 1"))
                ++lines;
            if (text.contains ("Macro modulator's knob"))
                ++lines;
        }
        if (lines != 3)
            return "ERROR copy for 0.1.30 lists " + juce::String (lines) + " of the 3 losses";
        const auto older = ProjectFile::fromJson (copy.json, why);
        if (! older || older->modulators.size() != 1 || older->modulators[0].routes.size() != 1)
            return "ERROR copy for 0.1.30 reads back wrong";
    }

    // 4. a modulated parameter is saved at its base, the plugin's own state otherwise
    {
        PluginEffect effect (std::make_unique<ValueStatePlugin>(), 512);
        effect.setParameter (0, 0.875f); // (mid-swing, as the renderer last sent it)
        const auto plain = effect.captureState();
        const auto atBase = effect.captureState ({ { 0, 0.25f } });
        if (! plain || ! atBase)
            return "ERROR capture failed";
        if (! decoded (*plain).startsWith ("0.875") || ! decoded (*atBase).startsWith ("0.25"))
            return "ERROR state: " + decoded (*plain) + " / " + decoded (*atBase);
        effect.setParameter (0, 0.5f); // (the audio thread's values go on after the capture)
        if (std::abs (effect.getInstance().getParameters()[0]->getValue() - 0.5f) > 1.0e-6f)
            return "ERROR the parameter held after a capture";
    }
    return "OK";
}
} // namespace lynx
