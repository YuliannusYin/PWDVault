// coding: utf-8
// =============================================================================
// Base64.cpp
//
// URL-safe base64 解码实现。手写解码表以避免引入 OpenSSL EVP_DecodeBlockSize 等
// 不便处理 URL-safe 字母表的 API；该实现严格遵循 RFC 4648。
// =============================================================================
#include "Base64.h"

#include <cstring>

namespace pwdvault::migrate {

namespace {

// base64 字符到 6 位值的映射表；-1 表示非法字符，-2 表示 padding。
// 同时支持标准字母表与 URL-safe 字母表（+/- 互换，//_ 互换）。
// 注：使用 int 而非 int8_t 以规避 MSVC 对 constexpr int8_t[] 的 C2078/C2131 误报。
constexpr int kDecTable[256] = {
    // 0x00-0x0F
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    // 0x10-0x1F
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    // 0x20-0x2F: space..slash, + maps to 62, - maps to 62, / maps to 63
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, 62, -1, 63,
    // 0x30-0x3F: 0-9 (52-61), :, ;, <, = (-2), >, ?
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -2, -1, -1,
    // 0x40-0x4F: @, A-O (0-14)
    -1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    // 0x50-0x5F: P-Z (15-25), [, \, ], ^, _ (63)
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, 63,
    // 0x60-0x6F: `, a-o (26-40)
    -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    // 0x70-0x7F: p-z (41-51), {, |, }, ~, DEL
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,
    // 0x80-0xFF: 全部非法
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

}  // namespace

core::Result<std::vector<uint8_t>> base64_decode_urlsafe(const std::string& input) {
    if (input.empty()) {
        return core::Result<std::vector<uint8_t>>::Ok({});
    }

    // 1. 跳过首尾的空白字符（容错）。Fernet token 通常不含空白，但读取文件时
    //    可能在末尾携带 '\n'，这里统一忽略首尾空白，避免误判。
    size_t start = 0;
    size_t end = input.size();
    while (start < end && (input[start] == ' ' || input[start] == '\r' ||
                           input[start] == '\n' || input[start] == '\t')) {
        ++start;
    }
    while (end > start && (input[end - 1] == ' ' || input[end - 1] == '\r' ||
                           input[end - 1] == '\n' || input[end - 1] == '\t')) {
        --end;
    }
    if (start == end) {
        return core::Result<std::vector<uint8_t>>::Ok({});
    }

    // 2. 统计实际有效字符与 padding 个数
    size_t valid_count = 0;
    int padding = 0;
    for (size_t i = start; i < end; ++i) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        // 跳过中间的空白（部分实现会在每 76 字符处换行）
        if (c == ' ' || c == '\r' || c == '\n' || c == '\t') {
            continue;
        }
        int v = kDecTable[c];
        if (v == -2) {
            ++padding;
            continue;
        }
        if (v < 0) {
            return core::Result<std::vector<uint8_t>>::Err(
                core::Error(core::ErrorCode::InvalidArgument,
                            std::string("base64_decode_urlsafe: invalid character at offset ") +
                                std::to_string(i)));
        }
        ++valid_count;
    }

    // 3. 长度合法性检查
    //    - 4 字符一组解码出 3 字节
    //    - padding 只能出现在末尾且最多 2 个
    //    - 若无显式 padding，valid_count 应为 4 的倍数；Fernet 通常无 padding
    if (padding > 2) {
        return core::Result<std::vector<uint8_t>>::Err(
            core::Error(core::ErrorCode::InvalidArgument,
                        "base64_decode_urlsafe: padding exceeds 2 characters"));
    }
    if (padding != 0) {
        // 带显式 padding：valid_count + padding 必须是 4 的倍数
        if ((valid_count + padding) % 4 != 0) {
            return core::Result<std::vector<uint8_t>>::Err(
                core::Error(core::ErrorCode::InvalidArgument,
                            "base64_decode_urlsafe: invalid length with padding"));
        }
    } else if (valid_count % 4 != 0) {
        // Fernet token 长度应是 4 的倍数（Python base64.urlsafe_b64encode 输出
        // 总是带 padding）。但若用户输入被裁剪过 padding，本实现也兼容：
        // 仅校验余数只能是 2 或 3（对应 1 或 2 字节 padding 缺失）。
        if (valid_count % 4 == 1) {
            return core::Result<std::vector<uint8_t>>::Err(
                core::Error(core::ErrorCode::InvalidArgument,
                            "base64_decode_urlsafe: invalid length (truncated)"));
        }
    }

    // 4. 计算输出字节数
    //    groups * 3 - padding（无 padding 时按余数推算）
    size_t groups;
    int effective_padding;
    if (padding != 0) {
        groups = (valid_count + padding) / 4;
        effective_padding = padding;
    } else {
        size_t rem = valid_count % 4;
        groups = valid_count / 4 + (rem != 0 ? 1 : 0);
        effective_padding = rem == 0 ? 0 : (rem == 2 ? 2 : 1);
    }
    const size_t out_len = groups * 3 - static_cast<size_t>(effective_padding);

    std::vector<uint8_t> out;
    out.reserve(out_len);

    // 5. 逐组解码
    uint32_t buf = 0;
    int bits = 0;
    size_t produced = 0;
    bool padding_seen = false;
    for (size_t i = start; i < end; ++i) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        if (c == ' ' || c == '\r' || c == '\n' || c == '\t') {
            continue;
        }
        if (c == '=') {
            padding_seen = true;
            continue;
        }
        if (padding_seen) {
            // '=' 后不应再出现有效字符
            return core::Result<std::vector<uint8_t>>::Err(
                core::Error(core::ErrorCode::InvalidArgument,
                            "base64_decode_urlsafe: data after padding"));
        }
        int v = kDecTable[c];
        if (v < 0) {
            return core::Result<std::vector<uint8_t>>::Err(
                core::Error(core::ErrorCode::InvalidArgument,
                            std::string("base64_decode_urlsafe: invalid character")));
        }
        buf = (buf << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            uint8_t byte = static_cast<uint8_t>((buf >> bits) & 0xFF);
            if (produced < out_len) {
                out.push_back(byte);
                ++produced;
            }
            buf &= (1u << bits) - 1u;
        }
    }

    // 6. 长度校验（防止内部计算错误）
    if (produced != out_len) {
        return core::Result<std::vector<uint8_t>>::Err(
            core::Error(core::ErrorCode::InternalError,
                        "base64_decode_urlsafe: decoded length mismatch"));
    }

    return core::Result<std::vector<uint8_t>>::Ok(std::move(out));
}

}  // namespace pwdvault::migrate
