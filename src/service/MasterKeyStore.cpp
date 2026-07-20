// coding: utf-8
// =============================================================================
// MasterKeyStore.cpp
//
// meta 文件二进制格式：
//   offset  size      field
//   0       4         magic       = 0x4D4B5650 ('P','V','K','M' little-endian)
//   4       2         version     = 1
//   6       4         salt_len    = 16
//   10      16        salt
//   26      4         blob_len    = 60 (12 IV + 32 ciphertext + 16 tag)
//   30      blob_len  encrypted_master_key_blob  = [IV || ciphertext || tag]
// =============================================================================
#include "MasterKeyStore.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <utility>
#include <vector>

#include "CryptoEngine.h"
#include "ICryptoEngine.h"
#include "Types.h"

#include <sodium.h>

namespace pwdvault::service {

namespace {

constexpr uint32_t kMetaMagic = 0x4D4B5650u;  // 'P','V','K','M' little-endian
constexpr uint16_t kMetaVersion = 1;
constexpr size_t kSaltLen = 16;
constexpr size_t kMasterKeyLen = 32;

/// meta 文件在内存中的解析结果。
struct MetaRecord {
    core::ByteVec salt;
    core::ByteVec encrypted_blob;  // [IV || ciphertext || tag]
};

/// 小端写入 POD 值到字节向量。
template <typename T>
void write_pod(std::ofstream& out, T value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

/// 从输入流读取 POD 值。
template <typename T>
bool read_pod(std::ifstream& in, T& value) {
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(in);
}

/// 安全清零 ByteVec。
void secure_zero(core::ByteVec& v) {
    if (!v.empty()) {
        sodium_memzero(v.data(), v.size());
    }
}

core::Result<MetaRecord> read_meta(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return core::Result<MetaRecord>::Err(
            core::Error(core::ErrorCode::StorageError,
                        "meta file open failed: " + path.string()));
    }

    uint32_t magic = 0;
    uint16_t version = 0;
    uint32_t salt_len = 0;
    if (!read_pod(f, magic) || !read_pod(f, version) || !read_pod(f, salt_len)) {
        return core::Result<MetaRecord>::Err(
            core::Error(core::ErrorCode::StorageError, "meta file truncated (header)"));
    }
    if (magic != kMetaMagic) {
        return core::Result<MetaRecord>::Err(
            core::Error(core::ErrorCode::StorageError, "meta file magic mismatch"));
    }
    if (version != kMetaVersion) {
        return core::Result<MetaRecord>::Err(
            core::Error(core::ErrorCode::StorageError,
                        "meta file version unsupported: " + std::to_string(version)));
    }
    if (salt_len != kSaltLen) {
        return core::Result<MetaRecord>::Err(
            core::Error(core::ErrorCode::StorageError,
                        "meta file salt length unexpected: " + std::to_string(salt_len)));
    }

    MetaRecord rec;
    rec.salt.resize(kSaltLen);
    if (!f.read(reinterpret_cast<char*>(rec.salt.data()), kSaltLen)) {
        return core::Result<MetaRecord>::Err(
            core::Error(core::ErrorCode::StorageError, "meta file truncated (salt)"));
    }

    uint32_t blob_len = 0;
    if (!read_pod(f, blob_len)) {
        return core::Result<MetaRecord>::Err(
            core::Error(core::ErrorCode::StorageError, "meta file truncated (blob_len)"));
    }
    // 粗略上界保护：master_key 加密后最长 12 + N + 16，N 不会超过 64KB
    if (blob_len > 65536) {
        return core::Result<MetaRecord>::Err(
            core::Error(core::ErrorCode::StorageError, "meta file blob too large"));
    }
    rec.encrypted_blob.resize(blob_len);
    if (blob_len > 0 && !f.read(reinterpret_cast<char*>(rec.encrypted_blob.data()),
                                static_cast<std::streamsize>(blob_len))) {
        return core::Result<MetaRecord>::Err(
            core::Error(core::ErrorCode::StorageError, "meta file truncated (blob)"));
    }

    return core::Result<MetaRecord>::Ok(std::move(rec));
}

core::Error write_meta(const std::filesystem::path& path, const MetaRecord& rec) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        return core::Error(core::ErrorCode::StorageError,
                           "meta file create failed: " + path.string());
    }
    write_pod(f, kMetaMagic);
    write_pod(f, kMetaVersion);
    write_pod(f, static_cast<uint32_t>(rec.salt.size()));
    f.write(reinterpret_cast<const char*>(rec.salt.data()),
            static_cast<std::streamsize>(rec.salt.size()));
    write_pod(f, static_cast<uint32_t>(rec.encrypted_blob.size()));
    f.write(reinterpret_cast<const char*>(rec.encrypted_blob.data()),
            static_cast<std::streamsize>(rec.encrypted_blob.size()));
    if (!f) {
        return core::Error(core::ErrorCode::StorageError, "meta file write failed");
    }
    return core::Error{};
}

}  // namespace

MasterKeyStore::MasterKeyStore(std::filesystem::path meta_path)
    : meta_path_(std::move(meta_path)) {}

MasterKeyStore::~MasterKeyStore() = default;

bool MasterKeyStore::exists() const {
    std::error_code ec;
    return std::filesystem::exists(meta_path_, ec);
}

core::Result<core::ByteVec> MasterKeyStore::initialize(const std::string& master_password,
                                                        core::ICryptoEngine& crypto) {
    if (exists()) {
        return core::Result<core::ByteVec>::Err(
            core::Error(core::ErrorCode::AlreadyExists,
                        "vault already initialized"));
    }

    // 1. 生成 16 字节 salt
    core::ByteVec salt(kSaltLen);
    randombytes_buf(salt.data(), kSaltLen);

    // 2. 用 password + salt 派生 KEK（32 字节）
    auto kek_result = crypto.derive_key(master_password, salt);
    if (!kek_result) {
        return core::Result<core::ByteVec>::Err(kek_result.error());
    }
    core::ByteVec kek = std::move(kek_result).value();

    // RAII：确保 KEK 在所有退出路径上清零
    struct KekZeroer {
        core::ByteVec& v;
        ~KekZeroer() { secure_zero(v); }
    } kek_zeroer{kek};

    // 3. 生成 32 字节 master_key
    auto gen_result = crypto.generate_key_and_iv();
    if (!gen_result) {
        return core::Result<core::ByteVec>::Err(gen_result.error());
    }
    core::ByteVec master_key = std::move(gen_result->first);

    // 4. 用 KEK 构造临时 CryptoEngine，加密 master_key
    crypto::CryptoEngine kek_engine(kek);
    auto enc_result = kek_engine.encrypt(master_key);
    if (!enc_result) {
        secure_zero(master_key);
        return core::Result<core::ByteVec>::Err(enc_result.error());
    }

    // 5. 写入 meta 文件
    MetaRecord rec;
    rec.salt = salt;
    rec.encrypted_blob = std::move(*enc_result);
    core::Error err = write_meta(meta_path_, rec);
    if (!err.ok()) {
        secure_zero(master_key);
        return core::Result<core::ByteVec>::Err(err);
    }

    return core::Result<core::ByteVec>::Ok(std::move(master_key));
}

core::Result<core::ByteVec> MasterKeyStore::unlock(const std::string& master_password,
                                                    core::ICryptoEngine& crypto) {
    auto meta_result = read_meta(meta_path_);
    if (!meta_result) {
        return core::Result<core::ByteVec>::Err(meta_result.error());
    }
    const MetaRecord& rec = *meta_result;

    // 1. 用 password + salt 派生 KEK
    auto kek_result = crypto.derive_key(master_password, rec.salt);
    if (!kek_result) {
        return core::Result<core::ByteVec>::Err(kek_result.error());
    }
    core::ByteVec kek = std::move(kek_result).value();

    struct KekZeroer {
        core::ByteVec& v;
        ~KekZeroer() { secure_zero(v); }
    } kek_zeroer{kek};

    // 2. 用 KEK 构造临时 CryptoEngine，解密 master_key
    crypto::CryptoEngine kek_engine(kek);
    auto dec_result = kek_engine.decrypt(rec.encrypted_blob);
    if (!dec_result) {
        // GCM tag 校验失败 → 主密码错误
        return core::Result<core::ByteVec>::Err(
            core::Error(core::ErrorCode::Unauthorized,
                        "master password incorrect"));
    }

    // 3. 校验长度并转回 ByteVec
    core::ByteVec master_key(dec_result->begin(), dec_result->end());
    if (master_key.size() != kMasterKeyLen) {
        secure_zero(master_key);
        return core::Result<core::ByteVec>::Err(
            core::Error(core::ErrorCode::CryptoError,
                        "decrypted master key length mismatch"));
    }

    return core::Result<core::ByteVec>::Ok(std::move(master_key));
}

}  // namespace pwdvault::service
