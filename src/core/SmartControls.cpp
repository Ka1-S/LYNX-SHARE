#include "core/SmartControls.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "core/Effects.h"
#include "core/GoalKnobs.h"
#include "core/ModelParams.h"
#include "core/SimpleTracks.h"
#include "core/SoundFont.h"
#include "core/SynthParams.h"

namespace lynx::smartcontrols
{
namespace
{
bool channelKind (AutomationTargetKind k) noexcept
{
    return k == AutomationTargetKind::channelParam || k == AutomationTargetKind::channelVolume || k == AutomationTargetKind::channelPan
           || k == AutomationTargetKind::channelAdjust;
}

const ChannelDef* channelOf (const Song& song, const Owner& o) noexcept
{
    return o.channel >= 0 && o.channel < (int) song.channels.size() ? &song.channels[(std::size_t) o.channel] : nullptr;
}

const MixerTrack* insertOf (const Song& song, const Owner& o) noexcept
{
    return o.insert > 0 && o.insert < (int) song.mixer.size() ? &song.mixer[(std::size_t) o.insert] : nullptr;
}

// the control with its index pointed at this track's channel / insert
Control resolved (const Owner& o, Control c) noexcept
{
    if (c.source == Source::target)
        c.target.index = channelKind (c.target.kind) ? o.channel : o.insert;
    else if (c.source == Source::goal)
        c.target.index = o.insert;
    return c;
}

std::string lower (std::string s)
{
    for (auto& ch : s)
        ch = (char) std::tolower ((unsigned char) ch);
    return s;
}

Control channelParam (const Owner& o, int param) { return { Source::target, { AutomationTargetKind::channelParam, o.channel, 0, param } }; }

// the instrument's parameters, every one (the remap menu) in the instrument's own order
std::vector<Control> allInstrumentParams (const Song& song, const Owner& o)
{
    std::vector<Control> out;
    const auto* ch = channelOf (song, o);
    if (ch == nullptr)
        return out;
    switch (ch->instrument)
    {
        case InstrumentKind::synth:
            for (int k = 0; k < synthparam::count; ++k)
                out.push_back (channelParam (o, synthparam::target (k)));
            break;
        case InstrumentKind::sampler:
            for (int k = 0; k < samplerparam::count; ++k)
                out.push_back (channelParam (o, k));
            break;
        case InstrumentKind::soundfont:
            for (int k = 0; k < sf::paramCount; ++k)
                if (sf::automatable (k))
                    out.push_back (channelParam (o, k));
            break;
        default:
            if (const auto* table = modelparams::tableOf (ch->instrument); table != nullptr && table->params != nullptr)
                for (const auto& p : *table->params)
                    if (! p.hasUnits())
                        out.push_back (channelParam (o, p.index));
            break;
    }
    return out;
}

std::string instrumentParamName (const ChannelDef& ch, int param)
{
    switch (ch.instrument)
    {
        case InstrumentKind::synth:
        {
            if (! synthparam::valid (param))
                return {};
            const auto k = synthparam::indexOf (param);
            const auto osc = synthparam::oscOf (k);
            const std::string name = synthparam::rangeOf (k).name;
            return osc > 0 ? "Osc " + std::to_string (osc) + " " + lower (name) : name;
        }
        case InstrumentKind::sampler: return samplerparam::name (param);
        case InstrumentKind::soundfont: return param >= 0 && param < sf::paramCount ? sf::paramInfo (param).name : "";
        default:
            if (const auto* p = modelparams::find (ch.instrument, param))
                return p->name;
            return {};
    }
}

bool effectUsable (const EffectSlot& fx) noexcept
{
    return fx.kind != EffectKind::plugin && fx.kind != EffectKind::parallel && fx.kind != EffectKind::patcher;
}

// the chain's knobs for the default mapping: a one-knob effect's knob, else the effect's Mix
std::vector<Control> chainControls (const Song& song, const Owner& o)
{
    std::vector<Control> out;
    const auto* m = insertOf (song, o);
    if (m == nullptr)
        return out;
    for (int slot = 0; slot < (int) m->effects.size(); ++slot)
    {
        const auto& fx = m->effects[(std::size_t) slot];
        if (! effectUsable (fx))
            continue;
        if (goals::active (fx))
            out.push_back ({ Source::goal, { AutomationTargetKind::effectParam, o.insert, slot, 0 } });
        else
            out.push_back ({ Source::target, { AutomationTargetKind::effectMix, o.insert, slot, 0 } });
    }
    return out;
}

std::vector<Control> trackControls (const Owner& o)
{
    return { { Source::reverbSend, { AutomationTargetKind::none, o.insert, 0, 0 } },
             { Source::target, { AutomationTargetKind::mixerPan, o.insert, 0, 0 } },
             { Source::target, { AutomationTargetKind::mixerVolume, o.insert, 0, 0 } } };
}
} // namespace

bool valid (const Song& song, const Owner& owner, const Control& raw)
{
    const auto c = resolved (owner, raw);
    const auto* m = insertOf (song, owner);
    switch (c.source)
    {
        case Source::none: return true;
        case Source::reverbSend: return m != nullptr && owner.insert != simpletracks::sharedReverb (song);
        case Source::goal:
            return m != nullptr && c.target.slot >= 0 && c.target.slot < (int) m->effects.size() && goals::active (m->effects[(std::size_t) c.target.slot]);
        case Source::target: break;
    }
    const auto& t = c.target;
    switch (t.kind)
    {
        case AutomationTargetKind::mixerVolume:
        case AutomationTargetKind::mixerPan: return m != nullptr;
        case AutomationTargetKind::effectMix:
        case AutomationTargetKind::effectParam:
        {
            if (m == nullptr || t.slot < 0 || t.slot >= (int) m->effects.size() || ! effectUsable (m->effects[(std::size_t) t.slot]))
                return false;
            if (t.kind == AutomationTargetKind::effectMix)
                return true;
            return t.param >= 0 && t.param < (int) effectInfo (m->effects[(std::size_t) t.slot].kind).params.size() && t.param < maxEffectParams;
        }
        case AutomationTargetKind::channelParam:
        {
            const auto* ch = channelOf (song, owner);
            return ch != nullptr && ! instrumentParamName (*ch, t.param).empty();
        }
        default: return false;
    }
}

Label labelOf (const Song& song, const Owner& owner, const Control& raw)
{
    const auto c = resolved (owner, raw);
    Label l;
    const auto* m = insertOf (song, owner);
    switch (c.source)
    {
        case Source::none: return l;
        case Source::reverbSend: l.name = "Reverb"; return l;
        case Source::goal:
            l.group = Group::effects;
            if (m != nullptr && c.target.slot >= 0 && c.target.slot < (int) m->effects.size())
                l.name = goals::name (m->effects[(std::size_t) c.target.slot].goal);
            return l;
        case Source::target: break;
    }
    const auto& t = c.target;
    switch (t.kind)
    {
        case AutomationTargetKind::mixerVolume: l.name = "Volume"; break;
        case AutomationTargetKind::mixerPan: l.name = "Pan"; break;
        case AutomationTargetKind::effectMix:
        case AutomationTargetKind::effectParam:
            l.group = Group::effects;
            if (m != nullptr && t.slot >= 0 && t.slot < (int) m->effects.size())
            {
                const auto& info = effectInfo (m->effects[(std::size_t) t.slot].kind);
                l.effect = info.name;
                if (t.kind == AutomationTargetKind::effectMix)
                    l.name = info.name, l.effect = "Mix";
                else if (t.param >= 0 && t.param < (int) info.params.size())
                    l.name = info.params[(std::size_t) t.param].name;
            }
            break;
        case AutomationTargetKind::channelParam:
            l.group = Group::sound;
            if (const auto* ch = channelOf (song, owner))
                l.name = instrumentParamName (*ch, t.param);
            break;
        default: break;
    }
    return l;
}

float valueOf (const Song& song, const Owner& owner, const Control& raw)
{
    const auto c = resolved (owner, raw);
    const auto* m = insertOf (song, owner);
    switch (c.source)
    {
        case Source::none: return 0.0f;
        case Source::reverbSend: return m != nullptr ? simpletracks::reverbSend (song, owner.insert) : 0.0f;
        case Source::goal:
            return m != nullptr && c.target.slot >= 0 && c.target.slot < (int) m->effects.size() ? m->effects[(std::size_t) c.target.slot].goalAmount : 0.0f;
        case Source::target: break;
    }
    if (c.target.kind == AutomationTargetKind::effectMix)
        return m != nullptr && c.target.slot >= 0 && c.target.slot < (int) m->effects.size() ? m->effects[(std::size_t) c.target.slot].mix : 0.0f;
    if (! valid (song, owner, raw))
        return 0.0f;
    return std::clamp (song.targetValue (c.target), 0.0f, 1.0f);
}

bool setValue (Song& song, const Owner& owner, const Control& raw, float value)
{
    if (! valid (song, owner, raw))
        return false;
    value = std::clamp (value, 0.0f, 1.0f);
    const auto c = resolved (owner, raw);
    switch (c.source)
    {
        case Source::none: return false;
        case Source::reverbSend: return simpletracks::setReverbSend (song, owner.insert, value < 0.005f ? 0.0f : value);
        case Source::goal:
            goals::setAmount (song.mixer[(std::size_t) owner.insert].effects[(std::size_t) c.target.slot], value);
            return true;
        case Source::target: break;
    }
    if (c.target.kind == AutomationTargetKind::effectMix)
    {
        song.mixer[(std::size_t) owner.insert].effects[(std::size_t) c.target.slot].mix = value;
        return true;
    }
    return song.setTargetValue (c.target, value);
}

float defaultValueOf (const Song& song, const Owner& owner, const Control& raw)
{
    const auto c = resolved (owner, raw);
    const auto* m = insertOf (song, owner);
    switch (c.source)
    {
        case Source::none: return 0.0f;
        case Source::reverbSend: return 0.0f;
        case Source::goal:
            return m != nullptr && c.target.slot >= 0 && c.target.slot < (int) m->effects.size() ? goals::defaultAmount (m->effects[(std::size_t) c.target.slot].goal) : 0.5f;
        case Source::target: break;
    }
    switch (c.target.kind)
    {
        case AutomationTargetKind::mixerVolume: return unityFader;
        case AutomationTargetKind::mixerPan: return 0.5f;
        case AutomationTargetKind::effectMix: return 1.0f;
        case AutomationTargetKind::effectParam:
            if (m != nullptr && c.target.slot >= 0 && c.target.slot < (int) m->effects.size())
                if (const auto& info = effectInfo (m->effects[(std::size_t) c.target.slot].kind); c.target.param >= 0 && c.target.param < (int) info.params.size())
                    return info.params[(std::size_t) c.target.param].defaultValue;
            return 0.5f;
        case AutomationTargetKind::channelParam:
            if (const auto* ch = channelOf (song, owner); ch != nullptr && ch->instrument == InstrumentKind::soundfont && c.target.param >= 0
                                                          && c.target.param < sf::paramCount)
                return sf::normalise (c.target.param, sf::paramInfo (c.target.param).def);
            return 0.5f;
        default: return 0.5f;
    }
}

std::vector<Control> instrumentControls (const Song& song, const Owner& owner, int most)
{
    std::vector<Control> out;
    const auto* ch = channelOf (song, owner);
    if (ch == nullptr || most <= 0)
        return out;
    auto add = [&] (int param) {
        if ((int) out.size() < most)
            out.push_back (channelParam (owner, param));
    };
    switch (ch->instrument)
    {
        case InstrumentKind::synth:
            for (const auto k : { (int) synthparam::cutoff, (int) synthparam::resonance, (int) synthparam::attack, (int) synthparam::release })
                add (synthparam::target (k));
            return out;
        case InstrumentKind::sampler:
            for (const auto k : { (int) samplerparam::cutoff, (int) samplerparam::resonance, (int) samplerparam::attack, (int) samplerparam::release })
                add (k);
            return out;
        case InstrumentKind::soundfont:
            for (const auto k : { (int) sf::pCutoff, (int) sf::pResonance, (int) sf::pAttack, (int) sf::pRelease })
                add (k);
            return out;
        default: break;
    }
    // an FL engine's knobs: the first ones whose names say filter, envelope, then drive / character; else its first knobs
    const auto all = allInstrumentParams (song, owner);
    static const char* const words[] = { "cutoff", "freq", "reso", "attack", "release", "decay", "drive", "dist", "detune", "mod", "chorus", "reverb" };
    for (const auto* w : words)
        for (const auto& c : all)
            if ((int) out.size() < most && lower (instrumentParamName (*ch, c.target.param)).find (w) != std::string::npos
                && std::find (out.begin(), out.end(), c) == out.end())
                out.push_back (c);
    for (const auto& c : all)
        if ((int) out.size() < most && std::find (out.begin(), out.end(), c) == out.end())
            out.push_back (c);
    return out;
}

std::vector<Control> candidates (const Song& song, const Owner& owner)
{
    auto out = allInstrumentParams (song, owner);
    if (const auto* m = insertOf (song, owner))
        for (int slot = 0; slot < (int) m->effects.size(); ++slot)
        {
            const auto& fx = m->effects[(std::size_t) slot];
            if (! effectUsable (fx))
                continue;
            if (goals::active (fx))
                out.push_back ({ Source::goal, { AutomationTargetKind::effectParam, owner.insert, slot, 0 } });
            out.push_back ({ Source::target, { AutomationTargetKind::effectMix, owner.insert, slot, 0 } });
            const auto& info = effectInfo (fx.kind);
            for (int p = 0; p < (int) info.params.size() && p < maxEffectParams; ++p)
                out.push_back ({ Source::target, { AutomationTargetKind::effectParam, owner.insert, slot, p } });
        }
    for (const auto& c : trackControls (owner))
        if (valid (song, owner, c))
            out.push_back (c);
    return out;
}

Mapping defaults (const Song& song, const Owner& owner)
{
    Mapping out {};
    if (insertOf (song, owner) == nullptr)
        return out;
    const auto sound = instrumentControls (song, owner, 4);
    const auto chain = chainControls (song, owner);
    std::vector<Control> trackPart;
    for (const auto& c : trackControls (owner))
        if (valid (song, owner, c))
            trackPart.push_back (c);
    std::vector<Control> list (sound);
    const auto chainRoom = std::max (0, knobCount - (int) list.size() - (int) trackPart.size());
    const auto firstChain = std::min ((int) chain.size(), chainRoom);
    list.insert (list.end(), chain.begin(), chain.begin() + firstChain);
    list.insert (list.end(), trackPart.begin(), trackPart.end());
    list.insert (list.end(), chain.begin() + firstChain, chain.end());
    for (int k = 0; k < knobCount && k < (int) list.size(); ++k)
        out[(std::size_t) k] = list[(std::size_t) k];
    return out;
}

Mapping mappingOf (const Song& song, const Owner& owner)
{
    auto out = defaults (song, owner);
    const auto* m = insertOf (song, owner);
    if (m == nullptr || m->smartControls.empty())
        return out;
    const auto user = decode (m->smartControls);
    for (int k = 0; k < knobCount; ++k)
        if (const auto& u = user[(std::size_t) k]; u && valid (song, owner, *u))
            out[(std::size_t) k] = resolved (owner, *u);
    return out;
}

std::string encode (const std::array<std::optional<Control>, knobCount>& user)
{
    if (std::none_of (user.begin(), user.end(), [] (const auto& u) { return u.has_value(); }))
        return {};
    std::string out;
    for (int k = 0; k < knobCount; ++k)
    {
        if (k > 0)
            out += ';';
        const auto& u = user[(std::size_t) k];
        if (! u)
            out += 'd';
        else if (u->source == Source::none)
            out += 'n';
        else if (u->source == Source::reverbSend)
            out += 'r';
        else if (u->source == Source::goal)
            out += "g:" + std::to_string (u->target.slot);
        else
            out += "t:" + std::to_string ((int) u->target.kind) + ":" + std::to_string (u->target.slot) + ":" + std::to_string (u->target.param);
    }
    return out;
}

std::array<std::optional<Control>, knobCount> decode (const std::string& text)
{
    std::array<std::optional<Control>, knobCount> out {};
    std::istringstream in (text);
    std::string part;
    for (int k = 0; k < knobCount && std::getline (in, part, ';'); ++k)
    {
        if (part == "n")
            out[(std::size_t) k] = Control {};
        else if (part == "r")
            out[(std::size_t) k] = Control { Source::reverbSend, {} };
        else if (part.rfind ("g:", 0) == 0)
        {
            Control c { Source::goal, { AutomationTargetKind::effectParam, -1, std::atoi (part.c_str() + 2), 0 } };
            if (c.target.slot >= 0)
                out[(std::size_t) k] = c;
        }
        else if (part.rfind ("t:", 0) == 0)
        {
            int kind = 0, slot = 0, param = 0;
            if (std::sscanf (part.c_str(), "t:%d:%d:%d", &kind, &slot, &param) == 3 && kind > 0 && kind <= (int) AutomationTargetKind::channelAdjust)
                out[(std::size_t) k] = Control { Source::target, { (AutomationTargetKind) kind, -1, slot, param } };
        }
    }
    return out;
}

bool remap (Song& song, const Owner& owner, int knob, std::optional<Control> control)
{
    if (knob < 0 || knob >= knobCount || insertOf (song, owner) == nullptr)
        return false;
    auto& text = song.mixer[(std::size_t) owner.insert].smartControls;
    auto user = decode (text);
    user[(std::size_t) knob] = control;
    text = encode (user);
    return true;
}

bool reset (Song& song, const Owner& owner)
{
    if (insertOf (song, owner) == nullptr)
        return false;
    song.mixer[(std::size_t) owner.insert].smartControls.clear();
    return true;
}
} // namespace lynx::smartcontrols
