#include "core/LynxBanks.h"

#include <algorithm>
#include <map>
#include <mutex>

namespace lynx::banks
{
#define LYNX_BANK(kind, factory) std::vector<Preset> factory();
#include "core/LynxBankList.inc"
#undef LYNX_BANK

namespace
{
struct Registry
{
    std::mutex mutex;
    std::map<InstrumentKind, Factory> factories;
    std::map<InstrumentKind, std::vector<Preset>> built;
};

Registry& registry()
{
    static Registry r;
    static const bool listed = [] {
#define LYNX_BANK(kind, factory) r.factories[InstrumentKind::kind] = &factory;
#include "core/LynxBankList.inc"
#undef LYNX_BANK
        return true;
    }();
    (void) listed;
    return r;
}

constexpr const char* pathPrefix = "lynxbank:";
} // namespace

Registrar::Registrar (InstrumentKind kind, Factory factory)
{
    auto& r = registry();
    const std::lock_guard<std::mutex> lock (r.mutex);
    r.factories[kind] = factory;
    r.built.erase (kind);
}

const std::vector<Preset>& bankOf (InstrumentKind kind)
{
    static const std::vector<Preset> none;
    auto& r = registry();
    const std::lock_guard<std::mutex> lock (r.mutex);
    if (auto it = r.built.find (kind); it != r.built.end())
        return it->second;
    const auto f = r.factories.find (kind);
    if (f == r.factories.end() || f->second == nullptr)
        return none;
    return r.built.emplace (kind, f->second()).first->second;
}

bool hasBank (InstrumentKind kind) { return ! bankOf (kind).empty(); }

std::vector<InstrumentKind> kindsWithBanks()
{
    std::vector<InstrumentKind> kinds;
    {
        auto& r = registry();
        const std::lock_guard<std::mutex> lock (r.mutex);
        for (const auto& [kind, factory] : r.factories)
            if (factory != nullptr)
                kinds.push_back (kind);
    }
    kinds.erase (std::remove_if (kinds.begin(), kinds.end(), [] (InstrumentKind k) { return ! hasBank (k); }), kinds.end());
    return kinds;
}

std::optional<ChannelDef> withPreset (const ChannelDef& current, int index)
{
    const auto& bank = bankOf (current.instrument);
    if (index < 0 || index >= (int) bank.size() || bank[(std::size_t) index].apply == nullptr)
        return std::nullopt;
    auto out = current;
    bank[(std::size_t) index].apply (out);
    out.instrument = current.instrument;
    out.name = bank[(std::size_t) index].name;
    return out;
}

std::optional<ChannelDef> defaultChannel (InstrumentKind kind, const std::string& engineName)
{
    const auto& bank = bankOf (kind);
    if (bank.empty() || bank.front().apply == nullptr)
        return std::nullopt;
    ChannelDef def;
    def.instrument = kind;
    bank.front().apply (def);
    def.instrument = kind;
    def.name = engineName;
    return def;
}

std::string pathOf (int index) { return pathPrefix + std::to_string (index); }

int indexOfPath (const std::string& path)
{
    const std::string prefix = pathPrefix;
    if (path.size() <= prefix.size() || path.compare (0, prefix.size(), prefix) != 0)
        return -1;
    int v = 0;
    for (auto i = prefix.size(); i < path.size(); ++i)
    {
        if (path[i] < '0' || path[i] > '9' || v > 1000000)
            return -1;
        v = v * 10 + (path[i] - '0');
    }
    return v;
}

std::string relativeOf (const Preset& preset)
{
    return "LYNX/" + (preset.category.empty() ? std::string() : preset.category + "/") + preset.name;
}
} // namespace lynx::banks
