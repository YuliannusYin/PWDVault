import time
import psutil
from src.core.cache import CacheManager

class CachePerformanceTest:
    """缓存性能测试类"""
    
    def __init__(self):
        """初始化测试类"""
        self.cache = CacheManager(max_size=100, default_ttl=3600)
    
    def test_cache_speed(self):
        """测试缓存速度"""
        print("=== 测试缓存速度 ===")
        
        # 准备测试数据
        test_data = [
            (1, 1, "test_website_1", "user1", "password123", "note1", 0, "related1"),
            (2, 2, "test_website_2", "user2", "password456", "note2", 0, "related2"),
            (3, 3, "test_website_3", "user3", "password789", "note3", 0, "related3"),
        ]
        
        # 模拟无缓存情况下的操作（模拟数据库查询和解密）
        print("\n1. 模拟无缓存情况下的操作时间：")
        start_time = time.time()
        for i in range(1000):
            # 模拟数据库查询
            rows = test_data.copy()
            # 模拟解密操作
            for row in rows:
                # 模拟密码解密
                password = row[4]
                # 模拟关联信息解密
                related_info = row[7]
        end_time = time.time()
        no_cache_time = end_time - start_time
        print(f"模拟无缓存执行1000次操作耗时: {no_cache_time:.4f}秒")
        
        # 测试有缓存情况下的查询时间
        print("\n2. 有缓存情况下的操作时间：")
        # 第一次操作，缓存未命中
        self.cache.set('all_passwords', test_data)
        
        start_time = time.time()
        for i in range(1000):
            # 从缓存获取
            cached_data = self.cache.get('all_passwords')
        end_time = time.time()
        with_cache_time = end_time - start_time
        print(f"有缓存执行1000次操作耗时: {with_cache_time:.4f}秒")
        
        # 计算性能提升
        speedup = no_cache_time / with_cache_time if with_cache_time > 0 else float('inf')
        print(f"\n性能提升: {speedup:.2f}倍")
    
    def test_cache_memory_usage(self):
        """测试缓存内存使用"""
        print("\n=== 测试缓存内存使用 ===")
        
        # 获取初始内存使用
        process = psutil.Process()
        initial_memory = process.memory_info().rss / 1024 / 1024  # MB
        print(f"初始内存使用: {initial_memory:.2f} MB")
        
        # 填充缓存
        test_data = []
        for i in range(100):
            data = f"test_data_{i}" * 1000  # 生成较大的数据
            test_data.append(data)
            self.cache.set(f"key_{i}", data)
        
        # 获取填充后的内存使用
        after_fill_memory = process.memory_info().rss / 1024 / 1024  # MB
        print(f"填充100条数据后内存使用: {after_fill_memory:.2f} MB")
        print(f"缓存占用内存: {after_fill_memory - initial_memory:.2f} MB")
        
        # 测试缓存清理
        self.cache.clear()
        after_clear_memory = process.memory_info().rss / 1024 / 1024  # MB
        print(f"清空缓存后内存使用: {after_clear_memory:.2f} MB")
        print(f"释放内存: {after_fill_memory - after_clear_memory:.2f} MB")
    
    def test_cache_hit_rate(self):
        """测试缓存命中率"""
        print("\n=== 测试缓存命中率 ===")
        
        # 填充缓存
        for i in range(50):
            self.cache.set(f"key_{i}", f"value_{i}")
        
        # 测试命中情况
        hits = 0
        misses = 0
        
        # 访问已缓存的键
        for i in range(50):
            if self.cache.get(f"key_{i}") is not None:
                hits += 1
            else:
                misses += 1
        
        # 访问未缓存的键
        for i in range(50, 100):
            if self.cache.get(f"key_{i}") is not None:
                hits += 1
            else:
                misses += 1
        
        # 获取缓存统计
        stats = self.cache.get_stats()
        print(f"缓存统计: {stats}")
        print(f"预期命中率: {hits / (hits + misses) * 100:.2f}%")
    
    def test_cache_expiry(self):
        """测试缓存过期机制"""
        print("\n=== 测试缓存过期机制 ===")
        
        # 设置一个1秒过期的缓存
        self.cache.set("temp_key", "temp_value", ttl=1)
        print(f"设置缓存后: {self.cache.get('temp_key')}")
        
        # 等待2秒
        time.sleep(2)
        print(f"2秒后: {self.cache.get('temp_key')}")
        
        # 测试过期项清理
        expired_count = self.cache.remove_expired()
        print(f"清理过期项数量: {expired_count}")
    
    def run_all_tests(self):
        """运行所有测试"""
        print("开始缓存性能测试...")
        print("=" * 60)
        
        self.test_cache_speed()
        self.test_cache_memory_usage()
        self.test_cache_hit_rate()
        self.test_cache_expiry()
        
        print("=" * 60)
        print("缓存性能测试完成！")

if __name__ == "__main__":
    test = CachePerformanceTest()
    test.run_all_tests()