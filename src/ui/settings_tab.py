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
        ttk.Label(self.settings_frame, text="默认存储路径:", font=('Arial', 10, 'bold')).grid(row=0, column=0, sticky=tk.W, pady=10, padx=10)
        
        # 路径输入框
        self.path_var = tk.StringVar(value=str(Config.get_app_data_dir()))
        self.path_entry = ttk.Entry(self.settings_frame, textvariable=self.path_var, width=60)
        self.path_entry.grid(row=0, column=1, pady=10, padx=10)
        
        # 移动按钮
        move_button = ttk.Button(self.settings_frame, text="移动", command=self._move_storage)
        move_button.grid(row=0, column=2, pady=10, padx=10)
        
        # 重置按钮
        reset_button = ttk.Button(self.settings_frame, text="重置为默认", command=self._reset_settings)
        reset_button.grid(row=1, column=2, pady=20, padx=10)
    
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
    
    def _move_storage(self):
        """移动存储位置"""
        # 打开文件夹选择对话框
        selected_path = filedialog.askdirectory(title="选择新的存储路径")
        if not selected_path:
            return
        
        # 验证路径有效性
        if not self._validate_path(selected_path):
            messagebox.showerror("错误", "请选择有效的存储路径")
            return
        
        # 迁移数据
        try:
            # 获取当前存储路径
            current_path = str(Config.get_app_data_dir())
            
            # 如果选择的路径与当前路径相同，直接返回
            if selected_path == current_path:
                messagebox.showinfo("提示", "新路径与当前路径相同，无需移动")
                return
            
            # 复制数据文件
            import shutil
            import os
            
            # 确保目标目录存在
            os.makedirs(selected_path, exist_ok=True)
            
            # 复制数据库文件
            db_path = Config.get_db_path()
            if os.path.exists(db_path):
                shutil.copy2(db_path, os.path.join(selected_path, 'passwords.db'))
            
            # 复制密钥文件
            key_path = Config.get_key_path()
            if os.path.exists(key_path):
                shutil.copy2(key_path, os.path.join(selected_path, 'key.key'))
            
            # 复制历史记录文件
            history_path = Config.get_history_path()
            if os.path.exists(history_path):
                shutil.copy2(history_path, os.path.join(selected_path, 'password_history.json'))
            
            # 更新配置文件
            Config.set_app_data_dir(selected_path)
            
            # 更新界面显示
            self.path_var.set(selected_path)
            
            messagebox.showinfo("成功", f"存储位置已成功移动到: {selected_path}\n请重启应用以应用新的存储路径")
        except Exception as e:
            messagebox.showerror("错误", f"移动存储位置失败: {str(e)}")
    
    def _browse_path(self):
        """浏览选择存储路径"""
        # 打开文件夹选择对话框
        selected_path = filedialog.askdirectory(title="选择存储路径")
        if selected_path:
            self.path_var.set(selected_path)
    
    def _save_settings(self):
        """保存设置"""
        # 获取输入的路径
        path = self.path_var.get().strip()
        
        # 验证路径有效性
        if not self._validate_path(path):
            messagebox.showerror("错误", "请选择有效的存储路径")
            return
        
        # 保存设置（这里只是示例，实际需要实现配置持久化）
        try:
            # 这里可以实现配置文件的写入
            # 目前只是显示成功消息
            messagebox.showinfo("成功", f"存储路径已设置为: {path}")
        except Exception as e:
            messagebox.showerror("错误", f"保存设置失败: {str(e)}")
    
    def _reset_settings(self):
        """重置为默认设置"""
        # 重置为默认路径
        default_path = str(Config.get_app_data_dir())
        self.path_var.set(default_path)
        messagebox.showinfo("成功", "已重置为默认存储路径")
    
    def _validate_path(self, path):
        """验证路径有效性
        
        Args:
            path (str): 路径字符串
            
        Returns:
            bool: 路径是否有效
        """
        # 检查路径是否为空
        if not path:
            return False
        
        # 检查路径是否存在
        if not os.path.exists(path):
            # 如果路径不存在，尝试创建
            try:
                os.makedirs(path, exist_ok=True)
                return True
            except:
                return False
        
        # 检查路径是否为目录
        if not os.path.isdir(path):
            return False
        
        # 检查路径是否可写
        try:
            test_file = os.path.join(path, "test_write.txt")
            with open(test_file, 'w') as f:
                f.write("test")
            os.remove(test_file)
            return True
        except:
            return False