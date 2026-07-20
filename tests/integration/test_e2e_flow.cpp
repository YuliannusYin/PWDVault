// coding: utf-8
// =============================================================================
// test_e2e_flow.cpp
//
// PwdVault 端到端集成测试。模拟 UI 进程调用服务进程的核心用户流程，
// 跳过命名管道 I/O，通过 ServiceCore::handle_request 直接调用 ServiceCore。
//
// 测试策略：
//   - 实例化真实的 CryptoEngine / InMemoryStorageEngine / PasswordGenerator
//   - 使用临时目录下的 vault.meta 文件，避免污染用户数据
//   - 通过 protocol::serialize / deserialize 在协议层进行序列化往返，
//     最大化覆盖“UI 序列化 → service 反序列化 → 业务逻辑 → 序列化 → UI 反序列化”
//     的完整链路（命名管道传输环节除外）。
//   - 失败响应统一按 ErrorResponse 反序列化，提取 ErrorCode 与 message 进行断言。
//
// 覆盖流程：
//   1. 首次启动初始化（unlock 未初始化 vault 应返回 NotFound；首次 login 成功）
//   2. 锁定后访问被拒（list_entries 返回 Unauthorized）
//   3. 解锁（正确密码成功；错误密码失败；连续 5 次错误后进入冷却期）
//   4. CRUD 全流程（add / get / list / search / update / remove）
//   5. 密码生成器（generate_password / estimate_strength）
//   6. 心跳 ping（返回合理的 server_timestamp）
// =============================================================================
#include "ServiceCore.h"

#include "CryptoEngine.h"
#include "InMemoryStorageEngine.h"
#include "PasswordGenerator.h"

#include "Commands.h"
#include "Messages.h"
#include "Serializer.h"

#include "Error.h"
#include "Result.h"
#include "Types.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

/// 生成一个测试用主密码（足够长以满足 Argon2id 推荐输入长度）。
constexpr const char* kTestMasterPassword = "MasterPass123!";

/// 生成唯一的临时 meta 文件路径，每个测试用例独立使用，避免相互干扰。
std::filesystem::path make_unique_meta_path() {
    static std::atomic<uint64_t> counter{0};
    const uint64_t n = counter.fetch_add(1, std::memory_order_relaxed);
    std::random_device rd;
    const uint64_t rnd = (static_cast<uint64_t>(rd()) << 32) | rd();
    const auto name = "pwdvault_test_" + std::to_string(n) + "_" +
                      std::to_string(rnd) + ".meta";
    return std::filesystem::temp_directory_path() / name;
}

}  // namespace

namespace pwdvault::test {

/// 端到端流程测试夹具。
///
/// 每个 TEST_F 实例在 SetUp 中构造一个全新的 ServiceCore，使用独立的临时
/// meta 文件与 InMemoryStorageEngine，确保测试间相互隔离、可重复执行。
class E2EFlowTest : public ::testing::Test {
protected:
    void SetUp() override {
        meta_path_ = make_unique_meta_path();

        // 与 service main.cpp 一致：CryptoEngine 用空 master_key 构造，
        // 仅用于 derive_key / generate_key_and_iv；entry 加密用的 master_key
        // 由 ServiceCore 在 login/unlock 后通过 set_master_key 内部构造独立实例。
        crypto_ = std::make_unique<crypto::CryptoEngine>(core::ByteSpan{});
        storage_ = std::make_unique<storage::InMemoryStorageEngine>();
        generator_ = std::make_unique<generator::PasswordGenerator>();

        core_ = std::make_unique<service::ServiceCore>(
            std::move(crypto_), std::move(storage_), std::move(generator_),
            meta_path_);
    }

    void TearDown() override {
        core_.reset();
        std::error_code ec;
        std::filesystem::remove(meta_path_, ec);
        // 忽略删除失败（如文件未创建）
    }

    // ------------------------------------------------------------------------
    // 协议层辅助：发送请求并解析响应
    // ------------------------------------------------------------------------

    /// 发送一个 IPC 请求，返回反序列化后的响应或错误。
    ///
    /// 协议链路完全模拟真实 UI 行为：
    ///   1. 序列化 Req 为 payload
    ///   2. 构造 MessageHeader（与 IpcClient::send_request 一致）
    ///   3. 调用 ServiceCore::handle_request(payload, header)
    ///   4. 优先按 Resp 反序列化响应
    ///   5. 若失败，按 ErrorResponse 反序列化，提取 ErrorCode/message
    template <typename Req, typename Resp>
    core::Result<Resp> send(protocol::CommandId cmd, const Req& req) {
        core::ByteVec payload = protocol::serialize(req);

        protocol::MessageHeader header;
        header.magic = protocol::kMagic;
        header.version = protocol::kProtocolVersion;
        header.command = cmd;
        header.request_id = ++next_request_id_;
        header.payload_size = static_cast<uint32_t>(payload.size());

        core::ByteVec resp_bytes = core_->handle_request(payload, header);

        // 优先按期望响应类型反序列化
        core::ByteSpan resp_span(resp_bytes.data(), resp_bytes.size());
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

    /// 针对无请求负载的命令（Ping / Lock / ListEntries）发送空负载。
    template <typename Resp>
    core::Result<Resp> send_empty(protocol::CommandId cmd) {
        core::ByteVec payload;  // 空 payload

        protocol::MessageHeader header;
        header.magic = protocol::kMagic;
        header.version = protocol::kProtocolVersion;
        header.command = cmd;
        header.request_id = ++next_request_id_;
        header.payload_size = 0;

        core::ByteVec resp_bytes = core_->handle_request(payload, header);

        core::ByteSpan resp_span(resp_bytes.data(), resp_bytes.size());
        auto resp_result = protocol::deserialize<Resp>(resp_span);
        if (resp_result.ok()) {
            return core::Result<Resp>::Ok(std::move(resp_result).value());
        }

        auto err_result = protocol::deserialize<protocol::ErrorResponse>(resp_span);
        if (err_result.ok()) {
            const auto& err_resp = err_result.value();
            return core::Result<Resp>::Err(
                core::Error(err_resp.code, err_resp.message));
        }

        return core::Result<Resp>::Err(core::Error(
            core::ErrorCode::IpcError, "反序列化响应失败"));
    }

    /// 便捷：执行首次 login 设置主密码并进入已解锁状态。
    /// 多个 TEST_F 用例在执行 CRUD / generate 等需已解锁的操作前调用此辅助。
    void login_first_time(const std::string& password = kTestMasterPassword) {
        protocol::LoginRequest req;
        req.password = password;
        req.is_first_time = true;
        auto r = send<protocol::LoginRequest, protocol::LoginResponse>(
            protocol::CommandId::Login, req);
        ASSERT_TRUE(r.ok()) << "login first_time 失败: " << r.error().what();
        ASSERT_TRUE(r.value().success)
            << "login first_time 返回 success=false: "
            << r.value().error_message;
    }

    /// 便捷：执行 lock 命令。
    void lock() {
        auto r = send_empty<protocol::LockResponse>(protocol::CommandId::Lock);
        ASSERT_TRUE(r.ok()) << "lock 失败: " << r.error().what();
    }

    /// 便捷：执行 unlock。
    core::Result<protocol::UnlockResponse> unlock(const std::string& password) {
        protocol::UnlockRequest req;
        req.password = password;
        return send<protocol::UnlockRequest, protocol::UnlockResponse>(
            protocol::CommandId::Unlock, req);
    }

    std::unique_ptr<service::ServiceCore> core_;
    std::filesystem::path meta_path_;

private:
    uint32_t next_request_id_ = 0;
    std::unique_ptr<crypto::CryptoEngine> crypto_;
    std::unique_ptr<storage::InMemoryStorageEngine> storage_;
    std::unique_ptr<generator::PasswordGenerator> generator_;
};

// =============================================================================
// 1. 首次启动初始化
// =============================================================================

/// 未初始化 vault 时调用 unlock（即用空密码 / 任意密码尝试解锁）应返回 NotFound。
TEST_F(E2EFlowTest, UnlockUninitializedVaultReturnsNotFound) {
    auto r = unlock("any-password-here");
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::NotFound)
        << "实际错误: " << r.error().what();
}

/// 非首次 login 时未初始化也应返回 NotFound。
TEST_F(E2EFlowTest, LoginNotFirstTimeOnUninitializedVaultReturnsNotFound) {
    protocol::LoginRequest req;
    req.password = kTestMasterPassword;
    req.is_first_time = false;
    auto r = send<protocol::LoginRequest, protocol::LoginResponse>(
        protocol::CommandId::Login, req);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::NotFound);
}

/// 首次 login 应成功并返回 success=true，使 vault 进入已解锁状态。
TEST_F(E2EFlowTest, FirstTimeLoginSucceeds) {
    protocol::LoginRequest req;
    req.password = kTestMasterPassword;
    req.is_first_time = true;
    auto r = send<protocol::LoginRequest, protocol::LoginResponse>(
        protocol::CommandId::Login, req);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_TRUE(r.value().success);
    EXPECT_TRUE(r.value().error_message.empty());
}

/// 已初始化后再次以 first_time=true 调 login 应返回 AlreadyExists。
TEST_F(E2EFlowTest, FirstTimeLoginTwiceReturnsAlreadyExists) {
    login_first_time();

    protocol::LoginRequest req;
    req.password = kTestMasterPassword;
    req.is_first_time = true;
    auto r = send<protocol::LoginRequest, protocol::LoginResponse>(
        protocol::CommandId::Login, req);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::AlreadyExists);
}

// =============================================================================
// 2. 未解锁访问被拒
// =============================================================================

/// 登录后默认是已解锁状态。lock() 后再访问 list_entries 应返回 Unauthorized。
TEST_F(E2EFlowTest, ListEntriesAfterLockReturnsUnauthorized) {
    login_first_time();
    lock();

    auto r = send_empty<protocol::ListEntriesResponse>(
        protocol::CommandId::ListEntries);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::Unauthorized);
}

/// lock 后 add_entry 也应返回 Unauthorized。
TEST_F(E2EFlowTest, AddEntryAfterLockReturnsUnauthorized) {
    login_first_time();
    lock();

    protocol::AddEntryRequest req;
    req.entry.website = "github.com";
    req.entry.username = "user1";
    req.entry.password = "p@ssw0rd";
    req.entry.note = "工作账号";

    auto r = send<protocol::AddEntryRequest, protocol::AddEntryResponse>(
        protocol::CommandId::AddEntry, req);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, core::ErrorCode::Unauthorized);
}

// =============================================================================
// 3. 解锁流程
// =============================================================================

/// lock 后用正确密码 unlock 应成功。
TEST_F(E2EFlowTest, UnlockWithCorrectPasswordSucceeds) {
    login_first_time();
    lock();

    auto r = unlock(kTestMasterPassword);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_TRUE(r.value().success);
    EXPECT_TRUE(r.value().error_message.empty());
}

/// lock 后用错误密码 unlock 应失败（success=false），不返回 ErrorCode。
TEST_F(E2EFlowTest, UnlockWithWrongPasswordFails) {
    login_first_time();
    lock();

    auto r = unlock("WrongPassword");
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_FALSE(r.value().success);
    EXPECT_FALSE(r.value().error_message.empty());
}

/// 连续 5 次错误密码后，第 6 次（即使使用正确密码）应进入冷却期并返回
/// success=false 与 "too many failed attempts" 提示。
TEST_F(E2EFlowTest, FiveFailedUnlocksTriggerCooldown) {
    login_first_time();
    lock();

    // 前 5 次错误密码：均返回 success=false，但 error_message 为 "master password incorrect"
    for (int i = 0; i < 5; ++i) {
        auto r = unlock("WrongPassword");
        ASSERT_TRUE(r.ok()) << "第 " << (i + 1) << " 次 unlock 调用应返回响应";
        EXPECT_FALSE(r.value().success)
            << "第 " << (i + 1) << " 次 unlock 不应成功";
    }

    // 第 6 次：即使使用正确密码，也应被冷却拦截
    auto r = unlock(kTestMasterPassword);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_FALSE(r.value().success);
    EXPECT_NE(r.value().error_message.find("too many failed attempts"),
              std::string::npos)
        << "应包含冷却提示，实际: " << r.value().error_message;
}

// =============================================================================
// 4. CRUD 全流程
// =============================================================================

/// add_entry 应返回带 id 的 entry，id > 0；get_entry 应返回字段一致的原条目。
/// 验证 password 字段经加解密往返后恢复原文。
TEST_F(E2EFlowTest, AddAndGetEntryRoundtrip) {
    login_first_time();

    protocol::AddEntryRequest add_req;
    add_req.entry.website = "github.com";
    add_req.entry.username = "user1";
    add_req.entry.password = "p@ssw0rd";
    add_req.entry.note = "工作账号";

    auto add_resp = send<protocol::AddEntryRequest, protocol::AddEntryResponse>(
        protocol::CommandId::AddEntry, add_req);
    ASSERT_TRUE(add_resp.ok()) << add_resp.error().what();
    const auto& added = add_resp.value().entry;
    EXPECT_GT(added.id, 0);
    EXPECT_EQ(added.website, "github.com");
    EXPECT_EQ(added.username, "user1");
    EXPECT_EQ(added.password, "p@ssw0rd");   // 响应中应为明文
    EXPECT_EQ(added.note, "工作账号");
    EXPECT_GT(added.created_at, 0);
    EXPECT_GE(added.updated_at, added.created_at);

    // get_entry 验证字段一致（含 password 经加解密往返后恢复原文）
    protocol::GetEntryRequest get_req;
    get_req.id = added.id;
    auto get_resp = send<protocol::GetEntryRequest, protocol::GetEntryResponse>(
        protocol::CommandId::GetEntry, get_req);
    ASSERT_TRUE(get_resp.ok()) << get_resp.error().what();
    const auto& got = get_resp.value().entry;
    EXPECT_EQ(got.id, added.id);
    EXPECT_EQ(got.website, added.website);
    EXPECT_EQ(got.username, added.username);
    EXPECT_EQ(got.password, "p@ssw0rd");  // 关键：解密后恢复原文
    EXPECT_EQ(got.note, added.note);
    EXPECT_EQ(got.created_at, added.created_at);
    EXPECT_EQ(got.updated_at, added.updated_at);
    // 响应中 iv/tag 应被清空（decrypt_entry 清空）
    EXPECT_TRUE(got.iv.empty());
    EXPECT_TRUE(got.tag.empty());
}

/// 添加多条不同网站的条目，list_entries 应返回全部。
TEST_F(E2EFlowTest, ListEntriesReturnsAllAdded) {
    login_first_time();

    auto add_one = [this](const std::string& website,
                         const std::string& username) -> int64_t {
        protocol::AddEntryRequest req;
        req.entry.website = website;
        req.entry.username = username;
        req.entry.password = "p@ssw0rd-" + website;
        req.entry.note = "";
        auto resp = send<protocol::AddEntryRequest, protocol::AddEntryResponse>(
            protocol::CommandId::AddEntry, req);
        EXPECT_TRUE(resp.ok()) << resp.error().what();
        return resp.ok() ? resp.value().entry.id : 0;
    };

    const int64_t id1 = add_one("github.com", "user1");
    const int64_t id2 = add_one("gitlab.com", "user2");
    const int64_t id3 = add_one("bitbucket.org", "user3");
    ASSERT_GT(id1, 0);
    ASSERT_GT(id2, 0);
    ASSERT_GT(id3, 0);
    EXPECT_NE(id1, id2);
    EXPECT_NE(id2, id3);

    auto list_resp = send_empty<protocol::ListEntriesResponse>(
        protocol::CommandId::ListEntries);
    ASSERT_TRUE(list_resp.ok()) << list_resp.error().what();
    EXPECT_GE(list_resp.value().entries.size(), 3u);

    // 验证每条 password 字段已解密恢复原文
    bool found_github = false;
    for (const auto& e : list_resp.value().entries) {
        if (e.id == id1) {
            EXPECT_EQ(e.website, "github.com");
            EXPECT_EQ(e.username, "user1");
            EXPECT_EQ(e.password, "p@ssw0rd-github.com");
            found_github = true;
        }
    }
    EXPECT_TRUE(found_github);
}

/// search_entries 限定字段时应只返回匹配的条目。
TEST_F(E2EFlowTest, SearchEntriesByWebsiteReturnsMatchOnly) {
    login_first_time();

    // 添加 3 条不同网站条目
    auto add_one = [this](const std::string& website,
                         const std::string& username) -> int64_t {
        protocol::AddEntryRequest req;
        req.entry.website = website;
        req.entry.username = username;
        req.entry.password = "p@ssw0rd";
        auto resp = send<protocol::AddEntryRequest, protocol::AddEntryResponse>(
            protocol::CommandId::AddEntry, req);
        return resp.ok() ? resp.value().entry.id : 0;
    };
    ASSERT_GT(add_one("github.com", "user1"), 0);
    ASSERT_GT(add_one("gitlab.com", "user2"), 0);
    ASSERT_GT(add_one("bitbucket.org", "user3"), 0);

    // 搜索 "github" 仅匹配 website 字段
    protocol::SearchEntriesRequest search_req;
    search_req.query.text = "github";
    search_req.query.fields = { "website" };
    search_req.query.case_sensitive = false;

    auto search_resp =
        send<protocol::SearchEntriesRequest, protocol::SearchEntriesResponse>(
            protocol::CommandId::SearchEntries, search_req);
    ASSERT_TRUE(search_resp.ok()) << search_resp.error().what();
    ASSERT_EQ(search_resp.value().entries.size(), 1u);
    const auto& hit = search_resp.value().entries.front();
    EXPECT_EQ(hit.website, "github.com");
    EXPECT_EQ(hit.username, "user1");
    EXPECT_EQ(hit.password, "p@ssw0rd");  // 解密后恢复原文
}

/// update_entry 应更新字段并返回最新值；get_entry 应反映更新。
TEST_F(E2EFlowTest, UpdateEntryModifiesFields) {
    login_first_time();

    protocol::AddEntryRequest add_req;
    add_req.entry.website = "github.com";
    add_req.entry.username = "user1";
    add_req.entry.password = "p@ssw0rd";
    add_req.entry.note = "工作账号";

    auto add_resp = send<protocol::AddEntryRequest, protocol::AddEntryResponse>(
        protocol::CommandId::AddEntry, add_req);
    ASSERT_TRUE(add_resp.ok()) << add_resp.error().what();
    const int64_t id = add_resp.value().entry.id;
    ASSERT_GT(id, 0);

    // 等待 1 秒以确保 updated_at 严格大于原值
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 更新 username 与 note
    protocol::UpdateEntryRequest upd_req;
    upd_req.entry = add_resp.value().entry;
    upd_req.entry.username = "user1_updated";
    upd_req.entry.note = "个人账号";
    upd_req.entry.password = "new-p@ssw0rd-2024";

    auto upd_resp =
        send<protocol::UpdateEntryRequest, protocol::UpdateEntryResponse>(
            protocol::CommandId::UpdateEntry, upd_req);
    ASSERT_TRUE(upd_resp.ok()) << upd_resp.error().what();
    const auto& updated = upd_resp.value().entry;
    EXPECT_EQ(updated.id, id);
    EXPECT_EQ(updated.username, "user1_updated");
    EXPECT_EQ(updated.note, "个人账号");
    EXPECT_EQ(updated.password, "new-p@ssw0rd-2024");  // 响应中为明文
    EXPECT_GT(updated.updated_at, updated.created_at);

    // get_entry 验证已持久化
    protocol::GetEntryRequest get_req;
    get_req.id = id;
    auto get_resp = send<protocol::GetEntryRequest, protocol::GetEntryResponse>(
        protocol::CommandId::GetEntry, get_req);
    ASSERT_TRUE(get_resp.ok()) << get_resp.error().what();
    EXPECT_EQ(get_resp.value().entry.username, "user1_updated");
    EXPECT_EQ(get_resp.value().entry.note, "个人账号");
    EXPECT_EQ(get_resp.value().entry.password, "new-p@ssw0rd-2024");
}

/// remove_entry 应成功；之后 get_entry 应返回 NotFound。
TEST_F(E2EFlowTest, RemoveEntryThenGetReturnsNotFound) {
    login_first_time();

    protocol::AddEntryRequest add_req;
    add_req.entry.website = "github.com";
    add_req.entry.username = "user1";
    add_req.entry.password = "p@ssw0rd";
    add_req.entry.note = "";

    auto add_resp = send<protocol::AddEntryRequest, protocol::AddEntryResponse>(
        protocol::CommandId::AddEntry, add_req);
    ASSERT_TRUE(add_resp.ok()) << add_resp.error().what();
    const int64_t id = add_resp.value().entry.id;
    ASSERT_GT(id, 0);

    // remove_entry
    protocol::RemoveEntryRequest rm_req;
    rm_req.id = id;
    auto rm_resp =
        send<protocol::RemoveEntryRequest, protocol::RemoveEntryResponse>(
            protocol::CommandId::RemoveEntry, rm_req);
    ASSERT_TRUE(rm_resp.ok()) << rm_resp.error().what();

    // get_entry 应返回 NotFound
    protocol::GetEntryRequest get_req;
    get_req.id = id;
    auto get_resp = send<protocol::GetEntryRequest, protocol::GetEntryResponse>(
        protocol::CommandId::GetEntry, get_req);
    ASSERT_FALSE(get_resp.ok());
    EXPECT_EQ(get_resp.error().code, core::ErrorCode::NotFound);
}

/// remove 不存在的条目应返回 NotFound。
TEST_F(E2EFlowTest, RemoveNonexistentReturnsNotFound) {
    login_first_time();

    protocol::RemoveEntryRequest rm_req;
    rm_req.id = 99999;
    auto rm_resp =
        send<protocol::RemoveEntryRequest, protocol::RemoveEntryResponse>(
            protocol::CommandId::RemoveEntry, rm_req);
    ASSERT_FALSE(rm_resp.ok());
    EXPECT_EQ(rm_resp.error().code, core::ErrorCode::NotFound);
}

/// get 不存在的条目应返回 NotFound。
TEST_F(E2EFlowTest, GetNonexistentReturnsNotFound) {
    login_first_time();

    protocol::GetEntryRequest get_req;
    get_req.id = 88888;
    auto get_resp = send<protocol::GetEntryRequest, protocol::GetEntryResponse>(
        protocol::CommandId::GetEntry, get_req);
    ASSERT_FALSE(get_resp.ok());
    EXPECT_EQ(get_resp.error().code, core::ErrorCode::NotFound);
}

// =============================================================================
// 5. 密码生成器
// =============================================================================

/// generate_password 应返回指定长度的密码。
TEST_F(E2EFlowTest, GeneratePasswordReturnsExpectedLength) {
    login_first_time();

    protocol::GeneratePasswordRequest req;
    req.options.length = 20;
    req.options.use_uppercase = true;
    req.options.use_lowercase = true;
    req.options.use_digits = true;
    req.options.use_symbols = true;
    req.options.exclude_ambiguous = false;

    auto resp =
        send<protocol::GeneratePasswordRequest, protocol::GeneratePasswordResponse>(
            protocol::CommandId::GeneratePassword, req);
    ASSERT_TRUE(resp.ok()) << resp.error().what();
    EXPECT_EQ(resp.value().password.size(), 20u);
}

/// generate_password 在已锁定时应返回 Unauthorized。
TEST_F(E2EFlowTest, GeneratePasswordAfterLockReturnsUnauthorized) {
    login_first_time();
    lock();

    protocol::GeneratePasswordRequest req;
    req.options.length = 16;
    auto resp =
        send<protocol::GeneratePasswordRequest, protocol::GeneratePasswordResponse>(
            protocol::CommandId::GeneratePassword, req);
    ASSERT_FALSE(resp.ok());
    EXPECT_EQ(resp.error().code, core::ErrorCode::Unauthorized);
}

/// estimate_strength 对弱密码返回较低值，对强密码返回较高值。
TEST_F(E2EFlowTest, EstimateStrengthOrdersWeakAndStrong) {
    login_first_time();

    protocol::EstimateStrengthRequest weak_req;
    weak_req.password = "short";
    auto weak_resp =
        send<protocol::EstimateStrengthRequest, protocol::EstimateStrengthResponse>(
            protocol::CommandId::EstimateStrength, weak_req);
    ASSERT_TRUE(weak_resp.ok()) << weak_resp.error().what();
    const int weak_bits = weak_resp.value().strength_bits;
    EXPECT_GE(weak_bits, 0);

    protocol::EstimateStrengthRequest strong_req;
    strong_req.password = "Very$tr0ng&P@ssw0rd!2024";
    auto strong_resp =
        send<protocol::EstimateStrengthRequest, protocol::EstimateStrengthResponse>(
            protocol::CommandId::EstimateStrength, strong_req);
    ASSERT_TRUE(strong_resp.ok()) << strong_resp.error().what();
    const int strong_bits = strong_resp.value().strength_bits;

    EXPECT_GT(strong_bits, weak_bits)
        << "强密码 entropy=" << strong_bits
        << " 应大于弱密码 entropy=" << weak_bits;
}

// =============================================================================
// 6. Ping
// =============================================================================

/// ping 应返回合理的 server_timestamp（接近当前 Unix 时间戳）。
TEST_F(E2EFlowTest, PingReturnsRecentTimestamp) {
    auto resp = send_empty<protocol::PingResponse>(protocol::CommandId::Ping);
    ASSERT_TRUE(resp.ok()) << resp.error().what();
    EXPECT_GT(resp.value().server_timestamp, 0u);

    // 校验时间戳接近当前时间（允许 ±60 秒误差，覆盖时钟漂移与测试延迟）
    using namespace std::chrono;
    const uint64_t now = static_cast<uint64_t>(
        duration_cast<seconds>(system_clock::now().time_since_epoch()).count());
    const int64_t diff = static_cast<int64_t>(resp.value().server_timestamp) -
                         static_cast<int64_t>(now);
    const int64_t abs_diff = diff < 0 ? -diff : diff;
    EXPECT_LE(abs_diff, 60)
        << "server_timestamp=" << resp.value().server_timestamp
        << " 距 now=" << now << " 偏差 " << diff << " 秒";
}

/// ping 在未解锁状态下也应可调用（不属于敏感操作）。
TEST_F(E2EFlowTest, PingWorksEvenWhenLocked) {
    login_first_time();
    lock();

    auto resp = send_empty<protocol::PingResponse>(protocol::CommandId::Ping);
    ASSERT_TRUE(resp.ok()) << resp.error().what();
    EXPECT_GT(resp.value().server_timestamp, 0u);
}

// =============================================================================
// 7. 完整用户旅程：初始化 → 解锁 → CRUD → 锁定
// =============================================================================

/// 端到端完整旅程：覆盖初始化、CRUD、锁定/解锁、删除等核心步骤的串联执行。
/// 此用例验证多个操作按真实用户使用顺序串联时不会出现状态泄漏或互相干扰。
TEST_F(E2EFlowTest, FullUserJourneyFromInitToLock) {
    // 1. 首次 login 设置主密码
    {
        protocol::LoginRequest req;
        req.password = kTestMasterPassword;
        req.is_first_time = true;
        auto r = send<protocol::LoginRequest, protocol::LoginResponse>(
            protocol::CommandId::Login, req);
        ASSERT_TRUE(r.ok()) << r.error().what();
        ASSERT_TRUE(r.value().success);
    }

    // 2. 添加 3 条不同网站的条目
    std::vector<int64_t> ids;
    for (int i = 0; i < 3; ++i) {
        protocol::AddEntryRequest req;
        req.entry.website = "site" + std::to_string(i) + ".com";
        req.entry.username = "user" + std::to_string(i);
        req.entry.password = "pwd-" + std::to_string(i);
        req.entry.note = "note " + std::to_string(i);
        auto r = send<protocol::AddEntryRequest, protocol::AddEntryResponse>(
            protocol::CommandId::AddEntry, req);
        ASSERT_TRUE(r.ok()) << r.error().what();
        ASSERT_GT(r.value().entry.id, 0);
        ids.push_back(r.value().entry.id);
    }

    // 3. list 应返回至少 3 条
    {
        auto r = send_empty<protocol::ListEntriesResponse>(
            protocol::CommandId::ListEntries);
        ASSERT_TRUE(r.ok()) << r.error().what();
        EXPECT_GE(r.value().entries.size(), 3u);
    }

    // 4. 更新第 1 条
    {
        protocol::UpdateEntryRequest req;
        req.entry.id = ids[0];
        req.entry.website = "site0-updated.com";
        req.entry.username = "user0_updated";
        req.entry.password = "new-pwd-0";
        req.entry.note = "updated note";
        auto r = send<protocol::UpdateEntryRequest, protocol::UpdateEntryResponse>(
            protocol::CommandId::UpdateEntry, req);
        ASSERT_TRUE(r.ok()) << r.error().what();
        EXPECT_EQ(r.value().entry.username, "user0_updated");
    }

    // 5. 删除第 2 条
    {
        protocol::RemoveEntryRequest req;
        req.id = ids[1];
        auto r = send<protocol::RemoveEntryRequest, protocol::RemoveEntryResponse>(
            protocol::CommandId::RemoveEntry, req);
        ASSERT_TRUE(r.ok()) << r.error().what();
    }

    // 6. lock 后再 unlock
    lock();
    {
        auto r = unlock(kTestMasterPassword);
        ASSERT_TRUE(r.ok()) << r.error().what();
        ASSERT_TRUE(r.value().success);
    }

    // 7. unlock 后 list 应反映更新与删除（剩余 2 条）
    {
        auto r = send_empty<protocol::ListEntriesResponse>(
            protocol::CommandId::ListEntries);
        ASSERT_TRUE(r.ok()) << r.error().what();
        EXPECT_EQ(r.value().entries.size(), 2u);
    }

    // 8. ping 始终可用
    {
        auto r = send_empty<protocol::PingResponse>(protocol::CommandId::Ping);
        ASSERT_TRUE(r.ok()) << r.error().what();
        EXPECT_GT(r.value().server_timestamp, 0u);
    }
}

}  // namespace pwdvault::test
