import os
import pathlib

class Config:
    """应用配置管理类"""
    
    @staticmethod
    def get_app_data_dir():
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