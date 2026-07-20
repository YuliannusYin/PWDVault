# PwdVault

一个模仿火绒安全软件架构的本地密码管理器，基于 C++20 + Qt 6 实现。所有密码数据
保存在用户本地，不与任何云端服务通信，旨在提供透明、可信、可控的密码安全管理
体验。

## 项目简介

PwdVault 将传统密码管理器重构为模仿火绒安全软件的双进程架构：

- **UI 进程**（`pwdvault-ui.exe`）：基于 Qt 6 Widgets 的图形界面，负责用户交互、
  密码本展示、密码生成与设置。
- **服务进程**（`pwdvault-service.exe`）：后台控制台进程，承载所有敏感操作
  （加解密、数据库读写、主密码验证），通过命名管道（Named Pipe）与 UI 通信。

这种分离使核心加密逻辑与 UI 解耦，便于后续将服务进程以系统服务方式部署、隔离
权限，并降低 UI 崩溃对敏感内存的影响面。

## 核心特性

- **双进程架构**：UI / Service 解耦，命名管道 IPC（OVERLAPPED I/O，多客户端并发）
- **AES-256-GCM 加密**：每个 entry 独立 IV + Tag，防重放攻击
- **Argon2id 密钥派生**：抵御 GPU/ASIC 暴力破解（libsodium 实现）
- **SQLite 持久化**：单文件数据库，便于备份与迁移
- **零云端**：默认不联网，不依赖任何远程服务
- **C++20 现代代码**：`std::span`、`std::expected` 风格 Result 类型、隐藏符号导出

## 构建要求

| 工具          | 版本                | 说明                                       |
|---------------|---------------------|--------------------------------------------|
| Visual Studio | 2022 (MSVC v143)    | 推荐，需支持 C++20                          |
| Qt            | 6.5+                | 模块：Widgets、Network、Concurrent          |
| vcpkg         | 最新                | manifest mode，自动拉取 OpenSSL/libsodium 等 |
| CMake         | 3.20+               | 3.27+ 可启用 CPack INNO 内置生成器          |
| Inno Setup    | 6.x（可选）          | 用于生成 Windows 安装包                     |

## 构建步骤

> 完整构建指南详见 [`docs/BUILD.md`](docs/BUILD.md)；架构设计见 [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)；
> IPC 协议见 [`docs/IPC_PROTOCOL.md`](docs/IPC_PROTOCOL.md)；安全设计见 [`docs/SECURITY.md`](docs/SECURITY.md)。

### 1. 配置环境变量

```powershell
$env:VCPKG_ROOT = "<vcpkg 路径>"
$env:CMAKE_PREFIX_PATH = "<Qt 6 安装路径>\msvc2022_64"
```

### 2. 配置 & 构建

```powershell
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
```

构建产物位于 `build/bin/Release/`：

- `pwdvault-ui.exe`：GUI 入口
- `pwdvault-service.exe`：服务进程入口

### 3. 运行

直接双击 `pwdvault-ui.exe` 即可启动；UI 会自动拉起服务进程。

### 4. 生成安装包（可选）

安装 [Inno Setup 6](https://jrsoftware.org/isinfo.php) 后：

```powershell
cmake --build build --target package_inno
```

输出位置：`build/package/pwdvault-0.1.0-setup.exe`

## 数据存储位置

应用运行时数据保存在：

```
%APPDATA%\PwdVault\
├── vault.db          # SQLite 数据库（加密后的 entry）
├── vault.meta        # 主密码 KEK 元数据（Argon2id salt、验证 tag 等）
└── logs\             # 运行日志
```

卸载应用**不会**删除 `%APPDATA%\PwdVault\`，避免用户数据意外丢失。

## 项目结构

```
PwdVault/
├── cmake/                  # CMake 辅助模块（CompilerWarnings、QtDeployment）
├── packaging/             # Inno Setup 脚本
├── src/
│   ├── sdk/                # SDK 库（core/crypto/storage/generator/protocol）
│   ├── service/            # pwdvault-service 进程
│   └── ui/                 # pwdvault-ui 进程
├── tests/                  # GoogleTest 单元测试
├── legacy-python/          # 原 Python 实现（已归档，不再维护）
├── CMakeLists.txt
├── LICENSE
└── README.md
```

## 许可证

本项目采用 [MIT 许可证](LICENSE)。

## 备注

原 Python + Tkinter 实现已归档到 `legacy-python/` 目录，仅作历史参考，不再维护。
所有新特性均在 C++/Qt 6 代码库上开发。
