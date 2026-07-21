from collections import OrderedDict
import time
from typing import Any, Dict, Optional, Tuple

class CacheManager:
    """缓存管理器类"""
    
    def __init__(self, max_size: int = 100, default_ttl: int = 3600):
        """初始化缓存管理器
        
        Args:
            max_size (int): 缓存最大容量
            default_ttl (int): 默认过期时间（秒）
        """
        self.max_size = max_size
        self.default_ttl = default_ttl
        self.cache: Dict[str, Tuple[Any, float]] = OrderedDict()
        self.hits = 0
        self.misses = 0
    
    def get(self, key: str) -> Optional[Any]:
        """获取缓存值
        
        Args:
            key (str): 缓存键
            
        Returns:
            Optional[Any]: 缓存值，如果不存在或已过期返回None
        """
        if key not in self.cache:
            self.misses += 1
            return None
        
        value, expiry = self.cache[key]
        if time.time() > expiry:
            # 缓存已过期
            del self.cache[key]
            self.misses += 1
            return None
        
        # 更新访问顺序（LRU机制）
        self.cache.move_to_end(key)
        self.hits += 1
        return value
    
    def set(self, key: str, value: Any, ttl: Optional[int] = None) -> None:
        """设置缓存值
        
        Args:
            key (str): 缓存键
            value (Any): 缓存值
            ttl (Optional[int]): 过期时间（秒），None则使用默认值
        """
        if ttl is None:
            ttl = self.default_ttl
        
        expiry = time.time() + ttl
        
        # 如果缓存已满，删除最久未使用的项
        if len(self.cache) >= self.max_size and key not in self.cache:
            self.cache.popitem(last=False)
        
        self.cache[key] = (value, expiry)
        self.cache.move_to_end(key)
    
    def delete(self, key: str) -> bool:
        """删除缓存值
        
        Args:
            key (str): 缓存键
            
        Returns:
            bool: 是否删除成功
        """
        if key in self.cache:
            del self.cache[key]
            return True
        return False
    
    def clear(self) -> None:
        """清空缓存"""
        self.cache.clear()
    
    def get_stats(self) -> Dict[str, int]:
        """获取缓存统计信息
        
        Returns:
            Dict[str, int]: 统计信息
        """
        total = self.hits + self.misses
        hit_rate = (self.hits / total * 100) if total > 0 else 0
        
        return {
            "hits": self.hits,
            "misses": self.misses,
            "total": total,
            "hit_rate": round(hit_rate, 2),
            "current_size": len(self.cache),
            "max_size": self.max_size
        }
    
    def update_config(self, max_size: Optional[int] = None, default_ttl: Optional[int] = None) -> None:
        """更新缓存配置
        
        Args:
            max_size (Optional[int]): 新的最大容量
            default_ttl (Optional[int]): 新的默认过期时间
        """
        if max_size is not None:
            self.max_size = max_size
            # 调整缓存大小
            while len(self.cache) > self.max_size:
                self.cache.popitem(last=False)
        
        if default_ttl is not None:
            self.default_ttl = default_ttl
    
    def remove_expired(self) -> int:
        """移除所有过期的缓存项
        
        Returns:
            int: 移除的过期项数量
        """
        current_time = time.time()
        expired_keys = [key for key, (_, expiry) in self.cache.items() if current_time > expiry]
        
        for key in expired_keys:
            del self.cache[key]
        
        return len(expired_keys)