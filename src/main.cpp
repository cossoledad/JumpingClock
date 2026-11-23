#include "probe.h"
#include "clock_data.h"
#include "settings_dialog.h"

#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <utility>

namespace {

constexpr wchar_t clockClass[] = L"JumpingClock.Clock";
constexpr wchar_t popupClass[] = L"JumpingClock.Popup";
constexpr UINT trayMessage = WM_APP + 4;
constexpr UINT revealMessage = WM_APP + 5;
constexpr UINT_PTR minuteTimer = 1;
constexpr UINT_PTR trayTimer = 3;
constexpr UINT trayId = 1;
constexpr UINT showCommand = 1;
constexpr UINT settingsCommand = 2;
constexpr UINT exitCommand = 3;
constexpr UINT recheckCommand = 4;
constexpr COLORREF background = RGB(31, 33, 38);
constexpr COLORREF primaryText = RGB(246, 247, 249);
constexpr COLORREF mutedText = RGB(205, 209, 218);

int Scale(int value, UINT dpi) {
    return MulDiv(value, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
}

COLORREF StatusColor(ProbeState state) {
    switch (state) {
    case ProbeState::reachable: return RGB(107, 221, 159);
    case ProbeState::noReply: return RGB(255, 129, 140);
    case ProbeState::error: return RGB(255, 203, 120);
    default: return RGB(174, 182, 197);
    }
}

bool IsStatusFilled(ProbeState state) {
    return state == ProbeState::reachable || state == ProbeState::noReply || state == ProbeState::error;
}

RECT WorkArea(POINT point) {
    MONITORINFO info{sizeof(info)};
    if (GetMonitorInfoW(MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST), &info)) {
        return info.rcWork;
    }
    RECT rect{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &rect, 0);
    return rect;
}

POINT ClampPosition(POINT point, const RECT& work, SIZE size) {
    point.x = std::clamp(point.x, work.left, std::max(work.left, work.right - size.cx));
    point.y = std::clamp(point.y, work.top, std::max(work.top, work.bottom - size.cy));
    return point;
}

int TextWidth(HDC dc, const std::wstring& text) {
    SIZE size{};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
    return size.cx;
}

HICON MakeTrayIcon(ProbeState state) {
    constexpr int size = 16;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits{};
    const HBITMAP color = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color) return nullptr;
    auto* pixels = static_cast<DWORD*>(bits);
    std::fill_n(pixels, size * size, 0U);
    const auto status = StatusColor(state);
    const DWORD accent = 0xFF000000U | (DWORD{GetRValue(status)} << 16) |
                         (DWORD{GetGValue(status)} << 8) | DWORD{GetBValue(status)};
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const int ring = (x - 7) * (x - 7) + (y - 7) * (y - 7);
            const bool hand = (x == 7 && y >= 3 && y <= 7) || (y == 7 && x >= 7 && x <= 10);
            if ((ring >= 29 && ring <= 47) || hand) {
                pixels[y * size + x] = 0xFFF4F6FA;
            }
            const int dot = (x - 12) * (x - 12) + (y - 12) * (y - 12);
            if (dot <= 13) {
                pixels[y * size + x] = dot <= 8 && (IsStatusFilled(state) || dot >= 4)
                    ? accent : 0xFF282C33;
            }
        }
    }
    const WORD maskBits[size]{};
    const HBITMAP mask = CreateBitmap(size, size, 1, 1, maskBits);
    HICON icon{};
    if (mask) {
        ICONINFO iconInfo{TRUE, 0, 0, mask, color};
        icon = CreateIconIndirect(&iconInfo);
        DeleteObject(mask);
    }
    DeleteObject(color);
    return icon;
}

class ClockView {
public:
    ~ClockView() { ClearFonts(); }

    SIZE Measure(HWND window, const Settings& settings, LONG maxWidth) {
        const UINT dpi = GetDpiForWindow(window);
        if (dpi_ != dpi) {
            ClearFonts();
            dpi_ = dpi;
            labelFont_ = CreateFontW(-Scale(13, dpi_), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Segoe UI");
            timeFont_ = CreateFontW(-Scale(16, dpi_), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Segoe UI");
        }
        pad_ = Scale(7, dpi_);
        gutter_ = Scale(9, dpi_);
        gap_ = Scale(8, dpi_);
        rowHeight_ = Scale(22, dpi_);

        HDC dc = GetDC(window);
        const HGDIOBJ original = SelectObject(dc, Font(labelFont_));
        cityWidth_ = 0;
        if (settings.showCity) {
            for (const auto& clock : settings.clocks) {
                cityWidth_ = std::max(cityWidth_, TextWidth(dc, clock.city));
            }
            cityWidth_ = std::min(cityWidth_, Scale(160, dpi_));
        }
        zoneWidth_ = settings.showZone
            ? std::max(TextWidth(dc, L"UTC+00:00"), TextWidth(dc, L"UTC−00:00")) : 0;
        SelectObject(dc, Font(timeFont_));
        int digitWidth = 0;
        for (wchar_t digit = L'0'; digit <= L'9'; ++digit) {
            digitWidth = std::max(digitWidth, TextWidth(dc, std::wstring(1, digit)));
        }
        const int timeWidth = 4 * digitWidth + TextWidth(dc, L":");
        SelectObject(dc, original);
        ReleaseDC(window, dc);

        const int fixedWidth = 2 * pad_ + gutter_ + timeWidth + (zoneWidth_ ? gap_ + zoneWidth_ : 0);
        cityWidth_ = std::min(cityWidth_, std::max(0, static_cast<int>(maxWidth) - fixedWidth - gap_));
        return {fixedWidth + (cityWidth_ ? gap_ + cityWidth_ : 0),
                2 * pad_ + static_cast<LONG>(settings.clocks.size()) * rowHeight_};
    }

    void Draw(HWND window, const Settings& settings, const std::vector<ClockTime>& times,
              ProbeState state) const {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        SetDCBrushColor(dc, background);
        FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        SetBkMode(dc, TRANSPARENT);

        const auto originalPen = SelectObject(dc, GetStockObject(DC_PEN));
        const auto originalBrush = SelectObject(dc, GetStockObject(IsStatusFilled(state) ? DC_BRUSH : NULL_BRUSH));
        SetDCPenColor(dc, StatusColor(state));
        SetDCBrushColor(dc, StatusColor(state));
        const int diameter = Scale(7, dpi_);
        const int top = pad_ + (rowHeight_ - diameter) / 2;
        Ellipse(dc, pad_, top, pad_ + diameter, top + diameter);
        SelectObject(dc, originalBrush);
        SelectObject(dc, originalPen);

        const HGDIOBJ originalFont = SelectObject(dc, Font(labelFont_));
        const UINT flags = DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS;
        for (size_t i = 0; i < settings.clocks.size() && i < times.size(); ++i) {
            const LONG y = pad_ + static_cast<LONG>(i) * rowHeight_;
            RECT cell{pad_ + gutter_, y, 0, y + rowHeight_};
            SelectObject(dc, Font(labelFont_));
            SetTextColor(dc, mutedText);
            if (cityWidth_) {
                cell.right = cell.left + cityWidth_;
                DrawTextW(dc, settings.clocks[i].city.c_str(), -1, &cell, flags);
                cell.left = cell.right + gap_;
            }
            if (zoneWidth_) {
                cell.right = cell.left + zoneWidth_;
                DrawTextW(dc, times[i].offset.c_str(), -1, &cell, flags);
                cell.left = cell.right + gap_;
            }
            SelectObject(dc, Font(timeFont_));
            SetTextColor(dc, primaryText);
            cell.right = client.right - pad_;
            DrawTextW(dc, times[i].time.c_str(), -1, &cell, flags | DT_RIGHT);
        }
        SelectObject(dc, originalFont);
        EndPaint(window, &paint);
    }

private:
    static HGDIOBJ Font(HFONT font) {
        return font ? font : GetStockObject(DEFAULT_GUI_FONT);
    }

    void ClearFonts() {
        if (labelFont_) DeleteObject(labelFont_);
        if (timeFont_) DeleteObject(timeFont_);
    }

    UINT dpi_{};
    HFONT labelFont_{};
    HFONT timeFont_{};
    int pad_{}, gap_{}, gutter_{}, rowHeight_{}, cityWidth_{}, zoneWidth_{};
};

class App {
public:
    explicit App(HINSTANCE instance) : instance_(instance) {}

    bool Start() {
        taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
        zones_ = LoadTimeZones();
        settings_ = LoadSettings();
        if (!Register(clockClass, WindowProc) || !Register(popupClass, PopupProc)) return false;
        const auto position = LoadPosition();
        const POINT initial = position.value_or(POINT{});
        window_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
                                  clockClass, L"JumpingClock", WS_POPUP, initial.x, initial.y, 1, 1,
                                  nullptr, nullptr, instance_, this);
        if (!window_) return false;

        probe_.Initialize(window_);
        probe_.SetTarget(settings_.target);
        UpdateOpacity();
        PositionClock(!position.has_value());
        AddTray();
        if (LoadFloatingVisible() || !trayAdded_) ShowWindow(window_, SW_SHOWNOACTIVATE);
        RefreshTime();
        return true;
    }

private:
    bool Register(PCWSTR name, WNDPROC procedure) {
        WNDCLASSW wc{};
        wc.hInstance = instance_;
        wc.lpfnWndProc = procedure;
        wc.lpszClassName = name;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        return RegisterClassW(&wc) != 0;
    }

    static App* FromWindow(HWND window, UINT message, LPARAM lParam) {
        if (message == WM_NCCREATE) {
            auto* app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
            return app;
        }
        return reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    }

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        if (auto* app = FromWindow(window, message, lParam); app && app->window_ == window) {
            return app->OnMessage(message, wParam, lParam);
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    static LRESULT CALLBACK PopupProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        if (auto* app = FromWindow(window, message, lParam); app && app->popup_ == window) {
            switch (message) {
            case WM_PAINT:
                app->popupView_.Draw(window, app->settings_, app->times_, app->probe_.State());
                return 0;
            case WM_ACTIVATE:
                if (LOWORD(wParam) == WA_INACTIVE && IsWindowVisible(window)) app->HidePopup();
                return 0;
            case WM_KEYDOWN:
                if (wParam == VK_ESCAPE) app->HidePopup();
                return 0;
            case WM_ERASEBKGND:
                return 1;
            case WM_DPICHANGED:
                if (!app->positioningPopup_ && IsWindowVisible(window)) app->PositionPopup();
                return 0;
            }
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    void PositionClock(bool initial = false, const RECT* suggested = nullptr) {
        RECT current{};
        if (suggested) current = *suggested;
        else GetWindowRect(window_, &current);
        const RECT work = WorkArea({current.left, current.top});
        const SIZE size = clockView_.Measure(window_, settings_, work.right - work.left);
        const int inset = Scale(12, GetDpiForWindow(window_));
        const POINT desired = initial ? POINT{work.right - size.cx - inset, work.top + inset}
                                      : POINT{current.left, current.top};
        const POINT point = dragging_ ? desired : ClampPosition(desired, work, size);
        SetWindowPos(window_, HWND_TOPMOST, point.x, point.y, size.cx, size.cy, SWP_NOACTIVATE);
    }

    void PositionPopup() {
        positioningPopup_ = true;
        const POINT anchor{trayAnchor_.left, trayAnchor_.top};
        const RECT work = WorkArea(anchor);
        SetWindowPos(popup_, nullptr, anchor.x, anchor.y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        const SIZE size = popupView_.Measure(popup_, settings_, work.right - work.left);
        const int gap = Scale(5, GetDpiForWindow(popup_));
        const LONG above = trayAnchor_.top - size.cy - gap;
        const LONG y = above >= work.top ? above : trayAnchor_.bottom + gap;
        const POINT point = ClampPosition({trayAnchor_.right - size.cx, y}, work, size);
        SetWindowPos(popup_, HWND_TOPMOST, point.x, point.y, size.cx, size.cy, SWP_NOACTIVATE);
        positioningPopup_ = false;
    }

    void UpdateOpacity() {
        SetLayeredWindowAttributes(window_, 0, static_cast<BYTE>(MulDiv(settings_.opacity, 255, 100)), LWA_ALPHA);
    }

    void RefreshTime() {
        KillTimer(window_, minuteTimer);
        if (!IsWindowVisible(window_) && (!popup_ || !IsWindowVisible(popup_))) return;
        SYSTEMTIME utc{};
        GetSystemTime(&utc);
        times_.clear();
        times_.reserve(settings_.clocks.size());
        for (const auto& clock : settings_.clocks) {
            const auto zone = std::ranges::lower_bound(zones_, clock.zoneId, {}, &TimeZone::id);
            times_.push_back(ConvertTime(utc, zone != zones_.end() && zone->id == clock.zoneId ? &*zone : nullptr));
        }
        UpdateClockViews();

        const UINT delay = 60'000 - (static_cast<UINT>(utc.wSecond) * 1'000 + utc.wMilliseconds);
        SetCoalescableTimer(window_, minuteTimer, delay, nullptr, 50);
    }

    void UpdateClockViews() {
        if (IsWindowVisible(window_)) InvalidateRect(window_, nullptr, FALSE);
        if (popup_ && IsWindowVisible(popup_)) InvalidateRect(popup_, nullptr, FALSE);
    }

    NOTIFYICONDATAW TrayData() const {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window_;
        data.uID = trayId;
        return data;
    }

    bool SetTray(DWORD operation) {
        auto data = TrayData();
        data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        data.uCallbackMessage = trayMessage;
        data.hIcon = MakeTrayIcon(probe_.State());
        const bool customIcon = data.hIcon != nullptr;
        if (!data.hIcon) data.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        const std::wstring text = std::wstring(L"JumpingClock · ") + ProbeStateText(probe_.State());
        wcsncpy_s(data.szTip, text.c_str(), _TRUNCATE);
        const bool success = Shell_NotifyIconW(operation, &data) != FALSE;
        if (customIcon) DestroyIcon(data.hIcon);
        if (success && operation == NIM_ADD) {
            data.uVersion = NOTIFYICON_VERSION_4;
            trayVersion4_ = Shell_NotifyIconW(NIM_SETVERSION, &data) != FALSE;
        }
        return success;
    }

    void AddTray() {
        KillTimer(window_, trayTimer);
        trayAdded_ = SetTray(NIM_ADD) || SetTray(NIM_MODIFY);
        if (!trayAdded_) SetCoalescableTimer(window_, trayTimer, 2'000, nullptr, 500);
    }

    void ProbeUpdated(ProbeState previous) {
        if (settingsDialog_) SendMessageW(settingsDialog_, updateProbeDetailsMessage, 0, 0);
        if (probe_.State() == previous) return;
        UpdateClockViews();
        if (trayAdded_ && !SetTray(NIM_MODIFY)) AddTray();
    }

    RECT TrayAnchor() const {
        NOTIFYICONIDENTIFIER id{};
        id.cbSize = sizeof(id);
        id.hWnd = window_;
        id.uID = trayId;
        RECT rect{};
        if (SUCCEEDED(Shell_NotifyIconGetRect(&id, &rect))) return rect;
        POINT cursor{};
        GetCursorPos(&cursor);
        return {cursor.x, cursor.y, cursor.x, cursor.y};
    }

    void TogglePopup() {
        if (settingsDialog_) {
            SetForegroundWindow(GetLastActivePopup(settingsDialog_));
            return;
        }
        if (popup_ && IsWindowVisible(popup_)) {
            HidePopup();
            return;
        }
        trayAnchor_ = TrayAnchor();
        if (!popup_) {
            popup_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, popupClass, L"JumpingClock 时区",
                                     WS_POPUP, trayAnchor_.left, trayAnchor_.top, 1, 1,
                                     nullptr, nullptr, instance_, this);
            if (!popup_) return;
        }
        PositionPopup();
        ShowWindow(popup_, SW_SHOW);
        SetForegroundWindow(popup_);
        RefreshTime();
    }

    void HidePopup() {
        ShowWindow(popup_, SW_HIDE);
        RefreshTime();
    }

    void SetFloatingVisible(bool visible) {
        if (!visible && !trayAdded_) {
            MessageBoxW(window_, L"系统托盘暂不可用，无法隐藏时钟。", L"JumpingClock", MB_OK | MB_ICONINFORMATION);
            return;
        }
        if (visible) PositionClock();
        ShowWindow(window_, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
        SaveFloatingVisible(visible);
        RefreshTime();
    }

    void OpenSettings() {
        if (settingsDialog_) return;
        if (popup_ && IsWindowVisible(popup_)) HidePopup();
        const std::wstring oldTarget = settings_.target;
        if (EditSettings(instance_, window_, settingsDialog_, settings_, zones_, probe_)) {
            if (settings_.target != oldTarget) {
                const auto previous = probe_.State();
                probe_.SetTarget(settings_.target);
                ProbeUpdated(previous);
            }
            UpdateOpacity();
            PositionClock();
            RefreshTime();
        }
    }

    void Menu(POINT point) {
        if (settingsDialog_ || menuOpen_) return;
        if (popup_ && IsWindowVisible(popup_)) HidePopup();
        HMENU menu = CreatePopupMenu();
        if (!menu) return;
        AppendMenuW(menu, MF_STRING, showCommand, IsWindowVisible(window_) ? L"隐藏悬浮时钟" : L"显示悬浮时钟");
        AppendMenuW(menu, MF_STRING, settingsCommand, L"设置…");
        if (probe_.State() != ProbeState::disabled) {
            AppendMenuW(menu, MF_STRING, recheckCommand, L"立即探测");
        }
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, exitCommand, L"退出");
        SetForegroundWindow(window_);
        menuOpen_ = true;
        const UINT command = static_cast<UINT>(TrackPopupMenuEx(
            menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, window_, nullptr));
        menuOpen_ = false;
        PostMessageW(window_, WM_NULL, 0, 0);
        if (trayAdded_) {
            auto data = TrayData();
            Shell_NotifyIconW(NIM_SETFOCUS, &data);
        }
        DestroyMenu(menu);

        switch (command) {
        case showCommand: SetFloatingVisible(!IsWindowVisible(window_)); break;
        case settingsCommand: OpenSettings(); break;
        case recheckCommand: RestartProbe(1); break;
        case exitCommand: DestroyWindow(window_); break;
        }
    }

    void RestartProbe(UINT delay) {
        const auto previous = probe_.State();
        probe_.Restart(delay);
        ProbeUpdated(previous);
    }

    LRESULT OnMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        if (taskbarCreated_ && message == taskbarCreated_) {
            AddTray();
            return 0;
        }
        switch (message) {
        case WM_PAINT:
            clockView_.Draw(window_, settings_, times_, probe_.State());
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_LBUTTONDOWN: {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ClientToScreen(window_, &point);
            return DefWindowProcW(window_, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(point.x, point.y));
        }
        case WM_ENTERSIZEMOVE:
            dragging_ = true;
            return 0;
        case WM_EXITSIZEMOVE: {
            dragging_ = false;
            PositionClock();
            RECT rect{};
            GetWindowRect(window_, &rect);
            SavePosition({rect.left, rect.top});
            return 0;
        }
        case WM_CONTEXTMENU: {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (point.x == -1 && point.y == -1) GetCursorPos(&point);
            Menu(point);
            return 0;
        }
        case WM_DISPLAYCHANGE:
        case WM_SETTINGCHANGE:
            PositionClock();
            if (popup_ && IsWindowVisible(popup_)) HidePopup();
            RefreshTime();
            return 0;
        case WM_DPICHANGED:
            PositionClock(false, reinterpret_cast<const RECT*>(lParam));
            UpdateClockViews();
            return 0;
        case WM_TIMECHANGE:
            RefreshTime();
            return 0;
        case WM_POWERBROADCAST:
            if (wParam == PBT_APMRESUMEAUTOMATIC) {
                RefreshTime();
                RestartProbe(750);
            }
            return TRUE;
        case WM_TIMER:
            if (wParam == minuteTimer) {
                RefreshTime();
            } else if (wParam == probeTimer) {
                const auto previous = probe_.State();
                probe_.OnTimer();
                ProbeUpdated(previous);
            } else if (wParam == trayTimer) {
                AddTray();
            }
            return 0;
        case probeCompleteMessage: {
            const auto previous = probe_.State();
            probe_.OnComplete();
            ProbeUpdated(previous);
            return 0;
        }
        case networkChangedMessage:
            if (probe_.State() != ProbeState::disabled) RestartProbe(750);
            return 0;
        case trayMessage: {
            const UINT event = LOWORD(lParam);
            if (event == NIN_SELECT || event == NIN_KEYSELECT || (!trayVersion4_ && event == WM_LBUTTONUP)) {
                TogglePopup();
            } else if (event == WM_CONTEXTMENU || (!trayVersion4_ && event == WM_RBUTTONUP)) {
                const RECT rect = TrayAnchor();
                Menu({rect.left, rect.top});
            }
            return 0;
        }
        case revealMessage:
            SetFloatingVisible(true);
            return 0;
        case WM_CLOSE:
            if (!settingsDialog_) DestroyWindow(window_);
            return 0;
        case WM_DESTROY:
            KillTimer(window_, minuteTimer);
            KillTimer(window_, probeTimer);
            KillTimer(window_, trayTimer);
            if (trayAdded_) {
                auto data = TrayData();
                Shell_NotifyIconW(NIM_DELETE, &data);
            }
            if (const HWND popup = std::exchange(popup_, nullptr)) DestroyWindow(popup);
            probe_.Shutdown();
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(window_, message, wParam, lParam);
    }

    HINSTANCE instance_{};
    HWND window_{};
    HWND popup_{};
    HWND settingsDialog_{};
    UINT taskbarCreated_{};
    bool trayAdded_{};
    bool trayVersion4_{};
    bool dragging_{};
    bool positioningPopup_{};
    bool menuOpen_{};
    RECT trayAnchor_{};
    ClockView clockView_;
    ClockView popupView_;
    Settings settings_;
    Probe probe_;
    std::vector<TimeZone> zones_;
    std::vector<ClockTime> times_;
};

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    const HANDLE singleInstance = CreateMutexW(nullptr, FALSE, L"Local\\JumpingClock.SingleInstance");
    if (!singleInstance) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (const HWND existing = FindWindowW(clockClass, nullptr)) PostMessageW(existing, revealMessage, 0, 0);
        CloseHandle(singleInstance);
        return 0;
    }

    App app(instance);
    if (!app.Start()) {
        MessageBoxW(nullptr, L"无法启动时钟窗口。", L"JumpingClock", MB_OK | MB_ICONERROR);
        CloseHandle(singleInstance);
        return 1;
    }
    MSG message{};
    BOOL status;
    while ((status = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    CloseHandle(singleInstance);
    return status == -1 ? 1 : static_cast<int>(message.wParam);
}
