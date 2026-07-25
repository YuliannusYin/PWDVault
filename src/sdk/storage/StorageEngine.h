// coding: utf-8
// =============================================================================
// StorageEngine.h
//
// PwdVault 存储引擎的 SQLite 持久化实现。
//
// 设计要点：
//   - 通过 `IStorageEngine` 抽象接口被服务进程调用，本类为生产实现。
//   - 使用 SQLite3 prepared statement + 参数绑定，杜绝 SQL 注入。
//   - RAII 管理 sqlite3 连接与 sqlite3_stmt 句柄：异常/作用域退出时自动释放。
//   - 线程安全：每个实例持有一个 std::mutex，串行化所有公开方法。
//   - PasswordEntry 中的 password / iv / tag 字段视为已加密的二进制数据，
//     本引擎只负责 BLOB 存取，不做任何加解密。
//
// Schema（init 时创建）：
//   CREATE TABLE IF NOT EXISTS passwords (
//       id INTEGER PRIMARY KEY AUTOINCREMENT,
//       website TEXT NOT NULL,
//       username TEXT NOT NULL,
//       password BLOB NOT NULL,
//       note TEXT,
//       iv BLOB NOT NULL,
//       tag BLOB NOT NULL,
//       created_at INTEGER NOT NULL,
//       updated_at INTEGER NOT NULL
//   );
//   CREATE INDEX IF NOT EXISTS idx_passwords_website  ON passwords(website);
//   CREATE INDEX IF NOT EXISTS idx_passwords_username ON passwords(username);
// =============================================================================
#pragma once

#include <filesystem>
#include <memory>
#include <mutex>

#include "IStorageEngine.h"

// 前置声明 sqlite3，避免在头文件中暴露 SQLite 头给外部包含方。
struct sqlite3;
struct sqlite3_stmt;

namespace pwdvault::storage {

/// SQLite 持久化存储引擎。
class StorageEngine : public core::IStorageEngine {
public:
    /// 打开（必要时创建）SQLite 数据库并初始化 schema。
    /// \param db_path 数据库文件路径；传入 ":memory:" 可使用纯内存数据库（测试用）
    /// \throws 无异常——构造失败通过内部状态记录，后续方法返回 StorageError。
    explicit StorageEngine(const std::filesystem::path& db_path);

    /// 关闭 SQLite 连接（由 RAII 自动完成）。
    ~StorageEngine() override;

    // 禁用拷贝：sqlite3 连接不可拷贝。
    StorageEngine(const StorageEngine&) = delete;
    StorageEngine& operator=(const StorageEngine&) = delete;

    core::Result<core::PasswordEntry> add_entry(const core::PasswordEntry& entry) override;
    core::Result<core::PasswordEntry> update_entry(const core::PasswordEntry& entry) override;
    core::Error remove_entry(int64_t id) override;
    core::Result<core::PasswordEntry> get_entry(int64_t id) override;
    core::Result<std::vector<core::PasswordEntry>> search_entries(
        const core::SearchQuery& query) override;
    core::Result<std::vector<core::PasswordEntry>> list_entries() override;

    core::Error begin_transaction() override;
    core::Error commit_transaction() override;
    core::Error rollback_transaction() override;

    core::Result<core::GeneratedPasswordRecord> add_generated_record(
        const core::GeneratedPasswordRecord& record) override;
    core::Result<core::GeneratedPasswordRecord> update_generated_record(
        const core::GeneratedPasswordRecord& record) override;
    core::Result<std::vector<core::GeneratedPasswordRecord>> list_generated_records() override;
    core::Error remove_generated_record(int64_t id) override;
    core::Error clear_generated_records() override;
    core::Result<std::string> get_setting(const std::string& key) override;
    core::Error set_setting(const std::string& key, const std::string& value) override;

private:
    /// 自定义 deleter：封装 sqlite3_close_v2。
    struct SqliteDbDeleter {
        void operator()(sqlite3* db) const;
    };
    using DbHandle = std::unique_ptr<sqlite3, SqliteDbDeleter>;

    /// 自定义 deleter：封装 sqlite3_finalize。
    struct SqliteStmtDeleter {
        void operator()(sqlite3_stmt* stmt) const;
    };
    using StmtHandle = std::unique_ptr<sqlite3_stmt, SqliteStmtDeleter>;

    DbHandle db_;
    std::mutex mutex_;

    /// 执行一条无参数 SQL（如 BEGIN/COMMIT/CREATE TABLE）。
    core::Error exec_sql(const char* sql);

    /// 初始化 schema（建表 + 建索引），由构造函数调用。
    core::Error init_schema();

    /// 从当前 step 后的结果行读取一条 PasswordEntry。
    /// 调用方需保证 stmt 已 step 到 SQLITE_ROW。
    static core::PasswordEntry read_row(sqlite3_stmt* stmt);

    /// 从当前 step 后的结果行读取一条 GeneratedPasswordRecord。
    static core::GeneratedPasswordRecord read_generated_row(sqlite3_stmt* stmt);

    /// 将一个 BLOB 列读出为 ByteVec。
    static core::ByteVec read_blob_column(sqlite3_stmt* stmt, int col);

    /// 当前 Unix 时间戳（秒）。
    static int64_t now_seconds();
};

}  // namespace pwdvault::storage
