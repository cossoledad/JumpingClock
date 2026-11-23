#include "clock_data.h"

#include <algorithm>
#include <array>
#include <cwchar>

namespace {

constexpr wchar_t settingsKey[] = L"Software\\JumpingClock";
constexpr wchar_t startupKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t startupValue[] = L"JumpingClock";

bool WriteValue(PCWSTR key, PCWSTR name, DWORD type, const void* data, DWORD bytes) {
    return RegSetKeyValueW(HKEY_CURRENT_USER, key, name, type, data, bytes) == ERROR_SUCCESS;
}

LONGLONG AsTicks(const SYSTEMTIME& time) {
    FILETIME ft{};
    SystemTimeToFileTime(&time, &ft);
    ULARGE_INTEGER value{};
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return static_cast<LONGLONG>(value.QuadPart);
}

} // namespace

std::vector<TimeZone> LoadTimeZones() {
    std::vector<TimeZone> zones;
    DYNAMIC_TIME_ZONE_INFORMATION rules{};
    for (DWORD i = 0; EnumDynamicTimeZoneInformation(i, &rules) == ERROR_SUCCESS; ++i) {
        zones.push_back({rules.TimeZoneKeyName, rules});
    }
    std::ranges::sort(zones, {}, &TimeZone::id);
    return zones;
}

ClockTime ConvertTime(const SYSTEMTIME& utc, const TimeZone* zone) {
    SYSTEMTIME local{};
    if (!zone || !SystemTimeToTzSpecificLocalTimeEx(&zone->rules, &utc, &local)) {
        return {L"—:—", L"—"};
    }
    constexpr LONGLONG ticksPerMinute = 600'000'000;
    int offset = static_cast<int>((AsTicks(local) - AsTicks(utc)) / ticksPerMinute);
    const wchar_t sign = offset < 0 ? L'−' : L'+';
    offset = std::abs(offset);
    wchar_t timeText[8]{}, offsetText[16]{};
    swprintf_s(timeText, L"%02u:%02u", static_cast<unsigned>(local.wHour),
               static_cast<unsigned>(local.wMinute));
    swprintf_s(offsetText, L"UTC%lc%02d:%02d", sign, offset / 60, offset % 60);
    return {timeText, offsetText};
}

Settings LoadSettings() {
    Settings settings;
    std::array<wchar_t, 4096> buffer{};
    DWORD bytes = static_cast<DWORD>(sizeof(buffer));
    if (RegGetValueW(HKEY_CURRENT_USER, settingsKey, L"Settings", RRF_RT_REG_MULTI_SZ,
                     nullptr, buffer.data(), &bytes) != ERROR_SUCCESS) {
        return settings;
    }

    std::vector<std::wstring> values;
    for (const wchar_t* item = buffer.data(); *item; item += wcslen(item) + 1) {
        values.emplace_back(item);
    }
    if (values.size() < 7 || values[0] != L"1" || (values.size() - 5) % 2 != 0 ||
        (values.size() - 5) / 2 > maxClocks) {
        return settings;
    }

    settings.showCity = values[1] == L"1";
    settings.showZone = values[2] == L"1";
    wchar_t* end{};
    const long opacity = wcstol(values[3].c_str(), &end, 10);
    if (*end == L'\0' && opacity >= minOpacity && opacity <= 100) {
        settings.opacity = static_cast<int>(opacity);
    }
    if (values[4] != L"-" && values[4].size() <= 15) {
        settings.target = values[4];
    }
    settings.clocks.clear();
    for (size_t i = 5; i < values.size(); i += 2) {
        settings.clocks.push_back({values[i].substr(0, maxCityLength), values[i + 1]});
    }
    return settings;
}

bool SaveSettings(const Settings& settings) {
    std::wstring buffer;
    auto append = [&buffer](const std::wstring& value) {
        buffer += value;
        buffer += L'\0';
    };
    append(L"1");
    append(settings.showCity ? L"1" : L"0");
    append(settings.showZone ? L"1" : L"0");
    append(std::to_wstring(settings.opacity));
    append(settings.target.empty() ? L"-" : settings.target);
    for (const auto& clock : settings.clocks) {
        append(clock.city);
        append(clock.zoneId);
    }
    return WriteValue(settingsKey, L"Settings", REG_MULTI_SZ, buffer.c_str(),
                      static_cast<DWORD>((buffer.size() + 1) * sizeof(wchar_t)));
}

bool LoadFloatingVisible() {
    DWORD visible = 1;
    DWORD bytes = sizeof(visible);
    return RegGetValueW(HKEY_CURRENT_USER, settingsKey, L"FloatingVisible", RRF_RT_REG_DWORD,
                        nullptr, &visible, &bytes) != ERROR_SUCCESS || visible != 0;
}

bool SaveFloatingVisible(bool visible) {
    const DWORD value = visible ? 1 : 0;
    return WriteValue(settingsKey, L"FloatingVisible", REG_DWORD, &value, sizeof(value));
}

std::optional<POINT> LoadPosition() {
    POINT position{};
    DWORD bytes = sizeof(position);
    if (RegGetValueW(HKEY_CURRENT_USER, settingsKey, L"Position", RRF_RT_REG_BINARY,
                     nullptr, &position, &bytes) == ERROR_SUCCESS && bytes == sizeof(position)) {
        return position;
    }
    return std::nullopt;
}

bool SavePosition(POINT position) {
    return WriteValue(settingsKey, L"Position", REG_BINARY, &position, sizeof(position));
}

bool IsStartupEnabled() {
    return RegGetValueW(HKEY_CURRENT_USER, startupKey, startupValue, RRF_RT_REG_SZ,
                        nullptr, nullptr, nullptr) == ERROR_SUCCESS;
}

bool SetStartupEnabled(bool enabled) {
    if (!enabled) {
        const LSTATUS result = RegDeleteKeyValueW(HKEY_CURRENT_USER, startupKey, startupValue);
        return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
    }
    // Windows limits the command in a Run entry to 260 characters.
    wchar_t path[MAX_PATH]{};
    const DWORD size = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (size == 0 || size + 2 > MAX_PATH) {
        return false;
    }
    const std::wstring command = L"\"" + std::wstring(path, size) + L"\"";
    return WriteValue(startupKey, startupValue, REG_SZ, command.c_str(),
                      static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
}
