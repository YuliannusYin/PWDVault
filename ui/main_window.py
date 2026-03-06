import tkinter as tk
from tkinter import ttk, messagebox
from ui.input_tab import InputTab
from ui.generator_tab import GeneratorTab
from ui.password_book_tab import PasswordBookTab
from database import DatabaseManager
from encryption import EncryptionManager
from history_manager import HistoryManager

class MainWindow:
    """主窗口类"""
    
    def __init__(self, root):
        """初始化主窗口
        
        Args:
            root: 根窗口
        """
        self.root = root
        self.root.title("密码管理程序")
        self.root.geometry("800x600")
        self.root.resizable(True, True)
        
        # 初始化管理器
        self.db_manager = DatabaseManager()
        self.encryption_manager = EncryptionManager()
        self.history_manager = HistoryManager()
        
        # 创建主框架
        self.main_frame = ttk.Frame(self.root, padding="20")
        self.main_frame.pack(fill=tk.BOTH, expand=True)
        
        # 创建标签页
        self.notebook = ttk.Notebook(self.main_frame)
        self.notebook.pack(fill=tk.BOTH, expand=True)
        
        # 创建各个功能标签页
        self._create_tabs()
        
        # 创建退出按钮
        self.exit_button = ttk.Button(self.main_frame, text="退出程序", command=self.exit_program)
        self.exit_button.pack(pady=10)
    
    def _create_tabs(self):
        """创建标签页"""
        # 创建密码录入标签页
        self.input_tab = InputTab(self.notebook, self.save_password)
        
        # 创建密码生成标签页
        self.generator_tab = GeneratorTab(
            self.notebook,
            self.save_to_history,
            self.use_password,
            self.show_history
        )
        
        # 创建密码本标签页
        self.password_book_tab = PasswordBookTab(
            self.notebook,
            self.load_passwords,
            self.search_passwords,
            self.view_password,
            self.edit_password,
            self.delete_password,
            self.copy_selected_password
        )
        
        # 加载初始数据
        self.load_passwords()
    
    def save_password(self, website, username, password, note):
        """保存密码
        
        Args:
            website (str): 网站/应用名称
            username (str): 账号/用户名
            password (str): 原始密码
            note (str): 备注
        """
        # 加密密码
        encrypted_password = self.encryption_manager.encrypt_password(password)
        # 保存到数据库
        self.db_manager.add_password(website, username, encrypted_password, note)
        messagebox.showinfo("成功", "密码保存成功")
    
    def load_passwords(self):
        """加载密码"""
        # 清空树状视图
        self.password_book_tab.clear_tree()
        # 加载所有密码
        rows = self.db_manager.get_all_passwords()
        for row in rows:
            # 解密密码
            decrypted_password = self.encryption_manager.decrypt_password(row[3])
            self.password_book_tab.add_to_tree((row[0], row[1], row[2], decrypted_password, row[4]))
    
    def search_passwords(self, search_term, search_id, search_website, search_username, search_password, search_note):
        """搜索密码
        
        Args:
            search_term (str): 搜索关键词
            search_id (bool): 是否搜索ID
            search_website (bool): 是否搜索网站/应用
            search_username (bool): 是否搜索账号/用户名
            search_password (bool): 是否搜索密码
            search_note (bool): 是否搜索备注
        """
        # 清空树状视图
        self.password_book_tab.clear_tree()
        # 搜索密码
        rows = self.db_manager.search_passwords(search_term, search_id, search_website, search_username, search_note)
        
        # 处理密码搜索的特殊情况
        if search_password and search_term:
            filtered_rows = []
            for row in rows:
                decrypted_password = self.encryption_manager.decrypt_password(row[3])
                if search_term in decrypted_password:
                    filtered_rows.append(row)
            rows = filtered_rows
        
        # 显示搜索结果
        for row in rows:
            # 解密密码
            decrypted_password = self.encryption_manager.decrypt_password(row[3])
            self.password_book_tab.add_to_tree((row[0], row[1], row[2], decrypted_password, row[4]))
    
    def view_password(self, values):
        """查看密码详情
        
        Args:
            values: 密码信息
        """
        # 创建查看窗口
        view_window = tk.Toplevel(self.root)
        view_window.title("查看密码详情")
        view_window.geometry("400x300")
        
        # 显示详情
        details_frame = ttk.LabelFrame(view_window, text="密码详情", padding="10")
        details_frame.pack(fill=tk.BOTH, expand=True, pady=10)
        
        ttk.Label(details_frame, text="网站/应用:", font=("Arial", 10, "bold")).grid(row=0, column=0, sticky=tk.W, pady=5, padx=5)
        ttk.Label(details_frame, text=values[1]).grid(row=0, column=1, sticky=tk.W, pady=5, padx=5)
        
        ttk.Label(details_frame, text="账号/用户名:", font=("Arial", 10, "bold")).grid(row=1, column=0, sticky=tk.W, pady=5, padx=5)
        ttk.Label(details_frame, text=values[2]).grid(row=1, column=1, sticky=tk.W, pady=5, padx=5)
        
        ttk.Label(details_frame, text="密码:", font=("Arial", 10, "bold")).grid(row=2, column=0, sticky=tk.W, pady=5, padx=5)
        ttk.Label(details_frame, text=values[3]).grid(row=2, column=1, sticky=tk.W, pady=5, padx=5)
        
        ttk.Label(details_frame, text="备注:", font=("Arial", 10, "bold")).grid(row=3, column=0, sticky=tk.W, pady=5, padx=5)
        ttk.Label(details_frame, text=values[4] if values[4] else "无").grid(row=3, column=1, sticky=tk.W, pady=5, padx=5)
        
        # 关闭按钮
        close_button = ttk.Button(view_window, text="关闭", command=view_window.destroy)
        close_button.pack(pady=10)
    
    def edit_password(self, values):
        """编辑密码
        
        Args:
            values: 密码信息
        """
        # 创建编辑窗口
        edit_window = tk.Toplevel(self.root)
        edit_window.title("编辑密码")
        edit_window.geometry("500x400")
        
        # 表单框架
        form_frame = ttk.LabelFrame(edit_window, text="编辑信息", padding="10")
        form_frame.pack(fill=tk.BOTH, expand=True, pady=10)
        
        # 网站/应用名称
        ttk.Label(form_frame, text="网站/应用名称:").grid(row=0, column=0, sticky=tk.W, pady=5, padx=5)
        edit_website_var = tk.StringVar(value=values[1])
        ttk.Entry(form_frame, textvariable=edit_website_var, width=50).grid(row=0, column=1, pady=5, padx=5)
        
        # 账号/用户名
        ttk.Label(form_frame, text="账号/用户名:").grid(row=1, column=0, sticky=tk.W, pady=5, padx=5)
        edit_username_var = tk.StringVar(value=values[2])
        ttk.Entry(form_frame, textvariable=edit_username_var, width=50).grid(row=1, column=1, pady=5, padx=5)
        
        # 密码
        ttk.Label(form_frame, text="密码:").grid(row=2, column=0, sticky=tk.W, pady=5, padx=5)
        edit_password_var = tk.StringVar(value=values[3])
        ttk.Entry(form_frame, textvariable=edit_password_var, width=50).grid(row=2, column=1, pady=5, padx=5)
        
        # 备注
        ttk.Label(form_frame, text="备注:").grid(row=3, column=0, sticky=tk.NW, pady=5, padx=5)
        edit_note_var = tk.StringVar(value=values[4] if values[4] else "")
        ttk.Entry(form_frame, textvariable=edit_note_var, width=50).grid(row=3, column=1, pady=5, padx=5)
        
        # 保存按钮
        def save_changes():
            website = edit_website_var.get().strip()
            username = edit_username_var.get().strip()
            password = edit_password_var.get().strip()
            note = edit_note_var.get().strip()
            
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
            
            # 加密密码并更新
            encrypted_password = self.encryption_manager.encrypt_password(password)
            self.db_manager.update_password(values[0], website, username, encrypted_password, note)
            
            messagebox.showinfo("成功", "密码更新成功")
            edit_window.destroy()
            self.load_passwords()
        
        save_button = ttk.Button(edit_window, text="保存", command=save_changes)
        save_button.pack(pady=10)
    
    def delete_password(self, values):
        """删除密码
        
        Args:
            values: 密码信息
        """
        if messagebox.askyesno("确认删除", f"确定要删除 {values[1]} 的密码吗？"):
            self.db_manager.delete_password(values[0])
            messagebox.showinfo("成功", "密码删除成功")
            self.load_passwords()
    
    def copy_selected_password(self, values):
        """复制选中的密码
        
        Args:
            values: 密码信息
        """
        # 获取密码并复制到剪贴板
        password = values[3]
        self.root.clipboard_clear()
        self.root.clipboard_append(password)
        messagebox.showinfo("成功", "密码已复制到剪贴板")
    
    def save_to_history(self, password):
        """保存密码到历史记录
        
        Args:
            password (str): 生成的密码
        """
        self.history_manager.save_to_history(password)
    
    def use_password(self, password):
        """使用密码
        
        Args:
            password (str): 生成的密码
        """
        # 复制到剪贴板
        self.root.clipboard_clear()
        self.root.clipboard_append(password)
        
        # 切换到密码录入界面
        self.notebook.select(0)  # 0表示第一个标签页（密码录入）
        
        # 粘贴到密码输入框
        self.input_tab.set_password(password)
        
        # 显示操作成功提示
        messagebox.showinfo("成功", "密码已复制并粘贴到录入界面")
    
    def show_history(self):
        """显示历史记录"""
        # 获取历史记录
        history = self.history_manager.get_history()
        
        # 创建历史记录窗口
        history_window = tk.Toplevel(self.root)
        history_window.title("密码历史记录")
        history_window.geometry("500x400")
        
        # 创建列表框
        list_frame = ttk.LabelFrame(history_window, text="最近生成的密码", padding="10")
        list_frame.pack(fill=tk.BOTH, expand=True, pady=10)
        
        # 创建滚动条
        scrollbar = ttk.Scrollbar(list_frame)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        
        # 创建列表框
        history_list = tk.Listbox(list_frame, yscrollcommand=scrollbar.set, width=60, height=15)
        history_list.pack(fill=tk.BOTH, expand=True, side=tk.LEFT)
        scrollbar.config(command=history_list.yview)
        
        # 填充历史记录
        for i, record in enumerate(history):
            display_text = f"{record['timestamp']} - {record['password']}"
            history_list.insert(tk.END, display_text)
        
        # 复制按钮
        def copy_from_history():
            selected_index = history_list.curselection()
            if not selected_index:
                messagebox.showinfo("提示", "请选择要复制的密码")
                return
            
            index = selected_index[0]
            if index < len(history):
                password = history[index]['password']
                self.root.clipboard_clear()
                self.root.clipboard_append(password)
                messagebox.showinfo("成功", "密码已复制到剪贴板")
        
        copy_button = ttk.Button(history_window, text="复制选中密码", command=copy_from_history)
        copy_button.pack(pady=10)
    
    def exit_program(self):
        """退出程序"""
        if messagebox.askyesno("确认退出", "确定要退出程序吗？"):
            # 关闭数据库连接
            self.db_manager.close()
            self.root.destroy()