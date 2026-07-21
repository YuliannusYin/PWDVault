// coding: utf-8
// =============================================================================
// IpcClient.cpp
//
// PwdVault UI 进程 IPC 客户端实现。基于 Windows 命名管道：
//   - connect_to_service: 用 CreateFileW 打开 \\\\.\pipe\PwdVaultService
//   - read_all / write_all: 用 overlapped I/O + WaitForSingleObject 实现 10 秒超时
//   - 重试: connect 失败时重试 3 次，间隔 500ms（QThread::msleep）
//   - RAII: ScopedHandle 包装事件句柄；管道句柄在析构/disconnect 中释放
// =============================================================================
#include "IpcClient.h"

#include <QThread>
#include <QString>

// Windows 头：放在 Qt 之后以避免 min/max 宏污染 Qt 模板
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace pwdvault::ui {

namespace {

/// 单次请求超时（毫秒）。超时返回 IpcError。
constexpr DWORD kRequestTimeoutMs = 10000;

/// connect 重试次数（不含首次尝试）。
constexpr int kConnectRetryCount = 3;

/// 每次 connect 重试前等待的毫秒数。
constexpr int kConnectRetryIntervalMs = 500;

/// RAII 包装 Windows HANDLE。释放时调用 CloseHandle。
class ScopedHandle {
public:
    ScopedHandle() = default;
    explicit ScopedHandle(HANDLE h) : handle_(h) {}
    ~ScopedHandle() { reset(); }

    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
    ScopedHandle(ScopedHandle&& other) noexcept : handle_(other.release()) {}
    ScopedHandle& operator=(ScopedHandle&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    [[nodiscard]] HANDLE get() const { return handle_; }
    [[nodiscard]] bool valid() const {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }
    HANDLE release() {
        HANDLE tmp = handle_;
        handle_ = nullptr;
        return tmp;
    }
    void reset(HANDLE h = nullptr) {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle_);
        }
        handle_ = h;
    }

private:
    HANDLE handle_ = nullptr;
};

/// UTF-8 字符串 → UTF-16 wstring，用于 Win32 API 调用。
std::wstring utf8_to_wide(std::string_view sv) {
    if (sv.empty()) return std::wstring();
    const int required = ::MultiByteToWideChar(
        CP_UTF8, 0, sv.data(), static_cast<int>(sv.size()), nullptr, 0);
    if (required <= 0) return std::wstring();
    std::wstring ws(static_cast<size_t>(required), L'\0');
    ::MultiByteToWideChar(
        CP_UTF8, 0, sv.data(), static_cast<int>(sv.size()), ws.data(), required);
    return ws;
}

}  // namespace

// ---------------------------------------------------------------------------
// 构造与析构
// ---------------------------------------------------------------------------

IpcClient::IpcClient(QObject* parent) : QObject(parent) {}

IpcClient::~IpcClient() {
    disconnect();
}

// ---------------------------------------------------------------------------
// 连接管理
// ---------------------------------------------------------------------------

bool IpcClient::connect_to_service(std::string_view pipe_name) {
    if (connected_) {
        return true;
    }

    const std::wstring wname = utf8_to_wide(pipe_name);

    // 总尝试次数 = 1 次初始 + kConnectRetryCount 次重试
    for (int attempt = 0; attempt <= kConnectRetryCount; ++attempt) {
        if (attempt > 0) {
            QThread::msleep(static_cast<unsigned long>(kConnectRetryIntervalMs));
        }

        HANDLE raw = ::CreateFileW(
            wname.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,                          // 独占访问
            nullptr,                    // 默认安全属性
            OPEN_EXISTING,               // 管道必须已由 service 创建
            FILE_FLAG_OVERLAPPED,        // 启用 overlapped 以支持超时
            nullptr);

        if (raw == INVALID_HANDLE_VALUE) {
            // 此轮失败，下一轮重试
            continue;
        }

        pipe_handle_ = raw;
        connected_ = true;
        request_id_ = 0;
        return true;
    }
    return false;
}

void IpcClient::disconnect() {
    if (pipe_handle_ != nullptr && pipe_handle_ != INVALID_HANDLE_VALUE) {
        ::CloseHandle(static_cast<HANDLE>(pipe_handle_));
        pipe_handle_ = nullptr;
    }
    if (connected_) {
        connected_ = false;
        emit disconnected();
    }
}

bool IpcClient::is_connected() const {
    return connected_;
}

// ---------------------------------------------------------------------------
// 内部 I/O 辅助
// ---------------------------------------------------------------------------

void IpcClient::handle_disconnect(const std::string& reason) {
    if (!connected_) {
        return;  // 已断开，避免重复触发信号
    }

    if (pipe_handle_ != nullptr && pipe_handle_ != INVALID_HANDLE_VALUE) {
        ::CancelIo(static_cast<HANDLE>(pipe_handle_));
        ::CloseHandle(static_cast<HANDLE>(pipe_handle_));
        pipe_handle_ = nullptr;
    }
    connected_ = false;

    emit error_occurred(QString::fromStdString(reason));
    emit disconnected();
}

bool IpcClient::write_all(const void* data, size_t size) {
    if (!connected_ || pipe_handle_ == nullptr) return false;
    HANDLE h = static_cast<HANDLE>(pipe_handle_);

    const auto* p = static_cast<const std::byte*>(data);
    size_t remaining = size;

    while (remaining > 0) {
        ScopedHandle event(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event.valid()) return false;

        OVERLAPPED ov{};
        ov.hEvent = event.get();

        const DWORD to_write = static_cast<DWORD>(
            std::min(remaining, static_cast<size_t>(0x7FFFFFFFu)));
        DWORD written = 0;
        BOOL ok = ::WriteFile(h, p, to_write, &written, &ov);

        if (!ok) {
            const DWORD err = ::GetLastError();
            if (err != ERROR_IO_PENDING) {
                return false;  // 管道已断或参数错误
            }
            // 等待 I/O 完成（带超时）
            const DWORD wait_result =
                ::WaitForSingleObject(event.get(), kRequestTimeoutMs);
            if (wait_result != WAIT_OBJECT_0) {
                // 超时或失败：取消未决 I/O，等待取消完成
                ::CancelIo(h);
                DWORD dummy = 0;
                ::GetOverlappedResult(h, &ov, &dummy, TRUE);
                return false;
            }
            if (!::GetOverlappedResult(h, &ov, &written, FALSE)) {
                return false;
            }
        }

        if (written == 0) return false;
        p += written;
        remaining -= written;
    }
    return true;
}

bool IpcClient::read_all(void* out, size_t size) {
    if (!connected_ || pipe_handle_ == nullptr) return false;
    HANDLE h = static_cast<HANDLE>(pipe_handle_);

    auto* p = static_cast<std::byte*>(out);
    size_t remaining = size;

    while (remaining > 0) {
        ScopedHandle event(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event.valid()) return false;

        OVERLAPPED ov{};
        ov.hEvent = event.get();

        const DWORD to_read = static_cast<DWORD>(
            std::min(remaining, static_cast<size_t>(0x7FFFFFFFu)));
        DWORD read = 0;
        BOOL ok = ::ReadFile(h, p, to_read, &read, &ov);

        if (!ok) {
            const DWORD err = ::GetLastError();
            if (err != ERROR_IO_PENDING) {
                return false;  // 管道已断（ERROR_BROKEN_PIPE 等）
            }
            const DWORD wait_result =
                ::WaitForSingleObject(event.get(), kRequestTimeoutMs);
            if (wait_result != WAIT_OBJECT_0) {
                ::CancelIo(h);
                DWORD dummy = 0;
                ::GetOverlappedResult(h, &ov, &dummy, TRUE);
                return false;
            }
            if (!::GetOverlappedResult(h, &ov, &read, FALSE)) {
                return false;
            }
        }

        if (read == 0) return false;
        p += read;
        remaining -= read;
    }
    return true;
}

// ---------------------------------------------------------------------------
// IPC 命令实现（实例化 send_request 模板）
// ---------------------------------------------------------------------------

core::Result<protocol::PingResponse> IpcClient::ping() {
    return send_request<protocol::PingRequest, protocol::PingResponse>(
        protocol::CommandId::Ping, protocol::PingRequest{});
}

core::Result<protocol::UnlockResponse> IpcClient::unlock(const std::string& password) {
    protocol::UnlockRequest req;
    req.password = password;
    return send_request<protocol::UnlockRequest, protocol::UnlockResponse>(
        protocol::CommandId::Unlock, req);
}

core::Result<protocol::LockResponse> IpcClient::lock() {
    return send_request<protocol::LockRequest, protocol::LockResponse>(
        protocol::CommandId::Lock, protocol::LockRequest{});
}

core::Result<protocol::EnableProgramPasswordResponse> IpcClient::enable_program_password(const std::string& password) {
    protocol::EnableProgramPasswordRequest req;
    req.password = password;
    return send_request<protocol::EnableProgramPasswordRequest, protocol::EnableProgramPasswordResponse>(
        protocol::CommandId::EnableProgramPassword, req);
}

core::Result<protocol::DisableProgramPasswordResponse> IpcClient::disable_program_password(const std::string& password) {
    protocol::DisableProgramPasswordRequest req;
    req.password = password;
    return send_request<protocol::DisableProgramPasswordRequest, protocol::DisableProgramPasswordResponse>(
        protocol::CommandId::DisableProgramPassword, req);
}

core::Result<protocol::ChangeProgramPasswordResponse> IpcClient::change_program_password(const std::string& old_password, const std::string& new_password) {
    protocol::ChangeProgramPasswordRequest req;
    req.old_password = old_password;
    req.new_password = new_password;
    return send_request<protocol::ChangeProgramPasswordRequest, protocol::ChangeProgramPasswordResponse>(
        protocol::CommandId::ChangeProgramPassword, req);
}

core::Result<protocol::GetVaultStatusResponse> IpcClient::get_vault_status() {
    return send_request<protocol::GetVaultStatusRequest, protocol::GetVaultStatusResponse>(
        protocol::CommandId::GetVaultStatus, protocol::GetVaultStatusRequest{});
}

core::Result<protocol::AddEntryResponse> IpcClient::add_entry(const core::PasswordEntry& entry) {
    protocol::AddEntryRequest req;
    req.entry = entry;
    return send_request<protocol::AddEntryRequest, protocol::AddEntryResponse>(
        protocol::CommandId::AddEntry, req);
}

core::Result<protocol::UpdateEntryResponse> IpcClient::update_entry(const core::PasswordEntry& entry) {
    protocol::UpdateEntryRequest req;
    req.entry = entry;
    return send_request<protocol::UpdateEntryRequest, protocol::UpdateEntryResponse>(
        protocol::CommandId::UpdateEntry, req);
}

core::Result<protocol::RemoveEntryResponse> IpcClient::remove_entry(int64_t id) {
    protocol::RemoveEntryRequest req;
    req.id = id;
    return send_request<protocol::RemoveEntryRequest, protocol::RemoveEntryResponse>(
        protocol::CommandId::RemoveEntry, req);
}

core::Result<protocol::GetEntryResponse> IpcClient::get_entry(int64_t id) {
    protocol::GetEntryRequest req;
    req.id = id;
    return send_request<protocol::GetEntryRequest, protocol::GetEntryResponse>(
        protocol::CommandId::GetEntry, req);
}

core::Result<protocol::SearchEntriesResponse> IpcClient::search_entries(const core::SearchQuery& query) {
    protocol::SearchEntriesRequest req;
    req.query = query;
    return send_request<protocol::SearchEntriesRequest, protocol::SearchEntriesResponse>(
        protocol::CommandId::SearchEntries, req);
}

core::Result<protocol::ListEntriesResponse> IpcClient::list_entries() {
    return send_request<protocol::ListEntriesRequest, protocol::ListEntriesResponse>(
        protocol::CommandId::ListEntries, protocol::ListEntriesRequest{});
}

core::Result<protocol::GeneratePasswordResponse> IpcClient::generate_password(const core::PasswordGeneratorOptions& options) {
    protocol::GeneratePasswordRequest req;
    req.options = options;
    return send_request<protocol::GeneratePasswordRequest, protocol::GeneratePasswordResponse>(
        protocol::CommandId::GeneratePassword, req);
}

core::Result<protocol::EstimateStrengthResponse> IpcClient::estimate_strength(const std::string& password) {
    protocol::EstimateStrengthRequest req;
    req.password = password;
    return send_request<protocol::EstimateStrengthRequest, protocol::EstimateStrengthResponse>(
        protocol::CommandId::EstimateStrength, req);
}

}  // namespace pwdvault::ui
