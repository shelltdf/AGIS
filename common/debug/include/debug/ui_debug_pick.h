#pragma once

#include <windows.h>

void AgisUiDebugPickInit(HINSTANCE inst);
void AgisUiDebugPickShutdown();
bool AgisUiDebugPickHandleMessage(MSG* msg);
