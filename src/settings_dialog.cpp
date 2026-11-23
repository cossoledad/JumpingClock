#include "probe.h"
#include "settings_dialog.h"

#include "resources.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace {

struct EntryDialog {
    const std::vector<TimeZone>& zones;
    ClockEntry entry;
};

struct SettingsDialog {
    HINSTANCE instance;
    HWND& active;
    Settings draft;
    const std::vector<TimeZone>& zones;
    const Probe& probe;
    std::wstring currentTarget;
};

std::wstring Trim(std::wstring text) {
    const auto first = text.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    text.resize(text.find_last_not_of(L" \t\r\n") + 1);
    text.erase(0, first);
    return text;
}

void Warn(HWND window, PCWSTR message) {
    MessageBoxW(window, message, L"JumpingClock", MB_OK | MB_ICONWARNING);
}

INT_PTR CALLBACK EntryProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_INITDIALOG) {
        auto& data = *reinterpret_cast<EntryDialog*>(lParam);
        SetWindowLongPtrW(window, DWLP_USER, lParam);
        SendDlgItemMessageW(window, IDC_CITY, EM_SETLIMITTEXT, maxCityLength, 0);
        SetDlgItemTextW(window, IDC_CITY, data.entry.city.c_str());
        const HWND combo = GetDlgItem(window, IDC_ZONE);
        int selected = CB_ERR;
        for (size_t i = 0; i < data.zones.size(); ++i) {
            const auto& zone = data.zones[i];
            std::wstring name = zone.rules.StandardName;
            if (name.empty()) name = zone.id;
            if (name != zone.id) name += L" · " + zone.id;
            const LRESULT row = SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
            if (row < 0) continue;
            SendMessageW(combo, CB_SETITEMDATA, static_cast<WPARAM>(row), static_cast<LPARAM>(i));
            if (zone.id == data.entry.zoneId) selected = static_cast<int>(row);
        }
        SendMessageW(combo, CB_SETCURSEL, selected, 0);
        return TRUE;
    }
    if (message != WM_COMMAND) return FALSE;
    if (LOWORD(wParam) == IDCANCEL) {
        EndDialog(window, IDCANCEL);
        return TRUE;
    }
    if (LOWORD(wParam) != IDOK) return FALSE;

    wchar_t city[maxCityLength + 1]{};
    GetDlgItemTextW(window, IDC_CITY, city, static_cast<int>(std::size(city)));
    std::wstring name = Trim(city);
    if (name.empty()) {
        Warn(window, L"请输入城市名称。");
        SetFocus(GetDlgItem(window, IDC_CITY));
        return TRUE;
    }
    const LRESULT row = SendDlgItemMessageW(window, IDC_ZONE, CB_GETCURSEL, 0, 0);
    if (row == CB_ERR) {
        Warn(window, L"请选择时区。");
        SetFocus(GetDlgItem(window, IDC_ZONE));
        return TRUE;
    }
    auto& data = *reinterpret_cast<EntryDialog*>(GetWindowLongPtrW(window, DWLP_USER));
    const LRESULT zone = SendDlgItemMessageW(window, IDC_ZONE, CB_GETITEMDATA, row, 0);
    data.entry = {std::move(name), data.zones.at(static_cast<size_t>(zone)).id};
    EndDialog(window, IDOK);
    return TRUE;
}

void RefreshList(HWND window, const Settings& settings, size_t selection) {
    const HWND list = GetDlgItem(window, IDC_CLOCKS);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const auto& clock : settings.clocks) {
        const std::wstring line = clock.city + L"  ·  " + clock.zoneId;
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
    }
    SendMessageW(list, LB_SETCURSEL, selection, 0);
    EnableWindow(GetDlgItem(window, IDC_ADD), settings.clocks.size() < maxClocks);
    EnableWindow(GetDlgItem(window, IDC_REMOVE), settings.clocks.size() > 1);
}

void RefreshStatus(HWND window, const SettingsDialog& data) {
    const std::wstring text = data.currentTarget.empty()
        ? data.probe.Details()
        : data.currentTarget + L"  ·  " + data.probe.Details();
    SetDlgItemTextW(window, IDC_STATUS, text.c_str());
}

bool Save(HWND window, SettingsDialog& data) {
    BOOL valid{};
    const UINT opacity = GetDlgItemInt(window, IDC_OPACITY, &valid, FALSE);
    if (!valid || opacity < minOpacity || opacity > 100) {
        Warn(window, L"不透明度必须在 35 到 100 之间。");
        SetFocus(GetDlgItem(window, IDC_OPACITY));
        return false;
    }
    wchar_t target[65]{};
    GetDlgItemTextW(window, IDC_TARGET, target, static_cast<int>(std::size(target)));
    std::wstring address = Trim(target);
    ULONG parsed{};
    if (!address.empty() && !ParseTarget(address.c_str(), parsed)) {
        Warn(window, L"请输入有效的单播 IPv4 地址，或留空停用探测。");
        SetFocus(GetDlgItem(window, IDC_TARGET));
        return false;
    }
    data.draft.target = std::move(address);
    data.draft.opacity = static_cast<int>(opacity);
    data.draft.showCity = IsDlgButtonChecked(window, IDC_SHOWCITY) == BST_CHECKED;
    data.draft.showZone = IsDlgButtonChecked(window, IDC_SHOWZONE) == BST_CHECKED;

    const bool previous = IsStartupEnabled();
    const bool startup = IsDlgButtonChecked(window, IDC_STARTUP) == BST_CHECKED;
    if (startup != previous && !SetStartupEnabled(startup)) {
        Warn(window, L"无法更新登录自启动设置。");
        return false;
    }
    if (!SaveSettings(data.draft)) {
        const bool restored = startup == previous || SetStartupEnabled(previous);
        Warn(window, restored ? L"无法保存设置。" : L"无法保存设置，也无法恢复原来的自启动状态。");
        return false;
    }
    return true;
}

INT_PTR CALLBACK SettingsProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* data = reinterpret_cast<SettingsDialog*>(GetWindowLongPtrW(window, DWLP_USER));
    if (message == WM_INITDIALOG) {
        data = reinterpret_cast<SettingsDialog*>(lParam);
        SetWindowLongPtrW(window, DWLP_USER, lParam);
        data->active = window;
        RefreshList(window, data->draft, 0);
        CheckDlgButton(window, IDC_SHOWCITY, data->draft.showCity ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(window, IDC_SHOWZONE, data->draft.showZone ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(window, IDC_STARTUP, IsStartupEnabled() ? BST_CHECKED : BST_UNCHECKED);
        SendDlgItemMessageW(window, IDC_OPACITY, EM_SETLIMITTEXT, 3, 0);
        SendDlgItemMessageW(window, IDC_TARGET, EM_SETLIMITTEXT, 64, 0);
        SetDlgItemInt(window, IDC_OPACITY, data->draft.opacity, FALSE);
        SetDlgItemTextW(window, IDC_TARGET, data->draft.target.c_str());
        RefreshStatus(window, *data);
        EnableWindow(GetDlgItem(window, IDC_ADD), !data->zones.empty() && data->draft.clocks.size() < maxClocks);
        EnableWindow(GetDlgItem(window, IDC_EDIT), !data->zones.empty());
        return TRUE;
    }
    if (!data) return FALSE;
    if (message == updateProbeDetailsMessage) {
        RefreshStatus(window, *data);
        return TRUE;
    }
    if (message == WM_NCDESTROY) {
        data->active = nullptr;
        return FALSE;
    }
    if (message != WM_COMMAND) return FALSE;

    const WORD command = LOWORD(wParam);
    if (command == IDOK) {
        if (Save(window, *data)) EndDialog(window, IDOK);
        return TRUE;
    }
    if (command == IDCANCEL) {
        EndDialog(window, IDCANCEL);
        return TRUE;
    }
    const LRESULT row = SendDlgItemMessageW(window, IDC_CLOCKS, LB_GETCURSEL, 0, 0);
    auto& clocks = data->draft.clocks;
    const bool edit = command == IDC_EDIT || (command == IDC_CLOCKS && HIWORD(wParam) == LBN_DBLCLK);
    if ((command == IDC_ADD && clocks.size() < maxClocks) || (edit && row >= 0)) {
        if (data->zones.empty()) return TRUE;
        EntryDialog entry{data->zones, edit ? clocks[static_cast<size_t>(row)] : ClockEntry{}};
        if (DialogBoxParamW(data->instance, MAKEINTRESOURCEW(IDD_CLOCK), window, EntryProc,
                            reinterpret_cast<LPARAM>(&entry)) == IDOK) {
            const size_t selected = edit ? static_cast<size_t>(row) : clocks.size();
            if (edit) clocks[selected] = std::move(entry.entry);
            else clocks.push_back(std::move(entry.entry));
            RefreshList(window, data->draft, selected);
        }
        return TRUE;
    }
    if (command == IDC_REMOVE && row >= 0 && clocks.size() > 1) {
        clocks.erase(clocks.begin() + row);
        RefreshList(window, data->draft, std::min(static_cast<size_t>(row), clocks.size() - 1));
        return TRUE;
    }
    return FALSE;
}

} // namespace

bool EditSettings(HINSTANCE instance, HWND owner, HWND& activeDialog, Settings& settings,
                  const std::vector<TimeZone>& zones, const Probe& probe) {
    SettingsDialog data{instance, activeDialog, settings, zones, probe, settings.target};
    const INT_PTR result = DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_SETTINGS), owner,
                                          SettingsProc, reinterpret_cast<LPARAM>(&data));
    if (result == -1) Warn(owner, L"无法打开设置窗口。");
    if (result != IDOK) return false;
    settings = std::move(data.draft);
    return true;
}
