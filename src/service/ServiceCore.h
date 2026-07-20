// coding: utf-8
// =============================================================================
// ServiceCore.h
//
// PwdVault 服务核心。接收 IPC 请求负载，按 CommandId 分发到各 handler，
// 调度 crypto / storage / generator 三个引擎完成业务逻辑。
//
// 状态机：
//   未初始化 → (Login first_time) → 已解锁 ←→ (Lock / Unlock) → 已锁定
//
// 线程安全：所有内部状态用 mutex 保护，handler 可被多个工作线程并发调用。
// =============================================================================
#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>

#include "core/Result.h"
#include "core/Types.h"
#include "protocol/Messages.h"

namespace pwdvault::core {
class ICryptoEngine;
class IStorageEngine;
class IPasswordGenerator;
}

namespace pwdvault::service {

class MasterKeyStore;

class ServiceCore {
public:
    /// 构造。
    /// \param crypto 加密引擎（构造时 master_key 可为空，仅用于 derive_key）
    /// \param storage 存储引擎
    /// \param generator 密码生成器
    /// \param meta_path master key meta 文件路径
    ServiceCore(std::unique_ptr<core::ICryptoEngine> crypto,
                std::unique_ptr<core::IStorageEngine> storage,
                std::unique_ptr<core::IPasswordGenerator> generator,
                std::filesystem::path meta_path);

    ~ServiceCore();

    ServiceCore(const ServiceCore&) = delete;
    ServiceCore& operator=(const ServiceCore&) = delete;

    /// 处理一条 IPC 请求。
    /// \param payload 请求负载（不含 16 字节 header）
    /// \param req_header 请求头（含 CommandId 与 request_id）
    /// \return 响应负载（已序列化；失败时为 ErrorResponse 序列化字节）
    core::ByteVec handle_request(core::ByteSpan payload,
                                 const protocol::MessageHeader& req_header);

private:
    // 各命令 handler，均返回序列化后的响应负载
    core::ByteVec handle_ping();
    core::ByteVec handle_login(core::ByteSpan payload);
    core::ByteVec handle_unlock(core::ByteSpan payload);
    core::ByteVec handle_lock();
    core::ByteVec handle_add_entry(core::ByteSpan payload);
    core::ByteVec handle_update_entry(core::ByteSpan payload);
    core::ByteVec handle_remove_entry(core::ByteSpan payload);
    core::ByteVec handle_get_entry(core::ByteSpan payload);
    core::ByteVec handle_search_entries(core::ByteSpan payload);
    core::ByteVec handle_list_entries();
    core::ByteVec handle_generate_password(core::ByteSpan payload);
    core::ByteVec handle_estimate_strength(core::ByteSpan payload);

    /// 构造 ErrorResponse 的序列化字节。
    core::ByteVec make_error(core::ErrorCode code, std::string message) const;

    /// 用 entry_crypto_ 加密 entry.password，填充 iv/tag 字段。
    /// \return 成功时返回填充后的 entry（password 为密文）；失败返回 Error
    core::Result<core::PasswordEntry> encrypt_entry(core::PasswordEntry entry) const;

    /// 用 entry_crypto_ 解密 entry.password，清空 iv/tag。
    core::Result<core::PasswordEntry> decrypt_entry(core::PasswordEntry entry) const;

    /// 设置 master_key 并构造 entry_crypto_。调用方需持锁。
    void set_master_key(core::ByteVec key);

    /// 清除 master_key 与 entry_crypto_。调用方需持锁。
    void clear_master_key();

    /// 检查是否处于锁定冷却期。调用方需持锁。
    bool is_in_cooldown() const;

private:
    std::unique_ptr<core::ICryptoEngine> crypto_;       ///< 注入，用于 derive_key
    std::unique_ptr<core::IStorageEngine> storage_;
    std::unique_ptr<core::IPasswordGenerator> generator_;
    std::unique_ptr<MasterKeyStore> key_store_;

    // 以下成员受 mutex_ 保护
    std::mutex mutex_;
    core::ByteVec master_key_;                            ///< entry 加密用主密钥
    std::unique_ptr<core::ICryptoEngine> entry_crypto_;   ///< 以 master_key_ 构造，用于 entry 加解密
    bool unlocked_ = false;
    int login_attempts_ = 0;
    std::chrono::steady_clock::time_point lock_until_{};
};

}  // namespace pwdvault::service
