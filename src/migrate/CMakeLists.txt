# coding: utf-8
# =============================================================================
# src/migrate/CMakeLists.txt
#
# PwdVault 旧数据迁移工具（pwdvault-migrate.exe）。
#   - main.cpp           迁移主程序：解析参数、读旧库、写新库
#   - FernetDecoder.cpp  Fernet token 解密（HMAC-SHA256 + AES-128-CBC + PKCS7）
#   - Base64.cpp         URL-safe base64 解码
#
# 不链接 Qt（控制台程序）；需要：
#   - PwdVault::Sdk          （CryptoEngine、StorageEngine、core 类型）
#   - OpenSSL::Crypto        （HMAC、AES-128-CBC、EVP）
#   - SQLite::SQLite3        （直接读取旧库；新库通过 StorageEngine 间接使用）
#   - unofficial-sodium::sodium  （sodium_memzero / sodium_init）
#
# 方案 A：MasterKeyStore.cpp 位于 src/service/，而 service 是 exe target 不可直接
# 链接。这里通过 target_sources 直接将该源文件加入迁移工具（参考
# tests/integration/CMakeLists.txt 的做法）。MasterKeyStore.cpp 仅依赖 SDK 头文件
# 与 libsodium，不依赖 main.cpp / IpcServer.cpp / ServiceCore.cpp，可安全独立编译。
# =============================================================================

add_executable(pwdvault-migrate
    main.cpp
    FernetDecoder.cpp
    Base64.cpp
)

# 直接编译 service 模块的 MasterKeyStore.cpp（不修改 src/service/CMakeLists.txt）
target_sources(pwdvault-migrate PRIVATE
    ${CMAKE_SOURCE_DIR}/src/service/MasterKeyStore.cpp
)

# service 头文件（MasterKeyStore.h）所在目录
# 同时显式将 src/sdk/ 加入 include path：MasterKeyStore.h 中使用
# `#include "core/Error.h"` 形式，需要 src/sdk/ 在搜索路径中才能解析到
# src/sdk/core/Error.h（pwdvault-sdk-core 仅暴露 src/sdk/core/，无法满足该形式）。
target_include_directories(pwdvault-migrate PRIVATE
    ${CMAKE_SOURCE_DIR}/src/service
    ${CMAKE_SOURCE_DIR}/src/sdk
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(pwdvault-migrate
    PRIVATE
        PwdVault::Sdk
        PwdVault::Warnings
        OpenSSL::Crypto
        SQLite::SQLite3
        unofficial-sodium::sodium
)

target_compile_features(pwdvault-migrate PRIVATE cxx_std_20)

set_target_properties(pwdvault-migrate PROPERTIES
    OUTPUT_NAME "pwdvault-migrate"
)

# 安装规则
install(TARGETS pwdvault-migrate RUNTIME DESTINATION bin)

message(STATUS "PwdVault: src/migrate/ 配置完成（pwdvault-migrate 可执行目标，控制台子系统）")
