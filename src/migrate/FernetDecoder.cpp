// coding: utf-8
// =============================================================================
// FernetDecoder.cpp
//
// 实现 Fernet token 的解码与解密：
//   1. URL-safe base64 解码密钥与 token
//   2. 校验 token 结构：version、长度、ciphertext 对齐
//   3. 重算 HMAC-SHA256 并常量时间比较
//   4. AES-128-CBC 解密
//   5. PKCS7 去填充
//
// OpenSSL 资源用 RAII（unique_ptr + 自定义 deleter）管理；HMAC 比较使用
// CRYPTO_memcmp 以避免时序侧信道。
// =============================================================================
#include "FernetDecoder.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <sodium.h>

#include <array>
#include <cstring>
#include <memory>
#include <utility>

#include "Base64.h"

namespace pwdvault::migrate {

namespace {

// ============================================================================
// OpenSSL RAII 包装
// ============================================================================
struct EvpCtxDeleter {
    void operator()(EVP_CIPHER_CTX* ctx) const noexcept {
        if (ctx) {
            EVP_CIPHER_CTX_free(ctx);
        }
    }
};
using EvpCtxPtr = std::unique_ptr<EVP_CIPHER_CTX, EvpCtxDeleter>;

// ============================================================================
// Fernet 规范常量
// ============================================================================
constexpr uint8_t kFernetVersion = 0x80;
constexpr size_t kVersionLen = 1;
constexpr size_t kTimestampLen = 8;
constexpr size_t kIvLen = 16;
constexpr size_t kHmacLen = 32;
constexpr size_t kKeyLen = 32;            // 32 字节密钥
constexpr size_t kSigningKeyLen = 16;     // 前 16 字节
constexpr size_t kEncryptionKeyLen = 16;  // 后 16 字节

// Fernet token 的最小长度（无密文时，即明文为空 + 1 字节 padding）：
//   version(1) + timestamp(8) + iv(16) + ciphertext(至少 16) + hmac(32) = 73
constexpr size_t kMinTokenLen = kVersionLen + kTimestampLen + kIvLen + 16 + kHmacLen;

// ============================================================================
// 辅助函数
// ============================================================================

core::Error make_crypto_error(std::string msg) {
    return core::Error{core::ErrorCode::CryptoError, std::move(msg)};
}

/// 安全清零字节向量。
void secure_zero(std::vector<uint8_t>& v) {
    if (!v.empty()) {
        sodium_memzero(v.data(), v.size());
    }
}

}  // namespace

// ============================================================================
// 构造与析构
// ============================================================================

FernetDecoder::FernetDecoder(const std::string& base64_key) {
    sodium_init();

    auto key_result = base64_decode_urlsafe(base64_key);
    if (!key_result.ok() || key_result.value().size() != kKeyLen) {
        valid_ = false;
        return;
    }

    const auto& raw = key_result.value();
    signing_key_.assign(raw.begin(), raw.begin() + kSigningKeyLen);
    encryption_key_.assign(raw.begin() + kSigningKeyLen, raw.end());
    valid_ = true;
}

FernetDecoder::~FernetDecoder() {
    secure_zero(signing_key_);
    secure_zero(encryption_key_);
}

// ============================================================================
// HMAC 校验
// ============================================================================

bool FernetDecoder::verify_hmac(const std::vector<uint8_t>& token) const {
    if (token.size() < kHmacLen) {
        return false;
    }

    // HMAC 计算范围：token 中除最后 32 字节外的所有字节
    const size_t hmac_input_len = token.size() - kHmacLen;
    const unsigned char* hmac_input = token.data();
    const unsigned char* expected_hmac = token.data() + hmac_input_len;

    // OpenSSL 3.0 推荐 EVP_MAC 接口，但 HMAC() 仍是稳定支持的便捷 API
    unsigned char computed_hmac[EVP_MAX_MD_SIZE] = {0};
    unsigned int computed_len = 0;
    unsigned char* p = HMAC(EVP_sha256(),
                            signing_key_.data(),
                            static_cast<int>(signing_key_.size()),
                            hmac_input,
                            static_cast<size_t>(hmac_input_len),
                            computed_hmac,
                            &computed_len);
    if (p == nullptr || computed_len != kHmacLen) {
        return false;
    }

    // 常量时间比较，防止时序侧信道
    return CRYPTO_memcmp(computed_hmac, expected_hmac, kHmacLen) == 0;
}

// ============================================================================
// decrypt
// ============================================================================

core::Result<std::string> FernetDecoder::decrypt(const std::string& base64_token) {
    if (!valid_) {
        return core::Result<std::string>::Err(
            make_crypto_error("FernetDecoder: key not initialized"));
    }

    // 1. base64 解码 token
    auto token_result = base64_decode_urlsafe(base64_token);
    if (!token_result.ok()) {
        return core::Result<std::string>::Err(token_result.error());
    }
    std::vector<uint8_t> token = std::move(token_result).value();

    if (token.size() < kMinTokenLen) {
        return core::Result<std::string>::Err(
            make_crypto_error("FernetDecoder: token too short"));
    }

    // 2. 校验 version
    if (token[0] != kFernetVersion) {
        return core::Result<std::string>::Err(
            make_crypto_error("FernetDecoder: unsupported version byte"));
    }

    // 3. 计算 ciphertext 长度并校验 16 字节对齐
    //    token = version(1) + timestamp(8) + iv(16) + ciphertext + hmac(32)
    const size_t ct_len = token.size() - (kVersionLen + kTimestampLen + kIvLen + kHmacLen);
    if (ct_len == 0 || ct_len % 16 != 0) {
        return core::Result<std::string>::Err(
            make_crypto_error("FernetDecoder: ciphertext length not aligned to 16 bytes"));
    }

    // 4. HMAC 校验
    if (!verify_hmac(token)) {
        return core::Result<std::string>::Err(
            make_crypto_error("FernetDecoder: HMAC verification failed"));
    }

    // 5. AES-128-CBC 解密
    const uint8_t* iv_ptr = token.data() + kVersionLen + kTimestampLen;
    const uint8_t* ct_ptr = iv_ptr + kIvLen;

    EvpCtxPtr ctx(EVP_CIPHER_CTX_new());
    if (!ctx) {
        return core::Result<std::string>::Err(
            make_crypto_error("FernetDecoder: EVP_CIPHER_CTX_new failed"));
    }

    if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_128_cbc(), nullptr,
                           encryption_key_.data(), iv_ptr) != 1) {
        return core::Result<std::string>::Err(
            make_crypto_error("FernetDecoder: EVP_DecryptInit_ex failed"));
    }

    // 禁用 OpenSSL 自动 padding，由我们手动处理 PKCS7 去填充（符合 Fernet 规范）。
    EVP_CIPHER_CTX_set_padding(ctx.get(), 0);

    std::vector<uint8_t> plaintext(ct_len, 0);  // 禁用 padding 时输出长度等于输入长度
    int plain_len = 0;
    if (EVP_DecryptUpdate(ctx.get(), plaintext.data(), &plain_len,
                          ct_ptr, static_cast<int>(ct_len)) != 1) {
        return core::Result<std::string>::Err(
            make_crypto_error("FernetDecoder: EVP_DecryptUpdate failed"));
    }

    int final_len = 0;
    if (EVP_DecryptFinal_ex(ctx.get(), plaintext.data() + plain_len, &final_len) != 1) {
        secure_zero(plaintext);
        return core::Result<std::string>::Err(
            make_crypto_error("FernetDecoder: EVP_DecryptFinal_ex failed"));
    }

    const size_t total_plain = static_cast<size_t>(plain_len) + static_cast<size_t>(final_len);
    plaintext.resize(total_plain);

    // 6. 手动去 PKCS7 padding
    //    Fernet 规范：明文经 PKCS7 填充至 16 字节倍数后加密，padding 长度 1..16。
    if (total_plain == 0 || total_plain % 16 != 0) {
        secure_zero(plaintext);
        return core::Result<std::string>::Err(
            make_crypto_error("FernetDecoder: plaintext length not aligned to 16 bytes"));
    }
    const uint8_t pad = plaintext[total_plain - 1];
    if (pad == 0 || pad > 16 || static_cast<size_t>(pad) > total_plain) {
        secure_zero(plaintext);
        return core::Result<std::string>::Err(
            make_crypto_error("FernetDecoder: invalid PKCS7 padding length"));
    }
    // 校验所有 padding 字节都等于 pad
    for (size_t i = total_plain - pad; i < total_plain; ++i) {
        if (plaintext[i] != pad) {
            secure_zero(plaintext);
            return core::Result<std::string>::Err(
                make_crypto_error("FernetDecoder: invalid PKCS7 padding bytes"));
        }
    }
    const size_t actual_plain_len = total_plain - pad;

    // 7. 拷贝为 std::string 并清零中间缓冲
    std::string result(reinterpret_cast<const char*>(plaintext.data()), actual_plain_len);
    secure_zero(plaintext);

    return core::Result<std::string>::Ok(std::move(result));
}

}  // namespace pwdvault::migrate
