#!/usr/bin/env python3
"""
测试数据导入导出功能
"""

import json
import os
import tempfile
from src.core.database import DatabaseManager
from src.core.encryption import EncryptionManager
from src.scripts.data_exchange import DataExchange


def test_export_import():
    """测试导出导入功能"""
    print("测试数据导入导出功能...")
    
    try:
        # 初始化管理器
        db_manager = DatabaseManager()
        encryption_manager = EncryptionManager()
        data_exchange = DataExchange(db_manager, encryption_manager)
        
        # 清空测试数据
        try:
            db_manager.cursor.execute('DELETE FROM passwords')
            db_manager.conn.commit()
        except Exception as e:
            print(f"清空数据库时出错: {e}")
        
        # 添加测试数据
        test_data = [
            {
                'website': 'test1.com',
                'username': 'user1',
                'password': 'password123',
                'note': '测试账号1',
                'number': 1,
                'sensitivity': 0,
                'related_info': '测试关联信息1'
            },
            {
                'website': 'test2.com',
                'username': 'user2',
                'password': 'password456',
                'note': '测试账号2',
                'number': 2,
                'sensitivity': 1,
                'related_info': '测试关联信息2'
            }
        ]
        
        for item in test_data:
            encrypted_password = encryption_manager.encrypt_password(item['password'])
            encrypted_related_info = encryption_manager.encrypt_related_info(item['related_info'])
            db_manager.add_password(
                website=item['website'],
                username=item['username'],
                password=encrypted_password,
                note=item['note'],
                number=item['number'],
                sensitivity=item['sensitivity'],
                related_info=encrypted_related_info
            )
        
        print("添加测试数据完成")
        
        # 测试JSON格式导出
        with tempfile.NamedTemporaryFile(suffix='.json', delete=False) as temp_file:
            json_path = temp_file.name
        
        success = data_exchange.export_data('json', json_path)
        print(f"JSON导出结果: {success}")
        
        if success:
            # 读取导出的JSON文件
            with open(json_path, 'r', encoding='utf-8') as f:
                exported_data = json.load(f)
            print(f"导出的JSON数据数量: {len(exported_data)}")
        
        # 测试CSV格式导出
        with tempfile.NamedTemporaryFile(suffix='.csv', delete=False) as temp_file:
            csv_path = temp_file.name
        
        success = data_exchange.export_data('csv', csv_path)
        print(f"CSV导出结果: {success}")
        
        # 测试TXT格式导出
        with tempfile.NamedTemporaryFile(suffix='.txt', delete=False) as temp_file:
            txt_path = temp_file.name
        
        success = data_exchange.export_data('txt', txt_path)
        print(f"TXT导出结果: {success}")
        
        # 清空数据库
        try:
            db_manager.cursor.execute('DELETE FROM passwords')
            db_manager.conn.commit()
            print("清空数据库")
        except Exception as e:
            print(f"清空数据库时出错: {e}")
        
        # 测试JSON格式导入
        success, count, errors = data_exchange.import_data(json_path, 'json')
        print(f"JSON导入结果: 成功={success}, 导入数量={count}, 错误={errors}")
        
        # 清空数据库
        try:
            db_manager.cursor.execute('DELETE FROM passwords')
            db_manager.conn.commit()
        except Exception as e:
            print(f"清空数据库时出错: {e}")
        
        # 测试CSV格式导入
        success, count, errors = data_exchange.import_data(csv_path, 'csv')
        print(f"CSV导入结果: 成功={success}, 导入数量={count}, 错误={errors}")
        
        # 清空数据库
        try:
            db_manager.cursor.execute('DELETE FROM passwords')
            db_manager.conn.commit()
        except Exception as e:
            print(f"清空数据库时出错: {e}")
        
        # 测试TXT格式导入
        success, count, errors = data_exchange.import_data(txt_path, 'txt')
        print(f"TXT导入结果: 成功={success}, 导入数量={count}, 错误={errors}")
        
        # 清理临时文件
        try:
            os.unlink(json_path)
            os.unlink(csv_path)
            os.unlink(txt_path)
        except Exception as e:
            print(f"清理临时文件时出错: {e}")
        
        # 关闭数据库连接
        db_manager.close()
        
        print("测试完成")
    except Exception as e:
        print(f"测试过程中出错: {e}")
        import traceback
        traceback.print_exc()


if __name__ == "__main__":
    test_export_import()