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

/// 标签（可被多条 PasswordEntry 共享）。
///
/// `name` 在全库范围内唯一；`color` 为十六进制颜色（如 "#AABBCC"），可空表示使用默认色。
/// `id == 0` 表示尚未入库的新标签。
struct Tag {
    int64_t id = 0;          ///< 主键，0 表示尚未分配的新标签
    std::string name;        ///< 唯一名称（不可重复，大小写敏感）
    std::string color;       ///< 十六进制颜色 "#RRGGBB"，可空
    int64_t created_at = 0;  ///< 创建时间（Unix 时间戳，秒）
    int64_t updated_at = 0;  ///< 最后更新时间（Unix 时间戳，秒）

    /// 是否为新标签（尚未分配 id）。
    bool is_new() const { return id == 0; }
};

/// 密码条目。
///
/// 字段语义（与 UI 表头一致）：
///   - `entry_name`（必填）：条目显示标题，如 "GitHub 个人账号"
///   - `account`（必填）：登录 ID，如 "alice@gmail.com"
///   - `username`（可选）：显示名，如 "张三"
///   - `password`（必填）：明文密码，仅在内存中存在
///   - `website`（可选）：站点 URL，如 "https://github.com"
///   - `note`（可选）：备注，支持 markdown 语法
///   - `tags`：关联的标签列表（多对多）
///
/// 持久化时由 ICryptoEngine 加密 `password`，同时填充 `iv` 与 `tag`。
/// `id == 0` 表示尚未入库的新条目。
struct PasswordEntry {
    int64_t id = 0;          ///< 条目主键，0 表示尚未分配的新条目
    std::string entry_name;  ///< *必填* 条目显示标题
    std::string account;     ///< *必填* 登录账号（用户 ID）
    std::string username;    ///< 可选 显示名
    std::string password;    ///< *必填* 明文密码，仅在内存中存在
    std::string website;     ///< 可选 站点 URL
    std::string note;        ///< 可选 备注（markdown 源码）
    std::vector<Tag> tags;   ///< 关联标签列表
    int64_t created_at = 0;  ///< 创建时间（Unix 时间戳，秒）
    int64_t updated_at = 0;  ///< 最后更新时间（Unix 时间戳，秒）
    ByteVec iv;              ///< AES-256-GCM 的 IV，固定 12 字节
    ByteVec tag;             ///< AES-256-GCM 的认证 tag，固定 16 字节

    /// 是否为新条目（尚未分配 id）。
    bool is_new() const { return id == 0; }
};

/// 搜索查询条件。
struct SearchQuery {
    std::string text;                          ///< 搜索文本（子串匹配）
    std::vector<std::string> fields;           ///< 限定搜索字段
                                               ///< （entry_name/account/username/website/note，
                                               ///<  空表示搜索全部可搜索字段）
    bool case_sensitive = false;               ///< 是否区分大小写
    std::vector<int64_t> tag_ids;              ///< 按标签过滤（OR 语义：包含任一即匹配）
                                               ///< 为空时不按标签过滤
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

/// 密码强度等级。
///
/// 阈值依据（估算熵 bit）：
///   - VeryWeak   < 28      6 位小写 ≈ 28 bit，属常见弱口令级别
///   - Weak       28 ~ 50   短长度或单一字符集
///   - Medium     50 ~ 70   中等长度 + 多字符集
///   - Strong     70 ~ 100  长密码 + 多字符集
///   - VeryStrong  >= 100   长密码 + 全字符集
enum class StrengthLevel : uint8_t {
    VeryWeak   = 0,
    Weak       = 1,
    Medium     = 2,
    Strong     = 3,
    VeryStrong = 4,
};

/// 密码强度评估结果。
///
/// \note `score` 与 `level` 一一对应（0..4），便于 UI 直接渲染进度条段数。
///       `warnings` 携带人类可读的中文问题描述，UI 可选择展示。
struct StrengthEstimate {
    int bits = 0;                          ///< 估算熵（bit 数），模式惩罚后的最终值
    StrengthLevel level = StrengthLevel::VeryWeak;  ///< 等级
    int score = 0;                         ///< 0..4，与 level 数值对应
    std::vector<std::string> warnings;     ///< 检测到的弱模式描述

    /// 按 bits 计算 level（不重新评估模式）。
    static StrengthLevel level_from_bits(int bits) {
        if (bits < 28)  return StrengthLevel::VeryWeak;
        if (bits < 50)  return StrengthLevel::Weak;
        if (bits < 70)  return StrengthLevel::Medium;
        if (bits < 100) return StrengthLevel::Strong;
        return StrengthLevel::VeryStrong;
    }
};

/// 生成器密码生成记录。
///
/// 与 PasswordEntry 类似：内存中 `password` 为明文，持久化时由 ICryptoEngine
/// 加密并填充 `iv`/`tag`。明文模式（未启用程序密码）下三者均为空、明文存储。
/// `id == 0` 表示尚未入库的新记录。
struct GeneratedPasswordRecord {
    int64_t id = 0;            ///< 主键，0 表示尚未分配的新记录
    std::string password;      ///< 内存中为明文，持久化时为密文 BLOB
    int32_t length = 0;        ///< 生成时的密码长度（便于列表展示）
    int64_t created_at = 0;    ///< 生成时间（Unix 时间戳，秒）
    ByteVec iv;                ///< AES-256-GCM 的 IV，明文模式下为空
    ByteVec tag;               ///< AES-256-GCM 的 tag，明文模式下为空
};

/// 生成器历史记录上限设置的特殊值：0 表示无限制。
inline constexpr int32_t kGeneratorLimitUnlimited = 0;

}  // namespace pwdvault::core
