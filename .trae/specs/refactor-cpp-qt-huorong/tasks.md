# Tasks

## 阶段 0：项目骨架与归档
- [x] Task 1: 归档原 Python 代码到 `legacy-python/` 目录，保持只读参考
  - [x] SubTask 1.1: 创建 `legacy-python/` 目录，移动 `src/`、`main.py`、`README.md` 进去
  - [x] SubTask 1.2: 在仓库根目录新建 `LEGACY.md` 简要说明归档原因与新版差异
- [x] Task 2: 搭建 CMake 工程骨架
  - [x] SubTask 2.1: 编写顶层 `CMakeLists.txt`，配置 C++17、Qt 6、输出目录
  - [x] SubTask 2.2: 创建 `cmake/` 辅助目录，封装 Qt 自动化工具调用
  - [x] SubTask 2.3: 配置 vcpkg 清单（`vcpkg.json`），声明 OpenSSL、libsodium、SQLite3、GoogleTest 依赖
  - [x] SubTask 2.4: 验证空工程可编译通过（`cmake -B build && cmake --build build`）— 注：环境缺 Qt 6/vcpkg 导致 configure 失败，CMake 骨架本身无误

## 阶段 1：SDK 引擎模块
- [x] Task 3: 实现 SDK 核心类型与接口
  - [x] SubTask 3.1: 在 `src/sdk/core/` 定义 `PasswordEntry`、`SearchQuery`、`Result<T>` 等基础类型
  - [x] SubTask 3.2: 定义 `IStorageEngine`、`ICryptoEngine`、`IPasswordGenerator` 抽象接口
- [x] Task 4: 实现加密引擎（`src/sdk/crypto/`）
  - [x] SubTask 4.1: 封装 OpenSSL AES-256-GCM 加解密
  - [x] SubTask 4.2: 封装 libsodium Argon2id 密钥派生
  - [x] SubTask 4.3: 实现主密钥与 KEK 的加密/解密逻辑
  - [x] SubTask 4.4: 编写 GoogleTest 单元测试覆盖加解密与密钥派生
- [x] Task 5: 实现存储引擎（`src/sdk/storage/`）
  - [x] SubTask 5.1: 封装 SQLite3 连接与建表脚本（含 `created_at`、`iv`、`tag` 新字段）
  - [x] SubTask 5.2: 实现 CRUD 操作与事务支持
  - [x] SubTask 5.3: 编写 GoogleTest 单元测试（使用内存数据库）
- [x] Task 6: 实现密码生成引擎（`src/sdk/generator/`）
  - [x] SubTask 6.1: 实现可配置长度与字符集的生成器（使用 cryptographically secure RNG）
  - [x] SubTask 6.2: 编写 GoogleTest 单元测试验证随机性与字符集约束

## 阶段 2：IPC 协议与服务进程
- [x] Task 7: 定义 IPC 协议（`src/sdk/protocol/`）
  - [x] SubTask 7.1: 定义命令枚举（AddEntry、SearchEntries、GeneratePassword、Login、Unlock 等）
  - [x] SubTask 7.2: 使用 JSON 或二进制序列化定义请求/响应消息结构
- [x] Task 8: 实现服务进程（`src/service/`）
  - [x] SubTask 8.1: 实现 `main.cpp`，支持命令行参数（`--install`、`--foreground`）
  - [x] SubTask 8.2: 实现命名管道服务端（`ipc_server.cpp`），支持多客户端并发
  - [x] SubTask 8.3: 实现服务核心（`service_core.cpp`），调度各引擎处理 IPC 命令
  - [x] SubTask 8.4: 实现主密码登录与解锁流程
  - [x] SubTask 8.5: 实现 UI 异常退出后的保活与超时退出逻辑

## 阶段 3：UI 进程
- [x] Task 9: 搭建 Qt UI 框架（`src/ui/`）
  - [x] SubTask 9.1: 实现 `main.cpp` 启动 Qt 事件循环
  - [x] SubTask 9.2: 实现 `main_window.cpp` 主窗口与侧边栏导航
  - [x] SubTask 9.3: 实现命名管道客户端（`ipc_client.cpp`），含连接重试与超时
- [x] Task 10: 实现核心功能视图（`src/ui/views/`）
  - [x] SubTask 10.1: 登录视图（首次设置主密码 / 后续验证主密码）
  - [x] SubTask 10.2: 密码本视图（列表展示、搜索、详情、编辑、删除）
  - [x] SubTask 10.3: 录入视图（添加新密码条目）
  - [x] SubTask 10.4: 生成器视图（参数配置、生成、复制到剪贴板）
- [x] Task 11: 实现 UI 与服务联动
  - [x] SubTask 11.1: UI 启动时拉起服务进程（QProcess）
  - [x] SubTask 11.2: 各视图通过 IPC 客户端调用服务命令
  - [x] SubTask 11.3: 处理服务断连的 UI 提示与重连

## 阶段 4：构建发布与文档
- [x] Task 12: 完善构建配置
  - [x] SubTask 12.1: 配置 CMake `install()` 规则，部署 Qt 插件与运行时（windeployqt）
  - [x] SubTask 12.2: 配置 CPack 生成 Inno Setup 安装包（采用手写 .iss + add_custom_target 方案，因 CMake 3.20 < CPack INNO 内置生成器要求的 3.27）
  - [x] SubTask 12.3: 安装包支持开始菜单快捷方式与卸载条目
- [x] Task 13: 集成测试与文档
  - [x] SubTask 13.1: 编写端到端集成测试（模拟 UI 调用服务全流程）
  - [x] SubTask 13.2: 在 `docs/` 编写架构设计文档与开发者构建指南
  - [x] SubTask 13.3: 更新根目录 `README.md` 介绍新版项目

## 阶段 5（可选）：旧数据迁移
- [x] Task 14: 实现旧数据迁移工具
  - [x] SubTask 14.1: 编写独立命令行程序 `pwdvault-migrate.exe`
  - [x] SubTask 14.2: 读取旧 `passwords.db` 与 `key.key`，解密后按新格式重新加密写入 `vault.db`
  - [x] SubTask 14.3: 在文档中说明迁移步骤

# Task Dependencies
- Task 2 依赖 Task 1（归档完成后才能在根目录搭新工程）
- Task 4、5、6 可并行（独立引擎模块）
- Task 7 依赖 Task 3（协议基于核心类型）
- Task 8 依赖 Task 4、5、6、7（服务需调度各引擎并使用协议）
- Task 9 依赖 Task 7（UI 需使用协议定义）
- Task 10 依赖 Task 9（视图基于 UI 框架）
- Task 11 依赖 Task 8、10（联动需 UI 与服务均就绪）
- Task 12 依赖 Task 11（构建发布需完整可运行）
- Task 13 依赖 Task 12
- Task 14 可与 Task 13 并行（独立工具）
