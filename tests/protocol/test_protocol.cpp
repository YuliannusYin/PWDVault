// coding: utf-8
// =============================================================================
// test_protocol.cpp
//
// PwdVault IPC 协议层 GoogleTest 单元测试。覆盖：
//   - MessageHeader 大小与字段布局
//   - 各 Request/Response 序列化→反序列化往返（round-trip）
//   - 基础类型与 core 类型的 round-trip
//   - pack_message + parse_header 正确解出 header 与 payload 边界
//   - parse_header 在数据不足（8 字节）时返回错误
//   - parse_header 在 magic 不匹配时返回错误
//   - command_name 返回正确字符串
// =============================================================================
#include "Commands.h"
#include "Messages.h"
#include "Serializer.h"

#include "Error.h"
#include "Result.h"
#include "Types.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

namespace {

pwdvault::core::ByteVec make_bytes(const std::string& s) {
    return pwdvault::core::ByteVec(
        reinterpret_cast<const std::byte*>(s.data()),
        reinterpret_cast<const std::byte*>(s.data()) + s.size());
}

pwdvault::core::PasswordEntry make_sample_entry() {
    pwdvault::core::PasswordEntry e;
    e.id = 42;
    e.website = "github.com";
    e.username = "alice";
    e.password = "p@ssw0rd!";
    e.note = "personal account";
    e.created_at = 1700000000;
    e.updated_at = 1710000000;
    e.iv = make_bytes("012345678901");    // 12 字节
    e.tag = make_bytes("0123456789012345"); // 16 字节
    return e;
}

}  // namespace

// ---------------------------------------------------------------------------
// MessageHeader 大小与字段
// ---------------------------------------------------------------------------

TEST(ProtocolMessageHeader, SizeIs16Bytes) {
    static_assert(sizeof(pwdvault::protocol::MessageHeader) == 16,
                  "MessageHeader must be 16 bytes");
    EXPECT_EQ(sizeof(pwdvault::protocol::MessageHeader), 16u);
}

TEST(ProtocolMessageHeader, DefaultFieldsMatchConstants) {
    pwdvault::protocol::MessageHeader h;
    EXPECT_EQ(h.magic, pwdvault::protocol::kMagic);
    EXPECT_EQ(h.version, pwdvault::protocol::kProtocolVersion);
    EXPECT_EQ(h.command, pwdvault::protocol::CommandId::Ping);
    EXPECT_EQ(h.request_id, 0u);
    EXPECT_EQ(h.payload_size, 0u);
}

// ---------------------------------------------------------------------------
// command_name
// ---------------------------------------------------------------------------

TEST(ProtocolCommandName, KnownCommands) {
    using namespace pwdvault::protocol;
    EXPECT_EQ(command_name(CommandId::Ping), "Ping");
    EXPECT_EQ(command_name(CommandId::Shutdown), "Shutdown");
    EXPECT_EQ(command_name(CommandId::Unlock), "Unlock");
    EXPECT_EQ(command_name(CommandId::Lock), "Lock");
    EXPECT_EQ(command_name(CommandId::EnableProgramPassword), "EnableProgramPassword");
    EXPECT_EQ(command_name(CommandId::DisableProgramPassword), "DisableProgramPassword");
    EXPECT_EQ(command_name(CommandId::ChangeProgramPassword), "ChangeProgramPassword");
    EXPECT_EQ(command_name(CommandId::GetVaultStatus), "GetVaultStatus");
    EXPECT_EQ(command_name(CommandId::AddEntry), "AddEntry");
    EXPECT_EQ(command_name(CommandId::UpdateEntry), "UpdateEntry");
    EXPECT_EQ(command_name(CommandId::RemoveEntry), "RemoveEntry");
    EXPECT_EQ(command_name(CommandId::GetEntry), "GetEntry");
    EXPECT_EQ(command_name(CommandId::SearchEntries), "SearchEntries");
    EXPECT_EQ(command_name(CommandId::ListEntries), "ListEntries");
    EXPECT_EQ(command_name(CommandId::GeneratePassword), "GeneratePassword");
    EXPECT_EQ(command_name(CommandId::EstimateStrength), "EstimateStrength");
}

TEST(ProtocolCommandName, UnknownCommandReturnsUnknown) {
    using namespace pwdvault::protocol;
    EXPECT_EQ(command_name(static_cast<CommandId>(0xFFFF)), "Unknown");
}

// ---------------------------------------------------------------------------
// 基础类型 round-trip
// ---------------------------------------------------------------------------

TEST(ProtocolPrimitiveRoundtrip, Uint16) {
    using namespace pwdvault::protocol;
    const uint16_t v = 0x1234;
    auto bytes = serialize(v);
    auto r = deserialize<uint16_t>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_EQ(r.value(), v);
}

TEST(ProtocolPrimitiveRoundtrip, Uint32) {
    using namespace pwdvault::protocol;
    const uint32_t v = 0xDEADBEEFu;
    auto bytes = serialize(v);
    auto r = deserialize<uint32_t>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_EQ(r.value(), v);
}

TEST(ProtocolPrimitiveRoundtrip, Uint64) {
    using namespace pwdvault::protocol;
    const uint64_t v = 0x0123456789ABCDEFULL;
    auto bytes = serialize(v);
    auto r = deserialize<uint64_t>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_EQ(r.value(), v);
}

TEST(ProtocolPrimitiveRoundtrip, Int64) {
    using namespace pwdvault::protocol;
    const int64_t v = -1234567890123LL;
    auto bytes = serialize(v);
    auto r = deserialize<int64_t>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_EQ(r.value(), v);
}

TEST(ProtocolPrimitiveRoundtrip, BoolTrue) {
    using namespace pwdvault::protocol;
    auto bytes = serialize(true);
    auto r = deserialize<bool>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_TRUE(r.value());
}

TEST(ProtocolPrimitiveRoundtrip, BoolFalse) {
    using namespace pwdvault::protocol;
    auto bytes = serialize(false);
    auto r = deserialize<bool>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_FALSE(r.value());
}

TEST(ProtocolPrimitiveRoundtrip, String) {
    using namespace pwdvault::protocol;
    const std::string v = "hello, 世界! pwdvault";
    auto bytes = serialize(v);
    auto r = deserialize<std::string>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_EQ(r.value(), v);
}

TEST(ProtocolPrimitiveRoundtrip, EmptyString) {
    using namespace pwdvault::protocol;
    const std::string v;
    auto bytes = serialize(v);
    auto r = deserialize<std::string>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_TRUE(r.value().empty());
}

TEST(ProtocolPrimitiveRoundtrip, ByteVec) {
    using namespace pwdvault::protocol;
    const auto v = make_bytes("\x00\x01\x02\xff\xfe\x00");
    auto bytes = serialize(v);
    auto r = deserialize<pwdvault::core::ByteVec>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_EQ(r.value(), v);
}

TEST(ProtocolPrimitiveRoundtrip, EmptyByteVec) {
    using namespace pwdvault::protocol;
    const pwdvault::core::ByteVec v;
    auto bytes = serialize(v);
    auto r = deserialize<pwdvault::core::ByteVec>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_TRUE(r.value().empty());
}

TEST(ProtocolPrimitiveRoundtrip, ErrorCode) {
    using namespace pwdvault::protocol;
    const auto v = pwdvault::core::ErrorCode::Unauthorized;
    auto bytes = serialize(v);
    auto r = deserialize<pwdvault::core::ErrorCode>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_EQ(r.value(), v);
}

// ---------------------------------------------------------------------------
// core 类型 round-trip
// ---------------------------------------------------------------------------

TEST(ProtocolCoreRoundtrip, PasswordEntry) {
    using namespace pwdvault::protocol;
    const auto orig = make_sample_entry();
    auto bytes = serialize(orig);
    auto r = deserialize<pwdvault::core::PasswordEntry>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    const auto& got = r.value();
    EXPECT_EQ(got.id, orig.id);
    EXPECT_EQ(got.website, orig.website);
    EXPECT_EQ(got.username, orig.username);
    EXPECT_EQ(got.password, orig.password);
    EXPECT_EQ(got.note, orig.note);
    EXPECT_EQ(got.created_at, orig.created_at);
    EXPECT_EQ(got.updated_at, orig.updated_at);
    EXPECT_EQ(got.iv, orig.iv);
    EXPECT_EQ(got.tag, orig.tag);
}

TEST(ProtocolCoreRoundtrip, SearchQueryWithFields) {
    using namespace pwdvault::protocol;
    pwdvault::core::SearchQuery orig;
    orig.text = "github";
    orig.fields = { "website", "username", "note" };
    orig.case_sensitive = true;
    auto bytes = serialize(orig);
    auto r = deserialize<pwdvault::core::SearchQuery>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    const auto& got = r.value();
    EXPECT_EQ(got.text, orig.text);
    EXPECT_EQ(got.fields, orig.fields);
    EXPECT_EQ(got.case_sensitive, orig.case_sensitive);
}

TEST(ProtocolCoreRoundtrip, SearchQueryEmptyFields) {
    using namespace pwdvault::protocol;
    pwdvault::core::SearchQuery orig;
    orig.text = "";
    orig.fields = {};
    orig.case_sensitive = false;
    auto bytes = serialize(orig);
    auto r = deserialize<pwdvault::core::SearchQuery>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_EQ(r.value().text, orig.text);
    EXPECT_TRUE(r.value().fields.empty());
    EXPECT_FALSE(r.value().case_sensitive);
}

TEST(ProtocolCoreRoundtrip, PasswordGeneratorOptions) {
    using namespace pwdvault::protocol;
    pwdvault::core::PasswordGeneratorOptions orig;
    orig.length = 32;
    orig.use_uppercase = false;
    orig.use_lowercase = true;
    orig.use_digits = true;
    orig.use_symbols = false;
    orig.custom_chars = "!@#$";
    orig.exclude_ambiguous = true;
    auto bytes = serialize(orig);
    auto r = deserialize<pwdvault::core::PasswordGeneratorOptions>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    const auto& got = r.value();
    EXPECT_EQ(got.length, orig.length);
    EXPECT_EQ(got.use_uppercase, orig.use_uppercase);
    EXPECT_EQ(got.use_lowercase, orig.use_lowercase);
    EXPECT_EQ(got.use_digits, orig.use_digits);
    EXPECT_EQ(got.use_symbols, orig.use_symbols);
    EXPECT_EQ(got.custom_chars, orig.custom_chars);
    EXPECT_EQ(got.exclude_ambiguous, orig.exclude_ambiguous);
}

// ---------------------------------------------------------------------------
// 请求消息 round-trip
// ---------------------------------------------------------------------------

TEST(ProtocolRequestRoundtrip, PingRequest) {
    using namespace pwdvault::protocol;
    PingRequest orig;
    auto bytes = serialize(orig);
    EXPECT_TRUE(bytes.empty());  // 空请求应产生空负载
    auto r = deserialize<PingRequest>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
}

TEST(ProtocolRequestRoundtrip, UnlockRequest) {
    using namespace pwdvault::protocol;
    UnlockRequest orig{ "program-password-123" };
    auto bytes = serialize(orig);
    auto r = deserialize<UnlockRequest>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_EQ(r.value().password, orig.password);
}

TEST(ProtocolRequestRoundtrip, EnableProgramPasswordRequest) {
    using namespace pwdvault::protocol;
    EnableProgramPasswordRequest orig{ "new-program-pwd" };
    auto bytes = serialize(orig);
    auto r = deserialize<EnableProgramPasswordRequest>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_EQ(r.value().password, orig.password);
}

TEST(ProtocolRequestRoundtrip, DisableProgramPasswordRequest) {
    using namespace pwdvault::protocol;
    DisableProgramPasswordRequest orig{ "current-pwd" };
    auto bytes = serialize(orig);
    auto r = deserialize<DisableProgramPasswordRequest>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_EQ(r.value().password, orig.password);
}

TEST(ProtocolRequestRoundtrip, ChangeProgramPasswordRequest) {
    using namespace pwdvault::protocol;
    ChangeProgramPasswordRequest orig{ "old-pwd", "new-pwd" };
    auto bytes = serialize(orig);
    auto r = deserialize<ChangeProgramPasswordRequest>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_EQ(r.value().old_password, orig.old_password);
    EXPECT_EQ(r.value().new_password, orig.new_password);
}

TEST(ProtocolRequestRoundtrip, GetVaultStatusRequest) {
    using namespace pwdvault::protocol;
    GetVaultStatusRequest orig;
    auto bytes = serialize(orig);
    EXPECT_TRUE(bytes.empty());  // 空请求应产生空负载
    auto r = deserialize<GetVaultStatusRequest>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
}

TEST(ProtocolRequestRoundtrip, AddEntryRequest) {
    using namespace pwdvault::protocol;
    AddEntryRequest orig{ make_sample_entry() };
    auto bytes = serialize(orig);
    auto r = deserialize<AddEntryRequest>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    const auto& got = r.value().entry;
    EXPECT_EQ(got.id, orig.entry.id);
    EXPECT_EQ(got.website, orig.entry.website);
    EXPECT_EQ(got.username, orig.entry.username);
    EXPECT_EQ(got.password, orig.entry.password);
    EXPECT_EQ(got.note, orig.entry.note);
    EXPECT_EQ(got.created_at, orig.entry.created_at);
    EXPECT_EQ(got.updated_at, orig.entry.updated_at);
    EXPECT_EQ(got.iv, orig.entry.iv);
    EXPECT_EQ(got.tag, orig.entry.tag);
}

TEST(ProtocolRequestRoundtrip, SearchEntriesRequest) {
    using namespace pwdvault::protocol;
    SearchEntriesRequest orig;
    orig.query.text = "github";
    orig.query.fields = { "website", "username" };
    orig.query.case_sensitive = true;
    auto bytes = serialize(orig);
    auto r = deserialize<SearchEntriesRequest>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_EQ(r.value().query.text, orig.query.text);
    EXPECT_EQ(r.value().query.fields, orig.query.fields);
    EXPECT_EQ(r.value().query.case_sensitive, orig.query.case_sensitive);
}

TEST(ProtocolRequestRoundtrip, GeneratePasswordRequest) {
    using namespace pwdvault::protocol;
    GeneratePasswordRequest orig;
    orig.options.length = 24;
    orig.options.use_uppercase = true;
    orig.options.use_lowercase = true;
    orig.options.use_digits = false;
    orig.options.use_symbols = true;
    orig.options.custom_chars = "%^&*";
    orig.options.exclude_ambiguous = true;
    auto bytes = serialize(orig);
    auto r = deserialize<GeneratePasswordRequest>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_EQ(r.value().options.length, orig.options.length);
    EXPECT_EQ(r.value().options.use_uppercase, orig.options.use_uppercase);
    EXPECT_EQ(r.value().options.use_lowercase, orig.options.use_lowercase);
    EXPECT_EQ(r.value().options.use_digits, orig.options.use_digits);
    EXPECT_EQ(r.value().options.use_symbols, orig.options.use_symbols);
    EXPECT_EQ(r.value().options.custom_chars, orig.options.custom_chars);
    EXPECT_EQ(r.value().options.exclude_ambiguous, orig.options.exclude_ambiguous);
}

TEST(ProtocolRequestRoundtrip, RemoveEntryRequest) {
    using namespace pwdvault::protocol;
    RemoveEntryRequest orig{ 99 };
    auto bytes = serialize(orig);
    auto r = deserialize<RemoveEntryRequest>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_EQ(r.value().id, orig.id);
}

TEST(ProtocolRequestRoundtrip, GetEntryRequest) {
    using namespace pwdvault::protocol;
    GetEntryRequest orig{ 7 };
    auto bytes = serialize(orig);
    auto r = deserialize<GetEntryRequest>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_EQ(r.value().id, orig.id);
}

TEST(ProtocolRequestRoundtrip, EstimateStrengthRequest) {
    using namespace pwdvault::protocol;
    EstimateStrengthRequest orig{ "p@ssw0rd-very-strong" };
    auto bytes = serialize(orig);
    auto r = deserialize<EstimateStrengthRequest>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_EQ(r.value().password, orig.password);
}

TEST(ProtocolRequestRoundtrip, EmptyRequests) {
    using namespace pwdvault::protocol;
    EXPECT_TRUE(deserialize<ShutdownRequest>(serialize(ShutdownRequest{})).ok());
    EXPECT_TRUE(deserialize<LockRequest>(serialize(LockRequest{})).ok());
    EXPECT_TRUE(deserialize<ListEntriesRequest>(serialize(ListEntriesRequest{})).ok());
    EXPECT_TRUE(deserialize<GetVaultStatusRequest>(serialize(GetVaultStatusRequest{})).ok());
}

// ---------------------------------------------------------------------------
// 响应消息 round-trip
// ---------------------------------------------------------------------------

TEST(ProtocolResponseRoundtrip, PingResponse) {
    using namespace pwdvault::protocol;
    PingResponse orig{ 1700000000ULL };
    auto bytes = serialize(orig);
    auto r = deserialize<PingResponse>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_EQ(r.value().server_timestamp, orig.server_timestamp);
}

TEST(ProtocolResponseRoundtrip, UnlockResponseSuccess) {
    using namespace pwdvault::protocol;
    UnlockResponse orig{ true, "" };
    auto bytes = serialize(orig);
    auto r = deserialize<UnlockResponse>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_TRUE(r.value().success);
    EXPECT_TRUE(r.value().error_message.empty());
}

TEST(ProtocolResponseRoundtrip, UnlockResponseFailure) {
    using namespace pwdvault::protocol;
    UnlockResponse orig{ false, "wrong password" };
    auto bytes = serialize(orig);
    auto r = deserialize<UnlockResponse>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_FALSE(r.value().success);
    EXPECT_EQ(r.value().error_message, orig.error_message);
}

TEST(ProtocolResponseRoundtrip, EnableProgramPasswordResponse) {
    using namespace pwdvault::protocol;
    EnableProgramPasswordResponse orig{ true, "" };
    auto bytes = serialize(orig);
    auto r = deserialize<EnableProgramPasswordResponse>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_TRUE(r.value().success);
    EXPECT_TRUE(r.value().error_message.empty());
}

TEST(ProtocolResponseRoundtrip, EnableProgramPasswordResponseFailure) {
    using namespace pwdvault::protocol;
    EnableProgramPasswordResponse orig{ false, "already enabled" };
    auto bytes = serialize(orig);
    auto r = deserialize<EnableProgramPasswordResponse>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_FALSE(r.value().success);
    EXPECT_EQ(r.value().error_message, orig.error_message);
}

TEST(ProtocolResponseRoundtrip, DisableProgramPasswordResponse) {
    using namespace pwdvault::protocol;
    DisableProgramPasswordResponse orig{ true, "" };
    auto bytes = serialize(orig);
    auto r = deserialize<DisableProgramPasswordResponse>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_TRUE(r.value().success);
}

TEST(ProtocolResponseRoundtrip, ChangeProgramPasswordResponse) {
    using namespace pwdvault::protocol;
    ChangeProgramPasswordResponse orig{ false, "old password incorrect" };
    auto bytes = serialize(orig);
    auto r = deserialize<ChangeProgramPasswordResponse>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_FALSE(r.value().success);
    EXPECT_EQ(r.value().error_message, orig.error_message);
}

TEST(ProtocolResponseRoundtrip, GetVaultStatusResponse) {
    using namespace pwdvault::protocol;
    {
        GetVaultStatusResponse orig{ false, false };  // 明文模式，未锁定
        auto bytes = serialize(orig);
        auto r = deserialize<GetVaultStatusResponse>(bytes);
        ASSERT_TRUE(r.ok()) << r.error().what();
        EXPECT_FALSE(r.value().password_enabled);
        EXPECT_FALSE(r.value().is_locked);
    }
    {
        GetVaultStatusResponse orig{ true, true };  // 加密模式，已锁定
        auto bytes = serialize(orig);
        auto r = deserialize<GetVaultStatusResponse>(bytes);
        ASSERT_TRUE(r.ok()) << r.error().what();
        EXPECT_TRUE(r.value().password_enabled);
        EXPECT_TRUE(r.value().is_locked);
    }
    {
        GetVaultStatusResponse orig{ true, false };  // 加密模式，已解锁
        auto bytes = serialize(orig);
        auto r = deserialize<GetVaultStatusResponse>(bytes);
        ASSERT_TRUE(r.ok()) << r.error().what();
        EXPECT_TRUE(r.value().password_enabled);
        EXPECT_FALSE(r.value().is_locked);
    }
}

TEST(ProtocolResponseRoundtrip, AddEntryResponse) {
    using namespace pwdvault::protocol;
    AddEntryResponse orig{ make_sample_entry() };
    auto bytes = serialize(orig);
    auto r = deserialize<AddEntryResponse>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_EQ(r.value().entry.id, orig.entry.id);
    EXPECT_EQ(r.value().entry.website, orig.entry.website);
    EXPECT_EQ(r.value().entry.username, orig.entry.username);
    EXPECT_EQ(r.value().entry.password, orig.entry.password);
}

TEST(ProtocolResponseRoundtrip, SearchEntriesResponse) {
    using namespace pwdvault::protocol;
    SearchEntriesResponse orig;
    orig.entries.push_back(make_sample_entry());
    pwdvault::core::PasswordEntry e2 = make_sample_entry();
    e2.id = 100;
    e2.website = "gitlab.com";
    orig.entries.push_back(e2);
    auto bytes = serialize(orig);
    auto r = deserialize<SearchEntriesResponse>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    ASSERT_EQ(r.value().entries.size(), 2u);
    EXPECT_EQ(r.value().entries[0].id, orig.entries[0].id);
    EXPECT_EQ(r.value().entries[0].website, orig.entries[0].website);
    EXPECT_EQ(r.value().entries[1].id, orig.entries[1].id);
    EXPECT_EQ(r.value().entries[1].website, orig.entries[1].website);
}

TEST(ProtocolResponseRoundtrip, SearchEntriesResponseEmpty) {
    using namespace pwdvault::protocol;
    SearchEntriesResponse orig;
    auto bytes = serialize(orig);
    auto r = deserialize<SearchEntriesResponse>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_TRUE(r.value().entries.empty());
}

TEST(ProtocolResponseRoundtrip, GeneratePasswordResponse) {
    using namespace pwdvault::protocol;
    GeneratePasswordResponse orig{ "Xy9!aBcDeFgH1234" };
    auto bytes = serialize(orig);
    auto r = deserialize<GeneratePasswordResponse>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_EQ(r.value().password, orig.password);
}

TEST(ProtocolResponseRoundtrip, EstimateStrengthResponse) {
    using namespace pwdvault;
    protocol::EstimateStrengthResponse orig;
    orig.estimate.bits = 96;
    orig.estimate.level = core::StrengthLevel::Strong;
    orig.estimate.score = 3;
    orig.estimate.warnings = {"检测到 3 位顺序字符序列", "检测到键盘序列（长度 4）"};
    auto bytes = protocol::serialize(orig);
    auto r = protocol::deserialize<protocol::EstimateStrengthResponse>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_EQ(r.value().estimate.bits, orig.estimate.bits);
    EXPECT_EQ(r.value().estimate.level, orig.estimate.level);
    EXPECT_EQ(r.value().estimate.score, orig.estimate.score);
    EXPECT_EQ(r.value().estimate.warnings, orig.estimate.warnings);
}

TEST(ProtocolTypeRoundtrip, StrengthEstimate) {
    using namespace pwdvault;
    core::StrengthEstimate orig;
    orig.bits = 0;
    orig.level = core::StrengthLevel::VeryWeak;
    orig.score = 0;
    // 空 warnings
    auto bytes = protocol::serialize(orig);
    auto r = protocol::deserialize<core::StrengthEstimate>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_EQ(r.value().bits, 0);
    EXPECT_EQ(r.value().level, core::StrengthLevel::VeryWeak);
    EXPECT_EQ(r.value().score, 0);
    EXPECT_TRUE(r.value().warnings.empty());
}

TEST(ProtocolTypeRoundtrip, StrengthEstimateAllLevels) {
    using namespace pwdvault;
    for (int lvl = 0; lvl <= 4; ++lvl) {
        core::StrengthEstimate orig;
        orig.bits = 50 + lvl * 10;
        orig.level = static_cast<core::StrengthLevel>(lvl);
        orig.score = lvl;
        orig.warnings = {"warning " + std::to_string(lvl)};
        auto bytes = protocol::serialize(orig);
        auto r = protocol::deserialize<core::StrengthEstimate>(bytes);
        ASSERT_TRUE(r.ok()) << r.error().what() << " at level=" << lvl;
        EXPECT_EQ(r.value().bits, orig.bits);
        EXPECT_EQ(r.value().level, orig.level);
        EXPECT_EQ(r.value().score, orig.score);
        EXPECT_EQ(r.value().warnings, orig.warnings);
    }
}

TEST(ProtocolResponseRoundtrip, EmptyResponses) {
    using namespace pwdvault::protocol;
    EXPECT_TRUE(deserialize<ShutdownResponse>(serialize(ShutdownResponse{})).ok());
    EXPECT_TRUE(deserialize<LockResponse>(serialize(LockResponse{})).ok());
    EXPECT_TRUE(deserialize<RemoveEntryResponse>(serialize(RemoveEntryResponse{})).ok());
}

TEST(ProtocolResponseRoundtrip, ErrorResponse) {
    using namespace pwdvault::protocol;
    ErrorResponse orig{ pwdvault::core::ErrorCode::Unauthorized, "invalid program password" };
    auto bytes = serialize(orig);
    auto r = deserialize<ErrorResponse>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_EQ(r.value().code, orig.code);
    EXPECT_EQ(r.value().message, orig.message);
}

// ---------------------------------------------------------------------------
// 消息帧 pack_message + parse_header
// ---------------------------------------------------------------------------

TEST(ProtocolFrame, PackAndParseHeader) {
    using namespace pwdvault::protocol;
    const UnlockRequest req{ "program-pwd" };
    auto payload = serialize(req);
    const uint32_t request_id = 0xCAFEBABEu;
    auto frame = pack_message(CommandId::Unlock, request_id, payload);

    // 帧 = 16 字节 header + payload
    ASSERT_EQ(frame.size(), sizeof(MessageHeader) + payload.size());

    auto ph = parse_header(frame);
    ASSERT_TRUE(ph.ok()) << ph.error().what();
    const auto& [hdr, offset] = ph.value();
    EXPECT_EQ(offset, sizeof(MessageHeader));
    EXPECT_EQ(hdr.magic, kMagic);
    EXPECT_EQ(hdr.version, kProtocolVersion);
    EXPECT_EQ(hdr.command, CommandId::Unlock);
    EXPECT_EQ(hdr.request_id, request_id);
    EXPECT_EQ(hdr.payload_size, payload.size());

    // 从 offset 开始取出 payload，应能反序列化回原请求
    pwdvault::core::ByteSpan payload_span(frame.data() + offset, hdr.payload_size);
    auto r = deserialize<UnlockRequest>(payload_span);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_EQ(r.value().password, req.password);
}

TEST(ProtocolFrame, PackMessageWithEmptyPayload) {
    using namespace pwdvault::protocol;
    auto frame = pack_message(CommandId::Ping, 1u, {});
    ASSERT_EQ(frame.size(), sizeof(MessageHeader));
    auto ph = parse_header(frame);
    ASSERT_TRUE(ph.ok()) << ph.error().what();
    EXPECT_EQ(ph.value().first.command, CommandId::Ping);
    EXPECT_EQ(ph.value().first.request_id, 1u);
    EXPECT_EQ(ph.value().first.payload_size, 0u);
}

TEST(ProtocolFrame, ParseHeaderWithInsufficientData) {
    using namespace pwdvault::protocol;
    // 仅 8 字节（不足 16 字节 header）
    pwdvault::core::ByteVec short_data(8, std::byte{0});
    auto ph = parse_header(short_data);
    ASSERT_FALSE(ph.ok());
    EXPECT_EQ(ph.error().code, pwdvault::core::ErrorCode::IpcError);
}

TEST(ProtocolFrame, ParseHeaderWithZeroBytes) {
    using namespace pwdvault::protocol;
    pwdvault::core::ByteVec empty;
    auto ph = parse_header(empty);
    ASSERT_FALSE(ph.ok());
    EXPECT_EQ(ph.error().code, pwdvault::core::ErrorCode::IpcError);
}

TEST(ProtocolFrame, ParseHeaderMagicMismatch) {
    using namespace pwdvault::protocol;
    // 构造 16 字节数据，但 magic 错误
    MessageHeader h;
    h.magic = 0xDeadBeef;  // 错误 magic
    h.version = kProtocolVersion;
    h.command = CommandId::Ping;
    h.request_id = 1u;
    h.payload_size = 0u;
    pwdvault::core::ByteVec frame(sizeof(h));
    std::memcpy(frame.data(), &h, sizeof(h));
    auto ph = parse_header(frame);
    ASSERT_FALSE(ph.ok());
    EXPECT_EQ(ph.error().code, pwdvault::core::ErrorCode::IpcError);
}

TEST(ProtocolFrame, FullRoundTripMultipleCommands) {
    using namespace pwdvault::protocol;
    // 模拟 UI 端发送 AddEntryRequest，service 端接收并解析
    AddEntryRequest req{ make_sample_entry() };
    auto payload = serialize(req);
    auto frame = pack_message(CommandId::AddEntry, 123u, payload);

    auto ph = parse_header(frame);
    ASSERT_TRUE(ph.ok()) << ph.error().what();
    EXPECT_EQ(ph.value().first.command, CommandId::AddEntry);
    EXPECT_EQ(ph.value().first.request_id, 123u);
    ASSERT_EQ(ph.value().first.payload_size, payload.size());

    pwdvault::core::ByteSpan payload_span(
        frame.data() + ph.value().second, ph.value().first.payload_size);
    auto r = deserialize<AddEntryRequest>(payload_span);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_EQ(r.value().entry.id, req.entry.id);
    EXPECT_EQ(r.value().entry.website, req.entry.website);
}

// ---------------------------------------------------------------------------
// 反序列化错误用例
// ---------------------------------------------------------------------------

TEST(ProtocolDeserializeError, TruncatedString) {
    using namespace pwdvault::protocol;
    // 仅长度前缀（4 字节），无对应内容
    pwdvault::core::ByteVec bad;
    bad.resize(4);
    const uint32_t len = 100;  // 声称 100 字节
    std::memcpy(bad.data(), &len, sizeof(len));
    auto r = deserialize<std::string>(bad);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, pwdvault::core::ErrorCode::InvalidArgument);
}

TEST(ProtocolDeserializeError, TruncatedUint32) {
    using namespace pwdvault::protocol;
    pwdvault::core::ByteVec bad(2, std::byte{0});  // 仅 2 字节
    auto r = deserialize<uint32_t>(bad);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, pwdvault::core::ErrorCode::InvalidArgument);
}

TEST(ProtocolDeserializeError, TruncatedPasswordEntry) {
    using namespace pwdvault::protocol;
    // 仅 4 字节，远不够 PasswordEntry 的最小序列化长度
    pwdvault::core::ByteVec bad(4, std::byte{0});
    auto r = deserialize<pwdvault::core::PasswordEntry>(bad);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, pwdvault::core::ErrorCode::InvalidArgument);
}

// ===========================================================================
// 生成器历史记录相关消息 round-trip
// ===========================================================================

TEST(ProtocolCoreRoundtrip, GeneratedPasswordRecord) {
    using namespace pwdvault::protocol;
    pwdvault::core::GeneratedPasswordRecord rec;
    rec.id = 123;
    rec.password = "P@ssw0rd-Generated!";
    rec.length = 20;
    rec.created_at = 1700000000;
    rec.iv = make_bytes("012345678901");
    rec.tag = make_bytes("0123456789012345");

    auto bytes = serialize(rec);
    auto r = deserialize<pwdvault::core::GeneratedPasswordRecord>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_EQ(r.value().id, rec.id);
    EXPECT_EQ(r.value().password, rec.password);
    EXPECT_EQ(r.value().length, rec.length);
    EXPECT_EQ(r.value().created_at, rec.created_at);
    EXPECT_EQ(r.value().iv, rec.iv);
    EXPECT_EQ(r.value().tag, rec.tag);
}

TEST(ProtocolCoreRoundtrip, GeneratedPasswordRecordPlaintextMode) {
    // 明文模式：iv / tag 为空，password 字段直接为明文
    using namespace pwdvault::protocol;
    pwdvault::core::GeneratedPasswordRecord rec;
    rec.id = 7;
    rec.password = "plaintext-pwd";
    rec.length = 13;
    rec.created_at = 1711111111;
    // iv / tag 留空

    auto bytes = serialize(rec);
    auto r = deserialize<pwdvault::core::GeneratedPasswordRecord>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_EQ(r.value().id, rec.id);
    EXPECT_EQ(r.value().password, rec.password);
    EXPECT_EQ(r.value().length, rec.length);
    EXPECT_EQ(r.value().created_at, rec.created_at);
    EXPECT_TRUE(r.value().iv.empty());
    EXPECT_TRUE(r.value().tag.empty());
}

TEST(ProtocolRequestRoundtrip, RemoveGeneratedRecordRequest) {
    using namespace pwdvault::protocol;
    RemoveGeneratedRecordRequest req;
    req.id = 999;
    auto bytes = serialize(req);
    auto r = deserialize<RemoveGeneratedRecordRequest>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_EQ(r.value().id, req.id);
}

TEST(ProtocolRequestRoundtrip, SetGeneratorLimitRequest) {
    using namespace pwdvault::protocol;
    SetGeneratorLimitRequest req;
    req.limit = 20;
    auto bytes = serialize(req);
    auto r = deserialize<SetGeneratorLimitRequest>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_EQ(r.value().limit, req.limit);
}

TEST(ProtocolRequestRoundtrip, SetGeneratorLimitRequestUnlimited) {
    // 0 = 无限制
    using namespace pwdvault::protocol;
    SetGeneratorLimitRequest req;
    req.limit = 0;
    auto bytes = serialize(req);
    auto r = deserialize<SetGeneratorLimitRequest>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_EQ(r.value().limit, 0);
}

TEST(ProtocolResponseRoundtrip, ListGeneratedRecordsResponse) {
    using namespace pwdvault::protocol;
    ListGeneratedRecordsResponse resp;
    pwdvault::core::GeneratedPasswordRecord r1;
    r1.id = 1;
    r1.password = "pwd1";
    r1.length = 4;
    r1.created_at = 100;
    pwdvault::core::GeneratedPasswordRecord r2;
    r2.id = 2;
    r2.password = "pwd2-longer";
    r2.length = 11;
    r2.created_at = 200;
    r2.iv = make_bytes("iv-12-bytes!");
    r2.tag = make_bytes("tag-16-bytes!!!");
    resp.records.push_back(r1);
    resp.records.push_back(r2);

    auto bytes = serialize(resp);
    auto r = deserialize<ListGeneratedRecordsResponse>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    ASSERT_EQ(r.value().records.size(), 2u);
    EXPECT_EQ(r.value().records[0].id, 1);
    EXPECT_EQ(r.value().records[0].password, "pwd1");
    EXPECT_EQ(r.value().records[1].id, 2);
    EXPECT_EQ(r.value().records[1].password, "pwd2-longer");
    EXPECT_EQ(r.value().records[1].iv, r2.iv);
    EXPECT_EQ(r.value().records[1].tag, r2.tag);
}

TEST(ProtocolResponseRoundtrip, ListGeneratedRecordsResponseEmpty) {
    using namespace pwdvault::protocol;
    ListGeneratedRecordsResponse resp;  // 空
    auto bytes = serialize(resp);
    auto r = deserialize<ListGeneratedRecordsResponse>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_TRUE(r.value().records.empty());
}

TEST(ProtocolResponseRoundtrip, GetGeneratorSettingsResponse) {
    using namespace pwdvault::protocol;
    GetGeneratorSettingsResponse resp;
    resp.history_limit = 50;
    auto bytes = serialize(resp);
    auto r = deserialize<GetGeneratorSettingsResponse>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_EQ(r.value().history_limit, 50);
}

TEST(ProtocolResponseRoundtrip, SetGeneratorLimitResponseSuccess) {
    using namespace pwdvault::protocol;
    SetGeneratorLimitResponse resp;
    resp.success = true;
    auto bytes = serialize(resp);
    auto r = deserialize<SetGeneratorLimitResponse>(bytes);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_TRUE(r.value().success);
}

TEST(ProtocolDeserializeError, TruncatedGeneratedPasswordRecord) {
    using namespace pwdvault::protocol;
    pwdvault::core::ByteVec bad(4, std::byte{0});
    auto r = deserialize<pwdvault::core::GeneratedPasswordRecord>(bad);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, pwdvault::core::ErrorCode::InvalidArgument);
}

// ---------------------------------------------------------------------------
// 生成器相关空负载结构 round-trip
// ---------------------------------------------------------------------------

TEST(ProtocolRoundtrip, GeneratedEmptyPayloads) {
    using namespace pwdvault::protocol;
    // 5 个空负载结构：serialize 返回空 ByteVec，deserialize 空输入返回 Ok
    {
        ListGeneratedRecordsRequest req;
        auto bytes = serialize(req);
        EXPECT_TRUE(bytes.empty());  // 空请求应产生空负载
        auto r = deserialize<ListGeneratedRecordsRequest>(bytes);
        ASSERT_TRUE(r.ok()) << r.error().what();
    }
    {
        ClearGeneratedRecordsRequest req;
        auto bytes = serialize(req);
        EXPECT_TRUE(bytes.empty());
        auto r = deserialize<ClearGeneratedRecordsRequest>(bytes);
        ASSERT_TRUE(r.ok()) << r.error().what();
    }
    {
        GetGeneratorSettingsRequest req;
        auto bytes = serialize(req);
        EXPECT_TRUE(bytes.empty());
        auto r = deserialize<GetGeneratorSettingsRequest>(bytes);
        ASSERT_TRUE(r.ok()) << r.error().what();
    }
    {
        RemoveGeneratedRecordResponse resp;
        auto bytes = serialize(resp);
        EXPECT_TRUE(bytes.empty());  // 空响应应产生空负载
        auto r = deserialize<RemoveGeneratedRecordResponse>(bytes);
        ASSERT_TRUE(r.ok()) << r.error().what();
    }
    {
        ClearGeneratedRecordsResponse resp;
        auto bytes = serialize(resp);
        EXPECT_TRUE(bytes.empty());
        auto r = deserialize<ClearGeneratedRecordsResponse>(bytes);
        ASSERT_TRUE(r.ok()) << r.error().what();
    }
}
