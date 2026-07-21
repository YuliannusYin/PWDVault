# coding: utf-8
# =============================================================================
# QtDeployment.cmake
#
# 封装 Qt 运行时部署逻辑，提供 deploy_qt_runtime(target) 函数。
#
# 部署分两个阶段：
#   1. POST_BUILD 阶段：构建目标后立即复制 Qt 运行时 DLL 与 platforms 插件
#      到输出目录，使开发期间可直接运行 .exe（无需 cmake --install）。
#   2. install 阶段：若 windeployqt 可用，调用它做完整部署（含翻译、
#      imageformats 等），用于正式打包。
#
# 典型用法（在目标定义后）：
#   deploy_qt_runtime(pwdvault-ui)
# =============================================================================

# 查找 windeployqt 可执行程序（与 Qt 同源）
# Qt6 提供 Qt6::windeployqt 导入目标（CMake 3.21+），优先使用；
# 否则手动在 Qt bin 目录查找。
function(_pwdvault_find_windeployqt out_var)
    if(TARGET Qt6::windeployqt)
        set(${out_var} "Qt6::windeployqt" PARENT_SCOPE)
        return()
    endif()

    # 从 qmake 路径推断 Qt 安装目录
    get_target_property(_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    if(_qmake_executable)
        get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
        if(EXISTS "${_qt_bin_dir}/windeployqt.exe")
            set(${out_var} "${_qt_bin_dir}/windeployqt.exe" PARENT_SCOPE)
            return()
        endif()
    endif()

    # 兜底：在 PATH 中查找
    find_program(_windeployqt_exe NAMES windeployqt windeployqt.exe)
    if(_windeployqt_exe)
        set(${out_var} "${_windeployqt_exe}" PARENT_SCOPE)
        return()
    endif()

    set(${out_var} "" PARENT_SCOPE)
endfunction()

# 查找 Qt 插件目录。
# 兼容多种安装布局：
#   - 标准 Qt 安装：Qt6_DIR=<prefix>/lib/cmake/Qt6，插件在 <prefix>/plugins
#   - vcpkg 安装：  Qt6_DIR=<prefix>/x64-windows/share/Qt6，
#                   qmake 在 <prefix>/x64-windows/tools/Qt6/bin，
#                   插件在 <prefix>/x64-windows/Qt6/plugins
function(_pwdvault_find_qt_plugins_dir out_var)
    # 1. 优先用 Qt6 CMake 配置提供的 QT6_INSTALL_PLUGINS 变量
    if(DEFINED QT6_INSTALL_PLUGINS AND EXISTS "${QT6_INSTALL_PLUGINS}/platforms/qwindows.dll")
        set(${out_var} "${QT6_INSTALL_PLUGINS}" PARENT_SCOPE)
        return()
    endif()

    # 2. 从 Qt6_DIR 推断（Qt6_DIR 指向 Qt6Config.cmake 所在目录）
    if(DEFINED Qt6_DIR)
        # vcpkg: Qt6_DIR=<root>/x64-windows/share/Qt6，插件在 <root>/x64-windows/Qt6/plugins
        get_filename_component(_dir "${Qt6_DIR}" DIRECTORY)  # .../x64-windows/share
        get_filename_component(_dir "${_dir}" DIRECTORY)      # .../x64-windows
        if(EXISTS "${_dir}/Qt6/plugins/platforms/qwindows.dll")
            set(${out_var} "${_dir}/Qt6/plugins" PARENT_SCOPE)
            return()
        endif()
        # 标准 Qt: Qt6_DIR=<prefix>/lib/cmake/Qt6，插件在 <prefix>/plugins
        get_filename_component(_dir "${Qt6_DIR}" DIRECTORY)  # .../lib/cmake
        get_filename_component(_dir "${_dir}" DIRECTORY)      # .../lib
        get_filename_component(_dir "${_dir}" DIRECTORY)      # .../<prefix>
        if(EXISTS "${_dir}/plugins/platforms/qwindows.dll")
            set(${out_var} "${_dir}/plugins" PARENT_SCOPE)
            return()
        endif()
    endif()

    # 3. 从 qmake 路径推断（兜底）
    get_target_property(_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    if(_qmake_executable)
        get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
        get_filename_component(_qt_prefix_dir "${_qt_bin_dir}" DIRECTORY)
        # vcpkg tools 风格：<prefix>/tools/Qt6/bin → 向上找到 <prefix>，插件在 <prefix>/Qt6/plugins
        get_filename_component(_vcpkg_tools_dir "${_qt_prefix_dir}" DIRECTORY)  # .../tools
        if(EXISTS "${_vcpkg_tools_dir}/../Qt6/plugins/platforms/qwindows.dll")
            get_filename_component(_candidate "${_vcpkg_tools_dir}/../Qt6/plugins" ABSOLUTE)
            if(EXISTS "${_candidate}/platforms/qwindows.dll")
                set(${out_var} "${_candidate}" PARENT_SCOPE)
                return()
            endif()
        endif()
        # 标准风格：<prefix>/bin → <prefix>/plugins
        if(EXISTS "${_qt_prefix_dir}/plugins/platforms/qwindows.dll")
            set(${out_var} "${_qt_prefix_dir}/plugins" PARENT_SCOPE)
            return()
        endif()
    endif()

    set(${out_var} "" PARENT_SCOPE)
endfunction()

# -----------------------------------------------------------------------------
# deploy_qt_runtime(target)
#
# 为指定可执行目标注册 Qt 运行时部署：
#   - POST_BUILD：构建后立即复制 platforms 插件到输出目录，使开发期间
#     可直接运行 .exe（不依赖 windeployqt，兼容 vcpkg 安装的 qtbase）。
#   - install 阶段：若 windeployqt 可用，调用它做完整部署（含翻译、
#     imageformats、styles 等），用于正式打包；不可用时仅复制 platforms。
#
# 参数：
#   target     - 必须是可执行目标（如 pwdvault-ui、pwdvault-service）
# -----------------------------------------------------------------------------
function(deploy_qt_runtime target)
    if(NOT WIN32)
        message(STATUS "deploy_qt_runtime: 非 Windows 平台，跳过 ${target}")
        return()
    endif()

    if(NOT TARGET ${target})
        message(FATAL_ERROR "deploy_qt_runtime: 目标 '${target}' 不存在")
    endif()

    _pwdvault_find_qt_plugins_dir(_qt_plugins_dir)
    _pwdvault_find_windeployqt(_windeployqt)

    # ----------------------------------------------------------------------
    # POST_BUILD 阶段：复制 platforms 插件到输出目录
    # 这是开发期间直接运行 .exe 的关键（Qt 通过 platforms/qwindows.dll
    # 提供原生窗口系统集成；缺失时弹出 "no Qt platform plugin could be
    # initialized" 错误）。
    # ----------------------------------------------------------------------
    if(_qt_plugins_dir)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                    "$<TARGET_FILE_DIR:${target}>/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_qt_plugins_dir}/platforms/qwindows.dll"
                    "$<TARGET_FILE_DIR:${target}>/platforms/qwindows.dll"
            COMMENT "deploy_qt_runtime: 复制 Qt platforms 插件到 $<TARGET_FILE_DIR:${target}>"
            VERBATIM
        )
        message(STATUS "deploy_qt_runtime: 已为 ${target} 注册 POST_BUILD platforms 插件复制")
    else()
        message(WARNING "deploy_qt_runtime: 未找到 Qt 插件目录，${target} 运行时可能因缺少 platforms 插件失败。"
                        " 请检查 Qt 安装或 CMAKE_PREFIX_PATH。")
    endif()

    # ----------------------------------------------------------------------
    # install 阶段：若 windeployqt 可用，调用它做完整部署
    # ----------------------------------------------------------------------
    if(_windeployqt)
        set(_deploy_args
            --no-translations        # 不部署翻译文件（项目内嵌中文资源）
            --no-system-d3d-compiler # 不复制 d3dcompiler_47.dll
            --no-opengl-sw           # 不复制软件 OpenGL
            --compiler-runtime       # 复制 VC++ 运行时
        )

        install(CODE
            "message(STATUS \"正在为 ${target} 部署 Qt 运行时...\")
             execute_process(
                 COMMAND \"${_windeployqt}\"
                         ${_deploy_args}
                         --release       # 默认部署 Release 版本 Qt
                         \"\${CMAKE_INSTALL_PREFIX}/bin/$<TARGET_FILE_NAME:${target}>\"
                 WORKING_DIRECTORY \"\${CMAKE_INSTALL_PREFIX}/bin\"
                 RESULT_VARIABLE _windeployqt_result
             )
             if(_windeployqt_result AND NOT _windeployqt_result EQUAL 0)
                 message(WARNING \"windeployqt 退出码: \${_windeployqt_result}\")
             endif()"
            COMPONENT Runtime
        )
        message(STATUS "deploy_qt_runtime: 已为 ${target} 注册 windeployqt 安装步骤")
    else()
        # windeployqt 不可用时，install 阶段也复制 platforms 插件
        if(_qt_plugins_dir)
            install(DIRECTORY "${_qt_plugins_dir}/platforms/"
                    DESTINATION bin/platforms
                    COMPONENT Runtime
                    FILES_MATCHING PATTERN "qwindows.dll")
            message(STATUS "deploy_qt_runtime: windeployqt 不可用，install 阶段将仅复制 platforms 插件")
        endif()
    endif()
endfunction()
