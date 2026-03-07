import sqlite3
from src.core.config import Config

class DatabaseManager:
    """数据库管理类"""
    
    def __init__(self):
        """初始化数据库管理器"""
        self.use_memory_db = False
        try:
            db_path = Config.get_db_path()
            # 确保目录存在
            db_path.parent.mkdir(parents=True, exist_ok=True)
            self.conn = sqlite3.connect(str(db_path))
            self.cursor = self.conn.cursor()
            self._create_table()
        except Exception as e:
            print(f"数据库初始化错误: {e}")
            # 创建内存数据库作为 fallback
            self.use_memory_db = True
            self.conn = sqlite3.connect(':memory:')
            self.cursor = self.conn.cursor()
            self._create_table()
            print("使用内存数据库作为 fallback")
    
    def _create_table(self):
        """创建密码表"""
        self.cursor.execute('''
            CREATE TABLE IF NOT EXISTS passwords (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                number INTEGER,
                website TEXT NOT NULL,
                username TEXT NOT NULL,
                password TEXT NOT NULL,
                note TEXT,
                sensitivity INTEGER DEFAULT 0,
                related_info TEXT
            )
        ''')
        self.conn.commit()
        # 升级表结构（兼容旧版本）
        self._upgrade_table()
    
    def _upgrade_table(self):
        """升级表结构，添加新字段"""
        # 检查并添加新字段
        self.cursor.execute("PRAGMA table_info(passwords)")
        columns = [column[1] for column in self.cursor.fetchall()]
        
        if 'number' not in columns:
            self.cursor.execute("ALTER TABLE passwords ADD COLUMN number INTEGER")
        
        if 'sensitivity' not in columns:
            self.cursor.execute("ALTER TABLE passwords ADD COLUMN sensitivity INTEGER DEFAULT 0")
        
        if 'related_info' not in columns:
            self.cursor.execute("ALTER TABLE passwords ADD COLUMN related_info TEXT")
        
        self.conn.commit()
    
    def add_password(self, website, username, password, note, number=None, sensitivity=0, related_info=None):
        """添加密码
        
        Args:
            website (str): 网站/应用名称
            username (str): 账号/用户名
            password (str): 加密后的密码
            note (str): 备注
            number (int): 编号
            sensitivity (int): 敏感性（0或1）
            related_info (str): 关联信息
        """
        self.cursor.execute('''
            INSERT INTO passwords (number, website, username, password, note, sensitivity, related_info)
            VALUES (?, ?, ?, ?, ?, ?, ?)
        ''', (number, website, username, password, note, sensitivity, related_info))
        self.conn.commit()
    
    def get_all_passwords(self):
        """获取所有密码
        
        Returns:
            list: 密码列表
        """
        self.cursor.execute('SELECT * FROM passwords ORDER BY number ASC NULLS LAST')
        return self.cursor.fetchall()
    
    def update_password(self, password_id, website, username, password, note, number=None, sensitivity=0, related_info=None):
        """更新密码
        
        Args:
            password_id (int): 密码ID
            website (str): 网站/应用名称
            username (str): 账号/用户名
            password (str): 加密后的密码
            note (str): 备注
            number (int): 编号
            sensitivity (int): 敏感性（0或1）
            related_info (str): 关联信息
        """
        self.cursor.execute('''
            UPDATE passwords
            SET number=?, website=?, username=?, password=?, note=?, sensitivity=?, related_info=?
            WHERE id=?
        ''', (number, website, username, password, note, sensitivity, related_info, password_id))
        self.conn.commit()
    
    def delete_password(self, password_id):
        """删除密码
        
        Args:
            password_id (int): 密码ID
        """
        self.cursor.execute('DELETE FROM passwords WHERE id=?', (password_id,))
        self.conn.commit()
    
    def search_passwords(self, search_term, search_website=True, search_username=False, search_note=False, search_number=False, search_related_info=False):
        """搜索密码
        
        Args:
            search_term (str): 搜索关键词
            search_website (bool): 是否搜索网站/应用
            search_username (bool): 是否搜索账号/用户名
            search_note (bool): 是否搜索备注
            search_number (bool): 是否搜索编号
            search_related_info (bool): 是否搜索关联信息
            
        Returns:
            list: 搜索结果
        """
        conditions = []
        params = []
        
        if search_term:
            if search_website:
                conditions.append('website LIKE ?')
                params.append('%' + search_term + '%')
            if search_username:
                conditions.append('username LIKE ?')
                params.append('%' + search_term + '%')
            if search_note:
                conditions.append('note LIKE ?')
                params.append('%' + search_term + '%')
            if search_number:
                conditions.append('number LIKE ?')
                params.append('%' + search_term + '%')
            if search_related_info:
                conditions.append('related_info LIKE ?')
                params.append('%' + search_term + '%')
        
        if conditions:
            query = 'SELECT * FROM passwords WHERE ' + ' OR '.join(conditions) + ' ORDER BY number ASC NULLS LAST'
            self.cursor.execute(query, params)
        else:
            self.cursor.execute('SELECT * FROM passwords ORDER BY number ASC NULLS LAST')
        
        return self.cursor.fetchall()
    
    def close(self):
        """关闭数据库连接"""
        self.conn.close()