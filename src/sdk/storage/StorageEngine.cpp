// coding: utf-8
// =============================================================================
// StorageEngine.cpp
//
// StorageEngine 的实现细节。所有公开方法均通过 mutex 串行化，确保线程安全。
// SQLite 资源由 RAII 句柄管理；prepared statement 在每次调用时临时构造、
// 作用域结束时自动 finalize。
// =============================================================================
#include "StorageEngine.h"

#include <chrono>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include <sqlite3.h>

namespace pwdvault::storage {

namespace {

/// 构造一个 StorageError，附带 SQLite 错误消息。
core::Error make_storage_error(const std::string& context, sqlite3* db) {
    std::string msg = context;
    if (db != nullptr) {
        msg += ": ";
        msg += sqlite3_errmsg(db);
    }
    return core::Error(core::ErrorCode::StorageError, std::move(msg));
}

/// 将 SQLite 的 rc 转换为对应的 Error。SQLITE_OK 与 SQLITE_DONE 视为成功。
core::Error rc_to_error(int rc, const std::string& context, sqlite3* db) {
    if (rc == SQLITE_OK || rc == SQLITE_DONE || rc == SQLITE_ROW) {
        return core::Error{};
    }
    if (rc == SQLITE_CONSTRAINT) {
        return core::Error(core::ErrorCode::AlreadyExists,
                           context + ": constraint violation");
    }
    return make_storage_error(context, db);
}

/// 转义 LIKE 通配符（%、_、\），返回 %pattern% 形式的子串匹配串。
std::string escape_like_pattern(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 4);
    for (char c : text) {
        if (c == '%' || c == '_' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out.insert(out.begin(), '%');
    out.push_back('%');
    return out;
}

/// 转义 GLOB 通配符（*、?、[），返回 *pattern* 形式的子串匹配串。
/// GLOB 在 SQLite 中始终区分大小写，用于 case_sensitive=true 分支。
std::string escape_glob_pattern(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 4);
    for (char c : text) {
        if (c == '*' || c == '?' || c == '[') {
            out.push_back('[');
            out.push_back(c);
            out.push_back(']');
        } else {
            out.push_back(c);
        }
    }
    out.insert(out.begin(), '*');
    out.push_back('*');
    return out;
}

/// 绑定 BLOB 参数；空指针退化为 zeroblob(0)，避免被当作 SQL NULL。
void bind_blob_safe(sqlite3_stmt* stmt, int idx, const void* data, int bytes) {
    if (data == nullptr) {
        sqlite3_bind_zeroblob(stmt, idx, 0);
    } else {
        sqlite3_bind_blob(stmt, idx, data, bytes, SQLITE_TRANSIENT);
    }
}

}  // namespace

// =============================================================================
// RAII deleter 实现
// =============================================================================

void StorageEngine::SqliteDbDeleter::operator()(sqlite3* db) const {
    if (db != nullptr) {
        sqlite3_close_v2(db);
    }
}

void StorageEngine::SqliteStmtDeleter::operator()(sqlite3_stmt* stmt) const {
    if (stmt != nullptr) {
        sqlite3_finalize(stmt);
    }
}

// =============================================================================
// 构造与析构
// =============================================================================

StorageEngine::StorageEngine(const std::filesystem::path& db_path) {
    sqlite3* raw = nullptr;
    // SQLITE_OPEN_NOMUTEX：使用多线程模式，由本类的 mutex 提供线程安全。
    // SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE：默认读写并允许新建。
    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX;
    int rc = sqlite3_open_v2(db_path.string().c_str(), &raw, flags, nullptr);
    if (rc != SQLITE_OK) {
        // 即便打开失败 raw 也可能被分配，需关闭以免泄漏。
        if (raw != nullptr) {
            sqlite3_close_v2(raw);
        }
        db_ = nullptr;
        return;
    }
    db_.reset(raw);

    // 设置忙等待超时（毫秒）：避免并发场景下立即返回 SQLITE_BUSY。
    sqlite3_busy_timeout(raw, 3000);

    // 初始化 schema；失败则关闭连接置空。
    core::Error err = init_schema();
    if (!err.ok()) {
        db_.reset();
    }
}

StorageEngine::~StorageEngine() = default;

// =============================================================================
// 内部辅助
// =============================================================================

core::Error StorageEngine::exec_sql(const char* sql) {
    if (db_ == nullptr) {
        return core::Error(core::ErrorCode::StorageError,
                           "database not opened");
    }
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_.get(), sql, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::string msg = err_msg ? err_msg : sqlite3_errmsg(db_.get());
        sqlite3_free(err_msg);
        return core::Error(core::ErrorCode::StorageError,
                           std::string("exec '") + sql + "' failed: " + msg);
    }
    return core::Error{};
}

core::Error StorageEngine::init_schema() {
    const char* kCreateTable = R"SQL(
        CREATE TABLE IF NOT EXISTS passwords (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            website     TEXT NOT NULL,
            username    TEXT NOT NULL,
            password    BLOB NOT NULL,
            note        TEXT,
            iv          BLOB NOT NULL,
            tag         BLOB NOT NULL,
            created_at  INTEGER NOT NULL,
            updated_at  INTEGER NOT NULL
        );
    )SQL";
    core::Error err = exec_sql(kCreateTable);
    if (!err.ok()) return err;
    err = exec_sql(
        "CREATE INDEX IF NOT EXISTS idx_passwords_website ON passwords(website);");
    if (!err.ok()) return err;
    err = exec_sql(
        "CREATE INDEX IF NOT EXISTS idx_passwords_username ON passwords(username);");
    if (!err.ok()) return err;

    // 生成器历史记录表
    const char* kCreateGenTable = R"SQL(
        CREATE TABLE IF NOT EXISTS generated_passwords (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            password    BLOB NOT NULL,
            length      INTEGER NOT NULL,
            iv          BLOB NOT NULL,
            tag         BLOB NOT NULL,
            created_at  INTEGER NOT NULL
        );
    )SQL";
    err = exec_sql(kCreateGenTable);
    if (!err.ok()) return err;
    err = exec_sql(
        "CREATE INDEX IF NOT EXISTS idx_genpw_created_at ON generated_passwords(created_at);");
    if (!err.ok()) return err;

    // 通用 KV 设置表
    err = exec_sql(
        "CREATE TABLE IF NOT EXISTS settings ("
        "  key   TEXT PRIMARY KEY,"
        "  value TEXT NOT NULL"
        ");");
    return err;
}

core::ByteVec StorageEngine::read_blob_column(sqlite3_stmt* stmt, int col) {
    const void* ptr = sqlite3_column_blob(stmt, col);
    int bytes = sqlite3_column_bytes(stmt, col);
    if (ptr == nullptr || bytes <= 0) {
        return {};
    }
    const auto* byte_ptr = static_cast<const std::byte*>(ptr);
    return core::ByteVec(byte_ptr, byte_ptr + static_cast<size_t>(bytes));
}

core::PasswordEntry StorageEngine::read_row(sqlite3_stmt* stmt) {
    core::PasswordEntry e;
    e.id = sqlite3_column_int64(stmt, 0);

    if (const unsigned char* v = sqlite3_column_text(stmt, 1)) {
        e.website.assign(reinterpret_cast<const char*>(v));
    }
    if (const unsigned char* v = sqlite3_column_text(stmt, 2)) {
        e.username.assign(reinterpret_cast<const char*>(v));
    }
    // password 字段为已加密的二进制；存为 std::string 的字节序列。
    if (const void* v = sqlite3_column_blob(stmt, 3)) {
        int n = sqlite3_column_bytes(stmt, 3);
        e.password.assign(static_cast<const char*>(v), static_cast<size_t>(n));
    }
    if (const unsigned char* v = sqlite3_column_text(stmt, 4)) {
        e.note.assign(reinterpret_cast<const char*>(v));
    }
    e.iv = read_blob_column(stmt, 5);
    e.tag = read_blob_column(stmt, 6);
    e.created_at = sqlite3_column_int64(stmt, 7);
    e.updated_at = sqlite3_column_int64(stmt, 8);
    return e;
}

int64_t StorageEngine::now_seconds() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

// =============================================================================
// CRUD
// =============================================================================

core::Result<core::PasswordEntry> StorageEngine::add_entry(
    const core::PasswordEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_ == nullptr) {
        return core::Result<core::PasswordEntry>::Err(
            core::Error(core::ErrorCode::StorageError, "database not opened"));
    }

    const char* kSql = R"SQL(
        INSERT INTO passwords
            (website, username, password, note, iv, tag, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?);
    )SQL";

    sqlite3_stmt* raw_stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_.get(), kSql, -1, &raw_stmt, nullptr);
    if (rc != SQLITE_OK) {
        return core::Result<core::PasswordEntry>::Err(
            make_storage_error("add_entry: prepare", db_.get()));
    }
    StmtHandle stmt(raw_stmt);

    sqlite3_bind_text(stmt.get(), 1, entry.website.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, entry.username.c_str(), -1, SQLITE_TRANSIENT);
    bind_blob_safe(stmt.get(), 3, entry.password.data(),
                   static_cast<int>(entry.password.size()));
    // note 可空
    if (entry.note.empty()) {
        sqlite3_bind_null(stmt.get(), 4);
    } else {
        sqlite3_bind_text(stmt.get(), 4, entry.note.c_str(), -1, SQLITE_TRANSIENT);
    }
    bind_blob_safe(stmt.get(), 5, entry.iv.data(),
                   static_cast<int>(entry.iv.size()));
    bind_blob_safe(stmt.get(), 6, entry.tag.data(),
                   static_cast<int>(entry.tag.size()));

    const int64_t ts = now_seconds();
    sqlite3_bind_int64(stmt.get(), 7, ts);
    sqlite3_bind_int64(stmt.get(), 8, ts);

    rc = sqlite3_step(stmt.get());
    if (rc != SQLITE_DONE) {
        return core::Result<core::PasswordEntry>::Err(
            rc_to_error(rc, "add_entry: step", db_.get()));
    }

    core::PasswordEntry out = entry;
    out.id = sqlite3_last_insert_rowid(db_.get());
    out.created_at = ts;
    out.updated_at = ts;
    return core::Result<core::PasswordEntry>::Ok(std::move(out));
}

core::Result<core::PasswordEntry> StorageEngine::update_entry(
    const core::PasswordEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_ == nullptr) {
        return core::Result<core::PasswordEntry>::Err(
            core::Error(core::ErrorCode::StorageError, "database not opened"));
    }
    if (entry.id == 0) {
        return core::Result<core::PasswordEntry>::Err(
            core::Error(core::ErrorCode::InvalidArgument,
                        "update_entry: id must not be 0"));
    }

    const char* kSql = R"SQL(
        UPDATE passwords SET
            website    = ?,
            username   = ?,
            password   = ?,
            note       = ?,
            iv         = ?,
            tag        = ?,
            updated_at = ?
        WHERE id = ?;
    )SQL";

    sqlite3_stmt* raw_stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_.get(), kSql, -1, &raw_stmt, nullptr);
    if (rc != SQLITE_OK) {
        return core::Result<core::PasswordEntry>::Err(
            make_storage_error("update_entry: prepare", db_.get()));
    }
    StmtHandle stmt(raw_stmt);

    sqlite3_bind_text(stmt.get(), 1, entry.website.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, entry.username.c_str(), -1, SQLITE_TRANSIENT);
    bind_blob_safe(stmt.get(), 3, entry.password.data(),
                   static_cast<int>(entry.password.size()));
    if (entry.note.empty()) {
        sqlite3_bind_null(stmt.get(), 4);
    } else {
        sqlite3_bind_text(stmt.get(), 4, entry.note.c_str(), -1, SQLITE_TRANSIENT);
    }
    bind_blob_safe(stmt.get(), 5, entry.iv.data(),
                   static_cast<int>(entry.iv.size()));
    bind_blob_safe(stmt.get(), 6, entry.tag.data(),
                   static_cast<int>(entry.tag.size()));
    const int64_t ts = now_seconds();
    sqlite3_bind_int64(stmt.get(), 7, ts);
    sqlite3_bind_int64(stmt.get(), 8, entry.id);

    rc = sqlite3_step(stmt.get());
    if (rc != SQLITE_DONE) {
        return core::Result<core::PasswordEntry>::Err(
            rc_to_error(rc, "update_entry: step", db_.get()));
    }

    if (sqlite3_changes(db_.get()) == 0) {
        return core::Result<core::PasswordEntry>::Err(
            core::Error(core::ErrorCode::NotFound,
                        "update_entry: id not found"));
    }

    core::PasswordEntry out = entry;
    out.updated_at = ts;
    // created_at 不被 UPDATE 修改，DB 中保留原值；
    // 这里沿用调用方传入的值，调用方通常已通过 get_entry 拿到完整条目。
    return core::Result<core::PasswordEntry>::Ok(std::move(out));
}

core::Error StorageEngine::remove_entry(int64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_ == nullptr) {
        return core::Error(core::ErrorCode::StorageError, "database not opened");
    }

    const char* kSql = "DELETE FROM passwords WHERE id = ?;";
    sqlite3_stmt* raw_stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_.get(), kSql, -1, &raw_stmt, nullptr);
    if (rc != SQLITE_OK) {
        return make_storage_error("remove_entry: prepare", db_.get());
    }
    StmtHandle stmt(raw_stmt);

    sqlite3_bind_int64(stmt.get(), 1, id);
    rc = sqlite3_step(stmt.get());
    if (rc != SQLITE_DONE) {
        return rc_to_error(rc, "remove_entry: step", db_.get());
    }
    if (sqlite3_changes(db_.get()) == 0) {
        return core::Error(core::ErrorCode::NotFound,
                           "remove_entry: id not found");
    }
    return core::Error{};
}

core::Result<core::PasswordEntry> StorageEngine::get_entry(int64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_ == nullptr) {
        return core::Result<core::PasswordEntry>::Err(
            core::Error(core::ErrorCode::StorageError, "database not opened"));
    }

    const char* kSql = R"SQL(
        SELECT id, website, username, password, note, iv, tag, created_at, updated_at
        FROM passwords WHERE id = ?;
    )SQL";
    sqlite3_stmt* raw_stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_.get(), kSql, -1, &raw_stmt, nullptr);
    if (rc != SQLITE_OK) {
        return core::Result<core::PasswordEntry>::Err(
            make_storage_error("get_entry: prepare", db_.get()));
    }
    StmtHandle stmt(raw_stmt);

    sqlite3_bind_int64(stmt.get(), 1, id);
    rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) {
        return core::Result<core::PasswordEntry>::Ok(read_row(stmt.get()));
    }
    if (rc == SQLITE_DONE) {
        return core::Result<core::PasswordEntry>::Err(
            core::Error(core::ErrorCode::NotFound, "get_entry: id not found"));
    }
    return core::Result<core::PasswordEntry>::Err(
        rc_to_error(rc, "get_entry: step", db_.get()));
}

core::Result<std::vector<core::PasswordEntry>> StorageEngine::search_entries(
    const core::SearchQuery& query) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_ == nullptr) {
        return core::Result<std::vector<core::PasswordEntry>>::Err(
            core::Error(core::ErrorCode::StorageError, "database not opened"));
    }

    // 字段白名单：password 是 BLOB，不参与搜索（密文搜索无意义）。
    // 字段名经白名单校验后直接拼入 SQL，杜绝注入。
    auto is_searchable_field = [](const std::string& f) {
        return f == "website" || f == "username" || f == "note";
    };

    std::vector<std::string> fields;
    if (query.fields.empty()) {
        fields = {"website", "username", "note"};
    } else {
        for (const auto& f : query.fields) {
            if (is_searchable_field(f)) {
                fields.push_back(f);
            }
        }
    }

    std::ostringstream sql;
    sql << "SELECT id, website, username, password, note, iv, tag, "
           "created_at, updated_at FROM passwords";
    if (!fields.empty()) {
        sql << " WHERE ";
        for (size_t i = 0; i < fields.size(); ++i) {
            if (i > 0) sql << " OR ";
            if (query.case_sensitive) {
                sql << fields[i] << " GLOB ?";
            } else {
                sql << fields[i] << " LIKE ? ESCAPE '\\'";
            }
        }
    }
    sql << " ORDER BY created_at DESC;";

    sqlite3_stmt* raw_stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_.get(), sql.str().c_str(), -1,
                                &raw_stmt, nullptr);
    if (rc != SQLITE_OK) {
        return core::Result<std::vector<core::PasswordEntry>>::Err(
            make_storage_error("search_entries: prepare", db_.get()));
    }
    StmtHandle stmt(raw_stmt);

    // 绑定每个字段的匹配模式。
    for (size_t i = 0; i < fields.size(); ++i) {
        std::string pattern = query.case_sensitive
                                  ? escape_glob_pattern(query.text)
                                  : escape_like_pattern(query.text);
        sqlite3_bind_text(stmt.get(), static_cast<int>(i + 1),
                          pattern.c_str(), -1, SQLITE_TRANSIENT);
    }

    std::vector<core::PasswordEntry> results;
    while (true) {
        rc = sqlite3_step(stmt.get());
        if (rc == SQLITE_ROW) {
            results.push_back(read_row(stmt.get()));
        } else if (rc == SQLITE_DONE) {
            break;
        } else {
            return core::Result<std::vector<core::PasswordEntry>>::Err(
                rc_to_error(rc, "search_entries: step", db_.get()));
        }
    }
    return core::Result<std::vector<core::PasswordEntry>>::Ok(std::move(results));
}

core::Result<std::vector<core::PasswordEntry>> StorageEngine::list_entries() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_ == nullptr) {
        return core::Result<std::vector<core::PasswordEntry>>::Err(
            core::Error(core::ErrorCode::StorageError, "database not opened"));
    }

    const char* kSql = R"SQL(
        SELECT id, website, username, password, note, iv, tag, created_at, updated_at
        FROM passwords ORDER BY created_at DESC;
    )SQL";
    sqlite3_stmt* raw_stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_.get(), kSql, -1, &raw_stmt, nullptr);
    if (rc != SQLITE_OK) {
        return core::Result<std::vector<core::PasswordEntry>>::Err(
            make_storage_error("list_entries: prepare", db_.get()));
    }
    StmtHandle stmt(raw_stmt);

    std::vector<core::PasswordEntry> results;
    while (true) {
        rc = sqlite3_step(stmt.get());
        if (rc == SQLITE_ROW) {
            results.push_back(read_row(stmt.get()));
        } else if (rc == SQLITE_DONE) {
            break;
        } else {
            return core::Result<std::vector<core::PasswordEntry>>::Err(
                rc_to_error(rc, "list_entries: step", db_.get()));
        }
    }
    return core::Result<std::vector<core::PasswordEntry>>::Ok(std::move(results));
}

// =============================================================================
// 事务
// =============================================================================

core::Error StorageEngine::begin_transaction() {
    std::lock_guard<std::mutex> lock(mutex_);
    return exec_sql("BEGIN TRANSACTION;");
}

core::Error StorageEngine::commit_transaction() {
    std::lock_guard<std::mutex> lock(mutex_);
    return exec_sql("COMMIT;");
}

core::Error StorageEngine::rollback_transaction() {
    std::lock_guard<std::mutex> lock(mutex_);
    return exec_sql("ROLLBACK;");
}

// =============================================================================
// 生成器历史记录
// =============================================================================

core::GeneratedPasswordRecord StorageEngine::read_generated_row(sqlite3_stmt* stmt) {
    core::GeneratedPasswordRecord r;
    r.id = sqlite3_column_int64(stmt, 0);
    // password 列：以 BLOB 读取后转为 std::string（密文也是字节序列）
    const void* pw_ptr = sqlite3_column_blob(stmt, 1);
    int pw_bytes = sqlite3_column_bytes(stmt, 1);
    if (pw_ptr && pw_bytes > 0) {
        r.password.assign(static_cast<const char*>(pw_ptr),
                          static_cast<size_t>(pw_bytes));
    }
    r.length = sqlite3_column_int(stmt, 2);
    r.iv = read_blob_column(stmt, 3);
    r.tag = read_blob_column(stmt, 4);
    r.created_at = sqlite3_column_int64(stmt, 5);
    return r;
}

core::Result<core::GeneratedPasswordRecord> StorageEngine::add_generated_record(
    const core::GeneratedPasswordRecord& record) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_ == nullptr) {
        return core::Result<core::GeneratedPasswordRecord>::Err(
            core::Error(core::ErrorCode::StorageError, "database not opened"));
    }

    const char* kSql = R"SQL(
        INSERT INTO generated_passwords (password, length, iv, tag, created_at)
        VALUES (?, ?, ?, ?, ?);
    )SQL";
    sqlite3_stmt* raw_stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_.get(), kSql, -1, &raw_stmt, nullptr);
    if (rc != SQLITE_OK) {
        return core::Result<core::GeneratedPasswordRecord>::Err(
            make_storage_error("add_generated_record: prepare", db_.get()));
    }
    StmtHandle stmt(raw_stmt);

    // password 绑定为 BLOB（无论是明文还是密文，按原始字节存储）
    bind_blob_safe(stmt.get(), 1,
                   record.password.data(),
                   static_cast<int>(record.password.size()));
    sqlite3_bind_int(stmt.get(), 2, record.length);
    bind_blob_safe(stmt.get(), 3,
                   record.iv.data(), static_cast<int>(record.iv.size()));
    bind_blob_safe(stmt.get(), 4,
                   record.tag.data(), static_cast<int>(record.tag.size()));
    const int64_t ts = now_seconds();
    sqlite3_bind_int64(stmt.get(), 5, ts);

    rc = sqlite3_step(stmt.get());
    if (rc != SQLITE_DONE) {
        return core::Result<core::GeneratedPasswordRecord>::Err(
            rc_to_error(rc, "add_generated_record: step", db_.get()));
    }

    core::GeneratedPasswordRecord out = record;
    out.id = sqlite3_last_insert_rowid(db_.get());
    out.created_at = ts;
    return core::Result<core::GeneratedPasswordRecord>::Ok(std::move(out));
}

core::Result<core::GeneratedPasswordRecord> StorageEngine::update_generated_record(
    const core::GeneratedPasswordRecord& record) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_ == nullptr) {
        return core::Result<core::GeneratedPasswordRecord>::Err(
            core::Error(core::ErrorCode::StorageError, "database not opened"));
    }
    if (record.id == 0) {
        return core::Result<core::GeneratedPasswordRecord>::Err(
            core::Error(core::ErrorCode::InvalidArgument,
                        "update_generated_record: id must not be 0"));
    }

    // 仅更新 password/length/iv/tag，保留 created_at 不变
    const char* kSql = R"SQL(
        UPDATE generated_passwords
        SET password = ?, length = ?, iv = ?, tag = ?
        WHERE id = ?;
    )SQL";
    sqlite3_stmt* raw_stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_.get(), kSql, -1, &raw_stmt, nullptr);
    if (rc != SQLITE_OK) {
        return core::Result<core::GeneratedPasswordRecord>::Err(
            make_storage_error("update_generated_record: prepare", db_.get()));
    }
    StmtHandle stmt(raw_stmt);

    bind_blob_safe(stmt.get(), 1,
                   record.password.data(),
                   static_cast<int>(record.password.size()));
    sqlite3_bind_int(stmt.get(), 2, record.length);
    bind_blob_safe(stmt.get(), 3,
                   record.iv.data(), static_cast<int>(record.iv.size()));
    bind_blob_safe(stmt.get(), 4,
                   record.tag.data(), static_cast<int>(record.tag.size()));
    sqlite3_bind_int64(stmt.get(), 5, record.id);

    rc = sqlite3_step(stmt.get());
    if (rc != SQLITE_DONE) {
        return core::Result<core::GeneratedPasswordRecord>::Err(
            rc_to_error(rc, "update_generated_record: step", db_.get()));
    }
    if (sqlite3_changes(db_.get()) == 0) {
        return core::Result<core::GeneratedPasswordRecord>::Err(
            core::Error(core::ErrorCode::NotFound,
                        "update_generated_record: id not found"));
    }

    // created_at 不变，直接返回调用方传入的 record
    return core::Result<core::GeneratedPasswordRecord>::Ok(record);
}

core::Result<std::vector<core::GeneratedPasswordRecord>>
StorageEngine::list_generated_records() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_ == nullptr) {
        return core::Result<std::vector<core::GeneratedPasswordRecord>>::Err(
            core::Error(core::ErrorCode::StorageError, "database not opened"));
    }

    const char* kSql = R"SQL(
        SELECT id, password, length, iv, tag, created_at
        FROM generated_passwords
        ORDER BY created_at DESC, id DESC;
    )SQL";
    sqlite3_stmt* raw_stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_.get(), kSql, -1, &raw_stmt, nullptr);
    if (rc != SQLITE_OK) {
        return core::Result<std::vector<core::GeneratedPasswordRecord>>::Err(
            make_storage_error("list_generated_records: prepare", db_.get()));
    }
    StmtHandle stmt(raw_stmt);

    std::vector<core::GeneratedPasswordRecord> results;
    while (true) {
        rc = sqlite3_step(stmt.get());
        if (rc == SQLITE_ROW) {
            results.push_back(read_generated_row(stmt.get()));
        } else if (rc == SQLITE_DONE) {
            break;
        } else {
            return core::Result<std::vector<core::GeneratedPasswordRecord>>::Err(
                rc_to_error(rc, "list_generated_records: step", db_.get()));
        }
    }
    return core::Result<std::vector<core::GeneratedPasswordRecord>>::Ok(std::move(results));
}

core::Error StorageEngine::remove_generated_record(int64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_ == nullptr) {
        return core::Error(core::ErrorCode::StorageError, "database not opened");
    }
    const char* kSql = "DELETE FROM generated_passwords WHERE id = ?;";
    sqlite3_stmt* raw_stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_.get(), kSql, -1, &raw_stmt, nullptr);
    if (rc != SQLITE_OK) {
        return make_storage_error("remove_generated_record: prepare", db_.get());
    }
    StmtHandle stmt(raw_stmt);
    sqlite3_bind_int64(stmt.get(), 1, id);
    rc = sqlite3_step(stmt.get());
    if (rc != SQLITE_DONE) {
        return rc_to_error(rc, "remove_generated_record: step", db_.get());
    }
    if (sqlite3_changes(db_.get()) == 0) {
        return core::Error(core::ErrorCode::NotFound,
                           "remove_generated_record: id not found");
    }
    return core::Error{};
}

core::Error StorageEngine::clear_generated_records() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_ == nullptr) {
        return core::Error(core::ErrorCode::StorageError, "database not opened");
    }
    return exec_sql("DELETE FROM generated_passwords;");
}

// =============================================================================
// 通用 KV 设置
// =============================================================================

core::Result<std::string> StorageEngine::get_setting(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_ == nullptr) {
        return core::Result<std::string>::Err(
            core::Error(core::ErrorCode::StorageError, "database not opened"));
    }
    const char* kSql = "SELECT value FROM settings WHERE key = ?;";
    sqlite3_stmt* raw_stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_.get(), kSql, -1, &raw_stmt, nullptr);
    if (rc != SQLITE_OK) {
        return core::Result<std::string>::Err(
            make_storage_error("get_setting: prepare", db_.get()));
    }
    StmtHandle stmt(raw_stmt);
    sqlite3_bind_text(stmt.get(), 1, key.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_ROW) {
        const unsigned char* txt = sqlite3_column_text(stmt.get(), 0);
        std::string value = txt ? reinterpret_cast<const char*>(txt) : "";
        return core::Result<std::string>::Ok(std::move(value));
    }
    if (rc == SQLITE_DONE) {
        // key 不存在，返回空字符串（不视为错误）
        return core::Result<std::string>::Ok(std::string{});
    }
    return core::Result<std::string>::Err(
        rc_to_error(rc, "get_setting: step", db_.get()));
}

core::Error StorageEngine::set_setting(const std::string& key,
                                         const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_ == nullptr) {
        return core::Error(core::ErrorCode::StorageError, "database not opened");
    }
    // value 为空 → 删除该 key（语义：空等价于不存在）
    if (value.empty()) {
        const char* kDel = "DELETE FROM settings WHERE key = ?;";
        sqlite3_stmt* raw_stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_.get(), kDel, -1, &raw_stmt, nullptr);
        if (rc != SQLITE_OK) {
            return make_storage_error("set_setting: delete prepare", db_.get());
        }
        StmtHandle stmt(raw_stmt);
        sqlite3_bind_text(stmt.get(), 1, key.c_str(), -1, SQLITE_TRANSIENT);
        rc = sqlite3_step(stmt.get());
        if (rc != SQLITE_DONE) {
            return rc_to_error(rc, "set_setting: delete step", db_.get());
        }
        return core::Error{};
    }
    const char* kSql = R"SQL(
        INSERT INTO settings (key, value) VALUES (?, ?)
        ON CONFLICT(key) DO UPDATE SET value = excluded.value;
    )SQL";
    sqlite3_stmt* raw_stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_.get(), kSql, -1, &raw_stmt, nullptr);
    if (rc != SQLITE_OK) {
        return make_storage_error("set_setting: upsert prepare", db_.get());
    }
    StmtHandle stmt(raw_stmt);
    sqlite3_bind_text(stmt.get(), 1, key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, value.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt.get());
    if (rc != SQLITE_DONE) {
        return rc_to_error(rc, "set_setting: upsert step", db_.get());
    }
    return core::Error{};
}

}  // namespace pwdvault::storage
