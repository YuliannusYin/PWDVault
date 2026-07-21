import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from src.core.config import Config
import os
import subprocess

# 确保应用数据目录存在
app_data_dir = Config.get_app_data_dir()

# 手动创建目录，确保万无一失
os.makedirs(app_data_dir, exist_ok=True)

class SettingsTab:
    """程序设置标签页"""
    
    def __init__(self, parent, cache_manager=None):
        """初始化设置标签页
        
        Args:
            parent: 父窗口
            cache_manager: 缓存管理器实例
        """
        self.parent = parent
        self.cache_manager = cache_manager
        self.tab = ttk.Frame(parent)
        parent.add(self.tab, text="程序设置")
        
        # 创建滚动区域
        self.canvas = tk.Canvas(self.tab)
        self.scrollbar = ttk.Scrollbar(self.tab, orient=tk.VERTICAL, command=self.canvas.yview)
        self.scrollable_frame = ttk.Frame(self.canvas)
        
        # 配置滚动区域
        self.scrollable_frame.bind(
            "<Configure>",
            lambda e: self.canvas.configure(
                scrollregion=self.canvas.bbox("all")
            )
        )
        
        self.canvas.create_window((0, 0), window=self.scrollable_frame, anchor="nw")
        self.canvas.configure(yscrollcommand=self.scrollbar.set)
        
        # 布局滚动区域
        self.canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        self.scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        
        # 创建设置框架
        self.settings_frame = ttk.LabelFrame(self.scrollable_frame, text="存储设置", padding="20")
        self.settings_frame.pack(fill=tk.BOTH, expand=True, pady=20, padx=20)
        
        # 存储路径设置
        self._create_storage_settings()
        
        # 常规设置区域
        self._create_general_settings()
        
        # 预留设置区域
        self._create_reserved_settings()
        
        # 添加鼠标滚轮支持
        self.canvas.bind_all("<MouseWheel>", lambda e: self.canvas.yview_scroll(int(-1*(e.delta/120)), "units"))
        
    def _create_storage_settings(self):
        """创建存储设置区域"""
        # 存储路径标签
        ttk.Label(self.settings_frame, text="程序文件存储路径:", font=('Arial', 10, 'bold')).grid(row=0, column=0, sticky=tk.W, pady=10, padx=10)
        
        # 路径显示
        self.path_var = tk.StringVar(value=str(Config.get_app_data_dir()))
        self.path_entry = ttk.Entry(self.settings_frame, textvariable=self.path_var, width=60, state='disabled')
        self.path_entry.grid(row=0, column=1, pady=10, padx=10, columnspan=2)
        
        # 按钮框架
        button_frame = ttk.Frame(self.settings_frame)
        button_frame.grid(row=1, column=1, pady=10, padx=10, sticky=tk.W)
        
        # 打开文件夹按钮
        open_folder_button = ttk.Button(button_frame, text="打开文件夹", command=self._open_folder)
        open_folder_button.pack(side=tk.LEFT, padx=5)
        
        # 重置按钮
        reset_button = ttk.Button(button_frame, text="重置为默认", command=self._reset_settings)
        reset_button.pack(side=tk.LEFT, padx=5)
    
    def _create_general_settings(self):
        """创建常规设置区域"""
        # 常规设置框架
        general_frame = ttk.LabelFrame(self.scrollable_frame, text="常规设置", padding="20")
        general_frame.pack(fill=tk.BOTH, expand=True, pady=20, padx=20)
        
        # 黑色模式设置
        ttk.Label(general_frame, text="黑色模式:", font=('Arial', 10, 'bold')).grid(row=0, column=0, sticky=tk.W, pady=10, padx=10)
        self.dark_mode_var = tk.BooleanVar(value=Config.get('dark_mode', False))
        dark_mode_check = ttk.Checkbutton(general_frame, variable=self.dark_mode_var, command=self._save_settings)
        dark_mode_check.grid(row=0, column=1, sticky=tk.W, pady=10, padx=10)
        
        # 自动清除剪贴板
        ttk.Label(general_frame, text="自动清除剪贴板:", font=('Arial', 10, 'bold')).grid(row=1, column=0, sticky=tk.W, pady=10, padx=10)
        self.auto_clear_clipboard_var = tk.BooleanVar(value=Config.get('auto_clear_clipboard', True))
        auto_clear_check = ttk.Checkbutton(general_frame, variable=self.auto_clear_clipboard_var, command=self._save_settings)
        auto_clear_check.grid(row=1, column=1, sticky=tk.W, pady=10, padx=10)
        
        # 剪贴板清除时间
        ttk.Label(general_frame, text="剪贴板清除时间(秒):", font=('Arial', 10, 'bold')).grid(row=2, column=0, sticky=tk.W, pady=10, padx=10)
        self.clipboard_clear_time_var = tk.IntVar(value=Config.get('clipboard_clear_time', 30))
        time_spinbox = ttk.Spinbox(general_frame, from_=5, to=300, textvariable=self.clipboard_clear_time_var, width=10, command=self._save_settings)
        time_spinbox.grid(row=2, column=1, sticky=tk.W, pady=10, padx=10)
        
        # 默认显示密码
        ttk.Label(general_frame, text="默认显示密码:", font=('Arial', 10, 'bold')).grid(row=3, column=0, sticky=tk.W, pady=10, padx=10)
        self.show_password_var = tk.BooleanVar(value=Config.get('show_password_by_default', False))
        show_password_check = ttk.Checkbutton(general_frame, variable=self.show_password_var, command=self._save_settings)
        show_password_check.grid(row=3, column=1, sticky=tk.W, pady=10, padx=10)
    
    def _create_reserved_settings(self):
        """创建预留设置区域"""
        # 预留设置框架
        reserved_frame = ttk.LabelFrame(self.scrollable_frame, text="预留设置", padding="20")
        reserved_frame.pack(fill=tk.BOTH, expand=True, pady=20, padx=20)
        
        # 多语言支持（预留）
        ttk.Label(reserved_frame, text="多语言支持:", font=('Arial', 10, 'bold')).grid(row=0, column=0, sticky=tk.W, pady=10, padx=10)
        ttk.Label(reserved_frame, text="功能开发中...").grid(row=0, column=1, sticky=tk.W, pady=10, padx=10)
        
        # 开发者模式（预留）
        ttk.Label(reserved_frame, text="开发者模式:", font=('Arial', 10, 'bold')).grid(row=1, column=0, sticky=tk.W, pady=10, padx=10)
        ttk.Label(reserved_frame, text="功能开发中...").grid(row=1, column=1, sticky=tk.W, pady=10, padx=10)
        
        # 缓存监控按钮（新增）
        ttk.Label(reserved_frame, text="缓存监控:", font=('Arial', 10, 'bold')).grid(row=2, column=0, sticky=tk.W, pady=10, padx=10)
        cache_monitor_button = ttk.Button(reserved_frame, text="查看缓存状态", command=self._show_cache_monitor)
        cache_monitor_button.grid(row=2, column=1, sticky=tk.W, pady=10, padx=10)
        
        # 日志查看按钮（新增）
        ttk.Label(reserved_frame, text="日志查看:", font=('Arial', 10, 'bold')).grid(row=3, column=0, sticky=tk.W, pady=10, padx=10)
        log_viewer_button = ttk.Button(reserved_frame, text="查看日志", command=self._show_logs)
        log_viewer_button.grid(row=3, column=1, sticky=tk.W, pady=10, padx=10)
    
    def _show_cache_monitor(self):
        """显示缓存监控窗口"""
        # 导入CacheMonitor类
        from src.ui.cache_monitor import CacheMonitor
        
        # 显示缓存监控窗口
        if self.cache_manager:
            monitor = CacheMonitor(self.parent.master, self.cache_manager)
            monitor.show_monitor()
        else:
            # 如果没有缓存管理器实例，创建一个新的
            from src.core.cache import CacheManager
            cache_manager = CacheManager()
            monitor = CacheMonitor(self.parent.master, cache_manager)
            monitor.show_monitor()
    
    def _show_logs(self):
        """显示日志查看窗口"""
        # 导入LogViewer类
        from src.ui.log_viewer import LogViewer
        
        # 显示日志查看窗口
        viewer = LogViewer(self.parent.master)
        viewer.show_logs()
    
    def _save_settings(self):
        """保存设置"""
        try:
            # 保存设置到配置文件
            Config.set('dark_mode', self.dark_mode_var.get())
            Config.set('auto_clear_clipboard', self.auto_clear_clipboard_var.get())
            Config.set('clipboard_clear_time', self.clipboard_clear_time_var.get())
            Config.set('show_password_by_default', self.show_password_var.get())
            
            # 显示成功消息
            messagebox.showinfo("成功", "设置已保存")
        except Exception as e:
            messagebox.showerror("错误", f"保存设置失败: {str(e)}")
    
    def _open_folder(self):
        """打开存储文件夹"""
        try:
            # 导入Logger类
            from src.core.logger import Logger
            logger = Logger()
            
            # 获取存储路径
            path = self.path_var.get()
            
            # 验证路径有效性
            if not path:
                messagebox.showerror("错误", "存储路径为空")
                logger.log("打开文件夹", "失败", path=path, error="存储路径为空")
                return
            
            # 验证路径是否存在
            if not os.path.exists(path):
                messagebox.showerror("错误", "存储路径不存在")
                logger.log("打开文件夹", "失败", path=path, error="存储路径不存在")
                return
            
            # 验证路径是否为目录
            if not os.path.isdir(path):
                messagebox.showerror("错误", "存储路径不是有效的目录")
                logger.log("打开文件夹", "失败", path=path, error="存储路径不是有效的目录")
                return
            
            # 验证访问权限
            try:
                os.listdir(path)
            except PermissionError:
                messagebox.showerror("错误", "没有访问该路径的权限")
                logger.log("打开文件夹", "失败", path=path, error="没有访问该路径的权限")
                return
            
            # 打开文件资源管理器
            if os.name == 'nt':  # Windows
                # 在Windows中，explorer命令可能返回非零值但仍成功打开文件夹
                subprocess.run(['explorer', path], check=False)
            elif os.name == 'posix':  # macOS or Linux
                if os.uname().sysname == 'Darwin':  # macOS
                    subprocess.run(['open', path], check=True)
                else:  # Linux
                    subprocess.run(['xdg-open', path], check=True)
            
            # 记录成功日志
            logger.log("打开文件夹", "成功", path=path)
            
        except Exception as e:
            messagebox.showerror("错误", f"打开文件夹失败: {str(e)}")
            # 导入Logger类
            from src.core.logger import Logger
            logger = Logger()
            logger.log("打开文件夹", "失败", path=self.path_var.get(), error=str(e))

    def _reset_settings(self):
        """重置为默认设置"""
        try:
            # 重置为默认路径
            default_path = str(Config.get_app_data_dir())
            self.path_var.set(default_path)
            
            # 重置其他设置
            self.dark_mode_var.set(False)
            self.auto_clear_clipboard_var.set(True)
            self.clipboard_clear_time_var.set(30)
            self.show_password_var.set(False)
            
            # 保存默认设置
            self._save_settings()
            
            messagebox.showinfo("成功", "已重置为默认设置")
        except Exception as e:
            messagebox.showerror("错误", f"重置设置失败: {str(e)}")
    
