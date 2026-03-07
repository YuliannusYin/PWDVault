# 密码管理程序 (PasswordManager)

一个功能完善、安全可靠的本地密码管理工具，帮助用户存储和管理各类账号密码信息。

## 项目简介

PasswordManager 是一个基于 Python 和 Tkinter 开发的本地密码管理应用，采用 SQLite 数据库存储数据，使用 Fernet 加密算法保护密码安全。该应用提供了直观的用户界面，支持密码的录入、生成、管理和搜索等功能，帮助用户安全地存储和管理各类账号密码信息。

## 核心功能

- **密码录入与保存**：支持输入网站、用户名、密码和备注信息
- **随机密码生成**：可自定义密码长度和包含的字符类型
- **密码管理**：查看、编辑、删除已保存的密码
- **密码搜索**：支持多字段搜索，包括网站、用户名、密码和备注
- **数据脱敏**：可切换密码和账号的显示状态，保护隐私
- **历史记录**：保存最近生成的密码，方便重复使用
- **剪贴板操作**：快速复制密码到剪贴板，方便使用
- **文件管理**：支持密码数据的导入导出功能
- **程序设置**：允许设置默认存储路径，支持路径浏览和验证

## 技术架构

### 技术栈
- **开发语言**：Python 3
- **GUI框架**：Tkinter
- **数据库**：SQLite
- **加密库**：cryptography.fernet
- **其他依赖**：json, datetime, random, string, os, pathlib

### 项目结构
```
PasswordManager/
├── src/
│   ├── core/
│   │   ├── __pycache__/
│   │   ├── __init__.py
│   │   ├── config.py                # 配置管理
│   │   ├── database.py              # 数据库操作
│   │   ├── encryption.py            # 密码加密
│   │   ├── history_manager.py       # 历史记录管理
│   │   └── password_generator.py    # 密码生成
│   ├── scripts/
│   │   ├── __pycache__/
│   │   ├── __init__.py
│   │   ├── clear_data.py            # 数据清理脚本
│   │   ├── data_exchange.py         # 数据交换功能
│   │   ├── data_exchange_ui.py      # 数据交换界面
│   │   ├── generate_test_data.py    # 测试数据生成
│   │   └── test_data_exchange.py    # 数据交换测试
│   ├── ui/
│   │   ├── __pycache__/
│   │   ├── __init__.py
│   │   ├── file_management_tab.py   # 文件管理标签页
│   │   ├── generator_tab.py         # 密码生成标签页
│   │   ├── input_tab.py             # 密码录入标签页
│   │   ├── main_window.py           # 主窗口
│   │   ├── password_book_tab.py     # 密码本标签页
│   │   └── settings_tab.py          # 程序设置标签页
│   └── __init__.py
├── __pycache__/
├── .gitignore                      # Git忽略文件
├── LICENSE                         # 许可证文件
├── README.md                       # 项目说明文档
└── main.py                         # 程序入口
```

### 核心模块

1. **MainWindow**：主窗口类，负责协调各个功能模块
2. **DatabaseManager**：数据库管理类，处理密码的存储和查询
3. **EncryptionManager**：加密管理类，负责密码的加密和解密
4. **PasswordGenerator**：密码生成器类，生成随机密码
5. **HistoryManager**：历史记录管理类，管理生成的密码历史
6. **FileManagementTab**：文件管理标签页，支持密码数据的导入导出
7. **SettingsTab**：程序设置标签页，管理应用配置和存储路径

## 安装步骤

### 环境要求
- Python 3.6 或更高版本
- pip 包管理工具

### 安装依赖

1. 克隆项目到本地
   ```bash
   git clone https://github.com/yourusername/PasswordManager.git
   cd PasswordManager
   ```

2. 安装所需依赖
   ```bash
   pip install cryptography
   ```

### 运行程序

```bash
python main.py
```

## 使用说明

### 密码录入
1. 在"账号密码录入"标签页中，填写网站/应用名称、账号/用户名、密码和备注
2. 点击"保存"按钮，密码将被加密后存储到数据库

### 密码生成
1. 在"随机密码生成"标签页中，设置密码长度和包含的字符类型
2. 点击"生成密码"按钮，系统将生成一个随机密码
3. 可以点击"复制"按钮将密码复制到剪贴板，或点击"使用"按钮将密码粘贴到录入界面
4. 点击"历史"按钮查看最近生成的密码

### 密码管理
1. 在"密码本"标签页中，查看所有已保存的密码
2. 使用搜索功能查找特定密码
3. 选择密码后，可以进行以下操作：
   - 查看详情：点击"查看详情"按钮
   - 编辑：点击"编辑"按钮修改密码信息
   - 删除：点击"删除"按钮删除密码
   - 复制密码：点击"复制密码"按钮将密码复制到剪贴板
4. 可以开启"数据脱敏"功能，隐藏密码和部分账号信息

### 文件管理
1. 在"文件管理"标签页中，可以进行密码数据的导入导出操作
2. 支持将密码数据导出为JSON或CSV格式
3. 支持从JSON或CSV文件导入密码数据
4. 导入前会进行数据验证，确保数据格式正确

### 程序设置
1. 在"程序设置"标签页中，可以设置默认存储路径
2. 可以通过"浏览"按钮选择新的存储路径
3. 系统会自动验证路径的有效性
4. 可以点击"重置为默认"按钮恢复默认存储路径
5. 预留了黑色模式、多语言支持和开发者模式的设置区域，将在未来版本中实现

## 安全说明

- 密码使用 Fernet 对称加密算法加密存储
- 加密密钥自动生成并存储在本地
- 建议定期备份应用数据目录
- 不要将加密密钥文件分享给他人

## 数据存储

应用数据存储在以下位置：
- **Windows**：`%APPDATA%\PasswordManager`
- **其他系统**：`~/.PasswordManager`

数据文件包括：
- `passwords.db`：SQLite 数据库文件，存储加密后的密码
- `key.key`：加密密钥文件
- `password_history.json`：密码历史记录文件

## 贡献指南

欢迎贡献代码和改进建议！请按照以下步骤进行：

1. Fork 本项目
2. 创建功能分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 打开 Pull Request

## 许可证

本项目采用 MIT 许可证 - 详见 [LICENSE](LICENSE) 文件

## 联系方式

- 项目链接：[https://github.com/yourusername/PasswordManager](https://github.com/yourusername/PasswordManager)
- 问题反馈：[Issues](https://github.com/yourusername/PasswordManager/issues)

---

感谢使用 PasswordManager！希望它能帮助您更安全、更方便地管理密码。