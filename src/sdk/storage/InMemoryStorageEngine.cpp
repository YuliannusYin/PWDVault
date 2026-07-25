// coding: utf-8
// =============================================================================
// InMemoryStorageEngine.cpp
//
// 纯内存存储引擎实现。所有方法线程安全（mutex 串行化）。
// 事务通过快照实现：begin 时深拷贝 entries_ 与 next_id_，rollback 时恢复；
// 不支持嵌套事务。所有 mutating 方法在事务外也照常工作。
// =============================================================================
#include "InMemoryStorageEngine.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <string>
#include <utility>

namespace pwdvault::storage {

namespace {

/// 将字符串转小写（ASCII），用于大小写不敏感的子串匹配。
std::string to_lower_ascii(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

/// 子串匹配：case_sensitive 控制是否区分大小写。
bool contains_substring(const std::string& haystack, const std::string& needle,
                        bool case_sensitive) {
    if (needle.empty()) {
        return true;
    }
    if (case_sensitive) {
        return haystack.find(needle) != std::string::npos;
    }
    return to_lower_ascii(haystack).find(to_lower_ascii(needle)) !=
           std::string::npos;
}

}  // namespace

int64_t InMemoryStorageEngine::now_seconds() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

std::vector<core::PasswordEntry>::iterator InMemoryStorageEngine::find_by_id(
    int64_t id) {
    return std::find_if(entries_.begin(), entries_.end(),
                        [id](const core::PasswordEntry& e) { return e.id == id; });
}

core::Result<core::PasswordEntry> InMemoryStorageEngine::add_entry(
    const core::PasswordEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    core::PasswordEntry out = entry;
    out.id = next_id_++;
    const int64_t ts = now_seconds();
    out.created_at = ts;
    out.updated_at = ts;
    entries_.push_back(out);
    return core::Result<core::PasswordEntry>::Ok(std::move(out));
}

core::Result<core::PasswordEntry> InMemoryStorageEngine::update_entry(
    const core::PasswordEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (entry.id == 0) {
        return core::Result<core::PasswordEntry>::Err(
            core::Error(core::ErrorCode::InvalidArgument,
                        "update_entry: id must not be 0"));
    }
    auto it = find_by_id(entry.id);
    if (it == entries_.end()) {
        return core::Result<core::PasswordEntry>::Err(
            core::Error(core::ErrorCode::NotFound, "update_entry: id not found"));
    }
    core::PasswordEntry out = entry;
    out.created_at = it->created_at;  // 保留原始创建时间
    out.updated_at = now_seconds();
    *it = out;
    return core::Result<core::PasswordEntry>::Ok(std::move(out));
}

core::Error InMemoryStorageEngine::remove_entry(int64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = find_by_id(id);
    if (it == entries_.end()) {
        return core::Error(core::ErrorCode::NotFound,
                           "remove_entry: id not found");
    }
    entries_.erase(it);
    return core::Error{};
}

core::Result<core::PasswordEntry> InMemoryStorageEngine::get_entry(int64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = find_by_id(id);
    if (it == entries_.end()) {
        return core::Result<core::PasswordEntry>::Err(
            core::Error(core::ErrorCode::NotFound, "get_entry: id not found"));
    }
    return core::Result<core::PasswordEntry>::Ok(*it);
}

core::Result<std::vector<core::PasswordEntry>>
InMemoryStorageEngine::search_entries(const core::SearchQuery& query) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 确定要搜索的字段集合；password 字段视为已加密字节，不参与搜索。
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

    std::vector<core::PasswordEntry> results;
    for (const auto& e : entries_) {
        bool matched = false;
        for (const auto& f : fields) {
            std::string value;
            if (f == "website") {
                value = e.website;
            } else if (f == "username") {
                value = e.username;
            } else if (f == "note") {
                value = e.note;
            }
            if (contains_substring(value, query.text, query.case_sensitive)) {
                matched = true;
                break;  // OR 语义：任一字段命中即纳入
            }
        }
        if (matched) {
            results.push_back(e);
        }
    }

    // 按 created_at 倒序，与 SQLite 实现保持一致。
    std::sort(results.begin(), results.end(),
              [](const core::PasswordEntry& a, const core::PasswordEntry& b) {
                  return a.created_at > b.created_at;
              });
    return core::Result<std::vector<core::PasswordEntry>>::Ok(std::move(results));
}

core::Result<std::vector<core::PasswordEntry>>
InMemoryStorageEngine::list_entries() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<core::PasswordEntry> results = entries_;
    std::sort(results.begin(), results.end(),
              [](const core::PasswordEntry& a, const core::PasswordEntry& b) {
                  return a.created_at > b.created_at;
              });
    return core::Result<std::vector<core::PasswordEntry>>::Ok(std::move(results));
}

core::Error InMemoryStorageEngine::begin_transaction() {
    std::lock_guard<std::mutex> lock(mutex_);
    // 不支持嵌套事务：若已在事务中则返回错误。
    if (txn_snapshot_.has_value()) {
        return core::Error(core::ErrorCode::StorageError,
                           "begin_transaction: nested transaction not supported");
    }
    Snapshot s;
    s.entries = entries_;
    s.next_id = next_id_;
    s.gen_records = gen_records_;
    s.gen_next_id = gen_next_id_;
    s.settings = settings_;
    txn_snapshot_ = std::move(s);
    return core::Error{};
}

core::Error InMemoryStorageEngine::commit_transaction() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!txn_snapshot_.has_value()) {
        return core::Error(core::ErrorCode::StorageError,
                           "commit_transaction: no active transaction");
    }
    txn_snapshot_.reset();  // 丢弃快照，保留当前状态
    return core::Error{};
}

core::Error InMemoryStorageEngine::rollback_transaction() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!txn_snapshot_.has_value()) {
        return core::Error(core::ErrorCode::StorageError,
                           "rollback_transaction: no active transaction");
    }
    entries_ = std::move(txn_snapshot_->entries);
    next_id_ = txn_snapshot_->next_id;
    gen_records_ = std::move(txn_snapshot_->gen_records);
    gen_next_id_ = txn_snapshot_->gen_next_id;
    settings_ = std::move(txn_snapshot_->settings);
    txn_snapshot_.reset();
    return core::Error{};
}

// =============================================================================
// 生成器历史记录
// =============================================================================

std::vector<core::GeneratedPasswordRecord>::iterator
InMemoryStorageEngine::find_gen_by_id(int64_t id) {
    return std::find_if(gen_records_.begin(), gen_records_.end(),
                        [id](const core::GeneratedPasswordRecord& r) { return r.id == id; });
}

core::Result<core::GeneratedPasswordRecord> InMemoryStorageEngine::add_generated_record(
    const core::GeneratedPasswordRecord& record) {
    std::lock_guard<std::mutex> lock(mutex_);
    core::GeneratedPasswordRecord out = record;
    out.id = gen_next_id_++;
    out.created_at = now_seconds();
    gen_records_.push_back(out);
    return core::Result<core::GeneratedPasswordRecord>::Ok(std::move(out));
}

core::Result<core::GeneratedPasswordRecord> InMemoryStorageEngine::update_generated_record(
    const core::GeneratedPasswordRecord& record) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (record.id == 0) {
        return core::Result<core::GeneratedPasswordRecord>::Err(
            core::Error(core::ErrorCode::InvalidArgument,
                        "update_generated_record: id must not be 0"));
    }
    auto it = find_gen_by_id(record.id);
    if (it == gen_records_.end()) {
        return core::Result<core::GeneratedPasswordRecord>::Err(
            core::Error(core::ErrorCode::NotFound,
                        "update_generated_record: id not found"));
    }
    // 保留原始 created_at，仅更新 password/length/iv/tag
    core::GeneratedPasswordRecord out = record;
    out.created_at = it->created_at;
    *it = out;
    return core::Result<core::GeneratedPasswordRecord>::Ok(std::move(out));
}

core::Result<std::vector<core::GeneratedPasswordRecord>>
InMemoryStorageEngine::list_generated_records() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<core::GeneratedPasswordRecord> results = gen_records_;
    std::sort(results.begin(), results.end(),
              [](const core::GeneratedPasswordRecord& a,
                 const core::GeneratedPasswordRecord& b) {
                  if (a.created_at != b.created_at) return a.created_at > b.created_at;
                  return a.id > b.id;
              });
    return core::Result<std::vector<core::GeneratedPasswordRecord>>::Ok(std::move(results));
}

core::Error InMemoryStorageEngine::remove_generated_record(int64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = find_gen_by_id(id);
    if (it == gen_records_.end()) {
        return core::Error(core::ErrorCode::NotFound,
                           "remove_generated_record: id not found");
    }
    gen_records_.erase(it);
    return core::Error{};
}

core::Error InMemoryStorageEngine::clear_generated_records() {
    std::lock_guard<std::mutex> lock(mutex_);
    gen_records_.clear();
    return core::Error{};
}

// =============================================================================
// 通用 KV 设置
// =============================================================================

std::vector<std::pair<std::string, std::string>>::iterator
InMemoryStorageEngine::find_setting(const std::string& key) {
    return std::find_if(settings_.begin(), settings_.end(),
                        [&key](const std::pair<std::string, std::string>& kv) {
                            return kv.first == key;
                        });
}

core::Result<std::string> InMemoryStorageEngine::get_setting(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = find_setting(key);
    if (it == settings_.end()) {
        return core::Result<std::string>::Ok(std::string{});
    }
    return core::Result<std::string>::Ok(it->second);
}

core::Error InMemoryStorageEngine::set_setting(const std::string& key,
                                                const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = find_setting(key);
    if (value.empty()) {
        if (it != settings_.end()) {
            settings_.erase(it);
        }
        return core::Error{};
    }
    if (it == settings_.end()) {
        settings_.emplace_back(key, value);
    } else {
        it->second = value;
    }
    return core::Error{};
}

}  // namespace pwdvault::storage
