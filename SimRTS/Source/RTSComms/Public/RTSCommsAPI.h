#pragma once

// UBT defines RTSCOMMS_API as DLLEXPORT/DLLIMPORT. Those tokens need Unreal's
// platform macros. Standalone tests can compile with -DRTSCOMMS_STANDALONE.
#if defined(RTSCOMMS_STANDALONE)
#ifndef RTSCOMMS_API
#define RTSCOMMS_API
#endif
#else
#include "HAL/Platform.h"
#endif
