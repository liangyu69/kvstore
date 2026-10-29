#include "db.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

int main() {
    std::string path = "../data/kvstore_bench";
    system("mkdir -p ../data");
    system("rm -rf ../data/kvstore_bench");

    std::unique_ptr<DB> db;
    if (!DB::Open(path, db).ok()) {
        printf("Failed to open DB\n");
        return 1;
    }

    const int kOps = 1000;
    std::string value(100, 'x');  // 100 字节的 value

    // 1. 测单条写
    {
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < kOps; i++) {
            std::string key = "single_" + std::to_string(i);
            db->Put(Slice(key), Slice(value));
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        printf("Single Put: %d ops in %ld ms (%.2f ops/sec)\n", 
               kOps, ms, kOps * 1000.0 / ms);
    }

    // 2. 测批量写
    {
        std::vector<BatchOp> ops;
        for (int i = 0; i < kOps; i++) {
            std::string key = "batch_" + std::to_string(i);
            ops.push_back({OpType::kPut, key, value});
        }

        auto start = std::chrono::high_resolution_clock::now();
        db->WriteBatch(ops);
        auto end = std::chrono::high_resolution_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        printf("Batch Put: %d ops in %ld ms (%.2f ops/sec)\n", 
               kOps, ms, kOps * 1000.0 / ms);
    }

    // 3. 测随机读
    {
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < kOps; i++) {
            std::string key = "batch_" + std::to_string(i);
            db->Get(Slice(key));
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        printf("Random Get: %d ops in %ld ms (%.2f ops/sec)\n", 
               kOps, ms, kOps * 1000.0 / ms);
    }

    return 0;
}