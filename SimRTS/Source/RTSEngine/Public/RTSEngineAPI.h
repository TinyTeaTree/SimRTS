#pragma once

// UBT defines RTSENGINE_API as DLLEXPORT/DLLIMPORT. Those tokens need Unreal's
// platform macros. Standalone smoke tests compile with -DRTSENGINE_STANDALONE
// and never pull Unreal headers.
#if defined(RTSENGINE_STANDALONE)
#ifndef RTSENGINE_API
#define RTSENGINE_API
#endif
#else
#include "HAL/Platform.h"
#endif
