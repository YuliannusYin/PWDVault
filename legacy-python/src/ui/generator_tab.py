import tkinter as tk
from tkinter import ttk, messagebox
from src.core.password_generator import PasswordGenerator

class GeneratorTab:
    """密码生成标签页"""
    
    def __init__(self, notebook, save_history_callback, use_password_callback, show_history_callback):
        """初始化密码生成标签页
        
        Args:
            notebook: 标签页容器
            save_history_callback: 保存历史记录的回调函数
            use_password_callback: 使用密码的回调函数
            show_history_callback: 显示历史记录的回调函数
        """
        self.save_history_callback = save_history_callback
        self.use_password_callback = use_password_callback
        self.show_history_callback = show_history_callback
        self.tab = ttk.Frame(notebook)
        notebook.add(self.tab, text="随机密码生成")
        self._create_widgets()
    
    def _create_widgets(self):
        """创建界面组件"""
        # 创建密码生成选项框架
        options_frame = ttk.LabelFrame(self.tab, text="密码生成选项", padding="10")
        options_frame.pack(fill=tk.BOTH, expand=True, pady=10)
        
        # 密码长度
        ttk.Label(options_frame, text="密码长度:").grid(row=0, column=0, sticky=tk.W, pady=5, padx=5)
        self.length_var = tk.IntVar(value=16)
        length_spinbox = ttk.Spinbox(options_frame, from_=8, to=32, textvariable=self.length_var, width=10)
        length_spinbox.grid(row=0, column=1, pady=5, padx=5, sticky=tk.W)
        
        # 包含字符类型
        ttk.Label(options_frame, text="包含字符类型:").grid(row=1, column=0, sticky=tk.W, pady=5, padx=5)
        
        # 大写字母
        self.uppercase_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(options_frame, text="大写字母", variable=self.uppercase_var).grid(row=2, column=1, sticky=tk.W, pady=2, padx=5)
        
        # 小写字母
        self.lowercase_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(options_frame, text="小写字母", variable=self.lowercase_var).grid(row=3, column=1, sticky=tk.W, pady=2, padx=5)
        
        # 数字
        self.digits_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(options_frame, text="数字", variable=self.digits_var).grid(row=4, column=1, sticky=tk.W, pady=2, padx=5)
        
        # 特殊符号
        self.special_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(options_frame, text="特殊符号", variable=self.special_var).grid(row=5, column=1, sticky=tk.W, pady=2, padx=5)
        
        # 生成的密码
        ttk.Label(options_frame, text="生成的密码:").grid(row=6, column=0, sticky=tk.W, pady=5, padx=5)
        self.generated_password_var = tk.StringVar()
        ttk.Entry(options_frame, textvariable=self.generated_password_var, width=50, state="readonly").grid(row=6, column=1, pady=5, padx=5)
        
        # 按钮框架（水平排列）
        button_frame = ttk.Frame(options_frame)
        button_frame.grid(row=7, column=1, sticky=tk.W, pady=10, padx=5)
        
        # 生成按钮
        generate_button = ttk.Button(button_frame, text="生成密码", command=self._generate_password, width=10)
        generate_button.pack(side=tk.LEFT, padx=5)
        
        # 复制按钮
        copy_button = ttk.Button(button_frame, text="复制", command=self._copy_password, width=8)
        copy_button.pack(side=tk.LEFT, padx=5)
        
        # 使用按钮
        use_button = ttk.Button(button_frame, text="使用", command=self._use_password, width=8)
        use_button.pack(side=tk.LEFT, padx=5)
        
        # 历史按钮
        history_button = ttk.Button(button_frame, text="历史", command=self.show_history_callback, width=8)
        history_button.pack(side=tk.LEFT, padx=5)
    
    def _generate_password(self):
        """生成密码"""
        try:
            password = PasswordGenerator.generate_password(
                length=self.length_var.get(),
                include_uppercase=self.uppercase_var.get(),
                include_lowercase=self.lowercase_var.get(),
                include_digits=self.digits_var.get(),
                include_special=self.special_var.get()
            )
            self.generated_password_var.set(password)
            # 保存到历史记录
            self.save_history_callback(password)
        except ValueError as e:
            messagebox.showerror("错误", str(e))
    
    def _copy_password(self):
        """复制密码到剪贴板"""
        password = self.generated_password_var.get()
        if not password:
            messagebox.showinfo("提示", "请先生成密码")
            return
        
        # 复制到剪贴板
        self.tab.clipboard_clear()
        self.tab.clipboard_append(password)
        messagebox.showinfo("成功", "密码已复制到剪贴板")
    
    def _use_password(self):
        """使用密码"""
        password = self.generated_password_var.get()
        if not password:
            messagebox.showinfo("提示", "请先生成密码")
            return
        
        # 调用回调函数使用密码
        self.use_password_callback(password)