import json
from datetime import datetime
from config import Config

class HistoryManager:
    """历史记录管理类"""
    
    def __init__(self):
        """初始化历史记录管理器"""
        self.history_path = Config.get_history_path()
        self._init_history_file()
    
    def _init_history_file(self):
        """初始化历史记录文件"""
        if not self.history_path.exists():
            with open(self.history_path, 'w') as f:
                f.write('[]')
    
    def save_to_history(self, password):
        """保存密码到历史记录
        
        Args:
            password (str): 生成的密码
        """
        # 读取历史记录
        with open(self.history_path, 'r') as f:
            history = json.load(f)
        
        # 添加新记录
        new_record = {
            'password': password,
            'timestamp': datetime.now().strftime('%Y年%m月%d日 %H时%M分%S秒')
        }
        
        # 插入到开头（时间倒序）
        history.insert(0, new_record)
        
        # 保持最多10条记录
        if len(history) > 10:
            history = history[:10]
        
        # 保存回文件
        with open(self.history_path, 'w') as f:
            json.dump(history, f, ensure_ascii=False, indent=2)
    
    def get_history(self):
        """获取历史记录
        
        Returns:
            list: 历史记录列表
        """
        self._init_history_file()
        with open(self.history_path, 'r') as f:
            return json.load(f)