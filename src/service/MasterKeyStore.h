// coding: utf-8
// =============================================================================
// MasterKeyStore.h
//
// PwdVault 主密码与 KEK 的持久化管理。
//
// 设计要点：
//   - meta 文件（vault.meta）保存 salt 与被 KEK 加密的 master_key。
//   - KEK 由主密码经 Argon2id 派生，仅存在于内存中，函数返回前清零。
//   - master_key 用于加解密 PasswordEntry 的 password 字段，仅在 unlock 成功后
//     存活于 ServiceCore 内存中，lock 或进程退出时清零。
//   - meta 文件格式为自定义二进制（magic + version + salt + encrypted_blob），
//     便于跨平台与版本演进。
//
// 依赖：
//   - core::ICryptoEngine：用于 derive_key / generate_key_and_iv
//   - crypto::CryptoEngine：内部构造临时实例以 KEK 加解密 master_key
// =============================================================================
#pragma once

#include <filesystem>

#include "core/Error.h"
#include "core/Result.h"
#include "core/Types.h"

namespace pwdvault::core {
class ICryptoEngine;
}

namespace pwdvault::service {

/// 主密码与 master_key 持久化存储。
class MasterKeyStore {
public:
    /// 构造。
    /// \param meta_path meta 文件路径（通常为 %APPDATA%/PwdVault/vault.meta）
    explicit MasterKeyStore(std::filesystem::path meta_path);

    /// 析构。本类不持有敏感数据成员，无需特殊清零。
    ~MasterKeyStore();

    MasterKeyStore(const MasterKeyStore&) = delete;
    MasterKeyStore& operator=(const MasterKeyStore&) = delete;

    /// 检查 meta 文件是否已存在（即 vault 是否已初始化）。
    bool exists() const;

    /// 首次初始化：生成 salt、KEK、master_key，将加密后的 master_key 写入 meta。
    /// \param master_password 用户主密码
    /// \param crypto 加密引擎（仅使用 derive_key 与 generate_key_and_iv，不使用 master_key）
    /// \return 成功时返回明文 master_key（32 字节），仅在内存
    core::Result<core::ByteVec> initialize(const std::string& master_password,
                                           core::ICryptoEngine& crypto);

    /// 解锁：读 meta，用 password + salt 派生 KEK，解密 master_key。
    /// \param master_password 用户主密码
    /// \param crypto 加密引擎（仅使用 derive_key）
    /// \return 成功时返回明文 master_key；密码错误或 GCM tag 校验失败返回 Unauthorized
    core::Result<core::ByteVec> unlock(const std::string& master_password,
                                       core::ICryptoEngine& crypto);

private:
    std::filesystem::path meta_path_;
};

}  // namespace pwdvault::service
