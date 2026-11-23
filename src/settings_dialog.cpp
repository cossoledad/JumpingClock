#include "probe.h"
#include "settings_dialog.h"

#include <commctrl.h>
#include "resources.h"

#include <algorithm>
#include <iterator>

namespace {

struct SettingsDialog {
    HWND owner;
    HWND& active;
    Settings draft;
    const std::vector<TimeZone>& zones;
    const Probe& probe;
    bool startup;
    bool loading = false;
    int legacyCity = CB_ERR;
};

std::wstring ReadTarget(HWND window) {
    wchar_t text[33]{};
    GetDlgItemTextW(window, IDC_TARGET, text, static_cast<int>(std::size(text)));
    std::wstring result = text;
    const auto first = result.find_first_not_of(L" \t");
    if (first == std::wstring::npos) return {};
    return result.substr(first, result.find_last_not_of(L" \t") - first + 1);
}

bool ValidTarget(const std::wstring& text) {
    ULONG address{};
    return text.empty() || ParseTarget(text.c_str(), address);
}

bool HasZone(const std::vector<TimeZone>& zones, PCWSTR zone) {
    return std::ranges::binary_search(zones, std::wstring_view(zone), {}, &TimeZone::id);
}

int Selection(HWND window) {
    return static_cast<int>(SendDlgItemMessageW(window, IDC_CLOCKS, LB_GETCURSEL, 0, 0));
}

void ShowStatus(HWND window, const SettingsDialog& data) {
    const int row = Selection(window);
    if (row < 0) return;
    const std::wstring& target = data.draft.clocks[static_cast<size_t>(row)].target;
    const std::wstring status = !ValidTarget(target) ? L"IPv4 无效" : data.probe.Details(target.c_str());
    SetDlgItemTextW(window, IDC_STATUS, status.c_str());
}

void Preview(HWND window, const SettingsDialog& data) {
    SendMessageW(data.owner, previewSettingsMessage, 0, reinterpret_cast<LPARAM>(&data.draft));
    ShowStatus(window, data);
}

void LoadRow(HWND window, SettingsDialog& data, size_t row) {
    const auto& entry = data.draft.clocks[row];
    const HWND combo = GetDlgItem(window, IDC_CITY);
    data.loading = true;
    if (data.legacyCity != CB_ERR) {
        SendMessageW(combo, CB_DELETESTRING, data.legacyCity, 0);
        data.legacyCity = CB_ERR;
    }
    int selected = CB_ERR;
    const int count = static_cast<int>(SendMessageW(combo, CB_GETCOUNT, 0, 0));
    for (int i = 0; i < count; ++i) {
        const auto index = static_cast<size_t>(SendMessageW(combo, CB_GETITEMDATA, i, 0));
        const auto& city = cities[index];
        if (entry.city == city.name && entry.zoneId == city.zoneId) {
            selected = i;
            break;
        }
    }
    if (selected == CB_ERR) {
        const auto result = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(entry.city.c_str()));
        if (result >= 0) {
            selected = data.legacyCity = static_cast<int>(result);
            SendMessageW(combo, CB_SETITEMDATA, selected, CB_ERR);
        }
    }
    SendMessageW(combo, CB_SETCURSEL, selected, 0);
    SetDlgItemTextW(window, IDC_TARGET, entry.target.c_str());
    data.loading = false;
    ShowStatus(window, data);
}

void RefreshList(HWND window, SettingsDialog& data, size_t selection) {
    const HWND list = GetDlgItem(window, IDC_CLOCKS);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const auto& entry : data.draft.clocks) {
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(entry.city.c_str()));
    }
    SendMessageW(list, LB_SETCURSEL, selection, 0);
    EnableWindow(GetDlgItem(window, IDC_ADD), data.draft.clocks.size() < maxClocks &&
                 SendDlgItemMessageW(window, IDC_CITY, CB_GETCOUNT, 0, 0) > 0);
    EnableWindow(GetDlgItem(window, IDC_REMOVE), data.draft.clocks.size() > 1);
    LoadRow(window, data, selection);
}

void UpdateOpacity(HWND window, int opacity) {
    const auto value = std::to_wstring(opacity) + L"%";
    SetDlgItemTextW(window, IDC_OPACITY_VALUE, value.c_str());
}

bool Save(HWND window, SettingsDialog& data) {
    for (size_t i = 0; i < data.draft.clocks.size(); ++i) {
        if (!ValidTarget(data.draft.clocks[i].target)) {
            SendDlgItemMessageW(window, IDC_CLOCKS, LB_SETCURSEL, i, 0);
            LoadRow(window, data, i);
            SetFocus(GetDlgItem(window, IDC_TARGET));
            return false;
        }
    }
    const bool previous = IsStartupEnabled();
    const bool changed = data.startup != previous;
    if (changed && !SetStartupEnabled(data.startup)) {
        MessageBoxW(window, L"自启更新失败。", L"设置", MB_OK | MB_ICONWARNING);
        return false;
    }
    if (SaveSettings(data.draft)) return true;
    const bool restored = !changed || SetStartupEnabled(previous);
    MessageBoxW(window, restored ? L"保存失败。" : L"保存失败；自启恢复失败。",
                L"设置", MB_OK | MB_ICONWARNING);
    return false;
}

void Initialize(HWND window, SettingsDialog& data) {
    data.active = window;
    const HWND combo = GetDlgItem(window, IDC_CITY);
    for (size_t i = 0; i < cities.size(); ++i) {
        if (!HasZone(data.zones, cities[i].zoneId)) continue;
        const LRESULT row = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(cities[i].name));
        if (row >= 0) SendMessageW(combo, CB_SETITEMDATA, row, i);
    }
    SendDlgItemMessageW(window, IDC_TARGET, EM_SETLIMITTEXT, 32, 0);
    CheckDlgButton(window, IDC_SHOWCITY, data.draft.showCity ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(window, IDC_SHOWZONE, data.draft.showZone ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(window, IDC_STARTUP, data.startup ? BST_CHECKED : BST_UNCHECKED);
    SendDlgItemMessageW(window, IDC_OPACITY, TBM_SETRANGE, TRUE, MAKELPARAM(minOpacity, 100));
    SendDlgItemMessageW(window, IDC_OPACITY, TBM_SETPOS, TRUE, data.draft.opacity);
    UpdateOpacity(window, data.draft.opacity);
    RefreshList(window, data, 0);
}

void AddClock(HWND window, SettingsDialog& data) {
    const auto available = [&](const City& city) { return HasZone(data.zones, city.zoneId); };
    auto city = std::ranges::find_if(cities, [&](const City& option) {
        return available(option) && std::ranges::none_of(data.draft.clocks, [&](const ClockEntry& entry) {
            return entry.city == option.name;
        });
    });
    if (city == cities.end()) city = std::ranges::find_if(cities, available);
    if (city == cities.end()) return;
    data.draft.clocks.push_back({city->name, city->zoneId, {}});
    RefreshList(window, data, data.draft.clocks.size() - 1);
    SetFocus(GetDlgItem(window, IDC_CITY));
}

INT_PTR CALLBACK SettingsProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INITDIALOG) {
        auto& data = *reinterpret_cast<SettingsDialog*>(lParam);
        SetWindowLongPtrW(window, DWLP_USER, lParam);
        Initialize(window, data);
        return TRUE;
    }
    auto* data = reinterpret_cast<SettingsDialog*>(GetWindowLongPtrW(window, DWLP_USER));
    if (!data) return FALSE;
    switch (message) {
    case WM_NCDESTROY:
        data->active = nullptr;
        return FALSE;
    case updateProbeDetailsMessage:
        ShowStatus(window, *data);
        return TRUE;
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lParam) != GetDlgItem(window, IDC_OPACITY)) return FALSE;
        data->draft.opacity = static_cast<int>(SendDlgItemMessageW(window, IDC_OPACITY, TBM_GETPOS, 0, 0));
        UpdateOpacity(window, data->draft.opacity);
        Preview(window, *data);
        return TRUE;
    case WM_COMMAND:
        break;
    default:
        return FALSE;
    }

    if (data->loading) return FALSE;
    const int row = Selection(window);
    const auto selection = static_cast<size_t>(row);
    const WORD notification = HIWORD(wParam);
    switch (LOWORD(wParam)) {
    case IDOK:
        if (Save(window, *data)) EndDialog(window, IDOK);
        return TRUE;
    case IDCANCEL:
        EndDialog(window, IDCANCEL);
        return TRUE;
    case IDC_CLOCKS:
        if (row >= 0 && notification == LBN_SELCHANGE) LoadRow(window, *data, selection);
        return TRUE;
    case IDC_ADD:
        if (data->draft.clocks.size() >= maxClocks) return TRUE;
        AddClock(window, *data);
        break;
    case IDC_REMOVE:
        if (row < 0 || data->draft.clocks.size() <= 1) return TRUE;
        data->draft.clocks.erase(data->draft.clocks.begin() + row);
        RefreshList(window, *data, std::min(selection, data->draft.clocks.size() - 1));
        break;
    case IDC_CITY: {
        if (row < 0 || notification != CBN_SELCHANGE) return TRUE;
        const LRESULT item = SendDlgItemMessageW(window, IDC_CITY, CB_GETCURSEL, 0, 0);
        if (item < 0) return TRUE;
        const LRESULT index = SendDlgItemMessageW(window, IDC_CITY, CB_GETITEMDATA, item, 0);
        if (index < 0 || static_cast<size_t>(index) >= cities.size()) return TRUE;
        const City& city = cities[static_cast<size_t>(index)];
        auto& entry = data->draft.clocks[selection];
        entry.city = city.name;
        entry.zoneId = city.zoneId;
        // Keep the open drop-down intact while previewing keyboard and mouse selection.
        const HWND list = GetDlgItem(window, IDC_CLOCKS);
        const LRESULT top = SendMessageW(list, LB_GETTOPINDEX, 0, 0);
        SendMessageW(list, LB_DELETESTRING, selection, 0);
        SendMessageW(list, LB_INSERTSTRING, selection, reinterpret_cast<LPARAM>(entry.city.c_str()));
        SendMessageW(list, LB_SETCURSEL, selection, 0);
        SendMessageW(list, LB_SETTOPINDEX, top, 0);
        break;
    }
    case IDC_TARGET:
        if (row < 0 || notification != EN_CHANGE) return TRUE;
        data->draft.clocks[selection].target = ReadTarget(window);
        break;
    case IDC_SHOWCITY:
        data->draft.showCity = IsDlgButtonChecked(window, IDC_SHOWCITY) == BST_CHECKED;
        break;
    case IDC_SHOWZONE:
        data->draft.showZone = IsDlgButtonChecked(window, IDC_SHOWZONE) == BST_CHECKED;
        break;
    case IDC_STARTUP:
        data->startup = IsDlgButtonChecked(window, IDC_STARTUP) == BST_CHECKED;
        return TRUE;
    default:
        return FALSE;
    }
    Preview(window, *data);
    return TRUE;
}

} // namespace

bool EditSettings(HINSTANCE instance, HWND owner, HWND& activeDialog, const Settings& settings,
                  const std::vector<TimeZone>& zones, const Probe& probe) {
    const INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES};
    if (!InitCommonControlsEx(&controls)) return false;
    SettingsDialog data{owner, activeDialog, settings, zones, probe, IsStartupEnabled()};
    const INT_PTR result = DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), owner,
                                          SettingsProc, reinterpret_cast<LPARAM>(&data));
    if (result == -1) MessageBoxW(owner, L"设置打开失败。", L"JumpingClock", MB_OK | MB_ICONERROR);
    return result == IDOK;
}
