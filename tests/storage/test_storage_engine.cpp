// coding: utf-8
// =============================================================================
// test_storage_engine.cpp
//
// 存储引擎单元测试。覆盖：
//   - InMemoryStorageEngine：CRUD、搜索、事务、list（无文件系统依赖）
//   - StorageEngine（真实 SQLite，:memory:）：BLOB 字段往返、基础 CRUD、事务
//
// 测试框架：GoogleTest。
// =============================================================================
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "InMemoryStorageEngine.h"
#include "StorageEngine.h"
#include "Types.h"

namespace {

/// 构造一个测试用 PasswordEntry（id=0 表示新条目）。
pwdvault::core::PasswordEntry make_entry(const std::string& website,
                                         const std::string& username,
                                         const std::string& note = "") {
    pwdvault::core::PasswordEntry e;
    e.website = website;
    e.username = username;
    e.password = "cipher-blob-bytes";  // 模拟已加密的密文（实际由 ICryptoEngine 产出）
    e.note = note;
    // 模拟 12 字节 IV 与 16 字节 GCM tag
    e.iv.assign(12, pwdvault::core::ByteVec::value_type{0xAB});
    e.tag.assign(16, pwdvault::core::ByteVec::value_type{0xCD});
    return e;
}

}  // namespace

// =============================================================================
// InMemoryStorageEngine 测试
// =============================================================================

TEST(InMemoryStorageEngineTest, AddEntryAssignsIncrementingId) {
    pwdvault::storage::InMemoryStorageEngine engine;
    auto e1 = make_entry("github.com", "alice");
    auto e2 = make_entry("gitlab.com", "bob");

    auto r1 = engine.add_entry(e1);
    ASSERT_TRUE(r1.ok()) << r1.error().what();
    auto r2 = engine.add_entry(e2);
    ASSERT_TRUE(r2.ok()) << r2.error().what();

    EXPECT_NE(r1.value().id, 0);
    EXPECT_NE(r2.value().id, 0);
    EXPECT_LT(r1.value().id, r2.value().id);
    EXPECT_GT(r1.value().created_at, 0);
    EXPECT_EQ(r1.value().created_at, r1.value().updated_at);
}

TEST(InMemoryStorageEngineTest, GetEntryReturnsInsertedAndNotFound) {
    pwdvault::storage::InMemoryStorageEngine engine;
    auto e = make_entry("github.com", "alice", "personal account");

    auto added = engine.add_entry(e);
    ASSERT_TRUE(added.ok());

    auto got = engine.get_entry(added.value().id);
    ASSERT_TRUE(got.ok()) << got.error().what();
    EXPECT_EQ(got.value().id, added.value().id);
    EXPECT_EQ(got.value().website, "github.com");
    EXPECT_EQ(got.value().username, "alice");
    EXPECT_EQ(got.value().note, "personal account");
    EXPECT_EQ(got.value().password, "cipher-blob-bytes");
    EXPECT_EQ(got.value().iv.size(), 12u);
    EXPECT_EQ(got.value().tag.size(), 16u);

    auto miss = engine.get_entry(99999);
    ASSERT_FALSE(miss.ok());
    EXPECT_EQ(miss.error().code, pwdvault::core::ErrorCode::NotFound);
}

TEST(InMemoryStorageEngineTest, UpdateEntryModifiesFieldsAndBumpsTimestamp) {
    pwdvault::storage::InMemoryStorageEngine engine;
    auto added = engine.add_entry(make_entry("github.com", "alice"));
    ASSERT_TRUE(added.ok());
    const int64_t created_at = added.value().created_at;

    // 等待 1 秒以确保 updated_at > created_at（时间戳为秒分辨率）。
    std::this_thread::sleep_for(std::chrono::seconds(1));

    auto to_update = added.value();
    to_update.username = "alice_new";
    to_update.note = "updated note";
    auto updated = engine.update_entry(to_update);
    ASSERT_TRUE(updated.ok()) << updated.error().what();
    EXPECT_EQ(updated.value().username, "alice_new");
    EXPECT_EQ(updated.value().note, "updated note");
    EXPECT_EQ(updated.value().created_at, created_at);
    EXPECT_GT(updated.value().updated_at, created_at);

    auto got = engine.get_entry(added.value().id);
    ASSERT_TRUE(got.ok());
    EXPECT_EQ(got.value().username, "alice_new");
    EXPECT_EQ(got.value().note, "updated note");
    EXPECT_EQ(got.value().created_at, created_at);
    EXPECT_GT(got.value().updated_at, created_at);
}

TEST(InMemoryStorageEngineTest, UpdateEntryNotFoundReturnsError) {
    pwdvault::storage::InMemoryStorageEngine engine;
    auto e = make_entry("github.com", "alice");
    e.id = 42;
    auto r = engine.update_entry(e);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, pwdvault::core::ErrorCode::NotFound);
}

TEST(InMemoryStorageEngineTest, RemoveEntryAndSubsequentGetFails) {
    pwdvault::storage::InMemoryStorageEngine engine;
    auto added = engine.add_entry(make_entry("github.com", "alice"));
    ASSERT_TRUE(added.ok());

    auto rm = engine.remove_entry(added.value().id);
    EXPECT_TRUE(rm.ok()) << rm.what();

    auto miss = engine.get_entry(added.value().id);
    ASSERT_FALSE(miss.ok());
    EXPECT_EQ(miss.error().code, pwdvault::core::ErrorCode::NotFound);
}

TEST(InMemoryStorageEngineTest, RemoveEntryNotFoundReturnsError) {
    pwdvault::storage::InMemoryStorageEngine engine;
    auto rm = engine.remove_entry(99999);
    ASSERT_FALSE(rm.ok());
    EXPECT_EQ(rm.error().code, pwdvault::core::ErrorCode::NotFound);
}

TEST(InMemoryStorageEngineTest, ListEntriesReturnsAll) {
    pwdvault::storage::InMemoryStorageEngine engine;
    ASSERT_TRUE(engine.add_entry(make_entry("a.com", "u1")).ok());
    ASSERT_TRUE(engine.add_entry(make_entry("b.com", "u2")).ok());
    ASSERT_TRUE(engine.add_entry(make_entry("c.com", "u3")).ok());

    auto list = engine.list_entries();
    ASSERT_TRUE(list.ok());
    EXPECT_EQ(list.value().size(), 3u);
}

TEST(InMemoryStorageEngineTest, SearchByWebsite) {
    pwdvault::storage::InMemoryStorageEngine engine;
    ASSERT_TRUE(engine.add_entry(make_entry("github.com", "alice")).ok());
    ASSERT_TRUE(engine.add_entry(make_entry("gitlab.com", "bob")).ok());
    ASSERT_TRUE(engine.add_entry(make_entry("example.com", "carol")).ok());

    pwdvault::core::SearchQuery q;
    q.text = "git";
    q.fields = {"website"};
    q.case_sensitive = false;
    auto r = engine.search_entries(q);
    ASSERT_TRUE(r.ok()) << r.error().what();
    ASSERT_EQ(r.value().size(), 2u);
    for (const auto& e : r.value()) {
        EXPECT_NE(e.website.find("git"), std::string::npos);
    }
}

TEST(InMemoryStorageEngineTest, SearchByUsername) {
    pwdvault::storage::InMemoryStorageEngine engine;
    ASSERT_TRUE(engine.add_entry(make_entry("a.com", "alice")).ok());
    ASSERT_TRUE(engine.add_entry(make_entry("b.com", "bob")).ok());
    ASSERT_TRUE(engine.add_entry(make_entry("c.com", "alice_smith")).ok());

    pwdvault::core::SearchQuery q;
    q.text = "alice";
    q.fields = {"username"};
    q.case_sensitive = false;
    auto r = engine.search_entries(q);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().size(), 2u);
}

TEST(InMemoryStorageEngineTest, SearchByNote) {
    pwdvault::storage::InMemoryStorageEngine engine;
    ASSERT_TRUE(engine.add_entry(make_entry("a.com", "u1", "work account")).ok());
    ASSERT_TRUE(engine.add_entry(make_entry("b.com", "u2", "personal")).ok());
    ASSERT_TRUE(engine.add_entry(make_entry("c.com", "u3", "WORK email")).ok());

    pwdvault::core::SearchQuery q;
    q.text = "work";
    q.fields = {"note"};
    q.case_sensitive = false;
    auto r = engine.search_entries(q);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().size(), 2u);  // "work account" + "WORK email"
}

TEST(InMemoryStorageEngineTest, SearchCaseSensitive) {
    pwdvault::storage::InMemoryStorageEngine engine;
    ASSERT_TRUE(engine.add_entry(make_entry("github.com", "Alice")).ok());
    ASSERT_TRUE(engine.add_entry(make_entry("example.com", "alice")).ok());

    pwdvault::core::SearchQuery q_cs;
    q_cs.text = "Alice";
    q_cs.fields = {"username"};
    q_cs.case_sensitive = true;
    auto r_cs = engine.search_entries(q_cs);
    ASSERT_TRUE(r_cs.ok());
    ASSERT_EQ(r_cs.value().size(), 1u);
    EXPECT_EQ(r_cs.value()[0].username, "Alice");

    pwdvault::core::SearchQuery q_ci;
    q_ci.text = "Alice";
    q_ci.fields = {"username"};
    q_ci.case_sensitive = false;
    auto r_ci = engine.search_entries(q_ci);
    ASSERT_TRUE(r_ci.ok());
    EXPECT_EQ(r_ci.value().size(), 2u);
}

TEST(InMemoryStorageEngineTest, SearchEmptyFieldsSearchesAll) {
    pwdvault::storage::InMemoryStorageEngine engine;
    ASSERT_TRUE(engine.add_entry(make_entry("github.com", "alice")).ok());
    ASSERT_TRUE(engine.add_entry(make_entry("example.com", "bob", "secret note")).ok());

    pwdvault::core::SearchQuery q;
    q.text = "github";  // 只匹配 website
    q.fields = {};       // 空表示搜索全部字段
    q.case_sensitive = false;
    auto r = engine.search_entries(q);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value().size(), 1u);
}

TEST(InMemoryStorageEngineTest, TransactionRollbackDiscardsAdds) {
    pwdvault::storage::InMemoryStorageEngine engine;
    ASSERT_TRUE(engine.begin_transaction().ok());
    ASSERT_TRUE(engine.add_entry(make_entry("a.com", "u1")).ok());
    ASSERT_TRUE(engine.add_entry(make_entry("b.com", "u2")).ok());
    ASSERT_TRUE(engine.rollback_transaction().ok());

    auto list = engine.list_entries();
    ASSERT_TRUE(list.ok());
    EXPECT_TRUE(list.value().empty());
}

TEST(InMemoryStorageEngineTest, TransactionCommitPersistsAdds) {
    pwdvault::storage::InMemoryStorageEngine engine;
    ASSERT_TRUE(engine.begin_transaction().ok());
    ASSERT_TRUE(engine.add_entry(make_entry("a.com", "u1")).ok());
    ASSERT_TRUE(engine.commit_transaction().ok());

    auto list = engine.list_entries();
    ASSERT_TRUE(list.ok());
    EXPECT_EQ(list.value().size(), 1u);
}

TEST(InMemoryStorageEngineTest, TransactionRollbackRestoresIdCounter) {
    // 回滚后再次 add_entry，新 id 不应与已回滚的条目 id 冲突，
    // 也不应跳过号段（确保 next_id_ 也被恢复）。
    pwdvault::storage::InMemoryStorageEngine engine;
    auto first = engine.add_entry(make_entry("a.com", "u1"));
    ASSERT_TRUE(first.ok());
    const int64_t first_id = first.value().id;

    ASSERT_TRUE(engine.begin_transaction().ok());
    auto txn_added = engine.add_entry(make_entry("b.com", "u2"));
    ASSERT_TRUE(txn_added.ok());
    ASSERT_TRUE(engine.rollback_transaction().ok());

    auto after = engine.add_entry(make_entry("c.com", "u3"));
    ASSERT_TRUE(after.ok());
    EXPECT_GT(after.value().id, first_id);

    auto list = engine.list_entries();
    ASSERT_TRUE(list.ok());
    EXPECT_EQ(list.value().size(), 2u);
}

// =============================================================================
// StorageEngine（真实 SQLite，:memory:）测试
// =============================================================================

TEST(StorageEngineSqliteTest, AddAndGetRoundtripsBlobFields) {
    pwdvault::storage::StorageEngine engine(std::filesystem::path{":memory:"});

    auto e = make_entry("github.com", "alice", "personal");
    // 故意填充含 0 字节的 BLOB，验证二进制安全性。
    e.password = std::string("enc\0ry\0pted", 11);
    e.iv = pwdvault::core::ByteVec(12, pwdvault::core::ByteVec::value_type{0x01});
    e.tag = pwdvault::core::ByteVec(16, pwdvault::core::ByteVec::value_type{0x02});

    auto added = engine.add_entry(e);
    ASSERT_TRUE(added.ok()) << added.error().what();
    EXPECT_NE(added.value().id, 0);

    auto got = engine.get_entry(added.value().id);
    ASSERT_TRUE(got.ok()) << got.error().what();
    EXPECT_EQ(got.value().website, "github.com");
    EXPECT_EQ(got.value().username, "alice");
    EXPECT_EQ(got.value().note, "personal");
    // BLOB 字段必须按字节完整往返（含嵌入的 \0）。
    EXPECT_EQ(got.value().password, e.password);
    EXPECT_EQ(got.value().iv, e.iv);
    EXPECT_EQ(got.value().tag, e.tag);
    EXPECT_EQ(got.value().iv.size(), 12u);
    EXPECT_EQ(got.value().tag.size(), 16u);
    EXPECT_GT(got.value().created_at, 0);
    EXPECT_EQ(got.value().created_at, got.value().updated_at);
}

TEST(StorageEngineSqliteTest, UpdateAndRemoveOnSqlite) {
    pwdvault::storage::StorageEngine engine(std::filesystem::path{":memory:"});

    auto added = engine.add_entry(make_entry("github.com", "alice"));
    ASSERT_TRUE(added.ok());
    const int64_t id = added.value().id;
    const int64_t created_at = added.value().created_at;

    std::this_thread::sleep_for(std::chrono::seconds(1));

    auto to_update = added.value();
    to_update.note = "after update";
    auto updated = engine.update_entry(to_update);
    ASSERT_TRUE(updated.ok()) << updated.error().what();
    EXPECT_EQ(updated.value().created_at, created_at);
    EXPECT_GT(updated.value().updated_at, created_at);

    auto got = engine.get_entry(id);
    ASSERT_TRUE(got.ok());
    EXPECT_EQ(got.value().note, "after update");

    ASSERT_TRUE(engine.remove_entry(id).ok());
    auto miss = engine.get_entry(id);
    ASSERT_FALSE(miss.ok());
    EXPECT_EQ(miss.error().code, pwdvault::core::ErrorCode::NotFound);
}

TEST(StorageEngineSqliteTest, SqliteTransactionRollbackAndCommit) {
    pwdvault::storage::StorageEngine engine(std::filesystem::path{":memory:"});

    ASSERT_TRUE(engine.begin_transaction().ok());
    ASSERT_TRUE(engine.add_entry(make_entry("a.com", "u1")).ok());
    ASSERT_TRUE(engine.rollback_transaction().ok());

    auto list = engine.list_entries();
    ASSERT_TRUE(list.ok());
    EXPECT_TRUE(list.value().empty());

    ASSERT_TRUE(engine.begin_transaction().ok());
    ASSERT_TRUE(engine.add_entry(make_entry("b.com", "u2")).ok());
    ASSERT_TRUE(engine.commit_transaction().ok());

    auto list2 = engine.list_entries();
    ASSERT_TRUE(list2.ok());
    EXPECT_EQ(list2.value().size(), 1u);
}
