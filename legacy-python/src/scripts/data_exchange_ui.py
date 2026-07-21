import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from src.scripts.data_exchange import DataExchange

class DataExchangeUI:
    """数据导入导出界面类"""
    
    def __init__(self, root, db_manager, encryption_manager, load_passwords_callback):
        """初始化数据导入导出界面
        
        Args:
            root: 根窗口
            db_manager (DatabaseManager): 数据库管理器
            encryption_manager (EncryptionManager): 加密管理器
            load_passwords_callback: 加载密码的回调函数
        """
        self.root = root
        self.db_manager = db_manager
        self.encryption_manager = encryption_manager
        self.load_passwords_callback = load_passwords_callback
        self.data_exchange = DataExchange(db_manager, encryption_manager)
    
    def show_export_dialog(self):
        """显示导出对话框"""
        # 创建导出窗口
        export_window = tk.Toplevel(self.root)
        export_window.title("导出数据")
        export_window.geometry("500x300")
        export_window.resizable(False, False)
        
        # 创建框架
        frame = ttk.LabelFrame(export_window, text="导出设置", padding="20")
        frame.pack(fill=tk.BOTH, expand=True, pady=10)
        
        # 格式选择
        ttk.Label(frame, text="导出格式:").grid(row=0, column=0, sticky=tk.W, pady=10, padx=5)
        format_var = tk.StringVar(value="json")
        format_frame = ttk.Frame(frame)
        format_frame.grid(row=0, column=1, sticky=tk.W, pady=10, padx=5)
        ttk.Radiobutton(format_frame, text="JSON", variable=format_var, value="json").pack(side=tk.LEFT, padx=10)
        ttk.Radiobutton(format_frame, text="CSV", variable=format_var, value="csv").pack(side=tk.LEFT, padx=10)
        ttk.Radiobutton(format_frame, text="TXT", variable=format_var, value="txt").pack(side=tk.LEFT, padx=10)
        
        # 文件路径选择
        ttk.Label(frame, text="导出路径:").grid(row=1, column=0, sticky=tk.W, pady=10, padx=5)
        path_var = tk.StringVar()
        path_entry = ttk.Entry(frame, textvariable=path_var, width=40)
        path_entry.grid(row=1, column=1, sticky=tk.W, pady=10, padx=5)
        
        def browse_path():
            """浏览文件路径"""
            file_types = []
            if format_var.get() == "json":
                file_types = [("JSON文件", "*.json"), ("所有文件", "*.*")]
            elif format_var.get() == "csv":
                file_types = [("CSV文件", "*.csv"), ("所有文件", "*.*")]
            elif format_var.get() == "txt":
                file_types = [("文本文件", "*.txt"), ("所有文件", "*.*")]
            
            file_path = filedialog.asksaveasfilename(
                defaultextension=f".{format_var.get()}",
                filetypes=file_types,
                title="选择导出文件"
            )
            if file_path:
                path_var.set(file_path)
        
        browse_button = ttk.Button(frame, text="浏览", command=browse_path)
        browse_button.grid(row=1, column=2, pady=10, padx=5)
        
        # 导出按钮
        def export_data():
            """导出数据"""
            export_format = format_var.get()
            export_path = path_var.get()
            
            if not export_path:
                messagebox.showerror("错误", "请选择导出文件路径")
                return
            
            # 显示进度条
            progress_window = tk.Toplevel(export_window)
            progress_window.title("导出进度")
            progress_window.geometry("300x100")
            progress_window.resizable(False, False)
            
            progress_frame = ttk.Frame(progress_window, padding="20")
            progress_frame.pack(fill=tk.BOTH, expand=True)
            
            ttk.Label(progress_frame, text="正在导出数据...").pack(pady=10)
            progress_bar = ttk.Progressbar(progress_frame, length=200, mode='indeterminate')
            progress_bar.pack(pady=10)
            progress_bar.start()
            
            # 执行导出
            export_window.update()
            success, error_msg = self.data_exchange.export_data(export_format, export_path)
            
            # 停止进度条
            progress_bar.stop()
            progress_window.destroy()
            
            if success:
                messagebox.showinfo("成功", f"数据已成功导出到\n{export_path}")
                export_window.destroy()
            else:
                messagebox.showerror("错误", f"导出数据失败: {error_msg}")
        
        export_button = ttk.Button(export_window, text="导出", command=export_data, width=15)
        export_button.pack(pady=20)
    
    def show_import_dialog(self):
        """显示导入对话框"""
        # 创建导入窗口
        import_window = tk.Toplevel(self.root)
        import_window.title("导入数据")
        import_window.geometry("500x350")
        import_window.resizable(False, False)
        
        # 创建框架
        frame = ttk.LabelFrame(import_window, text="导入设置", padding="20")
        frame.pack(fill=tk.BOTH, expand=True, pady=10)
        
        # 格式选择
        ttk.Label(frame, text="导入格式:").grid(row=0, column=0, sticky=tk.W, pady=10, padx=5)
        format_var = tk.StringVar(value="json")
        format_frame = ttk.Frame(frame)
        format_frame.grid(row=0, column=1, sticky=tk.W, pady=10, padx=5)
        ttk.Radiobutton(format_frame, text="JSON", variable=format_var, value="json").pack(side=tk.LEFT, padx=10)
        ttk.Radiobutton(format_frame, text="CSV", variable=format_var, value="csv").pack(side=tk.LEFT, padx=10)
        ttk.Radiobutton(format_frame, text="TXT", variable=format_var, value="txt").pack(side=tk.LEFT, padx=10)
        
        # 文件路径选择
        ttk.Label(frame, text="导入文件:").grid(row=1, column=0, sticky=tk.W, pady=10, padx=5)
        path_var = tk.StringVar()
        path_entry = ttk.Entry(frame, textvariable=path_var, width=40)
        path_entry.grid(row=1, column=1, sticky=tk.W, pady=10, padx=5)
        
        def browse_file():
            """浏览文件"""
            file_types = []
            if format_var.get() == "json":
                file_types = [("JSON文件", "*.json"), ("所有文件", "*.*")]
            elif format_var.get() == "csv":
                file_types = [("CSV文件", "*.csv"), ("所有文件", "*.*")]
            elif format_var.get() == "txt":
                file_types = [("文本文件", "*.txt"), ("所有文件", "*.*")]
            
            file_path = filedialog.askopenfilename(
                filetypes=file_types,
                title="选择导入文件"
            )
            if file_path:
                path_var.set(file_path)
        
        browse_button = ttk.Button(frame, text="浏览", command=browse_file)
        browse_button.grid(row=1, column=2, pady=10, padx=5)
        
        # 覆盖选项
        overwrite_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(frame, text="覆盖现有数据", variable=overwrite_var).grid(row=2, column=1, sticky=tk.W, pady=10, padx=5)
        
        # 导入按钮
        def import_data():
            """导入数据"""
            import_format = format_var.get()
            import_path = path_var.get()
            overwrite = overwrite_var.get()
            
            if not import_path:
                messagebox.showerror("错误", "请选择导入文件")
                return
            
            # 确认覆盖操作
            if overwrite:
                if not messagebox.askyesno("确认覆盖", "确定要覆盖现有所有数据吗？此操作不可恢复。"):
                    return
            
            # 显示进度条
            progress_window = tk.Toplevel(import_window)
            progress_window.title("导入进度")
            progress_window.geometry("300x100")
            progress_window.resizable(False, False)
            
            progress_frame = ttk.Frame(progress_window, padding="20")
            progress_frame.pack(fill=tk.BOTH, expand=True)
            
            ttk.Label(progress_frame, text="正在导入数据...").pack(pady=10)
            progress_bar = ttk.Progressbar(progress_frame, length=200, mode='indeterminate')
            progress_bar.pack(pady=10)
            progress_bar.start()
            
            # 执行导入
            import_window.update()
            success, count, errors = self.data_exchange.import_data(import_path, import_format, overwrite)
            
            # 停止进度条
            progress_bar.stop()
            progress_window.destroy()
            
            if success:
                # 重新加载密码
                self.load_passwords_callback()
                
                # 显示导入结果
                result_message = f"成功导入 {count} 条数据"
                if errors and errors != "无错误":
                    result_message += f"\n\n错误信息:\n{errors}"
                messagebox.showinfo("导入结果", result_message)
                import_window.destroy()
            else:
                messagebox.showerror("错误", f"导入数据失败:\n{errors}")
        
        import_button = ttk.Button(import_window, text="导入", command=import_data, width=15)
        import_button.pack(pady=20)