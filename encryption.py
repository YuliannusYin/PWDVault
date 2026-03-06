from cryptography.fernet import Fernet
from config import Config

class EncryptionManager:
    """加密管理类"""
    
    def __init__(self):
        """初始化加密管理器"""
        self.key = self._load_or_generate_key()
        self.cipher = Fernet(self.key)
    
    def _load_or_generate_key(self):
        """加载或生成加密密钥
        
        Returns:
            bytes: 加密密钥
        """
        key_path = Config.get_key_path()
        if key_path.exists():
            with open(key_path, 'rb') as key_file:
                return key_file.read()
        else:
            key = Fernet.generate_key()
            with open(key_path, 'wb') as key_file:
                key_file.write(key)
            return key
    
    def encrypt_password(self, password):
        """加密密码
        
        Args:
            password (str): 原始密码
            
        Returns:
            str: 加密后的密码
        """
        return self.cipher.encrypt(password.encode()).decode()
    
    def decrypt_password(self, encrypted_password):
        """解密密码
        
        Args:
            encrypted_password (str): 加密后的密码
            
        Returns:
            str: 解密后的密码
        """
        try:
            return self.cipher.decrypt(encrypted_password.encode()).decode()
        except Exception:
            return "[解密失败]"
    
    def encrypt_related_info(self, related_info):
        """加密关联信息
        
        Args:
            related_info (str): 原始关联信息
            
        Returns:
            str: 加密后的关联信息
        """
        if related_info:
            return self.cipher.encrypt(related_info.encode()).decode()
        return None
    
    def decrypt_related_info(self, encrypted_related_info):
        """解密关联信息
        
        Args:
            encrypted_related_info (str): 加密后的关联信息
            
        Returns:
            str: 解密后的关联信息
        """
        if encrypted_related_info:
            try:
                return self.cipher.decrypt(encrypted_related_info.encode()).decode()
            except Exception:
                return "[解密失败]"
        return None