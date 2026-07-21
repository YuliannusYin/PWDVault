// coding: utf-8
// =============================================================================
// FernetDecoder.h
//
// Fernet token 解码器，用于读取旧版 Python 密码管理器（cryptography.fernet）
// 加密的 password 字段。
//
// Fernet 规范参考：https://cryptography.io/en/latest/fernet/
//
// Fernet 密钥（32 字节，URL-safe base64 编码）：
//   [ signing_key (16B, HMAC-SHA256 key) || encryption_key (16B, AES-128 key) ]
//
// Fernet token（URL-safe base64 编码）：
//   Version (1B, 0x80) || Timestamp (8B big-endian) || IV (16B)
//   || Ciphertext (16B 的倍数, AES-128-CBC + PKCS7 padding) || HMAC-SHA256 (32B)
//
// HMAC 计算输入为：Version || Timestamp || IV || Ciphertext
// （即 token 中除最后 32 字节 HMAC 之外的所有字节）。
//
// 本类只实现解密；Fernet 加密不在迁移工具的需求范围内。
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/Result.h"

namespace pwdvault::migrate {

/// Fernet token 解码器。
///
/// 构造时解析 base64 编码的 32 字节密钥；之后可多次调用 decrypt() 解码 token。
class FernetDecoder {
public:
    /// 构造解码器。
    /// \param base64_key URL-safe base64 编码的 Fernet 密钥（32 字节解码后）
    /// \note 构造失败不抛异常；后续 decrypt() 会返回 CryptoError。
    explicit FernetDecoder(const std::string& base64_key);

    /// 析构。内部密钥在析构时清零（使用 sodium_memzero）。
    ~FernetDecoder();

    FernetDecoder(const FernetDecoder&) = delete;
    FernetDecoder& operator=(const FernetDecoder&) = delete;

    /// 密钥是否已成功解析（即构造函数是否成功）。
    bool valid() const noexcept { return valid_; }

    /// 解密 Fernet token。
    /// \param base64_token URL-safe base64 编码的 Fernet token
    /// \return 成功时返回明文字符串（UTF-8）；失败返回 CryptoError
    core::Result<std::string> decrypt(const std::string& base64_token);

private:
    /// 重新计算 HMAC 并与 token 中的 HMAC 常量时间比较。
    /// \param token 已 base64 解码的 token 字节
    /// \return true 表示 HMAC 校验通过
    bool verify_hmac(const std::vector<uint8_t>& token) const;

private:
    bool valid_ = false;
    std::vector<uint8_t> signing_key_;      ///< HMAC-SHA256 密钥（16B）
    std::vector<uint8_t> encryption_key_;   ///< AES-128-CBC 密钥（16B）
};

}  // namespace pwdvault::migrate
