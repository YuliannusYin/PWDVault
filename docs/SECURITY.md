# PwdVault 安全设计

本文档描述 PwdVault 的安全设计：威胁模型、加密方案、密钥层次、敏感数据保护与已知限制。
架构与密钥派生流程请参见 [ARCHITECTURE.md](ARCHITECTURE.md)；IPC 协议请参见
[IPC_PROTOCOL.md](IPC_PROTOCOL.md)。

## 1. 威胁模型

PwdVault 假设以下威胁场景，并针对每种威胁给出缓解措施：

### 1.1 本地攻击者（普通用户权限）

**威胁**：攻击者以同一台机器上普通用户身份运行，尝试读取其他用户的密码数据。

**缓解**：
- 加密模式下 `vault.db` 中 `password` 字段以 AES-256-GCM 加密存储，攻击者即使获取数据库
  文件也无法解密。
- `vault.meta` 中的 `encryption_key` 用 KEK 加密；KEK 由用户程序密码经 Argon2id 派生，无法
  在不知程序密码情况下恢复。
- Windows 用户隔离：`%APPDATA%\PwdVault\` 默认位于用户配置文件目录下，NTFS ACL 限制其他
  用户访问。

> **明文模式风险**：未启用程序密码时 `vault.db` 中 password 字段为明文存储，任何能读取该
> 文件的攻击者均可直接获取密码。明文模式仅适用于低敏感场景，建议启用程序密码。

### 1.2 内存取证

**威胁**：攻击者获取进程内存快照（通过 dump、调试器附加、内存镜像）以提取明文密码或
加密密钥。

**缓解**：
- **敏感数据清零**：`encryption_key`、KEK、派生密钥用 `sodium_memzero` 显式清零，避免残留。
- **生命周期最小化**：KEK 仅在 `ProgramPasswordStore::initialize / unlock / change_password`
  函数栈上存活，函数返回前清零；`encryption_key` 在 service 进程的 `ServiceCore` 成员中存活，
  但 `lock()` 或进程退出立即清零。
- **进程隔离**：UI 进程不持有 `encryption_key`，仅通过 IPC 请求 service 解密；UI 崩溃不会
  泄露加密密钥。

### 1.3 文件窃取

**威胁**：攻击者获取 `vault.db` 与 `vault.meta` 文件副本（如备份、共享目录）。

**缓解**：
- 加密模式下，攻击者需同时拥有 `vault.db`（含加密条目）与 `vault.meta`（含 salt + 加密的
  encryption_key）才能尝试离线破解。
- Argon2id 提供计算与内存硬度，使暴力破解每秒尝试次数极低（INTERACTIVE 参数约 1-5 次/秒）。
- AES-256-GCM 的认证 tag 防止篡改：任何对密文或 meta 文件的修改都会导致 tag 校验失败。
- 明文模式下 `vault.meta` 不存在，仅 `vault.db` 即可读取所有数据 —— 不提供文件窃取保护。

### 1.4 暴力破解程序密码

**威胁**：攻击者通过反复尝试程序密码（在线或离线）破解 vault。

**缓解**：
- **在线限速**：service 进程维护解锁失败计数器，连续 5 次失败后锁定 5 分钟。
- **Argon2id 离线硬度**：即使攻击者获取 `vault.meta`，每次离线尝试仍需消耗可观的 CPU
  与内存（INTERACTIVE 参数：ops ≈ 2-4，mem ≈ 64 MB），有效降低暴力破解速率。
- **建议用户使用强程序密码**：UI 在启用 / 修改程序密码时给出强度提示，鼓励使用 12+ 字符
  混合密码。
- **明文模式下本威胁不存在**：未启用程序密码即无密码可破解，但也意味着无任何访问控制。

## 2. 加密方案

### 2.1 算法选型

| 用途                | 算法            | 实现         | 参数                                |
|---------------------|-----------------|--------------|-------------------------------------|
| 对称加密            | AES-256-GCM     | OpenSSL EVP  | 256-bit key, 96-bit IV, 128-bit tag |
| 密钥派生（KDF）     | Argon2id        | libsodium    | 见下表                              |
| 安全随机数          | `randombytes_buf` | libsodium  | 基于 OS CSPRNG                       |
| 安全内存清零        | `sodium_memzero` | libsodium   | 防止编译器优化消除                   |
| 常量时间比较        | `sodium_memcmp` | libsodium    | 避免时序侧信道                       |

### 2.2 Argon2id 参数

采用 libsodium 的 `crypto_pwhash_argon2id` INTERACTIVE 预设：

| 参数             | 值                                | 说明                              |
|------------------|-----------------------------------|-----------------------------------|
| `ops_limit`      | `crypto_pwhash_OPSLIMIT_INTERACTIVE` | 迭代次数（≈2-4）               |
| `mem_limit`      | `crypto_pwhash_MEMLIMIT_INTERACTIVE` | 内存限制（≈64 MB）              |
| `salt`           | 16 字节随机数                     | 由 `randombytes_buf` 生成，持久化于 `vault.meta` |
| `algorithm`      | `crypto_pwhash_ALG_ARGON2ID13`   | Argon2id v1.3                    |
| 输出长度         | 32 字节                           | 用于 AES-256 加密密钥              |

> **参数选型理由**：INTERACTIVE 预设面向交互式场景（用户解锁），约 0.1-0.5 秒/次派生，
> 既提供合理硬度，又不致用户等待过久。生产环境若需更强保护可改用
> `crypto_pwhash_OPSLIMIT_SENSITIVE` / `MEMLIMIT_SENSITIVE`（≈1-3 秒/次）。

### 2.3 AES-256-GCM 加密流程

`CryptoEngine::encrypt(plaintext, associated_data)` 返回的字节序列布局：

```
[IV(12) || ciphertext(N) || tag(16)]
```

- **IV**：12 字节随机数，每次加密由 `randombytes_buf` 重新生成。AES-GCM 推荐使用 96-bit IV。
- **ciphertext**：与明文等长的密文，GCM 流密码模式无填充。
- **tag**：16 字节 GCM 认证 tag，覆盖 ciphertext 与 associated_data。
- **associated_data (AAD)**：当前为空（保留扩展），可用于未来绑定元数据如 entry id。

**解密时**：传入相同字节序列，OpenSSL EVP 内部校验 tag，失败则返回 `CryptoError`。

## 3. 密钥层次

加密模式下采用三层派生结构，最大限度保护用户数据：

```
用户程序密码 (program_password)
    │
    │  Argon2id(password, salt) → 32B
    │  (salt 持久化于 vault.meta)
    ▼
KEK (Key Encryption Key, 32 字节)
    │  仅内存，函数返回前 sodium_memzero 清零
    │  用于加解密 encryption_key
    │
    │  AES-256-GCM(KEK, encryption_key)
    ▼
encryption_key (32 字节, entry 加密用对称密钥)
    │  在 service 进程内存中存活（unlock → lock 期间）
    │  持久化为加密 blob 于 vault.meta
    │  lock() / 进程退出时 sodium_memzero 清零
    │  修改程序密码时仅重新包装本密钥，条目无需重新加密
    │
    │  AES-256-GCM(encryption_key, entry.password)
    │  每条 entry 独立 IV
    ▼
加密后的 entry.password (持久化于 vault.db)
```

明文模式下不生成 `vault.meta`，无 KEK / encryption_key 派生过程，`entry.password` 直接以
明文存储于 `vault.db`（iv/tag 列为空 BLOB）。

详见 [ARCHITECTURE.md §5 程序密码与密钥层次结构](ARCHITECTURE.md#5-程序密码与密钥层次结构)。

## 4. 敏感数据清零

PwdVault 在所有退出路径上对敏感数据执行 `sodium_memzero` 清零，避免内存残留：

| 数据                | 清零位置                                                 | 说明                                       |
|---------------------|----------------------------------------------------------|--------------------------------------------|
| KEK (32B)           | `ProgramPasswordStore::initialize / unlock / change_password` 末尾（RAII KekZeroer）| 函数返回前清零，覆盖所有退出路径 |
| encryption_key (32B) | `ServiceCore::clear_encryption_key()`，析构时调用         | `lock()` / 进程退出时清零                  |
| `entry_crypto_`    | `ServiceCore::clear_encryption_key()` 中 `entry_crypto_.reset()` | unique_ptr 析构触发 CryptoEngine 析构，内部 memzero |
| `*Request.password` | `handle_estimate_strength` 末尾（`secure_zero_string`）  | 评估强度后清零请求中的密码字符串             |
| CryptoEngine::encryption_key_ | 析构函数                                                  | 引擎持有的 encryption_key 在析构时清零      |

> **sodium_memzero 优势**：相比 `memset`，`sodium_memzero` 内部使用 volatile 指针与编译
> 屏障，可防止编译器优化消除清零操作（C/C++ 标准允许编译器在认为「无后续读取」时省略
> memset 调用）。

## 5. 常量时间比较

`CryptoEngine::verify_password(password, salt, expected_hash)` 使用 `sodium_memcmp` 进行
哈希比较，避免时序侧信道攻击：

```cpp
bool CryptoEngine::verify_password(const std::string& password,
                                    core::ByteSpan salt,
                                    core::ByteSpan expected_hash) {
    auto derived = derive_key(password, salt);
    if (!derived) return false;
    if (derived->size() != expected_hash.size()) return false;
    return sodium_memcmp(derived->data(), expected_hash.data(), derived->size()) == 0;
}
```

**注意**：当前 PwdVault 程序密码验证实际通过 GCM tag 校验失败间接判断（解密
encryption_key 时 tag 校验失败 → `Unauthorized`），未直接调用 `verify_password`。该接口
保留供未来扩展（如独立的程序密码哈希文件）。

## 6. 限制与速率控制

### 6.1 程序密码解锁失败锁定

`ServiceCore` 在加密模式下维护解锁失败计数器（明文模式下 unlock 永远成功，不触发计数）：

| 规则                  | 实现                                                            |
|-----------------------|-----------------------------------------------------------------|
| 最大连续失败次数      | 5 次（`kMaxLoginAttempts = 5`）                                 |
| 锁定时长              | 5 分钟（`kLockoutDuration = std::chrono::minutes(5)`）          |
| 冷却期检查            | `is_in_cooldown()` 比较 `lock_until_` 与 `steady_clock::now()` |
| 成功解锁后            | `login_attempts_ = 0`，清零计数器                              |
| 冷却期内任何 unlock   | 直接返回 `success=false`，错误信息 `"too many failed attempts"`|

详见 `src/service/ServiceCore.cpp` 中的 `handle_unlock` 实现。

### 6.2 服务进程自动退出

为缩小敏感数据存活窗口，service 进程在无客户端活动 30 秒后自动退出：

```cpp
constexpr auto kKeepaliveTimeout = std::chrono::seconds(30);

// service main.cpp 主循环
while (!g_should_exit.load()) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    if (server.active_client_count() == 0) {
        auto elapsed = std::chrono::steady_clock::now() - server.last_activity();
        if (elapsed > kKeepaliveTimeout) {
            log_line("INFO", "No client activity for 30 seconds, exiting");
            break;
        }
    }
}
```

进程退出时 `ServiceCore` 析构 → `clear_encryption_key()` → `encryption_key` 与
`entry_crypto_` 被 `sodium_memzero` 清零。

### 6.3 请求超时

- **客户端**：`IpcClient` 每个请求最多等待 10 秒，超时返回 `IpcError`。
- **服务端**：`IpcServer::client_loop` 区分读写超时策略：
  - **读取**：使用 `INFINITE` 超时。命名管道在客户端进程退出时立即产生
    `ERROR_BROKEN_PIPE`，无需空闲超时检测死连接；空闲超时反而会断开正常的
    空闲长连接（如用户查看密码时 UI 30 秒内不发请求）。
  - **写入**：30 秒超时，防止客户端不读取响应卡死工作线程。
  - 两者均通过 `WaitForMultipleObjects` 同时等待 I/O 事件与停止事件，
    `stop()` 可经 `CancelIoEx` 唤醒任何阻塞的 I/O。

## 7. 已知限制

PwdVault 的当前实现存在以下已知安全限制，未来版本可考虑改进：

### 7.1 未防御 Cold Boot Attack

**问题**：物理攻击者可通过冷启动攻击（cold boot attack）从 RAM 中提取残留的
`encryption_key` 或 KEK，即使已 `sodium_memzero`，仍可能在断电瞬间残留。

**缓解**：内存清零仅在进程主动调用时生效，无法防御物理内存取证。建议合盖休眠前主动 `lock()`。

### 7.2 未使用安全区（SGX / TPM）

**问题**：未利用 Intel SGX enclave 或 TPM 2.0 硬件隔离 `encryption_key`。

**影响**：
- 同机管理员权限的攻击者可附加调试器读取 service 进程内存中的 `encryption_key`。
- 无法防御内核态恶意软件。

**未来方向**：考虑使用 Windows DPAPI 包装 `encryption_key`，或集成 TPM 2.0 进行密钥密封。

### 7.3 未防御 IPC 嗅探

**问题**：UI 与 service 间的命名管道通信未加密，本机任何拥有 `SeDebugPrivilege` 的进程
可附加 service 或 UI 进程读取管道内容。

**缓解**：
- 命名管道通过 Windows ACL 限制为当前用户 SID 访问（当前实现未显式设置，默认继承进程
  安全描述符）。
- 用户程序密码在 IPC 中传输时为明文，但仅在本机进程间，且 service 立即用 sodium_memzero
  清零请求中的密码字符串。

**未来方向**：可考虑使用 `ConvertSecurityDescriptorToStringSecurityDescriptor` 显式设置
命名管道 ACL，仅允许当前用户 SID。

### 7.4 程序密码复杂度未强制校验

**问题**：`EnableProgramPassword` / `ChangeProgramPassword` 时未强制要求程序密码满足最小
复杂度（长度、字符集）。

**影响**：用户可设置弱程序密码（如 "123"），降低离线破解门槛。

**未来方向**：UI 在 `ProgramPasswordDialog` 中调用 `estimate_strength` 并给出提示（已部分
实现，仅显示强度条但未阻断）；service 端可加入最小强度校验（如拒绝 strength < 50 bits 的
程序密码）。

### 7.5 无多因素认证

**问题**：当前仅依赖程序密码单因素认证，未集成 TOTP / 硬件密钥（YubiKey 等）。

**未来方向**：可在 `vault.meta` 中追加第二因子字段（如 TOTP secret），unlock 时除程序密码
外要求额外输入。

### 7.6 无密码历史与撤销

**问题**：当前未保存 entry 历史版本，`update_entry` 直接覆盖。误删 / 误改后无法恢复。

**未来方向**：在 `vault.db` 中追加 `entry_history` 表，每次更新前归档旧版本。

## 8. 安全相关代码索引

| 关注点              | 源文件                                                       |
|---------------------|--------------------------------------------------------------|
| AES-256-GCM 实现    | `src/sdk/crypto/CryptoEngine.cpp`                            |
| Argon2id 派生       | `src/sdk/crypto/CryptoEngine.cpp` (`derive_key`)            |
| encryption_key 加解密 | `src/service/ProgramPasswordStore.cpp` (`initialize` / `unlock` / `change_password` / `destroy`) |
| entry 加解密        | `src/service/ServiceCore.cpp` (`encrypt_entry` / `decrypt_entry`) |
| 模式切换            | `src/service/ServiceCore.cpp` (`handle_enable_program_password` / `handle_disable_program_password` / `handle_change_program_password`) |
| 敏感数据清零        | `src/service/ServiceCore.cpp` (`secure_zero` / `secure_zero_string`)<br>`src/service/ProgramPasswordStore.cpp` (`KekZeroer` RAII)<br>`src/sdk/crypto/CryptoEngine.cpp`（析构 `sodium_memzero`） |
| 解锁限速            | `src/service/ServiceCore.cpp` (`handle_unlock` / `is_in_cooldown`) |
| 自动退出            | `src/service/main.cpp`（`kKeepaliveTimeout` 主循环）         |
| 常量时间比较        | `src/sdk/crypto/CryptoEngine.cpp` (`verify_password`)       |

## 9. 安全审计建议

如需对 PwdVault 进行安全审计，建议关注以下要点：

1. **密钥派生参数**：检查 `CryptoEngine::derive_key` 是否使用了正确的 Argon2id 参数与
   salt 长度。
2. **IV 生成**：确认每次 `encrypt` 调用都重新生成 12 字节随机 IV（不允许 IV 重用）。
3. **GCM tag 校验**：确认 `decrypt` 在 tag 校验失败时返回错误，且不返回部分明文。
4. **内存清零完整性**：审查所有持有 `encryption_key` / KEK / 明文密码的代码路径，确认所有
   退出路径（含异常 / 早返回）都执行清零。
5. **解锁限速绕过**：检查 `login_attempts_` 与 `lock_until_` 是否在所有失败路径上正确
   更新，无绕过。
6. **模式切换原子性**：检查 `EnableProgramPassword` / `DisableProgramPassword` 在批量
   重加密 / 重解密过程中的事务边界与回滚路径，确认中途失败不会留下混合明文 / 密文状态。
7. **IPC 安全**：检查命名管道 ACL，确认仅当前用户可访问。
8. **错误信息泄露**：检查错误响应是否泄露敏感信息（如「密码错误」与「用户不存在」
   的区分可能用于枚举）。
9. **明文模式残留**：禁用程序密码后确认 `vault.meta` 已删除，且所有条目 iv/tag 列已清空。

## 10. 相关文档

- [ARCHITECTURE.md](ARCHITECTURE.md)：架构与密钥派生流程
- [IPC_PROTOCOL.md](IPC_PROTOCOL.md)：IPC 协议与错误响应
- [BUILD.md](BUILD.md)：构建与运行
- [../README.md](../README.md)：项目概览
