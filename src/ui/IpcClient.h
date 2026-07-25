// coding: utf-8
// =============================================================================
// IpcClient.h
//
// PwdVault UI 进程的命名管道 IPC 客户端。封装与 service 进程之间的同步
// 请求/响应通信。UI 各视图通过此类调用 service 提供的命令。
//
// 设计要点：
//   - 同步阻塞调用：每个 IPC 命令在调用线程上同步执行，简化实现。
//     长时间运行的命令会阻塞 UI；后续 Task 11 可改为异步。
//   - 重试：connect_to_service 在失败时重试 3 次，间隔 500ms。
//   - 超时：每个请求最多等待 10 秒，超时返回 IpcError。
//   - 错误传播：service 端返回的 ErrorResponse 会被转换为 core::Error。
//   - 信号：disconnected() 在管道断裂时触发；error_occurred() 在内部
//     错误时触发，便于 UI 提示用户。
// =============================================================================
#pragma once

#include <QObject>
#include <QString>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "Commands.h"
#include "Error.h"
#include "Messages.h"
#include "Result.h"
#include "Serializer.h"
#include "Types.h"

namespace pwdvault::ui {

/// 命名管道 IPC 客户端。
///
/// 简化实现：所有调用同步阻塞，每个请求最多等待 10 秒。
/// 调用方应在 UI 线程上调用；如需后台调用，可包装在 QtConcurrent 中。
class IpcClient : public QObject {
    Q_OBJECT
public:
    /// service 命名管道的默认路径：\\\\.\\pipe\\PwdVaultService
    static constexpr const char* kDefaultPipeName = "\\\\.\\pipe\\PwdVaultService";

    explicit IpcClient(QObject* parent = nullptr);
    ~IpcClient() override;

    // 持有 Windows HANDLE，禁止拷贝/移动
    IpcClient(const IpcClient&) = delete;
    IpcClient& operator=(const IpcClient&) = delete;
    IpcClient(IpcClient&&) = delete;
    IpcClient& operator=(IpcClient&&) = delete;

    // -----------------------------------------------------------------------
    // 连接管理
    // -----------------------------------------------------------------------

    /// 连接到 service 命名管道。失败时重试 3 次，每次间隔 500ms。
    /// \param pipe_name 管道路径，默认为 \\\\.\pipe\PwdVaultService
    /// \return 连接成功返回 true
    bool connect_to_service(std::string_view pipe_name = kDefaultPipeName);

    /// 主动断开连接。释放管道句柄并复位状态。
    void disconnect();

    /// 是否处于已连接状态。
    [[nodiscard]] bool is_connected() const;

    // -----------------------------------------------------------------------
    // 同步 IPC 命令（每个最多等 10 秒）
    // -----------------------------------------------------------------------

    core::Result<protocol::PingResponse> ping();
    core::Result<protocol::UnlockResponse> unlock(const std::string& password);
    core::Result<protocol::LockResponse> lock();
    core::Result<protocol::EnableProgramPasswordResponse> enable_program_password(const std::string& password);
    core::Result<protocol::DisableProgramPasswordResponse> disable_program_password(const std::string& password);
    core::Result<protocol::ChangeProgramPasswordResponse> change_program_password(const std::string& old_password, const std::string& new_password);
    core::Result<protocol::GetVaultStatusResponse> get_vault_status();
    core::Result<protocol::AddEntryResponse> add_entry(const core::PasswordEntry& entry);
    core::Result<protocol::UpdateEntryResponse> update_entry(const core::PasswordEntry& entry);
    core::Result<protocol::RemoveEntryResponse> remove_entry(int64_t id);
    core::Result<protocol::GetEntryResponse> get_entry(int64_t id);
    core::Result<protocol::SearchEntriesResponse> search_entries(const core::SearchQuery& query);
    core::Result<protocol::ListEntriesResponse> list_entries();
    core::Result<protocol::GeneratePasswordResponse> generate_password(const core::PasswordGeneratorOptions& options);
    core::Result<protocol::EstimateStrengthResponse> estimate_strength(const std::string& password);

    // 生成器历史记录
    core::Result<protocol::ListGeneratedRecordsResponse> list_generated_records();
    core::Result<protocol::RemoveGeneratedRecordResponse> remove_generated_record(int64_t id);
    core::Result<protocol::ClearGeneratedRecordsResponse> clear_generated_records();
    core::Result<protocol::GetGeneratorSettingsResponse> get_generator_settings();
    core::Result<protocol::SetGeneratorLimitResponse> set_generator_limit(int32_t limit);

signals:
    /// 与 service 的连接断开时触发（仅在已连接→断开时触发一次）。
    void disconnected();

    /// 内部发生错误时触发，\p message 为人类可读描述（中文）。
    void error_occurred(const QString& message);

private:
    /// 通用同步请求/响应模板。序列化 \p req → 发送 → 读取响应 → 反序列化为 Resp。
    /// 若 service 返回 ErrorResponse，则转换为对应的 core::Error。
    template <typename Req, typename Resp>
    core::Result<Resp> send_request(protocol::CommandId cmd, const Req& req);

    /// 内部断连处理：关闭句柄、复位状态、触发 error_occurred + disconnected 信号。
    void handle_disconnect(const std::string& reason);

    /// 同步写入全部字节（带 10 秒超时）。
    bool write_all(const void* data, size_t size);

    /// 同步读取全部字节（带 10 秒超时）。
    bool read_all(void* out, size_t size);

    /// 管道句柄。使用 void* 避免在头里暴露 Windows HANDLE 类型；
    /// 在 .cpp 中通过 static_cast<HANDLE> 转换。nullptr 表示未连接。
    void* pipe_handle_ = nullptr;

    /// 当前连接状态。
    bool connected_ = false;

    /// 自增的请求 ID（与 service 端匹配请求/响应）。
    uint32_t request_id_ = 0;
};

// ---------------------------------------------------------------------------
// 模板方法实现（必须在头文件中可见以便实例化）
// ---------------------------------------------------------------------------

template <typename Req, typename Resp>
core::Result<Resp> IpcClient::send_request(protocol::CommandId cmd, const Req& req) {
    if (!connected_) {
        return core::Result<Resp>::Err(
            core::Error(core::ErrorCode::IpcError, "未连接到 service"));
    }

    core::ByteVec payload = protocol::serialize(req);
    const uint32_t this_id = request_id_++;
    core::ByteVec frame = protocol::pack_message(cmd, this_id, payload);

    if (!write_all(frame.data(), frame.size())) {
        handle_disconnect("写入 IPC 请求失败");
        return core::Result<Resp>::Err(
            core::Error(core::ErrorCode::IpcError, "写入 IPC 请求失败"));
    }

    std::byte header_buf[sizeof(protocol::MessageHeader)];
    if (!read_all(header_buf, sizeof(header_buf))) {
        handle_disconnect("读取 IPC 响应头失败");
        return core::Result<Resp>::Err(
            core::Error(core::ErrorCode::IpcError, "读取 IPC 响应头失败"));
    }

    auto header_result = protocol::parse_header(
        core::ByteSpan(header_buf, sizeof(header_buf)));
    if (!header_result.ok()) {
        return core::Result<Resp>::Err(header_result.error());
    }

    const auto& [header, offset] = header_result.value();
    (void)offset;  // 恒为 sizeof(MessageHeader)，此处不使用

    if (header.request_id != this_id) {
        return core::Result<Resp>::Err(core::Error(
            core::ErrorCode::IpcError, "响应 request_id 不匹配"));
    }

    core::ByteVec resp_payload(header.payload_size);
    if (header.payload_size > 0) {
        if (!read_all(resp_payload.data(), resp_payload.size())) {
            handle_disconnect("读取 IPC 响应负载失败");
            return core::Result<Resp>::Err(
                core::Error(core::ErrorCode::IpcError, "读取 IPC 响应负载失败"));
        }
    }

    core::ByteSpan resp_span(resp_payload.data(), resp_payload.size());

    // 优先按期望响应类型反序列化
    auto resp_result = protocol::deserialize<Resp>(resp_span);
    if (resp_result.ok()) {
        return core::Result<Resp>::Ok(std::move(resp_result).value());
    }

    // 否则尝试作为 ErrorResponse 解析（service 处理失败的统一错误响应）
    auto err_result = protocol::deserialize<protocol::ErrorResponse>(resp_span);
    if (err_result.ok()) {
        const auto& err_resp = err_result.value();
        return core::Result<Resp>::Err(
            core::Error(err_resp.code, err_resp.message));
    }

    return core::Result<Resp>::Err(core::Error(
        core::ErrorCode::IpcError, "反序列化响应失败"));
}

}  // namespace pwdvault::ui
