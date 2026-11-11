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
LYNX_MODEL_PARAMS (harmless, harmlessModelTable)     // round 168o (core/ModelParamsSynths.h)
LYNX_MODEL_PARAMS (dx10, dx10ModelTable)             // round 168o
LYNX_MODEL_PARAMS (simsynth, simSynthModelTable)     // round 168o
LYNX_MODEL_PARAMS (ogun, ogunModelTable)             // round 168o
LYNX_MODEL_PARAMS (autogun, autogunModelTable)       // round 168o
LYNX_MODEL_PARAMS (minisynth, miniSynthModelTable)   // round 168o
LYNX_MODEL_PARAMS (gms, gmsModelTable)               // round 168o
LYNX_MODEL_PARAMS (morphine, morphineModelTable)     // round 168o
LYNX_MODEL_PARAMS (kepler, keplerModelTable)                 // round 168q
LYNX_MODEL_PARAMS (keplerExo, keplerExoModelTable)           // round 168q
LYNX_MODEL_PARAMS (transistorbass, transistorBassModelTable) // round 168q
LYNX_MODEL_PARAMS (sawer, sawerModelTable)                   // round 168q
LYNX_MODEL_PARAMS (beepmap, beepMapModelTable)               // round 168q
LYNX_MODEL_PARAMS (granulizer, granulizerModelTable)         // round 168q
LYNX_MODEL_PARAMS (pocketSuperSaw, pocketSuperSawModelTable)   // round 190by (core/PocketSuperSaw.h)
