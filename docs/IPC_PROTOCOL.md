# PwdVault IPC 协议说明

本文档定义 PwdVault UI 进程（`pwdvault-ui.exe`）与服务进程（`pwdvault-service.exe`）之间
通过命名管道传输的二进制 IPC 协议。架构与数据流请参见 [ARCHITECTURE.md](ARCHITECTURE.md)。

## 1. 传输层

### 1.1 命名管道

| 属性           | 值                              |
|----------------|---------------------------------|
| 管道名         | `\\.\pipe\PwdVaultService`      |
| 管道类型       | 字节流（PIPE_TYPE_BYTE）         |
| 读模式         | 字节流（PIPE_READMODE_BYTE）    |
| 等待模式       | OVERLAPPED                     |
| 实例数         | 多实例（`PIPE_UNLIMITED_INSTANCES`）|
| 缓冲区大小     | 64 KB（默认）                   |

UI 客户端通过 `CreateFile` 打开管道，建立连接后即可双向读写。

### 1.2 并发模型

- **服务端**：监听线程使用 `ConnectNamedPipe(overlapped)` 等待客户端连接，每个客户端
  连接由独立工作线程处理。最多支持 `PIPE_UNLIMITED_INSTANCES` 个并发客户端。
- **客户端**：同步阻塞调用，每个请求最多等待 10 秒。失败重试 3 次，间隔 500ms。

## 2. 消息帧格式

每条消息由 **16 字节固定头部** + **变长负载** 组成：

```
+--------------------------------------------------------------+
|                       消息帧 (frame)                          |
+--------------------------------------+-----------------------+
|         MessageHeader (16 字节)      |   Payload (N 字节)    |
+------+------+--------+---------+-----+-----------------------+
|magic | ver  | command|req_id  |size |                       |
| 4 B  | 2 B  | 2 B    | 4 B    | 4 B |                       |
+------+------+--------+---------+-----+-----------------------+
```

### 2.1 MessageHeader 字段

| 偏移 | 长度  | 字段           | 类型        | 说明                                       |
|------|-------|----------------|-------------|--------------------------------------------|
| 0    | 4     | magic          | `uint32_t`  | 魔数 `0x50564456`（"PDVV" 小端）           |
| 4    | 2     | version        | `uint16_t`  | 协议版本，当前为 `1`                        |
| 6    | 2     | command        | `uint16_t`  | `CommandId` 枚举值                          |
| 8    | 4     | request_id     | `uint32_t`  | 请求 ID，用于异步匹配请求与响应            |
| 12   | 4     | payload_size   | `uint32_t`  | 负载字节数（不含 header，可为 0）          |

> **静态断言**：`sizeof(MessageHeader) == 16`，由
> `src/sdk/protocol/Messages.h` 中的 `static_assert` 强制保证，便于按固定偏移解析。

### 2.2 字节序

统一使用 **小端序**（Little-Endian）。x86 / Windows on ARM 均为小端，可直接 `memcpy`。

### 2.3 序列化方案

采用 TLV（Type-Length-Value）风格的二进制序列化：

- **定长字段**（`u16` / `u32` / `u64` / `i64` / `bool` / `ErrorCode`）直接 `memcpy`。
- **变长字段**（`std::string` / `core::ByteVec` / `std::vector<T>`）以 `uint32_t` 长度前缀
  打头，紧接原始字节。
- **复合结构** 按字段声明顺序逐字段序列化，不写额外 tag。
- **空负载**：`PingRequest` / `LockRequest` / `ListEntriesRequest` 等无字段结构的请求，
  序列化结果为空 `ByteVec`，`payload_size = 0`。

实现见 `src/sdk/protocol/Serializer.h` 与 `Serializer.cpp`。

## 3. 命令列表

`CommandId` 枚举定义于 `src/sdk/protocol/Commands.h`，按高字节分组：

### 3.1 系统级（0x00xx）

| CommandId       | 值     | 名称             | 请求结构         | 响应结构          |
|-----------------|--------|------------------|------------------|-------------------|
| `Ping`          | 0x0001 | 心跳检测         | `PingRequest`    | `PingResponse`    |
| `Shutdown`      | 0x0002 | 通知 service 退出| `ShutdownRequest`| `ShutdownResponse`|

### 3.2 会话级（0x01xx）

| CommandId                | 值     | 名称                       | 请求结构                       | 响应结构                        |
|--------------------------|--------|----------------------------|--------------------------------|---------------------------------|
| `Unlock`                 | 0x0101 | 解锁（加密模式下验证程序密码）| `UnlockRequest`                | `UnlockResponse`                |
| `Lock`                   | 0x0102 | 主动锁定（仅加密模式可用） | `LockRequest`                  | `LockResponse`                  |
| `EnableProgramPassword`  | 0x0103 | 启用程序密码（明文→加密）  | `EnableProgramPasswordRequest` | `EnableProgramPasswordResponse` |
| `DisableProgramPassword` | 0x0104 | 禁用程序密码（加密→明文）  | `DisableProgramPasswordRequest`| `DisableProgramPasswordResponse`|
| `ChangeProgramPassword`  | 0x0105 | 修改程序密码               | `ChangeProgramPasswordRequest` | `ChangeProgramPasswordResponse` |
| `GetVaultStatus`         | 0x0106 | 查询 vault 状态            | `GetVaultStatusRequest`        | `GetVaultStatusResponse`        |

> **状态机说明**：service 启动时检测 `vault.meta` 是否存在：
> - 不存在 → 明文模式（`password_enabled=false`，自动 `unlocked=true`）
> - 存在   → 加密模式（`password_enabled=true`，`unlocked=false`，需 `Unlock`）
>
> `Enable/Disable/ChangeProgramPassword` 仅在对应模式下可用；明文模式下
> `Lock` 返回 `InvalidArgument`，`Unlock` 直接返回 `success=true`。

### 3.3 条目 CRUD（0x02xx）

| CommandId       | 值     | 名称         | 请求结构              | 响应结构                |
|-----------------|--------|--------------|-----------------------|-------------------------|
| `AddEntry`      | 0x0200 | 新增条目     | `AddEntryRequest`     | `AddEntryResponse`      |
| `UpdateEntry`   | 0x0201 | 更新条目     | `UpdateEntryRequest`   | `UpdateEntryResponse`   |
| `RemoveEntry`   | 0x0202 | 删除条目     | `RemoveEntryRequest`  | `RemoveEntryResponse`   |
| `GetEntry`      | 0x0203 | 获取单条     | `GetEntryRequest`     | `GetEntryResponse`      |
| `SearchEntries` | 0x0204 | 搜索条目     | `SearchEntriesRequest`| `SearchEntriesResponse`|
| `ListEntries`   | 0x0205 | 列出全部     | `ListEntriesRequest`  | `ListEntriesResponse`   |

### 3.4 密码生成与强度评估（0x03xx）

| CommandId                  | 值     | 名称                  | 请求结构                        | 响应结构                          |
|----------------------------|--------|-----------------------|---------------------------------|-----------------------------------|
| `GeneratePassword`         | 0x0300 | 生成密码              | `GeneratePasswordRequest`       | `GeneratePasswordResponse`        |
| `EstimateStrength`         | 0x0301 | 评估强度              | `EstimateStrengthRequest`       | `EstimateStrengthResponse`        |
| `ListGeneratedRecords`     | 0x0302 | 列出生成记录          | `ListGeneratedRecordsRequest`   | `ListGeneratedRecordsResponse`    |
| `RemoveGeneratedRecord`    | 0x0303 | 按id删除单条生成记录  | `RemoveGeneratedRecordRequest`  | `RemoveGeneratedRecordResponse`   |
| `ClearGeneratedRecords`    | 0x0304 | 清空全部生成记录      | `ClearGeneratedRecordsRequest`  | `ClearGeneratedRecordsResponse`   |
| `GetGeneratorSettings`     | 0x0305 | 查询生成器设置        | `GetGeneratorSettingsRequest`   | `GetGeneratorSettingsResponse`    |
| `SetGeneratorLimit`        | 0x0306 | 设置历史记录上限      | `SetGeneratorLimitRequest`      | `SetGeneratorLimitResponse`       |

> **生成记录加密约定**：`generate_password` 成功后 service 自动追加一条
> `GeneratedPasswordRecord` 到 `generated_passwords` 表。password 字段在
> 加密模式下用 `entry_crypto_` 加密为 `[IV(12) || ciphertext || tag(16)]`，与
> entry.password 同构；明文模式下 iv / tag 为空、password 为明文 BLOB。
> 启用 / 禁用程序密码时，所有生成记录会随 entry 一起被重新加密 / 解密，
> 通过 `IStorageEngine::update_generated_record` 仅更新 password / iv / tag
> 字段，保留 id 与 created_at 不变（避免丢失原始生成时间）。
> `set_generator_limit` 设置上限后立即触发清理：保留最新 N 条（按
> `created_at DESC, id DESC` 排序），其余删除；`limit = 0` 表示无限制。

### 3.5 标签管理与条目-标签关联（0x04xx）

| CommandId        | 值     | 名称                  | 请求结构                | 响应结构                |
|------------------|--------|-----------------------|-------------------------|--------------------------|
| `AddTag`         | 0x0400 | 新增标签              | `AddTagRequest`         | `AddTagResponse`        |
| `UpdateTag`      | 0x0401 | 更新标签              | `UpdateTagRequest`      | `UpdateTagResponse`     |
| `RemoveTag`      | 0x0402 | 按 id 删除标签        | `RemoveTagRequest`      | `RemoveTagResponse`     |
| `ListTags`       | 0x0403 | 列出全部标签          | `ListTagsRequest`       | `ListTagsResponse`      |
| `GetTag`         | 0x0404 | 按 id 获取单条标签    | `GetTagRequest`         | `GetTagResponse`        |
| `FindTagByName`  | 0x0405 | 按 name 查找标签      | `FindTagByNameRequest`  | `FindTagByNameResponse` |
| `GetEntryTags`   | 0x0406 | 获取指定条目的标签    | `GetEntryTagsRequest`   | `GetEntryTagsResponse`  |
| `SetEntryTags`   | 0x0407 | 全量替换条目的标签关联| `SetEntryTagsRequest`   | `SetEntryTagsResponse`  |

> **标签存储约定**：标签独立存储于 `tags` 表，`name` 字段在全库范围内唯一
> （大小写敏感）。条目与标签为多对多关系，通过 `entry_tags` 关联表维护，
> 启用外键 `ON DELETE CASCADE`：删除 Tag 时自动清理关联，删除 Entry 时同步清理。
> `set_entry_tags` 为**全量替换**语义：以请求中的 `tag_ids` 列表为准，
> 重复项由实现去重，不存在的 `tag_id` 静默跳过。
>
> **AddEntry / UpdateEntry 隐式标签处理**：当 `PasswordEntry.tags` 中包含
> `id == 0` 的新标签时，service 端 `ServiceCore::resolve_entry_tags` 会自动
> 创建并分配 id，再回写关联；已存在的标签按 `id` 关联，按 `name` 去重。

## 4. 请求 / 响应结构

所有结构定义于 `src/sdk/protocol/Messages.h`，为 POD-like 聚合。

### 4.1 系统级

```cpp
struct PingRequest {};                          // 空负载
struct PingResponse {
    uint64_t server_timestamp = 0;              // service 端 Unix 时间戳（秒）
};

struct ShutdownRequest {};                       // 空负载
struct ShutdownResponse {};                      // 空负载
```

> **Shutdown 处理位置**：`Shutdown` 命令由 `service main.cpp` 中的 handler lambda
> 拦截处理，不进入 `ServiceCore::handle_request`。设置 `g_should_exit = true` 后
> service 主循环退出。

### 4.2 会话级

```cpp
struct UnlockRequest {
    std::string password;        // 待验证的程序密码（明文模式下忽略）
};
struct UnlockResponse {
    bool success = false;
    std::string error_message;   // 失败原因（如 "wrong password" / "too many failed attempts"）
};

struct LockRequest {};
struct LockResponse {};

// 启用程序密码：从明文模式切换到加密模式。
// service 内部生成 encryption_key、用 password 派生 KEK 包装后写入 vault.meta，
// 并对现有所有明文条目重新加密。失败时回滚（删除 vault.meta，恢复明文条目）。
struct EnableProgramPasswordRequest {
    std::string password;        // 新程序密码
};
struct EnableProgramPasswordResponse {
    bool success = false;
    std::string error_message;   // 例如 "program password is already enabled"
};

// 禁用程序密码：从加密模式切换回明文模式。
// service 验证 password 后解密所有条目为明文，并删除 vault.meta。
struct DisableProgramPasswordRequest {
    std::string password;        // 当前程序密码（用于验证身份）
};
struct DisableProgramPasswordResponse {
    bool success = false;
    std::string error_message;   // 例如 "wrong program password"
};

// 修改程序密码：验证 old_password 后，用 new_password 重新包装 encryption_key。
// encryption_key 本身不变，条目无需重新加密。
struct ChangeProgramPasswordRequest {
    std::string old_password;
    std::string new_password;
};
struct ChangeProgramPasswordResponse {
    bool success = false;
    std::string error_message;
};

// 查询 vault 状态（UI 启动时据此决定是否显示解锁视图）。
struct GetVaultStatusRequest {};  // 空负载
struct GetVaultStatusResponse {
    bool password_enabled = false;  // true=加密模式（vault.meta 存在）
    bool is_locked = false;         // true=加密模式且未解锁
};
```

### 4.3 条目 CRUD

```cpp
struct AddEntryRequest {
    core::PasswordEntry entry;   // entry.id 通常为 0，由 service 分配后返回
};
struct AddEntryResponse {
    core::PasswordEntry entry;   // 含分配的 id 与时间戳；password 为明文
};

struct UpdateEntryRequest {
    core::PasswordEntry entry;   // entry.id 必须非 0
};
struct UpdateEntryResponse {
    core::PasswordEntry entry;    // 更新后的最新值
};

struct RemoveEntryRequest {
    int64_t id = 0;
};
struct RemoveEntryResponse {};

struct GetEntryRequest {
    int64_t id = 0;
};
struct GetEntryResponse {
    core::PasswordEntry entry;   // password 字段为解密后的明文
};

struct SearchEntriesRequest {
    core::SearchQuery query;
};
struct SearchEntriesResponse {
    std::vector<core::PasswordEntry> entries;
};

struct ListEntriesRequest {};
struct ListEntriesResponse {
    std::vector<core::PasswordEntry> entries;
};
```

### 4.4 密码生成与强度评估

```cpp
struct GeneratePasswordRequest {
    core::PasswordGeneratorOptions options;
};
struct GeneratePasswordResponse {
    std::string password;
};

struct EstimateStrengthRequest {
    std::string password;
};
struct EstimateStrengthResponse {
    core::StrengthEstimate estimate;
};
```

#### 4.4.1 生成器历史记录

```cpp
struct ListGeneratedRecordsRequest {};     // 空负载
struct ListGeneratedRecordsResponse {
    std::vector<core::GeneratedPasswordRecord> records;  // 按 created_at 倒序
};

struct RemoveGeneratedRecordRequest {
    int64_t id = 0;
};
struct RemoveGeneratedRecordResponse {};    // 空负载

struct ClearGeneratedRecordsRequest {};     // 空负载
struct ClearGeneratedRecordsResponse {};    // 空负载

struct GetGeneratorSettingsRequest {};      // 空负载
struct GetGeneratorSettingsResponse {
    int32_t history_limit = 0;              // 0 = 无限制；正整数 = 保留最近 N 条
};

struct SetGeneratorLimitRequest {
    int32_t limit = 0;                       // 0 = 无限制；正整数 = 保留最近 N 条
};
struct SetGeneratorLimitResponse {
    bool success = false;
};
```

`core::GeneratedPasswordRecord`（`src/sdk/core/Types.h`）：

```cpp
struct GeneratedPasswordRecord {
    int64_t id = 0;            // 主键，0 表示尚未分配的新记录
    std::string password;      // 内存中为明文；持久化为密文 BLOB
    int32_t length = 0;        // 生成时的密码长度
    int64_t created_at = 0;    // Unix 时间戳（秒）
    ByteVec iv;                // AES-256-GCM 的 IV；明文模式下为空
    ByteVec tag;               // AES-256-GCM 的 tag；明文模式下为空
};
```

`core::StrengthEstimate` 字段布局：

| 字段       | 类型                    | 说明                                              |
| ---------- | ----------------------- | ------------------------------------------------- |
| `bits`     | `int`                   | 估算熵（bit 数），模式惩罚后的最终值             |
| `level`    | `core::StrengthLevel`   | 强度等级枚举（VeryWeak/Weak/Medium/Strong/VeryStrong） |
| `score`    | `int`                   | 0..4，与 `level` 数值对应，UI 直接用作进度条段数 |
| `warnings` | `std::vector<std::string>` | 检测到的弱模式中文描述（重复字符/顺序序列/键盘序列/分布不均） |

阈值（按 `bits` 计算 `level`）：

| level       | bits 范围    |
| ----------- | ------------ |
| VeryWeak    | < 28         |
| Weak        | 28 .. 50     |
| Medium      | 50 .. 70     |
| Strong      | 70 .. 100    |
| VeryStrong  | >= 100       |

### 4.5 标签（Tag）管理

```cpp
struct AddTagRequest {
    core::Tag tag;              // tag.id 通常为 0，由 service 分配后返回
};
struct AddTagResponse {
    core::Tag tag;              // 含分配的 id 与时间戳
};

struct UpdateTagRequest {
    core::Tag tag;              // tag.id 必须非 0
};
struct UpdateTagResponse {
    core::Tag tag;              // 更新后的最新值
};

struct RemoveTagRequest {
    int64_t id = 0;
};
struct RemoveTagResponse {};     // 空负载

struct ListTagsRequest {};      // 空负载
struct ListTagsResponse {
    std::vector<core::Tag> tags;  // 按 name 升序排列
};

struct GetTagRequest {
    int64_t id = 0;
};
struct GetTagResponse {
    core::Tag tag;
};

struct FindTagByNameRequest {
    std::string name;          // 精确匹配（大小写敏感）
};
struct FindTagByNameResponse {
    core::Tag tag;             // 未找到时返回 ErrorResponse（code=NotFound）
};

struct GetEntryTagsRequest {
    int64_t entry_id = 0;
};
struct GetEntryTagsResponse {
    std::vector<core::Tag> tags;  // 该条目关联的全部标签
};

struct SetEntryTagsRequest {
    int64_t entry_id = 0;
    std::vector<int64_t> tag_ids;  // 全量替换：重复项去重，不存在的 id 静默跳过
};
struct SetEntryTagsResponse {};    // 空负载
```

`core::Tag`（`src/sdk/core/Types.h`）：

```cpp
struct Tag {
    int64_t id = 0;            // 主键，0 表示尚未分配的新标签
    std::string name;          // 唯一名称（不可重复，大小写敏感）
    std::string color;         // 十六进制颜色 "#RRGGBB"，可空表示默认色
    int64_t created_at = 0;    // 创建时间（Unix 时间戳，秒）
    int64_t updated_at = 0;    // 最后更新时间（Unix 时间戳，秒）
};
```

> **Tag CRUD 错误约定**：
> - `AddTag` 名称重复 → `ErrorResponse`（`code=AlreadyExists`）
> - `UpdateTag` / `RemoveTag` / `GetTag` id 不存在 → `ErrorResponse`（`code=NotFound`）
> - `FindTagByName` 未找到 → `ErrorResponse`（`code=NotFound`）
> - `SetEntryTags` entry_id 不存在 → `ErrorResponse`（`code=NotFound`）

### 4.6 通用错误响应

当请求处理失败且无具体响应结构时，service 返回 `ErrorResponse`：

```cpp
struct ErrorResponse {
    core::ErrorCode code = core::ErrorCode::None;
    std::string message;
};
```

`ErrorCode` 枚举（`src/sdk/core/Error.h`）：

| 值                  | 名称              | 含义                                       |
|---------------------|-------------------|--------------------------------------------|
| `None`              | 无错误            | 成功                                       |
| `InvalidArgument`   | 参数非法          | 请求结构反序列化失败、字段非法、明文模式下调用 Lock 等 |
| `NotFound`          | 未找到            | 条目不存在                                 |
| `AlreadyExists`     | 已存在            | 主键 / 唯一约束冲突                        |
| `Unauthorized`      | 未授权            | 程序密码错误、密钥不匹配、vault 已锁定     |
| `CryptoError`       | 加密错误          | 加密 / 解密 / 密钥派生失败                 |
| `StorageError`      | 存储错误          | 数据库 IO、约束失败                        |
| `IpcError`          | IPC 错误          | 命名管道通信错误、协议解析失败             |
| `InternalError`     | 内部错误          | 其他未分类错误                             |

### 4.7 core 数据类型

`core::PasswordEntry`（`src/sdk/core/Types.h`）：

```cpp
struct PasswordEntry {
    int64_t id = 0;              // 主键，0 表示新条目
    std::string entry_name;      // *必填* 条目显示标题
    std::string account;         // *必填* 登录账号
    std::string username;        // 可选 显示名
    std::string password;        // *必填* 内存中明文；持久化为密文 + iv + tag
    std::string website;         // 可选 站点 URL
    std::string note;            // 可选 备注（markdown 源码）
    std::vector<Tag> tags;       // 关联标签列表
    int64_t created_at = 0;      // Unix 时间戳（秒）
    int64_t updated_at = 0;
    ByteVec iv;                  // AES-256-GCM 的 IV，固定 12 字节
    ByteVec tag;                 // AES-256-GCM 的 tag，固定 16 字节
};
```

`core::SearchQuery`：

```cpp
struct SearchQuery {
    std::string text;                       // 子串匹配文本
    std::vector<std::string> fields;        // 限定字段：
                                            // entry_name / account / username / website / note
                                            // 空表示搜索全部可搜索字段
    bool case_sensitive = false;
    std::vector<int64_t> tag_ids;           // 按标签过滤（OR 语义：包含任一即匹配）
                                            // 为空时不按标签过滤
};
```

`core::PasswordGeneratorOptions`：

```cpp
struct PasswordGeneratorOptions {
    size_t length = 16;
    bool use_uppercase = true;
    bool use_lowercase = true;
    bool use_digits = true;
    bool use_symbols = true;
    std::string custom_chars;
    bool exclude_ambiguous = false;         // 排除 il1Lo0O 等易混字符
};
```

## 5. 错误响应语义

服务端处理失败时统一返回 `ErrorResponse`（仍按 16 字节 header + payload 形式封装）。
客户端 `IpcClient::send_request` 优先按期望响应类型反序列化，失败时按 `ErrorResponse`
反序列化并转为 `core::Error` 返回。

**特例**：`UnlockResponse` / `EnableProgramPasswordResponse` /
`DisableProgramPasswordResponse` / `ChangeProgramPasswordResponse` 携带 `success=false`
字段表示「程序密码错误」或「冷却期」或「模式不匹配」，不返回 `ErrorResponse`。
调用方需检查 `success` 字段而非 `Result::ok()`。

| 场景                                  | 返回类型                          | 关键字段                                |
|---------------------------------------|-----------------------------------|-----------------------------------------|
| 程序密码错误（unlock / disable / change）| `*Response`（带 success 字段）| `success=false`, `error_message` 非空   |
| 5 次失败后冷却期                      | `UnlockResponse`                  | `success=false`, `"too many failed attempts"` |
| 加密模式下重复 Enable                 | `EnableProgramPasswordResponse`   | `success=false`, `"already enabled"`    |
| 明文模式下调用 Disable / Change       | `*Response`（带 success 字段）    | `success=false`, `error_message` 非空   |
| 明文模式下调用 Lock                   | `ErrorResponse`                   | `code=InvalidArgument`                  |
| vault 已锁定，访问 CRUD               | `ErrorResponse`                   | `code=Unauthorized`                     |
| 条目不存在（get / remove / update）   | `ErrorResponse`                   | `code=NotFound`                         |
| 请求结构反序列化失败                  | `ErrorResponse`                   | `code=InvalidArgument`                  |

## 6. 通信流程示例

### 6.1 UI 调用 add_entry 完整字节序列

假设 UI 添加一条新条目 `{website="github.com", username="alice", password="p@ssw0rd", note=""}`：

**步骤 1：UI 序列化请求**

```
AddEntryRequest req{ PasswordEntry{
    id=0,                # 新条目
    website="github.com",
    username="alice",
    password="p@ssw0rd",
    note="",
    created_at=0,        # 由 service 填充
    updated_at=0,        # 由 service 填充
    iv=[],               # 空
    tag=[]               # 空
} }

→ protocol::serialize(req) → ByteVec payload
```

`payload` 字节布局（小端序，所有 uint32_t 长度前缀）：

```
偏移   字段              字节（hex，小端）
----   ---------------   --------------------------------
0      entry.id (i64)   00 00 00 00 00 00 00 00          # id=0
8      website len(u32) 0A 00 00 00                      # 10 字节
12     website bytes    67 69 74 68 75 62 2E 63 6F 6D   # "github.com"
22     username len     05 00 00 00                      # 5 字节
26     username bytes   61 6C 69 63 65                   # "alice"
31     password len     08 00 00 00                      # 8 字节
35     password bytes   70 40 73 73 77 30 72 64          # "p@ssw0rd"
43     note len         00 00 00 00                      # 空
47     created_at(i64)  00 00 00 00 00 00 00 00
55     updated_at(i64)  00 00 00 00 00 00 00 00
63     iv_len(u32)     00 00 00 00                       # 空
67     tag_len(u32)     00 00 00 00                       # 空
```

总负载长度：71 字节。

**步骤 2：UI 封装 MessageHeader**

```
magic         = 0x50564456  →  56 44 56 50               # "PDVV" 小端
version       = 0x0001      →  01 00
command       = 0x0200      →  00 02                     # AddEntry
request_id    = 0x00000001  →  01 00 00 00
payload_size  = 0x00000047  →  47 00 00 00               # 71 字节
```

header 共 16 字节，frame = 16 + 71 = 87 字节。

**步骤 3：通过命名管道写入**

```
WriteFile(pipe_handle, frame.data(), 87)
```

**步骤 4：service 解析**

```cpp
// IpcServer::client_loop
read 16 字节 → parse_header → MessageHeader{command=AddEntry, request_id=1, payload_size=71}
read 71 字节 → payload
handler(payload, header) → ServiceCore::handle_request(payload, header)
  → deserialize<AddEntryRequest>(payload)
  → encrypt_entry(entry)        # AES-256-GCM 加密 password
  → storage_->add_entry(...)    # SQLite INSERT
  → serialize(AddEntryResponse{entry 含分配的 id})
→ ByteVec response_payload
```

**步骤 5：service 返回响应**

```
MessageHeader{
    magic=0x50564456,
    version=0x0001,
    command=0x0200,           # 与请求一致
    request_id=0x00000001,    # 与请求一致，便于客户端匹配
    payload_size=<N>
} + response_payload
```

**步骤 6：UI 解析响应**

```cpp
// IpcClient::send_request
read 16 字节 → parse_header → 校验 magic / request_id
read payload_size 字节 → ByteVec resp_payload
auto r = deserialize<AddEntryResponse>(resp_payload);
if (r.ok()) return Result<AddEntryResponse>::Ok(...);
// 否则尝试 ErrorResponse，转 Error
```

### 6.2 简单命令：Ping

```
请求帧（仅 16 字节 header，无 payload）：
  56 44 56 50   01 00   01 00   01 00 00 00   00 00 00 00
  ^magic        ^ver    ^Ping   ^req_id=1     ^payload_size=0

响应帧：
  56 44 56 50   01 00   01 00   01 00 00 00   08 00 00 00   <8 字节 timestamp>
  ^magic        ^ver    ^Ping   ^req_id=1     ^size=8       ^uint64_t Unix 秒
```

## 7. 版本兼容性

- **协议版本**：当前为 `1`。同一版本内向后兼容追加字段（反序列化按当前 schema 读取，
  多出的尾部字节忽略；不足字段返回 `InvalidArgument`）。
- **新增命令**：在 `CommandId` 追加枚举值，保持值唯一且不复用旧值，以维持向前兼容性。
  旧客户端发往新 service 的新命令会被 `handle_request` 默认分支拒绝。
- **meta 文件版本**：`vault.meta` 自带 `magic + version` 字段，未来不兼容变更需递增
  version 并在 `ProgramPasswordStore::read_meta` 中按 version 分支处理。

## 8. 相关文档

- [ARCHITECTURE.md](ARCHITECTURE.md)：架构设计与数据流总览
- [SECURITY.md](SECURITY.md)：加密方案与程序密码保护
- [BUILD.md](BUILD.md)：构建与运行测试
- 源码：`src/sdk/protocol/Commands.h`、`Messages.h`、`Serializer.h/.cpp`
- 源码：`src/ui/IpcClient.h/.cpp`、`src/service/IpcServer.h/.cpp`、`src/service/ServiceCore.h/.cpp`
