#pragma once

#include <windows.h>

#include <optional>
#include <string>
#include <vector>

constexpr size_t maxClocks = 16;
constexpr size_t maxCityLength = 40;
constexpr int minOpacity = 35;

struct ClockEntry {
    std::wstring city;
    std::wstring zoneId;
};

struct Settings {
    std::vector<ClockEntry> clocks{
        {L"北京", L"China Standard Time"},
        {L"洛杉矶", L"Pacific Standard Time"},
    };
    std::wstring target;
    bool showCity = true;
    bool showZone = true;
    int opacity = 90;
};

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
