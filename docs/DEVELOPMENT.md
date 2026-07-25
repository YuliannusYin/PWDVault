# PwdVault 开发流程指南

本文档面向 PwdVault 的人类开发者与贡献者，描述开发协作流程、Git 工作流、提交规范与发布流程。环境搭建与构建命令请参阅 [BUILD.md](BUILD.md)，架构设计请参阅 [ARCHITECTURE.md](ARCHITECTURE.md)。

---

## 1. Git 工作流

### 1.1 分支策略

采用简化的 GitHub Flow：

| 分支          | 用途                     | 保护规则           |
| ------------- | ------------------------ | ------------------ |
| `main`        | 稳定发布分支，始终可构建 | 禁止直接推送，需 PR |
| `develop`     | 日常集成分支（可选）     | 需 PR              |
| `feat/<name>` | 新功能开发分支           | 无                 |
| `fix/<name>`  | Bug 修复分支             | 无                 |
| `docs/<name>` | 文档更新分支             | 无                 |

分支命名示例：
- `feat/import-from-1password`
- `fix/login-lockout-bypass`
- `docs/update-security-guide`

### 1.2 开发流程

1. 从 `main`（或 `develop`）拉取最新代码：
   ```powershell
   git checkout main
   git pull origin main
   ```
2. 创建特性分支：
   ```powershell
   git checkout -b feat/your-feature
   ```
3. 开发并提交（遵循下文提交规范）
4. 推送分支并发起 Pull Request：
   ```powershell
   git push -u origin feat/your-feature
   ```
5. PR 通过代码审查后合并到 `main`
6. 删除已合并的分支

---

## 2. 提交规范

使用 [Conventional Commits](https://www.conventionalcommits.org/) 规范：

```
<type>(<scope>): <subject>

<body>

<footer>
```

### 2.1 type 类型

| type      | 说明                                     |
| --------- | ---------------------------------------- |
| `feat`    | 新功能                                   |
| `fix`     | Bug 修复                                 |
| `refactor`| 重构（不改变外部行为）                   |
| `docs`    | 文档更新                                 |
| `test`    | 新增或修改测试                           |
| `chore`   | 构建、工具、依赖等杂项                   |
| `perf`    | 性能优化                                 |
| `style`   | 代码格式（不影响功能）                   |
| `ci`      | CI 配置变更                              |
| `revert`  | 回滚之前的提交                           |

### 2.2 scope 范围

| scope      | 对应模块                            |
| ---------- | ----------------------------------- |
| `sdk`      | SDK 通用（跨子模块）                |
| `core`     | `src/sdk/core/`                     |
| `crypto`   | `src/sdk/crypto/`                   |
| `storage`  | `src/sdk/storage/`                  |
| `generator`| `src/sdk/generator/`                |
| `protocol` | `src/sdk/protocol/`                 |
| `service`  | `src/service/`                      |
| `ui`       | `src/ui/`                           |
| `migrate`  | `src/migrate/`                      |
| `build`    | CMake、vcpkg、打包脚本              |
| `docs`     | `docs/`、README、AGENTS             |
| `test`     | `tests/`                            |

### 2.3 subject 要求

- 使用祈使句（如 `add` 而非 `added` 或 `adds`）
- 首字母小写
- 结尾不加句号
- 不超过 72 字符

### 2.4 body 与 footer

- **body**：解释「为什么」做这个改动，而非「做了什么」（代码本身能说明做什么）。每行不超过 80 字符。
- **footer**：用于标注 BREAKING CHANGE 或关联 issue：
  ```
  BREAKING CHANGE: IPC 协议版本升至 2，旧 UI 不兼容
  Closes #42
  ```

### 2.5 示例

```
feat(ui): add password strength meter in LoginView

实时调用 estimate_strength IPC 显示强度条，颜色按 <40/40-80/>80 bits
分为红/黄/绿。用户输入主密码时即可感知强度，避免设置弱密码。

Closes #15
```

```
fix(crypto): clear derived key on verify_password failure

之前验证失败路径未清零派生密钥，可能导致敏感数据残留内存。
改用 RAII KekZeroer 确保所有退出路径都执行 sodium_memzero。
```

```
refactor(service): extract entry encrypt/decrypt to helper

ServiceCore 中 add_entry/update_entry 的加密逻辑重复，
抽取为 encrypt_entry/decrypt_entry 私有方法。
```

---

## 3. Pull Request 规范

### 3.1 PR 标题

遵循与提交相同的 Conventional Commits 格式。

### 3.2 PR 描述模板

```markdown
## 改动说明
<!-- 简要描述本次改动做了什么、为什么 -->

## 改动类型
- [ ] 新功能（feat）
- [ ] Bug 修复（fix）
- [ ] 重构（refactor）
- [ ] 文档（docs）
- [ ] 测试（test）
- [ ] 构建/工具（chore）

## 测试
- [ ] 已运行 `ctest --test-dir build --output-on-failure` 全部通过
- [ ] 已添加/更新对应单元测试
- [ ] 已添加端到端集成测试（涉及 IPC 命令时）

## 检查清单
- [ ] 代码遵循项目命名与风格约定
- [ ] 敏感数据已用 sodium_memzero 清零
- [ ] 错误处理使用 core::Result<T>
- [ ] 资源用 RAII 包装
- [ ] 已更新相关文档（docs/、README.md、AGENTS.md）
```

### 3.3 代码审查要点

审查者应关注：

1. **安全性**：敏感数据是否清零？比较是否常量时间？是否引入新的信任边界？
2. **接口稳定性**：是否修改了 `src/sdk/core/` 下的抽象接口？是否影响下游？
3. **测试覆盖**：是否覆盖错误路径与边界值？是否添加回归测试？
4. **文档同步**：用户可见行为变更是否更新文档？
5. **资源管理**：HANDLE / sqlite3* / EVP_CIPHER_CTX* 是否用 RAII？
6. **命名空间**：新增类型是否放入对应命名空间？

---

## 4. 版本管理

### 4.1 版本号规则

遵循 [Semantic Versioning](https://semver.org/)：`MAJOR.MINOR.PATCH`

- **MAJOR**：不兼容的 API 变更（如 IPC 协议版本升级、接口签名修改）
- **MINOR**：向后兼容的功能新增
- **PATCH**：向后兼容的 Bug 修复

当前版本：`3.1.0`（详见 [Releases](https://github.com/YuliannusYin/PWDVault/releases)）

### 4.2 版本号位置

发布新版本时需同步更新以下位置：

| 文件                              | 字段                                    |
| --------------------------------- | --------------------------------------- |
| `CMakeLists.txt`                  | `project(PwdVault VERSION ...)`         |
| `vcpkg.json`                      | `"version-string"`                      |
| `packaging/pwdvault.iss`          | `#define MyAppVersion`                  |
| `src/ui/views/SettingsView.cpp`   | `kAppVersion` 字符串                    |
| `src/ui/MainWindow.cpp`           | 顶栏副标题版本号                        |
| `docs/DEVELOPMENT.md`             | 本节「当前版本」                         |

---

## 5. 发布流程

### 5.1 发布前准备

1. 确认 `main` 分支处于绿色状态（CI 全部通过）
2. 更新版本号（见上节）
3. 更新 `docs/CHANGELOG.md`（若存在）或 Release Notes
4. 提交版本号变更：
   ```powershell
   git commit -m "chore: bump version to 3.1.0"
   ```

### 5.2 打 tag

```powershell
git tag -a v3.1.0 -m "Release v3.1.0"
git push origin v3.1.0
```

### 5.3 构建安装包

在 Windows 环境执行（需配置好 VS 2022、Qt 6、vcpkg、Inno Setup）：

```powershell
# 1. 全新配置
Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

# 2. 构建
cmake --build build --config Release

# 3. 测试
ctest --test-dir build -C Release --output-on-failure

# 4. 安装（让 windeployqt 部署 Qt 运行时）
cmake --install build --config Release --prefix build/install

# 5. 生成安装包
cmake --build build --target package_inno
```

输出：`build/package/pwdvault-3.1.0-setup.exe`

### 5.4 发布到 GitHub Releases

1. 在 GitHub 仓库页面点击「Releases」→「Draft a new release」
2. 选择刚推送的 tag（如 `v3.1.0`）
3. 填写 Release Title（如 `PwdVault 3.1.0`）
4. 在描述中粘贴 Release Notes
5. 上传 `pwdvault-3.1.0-setup.exe` 与 `pwdvault-3.1.0-portable.zip`（便携版）
6. 点击「Publish release」

### 5.5 发布后

1. 在 GitHub Issues 中关闭本次版本相关的 issue
2. 通知用户（如有社区渠道）
3. 开始下一个版本的开发

---

## 6. 依赖管理

### 6.1 新增依赖

新增第三方库依赖前需评估：

1. **许可证**：必须与 MIT 兼容（MIT、Apache-2.0、BSD、ISC 等）
2. **维护状态**：优先选择活跃维护的库
3. **体积**：评估对安装包大小的影响
4. **是否必须**：能否用现有依赖或少量自实现代码替代

流程：
1. 在 `vcpkg.json` 的 `dependencies` 数组追加库名
2. 在顶层 `CMakeLists.txt` 添加 `find_package(...)`
3. 在对应模块的 `CMakeLists.txt` 中 `target_link_libraries`
4. 更新 `docs/BUILD.md` 的依赖说明
5. 提交 PR 说明新增依赖的原因与评估结果

### 6.2 升级依赖

```powershell
# 更新 vcpkg 本地版本
cd $env:VCPKG_ROOT
git pull origin master
.\bootstrap-vcpkg.bat

# 删除 PwdVault build 目录强制重新拉取依赖
cd <PwdVault 根目录>
Remove-Item -Recurse -Force build
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
```

---

## 7. 相关文档

- [BUILD.md](BUILD.md)：环境搭建与构建命令
- [ARCHITECTURE.md](ARCHITECTURE.md)：架构设计与模块分层
- [IPC_PROTOCOL.md](IPC_PROTOCOL.md)：IPC 协议帧格式与命令列表
- [SECURITY.md](SECURITY.md)：威胁模型与加密方案
- [MIGRATION.md](MIGRATION.md)：旧数据迁移指南
- [README.md](../README.md)：终端用户文档
