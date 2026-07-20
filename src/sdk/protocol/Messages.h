// coding: utf-8
// =============================================================================
// Messages.h
//
// PwdVault IPC 协议消息结构。每个 CommandId 对应一对 Request/Response 结构。
// 消息在传输时由 Serializer.h/cpp 序列化为字节流，并由 MessageHeader 描述
// 帧的元信息（magic/version/command/request_id/payload_size）。
//
// 设计要点：
//   - 所有结构为普通聚合（POD-like），由 Serializer 通过模板特化进行二进制
//     序列化，避免引入 JSON/protobuf 等外部依赖。
//   - 字符串字段使用 std::string，二进制字段使用 core::ByteVec，序列化时
//     以长度前缀（uint32_t）+ 原始字节的方式编码（见 Serializer.h）。
//   - MessageHeader 大小固定 16 字节，由 static_assert 强制保证，便于
//     parse_header 在管道字节流中按固定偏移量定位 payload。
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Commands.h"
#include "Error.h"
#include "Types.h"

namespace pwdvault::protocol {

/// 协议魔数 "PDVV"（小端序：0x56 0x44 0x56 0x50）。
/// 用于在字节流头部快速识别 PwdVault IPC 帧，避免误解析其他来源数据。
inline constexpr uint32_t kMagic = 0x50564456u;

/// 协议版本号。当前为 1。
///   - 未来若引入不兼容变更（如字段重排、新枚举语义），递增此值。
///   - 同一版本内向后兼容追加字段，需在 Serializer 中处理缺省值。
inline constexpr uint16_t kProtocolVersion = 1;

/// 消息头（固定 16 字节）。
///
/// 字段布局（小端序）：
///   offset  size  field
///   0       4     magic          必须为 kMagic
///   4       2     version        协议版本，当前为 1
///   6       2     command        CommandId（按 uint16_t 传输）
///   8       4     request_id     用于异步匹配请求与响应
///   12      4     payload_size   负载字节数（不含 header）
struct MessageHeader {
    uint32_t magic = kMagic;
    uint16_t version = kProtocolVersion;
    CommandId command = CommandId::Ping;
    uint32_t request_id = 0;
    uint32_t payload_size = 0;
};
static_assert(sizeof(MessageHeader) == 16,
              "MessageHeader must be exactly 16 bytes for fixed-offset parsing");

// ---------------------------------------------------------------------------
// 请求消息
// ---------------------------------------------------------------------------

/// 心跳请求（无负载）。响应为 PingResponse 携带 service 端时间戳。
struct PingRequest {};

/// 关闭请求（无负载）。service 收到后执行优雅退出。
struct ShutdownRequest {};

/// 登录请求。
/// \param password 主密码（首次设置时为待设置值，否则为待验证值）
/// \param is_first_time true 表示首次设置主密码，false 表示验证已有主密码
struct LoginRequest {
    std::string password;
    bool is_first_time = false;
};

/// 解锁请求。语义类似 LoginRequest，但用于已锁定状态恢复会话。
struct UnlockRequest {
    std::string password;
};

/// 锁定请求（无负载）。清除 service 内存中的 KEK。
struct LockRequest {};

/// 新增条目请求。entry.id 通常为 0，由 service 分配后返回。
struct AddEntryRequest {
    core::PasswordEntry entry;
};

/// 更新已存在条目请求。entry.id 必须非 0。
struct UpdateEntryRequest {
    core::PasswordEntry entry;
};

/// 删除条目请求。
struct RemoveEntryRequest {
    int64_t id = 0;
};

/// 获取单条条目请求。
struct GetEntryRequest {
    int64_t id = 0;
};

/// 搜索条目请求。
struct SearchEntriesRequest {
    core::SearchQuery query;
};

/// 列出全部条目请求（无负载）。
struct ListEntriesRequest {};

/// 生成密码请求。
struct GeneratePasswordRequest {
    core::PasswordGeneratorOptions options;
};

/// 评估密码强度请求。
struct EstimateStrengthRequest {
    std::string password;
};

// ---------------------------------------------------------------------------
// 响应消息
// ---------------------------------------------------------------------------

/// 心跳响应。携带 service 端 Unix 时间戳（秒）。
struct PingResponse {
    uint64_t server_timestamp = 0;
};

/// 关闭响应（无负载）。
struct ShutdownResponse {};

/// 登录响应。
struct LoginResponse {
    bool success = false;
    std::string error_message;
};

/// 解锁响应。
struct UnlockResponse {
    bool success = false;
    std::string error_message;
};

/// 锁定响应（无负载）。
struct LockResponse {};

/// 新增条目响应。entry.id 为 service 分配的主键。
struct AddEntryResponse {
    core::PasswordEntry entry;
};

/// 更新条目响应。entry 为更新后的最新值。
struct UpdateEntryResponse {
    core::PasswordEntry entry;
};

/// 删除条目响应（无负载）。
struct RemoveEntryResponse {};

/// 获取条目响应。
struct GetEntryResponse {
    core::PasswordEntry entry;
};

/// 搜索条目响应。
struct SearchEntriesResponse {
    std::vector<core::PasswordEntry> entries;
};

/// 列出条目响应。
struct ListEntriesResponse {
    std::vector<core::PasswordEntry> entries;
};

/// 生成密码响应。
struct GeneratePasswordResponse {
    std::string password;
};

/// 评估强度响应。strength_bits 为估算的熵（bit 数）。
struct EstimateStrengthResponse {
    int strength_bits = 0;
};

// ---------------------------------------------------------------------------
// 通用错误响应
// ---------------------------------------------------------------------------

/// 通用错误响应。当请求处理失败且无具体响应结构时使用。
struct ErrorResponse {
    core::ErrorCode code = core::ErrorCode::None;
    std::string message;
};

}  // namespace pwdvault::protocol
