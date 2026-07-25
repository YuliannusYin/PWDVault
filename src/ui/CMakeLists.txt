# coding: utf-8
# =============================================================================
# src/ui/CMakeLists.txt
#
# PwdVault UI 进程（pwdvault-ui.exe）。基于 Qt 6 Widgets，链接 SDK 与协议层。
# 通过命名管道 IPC 客户端与 service 进程通信。
# =============================================================================
# 自动化 MOC/RCC/UIC 由顶层 CMake 的 set(CMAKE_AUTOMOC ON) 等开启
add_executable(pwdvault-ui WIN32
    main.cpp
    MainWindow.cpp
    IpcClient.cpp
    Theme.cpp
    IconKit.cpp
    views/UnlockView.cpp
    views/ProgramPasswordDialog.cpp
    views/PasswordBookView.cpp
    views/InputView.cpp
    views/GeneratorView.cpp
    views/SettingsView.cpp
    views/EditEntryDialog.cpp
    resources/resources.qrc
    pwdvault-ui.rc      # Windows 应用程序图标资源（嵌入多尺寸 ICO）
)
target_include_directories(pwdvault-ui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/views
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
