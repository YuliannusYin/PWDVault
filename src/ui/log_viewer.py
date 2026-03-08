import tkinter as tk
from tkinter import ttk, messagebox
from src.core.logger import Logger

class LogViewer:
    """日志查看器"""
    
    def __init__(self, parent):
        """初始化日志查看器
        
        Args:
            parent: 父窗口
        """
        self.parent = parent
        self.logger = Logger()
        self.window = None
    
    def show_logs(self):
        """显示日志窗口"""
        # 创建日志窗口
        self.window = tk.Toplevel(self.parent)
        self.window.title("日志查看")
        self.window.geometry("800x600")
        self.window.resizable(True, True)
        
        # 创建主框架
        main_frame = ttk.LabelFrame(self.window, text="日志记录", padding="20")
        main_frame.pack(fill=tk.BOTH, expand=True, pady=10)
        
        # 创建日志列表
        log_frame = ttk.Frame(main_frame)
        log_frame.pack(fill=tk.BOTH, expand=True)
        
        # 创建树状视图
        columns = ('timestamp', 'operation', 'result', 'details')
        self.tree = ttk.Treeview(log_frame, columns=columns, show='headings')
        
        # 设置列标题
        self.tree.heading('timestamp', text='时间')
        self.tree.heading('operation', text='操作')
        self.tree.heading('result', text='结果')
        self.tree.heading('details', text='详情')
        
        # 设置列宽
        self.tree.column('timestamp', width=200)
        self.tree.column('operation', width=150)
        self.tree.column('result', width=100)
        self.tree.column('details', width=350)
        
        # 垂直滚动条
        v_scrollbar = ttk.Scrollbar(log_frame, orient=tk.VERTICAL, command=self.tree.yview)
        self.tree.configure(yscroll=v_scrollbar.set)
        
        # 水平滚动条
        h_scrollbar = ttk.Scrollbar(log_frame, orient=tk.HORIZONTAL, command=self.tree.xview)
        self.tree.configure(xscroll=h_scrollbar.set)
        
        # 布局
        self.tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        v_scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        h_scrollbar.pack(side=tk.BOTTOM, fill=tk.X)
        
        # 按钮框架
        button_frame = ttk.Frame(main_frame)
        button_frame.pack(fill=tk.X, pady=10)
        
        # 刷新按钮
        refresh_button = ttk.Button(button_frame, text="刷新", command=self._refresh_logs)
        refresh_button.pack(side=tk.LEFT, padx=5)
        
        # 清空日志按钮
        clear_button = ttk.Button(button_frame, text="清空日志", command=self._clear_logs)
        clear_button.pack(side=tk.LEFT, padx=5)
        
        # 打开日志文件按钮
        open_file_button = ttk.Button(button_frame, text="打开日志文件", command=self._open_log_file)
        open_file_button.pack(side=tk.LEFT, padx=5)
        
        # 初始加载日志
        self._refresh_logs()
    
    def _refresh_logs(self):
        """刷新日志"""
        # 清空树状视图
        for item in self.tree.get_children():
            self.tree.delete(item)
        
        # 获取日志
        logs = self.logger.get_logs()
        
        # 倒序显示，最新的日志在前面
        for log in reversed(logs):
            # 格式化时间
            timestamp = log.get('timestamp', '')
            if timestamp:
                # 将ISO格式时间转换为更友好的格式
                import datetime
                try:
                    dt = datetime.datetime.fromisoformat(timestamp)
                    timestamp = dt.strftime('%Y-%m-%d %H:%M:%S')
                except Exception:
                    pass
            
            # 构建详情
            details = []
            for key, value in log.items():
                if key not in ['timestamp', 'operation', 'result']:
                    details.append(f"{key}: {value}")
            details_str = '; '.join(details) if details else '无'
            
            # 添加到树状视图
            self.tree.insert('', tk.END, values=(
                timestamp,
                log.get('operation', ''),
                log.get('result', ''),
                details_str
            ))
    
    def _clear_logs(self):
        """清空日志"""
        if messagebox.askyesno("确认清空", "确定要清空所有日志吗？"):
            self.logger.clear_logs()
            self._refresh_logs()
            messagebox.showinfo("成功", "日志已清空")
    
    def _open_log_file(self):
        """打开日志文件"""
        import os
        import subprocess
        
        log_file_path = self.logger.get_log_file_path()
        if os.path.exists(log_file_path):
            try:
                if os.name == 'nt':  # Windows
                    subprocess.run(['notepad.exe', log_file_path])
                elif os.name == 'posix':  # macOS or Linux
                    if os.uname().sysname == 'Darwin':  # macOS
                        subprocess.run(['open', log_file_path])
                    else:  # Linux
                        subprocess.run(['xdg-open', log_file_path])
            except Exception as e:
                messagebox.showerror("错误", f"打开日志文件失败: {str(e)}")
        else:
            messagebox.showinfo("提示", "日志文件不存在")