// coding: utf-8
// =============================================================================
// InMemoryStorageEngine.h
//
// IStorageEngine 的纯内存实现。主要用于：
//   - 单元测试：避免文件系统依赖，加速用例执行。
//   - 未来扩展：可作为缓存层或临时会话存储。
//
// 实现：
//   - 用 std::vector<PasswordEntry> 持有所有条目。
//   - 自增 id 计数器从 1 开始（0 保留给"未入库"语义）。
//   - 事务通过快照实现：begin 时保存 entries_ 与 next_id_ 副本，
//     rollback 时恢复；commit 时丢弃副本。不支持嵌套事务。
//   - 搜索使用 std::string::find 做简单子串匹配；
//     case_sensitive=false 时将两端文本统一转小写后比较。
// =============================================================================
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

#include "IStorageEngine.h"

namespace pwdvault::storage {

class InMemoryStorageEngine : public core::IStorageEngine {
public:
    InMemoryStorageEngine() = default;
    ~InMemoryStorageEngine() override = default;

    InMemoryStorageEngine(const InMemoryStorageEngine&) = delete;
    InMemoryStorageEngine& operator=(const InMemoryStorageEngine&) = delete;

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

private:
    /// 事务开启时保存的状态快照。
    struct Snapshot {
        std::vector<core::PasswordEntry> entries;
        int64_t next_id = 1;
    };

    std::vector<core::PasswordEntry> entries_;
    int64_t next_id_ = 1;
    std::optional<Snapshot> txn_snapshot_;
    std::mutex mutex_;

    /// 在 entries_ 中线性查找指定 id 的迭代器。
    std::vector<core::PasswordEntry>::iterator find_by_id(int64_t id);

    /// 当前 Unix 时间戳（秒）。
    static int64_t now_seconds();
};

}  // namespace pwdvault::storage
