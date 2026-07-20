// coding: utf-8
// =============================================================================
// ServiceCore.cpp
//
// 命令分发与业务逻辑实现。
//
// 加密约定：
//   - entry.password 在内存中为明文 std::string
//   - 持久化前用 entry_crypto_->encrypt(password) 加密，得到 [IV || ciphertext || tag]
//   - 拆分为 iv(12) / password(密文) / tag(16) 三段，分别存入 storage
//   - 读取时反向拼回 [IV || ciphertext || tag]，用 entry_crypto_->decrypt 解密
//
// 登录失败计数：
//   - 连续 5 次失败后锁定 5 分钟，期间 Unlock 直接返回错误
//   - 成功解锁后计数清零
// =============================================================================
#include "ServiceCore.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "CryptoEngine.h"
#include "ICryptoEngine.h"
#include "IPasswordGenerator.h"
#include "IStorageEngine.h"
#include "MasterKeyStore.h"
#include "Types.h"

#include "Commands.h"
#include "Messages.h"
#include "Serializer.h"

#include <sodium.h>

namespace pwdvault::service {

namespace {

constexpr size_t kIvLen = 12;
constexpr size_t kTagLen = 16;
constexpr int kMaxLoginAttempts = 5;
constexpr auto kLockoutDuration = std::chrono::minutes(5);

/// 安全清零 ByteVec。
void secure_zero(core::ByteVec& v) {
    if (!v.empty()) {
        sodium_memzero(v.data(), v.size());
    }
}

/// 安全清零 std::string。
void secure_zero_string(std::string& s) {
    if (!s.empty()) {
        sodium_memzero(s.data(), s.size());
    }
}

/// 当前 Unix 时间戳（秒）。
uint64_t now_unix_seconds() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<seconds>(system_clock::now().time_since_epoch()).count());
}

}  // namespace

// ============================================================================
// 构造与析构
// ============================================================================

ServiceCore::ServiceCore(std::unique_ptr<core::ICryptoEngine> crypto,
                         std::unique_ptr<core::IStorageEngine> storage,
                         std::unique_ptr<core::IPasswordGenerator> generator,
                         std::filesystem::path meta_path)
    : crypto_(std::move(crypto)),
      storage_(std::move(storage)),
      generator_(std::move(generator)),
      key_store_(std::make_unique<MasterKeyStore>(std::move(meta_path))) {}

ServiceCore::~ServiceCore() {
    std::lock_guard<std::mutex> lock(mutex_);
    clear_master_key();
}

// ============================================================================
// 内部辅助
// ============================================================================

void ServiceCore::set_master_key(core::ByteVec key) {
    // 调用方持锁
    master_key_ = std::move(key);
    entry_crypto_ = std::make_unique<crypto::CryptoEngine>(master_key_);
}

void ServiceCore::clear_master_key() {
    // 调用方持锁
    entry_crypto_.reset();
    secure_zero(master_key_);
    master_key_.clear();
}

bool ServiceCore::is_in_cooldown() const {
    return lock_until_ > std::chrono::steady_clock::now();
}

core::ByteVec ServiceCore::make_error(ErrorCode code, std::string message) const {
    protocol::ErrorResponse resp;
    resp.code = code;
    resp.message = std::move(message);
    return protocol::serialize(resp);
}

core::Result<core::PasswordEntry> ServiceCore::encrypt_entry(
    core::PasswordEntry entry) const {
    if (!entry_crypto_) {
        return core::Result<core::PasswordEntry>::Err(
            core::Error(core::ErrorCode::Unauthorized, "vault is locked"));
    }

    // 明文 password → ByteSpan
    core::ByteSpan plaintext(
        reinterpret_cast<const std::byte*>(entry.password.data()),
        entry.password.size());

    auto enc_result = entry_crypto_->encrypt(plaintext);
    if (!enc_result) {
        return core::Result<core::PasswordEntry>::Err(enc_result.error());
    }

    const auto& blob = *enc_result;
    // blob = [IV(12) || ciphertext || tag(16)]
    if (blob.size() < kIvLen + kTagLen) {
        return core::Result<core::PasswordEntry>::Err(
            core::Error(core::ErrorCode::CryptoError, "ciphertext blob too short"));
    }

    // 拆分
    entry.iv.assign(blob.begin(), blob.begin() + kIvLen);
    entry.tag.assign(blob.end() - kTagLen, blob.end());
    // password 字段存密文部分
    entry.password.assign(
        reinterpret_cast<const char*>(blob.data() + kIvLen),
        blob.size() - kIvLen - kTagLen);

    return core::Result<core::PasswordEntry>::Ok(std::move(entry));
}

core::Result<core::PasswordEntry> ServiceCore::decrypt_entry(
    core::PasswordEntry entry) const {
    if (!entry_crypto_) {
        return core::Result<core::PasswordEntry>::Err(
            core::Error(core::ErrorCode::Unauthorized, "vault is locked"));
    }

    // 重新拼装 [IV || ciphertext || tag]
    core::ByteVec blob;
    blob.reserve(entry.iv.size() + entry.password.size() + entry.tag.size());
    blob.insert(blob.end(), entry.iv.begin(), entry.iv.end());
    blob.insert(blob.end(),
                reinterpret_cast<const std::byte*>(entry.password.data()),
                reinterpret_cast<const std::byte*>(entry.password.data()) +
                    entry.password.size());
    blob.insert(blob.end(), entry.tag.begin(), entry.tag.end());

    auto dec_result = entry_crypto_->decrypt(blob);
    if (!dec_result) {
        return core::Result<core::PasswordEntry>::Err(dec_result.error());
    }

    entry.password = std::move(*dec_result);
    entry.iv.clear();
    entry.tag.clear();
    return core::Result<core::PasswordEntry>::Ok(std::move(entry));
}

// ============================================================================
// 请求分发
// ============================================================================

core::ByteVec ServiceCore::handle_request(core::ByteSpan payload,
                                          const protocol::MessageHeader& req_header) {
    using protocol::CommandId;
    switch (req_header.command) {
        case CommandId::Ping:             return handle_ping();
        case CommandId::Login:            return handle_login(payload);
        case CommandId::Unlock:           return handle_unlock(payload);
        case CommandId::Lock:             return handle_lock();
        case CommandId::AddEntry:         return handle_add_entry(payload);
        case CommandId::UpdateEntry:      return handle_update_entry(payload);
        case CommandId::RemoveEntry:      return handle_remove_entry(payload);
        case CommandId::GetEntry:         return handle_get_entry(payload);
        case CommandId::SearchEntries:    return handle_search_entries(payload);
        case CommandId::ListEntries:      return handle_list_entries();
        case CommandId::GeneratePassword: return handle_generate_password(payload);
        case CommandId::EstimateStrength: return handle_estimate_strength(payload);
        case CommandId::Shutdown:
            // Shutdown 由 main.cpp 的 handler lambda 拦截处理，不应到达此处
            return protocol::serialize(protocol::ShutdownResponse{});
    }
    return make_error(core::ErrorCode::InvalidArgument, "unknown command");
}

// ============================================================================
// 各 handler
// ============================================================================

core::ByteVec ServiceCore::handle_ping() {
    protocol::PingResponse resp;
    resp.server_timestamp = now_unix_seconds();
    return protocol::serialize(resp);
}

core::ByteVec ServiceCore::handle_login(core::ByteSpan payload) {
    auto req_result = protocol::deserialize<protocol::LoginRequest>(payload);
    if (!req_result) {
        return make_error(core::ErrorCode::InvalidArgument,
                          std::string("malformed LoginRequest: ") +
                              req_result.error().what());
    }
    const auto& req = *req_result;

    std::lock_guard<std::mutex> lock(mutex_);

    if (req.is_first_time) {
        // 首次设置主密码
        if (key_store_->exists()) {
            return make_error(core::ErrorCode::AlreadyExists,
                              "vault already initialized");
        }
        auto init_result = key_store_->initialize(req.password, *crypto_);
        if (!init_result) {
            return make_error(init_result.error().code,
                              std::string("initialize failed: ") +
                                  init_result.error().what());
        }
        set_master_key(std::move(*init_result));
        unlocked_ = true;
        login_attempts_ = 0;

        protocol::LoginResponse resp;
        resp.success = true;
        return protocol::serialize(resp);
    } else {
        // 验证已有主密码：等价于 unlock
        if (!key_store_->exists()) {
            return make_error(core::ErrorCode::NotFound,
                              "vault not initialized");
        }
        if (is_in_cooldown()) {
            protocol::LoginResponse resp;
            resp.success = false;
            resp.error_message = "too many failed attempts, please wait";
            return protocol::serialize(resp);
        }
        auto unlock_result = key_store_->unlock(req.password, *crypto_);
        if (!unlock_result) {
            ++login_attempts_;
            if (login_attempts_ >= kMaxLoginAttempts) {
                lock_until_ = std::chrono::steady_clock::now() + kLockoutDuration;
            }
            protocol::LoginResponse resp;
            resp.success = false;
            resp.error_message = unlock_result.error().what();
            return protocol::serialize(resp);
        }
        set_master_key(std::move(*unlock_result));
        unlocked_ = true;
        login_attempts_ = 0;

        protocol::LoginResponse resp;
        resp.success = true;
        return protocol::serialize(resp);
    }
}

core::ByteVec ServiceCore::handle_unlock(core::ByteSpan payload) {
    auto req_result = protocol::deserialize<protocol::UnlockRequest>(payload);
    if (!req_result) {
        return make_error(core::ErrorCode::InvalidArgument,
                          std::string("malformed UnlockRequest: ") +
                              req_result.error().what());
    }
    const auto& req = *req_result;

    std::lock_guard<std::mutex> lock(mutex_);

    if (!key_store_->exists()) {
        return make_error(core::ErrorCode::NotFound, "vault not initialized");
    }
    if (is_in_cooldown()) {
        protocol::UnlockResponse resp;
        resp.success = false;
        resp.error_message = "too many failed attempts, please wait";
        return protocol::serialize(resp);
    }

    auto unlock_result = key_store_->unlock(req.password, *crypto_);
    if (!unlock_result) {
        ++login_attempts_;
        if (login_attempts_ >= kMaxLoginAttempts) {
            lock_until_ = std::chrono::steady_clock::now() + kLockoutDuration;
        }
        protocol::UnlockResponse resp;
        resp.success = false;
        resp.error_message = unlock_result.error().what();
        return protocol::serialize(resp);
    }

    set_master_key(std::move(*unlock_result));
    unlocked_ = true;
    login_attempts_ = 0;

    protocol::UnlockResponse resp;
    resp.success = true;
    return protocol::serialize(resp);
}

core::ByteVec ServiceCore::handle_lock() {
    std::lock_guard<std::mutex> lock(mutex_);
    clear_master_key();
    unlocked_ = false;
    return protocol::serialize(protocol::LockResponse{});
}

core::ByteVec ServiceCore::handle_add_entry(core::ByteSpan payload) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!unlocked_) {
        return make_error(core::ErrorCode::Unauthorized, "vault is locked");
    }
    auto req_result = protocol::deserialize<protocol::AddEntryRequest>(payload);
    if (!req_result) {
        return make_error(core::ErrorCode::InvalidArgument,
                          std::string("malformed AddEntryRequest: ") +
                              req_result.error().what());
    }

    // 保存明文副本用于响应
    core::PasswordEntry plain_entry = req_result->entry;

    // 加密 password 字段
    auto enc_result = encrypt_entry(req_result->entry);
    if (!enc_result) {
        return make_error(enc_result.error().code, enc_result.error().what());
    }

    auto add_result = storage_->add_entry(*enc_result);
    if (!add_result) {
        return make_error(add_result.error().code, add_result.error().what());
    }

    // 响应中返回明文条目（带分配的 id 与时间戳）
    plain_entry.id = add_result->id;
    plain_entry.created_at = add_result->created_at;
    plain_entry.updated_at = add_result->updated_at;

    protocol::AddEntryResponse resp;
    resp.entry = std::move(plain_entry);
    return protocol::serialize(resp);
}

core::ByteVec ServiceCore::handle_update_entry(core::ByteSpan payload) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!unlocked_) {
        return make_error(core::ErrorCode::Unauthorized, "vault is locked");
    }
    auto req_result = protocol::deserialize<protocol::UpdateEntryRequest>(payload);
    if (!req_result) {
        return make_error(core::ErrorCode::InvalidArgument,
                          std::string("malformed UpdateEntryRequest: ") +
                              req_result.error().what());
    }

    core::PasswordEntry plain_entry = req_result->entry;

    auto enc_result = encrypt_entry(req_result->entry);
    if (!enc_result) {
        return make_error(enc_result.error().code, enc_result.error().what());
    }

    auto upd_result = storage_->update_entry(*enc_result);
    if (!upd_result) {
        return make_error(upd_result.error().code, upd_result.error().what());
    }

    plain_entry.updated_at = upd_result->updated_at;

    protocol::UpdateEntryResponse resp;
    resp.entry = std::move(plain_entry);
    return protocol::serialize(resp);
}

core::ByteVec ServiceCore::handle_remove_entry(core::ByteSpan payload) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!unlocked_) {
        return make_error(core::ErrorCode::Unauthorized, "vault is locked");
    }
    auto req_result = protocol::deserialize<protocol::RemoveEntryRequest>(payload);
    if (!req_result) {
        return make_error(core::ErrorCode::InvalidArgument,
                          std::string("malformed RemoveEntryRequest: ") +
                              req_result.error().what());
    }
    core::Error err = storage_->remove_entry(req_result->id);
    if (!err.ok()) {
        return make_error(err.code, err.what());
    }
    return protocol::serialize(protocol::RemoveEntryResponse{});
}

core::ByteVec ServiceCore::handle_get_entry(core::ByteSpan payload) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!unlocked_) {
        return make_error(core::ErrorCode::Unauthorized, "vault is locked");
    }
    auto req_result = protocol::deserialize<protocol::GetEntryRequest>(payload);
    if (!req_result) {
        return make_error(core::ErrorCode::InvalidArgument,
                          std::string("malformed GetEntryRequest: ") +
                              req_result.error().what());
    }
    auto get_result = storage_->get_entry(req_result->id);
    if (!get_result) {
        return make_error(get_result.error().code, get_result.error().what());
    }
    auto dec_result = decrypt_entry(std::move(*get_result));
    if (!dec_result) {
        return make_error(dec_result.error().code, dec_result.error().what());
    }
    protocol::GetEntryResponse resp;
    resp.entry = std::move(*dec_result);
    return protocol::serialize(resp);
}

core::ByteVec ServiceCore::handle_search_entries(core::ByteSpan payload) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!unlocked_) {
        return make_error(core::ErrorCode::Unauthorized, "vault is locked");
    }
    auto req_result = protocol::deserialize<protocol::SearchEntriesRequest>(payload);
    if (!req_result) {
        return make_error(core::ErrorCode::InvalidArgument,
                          std::string("malformed SearchEntriesRequest: ") +
                              req_result.error().what());
    }
    auto search_result = storage_->search_entries(req_result->query);
    if (!search_result) {
        return make_error(search_result.error().code, search_result.error().what());
    }
    protocol::SearchEntriesResponse resp;
    resp.entries.reserve(search_result->size());
    for (auto& e : *search_result) {
        auto dec_result = decrypt_entry(std::move(e));
        if (!dec_result) {
            return make_error(dec_result.error().code, dec_result.error().what());
        }
        resp.entries.push_back(std::move(*dec_result));
    }
    return protocol::serialize(resp);
}

core::ByteVec ServiceCore::handle_list_entries() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!unlocked_) {
        return make_error(core::ErrorCode::Unauthorized, "vault is locked");
    }
    auto list_result = storage_->list_entries();
    if (!list_result) {
        return make_error(list_result.error().code, list_result.error().what());
    }
    protocol::ListEntriesResponse resp;
    resp.entries.reserve(list_result->size());
    for (auto& e : *list_result) {
        auto dec_result = decrypt_entry(std::move(e));
        if (!dec_result) {
            return make_error(dec_result.error().code, dec_result.error().what());
        }
        resp.entries.push_back(std::move(*dec_result));
    }
    return protocol::serialize(resp);
}

core::ByteVec ServiceCore::handle_generate_password(core::ByteSpan payload) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!unlocked_) {
        return make_error(core::ErrorCode::Unauthorized, "vault is locked");
    }
    auto req_result = protocol::deserialize<protocol::GeneratePasswordRequest>(payload);
    if (!req_result) {
        return make_error(core::ErrorCode::InvalidArgument,
                          std::string("malformed GeneratePasswordRequest: ") +
                              req_result.error().what());
    }
    auto gen_result = generator_->generate(req_result->options);
    if (!gen_result) {
        return make_error(gen_result.error().code, gen_result.error().what());
    }
    protocol::GeneratePasswordResponse resp;
    resp.password = std::move(*gen_result);
    return protocol::serialize(resp);
}

core::ByteVec ServiceCore::handle_estimate_strength(core::ByteSpan payload) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!unlocked_) {
        return make_error(core::ErrorCode::Unauthorized, "vault is locked");
    }
    auto req_result = protocol::deserialize<protocol::EstimateStrengthRequest>(payload);
    if (!req_result) {
        return make_error(core::ErrorCode::InvalidArgument,
                          std::string("malformed EstimateStrengthRequest: ") +
                              req_result.error().what());
    }
    protocol::EstimateStrengthResponse resp;
    resp.strength_bits = generator_->estimate_strength(req_result->password);
    // 请求中的密码不再需要，清零（反序列化的 string 在 req_result 析构时释放，但不清零）
    secure_zero_string(req_result->password);
    return protocol::serialize(resp);
}

}  // namespace pwdvault::service
