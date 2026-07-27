# coding: utf-8
# =============================================================================
# src/ui/CMakeLists.txt
#
# PwdVault UI 进程（pwdvault-ui.exe）。基于 Qt 6 Widgets，链接 SDK 与协议层。
# 通过命名管道 IPC 客户端与 service 进程通信。
# =============================================================================
# 自动化 MOC/RCC/UIC 由顶层 CMake 的 set(CMAKE_AUTOMOC ON) 等开启

# -----------------------------------------------------------------------------
# 翻译流水线：.ts -> lrelease -> .qm，嵌入资源 :/translations/
# （Task 37 会用 tr() 包裹字符串，这里先搭好框架）
#
# 注意：qt_add_translation 依赖 Qt6 LinguistTools（lrelease）。vcpkg 的 qtbase
# 默认不含此组件，需在 vcpkg.json 中追加 "qttools" 依赖后重新配置才会安装。
# 未安装时跳过 .qm 生成，main.cpp 的 translator.load() 会优雅失败（无翻译，
# UI 退回源码字面量），不影响构建。
# -----------------------------------------------------------------------------
find_package(Qt6 OPTIONAL_COMPONENTS LinguistTools)

set(PWDVAULT_TRANSLATION_QRC "")
if(Qt6LinguistTools_FOUND)
    set(TS_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/translations/pwdvault_zh_CN.ts"
    )
    # 指定 .qm 输出到当前 binary 目录，路径可预测，便于下方生成的 .qrc 引用
    set_source_files_properties(${TS_FILES}
        PROPERTIES OUTPUT_LOCATION "${CMAKE_CURRENT_BINARY_DIR}")
    qt_add_translation(QM_FILES ${TS_FILES})

    # 生成 translations.qrc 引用 build 目录下的 .qm（绝对路径 + alias）。
    # 不修改静态 resources.qrc：其中文件需在源码树中存在，而 .qm 是 build 产物，
    # 静态 .qrc 无法引用 build 目录文件（除非 configure_file 整个 resources.qrc，
    # 但会破坏现有图标相对路径）。生成的 translations.qrc 是 Qt 标准做法。
    # AUTORCC 会扫描该 .qrc 并把 .qm 加入依赖，确保 lrelease 先于 RCC 执行。
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/translations.qrc"
        "<!DOCTYPE RCC><RCC version=\"1.0\">\n"
        "  <qresource prefix=\"/translations\">\n"
        "    <file alias=\"pwdvault_zh_CN.qm\">${CMAKE_CURRENT_BINARY_DIR}/pwdvault_zh_CN.qm</file>\n"
        "  </qresource>\n"
        "</RCC>\n")
    # 额外保险：显式声明 translations.qrc 依赖 .qm（AUTORCC 通常已自动处理）
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/translations.qrc"
        PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/pwdvault_zh_CN.qm")
    set(PWDVAULT_TRANSLATION_QRC "${CMAKE_CURRENT_BINARY_DIR}/translations.qrc")
    message(STATUS "PwdVault: Qt6LinguistTools 找到，翻译流水线已启用")
else()
    message(WARNING "PwdVault: Qt6LinguistTools 未找到，跳过 .qm 翻译生成。"
                    "在 vcpkg.json 中追加 \"qttools\" 依赖后重新配置即可启用翻译。")
endif()

# -----------------------------------------------------------------------------
# 版本号：configure_file 从 Version.h.in 生成 Version.h
# 源码引用 PWDVAULT_VERSION 宏而非硬编码版本字符串
# -----------------------------------------------------------------------------
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/Version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Version.h"
    @ONLY
)

set(PWDVAULT_UI_SOURCES
    main.cpp
    MainWindow.cpp
    IpcClient.cpp
    Theme.cpp
    IconKit.cpp
    StrengthUtil.cpp
    FlowLayout.cpp
    MarkdownUtil.cpp
    Toast.cpp
    ErrorMessages.cpp
    views/UnlockView.cpp
    views/ProgramPasswordDialog.cpp
    views/PasswordBookView.cpp
    views/PasswordBookDelegate.cpp
    views/PasswordBookModel.cpp
    views/InputView.cpp
    views/GeneratorView.cpp
    views/SettingsView.cpp
    views/EditEntryDialog.cpp
    views/GeneratorHistoryDialog.cpp
    views/TagInputWidget.cpp
    resources/resources.qrc
    pwdvault-ui.rc      # Windows 应用程序图标资源（嵌入多尺寸 ICO）
)
if(PWDVAULT_TRANSLATION_QRC)
    list(APPEND PWDVAULT_UI_SOURCES "${PWDVAULT_TRANSLATION_QRC}")
endif()
add_executable(pwdvault-ui WIN32 ${PWDVAULT_UI_SOURCES})
target_include_directories(pwdvault-ui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/views
    ${CMAKE_CURRENT_BINARY_DIR}    # 让源码 #include "Version.h" 找到生成的版本头
)
target_link_libraries(pwdvault-ui
    PRIVATE
        PwdVault::Sdk
        ${PWDVAULT_QT_LIBS}
        PwdVault::Warnings
)
target_compile_features(pwdvault-ui PRIVATE cxx_std_20)
set_target_properties(pwdvault-ui PROPERTIES
    OUTPUT_NAME "pwdvault-ui"
    WIN32_EXECUTABLE ON
)
# 安装规则
install(TARGETS pwdvault-ui RUNTIME DESTINATION bin)

# 部署 Qt 运行时：POST_BUILD 复制 platforms 插件 + install 阶段 windeployqt
# 必须在此目录调用，因为 add_custom_command(TARGET ...) 要求目标在创建它的
# 同一目录注册。
deploy_qt_runtime(pwdvault-ui)

message(STATUS "PwdVault: src/ui/ 配置完成（pwdvault-ui 可执行目标）")
