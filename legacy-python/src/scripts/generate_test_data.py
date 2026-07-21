import random
import string
import argparse
import logging
import time
import sys
from datetime import datetime, timedelta

# 添加项目根目录到路径
sys.path.append('.')

from src.core.database import DatabaseManager
from src.core.encryption import EncryptionManager

# 配置日志
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s',
    handlers=[
        logging.StreamHandler(),
        logging.FileHandler('generate_test_data.log')
    ]
)
logger = logging.getLogger(__name__)

class TestDataGenerator:
    """测试数据生成器"""
    
    def __init__(self, config=None):
        """初始化测试数据生成器
        
        Args:
            config (dict): 配置参数
        """
        self.config = config or {}
        self.db_manager = DatabaseManager()
        self.encryption_manager = EncryptionManager()
        
        # 默认配置
        self.default_config = {
            'data_count': 10,
            'password_length': 12,
            'username_length': 10,
            'string_length': 10,
            'sensitivity_distribution': [0.7, 0.3],  # 70% 非敏感，30% 敏感
            'use_email_as_username': 0.5,  # 50% 使用邮箱作为用户名
        }
        
        # 合并配置
        for key, value in self.default_config.items():
            if key not in self.config:
                self.config[key] = value
        
        # 预生成数据
        self._pregenerate_data()
    
    def _pregenerate_data(self):
        """预生成数据，减少重复计算"""
        logger.info("预生成数据...")
        
        # 网站列表
        self.websites = [
            'github.com', 'google.com', 'facebook.com', 'twitter.com', 'linkedin.com',
            'amazon.com', 'netflix.com', 'spotify.com', 'dropbox.com', 'zoom.us',
            'microsoft.com', 'apple.com', 'yahoo.com', 'bing.com', 'instagram.com',
            'youtube.com', 'reddit.com', 'pinterest.com', 'tumblr.com', 'quora.com'
        ]
        
        # 域名列表
        self.email_domains = [
            'gmail.com', 'yahoo.com', 'hotmail.com', 'outlook.com', 'example.com',
            'icloud.com', 'protonmail.com', 'zoho.com', 'aol.com', 'mail.com'
        ]
        
        # 用户名前缀
        self.username_prefixes = [
            'user', 'test', 'demo', 'dev', 'admin', 'guest', 'visitor', 'customer',
            'client', 'employee', 'student', 'teacher', 'doctor', 'engineer', 'designer'
        ]
        
        # 备注列表
        self.notes = [
            '个人账号', '工作账号', '测试账号', '重要账号', '临时账号',
            '家庭共享', '紧急备用', '常用账号', '新注册', '旧账号',
            '财务相关', '社交账号', '娱乐账号', '学习账号', '游戏账号'
        ]
        
        # 关联信息模板
        self.related_info_templates = [
            '注册时间: {date}',
            '最后修改: {date}',
            '安全问题: {question}',
            '备用邮箱: {email}',
            '恢复密钥: {key}',
            '创建于: {date}',
            '关联设备: {device}',
            '所属分类: {category}',
            '重要程度: {importance}',
            '使用频率: {frequency}'
        ]
        
        # 安全问题
        self.security_questions = [
            '宠物名字', '母亲的娘家姓', '小学名称', '第一辆车品牌',
            '童年好友名字', '最喜欢的电影', '最喜欢的食物', '出生城市'
        ]
        
        # 设备类型
        self.devices = [
            'iPhone', 'Android', 'Windows PC', 'MacBook', 'iPad',
            'Linux PC', 'Smart TV', 'Smart Watch', 'Tablet', 'Game Console'
        ]
        
        # 分类
        self.categories = [
            '工作', '个人', '家庭', '财务', '社交',
            '娱乐', '学习', '游戏', '健康', '旅行'
        ]
        
        # 重要程度
        self.importance_levels = ['高', '中', '低']
        
        # 使用频率
        self.frequency_levels = ['每天', '每周', '每月', '偶尔', '很少']
        
        logger.info("数据预生成完成")
    
    def generate_random_string(self, length=None):
        """生成随机字符串
        
        Args:
            length (int): 字符串长度
            
        Returns:
            str: 随机字符串
        """
        length = length or self.config['string_length']
        letters = string.ascii_letters + string.digits
        return ''.join(random.choices(letters, k=length))
    
    def generate_random_password(self, length=None):
        """生成随机密码
        
        Args:
            length (int): 密码长度
            
        Returns:
            str: 随机密码
        """
        length = length or self.config['password_length']
        characters = string.ascii_letters + string.digits + string.punctuation
        return ''.join(random.choices(characters, k=length))
    
    def generate_random_website(self):
        """生成随机网站名称
        
        Returns:
            str: 随机网站名称
        """
        return random.choice(self.websites)
    
    def generate_random_username(self, length=None):
        """生成随机用户名
        
        Args:
            length (int): 用户名长度
            
        Returns:
            str: 随机用户名
        """
        length = length or self.config['username_length']
        prefix = random.choice(self.username_prefixes)
        suffix = str(random.randint(100, 9999))
        return f"{prefix}{suffix}"
    
    def generate_random_email(self):
        """生成随机邮箱
        
        Returns:
            str: 随机邮箱
        """
        username = self.generate_random_username()
        domain = random.choice(self.email_domains)
        return f"{username}@{domain}"
    
    def generate_random_note(self):
        """生成随机备注
        
        Returns:
            str: 随机备注
        """
        return random.choice(self.notes)
    
    def generate_random_date(self, days_ago=365):
        """生成随机日期
        
        Args:
            days_ago (int): 最大天数
            
        Returns:
            str: 随机日期
        """
        date = datetime.now() - timedelta(days=random.randint(1, days_ago))
        return date.strftime('%Y-%m-%d')
    
    def generate_random_related_info(self):
        """生成随机关联信息
        
        Returns:
            str: 随机关联信息
        """
        template = random.choice(self.related_info_templates)
        
        if '{date}' in template:
            template = template.format(date=self.generate_random_date())
        elif '{question}' in template:
            template = template.format(question=random.choice(self.security_questions))
        elif '{email}' in template:
            template = template.format(email=self.generate_random_email())
        elif '{key}' in template:
            template = template.format(key=self.generate_random_string(16))
        elif '{device}' in template:
            template = template.format(device=random.choice(self.devices))
        elif '{category}' in template:
            template = template.format(category=random.choice(self.categories))
        elif '{importance}' in template:
            template = template.format(importance=random.choice(self.importance_levels))
        elif '{frequency}' in template:
            template = template.format(frequency=random.choice(self.frequency_levels))
        
        return template
    
    def generate_random_sensitivity(self):
        """根据分布生成随机敏感性
        
        Returns:
            int: 敏感性 (0 或 1)
        """
        return random.choices([0, 1], weights=self.config['sensitivity_distribution'])[0]
    
    def validate_data(self, data):
        """验证数据有效性
        
        Args:
            data (dict): 数据字典
            
        Returns:
            bool: 数据是否有效
        """
        # 检查必要字段
        required_fields = ['website', 'username', 'password', 'note']
        for field in required_fields:
            if not data.get(field):
                logger.error(f"缺少必要字段: {field}")
                return False
        
        # 检查字段长度
        if len(data['password']) < 6:
            logger.error("密码长度不足")
            return False
        
        if len(data['website']) < 3:
            logger.error("网站名称长度不足")
            return False
        
        return True
    
    def generate_data(self):
        """生成单条测试数据
        
        Returns:
            dict: 测试数据
        """
        # 生成数据
        data = {
            'number': None,  # 稍后设置
            'website': self.generate_random_website(),
            'username': self.generate_random_email() if random.random() < self.config['use_email_as_username'] else self.generate_random_username(),
            'password': self.generate_random_password(),
            'note': self.generate_random_note(),
            'sensitivity': self.generate_random_sensitivity(),
            'related_info': self.generate_random_related_info()
        }
        
        # 验证数据
        if not self.validate_data(data):
            logger.warning("数据验证失败，重新生成")
            return self.generate_data()
        
        return data
    
    def generate_test_data(self):
        """生成测试数据并保存到数据库"""
        start_time = time.time()
        data_count = self.config['data_count']
        
        logger.info(f"开始生成 {data_count} 条测试数据...")
        
        success_count = 0
        failed_count = 0
        
        for i in range(1, data_count + 1):
            try:
                # 生成数据
                data = self.generate_data()
                data['number'] = i
                
                # 加密数据
                encrypted_password = self.encryption_manager.encrypt_password(data['password'])
                encrypted_related_info = self.encryption_manager.encrypt_related_info(data['related_info'])
                
                # 保存到数据库
                self.db_manager.add_password(
                    website=data['website'],
                    username=data['username'],
                    password=encrypted_password,
                    note=data['note'],
                    number=data['number'],
                    sensitivity=data['sensitivity'],
                    related_info=encrypted_related_info
                )
                
                success_count += 1
                
                # 每生成10%的数据输出一次日志
                if i % max(1, data_count // 10) == 0:
                    progress = (i / data_count) * 100
                    logger.info(f"生成进度: {progress:.1f}% ({i}/{data_count})")
                    print(f"生成进度: {progress:.1f}% ({i}/{data_count})")
                    
            except Exception as e:
                failed_count += 1
                logger.error(f"生成数据 {i} 失败: {str(e)}")
        
        # 关闭数据库连接
        self.db_manager.close()
        
        end_time = time.time()
        elapsed_time = end_time - start_time
        
        logger.info(f"测试数据生成完成！")
        logger.info(f"成功: {success_count} 条, 失败: {failed_count} 条")
        logger.info(f"耗时: {elapsed_time:.2f} 秒")
        
        print(f"\n测试数据生成完成！")
        print(f"成功: {success_count} 条, 失败: {failed_count} 条")
        print(f"耗时: {elapsed_time:.2f} 秒")

if __name__ == "__main__":
    # 解析命令行参数
    parser = argparse.ArgumentParser(description='生成测试数据')
    parser.add_argument('--count', type=int, default=10, help='生成数据数量')
    parser.add_argument('--password-length', type=int, default=12, help='密码长度')
    parser.add_argument('--username-length', type=int, default=10, help='用户名长度')
    parser.add_argument('--string-length', type=int, default=10, help='字符串长度')
    parser.add_argument('--sensitivity-ratio', type=float, default=0.3, help='敏感数据比例')
    parser.add_argument('--email-ratio', type=float, default=0.5, help='使用邮箱作为用户名的比例')
    
    args = parser.parse_args()
    
    # 构建配置
    config = {
        'data_count': args.count,
        'password_length': args.password_length,
        'username_length': args.username_length,
        'string_length': args.string_length,
        'sensitivity_distribution': [1 - args.sensitivity_ratio, args.sensitivity_ratio],
        'use_email_as_username': args.email_ratio
    }
    
    # 生成测试数据
    generator = TestDataGenerator(config)
    generator.generate_test_data()
