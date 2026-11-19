// ---------- Module type names ----------

inline const char* moduleName(std::uint32_t type) noexcept
{
    switch (type)
    {
        case module::miniSynth: return "MiniSynth";
        case module::dwSampler: return "DW Sampler";
        case module::gmSynth: return "GM Synth";
        case module::transistorBass: return "Transistor Bass";
        case module::superSaw: return "SuperSaw";
        case module::slicer: return "Slicer";
        case module::threeOsc: return "3xOsc";
        case module::soundFont: return "SoundFont";
        case module::tuner: return "Tuner";
        case module::analyzer: return "Analyzer";
        default: return nullptr;
    }
}

// ---------- Parser ----------

struct ParseResult
{
    bool ok = false;
    std::string error;
    Project project;
};

ParseResult parseFlm(std::span<const std::uint8_t> bytes);

} // namespace lynx::flm_android