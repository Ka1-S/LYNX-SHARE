#pragma once

// core/ScriptDialog.h's Python side, for the script hosts (they include pocketpy.h): the ScriptDialog class's source
// and its native.

#include "core/ScriptDialog.h"
#include "pocketpy.h"

namespace lynx::scriptform
{
// The Python source of class ScriptDialog; append it to the host module's source. It calls `_h.execute`, so the
// module must `import <host module> as _h` and define `_i` (rounds a number to int) before it.
const char* dialogPython();
// Binds `execute` (ScriptDialog.Execute's native) into the host's native module
void bindExecute (py_GlobalRef hostModule);
} // namespace lynx::scriptform
