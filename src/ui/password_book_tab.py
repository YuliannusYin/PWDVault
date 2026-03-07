import tkinter as tk
from tkinter import ttk, messagebox

class PasswordBookTab:
    """密码本标签页"""
    
    def __init__(self, notebook, load_passwords_callback, search_passwords_callback, view_password_callback, edit_password_callback, delete_password_callback, copy_password_callback):
        """初始化密码本标签页
        
        Args:
            notebook: 标签页容器
            load_passwords_callback: 加载密码的回调函数
            search_passwords_callback: 搜索密码的回调函数
            view_password_callback: 查看密码的回调函数
            edit_password_callback: 编辑密码的回调函数
            delete_password_callback: 删除密码的回调函数
            copy_password_callback: 复制密码的回调函数
        """
        self.load_passwords_callback = load_passwords_callback
        self.search_passwords_callback = search_passwords_callback
        self.view_password_callback = view_password_callback
        self.edit_password_callback = edit_password_callback
        self.delete_password_callback = delete_password_callback
        self.copy_password_callback = copy_password_callback
        self.tab = ttk.Frame(notebook)
        notebook.add(self.tab, text="密码本")
        self._create_widgets()
        # 存储原始数据
        self.original_data = []
    
    def _create_widgets(self):
        """创建界面组件"""
        # 搜索框
        search_frame = ttk.LabelFrame(self.tab, text="搜索选项", padding="10")
        search_frame.pack(fill=tk.X, pady=10)
        
        # 搜索关键词
        ttk.Label(search_frame, text="搜索关键词:").grid(row=0, column=0, sticky=tk.W, pady=5, padx=5)
        self.search_var = tk.StringVar()
        search_entry = ttk.Entry(search_frame, textvariable=self.search_var, width=40)
        search_entry.grid(row=0, column=1, pady=5, padx=5, sticky=tk.W)
        
        # 数据脱敏开关
        self.masking_var = tk.BooleanVar(value=False)
        masking_frame = ttk.Frame(search_frame)
        masking_frame.grid(row=0, column=3, pady=5, padx=10, sticky=tk.E)
        ttk.Label(masking_frame, text="数据脱敏:").pack(side=tk.LEFT, padx=5)
        ttk.Checkbutton(masking_frame, text="开启", variable=self.masking_var, command=self._toggle_masking).pack(side=tk.LEFT, padx=5)
        
        # 搜索范围选择
        ttk.Label(search_frame, text="搜索范围:").grid(row=1, column=0, sticky=tk.W, pady=5, padx=5)
        
        # 搜索范围选项
        self.search_number_var = tk.BooleanVar(value=False)
        self.search_website_var = tk.BooleanVar(value=True)
        self.search_username_var = tk.BooleanVar(value=False)
        self.search_password_var = tk.BooleanVar(value=False)
        self.search_note_var = tk.BooleanVar(value=False)
        self.search_related_info_var = tk.BooleanVar(value=False)
        
        # 搜索范围复选框 - 分为两行显示
        search_range_frame = ttk.Frame(search_frame)
        search_range_frame.grid(row=1, column=1, pady=5, padx=5, sticky=tk.W)
        
        # 第一行搜索选项
        search_range_row1 = ttk.Frame(search_range_frame)
        search_range_row1.pack(side=tk.TOP, fill=tk.X)
        
        # 第二行搜索选项
        search_range_row2 = ttk.Frame(search_range_frame)
        search_range_row2.pack(side=tk.TOP, fill=tk.X)
        
        # 回调函数：处理密码选项的勾选
        def on_password_check():
            if self.search_password_var.get():
                # 当勾选密码时，取消其他所有选项
                self.search_number_var.set(False)
                self.search_website_var.set(False)
                self.search_username_var.set(False)
                self.search_note_var.set(False)
                self.search_related_info_var.set(False)
        
        # 回调函数：处理非密码选项的勾选
        def on_other_check():
            # 当勾选其他选项时，取消密码选项
            if self.search_number_var.get() or self.search_website_var.get() or self.search_username_var.get() or self.search_note_var.get() or self.search_related_info_var.get():
                self.search_password_var.set(False)
        
        # 第一行复选框
        ttk.Checkbutton(search_range_row1, text="编号", variable=self.search_number_var, command=on_other_check).pack(side=tk.LEFT, padx=10)
        ttk.Checkbutton(search_range_row1, text="网站/应用", variable=self.search_website_var, command=on_other_check).pack(side=tk.LEFT, padx=10)
        ttk.Checkbutton(search_range_row1, text="账号/用户名", variable=self.search_username_var, command=on_other_check).pack(side=tk.LEFT, padx=10)
        
        # 第二行复选框
        ttk.Checkbutton(search_range_row2, text="密码", variable=self.search_password_var, command=on_password_check).pack(side=tk.LEFT, padx=10)
        ttk.Checkbutton(search_range_row2, text="备注", variable=self.search_note_var, command=on_other_check).pack(side=tk.LEFT, padx=10)
        ttk.Checkbutton(search_range_row2, text="关联信息", variable=self.search_related_info_var, command=on_other_check).pack(side=tk.LEFT, padx=10)
        
        # 搜索按钮
        search_button = ttk.Button(search_frame, text="搜索", command=self._search_passwords)
        search_button.grid(row=0, column=2, pady=5, padx=10, sticky=tk.W)
        
        # 密码列表
        self.tree_frame = ttk.Frame(self.tab)
        self.tree_frame.pack(fill=tk.BOTH, expand=True)
        
        self.tree = ttk.Treeview(self.tree_frame, columns=('id', 'number', 'website', 'username', 'password', 'note', 'sensitivity', 'related_info'), show='headings')
        # 隐藏ID列
        self.tree.column('id', width=0, stretch=tk.NO)
        self.tree.heading('id', text='ID')
        # 隐藏敏感性列
        self.tree.column('sensitivity', width=0, stretch=tk.NO)
        self.tree.heading('sensitivity', text='敏感性')
        # 显示其他列
        self.tree.heading('number', text='编号')
        self.tree.heading('website', text='网站/应用')
        self.tree.heading('username', text='账号/用户名')
        self.tree.heading('password', text='密码')
        self.tree.heading('note', text='备注')
        self.tree.heading('related_info', text='关联信息')
        
        # 设置列宽
        self.tree.column('number', width=90)
        self.tree.column('website', width=200)
        self.tree.column('username', width=160)
        self.tree.column('password', width=160)
        self.tree.column('note', width=140)
        self.tree.column('related_info', width=180)
        
        # 垂直滚动条
        v_scrollbar = ttk.Scrollbar(self.tree_frame, orient=tk.VERTICAL, command=self.tree.yview)
        self.tree.configure(yscroll=v_scrollbar.set)
        
        # 水平滚动条
        h_scrollbar = ttk.Scrollbar(self.tree_frame, orient=tk.HORIZONTAL, command=self.tree.xview)
        self.tree.configure(xscroll=h_scrollbar.set)
        
        # 使用grid布局来正确放置树状视图和滚动条
        self.tree_frame.grid_rowconfigure(0, weight=1)
        self.tree_frame.grid_columnconfigure(0, weight=1)
        
        # 放置树状视图
        self.tree.grid(row=0, column=0, sticky=tk.NSEW)
        
        # 放置垂直滚动条
        v_scrollbar.grid(row=0, column=1, sticky=tk.NS)
        
        # 放置水平滚动条
        h_scrollbar.grid(row=1, column=0, sticky=tk.EW)
        
        # 操作按钮
        button_frame = ttk.Frame(self.tab)
        button_frame.pack(fill=tk.X, pady=10)
        
        view_button = ttk.Button(button_frame, text="查看详情", command=self._view_password)
        view_button.pack(side=tk.LEFT, padx=5)
        
        edit_button = ttk.Button(button_frame, text="编辑", command=self._edit_password)
        edit_button.pack(side=tk.LEFT, padx=5)
        
        delete_button = ttk.Button(button_frame, text="删除", command=self._delete_password)
        delete_button.pack(side=tk.LEFT, padx=5)
        
        refresh_button = ttk.Button(button_frame, text="刷新", command=self.load_passwords_callback)
        refresh_button.pack(side=tk.LEFT, padx=5)
        
        # 复制密码按钮
        copy_pass_button = ttk.Button(button_frame, text="复制密码", command=self._copy_selected_password)
        copy_pass_button.pack(side=tk.LEFT, padx=5)
    
    def _search_passwords(self):
        """搜索密码"""
        search_term = self.search_var.get().strip()
        self.search_passwords_callback(
            search_term,
            self.search_website_var.get(),
            self.search_username_var.get(),
            self.search_password_var.get(),
            self.search_note_var.get(),
            self.search_number_var.get(),
            self.search_related_info_var.get()
        )
    
    def _view_password(self):
        """查看密码详情"""
        selected_item = self.tree.selection()
        if not selected_item:
            messagebox.showinfo("提示", "请选择要查看的密码")
            return
        
        item = self.tree.item(selected_item[0])
        values = item['values']
        self.view_password_callback(values)
    
    def _edit_password(self):
        """编辑密码"""
        selected_item = self.tree.selection()
        if not selected_item:
            messagebox.showinfo("提示", "请选择要编辑的密码")
            return
        
        item = self.tree.item(selected_item[0])
        values = item['values']
        self.edit_password_callback(values)
    
    def _delete_password(self):
        """删除密码"""
        selected_item = self.tree.selection()
        if not selected_item:
            messagebox.showinfo("提示", "请选择要删除的密码")
            return
        
        item = self.tree.item(selected_item[0])
        values = item['values']
        self.delete_password_callback(values)
    
    def _copy_selected_password(self):
        """复制选中的密码"""
        selected_item = self.tree.selection()
        if not selected_item:
            messagebox.showinfo("提示", "请选择要复制密码的项目")
            return
        
        item = self.tree.item(selected_item[0])
        values = item['values']
        self.copy_password_callback(values)
    
    def clear_tree(self):
        """清空树状视图"""
        for item in self.tree.get_children():
            self.tree.delete(item)
        # 清空原始数据存储
        self.original_data.clear()
    
    def _toggle_masking(self):
        """切换数据脱敏状态"""
        # 只清空树状视图，保留原始数据
        for item in self.tree.get_children():
            self.tree.delete(item)
        # 重新加载数据，应用脱敏或取消脱敏
        if self.masking_var.get():
            # 应用脱敏
            for data in self.original_data:
                # 检查是否为敏感数据
                if data[6] == 1:
                    # 敏感数据，不显示
                    continue
                masked_data = self._mask_data(data)
                self.tree.insert('', tk.END, values=masked_data)
        else:
            # 显示原始数据
            for data in self.original_data:
                self.tree.insert('', tk.END, values=data)
    
    def _mask_data(self, values):
        """对数据进行脱敏处理
        
        Args:
            values: 原始数据值 (id, number, website, username, password, note, sensitivity, related_info)
        
        Returns:
            脱敏后的数据值
        """
        id_val, number, website, username, password, note, sensitivity, related_info = values
        
        # 密码完全脱敏
        masked_password = '*' * len(password)
        
        # 账号信息脱敏（邮箱格式）
        masked_username = username
        if '@' in username:
            # 邮箱格式，保留前3位，中间用等量*替代，保留@及之后的域名
            parts = username.split('@')
            if len(parts[0]) > 3:
                masked_part = '*' * (len(parts[0]) - 3)
                masked_username = f"{parts[0][:3]}{masked_part}@{parts[1]}"
        else:
            # 非邮箱格式，保留前3位，中间用等量*替代
            if len(username) > 3:
                masked_part = '*' * (len(username) - 3)
                masked_username = f"{username[:3]}{masked_part}"
        
        # 关联信息脱敏
        masked_related_info = '***' if related_info else ''
        
        return (id_val, number, website, masked_username, masked_password, note, sensitivity, masked_related_info)
    
    def add_to_tree(self, values):
        """添加数据到树状视图
        
        Args:
            values: 数据值 (id, number, website, username, password, note, sensitivity, related_info)
        """
        # 存储原始数据
        self.original_data.append(values)
        
        # 根据脱敏状态决定显示的数据
        if self.masking_var.get():
            # 检查是否为敏感数据
            if values[6] == 1:
                # 敏感数据，不显示
                return
            masked_data = self._mask_data(values)
            self.tree.insert('', tk.END, values=masked_data)
        else:
            self.tree.insert('', tk.END, values=values)