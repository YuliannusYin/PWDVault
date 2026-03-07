import secrets
import string

class PasswordGenerator:
    """密码生成器类"""
    
    @staticmethod
    def generate_password(length=18, include_uppercase=True, include_lowercase=True, include_digits=True, include_special=False):
        """生成随机密码
        
        Args:
            length (int): 密码长度
            include_uppercase (bool): 是否包含大写字母
            include_lowercase (bool): 是否包含小写字母
            include_digits (bool): 是否包含数字
            include_special (bool): 是否包含特殊符号
            
        Returns:
            str: 生成的密码
            
        Raises:
            ValueError: 当没有选择任何字符类型时
        """
        # 构建字符集
        char_set = ""
        if include_uppercase:
            char_set += string.ascii_uppercase
        if include_lowercase:
            char_set += string.ascii_lowercase
        if include_digits:
            char_set += string.digits
        if include_special:
            char_set += string.punctuation
        
        if not char_set:
            raise ValueError("至少选择一种字符类型")
        
        # 生成密码
        # 使用secrets模块替代random模块，提供更安全的密码生成
        # secrets模块使用加密安全的伪随机数生成器，适合生成密码等安全相关的随机值
        password = ''.join(secrets.choice(char_set) for _ in range(length))
        return password