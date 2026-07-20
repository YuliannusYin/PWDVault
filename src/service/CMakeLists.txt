# coding: utf-8
# =============================================================================
# src/service/CMakeLists.txt
#
# PwdVault 服务进程（pwdvault-service.exe）。
#   - main.cpp           进程入口：参数解析、引擎初始化、保活循环、信号处理
#   - IpcServer.cpp      命名管道服务端（OVERLAPPED I/O，多客户端并发）
#   - ServiceCore.cpp    业务调度核心（命令分发、登录/解锁、entry 加解密）
#   - MasterKeyStore.cpp 主密码与 KEK 持久化（vault.meta）
#
# 子系统：控制台（CONSOLE），不使用 WIN32，便于调试时 stdout 可见。
# 链接 PwdVault::Sdk（聚合 core/crypto/storage/generator/protocol）。
# 额外直接链接 libsodium：ServiceCore.cpp 与 MasterKeyStore.cpp 直接调用
# sodium_memzero / randombytes_buf，而 pwdvault-crypto 以 PRIVATE 方式链接
# libsodium，不会传递给下游目标。
# =============================================================================

add_executable(pwdvault-service
    main.cpp
    IpcServer.cpp
    ServiceCore.cpp
    MasterKeyStore.cpp
)

target_link_libraries(pwdvault-service
    PRIVATE
        PwdVault::Sdk
        PwdVault::Warnings
        unofficial-sodium::sodium
)

target_compile_features(pwdvault-service PRIVATE cxx_std_20)

set_target_properties(pwdvault-service PROPERTIES
    OUTPUT_NAME "pwdvault-service"
)

# 安装规则
install(TARGETS pwdvault-service RUNTIME DESTINATION bin)

message(STATUS "PwdVault: src/service/ 配置完成（pwdvault-service 可执行目标，控制台子系统）")
