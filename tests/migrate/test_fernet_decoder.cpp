// coding: utf-8
// =============================================================================
// test_fernet_decoder.cpp
//
// FernetDecoder 单元测试。覆盖：
//   - 有效 token 解密往返（与测试内的 FernetEncoder 互验）
//   - 错误密钥 → HMAC 校验失败
//   - 损坏 token（version / timestamp / iv / ciphertext / hmac 任一字节翻转）→ 失败
//   - 长度不足 / 非法 base64 / 错误密钥长度 → 失败
//   - 解码器不可用（无效密钥）时 decrypt 返回错误
//
// 测试策略：在测试内实现一个 FernetEncoder（直接调用 OpenSSL HMAC + AES-128-CBC），
// 用于生成 token 喂给 FernetDecoder.decrypt()。该 encoder 与 decoder 是独立实现，
// 仅共享 OpenSSL 与算法规范，因此可互相验证实现的正确性。
// =============================================================================
#include "FernetDecoder.h"

#include <gtest/gtest.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <sodium.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

// ============================================================================
// 测试用 Fernet 编码器（直接使用 OpenSSL，与 FernetDecoder 独立）
// ============================================================================

struct EvpCtxDeleter {
    void operator()(EVP_CIPHER_CTX* ctx) const noexcept {
        if (ctx) EVP_CIPHER_CTX_free(ctx);
    }
};
using EvpCtxPtr = std::unique_ptr<EVP_CIPHER_CTX, EvpCtxDeleter>;

constexpr uint8_t kFernetVersion = 0x80;
constexpr size_t kSigningKeyLen = 16;
constexpr size_t kEncryptionKeyLen = 16;
constexpr size_t kIvLen = 16;
constexpr size_t kHmacLen = 32;

/// URL-safe base64 编码（用于测试断言对照，结果含 padding）。
std::string base64_encode_urlsafe(const uint8_t* data, size_t len) {
    static const char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    size_t i = 0;
    while (i + 3 <= len) {
        uint32_t v = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8) |
                     uint32_t(data[i + 2]);
        out.push_back(kAlphabet[(v >> 18) & 0x3F]);
        out.push_back(kAlphabet[(v >> 12) & 0x3F]);
        out.push_back(kAlphabet[(v >> 6) & 0x3F]);
        out.push_back(kAlphabet[v & 0x3F]);
        i += 3;
    }
    if (i < len) {
        uint32_t v = uint32_t(data[i]) << 16;
        if (i + 1 < len) v |= uint32_t(data[i + 1]) << 8;
        out.push_back(kAlphabet[(v >> 18) & 0x3F]);
        out.push_back(kAlphabet[(v >> 12) & 0x3F]);
        if (i + 1 < len) {
            out.push_back(kAlphabet[(v >> 6) & 0x3F]);
            out.push_back('=');
        } else {
            out.push_back('=');
            out.push_back('=');
        }
    }
    return out;
}

/// 构造 Fernet token。
/// \param signing_key 16 字节 HMAC-SHA256 密钥
/// \param encryption_key 16 字节 AES-128-CBC 密钥
/// \param iv 16 字节 IV
/// \param plaintext 任意长度的明文
/// \param timestamp 8 字节大端时间戳
/// \return URL-safe base64 编码的 token
std::string fernet_encode(const std::vector<uint8_t>& signing_key,
                          const std::vector<uint8_t>& encryption_key,
                          const std::vector<uint8_t>& iv,
                          const std::string& plaintext,
                          uint64_t timestamp = 0) {
    // 1. PKCS7 padding
    const size_t pad_len = 16 - (plaintext.size() % 16);  // 1..16
    std::vector<uint8_t> padded;
    padded.reserve(plaintext.size() + pad_len);
    padded.insert(padded.end(),
                  reinterpret_cast<const uint8_t*>(plaintext.data()),
                  reinterpret_cast<const uint8_t*>(plaintext.data()) + plaintext.size());
    padded.insert(padded.end(), pad_len, static_cast<uint8_t>(pad_len));

    // 2. AES-128-CBC encrypt（禁用 OpenSSL 自动 padding，因为已手动 PKCS7 padding）
    std::vector<uint8_t> ciphertext(padded.size(), 0);
    {
        EvpCtxPtr ctx(EVP_CIPHER_CTX_new());
        EXPECT_NE(ctx, nullptr);
        EVP_EncryptInit_ex(ctx.get(), EVP_aes_128_cbc(), nullptr,
                           encryption_key.data(), iv.data());
        EVP_CIPHER_CTX_set_padding(ctx.get(), 0);  // 禁用自动 padding
        int out_len = 0;
        EVP_EncryptUpdate(ctx.get(), ciphertext.data(), &out_len,
                          padded.data(), static_cast<int>(padded.size()));
        int final_len = 0;
        EVP_EncryptFinal_ex(ctx.get(), ciphertext.data() + out_len, &final_len);
        ciphertext.resize(static_cast<size_t>(out_len) + static_cast<size_t>(final_len));
    }

    // 3. 拼装 token：version(1) || timestamp(8) || iv(16) || ciphertext || hmac(32)
    std::vector<uint8_t> token;
    token.reserve(1 + 8 + 16 + ciphertext.size() + 32);
    token.push_back(kFernetVersion);
    for (int i = 7; i >= 0; --i) {
        token.push_back(static_cast<uint8_t>((timestamp >> (i * 8)) & 0xFF));
    }
    token.insert(token.end(), iv.begin(), iv.end());
    token.insert(token.end(), ciphertext.begin(), ciphertext.end());

    // 4. HMAC-SHA256(signing_key, version || timestamp || iv || ciphertext)
    std::array<uint8_t, kHmacLen> hmac_out{};
    unsigned int hmac_len = 0;
    HMAC(EVP_sha256(),
         signing_key.data(), static_cast<int>(signing_key.size()),
         token.data(), token.size(),
         hmac_out.data(), &hmac_len);
    token.insert(token.end(), hmac_out.begin(), hmac_out.end());

    return base64_encode_urlsafe(token.data(), token.size());
}

/// 构造 32 字节密钥（前 16 signing + 后 16 encryption）的 base64 字符串。
std::string make_key_b64(const std::vector<uint8_t>& signing_key,
                         const std::vector<uint8_t>& encryption_key) {
    std::vector<uint8_t> raw;
    raw.insert(raw.end(), signing_key.begin(), signing_key.end());
    raw.insert(raw.end(), encryption_key.begin(), encryption_key.end());
    return base64_encode_urlsafe(raw.data(), raw.size());
}

std::vector<uint8_t> make_filled(uint8_t v, size_t n) {
    return std::vector<uint8_t>(n, v);
}

}  // namespace

// ============================================================================
// 测试夹具
// ============================================================================

class FernetDecoderTest : public ::testing::Test {
protected:
    void SetUp() override {
        sodium_init();
        // 使用一组固定的密钥与 IV，便于复现
        signing_key_ = make_filled(0x01, kSigningKeyLen);
        encryption_key_ = make_filled(0x02, kEncryptionKeyLen);
        iv_ = make_filled(0x03, kIvLen);
        key_b64_ = make_key_b64(signing_key_, encryption_key_);
    }

    std::vector<uint8_t> signing_key_;
    std::vector<uint8_t> encryption_key_;
    std::vector<uint8_t> iv_;
    std::string key_b64_;
};

// ============================================================================
// 构造与状态
// ============================================================================

TEST_F(FernetDecoderTest, ConstructorWithValidKeyIsValid) {
    pwdvault::migrate::FernetDecoder decoder(key_b64_);
    EXPECT_TRUE(decoder.valid());
}

TEST_F(FernetDecoderTest, ConstructorWithWrongKeyLengthIsInvalid) {
    // 16 字节（太短）
    std::string short_key = base64_encode_urlsafe(make_filled(0xAA, 16).data(), 16);
    pwdvault::migrate::FernetDecoder decoder(short_key);
    EXPECT_FALSE(decoder.valid());
}

TEST_F(FernetDecoderTest, ConstructorWithInvalidBase64IsInvalid) {
    pwdvault::migrate::FernetDecoder decoder("!!! not base64 !!!");
    EXPECT_FALSE(decoder.valid());
}

// ============================================================================
// 解密往返
// ============================================================================

TEST_F(FernetDecoderTest, DecryptRoundtripEmptyPlaintext) {
    const std::string plaintext;
    std::string token = fernet_encode(signing_key_, encryption_key_, iv_, plaintext);

    pwdvault::migrate::FernetDecoder decoder(key_b64_);
    auto result = decoder.decrypt(token);
    ASSERT_TRUE(result.ok()) << result.error().what();
    EXPECT_EQ(result.value(), plaintext);
}

TEST_F(FernetDecoderTest, DecryptRoundtripShortPlaintext) {
    const std::string plaintext = "hello";
    std::string token = fernet_encode(signing_key_, encryption_key_, iv_, plaintext);

    pwdvault::migrate::FernetDecoder decoder(key_b64_);
    auto result = decoder.decrypt(token);
    ASSERT_TRUE(result.ok()) << result.error().what();
    EXPECT_EQ(result.value(), plaintext);
}

TEST_F(FernetDecoderTest, DecryptRoundtripExactBlockMultiple) {
    const std::string plaintext(16, 'A');  // 16 字节 → 一个完整 block + 一整 block padding
    std::string token = fernet_encode(signing_key_, encryption_key_, iv_, plaintext);

    pwdvault::migrate::FernetDecoder decoder(key_b64_);
    auto result = decoder.decrypt(token);
    ASSERT_TRUE(result.ok()) << result.error().what();
    EXPECT_EQ(result.value(), plaintext);
}

TEST_F(FernetDecoderTest, DecryptRoundtripLongPlaintext) {
    const std::string plaintext(1000, 'X');  // 多 block
    std::string token = fernet_encode(signing_key_, encryption_key_, iv_, plaintext);

    pwdvault::migrate::FernetDecoder decoder(key_b64_);
    auto result = decoder.decrypt(token);
    ASSERT_TRUE(result.ok()) << result.error().what();
    EXPECT_EQ(result.value(), plaintext);
}

TEST_F(FernetDecoderTest, DecryptRoundtripUtf8Plaintext) {
    const std::string plaintext = "中文密码 üñîçødé";  // NOLINT: 测试用 UTF-8 字面量
    std::string token = fernet_encode(signing_key_, encryption_key_, iv_, plaintext);

    pwdvault::migrate::FernetDecoder decoder(key_b64_);
    auto result = decoder.decrypt(token);
    ASSERT_TRUE(result.ok()) << result.error().what();
    EXPECT_EQ(result.value(), plaintext);
}

// ============================================================================
// HMAC 校验失败 / 错误密钥
// ============================================================================

TEST_F(FernetDecoderTest, DecryptWithWrongKeyFails) {
    const std::string plaintext = "secret";
    std::string token = fernet_encode(signing_key_, encryption_key_, iv_, plaintext);

    // 构造一个不同的密钥
    auto other_signing = make_filled(0xFF, kSigningKeyLen);
    auto other_encryption = make_filled(0xEE, kEncryptionKeyLen);
    std::string other_key_b64 = make_key_b64(other_signing, other_encryption);

    pwdvault::migrate::FernetDecoder decoder(other_key_b64);
    ASSERT_TRUE(decoder.valid());
    auto result = decoder.decrypt(token);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, pwdvault::core::ErrorCode::CryptoError);
}

TEST_F(FernetDecoderTest, DecryptOnInvalidDecoderFails) {
    pwdvault::migrate::FernetDecoder decoder("invalid");  // 长度不对
    ASSERT_FALSE(decoder.valid());
    auto result = decoder.decrypt("gAAAAABd9r8NAAAAAQAAAAYAAAAAAAAAEW4AcZ5Pb1r2wvnQ1cG1I46pS0jmGTqzYK5Knv");
    ASSERT_FALSE(result.ok());
}

// ============================================================================
// Token 结构破坏
// ============================================================================

TEST_F(FernetDecoderTest, TamperedVersionFails) {
    const std::string plaintext = "some data";
    std::string token = fernet_encode(signing_key_, encryption_key_, iv_, plaintext);

    // 翻转 version 字节后的某个字节使 HMAC 不匹配或 version 不正确
    // 此处直接修改 base64 字符串的首字符（影响 version 字节）。
    // 注意：Fernet token 首字节固定 0x80，对应 base64 总是以 'g' 开头（解码后
    // 第一字节为 0x80）。我们替换首字符来破坏 version。
    ASSERT_FALSE(token.empty());
    std::string tampered = token;
    tampered[0] = (tampered[0] == 'g') ? 'h' : 'g';

    pwdvault::migrate::FernetDecoder decoder(key_b64_);
    auto result = decoder.decrypt(tampered);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, pwdvault::core::ErrorCode::CryptoError);
}

TEST_F(FernetDecoderTest, TamperedCiphertextFails) {
    const std::string plaintext = "tamper me";
    std::string token = fernet_encode(signing_key_, encryption_key_, iv_, plaintext);

    // token 为 73 字节 → 100 base64 字符。base64 第 40 字符对应 token 第 30 字节，
    // 该字节位于 ciphertext 区段（token 第 25-40 字节为 ciphertext）。
    ASSERT_GT(token.size(), 40u);
    std::string tampered = token;
    const size_t idx = 40;  // 位于 ciphertext 区域
    tampered[idx] = (tampered[idx] == 'A') ? 'B' : 'A';

    pwdvault::migrate::FernetDecoder decoder(key_b64_);
    auto result = decoder.decrypt(tampered);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, pwdvault::core::ErrorCode::CryptoError);
}

TEST_F(FernetDecoderTest, TamperedHmacFails) {
    const std::string plaintext = "hmac test";
    std::string token = fernet_encode(signing_key_, encryption_key_, iv_, plaintext);

    // token 为 73 字节 → 100 base64 字符（含末尾两个 '=' padding）。
    // base64 第 70 字符对应 token 字节 52，位于 HMAC 区段（token 第 41-72 字节为 HMAC）。
    // 注意：若改末尾的 '=' padding 字符，会改变 token 解码长度，导致触发其他校验失败
    // 而非 HMAC 校验失败，故这里选择 HMAC 中段字符篡改。
    ASSERT_GT(token.size(), 70u);
    std::string tampered = token;
    const size_t idx = 70;  // 位于 HMAC 区域
    tampered[idx] = (tampered[idx] == 'A') ? 'B' : 'A';

    pwdvault::migrate::FernetDecoder decoder(key_b64_);
    auto result = decoder.decrypt(tampered);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, pwdvault::core::ErrorCode::CryptoError);
}

// ============================================================================
// Token 长度与格式校验
// ============================================================================

TEST_F(FernetDecoderTest, ShortTokenFails) {
    pwdvault::migrate::FernetDecoder decoder(key_b64_);
    // 仅 1 字节（version），远小于最小长度
    auto result = decoder.decrypt("gA");
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, pwdvault::core::ErrorCode::CryptoError);
}

TEST_F(FernetDecoderTest, InvalidBase64Fails) {
    pwdvault::migrate::FernetDecoder decoder(key_b64_);
    auto result = decoder.decrypt("!!! not valid base64 !!!");
    ASSERT_FALSE(result.ok());
}

TEST_F(FernetDecoderTest, TokenWithCiphertextNotAlignedFails) {
    // 构造一个 base64 token，其 ciphertext 长度不是 16 的倍数（但 token 总长度
    // 仍 ≥ 最小长度 73 字节）。直接手工拼装：
    //   version(1) + timestamp(8) + iv(16) + ciphertext(17) + hmac(32) = 74 字节
    std::vector<uint8_t> bad;
    bad.push_back(kFernetVersion);
    bad.insert(bad.end(), 8, 0x00);    // timestamp
    bad.insert(bad.end(), 16, 0x00);   // iv
    bad.insert(bad.end(), 17, 0xAB);   // ciphertext (非 16 倍数)
    // HMAC 用正确的密钥计算（即便 HMAC 通过，长度校验仍应失败）
    std::array<uint8_t, kHmacLen> hmac_out{};
    unsigned int hmac_len = 0;
    HMAC(EVP_sha256(),
         signing_key_.data(), static_cast<int>(signing_key_.size()),
         bad.data(), bad.size(), hmac_out.data(), &hmac_len);
    bad.insert(bad.end(), hmac_out.begin(), hmac_out.end());

    std::string token = base64_encode_urlsafe(bad.data(), bad.size());
    pwdvault::migrate::FernetDecoder decoder(key_b64_);
    auto result = decoder.decrypt(token);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, pwdvault::core::ErrorCode::CryptoError);
}

TEST_F(FernetDecoderTest, TokenWithBadVersionFails) {
    // 构造一个 token：version=0x00（应为 0x80）
    std::vector<uint8_t> raw;
    raw.push_back(0x00);  // 错误的 version
    raw.insert(raw.end(), 8, 0x00);   // timestamp
    raw.insert(raw.end(), 16, 0x00);  // iv
    raw.insert(raw.end(), 16, 0xAB);  // 1 block ciphertext
    std::array<uint8_t, kHmacLen> hmac_out{};
    unsigned int hmac_len = 0;
    HMAC(EVP_sha256(),
         signing_key_.data(), static_cast<int>(signing_key_.size()),
         raw.data(), raw.size(), hmac_out.data(), &hmac_len);
    raw.insert(raw.end(), hmac_out.begin(), hmac_out.end());

    std::string token = base64_encode_urlsafe(raw.data(), raw.size());
    pwdvault::migrate::FernetDecoder decoder(key_b64_);
    auto result = decoder.decrypt(token);
    ASSERT_FALSE(result.ok());
}

TEST_F(FernetDecoderTest, MultipleDecryptCallsAreIndependent) {
    const std::string p1 = "first";
    const std::string p2 = "second plaintext";

    std::string t1 = fernet_encode(signing_key_, encryption_key_, iv_, p1);
    std::string t2 = fernet_encode(signing_key_, encryption_key_, iv_, p2);

    pwdvault::migrate::FernetDecoder decoder(key_b64_);
    auto r1 = decoder.decrypt(t1);
    auto r2 = decoder.decrypt(t2);
    ASSERT_TRUE(r1.ok()) << r1.error().what();
    ASSERT_TRUE(r2.ok()) << r2.error().what();
    EXPECT_EQ(r1.value(), p1);
    EXPECT_EQ(r2.value(), p2);
}

// ============================================================================
// 端到端：模拟旧库 password 字段的格式（base64 字符串可能含尾部换行）
// ============================================================================

TEST_F(FernetDecoderTest, TokenWithTrailingNewlineIsAccepted) {
    const std::string plaintext = "trailing newline";
    std::string token = fernet_encode(signing_key_, encryption_key_, iv_, plaintext);
    // 模拟从文件读取时可能携带的换行
    token.push_back('\n');

    pwdvault::migrate::FernetDecoder decoder(key_b64_);
    auto result = decoder.decrypt(token);
    ASSERT_TRUE(result.ok()) << result.error().what();
    EXPECT_EQ(result.value(), plaintext);
}
