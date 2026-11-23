#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <icmpapi.h>

#include "clock_data.h"

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
    void SetTargets(const Settings& settings, const Settings* previewBaseline = nullptr);
    void Restart(UINT delay);
    void OnTimer();
    void OnComplete();
    void Shutdown();

    bool HasTargets() const;
    ProbeState State(PCWSTR target) const;
    ProbeState OverallState() const;
    std::wstring Details(PCWSTR target) const;

private:
    struct Target {
        ULONG address{};
        bool enabled{};
        ULONGLONG due{};
        unsigned failures{};
        ProbeState state = ProbeState::waiting;
        ProbeState lastResult = ProbeState::waiting;
        DWORD lastError{};
        std::optional<SYSTEMTIME> lastTime;
    };

    const Target* Find(PCWSTR text) const;
    void ScheduleNext();
    void Send(Target& target);
    void ReadReply(Target& target, DWORD count, DWORD error);
    void Record(Target& target, ProbeState result, DWORD error);
    bool Open();

    HWND window_{};
    HANDLE interfaces_{};
    HANDLE icmp_ = INVALID_HANDLE_VALUE;
    HANDLE event_{};
    PTP_WAIT wait_{};
    std::vector<Target> targets_;
    std::optional<ULONG> pending_;
    bool discard_{};
    std::array<char, 8> payload_{'J', 'C'};
    std::array<ULONG_PTR, 16> reply_{};
};
