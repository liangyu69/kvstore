#include "db.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <cstdlib>
#include <thread>
#include <vector>

int main() {
    std::string path = "../data/kvstore_main_test";
    system("mkdir -p ../data");
    system("rm -rf ../data/kvstore_main_test");

    printf("=== 1. 基础读写与覆盖测试 ===\n");
    {
        std::unique_ptr<DB> db;
        assert(DB::Open(path, db).ok());

        // 写入
        assert(db->Put(Slice("name"), Slice("Alice")).ok());
        assert(db->Put(Slice("age"), Slice("20")).ok());
        assert(db->Get(Slice("name")).value() == "Alice");
        assert(db->Get(Slice("age")).value() == "20");

        // 覆盖
        assert(db->Put(Slice("name"), Slice("Bob")).ok());
        assert(db->Get(Slice("name")).value() == "Bob");

        // 删除
        assert(db->Delete(Slice("age")).ok());
        assert(!db->Get(Slice("age")).has_value());

        printf("基础读写 OK\n");
    }

    printf("=== 2. 重启恢复测试 ===\n");
    {
        // 重新打开，验证数据还在，且删除操作没有复活
        std::unique_ptr<DB> db;
        assert(DB::Open(path, db).ok());

        assert(db->Get(Slice("name")).value() == "Bob");
        assert(!db->Get(Slice("age")).has_value());

        printf("重启恢复 OK\n");
    }

    printf("=== 3. 并发与批量写测试 ===\n");
    {
        std::unique_ptr<DB> db;
        assert(DB::Open(path, db).ok());

        const int kThreads = 4;
        const int kOps = 100;

        std::vector<std::thread> threads;
        for (int t = 0; t < kThreads; t++) {
            threads.emplace_back([&, t]() {
                // 一半单条写，一半批量写
                for (int i = 0; i < kOps; i += 2) {
                    std::string k1 = "key_" + std::to_string(t) + "_" + std::to_string(i);
                    std::string v1 = "val_" + std::to_string(t) + "_" + std::to_string(i);
                    assert(db->Put(Slice(k1), Slice(v1)).ok());
                }

                std::vector<BatchOp> ops;
                for (int i = 1; i < kOps; i += 2) {
                    std::string k2 = "key_" + std::to_string(t) + "_" + std::to_string(i);
                    std::string v2 = "val_" + std::to_string(t) + "_" + std::to_string(i);
                    ops.push_back({OpType::kPut, k2, v2});
                }
                assert(db->WriteBatch(ops).ok());
            });
        }
        for (auto& th : threads) th.join();

        // 验证并发写入的数据是否完整
        for (int t = 0; t < kThreads; t++) {
            for (int i = 0; i < kOps; i++) {
                std::string k = "key_" + std::to_string(t) + "_" + std::to_string(i);
                std::string expected = "val_" + std::to_string(t) + "_" + std::to_string(i);
                auto v = db->Get(Slice(k));
                assert(v.has_value() && *v == expected);
            }
        }
        printf("并发与批量写 OK\n");
    }

    printf("\nAll functional tests passed!\n");
    return 0;
}