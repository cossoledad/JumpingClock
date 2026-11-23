#include "probe.h"

#include <algorithm>
#include <cstdio>
#include <string_view>

namespace {

bool Check(bool condition, const char* description) {
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", description);
    return condition;
}

bool TimeZones() {
    const auto zones = LoadTimeZones();
    const auto allMapped = std::ranges::all_of(cities, [&](const City& city) {
        return std::ranges::binary_search(zones, std::wstring_view(city.zoneId), {}, &TimeZone::id);
    });
    const auto pacific = std::ranges::find(zones, L"Pacific Standard Time", &TimeZone::id);
    const auto* zone = pacific == zones.end() ? nullptr : &*pacific;
    const auto before = ConvertTime({2026, 3, 0, 8, 9, 59}, zone);
    const auto after = ConvertTime({2026, 3, 0, 8, 10, 0}, zone);
    return Check(allMapped, "English city catalog references Windows zones") &
           Check(before.time == L"01:59" && before.offset == L"UTC−08:00" &&
                 after.time == L"03:00" && after.offset == L"UTC−07:00", "Pacific DST boundary");
}

bool SettingsMigration() {
    // Redirect only this process to a test key so no user settings are read or written.
    const std::wstring key = L"Software\\JumpingClockSmoke-" + std::to_wstring(GetCurrentProcessId()) +
                             L"-" + std::to_wstring(GetTickCount64());
    HKEY temporary{};
    const bool opened = RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS,
                                        nullptr, &temporary, nullptr) == ERROR_SUCCESS;
    if (!Check(opened, "create isolated settings key")) return false;
    const bool redirected = RegOverridePredefKey(HKEY_CURRENT_USER, temporary) == ERROR_SUCCESS;
    bool passed = Check(redirected, "redirect test settings");
    if (redirected) {
        constexpr wchar_t v1[] = L"1\0" L"0\0" L"1\0" L"78\0" L"127.0.0.1\0"
                                 L"北京\0" L"China Standard Time\0" L"洛杉矶\0" L"Pacific Standard Time\0";
        passed &= Check(RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\JumpingClock", L"Settings",
                                        REG_MULTI_SZ, v1, sizeof(v1)) == ERROR_SUCCESS, "seed v1 settings");
        Settings settings = LoadSettings();
        passed &= Check(settings.clocks.size() == 2 && settings.clocks[0].city == L"Beijing" &&
                        settings.clocks[1].city == L"Los Angeles" && !settings.showCity &&
                        settings.showZone && settings.opacity == 78 &&
                        settings.clocks[0].target == L"127.0.0.1" &&
                        settings.clocks[1].target == L"127.0.0.1", "migrate v1 shared probe and city names");
        settings.clocks[0].target.clear();
        settings.clocks[1].target = L"127.0.0.2";
        passed &= Check(SaveSettings(settings), "save v2 settings");
        const Settings restored = LoadSettings();
        passed &= Check(restored.clocks.size() == 2 && restored.clocks[0].target.empty() &&
                        restored.clocks[1].target == L"127.0.0.2", "restore separate targets");
        passed &= Check(RegOverridePredefKey(HKEY_CURRENT_USER, nullptr) == ERROR_SUCCESS,
                        "restore registry mapping");
    }
    RegCloseKey(temporary);
    passed &= Check(RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str()) == ERROR_SUCCESS, "remove test settings");
    return passed;
}

LRESULT CALLBACK ProbeWindow(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* probe = reinterpret_cast<Probe*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (probe && message == WM_TIMER && wParam == probeTimer) {
        probe->OnTimer();
        return 0;
    }
    if (probe && message == probeCompleteMessage) {
        probe->OnComplete();
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool Probes() {
    Probe probe;
    WNDCLASSW wc{};
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpfnWndProc = ProbeWindow;
    wc.lpszClassName = L"JumpingClock.Smoke";
    if (!Check(RegisterClassW(&wc) != 0, "register probe window")) return false;
    const HWND window = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0,
                                        HWND_MESSAGE, nullptr, wc.hInstance, &probe);
    if (!Check(window != nullptr, "create probe window")) return false;
    probe.Initialize(window);

    constexpr wchar_t first[] = L"127.0.0.1";
    constexpr wchar_t second[] = L"127.0.0.2";
    Settings baseline;
    baseline.clocks[0].target = first;
    baseline.clocks[1].target = first;
    auto draft = baseline;
    draft.clocks[0].target = second;
    probe.SetTargets(baseline);
    probe.SetTargets(draft, &baseline);
    bool passed = Check(probe.State(first) == ProbeState::waiting &&
                        probe.State(second) == ProbeState::waiting, "track separate targets");
    probe.SetTargets(baseline);
    const auto deadline = GetTickCount64() + 4000;
    while (probe.State(first) != ProbeState::reachable && GetTickCount64() < deadline) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
    }
    passed &= Check(probe.State(first) == ProbeState::reachable, "loopback ICMP");
    passed &= Check(probe.State(second) == ProbeState::disabled, "discard preview-only target");
    draft.clocks[1].target.clear();
    probe.SetTargets(draft, &baseline);
    passed &= Check(probe.State(first) == ProbeState::disabled, "pause replaced target");
    probe.SetTargets(baseline);
    passed &= Check(probe.State(first) == ProbeState::reachable &&
                    probe.State(second) == ProbeState::disabled, "restore pre-preview probe state");
    KillTimer(window, probeTimer);
    probe.Shutdown();
    DestroyWindow(window);
    return passed;
}

} // namespace

int main() {
    const bool zones = TimeZones();
    const bool settings = SettingsMigration();
    const bool probes = Probes();
    return zones && settings && probes ? 0 : 1;
}
