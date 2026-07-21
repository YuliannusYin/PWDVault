# coding: utf-8
# =============================================================================
# tests/CMakeLists.txt
#
# PwdVault 测试目录聚合入口。各模块的测试以子目录形式加入。
# =============================================================================

add_subdirectory(crypto)
add_subdirectory(storage)
add_subdirectory(generator)
add_subdirectory(protocol)
add_subdirectory(integration)
add_subdirectory(migrate)
