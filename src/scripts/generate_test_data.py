import random
import string
from database import DatabaseManager
from encryption import EncryptionManager

# 初始化管理器
db_manager = DatabaseManager()
encryption_manager = EncryptionManager()

# 生成随机字符串的函数
def generate_random_string(length=10):
    letters = string.ascii_letters + string.digits
    return ''.join(random.choice(letters) for _ in range(length))

# 生成随机密码的函数
def generate_random_password(length=12):
    characters = string.ascii_letters + string.digits + string.punctuation
    return ''.join(random.choice(characters) for _ in range(length))

# 生成随机网站名称
def generate_random_website():
    websites = ['github.com', 'google.com', 'facebook.com', 'twitter.com', 'linkedin.com',
                'amazon.com', 'netflix.com', 'spotify.com', 'dropbox.com', 'zoom.us']
    return random.choice(websites)

# 生成随机用户名
def generate_random_username():
    prefixes = ['user', 'test', 'demo', 'dev', 'admin']
    suffixes = [str(random.randint(100, 999)), generate_random_string(3)]
    return f"{random.choice(prefixes)}{random.choice(suffixes)}"

# 生成随机邮箱
def generate_random_email():
    domains = ['gmail.com', 'yahoo.com', 'hotmail.com', 'outlook.com', 'example.com']
    username = generate_random_username()
    domain = random.choice(domains)
    return f"{username}@{domain}"

# 生成随机备注
def generate_random_note():
    notes = ['个人账号', '工作账号', '测试账号', '重要账号', '临时账号',
             '家庭共享', '紧急备用', '常用账号', '新注册', '旧账号']
    return random.choice(notes)

# 生成随机关联信息
def generate_random_related_info():
    info = ['注册时间: 2024-01-01', '最后修改: 2024-03-01', '安全问题: 宠物名字',
            '备用邮箱: backup@example.com', '恢复密钥: ' + generate_random_string(16),
            '创建于: 2023-12-01', '关联设备: iPhone', '所属分类: 工作',
            '重要程度: 高', '使用频率: 每天']
    return random.choice(info)

# 生成10条测试数据
print("开始生成测试数据...")
for i in range(1, 11):
    # 生成数据
    number = i  # 编号从1到10
    website = generate_random_website()
    username = generate_random_email() if random.choice([True, False]) else generate_random_username()
    password = generate_random_password()
    note = generate_random_note()
    sensitivity = random.choice([0, 1])  # 随机敏感性
    related_info = generate_random_related_info()
    
    # 加密数据
    encrypted_password = encryption_manager.encrypt_password(password)
    encrypted_related_info = encryption_manager.encrypt_related_info(related_info)
    
    # 保存到数据库
    db_manager.add_password(
        website=website,
        username=username,
        password=encrypted_password,
        note=note,
        number=number,
        sensitivity=sensitivity,
        related_info=encrypted_related_info
    )
    
    print(f"生成数据 {i}/10: {website} - {username}")

# 关闭数据库连接
db_manager.close()

print("\n测试数据生成完成！")
print("共生成10条测试数据，包含不同的网站、用户名、密码、备注、敏感性和关联信息。")
