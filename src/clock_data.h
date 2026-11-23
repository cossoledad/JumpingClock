#pragma once

#include <windows.h>

#include <optional>
#include <string>
#include <vector>

constexpr size_t maxClocks = 16;
constexpr int minOpacity = 35;

struct ClockEntry {
    std::wstring city;
    std::wstring zoneId;
    std::wstring target;
};

struct Settings {
    std::vector<ClockEntry> clocks{
        {L"Beijing", L"China Standard Time", {}},
        {L"Los Angeles", L"Pacific Standard Time", {}},
    };
    bool showCity = true;
    bool showZone = true;
    int opacity = 90;
};

struct City {
    PCWSTR name;
    PCWSTR zoneId;
};

extern const std::vector<City> cities;

struct TimeZone {
    std::wstring id;
    DYNAMIC_TIME_ZONE_INFORMATION rules;
};

struct ClockTime {
    std::wstring time;
    std::wstring offset;
};

std::vector<TimeZone> LoadTimeZones();
ClockTime ConvertTime(const SYSTEMTIME& utc, const TimeZone* zone);
Settings LoadSettings();
bool SaveSettings(const Settings& settings);
bool LoadFloatingVisible();
bool SaveFloatingVisible(bool visible);
std::optional<POINT> LoadPosition();
bool SavePosition(POINT position);
bool IsStartupEnabled();
bool SetStartupEnabled(bool enabled);
