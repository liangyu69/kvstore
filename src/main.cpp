#include "db.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <cstdlib>

int main() {
    std::string path = "/tmp/kvstore_flush_test";
    system("rm -rf /tmp/kvstore_flush_test");

    printf("--- 第一次运行：写入 ---\n");
    {
        std::unique_ptr<DB> db;
        assert(DB::Open(path, db).ok());
        for (int i = 0; i < 20; i++) {
            std::string k = "key_" + std::to_string(i);
            std::string v = "value_" + std::to_string(i);
            assert(db->Put(Slice(k), Slice(v)).ok());
        }
    } // DB 析构，模拟进程退出

    printf("--- 第二次运行：重启 ---\n");
    {
        std::unique_ptr<DB> db;
        assert(DB::Open(path, db).ok());
        for (int i = 0; i < 20; i++) {
            std::string k = "key_" + std::to_string(i);
            std::string expected_v = "value_" + std::to_string(i);
            
            auto v = db->Get(Slice(k));
            if (!v.has_value() || *v != expected_v) {
                printf("ERROR: 读取失败 key=%s, 期望=%s, 实际=%s\n", 
                       k.c_str(), expected_v.c_str(), v.has_value() ? v->c_str() : "nullopt");
                return 1;
            }
        }
    }

    printf("Restart test passed! SSTable loaded successfully.\n");
    return 0;
}