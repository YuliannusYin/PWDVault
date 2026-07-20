// coding: utf-8
// =============================================================================
// main.cpp
//
// PwdVault 旧数据迁移工具（pwdvault-migrate.exe）。
//
// 用途：将旧版 Python 密码管理器（PasswordManager）遗留的数据迁移到新版
// PwdVault 的加密存储格式。
//
// 旧版数据布局（%APPDATA%\PasswordManager\）：
//   - passwords.db （SQLite，明文存储；password 字段为 Fernet 加密密文）
//   - key.key      （Fernet 密钥，URL-safe base64 编码的 32 字节）
//
// 新版数据布局（%APPDATA%\PwdVault\）：
//   - vault.db   （SQLite，password 字段为 AES-256-GCM 密文 + iv + tag）
//   - vault.meta （Argon2id salt + 加密的 master_key）
//
// 流程：
//   1. 解析命令行参数（--old-db / --old-key / --new-db / --master-password /
//      --dry-run / --help）
//   2. 检查旧文件存在性
//   3. 读取并解析旧 key.key
//   4. 打开旧 SQLite，读取 passwords 表所有记录
//   5. 初始化新 MasterKeyStore（用 master_password）
//   6. 创建新 StorageEngine + CryptoEngine（master_key）
//   7. 逐条记录：Fernet 解密 → 构造 PasswordEntry → AES-256-GCM 加密 → 写入
//   8. 打印迁移统计
//
// 不链接 Qt；链接 PwdVault::Sdk + OpenSSL + SQLite + libsodium。
// service 模块通过 target_sources 直接加入（参考 tests/integration/CMakeLists.txt）。
// =============================================================================
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <sodium.h>

#include <sqlite3.h>

#include "CryptoEngine.h"
#include "MasterKeyStore.h"
#include "StorageEngine.h"
#include "Types.h"

#include "FernetDecoder.h"

namespace {

// ============================================================================
// 路径与常量
// ============================================================================

constexpr const char* kDefaultLegacySubdir = "PasswordManager";
constexpr const char* kDefaultNewSubdir = "PwdVault";
constexpr const char* kLegacyDbName = "passwords.db";
constexpr const char* kLegacyKeyName = "key.key";
constexpr const char* kNewDbName = "vault.db";
constexpr const char* kNewMetaName = "vault.meta";

// ============================================================================
// 工具函数
// ============================================================================

/// 获取 %APPDATA% 目录。失败时返回空 path。
std::filesystem::path get_appdata_path() {
    const wchar_t* appdata = _wgetenv(L"APPDATA");
    if (appdata == nullptr || appdata[0] == L'\0') {
        return {};
    }
    return std::filesystem::path(appdata);
}

/// 输出一行日志：[LEVEL] message
void log_line(std::string_view level, std::string_view message) {
    std::cout << '[' << level << "] " << message << '\n';
}

/// 输出错误信息到 stderr。
void log_error(std::string_view message) {
    std::cerr << "[ERROR] " << message << '\n';
}

/// 读取整个文件为字符串。失败时返回空。
std::string read_file_to_string(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return {};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

/// 从控制台读取一行（不回显，使用 Windows GetConsoleMode API）。
/// 若不回显失败则退化为回显读取。
std::string read_password_from_console(std::string_view prompt) {
    std::cout << prompt << std::flush;

    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    bool echo_disabled = false;
    if (hin != INVALID_HANDLE_VALUE && GetConsoleMode(hin, &mode)) {
        SetConsoleMode(hin, mode & ~ENABLE_ECHO_INPUT);
        echo_disabled = true;
    }

    std::string line;
    std::getline(std::cin, line);

    if (echo_disabled) {
        SetConsoleMode(hin, mode);
        std::cout << '\n';
    }
    return line;
}

// ============================================================================
// 命令行参数
// ============================================================================

struct CliArgs {
    std::filesystem::path old_db_path;
    std::filesystem::path old_key_path;
    std::filesystem::path new_db_path;
    std::string master_password;
    bool dry_run = false;
    bool show_help = false;
    bool interactive_password = true;
};

void print_help() {
    std::cout <<
        "PwdVault Migration Tool (pwdvault-migrate)\n"
        "Usage: pwdvault-migrate [options]\n"
        "\n"
        "Options:\n"
        "  --old-db=<path>            Path to legacy passwords.db\n"
        "                             (default: %APPDATA%\\PasswordManager\\passwords.db)\n"
        "  --old-key=<path>           Path to legacy Fernet key.key\n"
        "                             (default: %APPDATA%\\PasswordManager\\key.key)\n"
        "  --new-db=<path>            Path to new vault.db\n"
        "                             (default: %APPDATA%\\PwdVault\\vault.db)\n"
        "  --master-password=<pw>     New master password (insecure on CLI;\n"
        "                             if omitted, prompted interactively)\n"
        "  --dry-run                  Only count entries to migrate; do not write\n"
        "  --help, -h                 Show this help and exit\n"
        "\n"
        "Exit codes:\n"
        "  0  success\n"
        "  1  argument error / missing file / unrecoverable failure\n"
        "  2  partial migration (some entries failed; see log)\n";
}

/// 解析 --key=value 形式的参数。返回值对：(matched, value)
std::pair<bool, std::string_view> extract_value(std::string_view arg,
                                                 std::string_view prefix) {
    if (arg.starts_with(prefix)) {
        return {true, arg.substr(prefix.size())};
    }
    return {false, {}};
}

CliArgs parse_args(int argc, char* argv[]) {
    CliArgs args;

    // 默认路径
    auto appdata = get_appdata_path();
    if (!appdata.empty()) {
        args.old_db_path = appdata / kDefaultLegacySubdir / kLegacyDbName;
        args.old_key_path = appdata / kDefaultLegacySubdir / kLegacyKeyName;
        args.new_db_path = appdata / kDefaultNewSubdir / kNewDbName;
    }

    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];

        if (a == "--help" || a == "-h") {
            args.show_help = true;
            continue;
        }
        if (a == "--dry-run") {
            args.dry_run = true;
            continue;
        }

        if (auto [ok, v] = extract_value(a, "--old-db="); ok) {
            args.old_db_path = std::filesystem::path(std::string(v));
            continue;
        }
        if (auto [ok, v] = extract_value(a, "--old-key="); ok) {
            args.old_key_path = std::filesystem::path(std::string(v));
            continue;
        }
        if (auto [ok, v] = extract_value(a, "--new-db="); ok) {
            args.new_db_path = std::filesystem::path(std::string(v));
            continue;
        }
        if (auto [ok, v] = extract_value(a, "--master-password="); ok) {
            args.master_password = std::string(v);
            args.interactive_password = false;
            continue;
        }

        log_error(std::string("Unknown argument: ") + std::string(a));
        args.show_help = true;
    }

    return args;
}

// ============================================================================
// 旧库读取
// ============================================================================

struct LegacyEntry {
    int64_t id = 0;
    std::string website;
    std::string username;
    std::string password_cipher;  // Fernet token（base64）
    std::string note;
};

/// RAII 包装 sqlite3*
struct SqliteDbDeleter {
    void operator()(sqlite3* db) const noexcept {
        if (db) sqlite3_close_v2(db);
    }
};
using SqliteDbPtr = std::unique_ptr<sqlite3, SqliteDbDeleter>;

/// 打开旧库并读取全部 passwords 记录。
/// \param db_path 旧库路径
/// \param[out] entries 读取的记录列表
/// \return 成功返回 0；失败返回非 0 错误码（错误信息已输出）
int read_legacy_entries(const std::filesystem::path& db_path,
                        std::vector<LegacyEntry>& entries) {
    sqlite3* raw = nullptr;
    // 只读模式打开，避免意外写入旧库
    int rc = sqlite3_open_v2(db_path.string().c_str(), &raw,
                             SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr);
    if (rc != SQLITE_OK) {
        log_error(std::string("Failed to open legacy database: ") +
                  (raw ? sqlite3_errmsg(raw) : sqlite3_errstr(rc)));
        if (raw) sqlite3_close_v2(raw);
        return 1;
    }
    SqliteDbPtr db(raw);

    // 旧库 schema 字段顺序（database.py）：
    //   passwords(id, number, website, username, password, note, sensitivity, related_info)
    // 这里通过名称绑定列，避免对顺序假设。
    const char* kSql =
        "SELECT id, website, username, password, note FROM passwords ORDER BY id ASC;";
    sqlite3_stmt* stmt_raw = nullptr;
    rc = sqlite3_prepare_v2(db.get(), kSql, -1, &stmt_raw, nullptr);
    if (rc != SQLITE_OK) {
        log_error(std::string("Failed to prepare SELECT: ") + sqlite3_errmsg(db.get()));
        return 1;
    }

    struct StmtDeleter {
        void operator()(sqlite3_stmt* s) const noexcept {
            if (s) sqlite3_finalize(s);
        }
    };
    std::unique_ptr<sqlite3_stmt, StmtDeleter> stmt(stmt_raw);

    while (true) {
        rc = sqlite3_step(stmt.get());
        if (rc == SQLITE_DONE) break;
        if (rc != SQLITE_ROW) {
            log_error(std::string("Failed to step SELECT: ") + sqlite3_errmsg(db.get()));
            return 1;
        }

        LegacyEntry e;
        e.id = sqlite3_column_int64(stmt.get(), 0);
        if (const unsigned char* v = sqlite3_column_text(stmt.get(), 1)) {
            e.website.assign(reinterpret_cast<const char*>(v));
        }
        if (const unsigned char* v = sqlite3_column_text(stmt.get(), 2)) {
            e.username.assign(reinterpret_cast<const char*>(v));
        }
        if (const unsigned char* v = sqlite3_column_text(stmt.get(), 3)) {
            e.password_cipher.assign(reinterpret_cast<const char*>(v));
        }
        if (const unsigned char* v = sqlite3_column_text(stmt.get(), 4)) {
            e.note.assign(reinterpret_cast<const char*>(v));
        }
        entries.push_back(std::move(e));
    }

    return 0;
}

// ============================================================================
// 迁移主流程
// ============================================================================

int run_migration(const CliArgs& args) {
    // 1. 检查旧文件
    if (args.old_db_path.empty() || args.old_key_path.empty() || args.new_db_path.empty()) {
        log_error("Could not determine paths (APPDATA not set?). "
                  "Please specify --old-db, --old-key, --new-db explicitly.");
        return 1;
    }
    if (!std::filesystem::exists(args.old_db_path)) {
        log_error(std::string("Legacy database not found: ") + args.old_db_path.string());
        return 1;
    }
    if (!std::filesystem::exists(args.old_key_path)) {
        log_error(std::string("Legacy key file not found: ") + args.old_key_path.string());
        return 1;
    }

    log_line("INFO", std::string("Legacy DB  : ") + args.old_db_path.string());
    log_line("INFO", std::string("Legacy Key : ") + args.old_key_path.string());
    log_line("INFO", std::string("New DB     : ") + args.new_db_path.string());

    // 2. 读取旧 key.key
    std::string fernet_key_b64 = read_file_to_string(args.old_key_path);
    if (fernet_key_b64.empty()) {
        log_error("Failed to read legacy key file (empty or unreadable).");
        return 1;
    }
    // 移除可能的尾部换行
    while (!fernet_key_b64.empty() &&
           (fernet_key_b64.back() == '\n' || fernet_key_b64.back() == '\r' ||
            fernet_key_b64.back() == ' ')) {
        fernet_key_b64.pop_back();
    }

    pwdvault::migrate::FernetDecoder fernet(fernet_key_b64);
    if (!fernet.valid()) {
        log_error("Failed to parse legacy Fernet key (expected 32-byte URL-safe base64).");
        return 1;
    }
    log_line("INFO", "Fernet key parsed successfully.");

    // 3. 读取旧库记录
    std::vector<LegacyEntry> legacy_entries;
    if (read_legacy_entries(args.old_db_path, legacy_entries) != 0) {
        return 1;
    }
    log_line("INFO", std::string("Read ") + std::to_string(legacy_entries.size()) +
             " legacy entries.");

    // 4. dry-run 模式：仅打印数量后退出
    if (args.dry_run) {
        std::cout << "[DRY-RUN] Would migrate " << legacy_entries.size()
                  << " entries. No data written.\n";
        std::cout << "[DRY-RUN] Reminder: legacy data is at:\n  "
                  << args.old_db_path.parent_path().string() << "\n";
        return 0;
    }

    // 5. 解析 master password
    std::string master_password = args.master_password;
    if (args.interactive_password) {
        std::string p1 = read_password_from_console("Enter new master password: ");
        std::string p2 = read_password_from_console("Confirm new master password: ");
        if (p1 != p2) {
            log_error("Passwords do not match.");
            return 1;
        }
        if (p1.empty()) {
            log_error("Master password must not be empty.");
            return 1;
        }
        master_password = std::move(p1);
    } else if (master_password.empty()) {
        log_error("Master password must not be empty.");
        return 1;
    }

    // 6. 确保新数据目录存在
    std::error_code ec;
    std::filesystem::create_directories(args.new_db_path.parent_path(), ec);
    if (ec) {
        log_error(std::string("Failed to create new data directory: ") + ec.message());
        return 1;
    }

    // 7. 初始化新的 MasterKeyStore
    //    若 vault.meta 已存在，则报错（迁移目标必须为空库，避免覆盖现有数据）
    auto meta_path = args.new_db_path.parent_path() / kNewMetaName;
    pwdvault::service::MasterKeyStore key_store(meta_path);
    if (key_store.exists()) {
        log_error(std::string("Target vault.meta already exists: ") + meta_path.string() +
                  ". Refusing to overwrite an initialized vault. "
                  "Please remove it manually if you intend to reinitialize.");
        return 1;
    }

    // CryptoEngine 构造时 master_key 可为空（仅用于 derive_key）
    pwdvault::crypto::CryptoEngine kek_crypto(pwdvault::core::ByteSpan{});
    auto init_result = key_store.initialize(master_password, kek_crypto);
    if (!init_result.ok()) {
        log_error(std::string("Failed to initialize master key store: ") +
                  init_result.error().what());
        return 1;
    }
    pwdvault::core::ByteVec master_key = std::move(init_result).value();
    struct MasterKeyZeroer {
        pwdvault::core::ByteVec& v;
        ~MasterKeyZeroer() {
            if (!v.empty()) {
                sodium_memzero(v.data(), v.size());
            }
        }
    } mk_zeroer{master_key};

    // 8. 创建新 StorageEngine + entry 加密用 CryptoEngine
    pwdvault::storage::StorageEngine storage(args.new_db_path);
    pwdvault::crypto::CryptoEngine entry_crypto(
        pwdvault::core::ByteSpan(master_key.data(), master_key.size()));

    // 9. 开启事务，逐条迁移
    if (auto e = storage.begin_transaction(); !e.ok()) {
        log_error(std::string("begin_transaction failed: ") + e.what());
        return 1;
    }

    size_t success_count = 0;
    size_t fail_count = 0;
    size_t idx = 0;
    for (const auto& legacy : legacy_entries) {
        ++idx;
        // 9a. Fernet 解密 password 字段
        auto dec = fernet.decrypt(legacy.password_cipher);
        if (!dec.ok()) {
            log_error(std::string("Decrypt failed for entry #") + std::to_string(idx) +
                      " (id=" + std::to_string(legacy.id) + ", website='" +
                      legacy.website + "'): " + dec.error().what());
            ++fail_count;
            continue;
        }
        const std::string& plaintext_password = dec.value();

        // 9b. 构造 PasswordEntry
        pwdvault::core::PasswordEntry entry;
        entry.website = legacy.website;
        entry.username = legacy.username;
        entry.password = plaintext_password;
        entry.note = legacy.note;

        // 9c. 用 entry_crypto 加密 password → [IV(12) || ciphertext || tag(16)]
        pwdvault::core::ByteSpan plain_span(
            reinterpret_cast<const std::byte*>(entry.password.data()),
            entry.password.size());
        auto enc = entry_crypto.encrypt(plain_span);
        if (!enc.ok()) {
            log_error(std::string("Encrypt failed for entry #") + std::to_string(idx) +
                      " (id=" + std::to_string(legacy.id) + "): " + enc.error().what());
            ++fail_count;
            continue;
        }
        const auto& blob = enc.value();
        // 解析 blob：[IV(12) || ciphertext || tag(16)]
        constexpr size_t kIvLen = 12;
        constexpr size_t kTagLen = 16;
        if (blob.size() < kIvLen + kTagLen) {
            log_error(std::string("Internal error: encrypted blob too short for entry #") +
                      std::to_string(idx));
            ++fail_count;
            continue;
        }
        entry.password.assign(reinterpret_cast<const char*>(blob.data() + kIvLen),
                              blob.size() - kIvLen - kTagLen);
        entry.iv.assign(blob.begin(), blob.begin() + kIvLen);
        entry.tag.assign(blob.end() - kTagLen, blob.end());

        // 9d. 写入新库
        auto add_result = storage.add_entry(entry);
        if (!add_result.ok()) {
            log_error(std::string("Add entry failed for entry #") + std::to_string(idx) +
                      " (id=" + std::to_string(legacy.id) + "): " +
                      add_result.error().what());
            ++fail_count;
            continue;
        }

        ++success_count;
    }

    // 10. 提交或回滚
    if (fail_count > 0 && success_count == 0) {
        // 全失败：回滚
        if (auto e = storage.rollback_transaction(); !e.ok()) {
            log_error(std::string("rollback_transaction failed: ") + e.what());
        }
        log_error("All entries failed; transaction rolled back.");
        return 1;
    }

    if (auto e = storage.commit_transaction(); !e.ok()) {
        log_error(std::string("commit_transaction failed: ") + e.what());
        // 即便 commit 失败，统计已记录；返回错误码
        return 1;
    }

    // 11. 打印迁移统计
    std::cout << "\n========================================\n";
    std::cout << "Migration Summary\n";
    std::cout << "========================================\n";
    std::cout << "  Total legacy entries : " << legacy_entries.size() << '\n';
    std::cout << "  Successfully migrated: " << success_count << '\n';
    std::cout << "  Failed               : " << fail_count << '\n';
    std::cout << "========================================\n";
    std::cout << "\nLegacy data location (please back up and then delete after verifying):\n";
    std::cout << "  " << args.old_db_path.parent_path().string() << "\n";
    std::cout << "\nNew vault location:\n";
    std::cout << "  " << args.new_db_path.parent_path().string() << "\n";

    if (fail_count > 0) {
        log_error("Some entries failed to migrate; please review the log above.");
        return 2;
    }
    return 0;
}

}  // namespace

// ============================================================================
// main 入口
// ============================================================================

int main(int argc, char* argv[]) {
    CliArgs args = parse_args(argc, argv);

    if (args.show_help) {
        print_help();
        return args.old_db_path.empty() ? 1 : 0;
    }

    try {
        return run_migration(args);
    } catch (const std::exception& e) {
        log_error(std::string("Unhandled exception: ") + e.what());
        return 1;
    } catch (...) {
        log_error("Unhandled unknown exception.");
        return 1;
    }
}
