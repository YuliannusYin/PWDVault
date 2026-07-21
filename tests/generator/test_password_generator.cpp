// coding: utf-8
// =============================================================================
// test_password_generator.cpp
//
// PasswordGenerator 单元测试（GoogleTest）。
// 覆盖：
//   - 长度正确性
//   - 字符集隔离（仅大写、仅自定义）
//   - exclude_ambiguous 生效
//   - 错误路径（空池、length=0、length=1025）
//   - 两次生成不同（随机性）
//   - estimate_strength 熵值估算
// =============================================================================
#include "PasswordGenerator.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

using pwdvault::core::ErrorCode;
using pwdvault::core::PasswordGeneratorOptions;
using pwdvault::generator::PasswordGenerator;

namespace {

/// 检查 \p s 中所有字符是否都落在 [lo, hi] 范围内
bool all_in_range(const std::string& s, char lo, char hi) {
    return std::all_of(s.begin(), s.end(),
                       [&](char c) { return c >= lo && c <= hi; });
}

/// 检查 \p s 中所有字符是否都属于 \p set
bool all_in_set(const std::string& s, const std::string& set) {
    return std::all_of(s.begin(), s.end(),
                       [&](char c) { return set.find(c) != std::string::npos; });
}

}  // namespace

// ---------------------------------------------------------------------------
// 长度与基本正确性
// ---------------------------------------------------------------------------

TEST(PasswordGeneratorTest, DefaultOptionsProduceRequestedLength) {
    PasswordGenerator gen;
    PasswordGeneratorOptions opts;  // 默认 length=16，四个 use_* 全 true
    auto r = gen.generate(opts);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_EQ(r.value().size(), opts.length);
}

TEST(PasswordGeneratorTest, OnlyUppercaseProducesOnlyAtoZ) {
    PasswordGenerator gen;
    PasswordGeneratorOptions opts;
    opts.use_uppercase = true;
    opts.use_lowercase = false;
    opts.use_digits = false;
    opts.use_symbols = false;
    opts.custom_chars.clear();
    opts.length = 64;
    auto r = gen.generate(opts);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_TRUE(all_in_range(r.value(), 'A', 'Z'))
        << "password contained non-uppercase: " << r.value();
}

// ---------------------------------------------------------------------------
// exclude_ambiguous
// ---------------------------------------------------------------------------

TEST(PasswordGeneratorTest, ExcludeAmbiguousRemovesIl1Lo0O) {
    PasswordGenerator gen;
    PasswordGeneratorOptions opts;
    opts.exclude_ambiguous = true;
    opts.length = 128;  // 较长以提高置信度
    auto r = gen.generate(opts);
    ASSERT_TRUE(r.ok()) << r.error().what();
    const std::string ambiguous = "il1Lo0O";
    for (char c : r.value()) {
        EXPECT_EQ(ambiguous.find(c), std::string::npos)
            << "found ambiguous char '" << c << "' in password: " << r.value();
    }
}

// ---------------------------------------------------------------------------
// 自定义字符集
// ---------------------------------------------------------------------------

TEST(PasswordGeneratorTest, CustomCharsOnlyWhenNoStandardSets) {
    PasswordGenerator gen;
    PasswordGeneratorOptions opts;
    opts.use_uppercase = false;
    opts.use_lowercase = false;
    opts.use_digits = false;
    opts.use_symbols = false;
    opts.custom_chars = "abc";
    opts.length = 32;
    auto r = gen.generate(opts);
    ASSERT_TRUE(r.ok()) << r.error().what();
    EXPECT_TRUE(all_in_set(r.value(), "abc"))
        << "password contained chars outside 'abc': " << r.value();
}

// ---------------------------------------------------------------------------
// 错误路径
// ---------------------------------------------------------------------------

TEST(PasswordGeneratorTest, EmptyPoolReturnsError) {
    PasswordGenerator gen;
    PasswordGeneratorOptions opts;
    opts.use_uppercase = false;
    opts.use_lowercase = false;
    opts.use_digits = false;
    opts.use_symbols = false;
    opts.custom_chars.clear();
    auto r = gen.generate(opts);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::InvalidArgument);
}

TEST(PasswordGeneratorTest, ZeroLengthReturnsError) {
    PasswordGenerator gen;
    PasswordGeneratorOptions opts;
    opts.length = 0;
    auto r = gen.generate(opts);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::InvalidArgument);
}

TEST(PasswordGeneratorTest, TooLargeLengthReturnsError) {
    PasswordGenerator gen;
    PasswordGeneratorOptions opts;
    opts.length = 1025;  // 上限 1024
    auto r = gen.generate(opts);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, ErrorCode::InvalidArgument);
}

// ---------------------------------------------------------------------------
// 随机性
// ---------------------------------------------------------------------------

TEST(PasswordGeneratorTest, TwoGenerationsDiffer) {
    PasswordGenerator gen;
    PasswordGeneratorOptions opts;
    opts.length = 32;
    auto r1 = gen.generate(opts);
    auto r2 = gen.generate(opts);
    ASSERT_TRUE(r1.ok()) << r1.error().what();
    ASSERT_TRUE(r2.ok()) << r2.error().what();
    // 32 位长度下碰撞概率约 1/94^32，可视为不可能
    EXPECT_NE(r1.value(), r2.value());
}

// ---------------------------------------------------------------------------
// estimate_strength
// ---------------------------------------------------------------------------

TEST(PasswordGeneratorTest, EstimateStrengthLowercase8) {
    PasswordGenerator gen;
    // 8 * log2(26) ≈ 37.6 bits
    int strength = gen.estimate_strength("abcdefgh");
    EXPECT_NEAR(strength, 37, 2);
}

TEST(PasswordGeneratorTest, EstimateStrengthAllClasses16) {
    PasswordGenerator gen;
    // 16 * log2(94) ≈ 104.87 bits（大小写+数字+32 个 ASCII 标点 = 94）
    int strength = gen.estimate_strength("Ab1!Cd2!Ef3!Gh4!");
    EXPECT_NEAR(strength, 105, 3);
}

TEST(PasswordGeneratorTest, EstimateStrengthEmptyPassword) {
    PasswordGenerator gen;
    EXPECT_EQ(gen.estimate_strength(""), 0);
}
