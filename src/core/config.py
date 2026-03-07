import os
import pathlib
import json

class Config:
    """应用配置管理类"""
    
    # 配置文件路径
    _config_path = pathlib.Path(os.environ.get('APPDATA', os.path.expanduser('~'))) / 'PasswordManager' / 'config.json'
    
    @classmethod
    def get_app_data_dir(cls):
        """获取应用数据目录
        
        Returns:
            pathlib.Path: 应用数据目录路径
        """
        # 尝试从配置文件读取
        try:
            if cls._config_path.exists():
                with open(cls._config_path, 'r', encoding='utf-8') as f:
                    config = json.load(f)
                    if 'app_data_dir' in config:
                        app_data_dir = pathlib.Path(config['app_data_dir'])
                        # 确保目录存在
                        app_data_dir.mkdir(parents=True, exist_ok=True)
                        return app_data_dir
        except Exception:
            pass
        
        # 默认路径
        app_data_dir = pathlib.Path(os.environ.get('APPDATA', os.path.expanduser('~'))) / 'PasswordManager'
        # 确保目录存在
        app_data_dir.mkdir(parents=True, exist_ok=True)
        return app_data_dir
    
    @classmethod
    def set_app_data_dir(cls, path):
        """设置应用数据目录
        
        Args:
            path (str): 新的应用数据目录路径
        """
        try:
            # 确保配置文件目录存在
            cls._config_path.parent.mkdir(parents=True, exist_ok=True)
            
            # 读取现有配置
            config = {}
            if cls._config_path.exists():
                with open(cls._config_path, 'r', encoding='utf-8') as f:
                    config = json.load(f)
            
            # 更新配置
            config['app_data_dir'] = path
            
            # 写入配置文件
            with open(cls._config_path, 'w', encoding='utf-8') as f:
                json.dump(config, f, indent=2, ensure_ascii=False)
        except Exception as e:
            raise Exception(f"保存配置失败: {str(e)}")
    
    @classmethod
    def get_db_path(cls):
        """获取数据库文件路径
        
        Returns:
            pathlib.Path: 数据库文件路径
        """
        return cls.get_app_data_dir() / 'passwords.db'
    
    @classmethod
    def get_key_path(cls):
        """获取加密密钥文件路径
        
        Returns:
            pathlib.Path: 加密密钥文件路径
        """
        return cls.get_app_data_dir() / 'key.key'
    
    @classmethod
    def get_history_path(cls):
        """获取历史记录文件路径
        
        Returns:
            pathlib.Path: 历史记录文件路径
        """
        return cls.get_app_data_dir() / 'password_history.json'