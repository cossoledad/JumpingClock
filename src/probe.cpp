#include "probe.h"

#include <netioapi.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <utility>

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
    case ProbeState::disabled: return L"未配置";
    case ProbeState::waiting: return L"等待";
    case ProbeState::reachable: return L"可达";
    case ProbeState::noReply: return L"无应答";
    case ProbeState::error: return L"探测失败";
    }
    return L"";
}

void Probe::Initialize(HWND window) {
    window_ = window;
}

void Probe::SetTargets(const Settings& settings, const Settings* previewBaseline) {
    const auto now = GetTickCount64();
    std::vector<Target> next;
    const auto add = [&](const Settings& source, bool enabled) {
        for (const auto& clock : source.clocks) {
            ULONG address{};
            if (!ParseTarget(clock.target.c_str(), address) ||
                std::ranges::find(next, address, &Target::address) != next.end()) continue;
            const auto previous = std::ranges::find(targets_, address, &Target::address);
            Target target{};
            if (previous != targets_.end()) target = *previous;
            if (enabled && !target.enabled) target.due = now + 300;
            target.address = address;
            target.enabled = enabled;
            next.push_back(target);
        }
    };
    add(settings, true);
    if (previewBaseline) add(*previewBaseline, false);
    targets_ = std::move(next);

    if (pending_) {
        const auto active = std::ranges::find(targets_, *pending_, &Target::address);
        discard_ = discard_ || active == targets_.end() || !active->enabled;
    }
    if (HasTargets() && !interfaces_) {
        NotifyIpInterfaceChange(AF_INET, InterfaceChanged, window_, FALSE, &interfaces_);
    }
    if (!HasTargets() && interfaces_) {
        CancelMibChangeNotify2(interfaces_);
        interfaces_ = nullptr;
    }
    ScheduleNext();
}

void Probe::Restart(UINT delay) {
    discard_ = pending_.has_value();
    const auto due = GetTickCount64() + delay;
    for (auto& target : targets_) {
        if (!target.enabled) continue;
        target.failures = 0;
        target.lastTime.reset();
        target.state = ProbeState::waiting;
        target.due = due;
    }
    ScheduleNext();
}

void Probe::ScheduleNext() {
    KillTimer(window_, probeTimer);
    if (pending_) return;
    std::optional<ULONGLONG> due;
    for (const auto& target : targets_) {
        if (target.enabled && (!due || target.due < *due)) due = target.due;
    }
    if (!due) return;
    const auto now = GetTickCount64();
    const UINT delay = *due > now ? static_cast<UINT>(*due - now) : 1;
    SetCoalescableTimer(window_, probeTimer, delay, nullptr, delay >= 10'000 ? 250 : 0);
}

void Probe::OnTimer() {
    KillTimer(window_, probeTimer);
    if (pending_) return;
    Target* next = nullptr;
    const auto now = GetTickCount64();
    for (auto& target : targets_) {
        if (target.enabled && target.due <= now && (!next || target.due < next->due)) {
            next = &target;
        }
    }
    if (next) Send(*next);
    ScheduleNext();
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

void Probe::Send(Target& target) {
    if (!Open()) {
        Record(target, ProbeState::error, GetLastError());
        return;
    }
    ResetEvent(event_);
    reply_.fill(0);
    const DWORD count = IcmpSendEcho2(
        icmp_, event_, nullptr, nullptr, target.address, payload_.data(), static_cast<WORD>(payload_.size()),
        nullptr, reply_.data(), static_cast<DWORD>(sizeof(reply_)), timeoutMs);
    const DWORD error = count ? ERROR_SUCCESS : GetLastError();
    if (error == ERROR_IO_PENDING) {
        pending_ = target.address;
        discard_ = false;
        SetThreadpoolWait(wait_, event_, nullptr);
    } else {
        ReadReply(target, count, error);
    }
}

void Probe::OnComplete() {
    if (!pending_) return;
    const auto target = std::ranges::find(targets_, *pending_, &Target::address);
    pending_.reset();
    if (!discard_ && target != targets_.end() && target->enabled) {
        const DWORD count = IcmpParseReplies(reply_.data(), static_cast<DWORD>(sizeof(reply_)));
        const DWORD error = count ? ERROR_SUCCESS : GetLastError();
        ReadReply(*target, count, error);
    }
    discard_ = false;
    ScheduleNext();
}

void Probe::ReadReply(Target& target, DWORD count, DWORD error) {
    // Both ICMP reply layouts begin with the address and status as 32-bit fields.
    if (count) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(reply_.data());
        ULONG responder{};
        std::memcpy(&responder, bytes + offsetof(ICMP_ECHO_REPLY, Address), sizeof(responder));
        std::memcpy(&error, bytes + offsetof(ICMP_ECHO_REPLY, Status), sizeof(error));
        if (error == IP_SUCCESS && responder == target.address) {
            Record(target, ProbeState::reachable, ERROR_SUCCESS);
            return;
        }
        if (error == IP_SUCCESS) error = IP_REQ_TIMED_OUT;
    }
    Record(target, IsNetworkFailure(error) ? ProbeState::noReply : ProbeState::error, error);
}

void Probe::Record(Target& target, ProbeState result, DWORD error) {
    target.lastResult = result;
    target.lastError = error;
    GetLocalTime(&target.lastTime.emplace());
    const auto now = GetTickCount64();
    if (result == ProbeState::reachable) {
        target.failures = 0;
        target.state = result;
        target.due = now + 30'000;
    } else if (result == ProbeState::error) {
        target.failures = 0;
        target.state = result;
        target.due = now + 60'000;
    } else {
        constexpr UINT delays[] = {3'000, 10'000, 30'000, 60'000};
        target.due = now + delays[target.failures];
        target.failures = std::min(target.failures + 1, 3U);
        if (target.failures >= 2) target.state = result;
    }
}

const Probe::Target* Probe::Find(PCWSTR text) const {
    ULONG address{};
    if (!ParseTarget(text, address)) return nullptr;
    const auto target = std::ranges::find(targets_, address, &Target::address);
    return target != targets_.end() && target->enabled ? &*target : nullptr;
}

bool Probe::HasTargets() const {
    return std::ranges::any_of(targets_, &Target::enabled);
}

ProbeState Probe::State(PCWSTR text) const {
    const auto* target = Find(text);
    return target ? target->state : ProbeState::disabled;
}

ProbeState Probe::OverallState() const {
    // Prefer the most actionable result, followed by pending checks and success.
    constexpr ProbeState priority[]{
        ProbeState::noReply, ProbeState::error, ProbeState::waiting, ProbeState::reachable,
    };
    for (const auto state : priority) {
        if (std::ranges::any_of(targets_, [state](const Target& target) {
                return target.enabled && target.state == state;
            })) return state;
    }
    return ProbeState::disabled;
}

std::wstring Probe::Details(PCWSTR address) const {
    const auto* target = Find(address);
    if (!target) return ProbeStateText(ProbeState::disabled);
    if (!target->lastTime) return ProbeStateText(target->state);
    wchar_t text[96]{};
    swprintf_s(text, L"%ls · %02u:%02u:%02u", ProbeStateText(target->lastResult),
               static_cast<unsigned>(target->lastTime->wHour),
               static_cast<unsigned>(target->lastTime->wMinute),
               static_cast<unsigned>(target->lastTime->wSecond));
    std::wstring result = text;
    if (target->lastResult == ProbeState::error) {
        result += L" (" + std::to_wstring(target->lastError) + L")";
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
