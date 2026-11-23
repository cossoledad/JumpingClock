#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <icmpapi.h>

#include <array>
#include <optional>
#include <string>

constexpr UINT probeCompleteMessage = WM_APP + 1;
constexpr UINT networkChangedMessage = WM_APP + 2;
constexpr UINT_PTR probeTimer = 2;

enum class ProbeState { disabled, waiting, reachable, noReply, error };

bool ParseTarget(PCWSTR text, ULONG& address);
PCWSTR ProbeStateText(ProbeState state);

class Probe {
public:
    void Initialize(HWND window);
    void SetTarget(const std::wstring& target);
    void Restart(UINT delay);
    void OnTimer();
    void OnComplete();
    void Shutdown();

    ProbeState State() const { return state_; }
    std::wstring Details() const;

private:
    void Schedule(UINT delay);
    void Send();
    void ReadReply(DWORD count, DWORD error);
    void Record(ProbeState result, DWORD error);
    bool Open();

    HWND window_{};
    HANDLE interfaces_{};
    HANDLE icmp_ = INVALID_HANDLE_VALUE;
    HANDLE event_{};
    PTP_WAIT wait_{};
    ULONG address_{};
    bool enabled_{};
    bool pending_{};
    bool discard_{};
    ULONGLONG due_{};
    unsigned failures_{};
    ProbeState state_ = ProbeState::disabled;
    ProbeState lastResult_ = ProbeState::disabled;
    DWORD lastError_{};
    std::optional<SYSTEMTIME> lastTime_;
    std::array<char, 8> payload_{'J', 'C'};
    std::array<ULONG_PTR, 16> reply_{};
};
