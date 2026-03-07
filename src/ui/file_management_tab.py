import tkinter as tk
from tkinter import ttk, messagebox
from src.scripts.data_exchange_ui import DataExchangeUI

class FileManagementTab:
    """文件管理标签页"""
    
    def __init__(self, notebook, db_manager, encryption_manager, load_passwords_callback):
        """初始化文件管理标签页
        
        Args:
            notebook: 标签页容器
            db_manager (DatabaseManager): 数据库管理器
            encryption_manager (EncryptionManager): 加密管理器
            load_passwords_callback: 加载密码的回调函数
        """
        self.tab = ttk.Frame(notebook)
        notebook.add(self.tab, text="文件管理")
        self.db_manager = db_manager
        self.encryption_manager = encryption_manager
        self.load_passwords_callback = load_passwords_callback
        self.data_exchange_ui = DataExchangeUI(None, db_manager, encryption_manager, load_passwords_callback)
        self._create_widgets()
    
    def _create_widgets(self):
        """创建界面组件"""
        # 创建文件管理框架
        file_frame = ttk.LabelFrame(self.tab, text="文件操作", padding="20")
        file_frame.pack(fill=tk.BOTH, expand=True, pady=10)
        
        # 创建按钮框架
        button_frame = ttk.Frame(file_frame)
        button_frame.pack(fill=tk.X, pady=20)
        
        # 导出数据按钮
        export_button = ttk.Button(button_frame, text="导出数据", command=self._show_export_dialog, width=20)
        export_button.pack(side=tk.LEFT, padx=10, pady=10)
        
        # 导入数据按钮
        import_button = ttk.Button(button_frame, text="导入数据", command=self._show_import_dialog, width=20)
        import_button.pack(side=tk.LEFT, padx=10, pady=10)
        
        # 操作说明
        info_frame = ttk.LabelFrame(file_frame, text="操作说明", padding="10")
        info_frame.pack(fill=tk.BOTH, expand=True, pady=10)
        
        info_text = (
            "1. 导出数据：将密码数据导出为JSON、CSV或TXT格式\n"  
            "2. 导入数据：从JSON、CSV或TXT文件导入密码数据\n"  
            "3. 导出的数据会包含所有密码信息，包括已解密的密码\n"  
            "4. 导入数据时请确保文件格式正确，避免数据丢失\n"
        )
        
        info_label = ttk.Label(info_frame, text=info_text, justify=tk.LEFT)
        info_label.pack(fill=tk.BOTH, expand=True)
    
    def _show_export_dialog(self):
        """显示导出对话框"""
        # 创建临时窗口作为父窗口
        temp_window = tk.Toplevel()
        temp_window.withdraw()  # 隐藏窗口
        
        # 保存原始父窗口
        original_parent = self.data_exchange_ui.root
        
        # 设置临时窗口为父窗口
        self.data_exchange_ui.root = temp_window
        
        # 显示导出对话框
        self.data_exchange_ui.show_export_dialog()
        
        # 恢复原始父窗口
        self.data_exchange_ui.root = original_parent
        
        # 销毁临时窗口
        temp_window.destroy()
    
    def _show_import_dialog(self):
        """显示导入对话框"""
        # 创建临时窗口作为父窗口
        temp_window = tk.Toplevel()
        temp_window.withdraw()  # 隐藏窗口
        
        # 保存原始父窗口
        original_parent = self.data_exchange_ui.root
        
        # 设置临时窗口为父窗口
        self.data_exchange_ui.root = temp_window
        
        # 显示导入对话框
        self.data_exchange_ui.show_import_dialog()
        
        # 恢复原始父窗口
        self.data_exchange_ui.root = original_parent
        
        # 销毁临时窗口
        temp_window.destroy()