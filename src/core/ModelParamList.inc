// Automatable engine parameters (core/ModelParams.h): one line per engine, LYNX_MODEL_PARAMS (<InstrumentKind name>,
// <function returning modelparams::Table>), the function declared in core/ModelParams.h or in the engine's header.
LYNX_MODEL_PARAMS (toxic, toxicModelTable)       // round 168n
LYNX_MODEL_PARAMS (poizone, poizoneModelTable)   // round 168n
LYNX_MODEL_PARAMS (fruitkick, fruitKickModelTable)   // round 168p (core/DrumControls.cpp)
LYNX_MODEL_PARAMS (plucked, pluckedModelTable)
LYNX_MODEL_PARAMS (drumpad, drumpadModelTable)
LYNX_MODEL_PARAMS (drumaxx, drumaxxModelTable)
LYNX_MODEL_PARAMS (bassdrum, bassDrumModelTable)
LYNX_MODEL_PARAMS (drumsynth, drumSynthModelTable)
LYNX_MODEL_PARAMS (sakura, sakuraModelTable)
LYNX_MODEL_PARAMS (slayer, slayerModelTable)
