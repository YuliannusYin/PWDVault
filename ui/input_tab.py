import tkinter as tk
from tkinter import ttk, messagebox

class InputTab:
    """密码录入标签页"""
    
    def __init__(self, notebook, save_callback):
        """初始化密码录入标签页
        
        Args:
            notebook: 标签页容器
            save_callback: 保存密码的回调函数
        """
        self.save_callback = save_callback
        self.tab = ttk.Frame(notebook)
        notebook.add(self.tab, text="账号密码录入")
        self._create_widgets()
    
    def _create_widgets(self):
        """创建界面组件"""
        # 创建表单框架
        form_frame = ttk.LabelFrame(self.tab, text="录入信息", padding="10")
        form_frame.pack(fill=tk.BOTH, expand=True, pady=10)
        
        # 编号
        ttk.Label(form_frame, text="编号:").grid(row=0, column=0, sticky=tk.W, pady=5, padx=5)
        self.number_var = tk.StringVar()
        ttk.Entry(form_frame, textvariable=self.number_var, width=20).grid(row=0, column=1, pady=5, padx=5, sticky=tk.W)
        
        # 网站/应用名称
        ttk.Label(form_frame, text="网站/应用名称:").grid(row=1, column=0, sticky=tk.W, pady=5, padx=5)
        self.website_var = tk.StringVar()
        ttk.Entry(form_frame, textvariable=self.website_var, width=50).grid(row=1, column=1, pady=5, padx=5)
        
        # 账号/用户名
        ttk.Label(form_frame, text="账号/用户名:").grid(row=2, column=0, sticky=tk.W, pady=5, padx=5)
        self.username_var = tk.StringVar()
        ttk.Entry(form_frame, textvariable=self.username_var, width=50).grid(row=2, column=1, pady=5, padx=5)
        
        # 密码
        ttk.Label(form_frame, text="密码:").grid(row=3, column=0, sticky=tk.W, pady=5, padx=5)
        self.password_var = tk.StringVar()
        ttk.Entry(form_frame, textvariable=self.password_var, width=50).grid(row=3, column=1, pady=5, padx=5)
        
        # 备注
        ttk.Label(form_frame, text="备注:").grid(row=4, column=0, sticky=tk.NW, pady=5, padx=5)
        self.note_var = tk.StringVar()
        ttk.Entry(form_frame, textvariable=self.note_var, width=50).grid(row=4, column=1, pady=5, padx=5)
        
        # 敏感性
        ttk.Label(form_frame, text="敏感性:").grid(row=5, column=0, sticky=tk.W, pady=5, padx=5)
        self.sensitivity_var = tk.BooleanVar(value=False)
        sensitivity_frame = ttk.Frame(form_frame)
        sensitivity_frame.grid(row=5, column=1, pady=5, padx=5, sticky=tk.W)
        ttk.Radiobutton(sensitivity_frame, text="普通 (0)", variable=self.sensitivity_var, value=False).pack(side=tk.LEFT, padx=10)
        ttk.Radiobutton(sensitivity_frame, text="敏感 (1)", variable=self.sensitivity_var, value=True).pack(side=tk.LEFT, padx=10)
        
        # 关联信息
        ttk.Label(form_frame, text="关联信息:").grid(row=6, column=0, sticky=tk.NW, pady=5, padx=5)
        self.related_info_var = tk.StringVar()
        ttk.Entry(form_frame, textvariable=self.related_info_var, width=50).grid(row=6, column=1, pady=5, padx=5)
        
        # 提交按钮
        submit_button = ttk.Button(form_frame, text="保存", command=self._save_password)
        submit_button.grid(row=7, column=1, sticky=tk.E, pady=10, padx=5)
    
    def _save_password(self):
        """保存密码"""
        number_str = self.number_var.get().strip()
        number = int(number_str) if number_str else None
        website = self.website_var.get().strip()
        username = self.username_var.get().strip()
        password = self.password_var.get().strip()
        note = self.note_var.get().strip()
        sensitivity = 1 if self.sensitivity_var.get() else 0
        related_info = self.related_info_var.get().strip()
        
        # 数据验证
        if not website:
            messagebox.showerror("错误", "网站/应用名称不能为空")
            return
        if not username:
            messagebox.showerror("错误", "账号/用户名不能为空")
            return
        if not password:
            messagebox.showerror("错误", "密码不能为空")
            return
        if number_str and not number_str.isdigit():
            messagebox.showerror("错误", "编号必须是整数")
            return
        
        # 调用回调函数保存密码
        self.save_callback(website, username, password, note, number, sensitivity, related_info)
        
        # 清空表单
        self.number_var.set("")
        self.website_var.set("")
        self.username_var.set("")
        self.password_var.set("")
        self.note_var.set("")
        self.sensitivity_var.set(False)
        self.related_info_var.set("")
    
    def set_password(self, password):
        """设置密码输入框的值
        
        Args:
            password (str): 密码值
        """
        self.password_var.set(password)