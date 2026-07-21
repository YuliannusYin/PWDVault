# AGENTS.md

## 项目概览

PwdVault 是一个本地密码管理器，模仿火绒安全软件的双进程架构：

- **UI 进程**（`pwdvault-ui.exe`）：Qt 6 Widgets 图形界面
- **服务进程**（`pwdvault-service.exe`）：后台进程，承载所有加解密与数据库操作
- **IPC**：Windows 命名管道（`\\.\pipe\PwdVaultService`），OVERLAPPED I/O，多客户端并发
- **加密**：AES-256-GCM（每条目独立 IV+Tag）+ Argon2id 密钥派生
- **存储**：SQLite 单文件，位于 `%APPDATA%\PwdVault\`

详细架构设计见 [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)。

---

## 仓库结构

```
PwdVault/
├── CMakeLists.txt              # 顶层 CMake（C++20、Qt 6、vcpkg manifest）
├── vcpkg.json                  # 依赖清单：OpenSSL、libsodium、SQLite3、GoogleTest
├── cmake/                      # CMake 辅助模块
│   ├── CompilerWarnings.cmake  # 通用警告级别（INTERFACE target: PwdVault::Warnings）
│   └── QtDeployment.cmake      # windeployqt 封装（deploy_qt_runtime 函数）
├── packaging/
│   └── pwdvault.iss            # Inno Setup 安装包脚本
├── src/
│   ├── sdk/                    # 共享 SDK（UI 与 service 都链接）
│   │   ├── core/               # 核心类型与抽象接口（header-only）
│   │   ├── crypto/             # AES-256-GCM + Argon2id 实现
│   │   ├── storage/            # SQLite 持久化 + InMemory 测试实现
│   │   ├── generator/          # 密码生成器（BCryptGenRandom）
│   │   └── protocol/           # IPC 协议（命令枚举、消息结构、二进制序列化）
│   ├── service/                # 服务进程
│   │   ├── main.cpp            # 入口（命令行参数、保活循环、Ctrl+C 处理）
│   │   ├── IpcServer.cpp       # 命名管道服务端
│   │   ├── ServiceCore.cpp     # 命令分发与业务逻辑
│   │   └── ProgramPasswordStore.cpp  # 程序密码与 encryption_key 持久化（vault.meta）
│   ├── ui/                     # UI 进程
│   │   ├── main.cpp            # Qt 入口（拉起 service）
│   │   ├── MainWindow.cpp      # 主窗口与侧边栏
│   │   ├── IpcClient.cpp       # 命名管道客户端
│   │   └── views/              # 4 个功能视图 + 编辑对话框
│   └── migrate/                # 旧 Python 数据迁移工具
│       ├── main.cpp            # CLI 入口
│       ├── FernetDecoder.cpp   # Fernet token 解密
│       └── Base64.cpp          # URL-safe base64 解码
├── tests/                      # GoogleTest 单元/集成测试
│   ├── crypto/                 # 加密引擎测试（19 用例）
│   ├── storage/                # 存储引擎测试（16 用例）
│   ├── generator/              # 生成器测试（11 用例）
│   ├── protocol/               # 协议序列化测试（27 用例）
│   ├── integration/            # 端到端流程测试（18 用例）
│   └── migrate/                # Fernet 解码测试（17 用例）
├── docs/                       # 设计文档（见下文导航）
├── legacy-python/              # 原 Python + Tkinter 实现（归档，只读参考）
├── LICENSE                     # MIT
├── README.md                   # 终端用户文档
└── AGENTS.md                   # 本文件
```

---

## 快速上手

### 环境要求

| 工具            | 版本       | 备注                            |
| --------------- | ---------- | ------------------------------- |
| Visual Studio   | 2022       | 需勾选「使用 C++ 的桌面开发」   |
| Qt              | 6.5+ LTS   | 模块：Widgets、Network、Concurrent |
| CMake           | 3.20+      |                                 |
| vcpkg           | 最新       | manifest mode 自动拉取依赖      |
| Inno Setup      | 6.x（可选）| 生成 Windows 安装包             |

完整环境配置步骤见 [`docs/BUILD.md`](docs/BUILD.md)。

### 构建

```powershell
# 1. 配置环境变量
$env:VCPKG_ROOT = "<vcpkg 路径>"
$env:CMAKE_PREFIX_PATH = "<Qt 6 安装路径>\msvc2022_64"

# 2. configure + build
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release

# 3. 运行全部测试
ctest --test-dir build --output-on-failure

# 4. （可选）生成安装包
cmake --install build --config Release --prefix build/install
cmake --build build --target package_inno
```

构建产物位于 `build/bin/Release/`：

- `pwdvault-ui.exe`：GUI 入口（双击启动，会自动拉起 service）
- `pwdvault-service.exe`：服务进程（通常由 UI 自动拉起，也可独立运行调试）
- `pwdvault-migrate.exe`：旧数据迁移工具

---

## 文档导航

| 文档                                                | 内容                                              |
| --------------------------------------------------- | ------------------------------------------------- |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)        | 双进程架构图、SDK 分层、数据流、密钥层次、扩展指南 |
| [docs/BUILD.md](docs/BUILD.md)                      | 详细环境配置、构建命令、调试技巧、常见问题排查     |
| [docs/IPC_PROTOCOL.md](docs/IPC_PROTOCOL.md)        | 命名管道属性、MessageHeader 字段、命令列表、序列化方案、字节级时序示例 |
| [docs/SECURITY.md](docs/SECURITY.md)                | 威胁模型、加密算法选型、密钥层次、敏感数据清零、已知限制 |
| [docs/MIGRATION.md](docs/MIGRATION.md)              | 旧 Python 数据迁移步骤、`pwdvault-migrate.exe` 用法、故障排查 |
| [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)          | Git 工作流、提交规范、PR 规范、版本管理、发布流程   |
| [README.md](README.md)                              | 终端用户文档（功能介绍、下载安装、使用教程）       |

---

## 代码约定

### C++ 风格

- **标准**：C++20（使用 `std::span`、`std::optional`、`std::filesystem`）
- **命名空间**：
  - `pwdvault::core` — 核心类型与接口
  - `pwdvault::crypto` / `storage` / `generator` / `protocol` — 各引擎
  - `pwdvault::service` / `ui` / `migrate` — 各进程
- **头文件保护**：统一用 `#pragma once`，不用 include guard
- **错误处理**：使用 `core::Result<T>` 而非异常；接口返回 `Result<T>::Err(...)` 表失败
- **资源管理**：RAII 优先，OpenSSL/SQLite/Windows HANDLE 用 `std::unique_ptr` + 自定义 deleter
- **敏感数据**：encryption_key、KEK、派生密钥在析构时用 `sodium_memzero` 清零；HMAC 比较用 `CRYPTO_memcmp` 或 `sodium_memcmp`
- **隐藏符号**：默认 `CMAKE_CXX_VISIBILITY_PRESET hidden`，仅显式导出

### CMake 风格

- **modern CMake**：用 `target_*` 系列命令，避免全局 `include_directories`/`add_compile_options`
- **别名 target**：所有公开库提供 `PwdVault::<Name>` 别名（如 `PwdVault::Sdk`、`PwdVault::Crypto`）
- **INTERFACE 库**：header-only 模块用 INTERFACE 库（如 `pwdvault-sdk-core`）
- **警告**：通过 INTERFACE target `PwdVault::Warnings` 传播，避免重复配置
- **字符编码**：MSVC 加 `/utf-8`，源文件统一 UTF-8

### 命名约定

- **类型/类**：`PascalCase`（`PasswordEntry`、`CryptoEngine`、`IStorageEngine`）
- **接口**：以 `I` 前缀（`IStorageEngine`、`ICryptoEngine`）
- **方法/变量**：`snake_case`（`add_entry`、`encryption_key_`）
- **成员变量**：带尾部下划线（`unlocked_`、`request_id_`）
- **常量/枚举值**：`PascalCase`（`CommandId::AddEntry`、`ErrorCode::NotFound`）
- **文件名**：`PascalCase.cpp`/`.h`（与类名一致）

---

## 模块职责

### SDK（`src/sdk/`）

共享库，UI 与 service 都链接。各子模块独立编译、独立测试、可替换实现。

| 模块          | 职责                                         | 关键接口                                                                          |
| ------------- | -------------------------------------------- | --------------------------------------------------------------------------------- |
| `core`        | 核心类型与抽象接口（header-only）            | `PasswordEntry`、`Result<T>`、`IStorageEngine`、`ICryptoEngine`、`IPasswordGenerator` |
| `crypto`      | AES-256-GCM 加解密 + Argon2id 密钥派生       | `CryptoEngine`                                                                    |
| `storage`     | SQLite 持久化 + InMemory 测试实现            | `StorageEngine`、`InMemoryStorageEngine`                                          |
| `generator`   | 密码生成（BCryptGenRandom + rejection sampling） | `PasswordGenerator`                                                            |
| `protocol`    | IPC 协议（命令枚举、消息结构、二进制序列化） | `CommandId`、`MessageHeader`、`Serializer`                                        |

### 服务进程（`src/service/`）

后台控制台进程，承载所有敏感操作。

| 文件                   | 职责                                                              |
| ---------------------- | ----------------------------------------------------------------- |
| `main.cpp`             | 命令行参数解析、引擎实例化、IpcServer 启动、保活与超时退出、Ctrl+C 信号处理 |
| `IpcServer.cpp`        | 命名管道服务端（OVERLAPPED I/O，多客户端并发，30 秒读超时）       |
| `ServiceCore.cpp`      | 命令分发、entry 加解密、解锁失败计数与冷却（5 次失败锁 5 分钟）、明文/加密模式切换 |
| `ProgramPasswordStore.cpp` | vault.meta 文件读写（salt + Argon2id 参数 + 加密的 encryption_key）、修改程序密码、禁用程序密码（destroy meta） |

### UI 进程（`src/ui/`）

Qt 6 Widgets GUI 程序，不持有任何敏感数据，所有操作通过 IPC 调用 service。

| 文件                           | 职责                                       |
| ------------------------------ | ------------------------------------------ |
| `main.cpp`                     | Qt 入口、首次启动拉起 service、连接重试    |
| `MainWindow.cpp`               | 主窗口（侧边栏 + QStackedWidget）、GetVaultStatus 启动流程（明文直接进入 / 加密显示解锁）、重连流程 |
| `IpcClient.cpp`                | 命名管道客户端（同步调用，10 秒超时，4 次重试） |
| `views/UnlockView.cpp`         | 解锁视图（加密模式下输入程序密码解锁；明文模式下不显示） |
| `views/ProgramPasswordDialog.cpp` | 程序密码管理对话框（启用 / 禁用 / 修改三种模式） |
| `views/PasswordBookView.cpp`   | 密码本（列表、搜索、详情、编辑、删除、复制密码） |
| `views/InputView.cpp`          | 录入视图                                   |
| `views/GeneratorView.cpp`      | 生成器视图（参数配置 + 强度条）            |
| `views/SettingsView.cpp`       | 设置视图（版本信息、锁定、关于）          |
| `views/EditEntryDialog.cpp`    | 编辑条目对话框（模态）                     |

### 迁移工具（`src/migrate/`）

独立命令行程序 `pwdvault-migrate.exe`，从旧 Python 版 `passwords.db` + `key.key` 迁移到新格式。详见 [`docs/MIGRATION.md`](docs/MIGRATION.md)。

---

## 添加新功能指南

### 新增 IPC 命令

1. 在 `src/sdk/protocol/Commands.h` 的 `CommandId` 枚举追加新值（按高字节分组，不复用旧值）
2. 在 `src/sdk/protocol/Messages.h` 追加 `XxxRequest` / `XxxResponse` struct
3. 在 `src/sdk/protocol/Serializer.h` 声明 + `Serializer.cpp` 实现 `serialize`/`deserialize` 特化
4. 在 `src/service/ServiceCore.h/cpp` 添加 `handle_xxx` 私有方法并注册到 `handle_request` 分发
5. 在 `src/ui/IpcClient.h/cpp` 添加对应的同步调用方法
6. 在 UI 视图中调用该方法
7. 在 `tests/protocol/test_protocol.cpp` 添加 round-trip 测试
8. 在 `tests/integration/test_e2e_flow.cpp` 添加端到端用例
9. 在 `docs/IPC_PROTOCOL.md` 命令列表表格中记录

### 新增引擎实现

1. 在 `src/sdk/<module>/` 下创建新实现类，继承对应接口（如 `IStorageEngine`）
2. 在 `src/sdk/<module>/CMakeLists.txt` 加入源文件
3. 在服务进程 `main.cpp` 中实例化并注入 `ServiceCore`
4. 在 `tests/<module>/` 添加单元测试
5. 更新 `docs/ARCHITECTURE.md` 的模块说明

### 新增 UI 视图

1. 在 `src/ui/views/` 创建 `XxxView.h`/`cpp`，继承 `QWidget`，`Q_OBJECT`
2. 在 `src/ui/CMakeLists.txt` 的 `add_executable` 加入源文件
3. 在 `MainWindow.h/cpp` 添加侧边栏条目与 `QStackedWidget` 页面
4. 通过 `IpcClient*` 调用 service，不要在 UI 层做加解密
5. 用 `QtConcurrent::run` 包装耗时 IPC 调用避免阻塞 UI 线程

---

## 测试约定

- **框架**：GoogleTest（`gtest_main`）
- **位置**：`tests/<module>/test_<name>.cpp`
- **运行**：`ctest --test-dir build --output-on-failure`
- **过滤**：`ctest --test-dir build -R "CryptoEngine" --output-on-failure`
- **覆盖要求**：
  - 所有 SDK 模块需有单元测试（round-trip、错误路径、边界值）
  - 所有 IPC 命令需在 `tests/integration/` 中有端到端测试
  - 修复 bug 时需添加回归测试
- **测试隔离**：使用 `InMemoryStorageEngine` 避免文件系统依赖；集成测试在 `SetUp` 中生成唯一临时 meta 文件路径，`TearDown` 清理

---

## 修改代码前的必读规则

1. **接口稳定性**：`src/sdk/core/` 下的抽象接口（`IStorageEngine`、`ICryptoEngine`、`IPasswordGenerator`）一旦发布应保持稳定。修改前评估对 service、ui、migrate、tests 的影响。
2. **不要修改 `legacy-python/`**：该目录是历史归档，仅作参考。
3. **测试必须通过**：修改任何 SDK 模块后，运行对应 `tests/<module>/` 的测试；修改 service 后运行 `tests/integration/`。
4. **文档同步**：修改 IPC 协议、加密方案、数据格式、文件位置等用户可见行为时，同步更新 `docs/` 对应文档。
5. **不引入新依赖**：除非必要，避免在 `vcpkg.json` 中新增依赖。如需新增，先评估许可证与体积影响，并参阅 [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) 的依赖管理流程。
6. **敏感数据处理**：新代码涉及密钥、密码、明文 entry 时，确保用 `sodium_memzero` 清零；比较用常量时间。
7. **错误处理**：用 `core::Result<T>` 而非异常；不要在析构函数中抛异常。
8. **跨平台**：当前仅支持 Windows。若引入跨平台代码，用 `#ifdef _WIN32` 隔离 Windows 专属 API。
9. **资源管理**：所有 HANDLE / `sqlite3*` / `EVP_CIPHER_CTX*` 等用 RAII 包装，避免裸指针与手动释放。
10. **命名空间**：新增类型必须放入对应命名空间，不要污染全局。

---

## 推荐工作流

1. 阅读本文件建立项目整体认知
2. 根据任务定位到具体模块，阅读该模块的源代码与测试
3. 查阅 `docs/` 中相关设计文档理解背景
4. 实现修改后，运行对应测试验证
5. 如有必要，更新 `docs/` 与本文件

---

## 许可证

本项目采用 [MIT 许可证](LICENSE)。
