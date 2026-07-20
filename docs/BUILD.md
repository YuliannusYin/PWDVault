# PwdVault 开发者构建指南

本文档面向 PwdVault 的开发者与贡献者，描述在 Windows 上从零开始构建、测试、安装与打包
PwdVault 的完整流程。项目概览请参见 [README.md](../README.md)，架构设计请参见
[ARCHITECTURE.md](ARCHITECTURE.md)。

## 1. 环境要求

| 工具          | 版本                | 说明                                              |
|---------------|---------------------|---------------------------------------------------|
| Visual Studio | 2022 (MSVC v143)    | 需安装「使用 C++ 的桌面开发」工作负载，支持 C++20 |
| Qt            | 6.5+ LTS            | 模块：Widgets、Network、Concurrent                 |
| vcpkg         | 最新                | manifest mode，自动拉取 OpenSSL / libsodium 等     |
| CMake         | 3.20+               | 3.27+ 可启用 CPack INNO 内置生成器                 |
| Git           | 2.x                 | 克隆仓库与子模块                                   |
| Inno Setup    | 6.x（可选）         | 用于生成 Windows 安装包                            |

> **平台**：PwdVault 仅支持 Windows 10 及以上。macOS / Linux 不在当前支持范围。

## 2. 环境配置步骤

### 2.1 安装 Visual Studio 2022

1. 下载 [Visual Studio 2022 Community](https://visualstudio.microsoft.com/zh-hans/vs/community/)
   （免费版即可）。
2. 启动安装器，在「工作负载」中勾选：
   - **使用 C++ 的桌面开发**（Desktop development with C++）
3. 在「单个组件」中确认已勾选：
   - MSVC v143 - VS 2022 C++ x64/x86 build tools（最新）
   - Windows 11 SDK（或 Windows 10 SDK 最新版）
   - 适用于 Windows 的 C++ CMake 工具
4. 完成安装后重启。

### 2.2 安装 Qt 6

1. 下载 [Qt 在线安装器](https://www.qt.io/download)。
2. 安装时选择组件：
   - **Qt 6.5.x**（或更高 LTS 版本）→ **MSVC 2022 64-bit**
   - **Developer and Designer Tools** → Qt Creator（可选，但推荐）
3. 记录安装路径，如 `D:\Qt\6.5.3\msvc2022_64`。

### 2.3 克隆并初始化 vcpkg

```powershell
# 选一个固定路径，例如 C:\dev\vcpkg
cd C:\dev
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
```

### 2.4 设置环境变量

在 PowerShell 中设置（或加入用户环境变量以持久化）：

```powershell
# vcpkg 根目录
$env:VCPKG_ROOT = "C:\dev\vcpkg"

# Qt 6 安装路径（指向 msvc2022_64 目录）
$env:CMAKE_PREFIX_PATH = "D:\Qt\6.5.3\msvc2022_64"
```

> **持久化**：可通过 `setx VCPKG_ROOT "C:\dev\vcpkg"` 永久写入用户环境变量。
> 重启 PowerShell 后生效。

### 2.5 克隆 PwdVault 仓库

```powershell
cd C:\dev
git clone <repo-url> PWDVault
cd PWDVault
```

## 3. 构建步骤

### 3.1 配置（CMake Configure）

```powershell
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
```

> **首次配置耗时**：vcpkg manifest mode 会自动下载并编译 OpenSSL、libsodium、SQLite3、
> GoogleTest 依赖，预计 5-15 分钟。后续构建会复用 vcpkg 已安装的包。

### 3.2 构建

```powershell
# Release 构建（顶层 CMakeLists.txt 默认为 Release）
cmake --build build --config Release
```

构建产物：

| 文件                     | 路径                                  | 说明            |
|--------------------------|---------------------------------------|-----------------|
| `pwdvault-ui.exe`        | `build/bin/Release/pwdvault-ui.exe`   | GUI 入口        |
| `pwdvault-service.exe`   | `build/bin/Release/pwdvault-service.exe` | 服务进程入口   |
| 单元测试可执行文件      | `build/bin/Release/test_*.exe`        | GoogleTest 用例 |

### 3.3 多配置生成器说明

Visual Studio 生成器是多配置（Multi-Config）的，因此可同时构建 Debug 与 Release：

```powershell
cmake --build build --config Debug
cmake --build build --config Release
```

若使用 Ninja 单配置生成器：

```powershell
cmake -B build -S . -G "Ninja" -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build
```

## 4. 运行测试

### 4.1 通过 CTest 运行全部测试

```powershell
ctest --test-dir build --output-on-failure
```

> `--output-on-failure` 会在测试失败时打印完整 stdout，便于定位问题。

### 4.2 指定构建配置（Multi-Config 生成器）

```powershell
ctest --test-dir build -C Release --output-on-failure
```

### 4.3 运行单个测试可执行文件

```powershell
.\build\bin\Release\test_crypto_engine.exe
.\build\bin\Release\test_storage_engine.exe
.\build\bin\Release\test_password_generator.exe
.\build\bin\Release\test_protocol.exe
.\build\bin\Release\test_e2e_flow.exe   # 端到端集成测试
```

### 4.4 GoogleTest 过滤

```powershell
# 仅运行名字包含 "Add" 的用例
.\build\bin\Release\test_e2e_flow.exe --gtest_filter="*Add*"

# 列出所有用例名
.\build\bin\Release\test_e2e_flow.exe --gtest_list_tests
```

## 5. 安装

### 5.1 默认安装

```powershell
cmake --install build --config Release --prefix build/install
```

安装目录结构：

```
build/install/
├── bin/
│   ├── pwdvault-ui.exe
│   ├── pwdvault-service.exe
│   ├── Qt6Core.dll, Qt6Widgets.dll, ...   # windeployqt 自动部署
│   └── platforms/, styles/, ...            # Qt 插件
├── LICENSE
└── README.md
```

> **windeployqt 自动部署**：顶层 `CMakeLists.txt` 通过 `cmake/QtDeployment.cmake`
> 中的 `deploy_qt_runtime(pwdvault-ui)` 在 install 阶段自动调用 windeployqt 部署 Qt
> 运行时依赖。需确保 `windeployqt.exe` 在 `PATH` 中或通过 `CMAKE_PREFIX_PATH` 找到。

### 5.2 自定义安装路径

```powershell
cmake --install build --config Release --prefix "C:\Program Files\PwdVault"
```

> 安装到 `Program Files` 需要管理员权限，请用「以管理员身份运行」的 PowerShell。

## 6. 生成安装包

### 6.1 准备 Inno Setup

1. 下载并安装 [Inno Setup 6](https://jrsoftware.org/isinfo.php)。
2. 默认安装路径为 `C:\Program Files (x86)\Inno Setup 6\`，CMake 配置阶段会自动检测
   `iscc.exe`。

### 6.2 构建安装包

```powershell
# 先构建可执行目标
cmake --build build --config Release

# 调用 iscc 编译 packaging/pwdvault.iss
cmake --build build --target package_inno
```

输出位置：`build/package/pwdvault-<version>-setup.exe`（具体路径见 `packaging/pwdvault.iss`）。

> 若 CMake 配置阶段未检测到 `iscc.exe`，会输出提示但不会阻断配置。此时 `package_inno`
> 目标仍会创建，但执行时会调用失败的 `iscc` 命令。

## 7. 调试技巧

### 7.1 用 Visual Studio 打开

CMake 项目可直接用 VS 打开：

1. 启动 Visual Studio 2022。
2. 选择「打开本地文件夹」，定位到 PwdVault 根目录。
3. VS 会自动识别 `CMakeLists.txt` 并执行配置（可能需要设置
   `CMakeSettings.json` 中的 `VCPKG_ROOT` 与 `CMAKE_PREFIX_PATH`）。

### 7.2 用 Qt Creator 打开

1. 启动 Qt Creator。
2. 选择「File → Open File or Project」，定位到 `PwdVault/CMakeLists.txt`。
3. 配置 Kit：选择 MSVC 2022 64-bit，确保 vcpkg 工具链路径正确。

### 7.3 命令行调试

```powershell
# 启动 service 进程（控制台模式，stdout 可见）
.\build\bin\Release\pwdvault-service.exe --foreground

# 另开一个终端启动 UI
.\build\bin\Release\pwdvault-ui.exe

# 用 Visual Studio 调试器附加到进程：
# Debug → Attach to Process → 选择 pwdvault-service.exe
```

### 7.4 service 命令行参数

```powershell
# 默认前台运行
.\pwdvault-service.exe --foreground

# 指定命名管道路径
.\pwdvault-service.exe --pipe-name="\\.\pipe\MyVault"

# 显示帮助
.\pwdvault-service.exe --help
```

## 8. 常见问题

### Q1：CMake 报错 "Could not find Qt6"

**原因**：`CMAKE_PREFIX_PATH` 未指向 Qt 6 安装目录，或 Qt 6 未安装。

**解决**：

```powershell
# 检查环境变量
echo $env:CMAKE_PREFIX_PATH

# 临时指定（指向 msvc2022_64 目录）
cmake -B build -S . `
    -DCMAKE_PREFIX_PATH="D:\Qt\6.5.3\msvc2022_64" `
    -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
```

### Q2：vcpkg 包未安装 / 配置超时

**原因**：vcpkg manifest mode 在首次配置时需下载并编译 OpenSSL、libsodium 等大型库，
耗时较长；网络问题可能导致失败。

**解决**：

1. 确认 `vcpkg.json` 中依赖正确（`openssl`、`libsodium`、`sqlite3`、`gtest`）。
2. 设置 vcpkg 镜像或代理（如有需要）：
   ```powershell
   $env:VCPKG_DEFAULT_HOST_TRIPLET = "x64-windows"
   $env:HTTP_PROXY = "http://your-proxy:port"
   ```
3. 删除 `build/` 目录后重新配置：
   ```powershell
   Remove-Item -Recurse -Force build
   cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
   ```

### Q3：MSVC 编译中文乱码

**原因**：源文件编码非 UTF-8，或编译选项未启用 `/utf-8`。

**解决**：

1. 顶层 `CMakeLists.txt` 已配置 `add_compile_options(/utf-8)`，确保未覆盖。
2. 检查源文件编码：用 VS Code 打开，右下角应显示 `UTF-8`。
3. 用记事本另存为 UTF-8（带 BOM 亦可，`/utf-8` 同时支持）。

### Q4：构建时报错 "cannot open input file 'Qt6Widgets.lib'"

**原因**：链接器找不到 Qt 库，通常因 `CMAKE_PREFIX_PATH` 配置错误。

**解决**：确认 `CMAKE_PREFIX_PATH` 指向的是 `msvc2022_64` 子目录，而非 Qt 根目录。
错误示例：`-DCMAKE_PREFIX_PATH="D:\Qt"`。正确示例：`-DCMAKE_PREFIX_PATH="D:\Qt\6.5.3\msvc2022_64"`。

### Q5：windeployqt 找不到

**原因**：`windeployqt.exe` 未在 `PATH` 中。

**解决**：

```powershell
# 临时加入 PATH
$env:PATH = "D:\Qt\6.5.3\msvc2022_64\bin;" + $env:PATH

# 或显式指定 windeployqt 路径
cmake -B build -S . `
    -DQT_DEPLOY_TOOL="D:\Qt\6.5.3\msvc2022_64\bin\windeployqt.exe" `
    -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
```

### Q6：测试用例失败 "vault already initialized"

**原因**：测试残留的 `vault.meta` 文件未被清理，导致首次 `login(is_first_time=true)`
时检测到已有 vault。

**解决**：

1. 检查 `%APPDATA%\PwdVault\` 目录，删除 `vault.meta` 与 `vault.db` 后重试。
2. 端到端集成测试 (`test_e2e_flow`) 使用 `std::filesystem::temp_directory_path()`
   下的独立 meta 文件，每个用例独立，不应出现此问题；若出现请检查 `TearDown()` 是否执行。

### Q7：CMake 配置警告 "Could not find ISCC"

**原因**：未安装 Inno Setup，或 `iscc.exe` 不在标准路径。

**解决**：

1. 安装 [Inno Setup 6](https://jrsoftware.org/isinfo.php)。
2. 若已安装到非标准路径，可在 CMake 配置时显式指定：
   ```powershell
   cmake -B build -S . -DISCC_EXECUTABLE="D:\Tools\InnoSetup6\iscc.exe" `
       -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
   ```
3. 若不打算生成安装包，可忽略此警告，不影响 `pwdvault-ui.exe` 与 `pwdvault-service.exe`
   的构建与运行。

## 9. CI / 自动化构建（参考）

建议在 CI 中使用以下命令序列（GitHub Actions / Azure Pipelines 通用）：

```powershell
# 1. 配置
cmake -B build -S . `
    -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
    -DCMAKE_PREFIX_PATH="$env:QT_PREFIX_PATH" `
    -DCMAKE_BUILD_TYPE=Release

# 2. 构建
cmake --build build --config Release

# 3. 测试
ctest --test-dir build -C Release --output-on-failure

# 4. 安装（可选）
cmake --install build --config Release --prefix build/install

# 5. 打包（可选）
cmake --build build --target package_inno
```

## 10. 相关文档

- [README.md](../README.md)：项目概览与快速开始
- [ARCHITECTURE.md](ARCHITECTURE.md)：架构设计与模块分层
- [IPC_PROTOCOL.md](IPC_PROTOCOL.md)：IPC 协议帧格式与命令列表
- [SECURITY.md](SECURITY.md)：威胁模型与加密方案
