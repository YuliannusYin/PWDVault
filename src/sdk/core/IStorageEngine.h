// coding: utf-8
// =============================================================================
// IStorageEngine.h
//
// 存储引擎抽象接口。服务进程通过此接口操作密码条目库；
// 具体实现可以是 SQLite 持久化实现（生产）或内存实现（单元测试）。
//
// 实现方须保证：
//   - 所有方法线程安全（服务进程可能并发调用）
//   - 事务方法（begin/commit/rollback）可重入嵌套或显式禁止嵌套（由实现决定）
//   - `add_entry` 返回的条目包含新分配的 id（!= 0）
// =============================================================================
#pragma once

#include <cstdint>
#include <vector>

#include "Error.h"
#include "Result.h"
#include "Types.h"

namespace pwdvault::core {

/// 存储引擎抽象接口。
class IStorageEngine {
public:
    virtual ~IStorageEngine() = default;

    /// 新增条目。
    /// \param entry 待新增条目，`id` 通常为 0（由实现分配）
    /// \return 成功时返回带分配 id 的条目；失败时返回错误
    virtual Result<PasswordEntry> add_entry(const PasswordEntry& entry) = 0;

    /// 更新已存在条目。`entry.id` 必须非 0。
    virtual Result<PasswordEntry> update_entry(const PasswordEntry& entry) = 0;

    /// 删除指定 id 的条目。
    virtual Error remove_entry(int64_t id) = 0;

    /// 按 id 获取单个条目。
    virtual Result<PasswordEntry> get_entry(int64_t id) = 0;

    /// 按 SearchQuery 条件搜索条目。
    virtual Result<std::vector<PasswordEntry>> search_entries(const SearchQuery& query) = 0;

    /// 列出全部条目（按 updated_at 倒序由实现决定）。
    virtual Result<std::vector<PasswordEntry>> list_entries() = 0;

    /// 开启事务。
    virtual Error begin_transaction() = 0;

    /// 提交事务。
    virtual Error commit_transaction() = 0;

    /// 回滚事务。
    virtual Error rollback_transaction() = 0;
};

}  // namespace pwdvault::core
