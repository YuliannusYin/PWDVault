# coding: utf-8
# =============================================================================
# QtDeployment.cmake
#
# 封装 windeployqt 调用逻辑，提供 deploy_qt_runtime(target) 函数。
# 在安装或打包阶段自动为目标可执行程序部署 Qt 运行时（DLL）与插件
# （platforms/、styles/、imageformats/、tls/ 等）。
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

# -----------------------------------------------------------------------------
# deploy_qt_runtime(target)
#
# 为指定可执行目标注册 windeployqt 安装后置步骤：
#   - 安装时调用 windeployqt，将 Qt 运行时与插件复制到 DESTDIR/bin
#   - 支持编译时（debug/release）与运行时（debug）两个版本切换
#
# 参数：
#   target     - 必须是可执行目标（如 pwdvault-ui、pwdvault-service）
#   QML_DIR    - (可选) QML 模块目录，本项目暂不使用 QML
# -----------------------------------------------------------------------------
function(deploy_qt_runtime target)
    if(NOT WIN32)
        message(STATUS "deploy_qt_runtime: 非 Windows 平台，跳过 ${target}")
        return()
    endif()

    if(NOT TARGET ${target})
        message(FATAL_ERROR "deploy_qt_runtime: 目标 '${target}' 不存在")
    endif()

    _pwdvault_find_windeployqt(_windeployqt)
    if(NOT _windeployqt)
        message(WARNING "deploy_qt_runtime: 未找到 windeployqt，跳过 ${target} 的 Qt 运行时部署。"
                        " 请确保 Qt bin 目录在 PATH 中或通过 CMAKE_PREFIX_PATH 指定 Qt 安装目录。")
        return()
    endif()

    # 构造部署命令
    set(_deploy_args
        --no-translations        # 不部署翻译文件（项目内嵌中文资源）
        --no-system-d3d-compiler # 不复制 d3dcompiler_47.dll
        --no-opengl-sw           # 不复制软件 OpenGL
        --compiler-runtime       # 复制 VC++ 运行时
    )

    # 处理 Debug/Release 配置
    set(_deploy_dir "$<TARGET_FILE_DIR:${target}>")
    set(_deploy_target "$<TARGET_FILE:${target}>")

    # 通过 install(CODE) 在 cmake --install 阶段执行
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
endfunction()
