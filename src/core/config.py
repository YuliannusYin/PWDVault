import os
import pathlib
import json

class Config:
    """应用配置管理类"""
    
    # 默认配置
    _default_config = {
        "app_data_dir": "",
        "dark_mode": False,
        "language": "zh_CN",
        "auto_clear_clipboard": True,
        "clipboard_clear_time": 30,
        "show_password_by_default": False
    }
    
    _config_data = None
    
    @classmethod
    def get_config_path(cls):
        """获取配置文件路径
        
        Returns:
            pathlib.Path: 配置文件路径
        """
        return cls.get_app_data_dir() / 'config.json'
    
    @classmethod
    def get_app_data_dir(cls):
        """获取应用数据目录
        
        Returns:
            pathlib.Path: 应用数据目录路径
        """
        # 获取用户的 AppData 目录
        appdata_dir = os.environ.get('APPDATA')
        if not appdata_dir:
            # 如果环境变量不存在，使用默认路径
            appdata_dir = os.path.expanduser('~') + '\\AppData\\Roaming'
        
        # 使用固定路径结构
        app_data_dir = pathlib.Path(appdata_dir) / 'PasswordManager'
        # 确保目录存在
        app_data_dir.mkdir(parents=True, exist_ok=True)
        return app_data_dir
    
    @classmethod
    def load_config(cls):
        """加载配置文件
        
        Returns:
            dict: 配置数据
        """
        if cls._config_data is None:
            config_path = cls.get_config_path()
            try:
                if config_path.exists():
                    with open(config_path, 'r', encoding='utf-8') as f:
                        cls._config_data = json.load(f)
                else:
                    # 创建默认配置文件
                    cls._config_data = cls._default_config.copy()
                    cls.save_config()
            except Exception:
                # 配置文件格式错误，使用默认配置
                cls._config_data = cls._default_config.copy()
                cls.save_config()
        return cls._config_data
    
    @classmethod
    def save_config(cls):
        """保存配置文件"""
        if cls._config_data is None:
            cls.load_config()
        
        config_path = cls.get_config_path()
        try:
            with open(config_path, 'w', encoding='utf-8') as f:
                json.dump(cls._config_data, f, indent=2, ensure_ascii=False)
        except Exception as e:
            raise Exception(f"保存配置文件失败: {str(e)}")
    
    @classmethod
    def get(cls, key, default=None):
        """获取配置值
        
        Args:
            key (str): 配置键
            default: 默认值
            
        Returns:
            配置值
        """
        config = cls.load_config()
        return config.get(key, default)
    
    @classmethod
    def set(cls, key, value):
        """设置配置值
        
        Args:
            key (str): 配置键
            value: 配置值
        """
        config = cls.load_config()
        config[key] = value
        cls.save_config()
    
    @staticmethod
    def get_db_path():
        """获取数据库文件路径
        
        Returns:
            pathlib.Path: 数据库文件路径
        """
        return Config.get_app_data_dir() / 'passwords.db'
    
    @staticmethod
    def get_key_path():
        """获取加密密钥文件路径
        
        Returns:
            pathlib.Path: 加密密钥文件路径
        """
        return Config.get_app_data_dir() / 'key.key'
    
    @staticmethod
    def get_history_path():
        """获取历史记录文件路径
        
        Returns:
            pathlib.Path: 历史记录文件路径
        """
        return Config.get_app_data_dir() / 'password_history.json'