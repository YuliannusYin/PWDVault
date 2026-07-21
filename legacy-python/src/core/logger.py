import os
import json
from datetime import datetime
from pathlib import Path

class Logger:
    """日志管理器类"""
    
    def __init__(self, log_dir=None):
        """初始化日志管理器
        
        Args:
            log_dir: 日志存储目录
        """
        if log_dir is None:
            from src.core.config import Config
            log_dir = Config.get_app_data_dir()
        
        self.log_dir = Path(log_dir)
        self.log_dir.mkdir(parents=True, exist_ok=True)
        self.log_file = self.log_dir / "app.log"
        
    def log(self, operation, result, **kwargs):
        """记录日志
        
        Args:
            operation (str): 操作类型
            result (str): 操作结果
            **kwargs: 其他参数
        """
        log_entry = {
            "timestamp": datetime.now().isoformat(),
            "operation": operation,
            "result": result,
            **kwargs
        }
        
        # 读取现有日志
        logs = []
        if self.log_file.exists():
            try:
                with open(self.log_file, 'r', encoding='utf-8') as f:
                    logs = json.load(f)
            except Exception:
                logs = []
        
        # 添加新日志
        logs.append(log_entry)
        
        # 限制日志数量，只保留最近1000条
        if len(logs) > 1000:
            logs = logs[-1000:]
        
        # 写入日志文件
        try:
            with open(self.log_file, 'w', encoding='utf-8') as f:
                json.dump(logs, f, ensure_ascii=False, indent=2)
        except Exception:
            pass
    
    def get_logs(self, limit=None):
        """获取日志
        
        Args:
            limit: 限制返回的日志数量
            
        Returns:
            list: 日志列表
        """
        if not self.log_file.exists():
            return []
        
        try:
            with open(self.log_file, 'r', encoding='utf-8') as f:
                logs = json.load(f)
        except Exception:
            return []
        
        if limit:
            return logs[-limit:]
        return logs
    
    def clear_logs(self):
        """清空日志"""
        if self.log_file.exists():
            try:
                with open(self.log_file, 'w', encoding='utf-8') as f:
                    json.dump([], f)
            except Exception:
                pass
    
    def get_log_file_path(self):
        """获取日志文件路径
        
        Returns:
            str: 日志文件路径
        """
        return str(self.log_file)