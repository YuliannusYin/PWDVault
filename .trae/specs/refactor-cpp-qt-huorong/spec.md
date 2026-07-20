# 火绒风格重构 Spec

## Why
当前项目是学习 Python 时编写的 Tkinter 密码管理器，代码组织松散（UI 与业务耦合、测试代码混入生产目录、缺乏构建流水线）。为学习火绒安全软件的工程化实践，将项目重构为 C++ + Qt 6 的现代桌面应用，采用 UI 进程 + 服务进程分离、模块化引擎架构、CMake 构建发布流水线，使其具备工业级安全软件的雏形与良好的可扩展性。

## What Changes
- **BREAKING**：完全弃用 Python 技术栈，重写为 C++17 + Qt 6 项目
- **BREAKING**：原 `src/` 目录废弃，采用新的 C++ 项目结构
- 采用 UI 进程 + 服务进程前后端分离架构，通过本地命名管道 IPC 通信
- 模块化引擎架构：存储引擎、加密引擎、密码生成引擎解耦为独立 SDK 模块
- 引入 CMake 构建系统、GoogleTest 单元测试框架、CPack 安装包打包
- 精简功能：保留密码 CRUD、随机生成、搜索三大核心；移除缓存监视器、测试数据生成、数据交换 UI 等冗余模块
- 使用 AES-256-GCM + Argon2id 替代 Fernet 作为加密方案
- 引入主密码登录机制（基于 Argon2id 派生密钥），首次运行需设置主密码
- 服务进程作为最小权限后台进程运行，UI 进程可独立启停

## Impact
- **Affected specs**: 无（首个 spec）
- **Affected code**: 全量重写。原 `src/core/`、`src/ui/`、`src/scripts/` 目录将归档为 `legacy-python/` 供参考，新代码位于 `src/` 下
- **新增依赖**：Qt 6 (Widgets、Network、Concurrent)、OpenSSL、libsodium、SQLite3、GoogleTest、CMake ≥ 3.20
- **开发环境**：Visual Studio 2022（MSVC v143）、Qt Creator（可选）、vcpkg 包管理

## 项目结构（目标）
```
PWDVault/
├── CMakeLists.txt                  # 顶层 CMake
├── cmake/                          # CMake 辅助模块
├── src/
│   ├── sdk/                        # 共享 SDK（UI 与服务都链接）
│   │   ├── core/                   # 核心接口与类型
│   │   ├── storage/                # 存储引擎（SQLite 封装）
│   │   ├── crypto/                 # 加密引擎（AES-256-GCM + Argon2id）
│   │   ├── generator/              # 密码生成引擎
│   │   └── protocol/               # IPC 协议定义
│   ├── service/                    # 服务进程（后台）
│   │   ├── main.cpp
│   │   ├── service_core.cpp
│   │   └── ipc_server.cpp          # 命名管道服务端
│   ├── ui/                         # UI 进程（Qt Widgets）
│   │   ├── main.cpp
│   │   ├── main_window.cpp
│   │   ├── views/                  # 各功能视图
│   │   └── ipc_client.cpp          # 命名管道客户端
│   └── app/                        # 应用级公共代码
├── tests/                          # GoogleTest 单元测试
├── packaging/                      # 安装包资源（.iss / .nsi）
├── legacy-python/                  # 原 Python 代码归档（只读参考）
└── docs/                           # 设计文档
```

## ADDED Requirements

### Requirement: 双进程架构
系统 SHALL 拆分为 UI 进程（pwdvault-ui.exe）与服务进程（pwdvault-service.exe）。UI 进程负责界面渲染与用户交互；服务进程负责加密存储与业务逻辑。两者通过 Windows 命名管道 IPC 通信。

#### Scenario: UI 启动时拉起服务
- **WHEN** 用户启动 UI 进程
- **THEN** UI 进程检测服务进程是否运行，若未运行则拉起服务进程
- **AND** UI 通过命名管道建立连接后方可使用功能

#### Scenario: UI 异常退出时服务保活
- **WHEN** UI 进程崩溃或被关闭
- **THEN** 服务进程继续运行（最长保活 30 秒等待重连）
- **AND** 超时无连接后服务进程自动退出

### Requirement: 主密码登录
系统 SHALL 在首次启动时要求用户设置主密码，使用 Argon2id 算法派生密钥加密密钥（KEK），KEK 仅在内存中存在，服务进程退出即丢失。

#### Scenario: 首次启动设置主密码
- **WHEN** 用户首次启动应用且未检测到密码库
- **THEN** 引导用户设置主密码（需输入两次确认）
- **AND** 使用 Argon2id 派生 KEK，KEK 用于加密数据库主密钥
- **AND** 数据库主密钥经 KEK 加密后存储于元数据表

#### Scenario: 后续启动验证主密码
- **WHEN** 用户启动应用并存在密码库
- **THEN** UI 弹出登录窗口要求输入主密码
- **AND** 输入正确则解密 KEK 并加载到服务进程内存；错误则提示重试（限 5 次）

### Requirement: 模块化引擎架构
系统 SHALL 将核心能力拆分为独立的引擎模块，每个模块有清晰的接口边界，可独立编译与单元测试。

#### Scenario: 引擎可独立调用
- **WHEN** 上层模块（如服务进程）需要执行加密或存储操作
- **THEN** 通过 SDK 提供的抽象接口（IStorageEngine、ICryptoEngine、IPasswordGenerator）调用
- **AND** 引擎实现可被替换（例如存储引擎可换为内存实现用于测试）

### Requirement: 构建发布流水线
系统 SHALL 提供 CMake 构建、单元测试、安装包打包的完整流水线。

#### Scenario: 开发者本地构建
- **WHEN** 开发者执行 `cmake -B build && cmake --build build --config Release`
- **THEN** 生成 UI 进程、服务进程两个可执行文件到 `build/bin/`
- **AND** 执行 `ctest --test-dir build` 运行所有单元测试

#### Scenario: 生成安装包
- **WHEN** 开发者执行 `cpack -C Release`
- **THEN** 生成 Windows 安装包（.exe），包含 UI、服务、所需 Qt 插件与运行时依赖
- **AND** 安装包支持静默安装、卸载、添加/删除程序条目

### Requirement: 可扩展性
系统 SHALL 提供清晰的扩展点，便于未来新增引擎模块或 IPC 命令。

#### Scenario: 新增 IPC 命令
- **WHEN** 开发者需要新增一个业务命令（例如密码强度评估）
- **THEN** 在 `protocol/` 中定义命令枚举与序列化结构即可
- **AND** 服务端注册 handler、客户端调用，无需改动框架代码

#### Scenario: 新增引擎实现
- **WHEN** 开发者需要替换存储引擎（例如改为远程存储）
- **THEN** 实现 `IStorageEngine` 接口并在服务启动时注入即可
- **AND** 不影响 UI 层与其他引擎

## MODIFIED Requirements

### Requirement: 密码录入与保存
原 Python 版本通过 `DatabaseManager.add_password` 直接写入 SQLite。重构后改为：UI 通过 IPC 调用服务的 `AddEntry` 命令，服务进程经加密引擎加密字段后由存储引擎写入。**BREAKING**：原 `passwords` 表结构变更，新增 `created_at`、`updated_at`、`iv`、`tag` 字段以适配 AES-256-GCM。

### Requirement: 随机密码生成
原 Python 版本在 UI 层直接调用 `PasswordGenerator`。重构后密码生成由服务进程的生成引擎统一处理，UI 仅展示与复制，避免在 UI 进程内存中暴露生成算法状态。

### Requirement: 密码搜索
原 Python 版本直接 SQL LIKE 查询。重构后改为服务进程在内存索引中搜索（仅匹配解密后的明文字段），避免在数据库层处理加密数据。

### Requirement: 数据存储位置
原存储于 `%APPDATA%\PasswordManager`。重构后改为 `%APPDATA%\PwdVault\`，包含 `vault.db`（SQLite）、`vault.meta`（Argon2 参数与加密主密钥）。**BREAKING**：与旧版数据不兼容，需提供一次性迁移工具（独立命令行程序）将旧数据导入新格式。

## REMOVED Requirements

### Requirement: 缓存监视器（cache_monitor）
**Reason**: 该模块是早期性能测试遗留代码，实际使用价值低，且与新的双进程架构冲突（缓存应位于服务进程内，对 UI 透明）
**Migration**: 删除 `src/scripts/test_cache_performance.py` 与 `src/ui/cache_monitor.py`，缓存逻辑下沉到服务进程内部实现，UI 不再可见

### Requirement: 测试数据生成脚本
**Reason**: 仅开发期使用，不属于生产代码
**Migration**: 移至 `tests/fixtures/` 下作为测试辅助，不在主程序目录暴露

### Requirement: 数据交换 UI（data_exchange_ui）
**Reason**: 独立的导入导出界面冗余，且当前实现安全风险高（明文导出无保护）
**Migration**: 移除独立 UI；后续如需导入导出，由服务进程提供加密导出（.pvx 格式）能力，UI 仅作为入口
