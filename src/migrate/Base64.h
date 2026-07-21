// coding: utf-8
// =============================================================================
// Base64.h
//
// URL-safe base64 解码工具，用于解析旧版 Python 密码管理器遗留的 Fernet 密钥
// 与 token。Fernet 规范要求使用 RFC 4648 的 URL-safe 字母表（'-' 与 '_' 替代
// '+' 与 '/'），且通常省略尾部的 '=' padding。
//
// 本头只提供解码；编码不需要，因为迁移工具只读取旧数据不生成 Fernet token。
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/Result.h"

namespace pwdvault::migrate {

/// URL-safe base64 解码。
/// - 接受标准与 URL-safe 两种字母表（'+'/'-' 互换，'/'/'_' 互换）。
/// - 自动补齐缺失的 '=' padding。
/// - 输入包含非法字符时返回 InvalidArgument 错误。
/// - 空输入返回空字节向量（成功）。
core::Result<std::vector<uint8_t>> base64_decode_urlsafe(const std::string& input);

}  // namespace pwdvault::migrate
