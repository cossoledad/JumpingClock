#pragma once

#include "clock_data.h"

constexpr UINT updateProbeDetailsMessage = WM_APP + 3;
class Probe;

bool EditSettings(HINSTANCE instance, HWND owner, HWND& activeDialog, Settings& settings,
                  const std::vector<TimeZone>& zones, const Probe& probe);
