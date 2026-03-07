import json
import csv
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from src.core.database import DatabaseManager
from src.core.encryption import EncryptionManager

class DataExchange:
    """数据导入导出类"""
    
    def __init__(self, db_manager, encryption_manager):
        """初始化数据导入导出管理器
        
        Args:
            db_manager (DatabaseManager): 数据库管理器
            encryption_manager (EncryptionManager): 加密管理器
        """
        self.db_manager = db_manager
        self.encryption_manager = encryption_manager
    
    def export_data(self, export_format, export_path):
        """导出数据
        
        Args:
            export_format (str): 导出格式 (json, csv, txt)
            export_path (str): 导出文件路径
        
        Returns:
            bool: 导出是否成功
        """
        try:
            # 获取所有密码数据
            rows = self.db_manager.get_all_passwords()
            
            # 准备导出数据
            export_data = []
            for row in rows:
                if len(row) >= 8:
                    # 解密密码和关联信息
                    decrypted_password = self.encryption_manager.decrypt_password(row[4])
                    decrypted_related_info = self.encryption_manager.decrypt_related_info(row[7]) if row[7] else None
                    
                    # 构造导出数据
                    export_data.append({
                        'id': row[0],
                        'number': row[1],
                        'website': row[2],
                        'username': row[3],
                        'password': decrypted_password,
                        'note': row[5],
                        'sensitivity': row[6],
                        'related_info': decrypted_related_info
                    })
            
            # 根据格式导出
            if export_format == 'json':
                with open(export_path, 'w', encoding='utf-8') as f:
                    json.dump(export_data, f, ensure_ascii=False, indent=2)
            
            elif export_format == 'csv':
                with open(export_path, 'w', newline='', encoding='utf-8') as f:
                    writer = csv.DictWriter(f, fieldnames=['id', 'number', 'website', 'username', 'password', 'note', 'sensitivity', 'related_info'])
                    writer.writeheader()
                    writer.writerows(export_data)
            
            elif export_format == 'txt':
                with open(export_path, 'w', encoding='utf-8') as f:
                    for item in export_data:
                        f.write(f"ID: {item['id']}\n")
                        f.write(f"编号: {item['number']}\n")
                        f.write(f"网站/应用: {item['website']}\n")
                        f.write(f"账号/用户名: {item['username']}\n")
                        f.write(f"密码: {item['password']}\n")
                        f.write(f"备注: {item['note']}\n")
                        f.write(f"敏感性: {'敏感' if item['sensitivity'] == 1 else '普通'}\n")
                        f.write(f"关联信息: {item['related_info']}\n")
                        f.write("-" * 50 + "\n")
            
            return True
        except Exception as e:
            print(f"导出数据错误: {e}")
            return False
    
    def import_data(self, import_path, import_format, overwrite=False):
        """导入数据
        
        Args:
            import_path (str): 导入文件路径
            import_format (str): 导入格式 (json, csv, txt)
            overwrite (bool): 是否覆盖现有数据
        
        Returns:
            tuple: (是否成功, 导入数量, 错误信息)
        """
        try:
            import_data = []
            
            # 根据格式导入
            if import_format == 'json':
                with open(import_path, 'r', encoding='utf-8') as f:
                    import_data = json.load(f)
            
            elif import_format == 'csv':
                with open(import_path, 'r', newline='', encoding='utf-8') as f:
                    reader = csv.DictReader(f)
                    for row in reader:
                        # 转换数据类型
                        item = {
                            'id': int(row['id']) if row['id'] else None,
                            'number': int(row['number']) if row['number'] else None,
                            'website': row['website'],
                            'username': row['username'],
                            'password': row['password'],
                            'note': row['note'],
                            'sensitivity': int(row['sensitivity']) if row['sensitivity'] else 0,
                            'related_info': row['related_info'] if row['related_info'] else None
                        }
                        import_data.append(item)
            
            elif import_format == 'txt':
                with open(import_path, 'r', encoding='utf-8') as f:
                    lines = f.readlines()
                    item = {}
                    for line in lines:
                        line = line.strip()
                        if line.startswith('ID:'):
                            if item:
                                import_data.append(item)
                                item = {}
                            item['id'] = int(line.split(':', 1)[1].strip())
                        elif line.startswith('编号:'):
                            value = line.split(':', 1)[1].strip()
                            item['number'] = int(value) if value else None
                        elif line.startswith('网站/应用:'):
                            item['website'] = line.split(':', 1)[1].strip()
                        elif line.startswith('账号/用户名:'):
                            item['username'] = line.split(':', 1)[1].strip()
                        elif line.startswith('密码:'):
                            item['password'] = line.split(':', 1)[1].strip()
                        elif line.startswith('备注:'):
                            item['note'] = line.split(':', 1)[1].strip()
                        elif line.startswith('敏感性:'):
                            value = line.split(':', 1)[1].strip()
                            item['sensitivity'] = 1 if value == '敏感' else 0
                        elif line.startswith('关联信息:'):
                            item['related_info'] = line.split(':', 1)[1].strip()
                    if item:
                        import_data.append(item)
            
            # 数据校验
            valid_data = []
            errors = []
            
            for i, item in enumerate(import_data):
                # 检查必要字段
                if not item.get('website') or not item.get('username') or not item.get('password'):
                    errors.append(f"第{i+1}条数据缺少必要字段")
                    continue
                
                # 检查数据类型
                if item.get('sensitivity') not in [0, 1]:
                    errors.append(f"第{i+1}条数据敏感性值无效")
                    continue
                
                valid_data.append(item)
            
            # 处理数据导入
            if valid_data:
                if overwrite:
                    # 清空现有数据
                    self.db_manager.cursor.execute('DELETE FROM passwords')
                    self.db_manager.conn.commit()
                
                # 导入数据
                for item in valid_data:
                    # 加密密码和关联信息
                    encrypted_password = self.encryption_manager.encrypt_password(item['password'])
                    encrypted_related_info = self.encryption_manager.encrypt_related_info(item['related_info'])
                    
                    # 插入数据（忽略ID，使用自增）
                    self.db_manager.add_password(
                        website=item['website'],
                        username=item['username'],
                        password=encrypted_password,
                        note=item['note'],
                        number=item['number'],
                        sensitivity=item['sensitivity'],
                        related_info=encrypted_related_info
                    )
            
            return True, len(valid_data), '\n'.join(errors) if errors else '无错误'
        except Exception as e:
            print(f"导入数据错误: {e}")
            return False, 0, str(e)