import sqlite3
from config import Config

class DatabaseManager:
    """数据库管理类"""
    
    def __init__(self):
        """初始化数据库管理器"""
        db_path = Config.get_db_path()
        self.conn = sqlite3.connect(str(db_path))
        self.cursor = self.conn.cursor()
        self._create_table()
    
    def _create_table(self):
        """创建密码表"""
        self.cursor.execute('''
            CREATE TABLE IF NOT EXISTS passwords (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                website TEXT NOT NULL,
                username TEXT NOT NULL,
                password TEXT NOT NULL,
                note TEXT
            )
        ''')
        self.conn.commit()
    
    def add_password(self, website, username, password, note):
        """添加密码
        
        Args:
            website (str): 网站/应用名称
            username (str): 账号/用户名
            password (str): 加密后的密码
            note (str): 备注
        """
        self.cursor.execute('''
            INSERT INTO passwords (website, username, password, note)
            VALUES (?, ?, ?, ?)
        ''', (website, username, password, note))
        self.conn.commit()
    
    def get_all_passwords(self):
        """获取所有密码
        
        Returns:
            list: 密码列表
        """
        self.cursor.execute('SELECT * FROM passwords')
        return self.cursor.fetchall()
    
    def update_password(self, password_id, website, username, password, note):
        """更新密码
        
        Args:
            password_id (int): 密码ID
            website (str): 网站/应用名称
            username (str): 账号/用户名
            password (str): 加密后的密码
            note (str): 备注
        """
        self.cursor.execute('''
            UPDATE passwords
            SET website=?, username=?, password=?, note=?
            WHERE id=?
        ''', (website, username, password, note, password_id))
        self.conn.commit()
    
    def delete_password(self, password_id):
        """删除密码
        
        Args:
            password_id (int): 密码ID
        """
        self.cursor.execute('DELETE FROM passwords WHERE id=?', (password_id,))
        self.conn.commit()
    
    def search_passwords(self, search_term, search_id=False, search_website=True, search_username=False, search_note=False):
        """搜索密码
        
        Args:
            search_term (str): 搜索关键词
            search_id (bool): 是否搜索ID
            search_website (bool): 是否搜索网站/应用
            search_username (bool): 是否搜索账号/用户名
            search_note (bool): 是否搜索备注
            
        Returns:
            list: 搜索结果
        """
        conditions = []
        params = []
        
        if search_term:
            if search_id:
                conditions.append('id LIKE ?')
                params.append('%' + search_term + '%')
            if search_website:
                conditions.append('website LIKE ?')
                params.append('%' + search_term + '%')
            if search_username:
                conditions.append('username LIKE ?')
                params.append('%' + search_term + '%')
            if search_note:
                conditions.append('note LIKE ?')
                params.append('%' + search_term + '%')
        
        if conditions:
            query = 'SELECT * FROM passwords WHERE ' + ' OR '.join(conditions)
            self.cursor.execute(query, params)
        else:
            self.cursor.execute('SELECT * FROM passwords')
        
        return self.cursor.fetchall()
    
    def close(self):
        """关闭数据库连接"""
        self.conn.close()