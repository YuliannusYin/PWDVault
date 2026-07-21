// coding: utf-8
// =============================================================================
// Types.h
//
// PwdVault SDK 核心数据类型与公共别名。所有引擎接口（crypto/storage/generator）
// 与协议层均基于此处定义的类型。
//
// 注意：本项目最低 C++ 标准为 C++20（顶层 CMakeLists.txt 中
//       set(CMAKE_CXX_STANDARD 20)），因为本头使用了 std::span。
// =============================================================================
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace pwdvault::core {

/// 字节向量（持有所有权），用于密文、IV、tag、密钥等二进制数据。
using ByteVec = std::vector<std::byte>;

/// 只读字节视图（不持有所有权），用于函数参数传递以避免拷贝。
using ByteSpan = std::span<const std::byte>;

/// 密码条目。
///
/// 内存中的 `password` 字段为明文；持久化时由 ICryptoEngine 加密，
/// 同时填充 `iv` 与 `tag`。`id == 0` 表示尚未入库的新条目。
struct PasswordEntry {
    int64_t id = 0;          ///< 条目主键，0 表示尚未分配的新条目
    std::string website;      ///< 站点/服务名（如 "github.com"）
    std::string username;     ///< 用户名或登录账号
    std::string password;     ///< 明文密码，仅在内存中存在
    std::string note;         ///< 备注（可空）
    int64_t created_at = 0;   ///< 创建时间（Unix 时间戳，秒）
    int64_t updated_at = 0;   ///< 最后更新时间（Unix 时间戳，秒）
    ByteVec iv;               ///< AES-256-GCM 的 IV，固定 12 字节
    ByteVec tag;              ///< AES-256-GCM 的认证 tag，固定 16 字节

    /// 是否为新条目（尚未分配 id）。
    bool is_new() const { return id == 0; }
};

/// 搜索查询条件。
struct SearchQuery {
    std::string text;                          ///< 搜索文本（子串匹配）
    std::vector<std::string> fields;           ///< 限定搜索字段
                                               ///< （website/username/password/note，
                                               ///<  空表示搜索全部字段）
    bool case_sensitive = false;               ///< 是否区分大小写
};

/// 密码生成器选项。
struct PasswordGeneratorOptions {
    size_t length = 16;          ///< 目标密码长度
    bool use_uppercase = true;   ///< 是否包含大写字母 A-Z
    bool use_lowercase = true;   ///< 是否包含小写字母 a-z
    bool use_digits = true;      ///< 是否包含数字 0-9
    bool use_symbols = true;     ///< 是否包含常见符号（!@#$%^&* 等）
    std::string custom_chars;    ///< 追加自定义字符集
    bool exclude_ambiguous = false;  ///< 排除易混字符（il1Lo0O 等）
};

}  // namespace pwdvault::core
