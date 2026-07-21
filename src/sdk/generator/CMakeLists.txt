# coding: utf-8
# =============================================================================
# src/sdk/generator/CMakeLists.txt
#
# PwdVault 密码生成引擎。定义 pwdvault-generator STATIC 库，实现
# IPasswordGenerator 抽象接口：
#   - PasswordGenerator.h/.cpp  密码生成与强度估算
#
# RNG 选用 Windows 原生 BCryptGenRandom（密码学安全），链接系统库 bcrypt。
# =============================================================================

add_library(pwdvault-generator STATIC
    PasswordGenerator.cpp
)

target_include_directories(pwdvault-generator PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(pwdvault-generator
    PUBLIC
        pwdvault-sdk-core
        PwdVault::Warnings
    PRIVATE
        bcrypt   # Windows 系统库（CNG: BCryptGenRandom）
)

add_library(PwdVault::Generator ALIAS pwdvault-generator)

message(STATUS "PwdVault: src/sdk/generator/ 配置完成（STATIC 库，BCryptGenRandom RNG）")
