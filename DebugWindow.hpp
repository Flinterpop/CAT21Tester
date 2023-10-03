#pragma once

#include "imgui.h"

void DebugWindow(bool* p_open);
void pme(const char* buf);

void pmef(char* fmt, ...);

void ClearLog();
