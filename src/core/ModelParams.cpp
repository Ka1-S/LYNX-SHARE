#include "core/ModelParams.h"

#include <cmath>
#include <cstring>
#include <map>

#include "core/DrumControls.h"
#include "core/BeepMap.h"
#include "core/Granulizer.h"
#include "core/Kepler.h"
#include "core/ModelParamsSynths.h"
#include "core/PocketSuperSaw.h"
#include "core/PoiZone.h"
#include "core/Sawer.h"
#include "core/Toxic.h"
#include "core/TransistorBass.h"

namespace lynx::modelparams
{
namespace
{
using Offset = int (*) (const std::vector<std::uint8_t>&, int);

template <Offset offset>
std::optional<float> stateFloat (const ChannelDef& c, const EngineParam& p)
{
    const auto at = offset (c.pluginState, p.index);
    if (at < 0 || (std::size_t) at + 4 > c.pluginState.size())
        return std::nullopt;
    float v = 0.0f;
    std::memcpy (&v, c.pluginState.data() + at, 4);
    return std::isfinite (v) ? v : p.low;
}

template <Offset offset>
bool setStateFloat (ChannelDef& c, const EngineParam& p, float raw)
{
    const auto at = offset (c.pluginState, p.index);
    if (at < 0 || (std::size_t) at + 4 > c.pluginState.size())
        return false;
    std::memcpy (c.pluginState.data() + at, &raw, 4);
    return true;
}

const std::map<InstrumentKind, Table>& tables()
{
    static const std::map<InstrumentKind, Table> all = [] {
        std::map<InstrumentKind, Table> t;
#define LYNX_MODEL_PARAMS(kind, function) t[InstrumentKind::kind] = function();
#include "core/ModelParamList.inc"
#undef LYNX_MODEL_PARAMS
        return t;
    }();
    return all;
}
} // namespace

Table toxicModelTable() { return { &toxicParams(), &stateFloat<toxicParamOffset>, &setStateFloat<toxicParamOffset> }; }
namespace
{
// (PoiZone's wheels, 157 / 158, live in modelParams: FL's state has no room for them)
std::optional<float> poizoneValue (const ChannelDef& c, const EngineParam& p)
{
    if (p.index == poizone::modWheelParam || p.index == poizone::bendWheelParam)
    {
        const auto [mw, pb] = poizone::wheelsOf (c);
        return p.index == poizone::modWheelParam ? mw : pb;
    }
    return stateFloat<poizoneParamOffset> (c, p);
}
bool setPoizoneValue (ChannelDef& c, const EngineParam& p, float raw)
{
    return poizone::setWheel (c, p.index, raw) || setStateFloat<poizoneParamOffset> (c, p, raw);
}
} // namespace

Table poizoneModelTable() { return { &poizoneParams(), &poizoneValue, &setPoizoneValue }; }

const Table* tableOf (InstrumentKind kind)
{
    const auto& all = tables();
    const auto it = all.find (kind);
    return it != all.end() && it->second.params != nullptr ? &it->second : nullptr;
}

const EngineParam* find (InstrumentKind kind, int index)
{
    const auto* t = tableOf (kind);
    return t != nullptr ? findEngineParam (*t->params, index) : nullptr;
}

const EngineParam* findByName (InstrumentKind kind, const std::string& name)
{
    if (const auto* t = tableOf (kind))
        for (const auto& p : *t->params)
            if (p.name == name)
                return &p;
    return nullptr;
}

std::optional<float> rawValue (const ChannelDef& channel, int index)
{
    const auto* t = tableOf (channel.instrument);
    const auto* p = t != nullptr ? findEngineParam (*t->params, index) : nullptr;
    if (p == nullptr || t->get == nullptr)
        return std::nullopt;
    return t->get (channel, *p);
}

bool setRawValue (ChannelDef& channel, int index, float raw)
{
    const auto* t = tableOf (channel.instrument);
    const auto* p = t != nullptr ? findEngineParam (*t->params, index) : nullptr;
    if (p == nullptr || t->set == nullptr || ! std::isfinite (raw))
        return false;
    return t->set (channel, *p, p->raw (p->normalised (raw)));
}

std::optional<float> value01 (const ChannelDef& channel, int index)
{
    const auto* p = find (channel.instrument, index);
    const auto raw = rawValue (channel, index);
    if (p == nullptr || ! raw)
        return std::nullopt;
    return p->normalised (*raw);
}

bool setValue01 (ChannelDef& channel, int index, float value)
{
    const auto* p = find (channel.instrument, index);
    return p != nullptr && std::isfinite (value) && setRawValue (channel, index, p->raw (value));
}
} // namespace lynx::modelparams
