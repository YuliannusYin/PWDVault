import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from src.core.config import Config
import os

class SettingsTab:
    """程序设置标签页"""
    
    def __init__(self, parent):
        """初始化设置标签页
        
        Args:
            parent: 父窗口
        """
        self.parent = parent
        self.tab = ttk.Frame(parent)
        parent.add(self.tab, text="程序设置")
        
        # 创建设置框架
        self.settings_frame = ttk.LabelFrame(self.tab, text="存储设置", padding="20")
        self.settings_frame.pack(fill=tk.BOTH, expand=True, pady=20, padx=20)
        
        # 存储路径设置
        self._create_storage_settings()
        
        # 预留设置区域
        self._create_reserved_settings()
        
    def _create_storage_settings(self):
        """创建存储设置区域"""
        # 存储路径标签
        ttk.Label(self.settings_frame, text="程序文件存储路径:", font=('Arial', 10, 'bold')).grid(row=0, column=0, sticky=tk.W, pady=10, padx=10)
        
        # 路径显示
        self.path_var = tk.StringVar(value=str(Config.get_app_data_dir()))
        self.path_entry = ttk.Entry(self.settings_frame, textvariable=self.path_var, width=60, state='disabled')
        self.path_entry.grid(row=0, column=1, pady=10, padx=10)
    
    def _create_reserved_settings(self):
        """创建预留设置区域"""
        # 预留设置框架
        reserved_frame = ttk.LabelFrame(self.tab, text="预留设置", padding="20")
        reserved_frame.pack(fill=tk.BOTH, expand=True, pady=20, padx=20)
        
        # 黑色模式设置（预留）
        ttk.Label(reserved_frame, text="黑色模式:", font=('Arial', 10, 'bold')).grid(row=0, column=0, sticky=tk.W, pady=10, padx=10)
        ttk.Label(reserved_frame, text="功能开发中...").grid(row=0, column=1, sticky=tk.W, pady=10, padx=10)
        
        # 多语言支持（预留）
        ttk.Label(reserved_frame, text="多语言支持:", font=('Arial', 10, 'bold')).grid(row=1, column=0, sticky=tk.W, pady=10, padx=10)
        ttk.Label(reserved_frame, text="功能开发中...").grid(row=1, column=1, sticky=tk.W, pady=10, padx=10)
        
        # 开发者模式（预留）
        ttk.Label(reserved_frame, text="开发者模式:", font=('Arial', 10, 'bold')).grid(row=2, column=0, sticky=tk.W, pady=10, padx=10)
        ttk.Label(reserved_frame, text="功能开发中...").grid(row=2, column=1, sticky=tk.W, pady=10, padx=10)
    

    
    def _reset_settings(self):
        """重置为默认设置"""
        # 重置为默认路径
        default_path = str(Config.get_app_data_dir())
        self.path_var.set(default_path)
        messagebox.showinfo("成功", "已重置为默认存储路径")
    
