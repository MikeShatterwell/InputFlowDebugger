// Copyright Mike Desrosiers, All Rights Reserved.

#include "InputFlowDebugger.h"

// Core
#include <Modules/ModuleManager.h>

static_assert(!UE_SERVER, "InputFlowDebugger is not allowed in Server builds.");

IMPLEMENT_MODULE(FInputFlowDebuggerModule, InputFlowDebugger);