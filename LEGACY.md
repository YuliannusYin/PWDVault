# Legacy Python 代码归档

本目录下的 `legacy-python/` 保存了项目最初的 Python + Tkinter 实现的源代码，仅作历史参考用途。

## 归档原因

项目正在按照火绒安全软件的工程化思路重构为 C++ + Qt 6 应用：

- 原 Python 实现存在 UI 与业务深度耦合、缺乏构建流水线、测试代码混入生产目录等问题
- 新版采用 UI 进程 + 服务进程分离、模块化引擎架构、CMake 构建发布流水线
- 加密方案由 Fernet 升级为 AES-256-GCM + Argon2id

## 与新版的差异

| 维度 | 旧版（legacy-python） | 新版（C++ + Qt） |
|------|----------------------|------------------|
| 语言 | Python 3 | C++17 |
| GUI | Tkinter | Qt 6 Widgets |
| 架构 | 单进程 | UI 进程 + 服务进程 |
| 加密 | Fernet 对称加密 | AES-256-GCM + Argon2id |
| 存储 | `%APPDATA%\PasswordManager` | `%APPDATA%\PwdVault` |
| 构建 | 无 | CMake + vcpkg + CPack |
| 数据兼容 | — | 不兼容，需使用迁移工具转换 |

## 重要说明

- 新旧数据格式**不兼容**，旧 `passwords.db` + `key.key` 无法直接被新版读取
- 后续会提供独立迁移工具 `pwdvault-migrate.exe` 用于一次性数据迁移
- 此目录下的代码不再维护，请勿修改
