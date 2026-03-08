import tkinter as tk
from tkinter import ttk

class CacheMonitor:
    """缓存监控界面"""
    
    def __init__(self, parent, cache_manager):
        """初始化缓存监控界面
        
        Args:
            parent: 父窗口
            cache_manager: 缓存管理器
        """
        self.parent = parent
        self.cache_manager = cache_manager
        self.window = None
    
    def show_monitor(self):
        """显示缓存监控窗口"""
        # 创建监控窗口
        self.window = tk.Toplevel(self.parent)
        self.window.title("缓存监控")
        self.window.geometry("500x400")
        self.window.resizable(False, False)
        
        # 创建主框架
        main_frame = ttk.LabelFrame(self.window, text="缓存状态", padding="20")
        main_frame.pack(fill=tk.BOTH, expand=True, pady=10)
        
        # 统计信息
        stats_frame = ttk.LabelFrame(main_frame, text="统计信息", padding="10")
        stats_frame.pack(fill=tk.X, pady=10)
        
        # 命中率
        ttk.Label(stats_frame, text="命中率:", font=('Arial', 10, 'bold')).grid(row=0, column=0, sticky=tk.W, pady=5, padx=5)
        self.hit_rate_var = tk.StringVar()
        ttk.Label(stats_frame, textvariable=self.hit_rate_var).grid(row=0, column=1, sticky=tk.W, pady=5, padx=5)
        
        # 缓存大小
        ttk.Label(stats_frame, text="缓存大小:", font=('Arial', 10, 'bold')).grid(row=1, column=0, sticky=tk.W, pady=5, padx=5)
        self.size_var = tk.StringVar()
        ttk.Label(stats_frame, textvariable=self.size_var).grid(row=1, column=1, sticky=tk.W, pady=5, padx=5)
        
        # 命中次数
        ttk.Label(stats_frame, text="命中次数:", font=('Arial', 10, 'bold')).grid(row=2, column=0, sticky=tk.W, pady=5, padx=5)
        self.hits_var = tk.StringVar()
        ttk.Label(stats_frame, textvariable=self.hits_var).grid(row=2, column=1, sticky=tk.W, pady=5, padx=5)
        
        # 未命中次数
        ttk.Label(stats_frame, text="未命中次数:", font=('Arial', 10, 'bold')).grid(row=3, column=0, sticky=tk.W, pady=5, padx=5)
        self.misses_var = tk.StringVar()
        ttk.Label(stats_frame, textvariable=self.misses_var).grid(row=3, column=1, sticky=tk.W, pady=5, padx=5)
        
        # 配置信息
        config_frame = ttk.LabelFrame(main_frame, text="配置信息", padding="10")
        config_frame.pack(fill=tk.X, pady=10)
        
        # 最大容量
        ttk.Label(config_frame, text="最大容量:", font=('Arial', 10, 'bold')).grid(row=0, column=0, sticky=tk.W, pady=5, padx=5)
        self.max_size_var = tk.StringVar()
        ttk.Label(config_frame, textvariable=self.max_size_var).grid(row=0, column=1, sticky=tk.W, pady=5, padx=5)
        
        # 默认过期时间
        ttk.Label(config_frame, text="默认过期时间:", font=('Arial', 10, 'bold')).grid(row=1, column=0, sticky=tk.W, pady=5, padx=5)
        self.ttl_var = tk.StringVar()
        ttk.Label(config_frame, textvariable=self.ttl_var).grid(row=1, column=1, sticky=tk.W, pady=5, padx=5)
        
        # 操作按钮
        button_frame = ttk.Frame(main_frame)
        button_frame.pack(fill=tk.X, pady=10)
        
        # 刷新按钮
        refresh_button = ttk.Button(button_frame, text="刷新", command=self.refresh_stats)
        refresh_button.pack(side=tk.LEFT, padx=5)
        
        # 清空缓存按钮
        clear_button = ttk.Button(button_frame, text="清空缓存", command=self.clear_cache)
        clear_button.pack(side=tk.LEFT, padx=5)
        
        # 清理过期项按钮
        cleanup_button = ttk.Button(button_frame, text="清理过期项", command=self.cleanup_expired)
        cleanup_button.pack(side=tk.LEFT, padx=5)
        
        # 初始化统计信息
        self.refresh_stats()
    
    def refresh_stats(self):
        """刷新统计信息"""
        stats = self.cache_manager.get_stats()
        
        # 更新统计信息
        self.hit_rate_var.set(f"{stats['hit_rate']}%")
        self.size_var.set(f"{stats['current_size']}/{stats['max_size']}")
        self.hits_var.set(str(stats['hits']))
        self.misses_var.set(str(stats['misses']))
        
        # 更新配置信息
        self.max_size_var.set(str(self.cache_manager.max_size))
        self.ttl_var.set(f"{self.cache_manager.default_ttl}秒")
    
    def clear_cache(self):
        """清空缓存"""
        self.cache_manager.clear()
        self.refresh_stats()
    
    def cleanup_expired(self):
        """清理过期项"""
        count = self.cache_manager.remove_expired()
        self.refresh_stats()
        # 显示清理结果
        tk.messagebox.showinfo("清理完成", f"清理了 {count} 个过期项")