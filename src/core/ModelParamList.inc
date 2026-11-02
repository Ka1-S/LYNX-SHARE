// Automatable engine parameters (core/ModelParams.h): one line per engine, LYNX_MODEL_PARAMS (<InstrumentKind name>,
// <function returning modelparams::Table>), the function declared in core/ModelParams.h or in the engine's header.
LYNX_MODEL_PARAMS (toxic, toxicModelTable)       // round 168n
LYNX_MODEL_PARAMS (poizone, poizoneModelTable)   // round 168n
LYNX_MODEL_PARAMS (kepler, keplerModelTable)                 // round 168q
LYNX_MODEL_PARAMS (keplerExo, keplerExoModelTable)           // round 168q
LYNX_MODEL_PARAMS (transistorbass, transistorBassModelTable) // round 168q
LYNX_MODEL_PARAMS (sawer, sawerModelTable)                   // round 168q
LYNX_MODEL_PARAMS (beepmap, beepMapModelTable)               // round 168q
LYNX_MODEL_PARAMS (granulizer, granulizerModelTable)         // round 168q
