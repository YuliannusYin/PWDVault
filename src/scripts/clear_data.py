import os
import shutil
from config import Config

# 获取应用数据目录
app_data_dir = Config.get_app_data_dir()

# 要删除的文件
files_to_delete = [
    Config.get_db_path(),
    Config.get_key_path(),
    Config.get_history_path()
]

# 删除文件
for file_path in files_to_delete:
    if file_path.exists():
        print(f"删除文件: {file_path}")
        file_path.unlink()
    else:
        print(f"文件不存在: {file_path}")

# 可选：删除整个应用数据目录
print(f"\n是否删除整个应用数据目录 ({app_data_dir})？")
print("注意：这将删除所有数据，包括可能的其他文件。")
confirm = input("输入 'yes' 确认删除，或按其他键跳过: ")

if confirm.lower() == 'yes':
    if app_data_dir.exists():
        print(f"删除目录: {app_data_dir}")
        shutil.rmtree(app_data_dir)
        print("目录已删除")
    else:
        print("目录不存在")
else:
    print("跳过删除目录")

print("\n数据清除完成！")
