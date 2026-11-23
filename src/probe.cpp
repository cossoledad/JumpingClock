#include "probe.h"

#include <netioapi.h>

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace {

constexpr UINT timeoutMs = 1000;

void CALLBACK EchoComplete(PTP_CALLBACK_INSTANCE, void* context, PTP_WAIT, TP_WAIT_RESULT) {
    PostMessageW(static_cast<HWND>(context), probeCompleteMessage, 0, 0);
}

void CALLBACK InterfaceChanged(void* context, MIB_IPINTERFACE_ROW*, MIB_NOTIFICATION_TYPE) {
    PostMessageW(static_cast<HWND>(context), networkChangedMessage, 0, 0);
}

bool IsNetworkFailure(DWORD error) {
    return (error >= IP_DEST_NET_UNREACHABLE && error <= IP_DEST_PORT_UNREACHABLE) ||
           error == IP_REQ_TIMED_OUT || error == IP_TTL_EXPIRED_TRANSIT ||
           error == IP_GENERAL_FAILURE || error == ERROR_NETWORK_UNREACHABLE ||
           error == ERROR_HOST_UNREACHABLE;
}

} // namespace

bool ParseTarget(PCWSTR text, ULONG& address) {
    IN_ADDR parsed{};
    if (InetPtonW(AF_INET, text, &parsed) != 1 || parsed.S_un.S_un_b.s_b1 == 0 ||
        parsed.S_un.S_un_b.s_b1 >= 224) {
        return false;
    }
    address = parsed.S_un.S_addr;
    return true;
}

PCWSTR ProbeStateText(ProbeState state) {
    switch (state) {
    case ProbeState::disabled: return L"未配置 IPv4";
    case ProbeState::waiting: return L"等待探测结果";
    case ProbeState::reachable: return L"目标可达";
    case ProbeState::noReply: return L"目标无应答";
    case ProbeState::error: return L"探测失败";
    }
    return L"";
}

void Probe::Initialize(HWND window) {
    window_ = window;
}

void Probe::SetTarget(const std::wstring& target) {
    enabled_ = ParseTarget(target.c_str(), address_);
    if (enabled_ && !interfaces_) {
        NotifyIpInterfaceChange(AF_INET, InterfaceChanged, window_, FALSE, &interfaces_);
    }
    if (!enabled_ && interfaces_) {
        CancelMibChangeNotify2(interfaces_);
        interfaces_ = nullptr;
    }
    Restart(1);
}

void Probe::Restart(UINT delay) {
    KillTimer(window_, probeTimer);
    due_ = 0;
    discard_ = pending_;
    failures_ = 0;
    lastTime_.reset();
    state_ = enabled_ ? ProbeState::waiting : ProbeState::disabled;
    if (enabled_) {
        Schedule(delay);
    }
}

void Probe::Schedule(UINT delay) {
    due_ = GetTickCount64() + delay;
    SetCoalescableTimer(window_, probeTimer, delay, nullptr, delay >= 10'000 ? 250 : 0);
}

void Probe::OnTimer() {
    KillTimer(window_, probeTimer);
    if (!enabled_ || due_ == 0) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (now < due_) {
        Schedule(static_cast<UINT>(due_ - now));
    } else if (!pending_) {
        due_ = 0;
        Send();
    }
}

bool Probe::Open() {
    if (icmp_ == INVALID_HANDLE_VALUE) {
        icmp_ = IcmpCreateFile();
        if (icmp_ == INVALID_HANDLE_VALUE) return false;
    }
    if (!event_) {
        event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!event_) return false;
    }
    if (!wait_) {
        wait_ = CreateThreadpoolWait(EchoComplete, window_, nullptr);
        if (!wait_) return false;
    }
    return true;
}

void Probe::Send() {
    if (!Open()) {
        Record(ProbeState::error, GetLastError());
        return;
    }
    ResetEvent(event_);
    reply_.fill(0);
    const DWORD count = IcmpSendEcho2(
        icmp_, event_, nullptr, nullptr, address_, payload_.data(), static_cast<WORD>(payload_.size()),
        nullptr, reply_.data(), static_cast<DWORD>(sizeof(reply_)), timeoutMs);
    const DWORD error = count ? ERROR_SUCCESS : GetLastError();
    if (error == ERROR_IO_PENDING) {
        pending_ = true;
        SetThreadpoolWait(wait_, event_, nullptr);
    } else {
        ReadReply(count, error);
    }
}

void Probe::OnComplete() {
    if (!pending_) return;
    pending_ = false;
    if (discard_) {
        discard_ = false;
        if (enabled_ && due_ && GetTickCount64() >= due_) {
            OnTimer();
        }
        return;
    }
    const DWORD count = IcmpParseReplies(reply_.data(), static_cast<DWORD>(sizeof(reply_)));
    const DWORD error = count ? ERROR_SUCCESS : GetLastError();
    ReadReply(count, error);
}

void Probe::ReadReply(DWORD count, DWORD error) {
    // Both ICMP reply layouts begin with the address and status as 32-bit fields.
    if (count) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(reply_.data());
        ULONG responder{};
        std::memcpy(&responder, bytes + offsetof(ICMP_ECHO_REPLY, Address), sizeof(responder));
        std::memcpy(&error, bytes + offsetof(ICMP_ECHO_REPLY, Status), sizeof(error));
        if (error == IP_SUCCESS && responder == address_) {
            Record(ProbeState::reachable, ERROR_SUCCESS);
            return;
        }
        if (error == IP_SUCCESS) error = IP_REQ_TIMED_OUT;
    }
    Record(IsNetworkFailure(error) ? ProbeState::noReply : ProbeState::error, error);
}

void Probe::Record(ProbeState result, DWORD error) {
    lastResult_ = result;
    lastError_ = error;
    GetLocalTime(&lastTime_.emplace());
    if (result == ProbeState::reachable) {
        failures_ = 0;
        state_ = result;
        Schedule(30'000);
    } else if (result == ProbeState::error) {
        failures_ = 0;
        state_ = result;
        Schedule(60'000);
    } else {
        constexpr UINT delays[] = {3'000, 10'000, 30'000, 60'000};
        const UINT delay = delays[failures_];
        failures_ = std::min(failures_ + 1, 3U);
        if (failures_ >= 2) state_ = result;
        Schedule(delay);
    }
}

std::wstring Probe::Details() const {
    if (!lastTime_) return ProbeStateText(state_);
    wchar_t text[96]{};
    swprintf_s(text, L"%02u:%02u:%02u  %ls", static_cast<unsigned>(lastTime_->wHour),
               static_cast<unsigned>(lastTime_->wMinute),
               static_cast<unsigned>(lastTime_->wSecond), ProbeStateText(lastResult_));
    std::wstring result = text;
    if (lastResult_ == ProbeState::error) {
        result += L"（错误 " + std::to_wstring(lastError_) + L"）";
    }
    return result;
}

void Probe::Shutdown() {
    if (interfaces_) CancelMibChangeNotify2(interfaces_);
    if (wait_) {
        SetThreadpoolWait(wait_, nullptr, nullptr);
        WaitForThreadpoolWaitCallbacks(wait_, TRUE);
        CloseThreadpoolWait(wait_);
    }
    // Keep the event, request and reply buffers alive until the kernel completes the request.
    if (pending_) WaitForSingleObject(event_, INFINITE);
    if (icmp_ != INVALID_HANDLE_VALUE) IcmpCloseHandle(icmp_);
    if (event_) CloseHandle(event_);
}
