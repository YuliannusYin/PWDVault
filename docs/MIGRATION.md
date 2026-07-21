# PwdVault 旧版数据迁移指南

PwdVault 从 Python + Tkinter 重构为 C++ + Qt 6 后，数据格式与旧版**不兼容**。
本文档说明旧版数据的存放位置，以及如何使用 `pwdvault-migrate.exe` 工具将旧库
迁移到新版存储格式。

迁移工具支持两种目标模式：
- **加密模式**（默认）：启用程序密码，password 字段经 AES-256-GCM 加密存储，
  生成 `vault.meta`。
- **明文模式**（`--no-password`）：不启用程序密码，password 字段以明文存储，
  iv/tag 列为空 BLOB，不生成 `vault.meta`。后续可在 UI 设置中启用程序密码。

> **适用场景**：仅当你之前安装过旧版 Python 实现的 PasswordManager 并存有数据时
> 才需要执行迁移。全新安装的用户**不需要**运行此工具。

---

## 1. 旧版数据位置

旧版（Python）数据存放在：

```
%APPDATA%\PasswordManager\
├── passwords.db   # SQLite 数据库（明文存储 schema；password 字段为 Fernet 密文）
├── key.key        # Fernet 对称密钥（URL-safe base64 编码的 32 字节）
├── config.json    # 旧版配置文件（迁移工具不处理）
└── password_history.json  # 旧版密码历史（迁移工具不处理）
```

旧库 schema：

```sql
CREATE TABLE passwords (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    number       INTEGER,
    website      TEXT NOT NULL,
    username     TEXT NOT NULL,
    password     TEXT NOT NULL,   -- Fernet 加密后的密文（base64 token）
    note         TEXT,
    sensitivity  INTEGER DEFAULT 0,
    related_info TEXT
);
```

旧版 `key.key` 文件内容为 Fernet 密钥的 URL-safe base64 文本（44 字符含一个
padding '='，对应 32 字节原始密钥）。

---

## 2. 新版数据位置

新版（C++ / Qt 6）数据存放在：

```
%APPDATA%\PwdVault\
├── vault.db   # SQLite 数据库
│              #   加密模式：password=AES-256-GCM 密文 + iv + tag
│              #   明文模式：password=明文 / iv=空 / tag=空
└── vault.meta # 仅加密模式存在：Argon2id salt + 加密的 encryption_key
```

加密模式下，新版使用 Argon2id 从程序密码派生 KEK，再加密随机生成的 32 字节
encryption_key；encryption_key 用于 AES-256-GCM 加密每条记录的 password 字段。
详见 [SECURITY.md](SECURITY.md)。

---

## 3. 迁移工具使用方法

### 3.1 基本用法

打开 PowerShell 或 cmd，执行：

```powershell
# 加密模式（默认）：迁移后启用程序密码保护
pwdvault-migrate.exe --program-password=YourNewProgramPassword

# 明文模式：迁移后不启用程序密码，password 字段以明文存储
pwdvault-migrate.exe --no-password
```

工具会自动从默认位置读取旧库与旧密钥，并写入新库到默认位置。
若不传 `--program-password` 也未指定 `--no-password`，工具会在控制台交互式
提示输入两次程序密码（不回显）。

> **向后兼容**：旧版参数名 `--master-password=` 仍被接受，等价于
> `--program-password=`。

### 3.2 命令行参数

| 参数                          | 说明                                                         | 默认值                                       |
|-------------------------------|--------------------------------------------------------------|----------------------------------------------|
| `--old-db=<path>`             | 旧 `passwords.db` 路径                                       | `%APPDATA%\PasswordManager\passwords.db`     |
| `--old-key=<path>`            | 旧 `key.key` 路径                                            | `%APPDATA%\PasswordManager\key.key`          |
| `--new-db=<path>`             | 新 `vault.db` 路径                                           | `%APPDATA%\PwdVault\vault.db`                |
| `--program-password=<pw>`     | 新程序密码（不传则交互式输入；明文模式下忽略）              | —（交互式输入）                              |
| `--no-password`               | 迁移为明文模式（不启用程序密码，无 vault.meta）             | —                                            |
| `--dry-run`                   | 仅统计将迁移的条目数，不写入新库                             | —                                            |
| `--help`, `-h`                | 显示帮助并退出                                               | —                                            |

> `--program-password` 与 `--no-password` 互斥；同时指定会报错退出。

### 3.3 推荐流程

1. **先备份**：将整个 `%APPDATA%\PasswordManager\` 目录复制到其他位置作为备份。
2. **dry-run 预检**：
   ```powershell
   pwdvault-migrate.exe --dry-run
   ```
   确认能正确读取旧库与旧密钥，并查看条目数量。
3. **正式迁移**：
   ```powershell
   # 加密模式（推荐）
   pwdvault-migrate.exe
   ```
   按提示输入新的程序密码（建议使用强密码）。
   或选择明文模式：
   ```powershell
   pwdvault-migrate.exe --no-password
   ```
4. **验证新库**：启动 `pwdvault-ui.exe`：
   - 加密模式：用刚设置的程序密码解锁，确认所有条目都能正确显示且密码能解密。
   - 明文模式：直接进入主界面（无需密码），确认所有条目都能正确显示。
5. **清理旧文件**：确认数据无误后，**手动删除** `%APPDATA%\PasswordManager\`
   目录（详见第 5 节）。

### 3.4 退出码

| 退出码 | 含义                                                         |
|--------|--------------------------------------------------------------|
| `0`    | 迁移成功（全部条目均已写入）                                |
| `1`    | 参数错误 / 文件缺失 / 不可恢复的失败                        |
| `2`    | 部分条目迁移失败（成功部分已提交；请查看日志）              |

---

## 4. 迁移流程详解

`pwdvault-migrate.exe` 内部执行以下步骤：

1. **参数解析**：解析命令行，确定旧库、旧密钥、新库路径、目标模式（加密 / 明文）、
   程序密码来源。
2. **存在性检查**：确认旧库与旧密钥文件存在；若目标 `vault.meta` 已存在则报错
   退出（避免覆盖已初始化的库）。
3. **读取旧密钥**：从 `key.key` 读取 URL-safe base64 文本，解码为 32 字节
   原始密钥，拆分为 `signing_key`（前 16 字节，HMAC-SHA256 用）与
   `encryption_key`（后 16 字节，AES-128-CBC 用）。
4. **打开旧库**：以只读方式打开 `passwords.db`，执行 `SELECT id, website,
   username, password, note FROM passwords`，读取全部记录。
5. **初始化新库密钥存储**（仅加密模式）：
   - 创建新数据目录 `%APPDATA%\PwdVault\`（若不存在）。
   - 构造 `ProgramPasswordStore`，调用 `initialize(program_password)`：
     生成 16 字节 salt、用 Argon2id 派生 KEK、生成 32 字节 encryption_key、
     用 AES-256-GCM 加密 encryption_key 后写入 `vault.meta`。
   - 明文模式下跳过此步骤，不生成 `vault.meta`。
6. **创建 StorageEngine**：打开（或创建）`vault.db`，自动建表与索引。
7. **逐条迁移**（在单个 SQLite 事务内）：
   - 用 `FernetDecoder` 解密旧 `password` 字段（HMAC 校验 + AES-128-CBC + PKCS7）。
   - 构造 `PasswordEntry{website, username, password(明文), note}`。
   - 加密模式：用新版 `CryptoEngine`（以 encryption_key 构造）对明文密码执行
     AES-256-GCM 加密，得到 `[IV(12) || ciphertext || tag(16)]`，拆分填充到
     `entry.password / iv / tag`。
   - 明文模式：password 保持明文，iv / tag 留空。
   - 调用 `storage.add_entry()` 写入。
8. **提交事务**：所有成功条目一次性提交；若全部失败则回滚。
9. **打印统计**：输出总数、成功数、失败数、目标模式（加密 / 明文），以及旧文件
   位置提示。

---

## 5. 迁移后处理

迁移工具**不会自动删除旧文件**。请按以下步骤操作：

1. 启动 `pwdvault-ui.exe`：
   - 加密模式：用迁移时设置的程序密码解锁。
   - 明文模式：直接进入主界面（无需密码）。
2. 逐条核对已迁移的密码条目（重点检查网站、用户名、密码能否正确显示）。
3. 确认无误后，手动删除整个旧数据目录：
   ```powershell
   Remove-Item -Recurse -Force "$env:APPDATA\PasswordManager"
   ```
   > **注意**：删除前请确保已备份。该目录包含旧 Fernet 密钥 `key.key`，
   > 一旦删除，旧版密文将无法再被解密。

> **明文模式后续启用程序密码**：迁移到明文模式后，可随时在 UI「设置」页面
> 点击「启用程序密码」按钮，将所有明文条目重新加密并切换到加密模式。

---

## 6. 故障排查

### 6.1 报错："Legacy database not found"

旧库不存在于默认路径。可能原因：

- 从未安装过旧版 PasswordManager（无需迁移）。
- 旧库被移动到其他位置：通过 `--old-db=<path>` 显式指定。

### 6.2 报错："Legacy key file not found"

旧 Fernet 密钥不存在。可能原因：

- 密钥被移动或删除：从备份恢复 `key.key` 后重试。
- 通过 `--old-key=<path>` 显式指定其他位置。

### 6.3 报错："Failed to parse legacy Fernet key"

旧 `key.key` 文件内容不是合法的 32 字节 Fernet 密钥（URL-safe base64）。可能
原因：

- 文件被损坏或被其他工具改写过。
- 文件实际是其他格式（如 PEM 私钥）。请确认是旧版 PasswordManager 生成的。

### 6.4 报错："HMAC verification failed"（单条记录）

某条记录的 Fernet token HMAC 校验失败。可能原因：

- 该条记录是其他 Fernet 密钥加密的（旧版曾经重置过密钥）。
- `password` 字段在数据库中被手工篡改过。

工具会跳过该条记录并继续处理其他记录，迁移结束后查看日志中失败条目的 id
与 website。

### 6.5 报错："Target vault.meta already exists"

新库的 `%APPDATA%\PwdVault\vault.meta` 已存在，工具拒绝覆盖已初始化的库
以防止数据丢失。处理方式：

- 若新库是空的或无效，可手动删除 `vault.meta` 与 `vault.db` 后重新迁移：
  ```powershell
  Remove-Item "$env:APPDATA\PwdVault\vault.meta"
  Remove-Item "$env:APPDATA\PwdVault\vault.db"
  ```
- 若新库已有有效数据，请使用不同的 `--new-db=<path>` 指定其他位置。

> 明文模式下若 `vault.meta` 已存在也会报此错（明文模式不应存在 `vault.meta`）。
> 按上述方式删除后重试。

### 6.6 报错："Program password must not be empty"

未提供程序密码或两次输入不一致。重新运行工具并输入合法程序密码。
若希望迁移为明文模式，请显式使用 `--no-password`。

### 6.7 报错："--no-password and --program-password are mutually exclusive"

同时指定了 `--no-password` 与 `--program-password`，二者互斥。请二选一：
- 仅 `--program-password=<pw>`（或交互式输入）→ 加密模式
- 仅 `--no-password` → 明文模式

### 6.8 报错："begin_transaction / commit_transaction failed"

新库 SQLite I/O 错误。可能原因：

- 磁盘空间不足。
- 新库路径所在目录无写权限。
- `vault.db` 被其他进程（如 `pwdvault-service.exe`）独占锁定。

请关闭所有 PwdVault 相关进程后重试。

### 6.9 中文显示乱码

旧库中 `website` / `username` / `note` 字段在旧版 Python 中以 UTF-8 存储；
迁移工具按 UTF-8 读取与写入。若控制台显示乱码，是终端代码页问题，不影响
数据正确性。可在 PowerShell 中执行 `chcp 65001` 切换到 UTF-8 代码页后再运行
工具。

---

## 7. 安全说明

- 加密模式下，迁移过程中旧 Fernet 密钥与程序密码在内存中以明文短暂存在；工具退出
  时会通过 `sodium_memzero` 清零相关缓冲区。明文模式下不涉及程序密码，仅短暂
  存在旧 Fernet 密钥。
- HMAC 比较使用 `CRYPTO_memcmp` 常量时间比较，避免时序侧信道。
- 迁移工具不写日志文件，不在磁盘上留存明文密码。
- 旧 `key.key` 与旧 `passwords.db` 在迁移过程中**只读**，工具不会修改它们。
- **明文模式风险提示**：明文模式下 `vault.db` 中 password 字段以明文存储，任何能
  读取该文件的攻击者均可直接获取密码。仅适用于低敏感场景，建议尽快在 UI 设置中
  启用程序密码切换到加密模式。
