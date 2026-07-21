# coding: utf-8
# =============================================================================
# src/sdk/storage/CMakeLists.txt
#
# PwdVault 存储引擎模块。定义 pwdvault-storage STATIC 库，提供：
#   - StorageEngine.h/.cpp         SQLite3 持久化实现（生产）
#   - InMemoryStorageEngine.h/.cpp 纯内存实现（测试 / 缓存）
#
# 通过抽象接口 IStorageEngine 被服务进程调用；具体实现可替换。
# 依赖：SQLite3（PRIVATE，仅 .cpp 内部使用，不外泄到头文件）。
# =============================================================================

add_library(pwdvault-storage STATIC
    StorageEngine.cpp
    InMemoryStorageEngine.cpp
)

target_include_directories(pwdvault-storage PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(pwdvault-storage
    PUBLIC
        pwdvault-sdk-core
        PwdVault::Warnings
    PRIVATE
        SQLite::SQLite3
)

add_library(PwdVault::Storage ALIAS pwdvault-storage)

message(STATUS "PwdVault: src/sdk/storage/ 配置完成（STATIC 库，SQLite 持久化 + 内存实现）")
