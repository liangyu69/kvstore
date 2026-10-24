#include "db.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <cstdlib>

int main() {
    std::string path = "../data/kvstore_test";
    system("rm -rf ../data/kvstore_test");

    printf("--- 第一次运行：写入、覆盖、删除 ---\n");
    {
        std::unique_ptr<DB> db;
        assert(DB::Open(path, db).ok());

        // 1. 写入 30 条
        for (int i = 0; i < 30; i++) {
            std::string k = "key_" + std::to_string(i);
            std::string v = "v1_" + std::to_string(i);
            assert(db->Put(Slice(k), Slice(v)).ok());
        }

        // 2. 覆盖其中 10 条
        for (int i = 0; i < 10; i++) {
            std::string k = "key_" + std::to_string(i);
            std::string v = "v2_" + std::to_string(i);
            assert(db->Put(Slice(k), Slice(v)).ok());
        }

        // 3. 删除其中 5 条
        for (int i = 10; i < 15; i++) {
            std::string k = "key_" + std::to_string(i);
            assert(db->Delete(Slice(k)).ok());
        }
    }

    printf("--- 第二次运行：重启，验证 ---\n");
    {
        std::unique_ptr<DB> db;
        assert(DB::Open(path, db).ok());

        // 验证覆盖的（0-9 应该是 v2）
        for (int i = 0; i < 10; i++) {
            std::string k = "key_" + std::to_string(i);
            std::string expected = "v2_" + std::to_string(i);
            auto v = db->Get(Slice(k));
            assert(v.has_value() && *v == expected);
        }

        // 验证删除的（10-14 应该是 nullopt）
        for (int i = 10; i < 15; i++) {
            std::string k = "key_" + std::to_string(i);
            auto v = db->Get(Slice(k));
            assert(!v.has_value());
        }

        // 验证未动的（15-29 应该是 v1）
        for (int i = 15; i < 30; i++) {
            std::string k = "key_" + std::to_string(i);
            std::string expected = "v1_" + std::to_string(i);
            auto v = db->Get(Slice(k));
            assert(v.has_value() && *v == expected);
        }
    }

    printf("Compaction ultimate test passed!\n");
    return 0;
}